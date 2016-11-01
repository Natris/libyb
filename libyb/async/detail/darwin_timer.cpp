#include "../cancel_exception.hpp"
#include "../../utils/noncopyable.hpp"
#include "../task.hpp"
#include "darwin_wait_context.hpp"
#include <exception>
#include <utility>
#include <poll.h>
#include <cassert>
#include <sys/time.h>

namespace yb {
namespace detail {

class darwin_timer_task
	: public task_base<void>, noncopyable
{
public:
	darwin_timer_task(int ms)
		: m_ms(ms), m_time((uint64_t)-1)
	{
	}

	task<void> cancel_and_wait() throw() override
	{
		return task<void>::from_exception(std::make_exception_ptr(task_cancelled()));
	}

	void prepare_wait(task_wait_preparation_context & ctx, cancel_level cl) override
	{
		if (m_time == (uint64_t)-1) {
			struct timeval timeout;
			gettimeofday(&timeout, nullptr);
			m_time = (uint64_t)timeout.tv_sec * 1000 + timeout.tv_usec / 1000 + m_ms;
		}
		ctx.get()->m_timeouts.push_back(m_time);
	}

	task<void> finish_wait(task_wait_finalization_context &) throw() override
	{
		return async::value();
	}

private:
	int m_ms;
	uint64_t m_time;
};

} // namespace detail


task<void> wait_ms(int milliseconds)
{
	return task<void>::from_task(new detail::darwin_timer_task(milliseconds));
}

} // namespace yb

