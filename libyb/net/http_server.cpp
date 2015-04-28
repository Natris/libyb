#include "http_server.hpp"
#include "net.hpp"
#include "detail/http_request_parser.hpp"
#include "../async/channel.hpp"
#include "../async/group.hpp"
#include "../utils/lexcast.hpp"
#include <sstream>
using namespace yb;

namespace {

class pump_stream
	: public stream
{
public:
	pump_stream(yb::channel<yb::buffer_view> const & chan)
		: m_buffers(chan)
	{
	}

	task<buffer_view> read(buffer_policy policy, size_t max_size) override
	{
		struct cont_t
		{
			explicit cont_t(buffer_policy policy)
				: m_policy(std::move(policy))
			{
			}

			task<buffer_view> operator()(buffer_view view)
			{
				return m_policy.copy(std::move(view));
			}

			buffer_policy m_policy;
		};

		return m_buffers.receive().then(cont_t(std::move(policy)));
	}

	task<size_t> write(buffer_ref buf) override { return yb::nulltask; }

private:
	yb::channel<yb::buffer_view> m_buffers;
};

class http_handler
{
public:
	http_handler(stream & s);
	task<void> run(std::function<task<http_response>(http_request const &)> fn);

	struct request_buffer_handler;

private:
	stream & m_s;
	yb::buffer_policy m_bp;
	http_request_parser m_parser;

	yb::channel<yb::task<http_response>> m_response_channel;

	size_t m_req_body_remaining;
	yb::channel<yb::buffer_view> m_request_body_channel;

	std::string m_current_response;
};

} // namespace

yb::task<void> yb::run_http_server(stream & s, std::function<task<http_response>(http_request const &)> fn)
{
	std::shared_ptr<http_handler> ctx = std::make_shared<http_handler>(s);
	return ctx->run(std::move(fn)).keep_alive(ctx);
}

task<void> yb::run_http_server(uint16_t port, std::function<task<http_response>(http_request const &)> fn)
{
	std::shared_ptr<yb::group> socket_group = std::make_shared<yb::group>();

	yb::task<void> group_task = yb::group::create(*socket_group);
	yb::task<void> listen_task = yb::tcp_listen(port, [socket_group, fn](yb::tcp_socket & s) {
		std::shared_ptr<yb::tcp_socket> ss = std::make_shared<yb::tcp_socket>(std::move(s));
		socket_group->post(yb::run_http_server(*ss, fn).keep_alive(ss));
	});

	return std::move(group_task) | std::move(listen_task);
}

struct http_handler::request_buffer_handler
{
public:
	request_buffer_handler(http_handler * self, buffer_view && buf, std::function<task<http_response>(http_request const &)> const & fn)
		: m_self(self), m_buf(std::move(buf)), m_fn(fn)
	{
	}

	request_buffer_handler(request_buffer_handler && o)
		: m_buf(std::move(o.m_buf)), m_self(o.m_self), m_fn(std::move(o.m_fn))
	{

	}

	task<buffer_view> operator()()
	{
		yb::buffer_ref rem = m_self->m_parser.push_data(*m_buf);
		if (m_self->m_parser.error())
			return async::raise<buffer_view>(std::runtime_error("parsing failed"));

		m_buf.shrink(rem.begin() - m_buf->begin(), rem.size());
		if (!m_self->m_parser.done())
		{
			if (m_buf.empty())
				return read(m_self->m_s, m_self->m_bp.ref(), 0);
			else
				return async::value(std::move(m_buf));
		}

		http_request req = m_self->m_parser.req();
		m_self->m_parser.clear();

		auto te = req.headers.get_split("transfer-encoding");
		if (!te.empty())
		{
			yb::string_ref te_value = te.front();

			if (te_value != "chunked" && te_value != "identity")
				return async::raise<buffer_view>(std::runtime_error("transfer encoding unsupported"));

			te.pop_front();
			if (!te.empty())
				return async::raise<buffer_view>(std::runtime_error("transfer encoding unsupported"));

			if (te_value != "identity")
				return async::raise<buffer_view>(std::runtime_error("XXX unimplemented"));
		}

		size_t content_length = lexical_cast<size_t>(req.headers.get("content-length", "0"));

		yb::task<http_response> resp;
		if (content_length == 0)
		{
			resp = m_fn(std::move(req));
		}
		else
		{
			m_self->m_request_body_channel = yb::channel<yb::buffer_view>::create_finite<3>();
			req.body = std::make_shared<pump_stream>(m_self->m_request_body_channel);
			m_self->m_req_body_remaining = content_length;
			resp = m_fn(std::move(req));
		}

		struct cont_t
		{
			cont_t(buffer_view && buf, http_handler * self)
				: m_buf(std::move(buf)), m_self(self)
			{
			}

			cont_t(cont_t && o)
				: m_buf(std::move(o.m_buf)), m_self(o.m_self)
			{
			}

			task<buffer_view> operator()()
			{
				if (!m_buf->empty())
					return async::value(std::move(m_buf));
				return read(m_self->m_s, m_self->m_bp.ref(), 0);
			}

			buffer_view m_buf;
			http_handler * m_self;
		};

		return m_self->m_response_channel.send(std::move(resp)).then(cont_t(std::move(m_buf), m_self));
	}

