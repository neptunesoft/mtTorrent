#include "Interface2.h"

using namespace mtt;

bool DownloadedPiece::isValid(char* expectedHash)
{
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((const unsigned char*)data.data(), dataSize, hash);

	return memcmp(hash, expectedHash, SHA_DIGEST_LENGTH) == 0;
}

void DownloadedPiece::reset(size_t maxPieceSize)
{
	data.resize(maxPieceSize);
	dataSize = 0;
	receivedBlocks = 0;
	index = -1;
}

void DownloadedPiece::addBlock(PieceBlock& block)
{
	receivedBlocks++;
	dataSize += block.info.length;
	memcpy(&data[0] + block.info.begin, block.data.data(), block.info.length);
}

mtt::Addr::Addr()
{
	memset(addrBytes, 0, 16);
}

mtt::Addr::Addr(uint32_t ip, uint16_t port)
{
	set(ip, port);
}

mtt::Addr::Addr(uint8_t* ip, uint16_t port, bool isIpv6)
{
	set(ip, port, isIpv6);
}

mtt::Addr::Addr(uint8_t* buffer, bool v6)
{
	parse(buffer, v6);
}

void mtt::Addr::set(uint8_t* ip, uint16_t p, bool isIpv6)
{
	memcpy(addrBytes, ip, isIpv6 ? 16 : 4);
	port = port;
	ipv6 = isIpv6;
}

void mtt::Addr::set(uint32_t ip, uint16_t port)
{
	memcpy(addrBytes, &ip, 4);
	port = port;
	ipv6 = false;
}

void mtt::Addr::set(DataBuffer ip, uint16_t port)
{
	memcpy(addrBytes, ip.data(), ip.size());
	port = port;
	ipv6 = ip.size() > 4;
}

size_t mtt::Addr::parse(uint8_t* buffer, bool v6)
{
	size_t addrSize = v6 ? 16 : 4;
	memcpy(addrBytes, buffer, addrSize);

	/*if (!v6)
	{
		uint8_t ipAddr[4];
		ipAddr[0] = *reinterpret_cast<uint8_t*>(buffer);
		ipAddr[1] = *(reinterpret_cast<uint8_t*>(buffer + 1));
		ipAddr[2] = *(reinterpret_cast<uint8_t*>(buffer + 2));
		ipAddr[3] = *(reinterpret_cast<uint8_t*>(buffer + 3));

		str.resize(16);
		str.resize(sprintf_s(&str[0], 16, "%d.%d.%d.%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]));
	}
	else
	{
		str.resize(40);
		sprintf_s(&str[0], 40, "%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
			addrBytes[0], addrBytes[1], addrBytes[2], addrBytes[3], addrBytes[4], addrBytes[5], addrBytes[6], addrBytes[7],
			addrBytes[8], addrBytes[9], addrBytes[10], addrBytes[11], addrBytes[12], addrBytes[13], addrBytes[14], addrBytes[15]);
		str.resize(39);
	}*/

	port = _byteswap_ushort(*reinterpret_cast<const uint16_t*>(buffer + addrSize));

	return 2 + addrSize;
}
