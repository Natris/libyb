#include "../net.hpp"
#include "../pipe.hpp"
#include "../../async/detail/unix_file.hpp"
#include "../../async/detail/linux_fdpoll_task.hpp"
#include "../../utils/detail/scoped_unix_fd.hpp"
#include "../../utils/detail/unix_system_error.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include <fcntl.h>
using namespace yb;

struct pipe::impl
{
	int fd;
};

pipe::pipe()
	: m_pimpl(0)
{
}

pipe::pipe(impl * pimpl)
	: m_pimpl(pimpl)
{
}

pipe::~pipe()
{
	this->clear();
}

pipe::pipe(pipe && o)
	: m_pimpl(o.m_pimpl)
{
	o.m_pimpl = 0;
}

yb::pipe & pipe::operator=(pipe && o)
{
	pipe tmp(std::move(o));
	std::swap(m_pimpl, tmp.m_pimpl);
	return *this;
}

void pipe::clear()
{
	if (m_pimpl)
	{
		::close(m_pimpl->fd);
		delete m_pimpl;
		m_pimpl = 0;
	}
}

task<buffer_view> pipe::read(buffer_policy policy, size_t max_size)
{
	return detail::read_linux_fd(m_pimpl->fd, std::move(policy), max_size);
}

task<size_t> pipe::write(buffer_ref buf)
{
	return detail::send_linux_fd(m_pimpl->fd, buf, MSG_NOSIGNAL);
}

task<yb::pipe> yb::pipe_connect(string_ref const & pipeName)
{
	struct ctx_t
	{
		detail::scoped_unix_fd s;
	};
	std::shared_ptr<ctx_t> ctx(std::make_shared<ctx_t>());

	ctx->s.reset(::socket(AF_UNIX, SOCK_STREAM, 0));
	if (ctx->s.get() == -1)
		return async::raise<yb::pipe>(detail::unix_system_error(errno));

	struct sockaddr_un sa = {};
	if (pipeName.size() >= sizeof(sa.sun_path))
		return async::raise<yb::pipe>(detail::unix_system_error(ENAMETOOLONG));

	int flags;
	if ((flags = fcntl(ctx->s.get(), F_GETFL)) < 0)
		return async::raise<yb::pipe>(detail::unix_system_error(errno));

	flags |= O_NONBLOCK;

	if ((flags = fcntl(ctx->s.get(), F_SETFL, flags)) < 0)
		return async::raise<yb::pipe>(detail::unix_system_error(errno));

	sa.sun_family = AF_UNIX;
	memcpy(&sa.sun_path[0], pipeName.data(), pipeName.size());
	sa.sun_path[pipeName.size()] = '\0';

	if (::connect(ctx->s.get(), (sockaddr const *)&sa, sizeof sa) == 0) {
		std::unique_ptr<pipe::impl> pimpl(new pipe::impl());
		pimpl->fd = ctx->s.release();

		yb::pipe sock(pimpl.release());
		return async::value(std::move(sock));
	}

	if (errno != EINPROGRESS)
		return async::raise<yb::pipe>(detail::unix_system_error(errno));

	return make_linux_pollfd_task(ctx->s.get(), POLLIN, [](cancel_level cl) {
		return cl < cl_quit;
	}).then([ctx](short) {
		std::unique_ptr<pipe::impl> pimpl(new pipe::impl());
		pimpl->fd = ctx->s.release();

		yb::pipe sock(pimpl.release());
		return async::value(std::move(sock));
	});
}
