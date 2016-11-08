#include "descriptor_reader.hpp"
#include "channel.hpp"
#include "../utils/noncopyable.hpp"

namespace yb {

namespace {

struct handler
	: packet_handler, noncopyable
{
	explicit handler(device & d)
		: m_dev(d), m_out(channel<task<device_descriptor>>::create_finite()), m_registered(true)
	{
		m_reg = d.register_receiver(*this);
	}

	~handler()
	{
		if (m_registered)
			m_dev.unregister_receiver(m_reg);
	}

	void handle_packet(packet const & p)
	{
		if (p[0] != 0)
			return;

		try
		{
			m_buffer.insert(m_buffer.end(), p.begin() + 1, p.end());
			if (p.size() != 16)
			{
				device_descriptor dd = device_descriptor::parse(m_buffer.data(), m_buffer.data() + m_buffer.size());
				m_out.send(task<device_descriptor>::from_value(std::move(dd)));

				m_dev.unregister_receiver(m_reg);
				m_registered = false;
			}
		}
		catch (...)
		{
			m_out.send(task<device_descriptor>::from_exception(std::current_exception()));
			m_dev.unregister_receiver(m_reg);
			m_registered = false;
		}
	}

	device & m_dev;

	std::vector<uint8_t> m_buffer;
	channel<task<device_descriptor>> m_out;

	bool m_registered;
	device::receiver_registration m_reg;
};

}

task<device_descriptor> read_device_descriptor(device & d)
{
	try
	{
		std::shared_ptr<handler> h(new handler(d));
		return d.write_packet(yb::make_packet(0) % 0).then([&d, h]() -> task<device_descriptor> {
			std::shared_ptr<handler> h2(h);
			return h->m_out.receive().then([h2](task<device_descriptor> t) { return t; });
		});
	}
	catch (...)
	{
		return async::raise<device_descriptor>();
	}
}

} // namespace yb
