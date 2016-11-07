#include "linux_event.hpp"
#include "unix_system_error.hpp"
#include <unistd.h>
#include <stdexcept>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
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
	unsigned char val = 1;
	for (;;) {
		auto res = write(m_darwin.writefd, &val, sizeof val);
		if (res > 0) {
			return;
		}
	}
}

void linux_event::reset()
{
	for (;;) {
		unsigned char val;
		struct pollfd pf;
		pf.events = POLLIN;
		pf.fd = m_darwin.readfd;
		int res = poll(&pf, 1, 0);
		if (res > 0) {
			::read(m_darwin.readfd, &val, sizeof val);
		} else if (res == 0) {
			return;
		}
	}
}

void linux_event::wait() const
{
	struct pollfd pf = this->get_poll();
	while (::poll(&pf, 1, -1) <= 0)
		;
}

struct pollfd linux_event::get_poll() const
{
	struct pollfd pf = {};
	pf.fd = m_darwin.readfd;
	pf.events = POLLIN;
	return pf;
}
