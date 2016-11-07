#ifndef LIBYB_ASYNC_DETAIL_LOOP_TASK_HPP
#define LIBYB_ASYNC_DETAIL_LOOP_TASK_HPP

#include "../task_base.hpp"
#include "../task.hpp"
#include "wait_context.hpp"
#include <algorithm>

namespace yb {
namespace detail {

template <typename T>
struct loop_state
{
	loop_state();
	loop_state(loop_state<T> && o);

	template <typename U>
	explicit loop_state(U && state);

	T m_state;
};

template <>
struct loop_state<void>
{
};

template <typename S, typename F, typename T>
class loop_task
	: public task_base<void>, private loop_state<T>
{
public:
	loop_task(task<S> && t, F f, loop_state<T> && state);

	task<void> cancel_and_wait() throw();
	void prepare_wait(task_wait_preparation_context & ctx, cancel_level cl);
	task<void> finish_wait(task_wait_finalization_context & ctx) throw();
	std::string dbg_print(const detail::dbg_print_ctx& ctx) override
	{
		std::string str = detail::dbg_print(ctx, "loop_task: m_cancel_level=%d", (int)m_cancel_level);
		str += m_task.dbg_print(ctx);
		return str;
	}

private:
	task<S> m_task;
	F m_f;
	cancel_level m_cancel_level;
};

} // namespace detail
} // namespace yb

