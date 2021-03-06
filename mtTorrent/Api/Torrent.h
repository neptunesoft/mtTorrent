#pragma once

#include "Public/Status.h"
#include "Api/Interface.h"
#include "Api/FileTransfer.h"
#include "Api/Peers.h"
#include "Api/MagnetDownload.h"
#include <memory>


namespace mttApi
{
	class Torrent
	{
	public:

		API_EXPORT bool start();
		API_EXPORT void stop();

		enum class State
		{
			Stopped,
			Started,
			DownloadUtm,
		};

		API_EXPORT State getStatus();
		API_EXPORT mtt::Status getLastError();
		API_EXPORT float checkingProgress();
		API_EXPORT void checkFiles();

		API_EXPORT mtt::DownloadSelection getFilesSelection();
		API_EXPORT std::vector<float> getFilesProgress();
		API_EXPORT bool selectFiles(const std::vector<bool>&);
		API_EXPORT void setFilesPriority(const std::vector<mtt::Priority>&);
		API_EXPORT std::string getLocationPath();
		API_EXPORT mtt::Status setLocationPath(const std::string& path);

		API_EXPORT std::string name();
		API_EXPORT float currentProgress();
		API_EXPORT float currentSelectionProgress();
		API_EXPORT size_t downloaded();
		API_EXPORT size_t uploaded();
		API_EXPORT size_t dataLeft();
		API_EXPORT bool finished();
		API_EXPORT bool selectionFinished();

		API_EXPORT const mtt::TorrentFileInfo& getFileInfo();
		API_EXPORT std::shared_ptr<Peers> getPeers();
		API_EXPORT std::shared_ptr<mttApi::FileTransfer> getFileTransfer();
		API_EXPORT std::shared_ptr<mttApi::MagnetDownload> getMagnetDownload();

		API_EXPORT bool getPiecesBitfield(uint8_t* dataBitfield, size_t dataSize);
		API_EXPORT bool getReceivedPieces(uint32_t* dataPieces, size_t& dataSize);
	};
}
