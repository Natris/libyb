#ifndef LIBYB_ASYNC_DETAIL_PREPARED_TASK_IMPL_HPP
#define LIBYB_ASYNC_DETAIL_PREPARED_TASK_IMPL_HPP

#include "prepared_task.hpp"
#include "../task.hpp"

namespace yb {
namespace detail {

template <typename R>
class prepared_task_impl
	: public prepared_task
{
public:
	prepared_task_impl(task<R> && t);

	task_result<R> fetch_result() throw();

	void apply_cancel() override;
	void prepare_wait(task_wait_preparation_context & prep_ctx) override;
	bool finish_wait(task_wait_finalization_context & fin_ctx) override;
	void cancel_and_wait() override;

private:
	cancel_level m_applied_cl;
	task<R> m_task;
};

template <typename R>
class prepared_task_guard
{
public:
	explicit prepared_task_guard(prepared_task_impl<R> * pt);
	~prepared_task_guard();

	prepared_task_impl<R> * get() const;
	prepared_task_impl<R> * operator->() const;

private:
	prepared_task_impl<R> * m_pt;

	prepared_task_guard(prepared_task_guard const &);
	prepared_task_guard & operator=(prepared_task_guard const &);
};

template <typename R>
class shadow_task
	: public task_base<R>
{
public:
	explicit shadow_task(prepared_task_impl<R> * pt);
	~shadow_task();

	void cancel(cancel_level cl) throw() override;
	task_result<R> cancel_and_wait() throw() override;
	void prepare_wait(task_wait_preparation_context & ctx) override;
	task<R> finish_wait(task_wait_finalization_context & ctx) throw() override;

private:
	prepared_task_impl<R> * m_pt;

	shadow_task(shadow_task const &);
	shadow_task & operator=(shadow_task const &);
};

template <typename R>
prepared_task_impl<R>::prepared_task_impl(task<R> && t)
	: m_applied_cl(cl_none), m_task(std::move(t))
{
}

template <typename R>
task_result<R> prepared_task_impl<R>::fetch_result() throw()
{
	return m_task.get_result();
}

template <typename R>
void prepared_task_impl<R>::apply_cancel()
{
	cancel_level req_cl = this->requested_cancel_level();
	if (req_cl > m_applied_cl)
	{
		m_applied_cl = req_cl;
		m_task.cancel(req_cl);
	}
}

template <typename R>
void prepared_task_impl<R>::prepare_wait(task_wait_preparation_context & prep_ctx)
{
	m_task.prepare_wait(prep_ctx);
}

template <typename R>
bool prepared_task_impl<R>::finish_wait(task_wait_finalization_context & fin_ctx)
{
	m_task.finish_wait(fin_ctx);

	bool done = m_task.has_result();
	if (done)
		this->mark_finished();
	return done;
}

template <typename R>
void prepared_task_impl<R>::cancel_and_wait()
{
	m_task = task<R>(m_task.cancel_and_wait());
	this->mark_finished();
}

template <typename R>
shadow_task<R>::shadow_task(prepared_task_impl<R> * pt)
	: m_pt(pt)
{
	m_pt->addref();
}

template <typename R>
shadow_task<R>::~shadow_task()
{
	m_pt->release();
}

template <typename R>
void shadow_task<R>::cancel(cancel_level cl) throw()
{
	m_pt->request_cancel(cl);
}

template <typename R>
task_result<R> shadow_task<R>::cancel_and_wait() throw()
{
	m_pt->shadow_cancel_and_wait();
	return m_pt->fetch_result();
}

template <typename R>
void shadow_task<R>::prepare_wait(task_wait_preparation_context & ctx)
{
	m_pt->prepare_wait(ctx);
}

template <typename R>
task<R> shadow_task<R>::finish_wait(task_wait_finalization_context &) throw()
{
	return task<R>(m_pt->fetch_result());
}

template <typename R>
prepared_task_guard<R>::prepared_task_guard(prepared_task_impl<R> * pt)
	: m_pt(pt)
{
}

template <typename R>
prepared_task_guard<R>::~prepared_task_guard()
{
	m_pt->release();
}

template <typename R>
prepared_task_impl<R> * prepared_task_guard<R>::get() const
{
	return m_pt;
}

template <typename R>
prepared_task_impl<R> * prepared_task_guard<R>::operator->() const
{
	return m_pt;
}

} // namespace detail
} // namespace yb

#endif // LIBYB_ASYNC_DETAIL_PREPARED_TASK_IMPL_HPP
