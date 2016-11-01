#ifndef LIBYB_ASYNC_DETAIL_UNIX_FILE_HPP
#define LIBYB_ASYNC_DETAIL_UNIX_FILE_HPP

#include "../task.hpp"
#include "../../buffer.hpp"

#ifdef __APPLE__
static  const int MSG_NOSIGNAL = 0;
#endif // __APPLE__

namespace yb {
namespace detail {

task<buffer_view> read_linux_fd(int fd, buffer_policy policy, size_t max_size);
task<size_t> write_linux_fd(int fd, buffer_ref buf);
task<size_t> send_linux_fd(int fd, buffer_ref buf, int flags);

}
}

#endif // LIBYB_ASYNC_DETAIL_UNIX_FILE_HPP
