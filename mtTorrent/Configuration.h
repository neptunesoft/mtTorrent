#pragma once
#include <string>

namespace mtt
{
	namespace config
	{
		struct External
		{
			uint32_t listenPort = 55125;
			uint32_t maxPeersPerRequest = 100;

			std::string defaultDirectory;
		};

		struct Internal
		{
			uint8_t hashId[20];
			uint32_t key = 1111;
		};

		extern External external;
		extern Internal internal;
	}
}