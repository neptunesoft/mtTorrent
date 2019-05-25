#include "UdpAsyncWriter.h"

using UdpPacketCallback = std::function<void(udp::endpoint&, DataBuffer&)>;

class UdpAsyncReceiver : public std::enable_shared_from_this<UdpAsyncReceiver>
{
public:

	UdpAsyncReceiver(asio::io_service& io_service, uint16_t port, bool ipv6);

	void listen();
	void stop();

	UdpPacketCallback receiveCallback;

private:

	void handle_receive(const std::error_code& error, std::size_t bytes_transferred);

	bool active = false;
	udp::socket socket_;
	udp::endpoint remote_endpoint_;

	const size_t BufferSize = 2 * 1024;
	DataBuffer buffer;

};
