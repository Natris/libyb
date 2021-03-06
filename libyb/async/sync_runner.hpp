#ifndef LIBYB_ASYNC_SYNC_RUNNER_HPP
#define LIBYB_ASYNC_SYNC_RUNNER_HPP

#include "task.hpp"
#include <utility> //move
#include <list>

namespace yb {

class sync_runner;

template <typename T>
class sync_promise
{
public:
	explicit sync_promise(sync_runner * runner)
		: m_runner(runner), m_refcount(0)
	{
	}

	void addref()
	{
		++m_refcount;
	}

	void release()
	{
		if (!--m_refcount)
			delete this;
	}

	void cancel(cancel_level cl)
	{
		m_task.cancel(cl);
	}

	void cancel_and_wait()
	{
		m_task = async::result(m_task.cancel_and_wait());
	}

	void set_task(task<T> && t)
	{
		m_task = std::move(t);
	}

	void prepare_wait(task_wait_preparation_context & ctx)
	{
		m_task.prepare_wait(ctx);
	}

	void finish_wait(task_wait_finalization_context & ctx) throw()
	{
		m_task.finish_wait(ctx);
	}

	bool has_result() const
	{
		return m_task.has_result();
	}

	task_result<T> get();

private:
	sync_runner * m_runner;
	int m_refcount;
	task<T> m_task;

	friend class sync_runner;
};

template <typename T>
class sync_future
{
public:
	explicit sync_future(sync_promise<T> * promise)
		: m_promise(promise)
	{
		assert(m_promise);
		m_promise->addref();
	}

	explicit sync_future(std::exception_ptr exc)
		: m_promise(0), m_exception(std::move(exc))
	{
	}

	sync_future(sync_future && o)
		: m_promise(o.m_promise), m_exception(std::move(o.m_exception))
	{
		o.m_promise = 0;
	}

	~sync_future()
	{
		if (m_promise)
		{
			m_promise->cancel_and_wait();
			m_promise->release();
		}
	}

	sync_future & operator=(sync_future && o)
	{
		if (m_promise)
			m_promise->release();
		m_promise = o.m_promise;
		o.m_promise = 0;
		m_exception = std::move(o.m_exception);
		return *this;
	}

	void detach()
	{
		if (m_promise)
		{
			m_promise->release();
			m_promise = 0;
		}
	}

	task_result<T> try_get()
	{
		if (m_promise)
			return m_promise->get();
		else
			return task_result<T>(m_exception);
	}

	T get(cancel_level cl = cl_none)
	{
		if (cl != cl_none)
			this->cancel(cl);
		return m_promise->get().get();
	}

	void cancel(cancel_level cl)
	{
		if (m_promise)
			m_promise->cancel(cl);
	}

	T wait(cancel_level cl = cl_none)
	{
		return this->get(cl);
	}

private:
	sync_promise<T> * m_promise;
	std::exception_ptr m_exception;

	sync_future(sync_future const &);
	sync_future & operator=(sync_future const &);

	friend class sync_runner;
};

class sync_runner
{
public:
	~sync_runner()
	{
	}

	template <typename T>
	sync_future<T> post(task<T> && t)
	{
		assert(!t.empty());

		try
		{
			std::unique_ptr<sync_promise<T>> promise(new sync_promise<T>(this));
			if (t.has_result())
			{
				promise->set_task(std::move(t));
				return sync_future<T>(promise.release());
			}

			task<void> tt(new promise_task<T>(promise.get()));
			sync_promise<T> * ppromise = promise.release();
			m_parallel_tasks |= std::move(tt);
			ppromise->set_task(std::move(t));
			return sync_future<T>(ppromise);
		}
		catch (...)
		{
			return sync_future<T>(std::current_exception());
		}
	}

	template <typename T>
	void post_detached(task<T> && t)
	{
		assert(!t.empty());
		if (t.has_task())
			m_parallel_tasks |= std::move(t);
	}

	template <typename T>
	friend sync_future<T> operator|(sync_runner & r, task<T> && t)
	{
		return r.post(std::move(t));
	}

	template <typename T>
	friend sync_runner & operator|=(sync_runner & r, task<T> && t)
	{
		r.post_detached(std::move(t));
		return r;
	}

	template <typename T>
	task_result<T> try_run(sync_future<T> const & f)
	{
		sync_promise<T> * promise = f.m_promise;
		if (!promise)
			return task_result<T>(f.m_exception);

		try
		{
			task_wait_preparation_context wait_ctx;

			assert(!promise->m_task.empty());
			while (!promise->m_task.has_result())
			{
				this->poll_one(wait_ctx);
			}

			return promise->m_task.get_result();
		}
		catch (...)
		{
			return std::current_exception();
		}
	}

	template <typename T>
	task_result<T> try_run(task<T> && t)
	{
		sync_future<T> f = this->post(std::move(t));
		return this->try_run(f);
	}

	template <typename T>
	T run(task<T> && t)
	{
		return this->try_run(std::move(t)).get();
	}

	template <typename T>
	friend T operator<<(sync_runner & r, task<T> && t)
	{
		return r.run(std::move(t));
	}

private:
	void poll_one(task_wait_preparation_context & wait_ctx);

	template <typename T>
	class promise_task
		: public task_base<void>
	{
	public:
		promise_task(sync_promise<T> * promise)
			: m_promise(promise)
		{
			m_promise->addref();
		}

		~promise_task()
		{
			m_promise->release();
		}

		void cancel(cancel_level cl) throw()
		{
			m_promise->cancel(cl);
		}

		task_result<void> cancel_and_wait() throw()
		{
			m_promise->cancel_and_wait();
			assert(m_promise->has_result());
			return task_result<void>();
		}

		void prepare_wait(task_wait_preparation_context & ctx)
		{
			m_promise->prepare_wait(ctx);
		}

		task<void> finish_wait(task_wait_finalization_context & ctx) throw()
		{
			m_promise->finish_wait(ctx);
			if (m_promise->has_result())
				return async::value();
			return nulltask;
		}

	private:
		sync_promise<T> * m_promise;
	};

	task<void> m_parallel_tasks;
};

template <typename T>
task_result<T> sync_promise<T>::get()
{
	return m_runner->try_run(sync_future<T>(this));
}

} // namespace yb

#endif // LIBYB_ASYNC_SYNC_RUNNER_HPP
