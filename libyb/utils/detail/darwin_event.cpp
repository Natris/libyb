#include "linux_event.hpp"
#include "unix_system_error.hpp"
#include <unistd.h>
#include <stdexcept>
#include <errno.h>
#include <stdint.h>
using namespace yb::detail;

linux_event::linux_event()
{
	int filedes[2];
	if (pipe(filedes))
		throw yb::detail::unix_system_error(errno);
	m_darwin.readfd = filedes[0];
	m_darwin.writefd = filedes[1];
}

linux_event::~linux_event()
{
	if (m_darwin.readfd != -1) {
		close(m_darwin.readfd);
		close(m_darwin.writefd);
	}
}

void linux_event::set()
{
	uint64_t val = 1;
	write(m_darwin.writefd, &val, sizeof val);
}

void linux_event::reset()
{
	uint64_t val;
	::read(m_darwin.readfd, &val, sizeof val);
}

void linux_event::wait() const
{
	struct pollfd pf = this->get_poll();
	::poll(&pf, 1, -1);
}

struct pollfd linux_event::get_poll() const
{
	struct pollfd pf = {};
	pf.fd = m_darwin.readfd;
	pf.events = POLLIN;
	return pf;
}
