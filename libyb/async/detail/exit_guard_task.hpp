#ifndef LIBYB_ASYNC_DETAIL_EXIT_GUARD_TASK_HPP
#define LIBYB_ASYNC_DETAIL_EXIT_GUARD_TASK_HPP

#include "../task_base.hpp"

namespace yb {
namespace detail {

class exit_guard_task
	: public task_base<void>
{
public:
	explicit exit_guard_task(cancel_level cancel_threshold);

	task<void> cancel_and_wait() throw() override;
	void prepare_wait(task_wait_preparation_context & ctx, cancel_level cl) override;
	task<void> finish_wait(task_wait_finalization_context & ctx) throw() override;
	std::string dbg_print(const detail::dbg_print_ctx& ctx) override
	{
		return detail::dbg_print(ctx, "exit_guard_task: threshold=%d", (int)m_threshold);
	}

private:
	cancel_level m_threshold;
};

} // namespace detail
} // namespace yb

#endif // LIBYB_ASYNC_DETAIL_EXIT_GUARD_TASK_HPP
