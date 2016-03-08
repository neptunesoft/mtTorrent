#include "TcpStream.h"
#include <iostream>

TcpStream::TcpStream() : resolver(io_service)
{

}

void TcpStream::setTarget(const char* hostname, const char* p)
{ 
	host = hostname;
	port = p;
}

bool TcpStream::active()
{
	return connected();
}

void TcpStream::close()
{
	std::lock_guard<std::mutex> guard(socket_mutex);

	socket->close();
}

void TcpStream::ensureConnection()
{
	std::lock_guard<std::mutex> guard(socket_mutex);

	if (!socket || !connected())
	{
		socket = std::make_unique<tcp::socket>(io_service);
		connect(host.c_str(), port.c_str());
	}
}

void TcpStream::connect(const char* host, const char* port)
{
	openTcpSocket(*socket, resolver, host, port);

	std::thread(&TcpStream::socketListening, this).detach();
}

bool TcpStream::connected()
{
	return socket->is_open();
}

void TcpStream::write(std::vector<char> data)
{
	boost::asio::streambuf dBuffer;
	dBuffer.sputn(data.data(), data.size());

	try
	{
		ensureConnection();

		boost::asio::write(*socket, dBuffer);
	}
	catch (const std::exception&e)
	{
		std::cout << "Socket write exception: " << e.what() << "\n";
		close();
	}
}

void TcpStream::socketListening()
{
	try
	{
		while (connected())
		{
			blockingRead();
		}
	}
	catch (const std::exception&e)
	{
		std::cout << "Socket listening exception: " << e.what() << "\n";
		close();
	}
}

void TcpStream::blockingRead()
{
	boost::asio::streambuf response;
	boost::system::error_code error;

	size_t len = boost::asio::read(*socket, response, error);

	if (len > 0)
	{
		std::istream response_stream(&response);
		std::vector<char> buffer;

		buffer.resize(len);
		response_stream.read(&buffer[0], len);

		appendData(buffer);
	}
	else
	{
		if (error == boost::asio::error::eof)
		{
			close();
		}

		throw boost::system::system_error(error);
	}		
}

std::vector<char> TcpStream::getReceivedData()
{
	std::lock_guard<std::mutex> guard(buffer_mutex);

	return buffer;
}

void TcpStream::appendData(std::vector<char>& data)
{
	std::lock_guard<std::mutex> guard(buffer_mutex);

	buffer.insert(buffer.end(), data.begin(), data.end());
}

void TcpStream::consumeData(size_t size)
{
	std::lock_guard<std::mutex> guard(buffer_mutex);

	buffer.erase(buffer.begin(), buffer.begin() + size);
}

void TcpStream::resetData()
{
	std::lock_guard<std::mutex> guard(buffer_mutex);

	buffer.clear();
}

std::vector<char> TcpStream::sendBlockingRequest(std::vector<char>& data)
{
	try
	{
		if (!active())
			openTcpSocket(*socket, resolver, host.c_str(), port.c_str());

		return sendTcpRequest(*socket, data);
	}
	catch (const std::exception&e)
	{
		std::cout << "Socket request exception: " << e.what() << "\n";
		close();
	}

	return{};
}
