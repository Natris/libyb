#include "notification_event.hpp"
#include "../../utils/detail/unix_system_error.hpp"
#include "linux_fdpoll_task.hpp"
#include <unistd.h>
#include <errno.h>
using namespace yb;

notification_event::notification_event()
{
	int filedes[2];
	if (pipe(filedes))
		throw yb::detail::unix_system_error(errno);
	m_darwin.readfd = filedes[0];
	m_darwin.writefd = filedes[1];
}

notification_event::~notification_event()
{
	if (m_darwin.readfd != -1) {
		close(m_darwin.readfd);
		close(m_darwin.writefd);
	}
}

notification_event::notification_event(notification_event && o)
	: m_darwin(o.m_darwin)
{
	o.m_darwin.readfd = -1;
}

notification_event & notification_event::operator=(notification_event && o)
{
	std::swap(m_darwin, o.m_darwin);
	return *this;
}

void notification_event::set()
{
	uint64_t val = 1;
	write(m_darwin.writefd, &val, sizeof val);
}

void notification_event::reset()
{
	uint64_t val;
	while (read(m_darwin.readfd, &val, sizeof val) > 0) 
		;
}

yb::task<void> notification_event::wait()
{
	return yb::make_linux_pollfd_task(m_darwin.readfd, POLLIN, [](yb::cancel_level cl) {
		return cl < yb::cl_abort;
	}).ignore_result();
}
