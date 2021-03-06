#pragma once

#include "utils/ServiceThreadpool.h"
#include "PeerCommunication.h"
#include "UdpTrackerComm.h"
#include "utils/ScheduledTimer.h"
#include "utils/Uri.h"

namespace mtt
{
	struct TrackerManager
	{
	public:

		TrackerManager(TorrentPtr t);

		using AnnounceCallback = std::function<void(Status, const AnnounceResponse*, Tracker*)>;
		void start(AnnounceCallback announceCallback);
		void stop();

		void addTracker(std::string addr);
		void addTrackers(const std::vector<std::string>& trackers);
		void removeTrackers();

		std::shared_ptr<Tracker> getTrackerByAddr(const std::string& addr);
		std::vector<std::pair<std::string,std::shared_ptr<Tracker>>> getTrackers();

		uint32_t getTrackersCount();

	private:

		struct TrackerInfo
		{
			std::shared_ptr<Tracker> comm;
			std::shared_ptr<ScheduledTimer> timer;

			Uri uri;
			std::string fullAddress;
			bool httpFallback = false;

			bool httpFallbackUsed = false;
			uint32_t retryCount = 0;
		};
		std::vector<TrackerInfo> trackers;
		std::mutex trackersMutex;

		void start(TrackerInfo*);
		void startNext();
		void stopAll();
		TrackerInfo* findTrackerInfo(Tracker*);
		TrackerInfo* findTrackerInfo(std::string host);

		void onAnnounce(AnnounceResponse&, Tracker*);
		void onTrackerFail(Tracker*);

		TorrentPtr torrent;
		AnnounceCallback announceCallback;
	};
}