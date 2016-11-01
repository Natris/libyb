#include "../sync_runner.hpp"
#include "../../utils/detail/pthread_mutex.hpp"
#include "../../utils/detail/linux_event.hpp"
#include "../../utils/detail/unix_system_error.hpp"
#ifdef __APPLE__
#include "darwin_wait_context.hpp"
#else // __APPLE__
#include "linux_wait_context.hpp"
#endif // __APPLE__
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdexcept>
#include <list>
#include <cassert>
#include <errno.h>
using namespace yb;

struct sync_runner::impl
{
	struct task_entry
	{
		task_wait_memento memento;
		detail::prepared_task * pt;
	};

	detail::pthread_mutex m_mutex;
	detail::linux_event m_update_event;
	pid_t m_associated_thread;
	std::list<task_entry> m_tasks;
};

sync_runner::sync_runner(bool associate_thread_now)
	: m_pimpl(new impl())
{
	m_pimpl->m_associated_thread = associate_thread_now? syscall(SYS_gettid): 0;
}

sync_runner::~sync_runner()
{
	for (std::list<impl::task_entry>::iterator it = m_pimpl->m_tasks.begin(), eit = m_pimpl->m_tasks.end(); it != eit; ++it)
	{
		it->pt->cancel_and_wait();
		it->pt->detach_event_sink();
	}
}

void sync_runner::associate_current_thread()
{
	assert(m_pimpl->m_associated_thread == 0);
	m_pimpl->m_associated_thread = syscall(SYS_gettid);
}

void sync_runner::run_until(detail::prepared_task * focused_pt)
{
	assert(m_pimpl->m_associated_thread == syscall(SYS_gettid));

	task_wait_preparation_context prep_ctx;
	task_wait_preparation_context_impl * prep_ctx_impl = prep_ctx.get();

	task_wait_finalization_context fin_ctx;
	fin_ctx.prep_ctx = &prep_ctx;

	detail::scoped_pthread_lock l(m_pimpl->m_mutex);

	bool done = false;
	while (!done && !m_pimpl->m_tasks.empty())
	{
		prep_ctx.clear();

		prep_ctx_impl->m_pollfds.push_back(m_pimpl->m_update_event.get_poll());

		for (auto it = m_pimpl->m_tasks.begin(); it != m_pimpl->m_tasks.end(); ++it)
		{
			impl::task_entry & pe = *it;
			task_wait_memento_builder mb(prep_ctx);
			pe.pt->prepare_wait(prep_ctx);
			pe.memento = mb.finish();
		}

		if (prep_ctx_impl->m_finished_tasks)
		{
			fin_ctx.selected_poll_item = (size_t)-1;
			fin_ctx.selected_timer_item = (size_t)-1;
			for (std::list<impl::task_entry>::iterator it = m_pimpl->m_tasks.begin(), eit = m_pimpl->m_tasks.end(); it != eit;)
			{
				impl::task_entry & pe = *it;

				if (pe.memento.finished_task_count)
				{
					fin_ctx.finished_tasks = pe.memento.finished_task_count;
					if (pe.pt->finish_wait(fin_ctx))
					{
						if (pe.pt == focused_pt)
							done = true;
						pe.pt->detach_event_sink();
						it = m_pimpl->m_tasks.erase(it);
					}
					else
					{
						++it;
					}
				}
				else
				{
					++it;
				}
			}
		}
		else
		{
			m_pimpl->m_update_event.reset();
			m_pimpl->m_mutex.unlock();

			int timeout = -1;
			auto tit = min_element(prep_ctx_impl->m_timeouts.begin(), prep_ctx_impl->m_timeouts.end());

			int r;
			for (;;)
			{
				if (tit != prep_ctx_impl->m_timeouts.end()) 
				{
					struct timeval to;
					gettimeofday(&to, nullptr);
					uint64_t tnow = (uint64_t)to.tv_sec * 1000 + to.tv_usec / 1000;
					if (tnow > *tit)
					{
						timeout = 0;
					}
					else
					{
						timeout = *tit - tnow;
						if (timeout > INT_MAX)
							timeout = INT_MAX;
					}
				}
				r = poll(prep_ctx_impl->m_pollfds.data(), prep_ctx_impl->m_pollfds.size(), timeout);
				if (r != -1)
					break;
				int e = errno;
				if (e == EINTR)
					continue;
				throw detail::unix_system_error(e);
			}
			m_pimpl->m_mutex.lock();

			if (r != 0)
			{
				for (size_t i = 0; r; ++i)
				{
					struct pollfd & p = prep_ctx_impl->m_pollfds[i];
					if (!p.revents)
						continue;
					--r;

					if (i == 0)
						continue;

					fin_ctx.finished_tasks = 0;
					fin_ctx.selected_timer_item = (size_t)-1;
					for (std::list<impl::task_entry>::iterator it = m_pimpl->m_tasks.begin(), eit = m_pimpl->m_tasks.end(); it != eit; ++it)
					{
						impl::task_entry & pe = *it;
						if (pe.memento.poll_item_first <= i && pe.memento.poll_item_last > i)
						{
							fin_ctx.selected_poll_item = i;
							if (pe.pt->finish_wait(fin_ctx))
							{
								if (pe.pt == focused_pt)
									done = true;
								pe.pt->detach_event_sink();
								m_pimpl->m_tasks.erase(it);
							}
							break;
						}
					}
				}
			}
			else
			{
				int i = tit - prep_ctx_impl->m_timeouts.begin();
				fin_ctx.finished_tasks = 0;
				fin_ctx.selected_poll_item = (size_t)-1;
				for (std::list<impl::task_entry>::iterator it = m_pimpl->m_tasks.begin(), eit = m_pimpl->m_tasks.end(); it != eit; ++it)
				{
					impl::task_entry & pe = *it;
					if (pe.memento.timer_item_first <= i && pe.memento.timer_item_last > i)
					{
						fin_ctx.selected_timer_item = i;
						if (pe.pt->finish_wait(fin_ctx))
						{
							if (pe.pt == focused_pt)
								done = true;
							pe.pt->detach_event_sink();
							m_pimpl->m_tasks.erase(it);
						}
						break;
					}
				}
			}
		}
	}
}

void sync_runner::submit(detail::prepared_task * pt)
{
	impl::task_entry te;
	te.pt = pt;

	detail::scoped_pthread_lock l(m_pimpl->m_mutex);
	m_pimpl->m_tasks.push_back(te);
	pt->attach_event_sink(*this);

	m_pimpl->m_update_event.set();
}

void sync_runner::cancel(detail::prepared_task *) throw()
{
	m_pimpl->m_update_event.set();
}

void sync_runner::cancel_and_wait(detail::prepared_task * pt) throw()
{
	assert(m_pimpl->m_associated_thread != 0);

	if (m_pimpl->m_associated_thread == syscall(SYS_gettid))
	{
		for (std::list<impl::task_entry>::iterator it = m_pimpl->m_tasks.begin(), eit = m_pimpl->m_tasks.end(); it != eit; ++it)
		{
			if (it->pt == pt)
			{
				m_pimpl->m_tasks.erase(it);
				break;
			}
		}

		pt->cancel_and_wait();
		pt->detach_event_sink();
	}
	else
	{
		pt->request_cancel(cl_kill);
		pt->shadow_wait();
	}
}
