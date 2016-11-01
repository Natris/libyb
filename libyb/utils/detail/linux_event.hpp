#ifndef LIBYB_UTILS_DETAIL_LINUX_EVENT_HPP
#define LIBYB_UTILS_DETAIL_LINUX_EVENT_HPP

#include <poll.h>

namespace yb {
namespace detail {

class linux_event
{
public:
	linux_event();
	~linux_event();

	void set();
	void reset();
	void wait() const;
	struct pollfd get_poll() const;

private:
	union {
		int m_fd;
		struct {
			int readfd;
			int writefd;
		} m_darwin;
	};

	linux_event(linux_event const &);
	linux_event & operator=(linux_event const &);
};

}
}

#endif // LIBYB_UTILS_DETAIL_LINUX_EVENT_HPP
