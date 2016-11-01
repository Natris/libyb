#ifndef LIBYB_NET_PIPE_HPP
#define LIBYB_NET_PIPE_HPP
#include "../async/task.hpp"
#include "../async/stream.hpp"

namespace yb {

class pipe
	: public stream
{
public:
	struct impl;

	pipe();
	explicit pipe(impl * pimpl);
	~pipe();
	pipe(pipe && o);
	pipe & operator=(pipe && o);

	void clear();

	task<buffer_view> read(buffer_policy policy, size_t max_size) override;
	task<size_t> write(buffer_ref buf) override;

private:
	impl * m_pimpl;

	pipe(pipe const &);
	pipe & operator=(pipe const &);
};

task<pipe> pipe_connect(string_ref const & pipeName);

} // namespace yb

#endif // LIBYB_NET_PIPE_HPP