namespace yb {
namespace detail {

template <typename R, typename F, typename T>
task<R> invoke_loop_body2(F & f, task<R> && r, loop_state<T> & state, cancel_level cl)
{
	return f(r.get(), state.m_state, cl);
}

template <typename F, typename T>
task<void> invoke_loop_body2(F & f, task<void> &&, loop_state<T> & state, cancel_level cl)
{
	return f(state.m_state, cl);
}

template <typename R, typename F>
task<R> invoke_loop_body2(F & f, task<R> && r, loop_state<void> &, cancel_level cl)
{
	return f(r.get(), cl);
}

template <typename F>
task<void> invoke_loop_body2(F & f, task<void> &&, loop_state<void> &, cancel_level cl)
{
	return f(cl);
}

template <typename R, typename F, typename T>
task<R> invoke_loop_body(F & f, task<R> && r, loop_state<T> & state, cancel_level cl)
{
	try
	{
		return invoke_loop_body2(f, std::move(r), state, cl);
	}
	catch (...)
	{
		return async::raise<R>();
	}
}

template <typename T>
loop_state<T>::loop_state()
	: m_state()
{
}

template <typename T>
loop_state<T>::loop_state(loop_state<T> && o)
	: m_state(std::move(o.m_state))
{
}

template <typename T>
template <typename U>
loop_state<T>::loop_state(U && state)
	: m_state(std::forward<U>(state))
{
}

template <typename S, typename F, typename T>
loop_task<S, F, T>::loop_task(task<S> && t, F f, loop_state<T> && state)
	: loop_state<T>(std::move(state)), m_task(std::move(t)), m_f(std::move(f)), m_cancel_level(cl_none)
{
}

template <typename S, typename F, typename T>
task<void> loop_task<S, F, T>::cancel_and_wait() throw()
{
	while (!m_task.empty())
	{
		task<S> r = m_task.cancel_and_wait();
		if (r.has_exception())
			return task<void>::from_exception(r.exception());
		m_task = invoke_loop_body(m_f, std::move(r), *this, cl_kill);
	}

	return task<void>::from_value();
}

template <typename S, typename F, typename T>
void loop_task<S, F, T>::prepare_wait(task_wait_preparation_context & ctx, cancel_level cl)
{
	m_cancel_level = cl;
	m_task.prepare_wait(ctx, cl);
}

template <typename S, typename F, typename T>
task<void> loop_task<S, F, T>::finish_wait(task_wait_finalization_context & ctx) throw()
{
	m_task.finish_wait(ctx);
	while (m_task.has_value() || m_task.has_exception())
	{
		if (m_task.has_exception())
			return task<void>::from_exception(m_task.exception());
		m_task = invoke_loop_body(m_f, std::move(m_task), *this, m_cancel_level);
		if (m_task.empty())
			return async::value();
	}

	return nulltask;
}

template <typename S, typename T, typename F>
task<void> loop(task<S> && t, loop_state<T> & state, F && f)
{
	while (t.has_value() || t.has_exception())
	{
		if (t.has_exception())
			return task<void>::from_exception(t.exception());
		t = detail::invoke_loop_body(f, std::move(t), state, cl_none);
		if (t.empty())
			return async::value();
	}

	try
	{
		return task<void>::from_task(new detail::loop_task<S, F, T>(std::move(t), std::move(f), std::move(state)));
	}
	catch (...)
	{
		while (!t.empty())
		{
			task<S> r = t.cancel_and_wait();
			if (r.has_exception())
				return async::raise<void>(r.exception());
			t = detail::invoke_loop_body(f, std::move(r), state, cl_none);
		}

		return async::raise<void>();
	}
}

template <typename R, typename S, typename F>
class loop2_task
	: public task_base<R>
{
public:
	loop2_task(task<S> && state, F f)
		: m_state(std::move(state)), m_f(std::move(f)), m_cancel_level(cl_none)
	{
	}

	task<R> cancel_and_wait() throw()
	{
		try
		{
			for (;;)
			{
				m_state = m_state.cancel_and_wait();
				if (m_state.has_exception())
					return task<R>::from_exception(m_state.exception());
				task<R> r = m_f(m_state, cl_kill);
				if (r.has_task())
					return r.cancel_and_wait();
			}
		}
		catch (...)
		{
			return async::raise<R>();
		}
	}

	void prepare_wait(task_wait_preparation_context & ctx, cancel_level cl)
	{
		m_cancel_level = cl;
		m_state.prepare_wait(ctx, cl);
	}

	task<R> finish_wait(task_wait_finalization_context & ctx) throw()
	{
		try
		{
			m_state.finish_wait(ctx);
			while (m_state.has_value() || m_state.has_exception())
			{
				if (m_state.has_exception())
					return task<R>::from_exception(m_state.exception());
				task<R> r = m_f(m_state, m_cancel_level);
				if (!r.empty())
					return std::move(r);
			}
		}
		catch (...)
		{
			return async::raise<R>();
		}

		return nulltask;
	}
	std::string dbg_print(const detail::dbg_print_ctx& ctx) override
	{
		std::string str = detail::dbg_print(ctx, "loop2_task: m_cancel_level=%d", (int)m_cancel_level);
		str += m_state.dbg_print(ctx);
		return str;
	}

private:
	task<S> m_state;
	F m_f;
	cancel_level m_cancel_level;
};

} // namespace detail

template <typename S, typename F>
task<void> loop(task<S> && t, F f)
{
	detail::loop_state<void> state;
	return detail::loop(std::move(t), state, std::move(f));
}

template <typename S, typename T, typename F, typename U>
task<void> loop_with_state(task<S> && t, U && s, F f)
{
	detail::loop_state<T> state(std::move(s));
	return detail::loop(std::move(t), state, std::move(f));
}

template <typename S, typename T, typename F>
task<void> loop_with_state(task<S> && t, F f)
{
	detail::loop_state<T> state;
	return detail::loop(std::move(t), state, std::move(f));
}

template <typename F>
task<void> loop(F && f)
{
	return loop(async::value(), std::move(f));
}

template <typename S, typename F>
auto loop2(F f) -> decltype(f(*(task<S> *)0, cl_none))
{
	typedef typename detail::unwrap_task<decltype(f(*(task<S> *)0, cl_none))>::type R;

	task<S> state;

	try
	{
		task<R> r = f(state, cl_none);
		while (r.empty() && !state.has_task())
		{
			if (state.has_exception())
				return task<R>::from_exception(state.exception());
			r = f(state, cl_none);
		}

		if (!r.empty())
			return std::move(r);
	}
	catch (...)
	{
		return async::raise<R>();
	}

	try
	{
		return task<R>::from_task(new detail::loop2_task<R, S, F>(std::move(state), std::move(f)));
	}
	catch (...)
	{
		try
		{
			for (;;)
			{
				state = state.cancel_and_wait();
				if (state.has_exception())
					return task<R>::from_exception(state.exception());
				task<R> r = f(state, cl_kill);
				if (r.has_task())
					return r.cancel_and_wait();
			}
		}
		catch (...)
		{
			return async::raise<R>();
		}

		return async::raise<R>();
	}
}

} // namespace yb

#endif // LIBYB_ASYNC_DETAIL_LOOP_TASK_HPP
