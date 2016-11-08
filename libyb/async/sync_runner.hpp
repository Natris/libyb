#ifndef LIBYB_ASYNC_SYNC_RUNNER_HPP
#define LIBYB_ASYNC_SYNC_RUNNER_HPP

#include "runner.hpp"
#include <memory>

namespace yb {

class sync_runner
	: public runner
	, private detail::prepared_task_event_sink
{
public:
	explicit sync_runner(bool associate_thread_now = true);
	~sync_runner();

	void associate_current_thread();

	void run_until(detail::prepared_task * pt) override;
	void submit(detail::prepared_task * pt) override;

	std::string dbg_print_unsafe() override;
private:
	void cancel(detail::prepared_task * pt) throw() override;

	struct impl;
	std::unique_ptr<impl> m_pimpl;

	sync_runner(sync_runner const &);
	sync_runner & operator=(sync_runner const &);
};

} // namespace yb

#endif // LIBYB_ASYNC_SYNC_RUNNER_HPP
