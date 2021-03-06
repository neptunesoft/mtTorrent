#include "FileTransfer.h"

size_t mttApi::FileTransfer::getDownloadSpeed()
{
	return static_cast<mtt::FileTransfer*>(this)->getDownloadSpeed();
}

size_t mttApi::FileTransfer::getUploadSpeed()
{
	return static_cast<mtt::FileTransfer*>(this)->getUploadSpeed();
}

std::vector<mtt::ActivePeerInfo> mttApi::FileTransfer::getPeersInfo()
{
	return static_cast<mtt::FileTransfer*>(this)->getPeersInfo();
}

std::vector<uint32_t> mttApi::FileTransfer::getCurrentRequests()
{
	return static_cast<mtt::FileTransfer*>(this)->getCurrentRequests();
}

uint32_t mttApi::FileTransfer::getCurrentRequestsCount()
{
	return static_cast<mtt::FileTransfer*>(this)->getCurrentRequestsCount();
}