	buffer_view m_buf;
	http_handler * m_self;
	std::function<task<http_response>(http_request const &)> m_fn;

private:
	request_buffer_handler(request_buffer_handler const &);
};

http_handler::http_handler(stream & s)
	: m_s(s), m_bp(4096, /*sharable=*/true), m_response_channel(yb::channel<yb::task<http_response>>::create_finite<3>()),
	m_req_body_remaining(0), m_request_body_channel(yb::channel<yb::buffer_view>::create_finite<3>())
{
}

namespace {

class http_chunker
	: public istream
{
public:
	http_chunker(std::shared_ptr<istream> body, size_t buf_size = 4096)
		: m_body(std::move(body)), m_offset(0)
	{
		m_buffer.reserve(buf_size);
	}

	task<buffer_view> read(buffer_policy policy, size_t max_size) override
	{
		struct cont_t
		{
			task<buffer_view> operator()(buffer_view bv)
			{
				uint8_t chunk_header[16];
				uint8_t * chunk_header_end = chunk_header;
				if (bv->size() == 0)
				{
					*chunk_header_end++ = '0';
				}
				else
				{
					size_t s = bv->size();
					while (s)
					{
						static uint8_t const digits[] = "0123456789abcdef";
						*chunk_header_end++ = digits[s & 0xf];
						s >>= 4;
					}
					std::reverse(chunk_header, chunk_header_end);
					*chunk_header_end++ = '\r';
					*chunk_header_end++ = '\n';
				}

				self->m_buffer.assign(chunk_header, chunk_header_end);
				self->m_buffer.insert(self->m_buffer.end(), bv->data(), bv->data() + bv->size());
				self->m_buffer.push_back('\r');
				self->m_buffer.push_back('\n');
				self->m_offset = 0;
				return self->process(policy, max_size);
			}

			http_chunker * self;
			buffer_policy policy;
			size_t max_size;
		};

		if (m_offset != m_buffer.size())
			return this->process(policy, max_size);

		cont_t cont = { this, std::move(policy), max_size };
		return m_body->read(buffer_policy(4096), m_buffer.capacity() - 18).then(std::move(cont));
	}

private:
	task<buffer_view> process(buffer_policy & policy, size_t max_size)
	{
		max_size = (std::min)(m_buffer.size() - m_offset, max_size);
		return policy.fetch(1, max_size).then([this, max_size](buffer buf) {
			size_t copy_size = (std::min)(buf.size(), max_size);
			std::copy(m_buffer.data() + m_offset, m_buffer.data() + m_offset + copy_size, buf.data());
			m_offset += copy_size;
			return buffer_view(std::move(buf), copy_size);
		});
	}

	std::shared_ptr<istream> m_body;
	std::vector<uint8_t> m_buffer;
	size_t m_offset;
};

}

yb::task<void> http_handler::run(std::function<task<http_response>(http_request const &)> fn)
{
	task<void> request_loop = loop([this, fn](yb::cancel_level cl) -> yb::task<void> {

		return loop(read(m_s, m_bp.ref(), 0), [this, fn](yb::buffer_view buf, cancel_level cl) -> yb::task<yb::buffer_view> {

			if (buf.empty())
				return async::raise<buffer_view>(yb::task_cancelled());

			task<void> presend;
			if (m_req_body_remaining)
			{
				size_t chunk_size = (std::min)(m_req_body_remaining, buf->size());

				yb::buffer_view buf_clone = buf.clone();
				buf_clone.shrink(0, chunk_size);
				m_req_body_remaining -= chunk_size;

				presend = m_request_body_channel.send(std::move(buf_clone));
				if (!m_req_body_remaining)
				{
					presend = presend.then([this]() {
						m_request_body_channel.send(buffer_view());
					});
				}

				buf.shrink(chunk_size, buf->size() - chunk_size);
			}
			else
			{
				presend = async::value();
			}

			return presend.then(request_buffer_handler(this, std::move(buf), fn));
		});
	}).continue_with([this](yb::task<void>) {
		return m_response_channel.send(async::raise<http_response>(task_cancelled()));
	});

	return std::move(request_loop) | loop([this](yb::cancel_level cl) -> yb::task<void> {
		return m_response_channel.receive().then([this](yb::task<http_response> resp_promise) {
			return resp_promise.then([this](http_response const & resp) {
				std::ostringstream oss;
				oss << "HTTP/1.1 " << resp.status_code << " " << resp.get_status_text() << "\r\n";
				for (auto && kv: resp.headers.get_all())
					oss << kv.first << ": " << kv.second << "\r\n";

				std::string content_length;
				std::shared_ptr<istream> body;
				if (!resp.body || resp.headers.try_get("content-length", content_length))
				{
					oss << "\r\n";
					body = resp.body;
				}
				else
				{
					oss << "transfer-encoding: chunked\r\n\r\n";
					body = std::make_shared<http_chunker>(resp.body);
				}

				m_current_response = oss.str();

				return write_all(m_s, yb::buffer_ref((uint8_t const *)m_current_response.data(), m_current_response.size())).then([this, body]() {
					if (!body)
						return async::value();
					return copy(m_s, *body, 4096*1024);
				}).keep_alive(body);
			});
		});
	});
}
