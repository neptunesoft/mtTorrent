#pragma once

#include <asio.hpp>
#include <memory>
#include <thread>

class ServiceThreadpool
{
public:

	ServiceThreadpool();
	ServiceThreadpool(uint32_t startWorkers);
	~ServiceThreadpool();

	asio::io_service io;

	void start(uint32_t startWorkers, bool finishAfterWork = false);
	void stop();

	void adjust(uint32_t estimatedWork);

private:

	std::shared_ptr<asio::io_service::work> work;

	std::thread myThreads[20];
	uint32_t workers = 0;
};