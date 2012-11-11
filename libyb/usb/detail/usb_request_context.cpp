#include "usb_request_context.hpp"
using namespace yb;
using namespace yb::detail;

task<size_t> usb_request_context::get_descriptor(HANDLE hFile, uint8_t desc_type, uint8_t desc_index, uint16_t langid, unsigned char * data, int length)
{
	req = libusb0_win32_request();
	req.timeout = 5000;
	req.descriptor.type = desc_type;
	req.descriptor.index = desc_index;
	req.descriptor.language_id = langid;
	return opctx.ioctl(hFile, LIBUSB_IOCTL_GET_DESCRIPTOR, &req, sizeof req, data, length);
}

task<void> usb_request_context::get_device_descriptor(HANDLE hFile, usb_device_descriptor & desc)
{
	return this->get_descriptor(hFile, 1, 0, 0, reinterpret_cast<uint8_t *>(&desc), sizeof desc).then([&desc](size_t r) -> task<void> {
		if (r != sizeof desc)
			return async::raise<void>(std::runtime_error("too short a response"));
		if (desc.bLength != sizeof desc || desc.bDescriptorType != 1)
			return async::raise<void>(std::runtime_error("invalid response"));
		// FIXME: endianity
		return async::value();
	});
}

task<size_t> usb_request_context::bulk_read(HANDLE hFile, usb_endpoint_t ep, uint8_t * buffer, size_t size)
{
	assert((ep & 0x80) != 0);

	req = libusb0_win32_request();
	req.timeout = 5000;
	req.endpoint.endpoint = ep;
	return opctx.ioctl(
		hFile,
		LIBUSB_IOCTL_INTERRUPT_OR_BULK_READ,
		&req,
		sizeof req,
		buffer,
		size);
}

task<size_t> usb_request_context::bulk_write(HANDLE hFile, usb_endpoint_t ep, uint8_t const * buffer, size_t size)
{
	assert((ep & 0x80) == 0);

	req = libusb0_win32_request();
	req.timeout = 5000;
	req.endpoint.endpoint = ep;
	return opctx.ioctl(
		hFile,
		LIBUSB_IOCTL_INTERRUPT_OR_BULK_WRITE,
		&req,
		sizeof req,
		const_cast<uint8_t *>(buffer),
		size);
}

task<size_t> usb_request_context::control_read(HANDLE hFile, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t * buffer, size_t size)
{
	assert((bmRequestType & 0x80) != 0);
	assert((bmRequestType & 0x60) < (3<<5));
	assert((bmRequestType & 0x1f) < 3);

	req = libusb0_win32_request();
	req.timeout = 5000;
	req.vendor.type = (bmRequestType & 0x60) >> 5;
	req.vendor.recipient = (bmRequestType & 0x1f);
	req.vendor.bRequest = bRequest;
	req.vendor.wValue = wValue;
	req.vendor.wIndex = wIndex;

	return opctx.ioctl(
		hFile,
		LIBUSB_IOCTL_VENDOR_READ,
		&req,
		sizeof req,
		buffer,
		size);
}

task<void> usb_request_context::control_write(HANDLE hFile, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t const * buffer, size_t size)
{
	assert((bmRequestType & 0x80) == 0);
	assert((bmRequestType & 0x60) < (3<<5));
	assert((bmRequestType & 0x1f) < 3);

	req = libusb0_win32_request();
	req.timeout = 5000;
	req.vendor.type = (bmRequestType & 0x60) >> 5;
	req.vendor.recipient = (bmRequestType & 0x1f);
	req.vendor.bRequest = bRequest;
	req.vendor.wValue = wValue;
	req.vendor.wIndex = wIndex;

	buf.resize(sizeof(libusb0_win32_request) + size);
	std::copy(reinterpret_cast<uint8_t const *>(&req), reinterpret_cast<uint8_t const *>(&req) + sizeof req, buf.begin());
	std::copy(buffer, buffer + size, buf.begin() + sizeof req);

	return opctx.ioctl(
		hFile,
		LIBUSB_IOCTL_VENDOR_WRITE,
		buf.data(),
		buf.size(),
		0,
		0).ignore_result();
}

task<void> usb_request_context::claim_interface(HANDLE hFile, uint8_t intfno)
{
	req = libusb0_win32_request();
	req.intf.interface_number = intfno;
	return opctx.ioctl(hFile, LIBUSB_IOCTL_CLAIM_INTERFACE, &req, sizeof req, 0, 0).ignore_result();
}

task<void> usb_request_context::release_interface(HANDLE hFile, uint8_t intfno)
{
	req = libusb0_win32_request();
	req.intf.interface_number = intfno;
	return opctx.ioctl(hFile, LIBUSB_IOCTL_RELEASE_INTERFACE, &req, sizeof req, 0, 0).ignore_result();
}