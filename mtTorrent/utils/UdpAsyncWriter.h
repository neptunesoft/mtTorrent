#pragma once

#include "utils\Network.h"
#include <mutex>
#include <future>
#include <memory>
#include <array>
#include <functional>
#include <deque>

class UdpAsyncReceiver;
class UdpAsyncWriter;
using UdpRequest = std::shared_ptr<UdpAsyncWriter>;

class UdpAsyncWriter : public std::enable_shared_from_this<UdpAsyncWriter>
{
	friend class UdpAsyncReceiver;

public:

	UdpAsyncWriter(asio::io_service& io_service);
	~UdpAsyncWriter();

	void setAddress(Addr& addr);
	void setAddress(udp::endpoint& addr);
	void setAddress(const std::string& hostname, const std::string& port);
	void setAddress(const std::string& hostname, const std::string& port, bool ipv6);
	void setBindPort(uint16_t port);

	std::string getName();
	udp::endpoint& getEndpoint();

	void write(const DataBuffer& data);
	void write();
	void close();

	std::function<void(UdpRequest)> onCloseCallback;

protected:

	std::mutex stateMutex;
	enum { Clear, Initialized, Connected } state = Clear;

	void postFail(std::string place, const std::error_code& error);

	void handle_resolve(const std::error_code& error, udp::resolver::iterator iterator, std::shared_ptr<udp::resolver> resolver);
	void handle_connect(const std::error_code& err);
	
	void do_write(DataBuffer data);
	void do_rewrite();
	void do_close();

	DataBuffer messageBuffer;

	void send_message();
	void handle_write(const std::error_code& error, size_t sz);

	uint16_t bindPort = 0;

	udp::endpoint target_endpoint;
	udp::socket socket;
	asio::io_service& io_service;

	std::string hostname;
	std::string port;

};
