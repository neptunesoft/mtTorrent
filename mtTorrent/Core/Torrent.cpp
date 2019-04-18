#include "Torrent.h"
#include "utils/TorrentFileParser.h"
#include "MetadataDownload.h"
#include "Peers.h"
#include "Configuration.h"
#include "FileTransfer.h"

mtt::TorrentPtr mtt::Torrent::fromFile(std::string filepath)
{
	mtt::TorrentPtr torrent = std::make_shared<Torrent>();
	torrent->infoFile = mtt::TorrentFileParser::parseFile(filepath.data());

	if (!torrent->infoFile.info.name.empty())
	{
		torrent->peers = std::make_unique<Peers>(torrent);
		torrent->fileTransfer = std::make_unique<FileTransfer>(torrent);
		torrent->init();
		return torrent;
	}
	else
		return nullptr;
}

mtt::TorrentPtr mtt::Torrent::fromMagnetLink(std::string link)
{
	mtt::TorrentPtr torrent = std::make_shared<Torrent>();
	if (torrent->infoFile.parseMagnetLink(link) != Status::Success)
		return nullptr;

	torrent->peers = std::make_unique<Peers>(torrent);
	torrent->fileTransfer = std::make_unique<FileTransfer>(torrent);

	return torrent;
}

void mtt::Torrent::downloadMetadata(std::function<void(Status, MetadataDownloadState&)> callback)
{
	state = State::DownloadUtm;
	utmDl = std::make_unique<MetadataDownload>(*peers);
	utmDl->start([this, callback](Status s, MetadataDownloadState& state)
	{
		if (s == Status::Success && state.finished)
		{
			infoFile.info = utmDl->metadata.getRecontructedInfo();
			init();
		}

		if (state.finished)
		{
			this->state = State::Stopped;
		}

		callback(s, state);
	}
	);
}

void mtt::Torrent::init()
{
	files.init(infoFile.info);
}

bool mtt::Torrent::start()
{
	lastError = Status::E_InvalidInput;

	if (files.selection.files.empty())
		return false;

	lastError = files.prepareSelection();

	if (lastError != mtt::Status::Success)
		return false;

	service.start(2);

	state = State::Started;

	if (checking)
		return true;

	fileTransfer->start();

	return true;
}

void mtt::Torrent::pause()
{

}

void mtt::Torrent::stop()
{
	if (utmDl)
	{
		utmDl->stop();
	}

	if (fileTransfer)
	{
		fileTransfer->stop();
	}

	service.stop();
	state = State::Stopped;
	lastError = Status::Success;
}

std::shared_ptr<mtt::PiecesCheck> mtt::Torrent::checkFiles(std::function<void(std::shared_ptr<PiecesCheck>)> onFinish)
{
	auto checkFunc = [this, onFinish](std::shared_ptr<PiecesCheck> check)
	{
		{
			std::lock_guard<std::mutex> guard(checkStateMutex);
			checkState.reset();
		}

		checking = false;

		if (!check->rejected)
		{
			files.progress.fromList(check->pieces);
		}

		if (state == State::Started)
			start();

		onFinish(check);
	};

	checking = true;
	std::lock_guard<std::mutex> guard(checkStateMutex);
	checkState = files.storage.checkStoredPiecesAsync(infoFile.info.pieces, service.io, checkFunc);
	return checkState;
}

float mtt::Torrent::checkingProgress()
{
	std::lock_guard<std::mutex> guard(checkStateMutex);

	if (checkState)
		return checkState->piecesChecked / (float)checkState->piecesCount;
	else
		return 1;
}

std::shared_ptr<mtt::PiecesCheck> mtt::Torrent::getCheckState()
{
	std::lock_guard<std::mutex> guard(checkStateMutex);

	return checkState;
}

bool mtt::Torrent::finished()
{
	return files.progress.getPercentage() == 1;
}

uint8_t* mtt::Torrent::hash()
{
	return infoFile.info.hash;
}

std::string mtt::Torrent::name()
{
	return infoFile.info.name;
}

float mtt::Torrent::currentProgress()
{
	return files.progress.getPercentage();
}

size_t mtt::Torrent::downloaded()
{
	return (size_t) (infoFile.info.fullSize*files.progress.getPercentage());
}

size_t mtt::Torrent::downloadSpeed()
{
	return fileTransfer ? fileTransfer->getDownloadSpeed() : 0;
}

size_t mtt::Torrent::uploaded()
{
	return fileTransfer ? fileTransfer->getUploadSum() : 0;
}

size_t mtt::Torrent::uploadSpeed()
{
	return fileTransfer ? fileTransfer->getUploadSpeed() : 0;
}

size_t mtt::Torrent::dataLeft()
{
	return infoFile.info.fullSize - downloaded();
}
