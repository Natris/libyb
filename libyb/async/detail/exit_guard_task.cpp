#include "exit_guard_task.hpp"
#include "../cancel_exception.hpp"
#include "../task.hpp"
using namespace yb;
using namespace yb::detail;

exit_guard_task::exit_guard_task(cancel_level cancel_threshold)
	: m_threshold(cancel_threshold)
{
}

task<void> exit_guard_task::cancel_and_wait() throw()
{
	return task<void>::from_value();
}

void exit_guard_task::prepare_wait(task_wait_preparation_context & ctx, cancel_level cl)
{
	if (cl >= m_threshold)
		ctx.set_finished();
}

task<void> exit_guard_task::finish_wait(task_wait_finalization_context &) throw()
{
	return async::value();
}