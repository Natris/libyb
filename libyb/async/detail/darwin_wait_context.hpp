#ifndef LIBYB_ASYNC_DETAIL_DARWIN_WAIT_CONTEXT_HPP
#define LIBYB_ASYNC_DETAIL_DARWIN_WAIT_CONTEXT_HPP

#include "wait_context.hpp"
#include <vector>
#include <sys/poll.h>
#include <sys/time.h>

namespace yb {

struct task_wait_poll_item
{
};

struct task_wait_preparation_context_impl
{
	std::vector<struct pollfd> m_pollfds;
	std::vector<uint64_t> m_timeouts; // gettimeofday in msec
	size_t m_finished_tasks;
};

} // namespace yb

#endif // LIBYB_ASYNC_DETAIL_DARWIN_WAIT_CONTEXT_HPP
