#include "PeerCommunication.h"
#include "utils/PacketHelper.h"
#include "Configuration.h"

#define BT_LOG(x) {} WRITE_LOG("PEER " << stream->getName() << ": " << x)

using namespace mtt;

namespace mtt
{
	namespace bt
	{
		DataBuffer createHandshake(uint8_t* torrentHash, uint8_t* clientHash)
		{
			PacketBuilder packet(70);
			packet.add(19);
			packet.add("BitTorrent protocol", 19);

			uint8_t reserved_byte[8] = { 0 };
			reserved_byte[5] |= 0x10;	//Extension Protocol

			if (mtt::config::external.enableDht)
				reserved_byte[7] |= 0x80;

			packet.add(reserved_byte, 8);

			packet.add(torrentHash, 20);
			packet.add(clientHash, 20);

			return packet.getBuffer();
		}

		DataBuffer createStateMessage(PeerMessageId id)
		{
			PacketBuilder packet(5);
			packet.add32(1);
			packet.add(id);

			return packet.getBuffer();
		}

		DataBuffer createBlockRequest(PieceBlockInfo& block)
		{
			PacketBuilder packet(17);
			packet.add32(13);
			packet.add(Request);
			packet.add32(block.index);
			packet.add32(block.begin);
			packet.add32(block.length);

			return packet.getBuffer();
		}

		DataBuffer createHave(uint32_t idx)
		{
			PacketBuilder packet(9);
			packet.add32(5);
			packet.add(Have);
			packet.add32(idx);

			return packet.getBuffer();
		}

		DataBuffer createPort(uint16_t port)
		{
			PacketBuilder packet(9);
			packet.add32(3);
			packet.add(Port);
			packet.add16(port);

			return packet.getBuffer();
		}

		DataBuffer createBitfield(DataBuffer& bitfield)
		{
			PacketBuilder packet(5 + (uint32_t)bitfield.size());
			packet.add32(1 + (uint32_t)bitfield.size());
			packet.add(Bitfield);
			packet.add(bitfield.data(), bitfield.size());

			return packet.getBuffer();
		}

		DataBuffer createPiece(PieceBlock& block)
		{
			uint32_t dataSize = 1 + 8 + (uint32_t)block.data.size();
			PacketBuilder packet(4 + dataSize);
			packet.add32(dataSize);
			packet.add(Piece);
			packet.add32(block.info.index);
			packet.add32(block.info.begin);
			packet.add(block.data.data(), block.data.size());

			return packet.getBuffer();
		}
	}
}

mtt::PeerInfo::PeerInfo()
{
	memset(id, 0, 20);
	memset(protocol, 0, 8);
}

bool mtt::PeerInfo::supportsExtensions()
{
	return (protocol[5] & 0x10) != 0;
}

bool mtt::PeerInfo::supportsDht()
{
	return (protocol[8] & 0x80) != 0;
}

PeerCommunication::PeerCommunication(TorrentInfo& t, std::shared_ptr<TcpAsyncStream> s) : torrent(t)
{
	stream = s;
	state.action = PeerCommunicationState::Connected;
	
	initializeCallbacks();
	dataReceived();
}

PeerCommunication::PeerCommunication(TorrentInfo& t, boost::asio::io_service& io_service) : torrent(t)
{
	stream = std::make_shared<TcpAsyncStream>(io_service);
	initializeCallbacks();
}

void mtt::PeerCommunication::initializeCallbacks()
{
	stream->onConnectCallback = std::bind(&PeerCommunication::connectionOpened, this);
	stream->onCloseCallback = std::bind(&PeerCommunication::connectionClosed, this);
	stream->onReceiveCallback = std::bind(&PeerCommunication::dataReceived, this);
	ext.stream = stream;

	ext.pex.onPexMessage = [this](mtt::ext::PeerExchange::Message& msg)
	{
		std::lock_guard<std::mutex> guard(listenerMutex);
		listener->pexReceived(this, msg);
	};
	ext.utm.onUtMetadataMessage = [this](mtt::ext::UtMetadata::Message& msg)
	{
		std::lock_guard<std::mutex> guard(listenerMutex);
		listener->metadataPieceReceived(this, msg);
	};
}

void PeerCommunication::sendHandshake(Addr& address)
{
	if (state.action != PeerCommunicationState::Disconnected)
		resetState();

	state.action = PeerCommunicationState::Connecting;
	stream->connect(address.addrBytes, address.port, address.ipv6);
}

void mtt::PeerCommunication::sendHandshake()
{
	if (!state.finishedHandshake && state.action == PeerCommunicationState::Connected)
	{
		state.action = PeerCommunicationState::Handshake;
		stream->write(mtt::bt::createHandshake(torrent.hash, mtt::config::internal_.hashId));
	}
}

void PeerCommunication::dataReceived()
{
	std::lock_guard<std::mutex> guard(read_mutex);

	auto message = readNextStreamMessage();

	while (message.id != Invalid)
	{
		handleMessage(message);
		message = readNextStreamMessage();
	}
}

void PeerCommunication::connectionOpened()
{
	state.action = PeerCommunicationState::Connected;

	sendHandshake();
}

void mtt::PeerCommunication::stop()
{
	if (state.action != PeerCommunicationState::Disconnected)
	{
		state.action = PeerCommunicationState::Disconnected;
		stream->close();
	}
}

Addr mtt::PeerCommunication::getAddress()
{
	Addr out;
	out.set(stream->getEndpoint().address(), stream->getEndpoint().port());
	return out;
}

void mtt::PeerCommunication::setListener(IPeerListener* l)
{
	std::lock_guard<std::mutex> guard(listenerMutex);
	listener = l;
}

void mtt::PeerCommunication::connectionClosed()
{
	state.action = PeerCommunicationState::Disconnected;
	{
		std::lock_guard<std::mutex> guard(listenerMutex);
		listener->connectionClosed(this);
	}
}

mtt::PeerMessage mtt::PeerCommunication::readNextStreamMessage()
{
	auto data = stream->getReceivedData();
	PeerMessage msg(data);

	if (msg.id != Invalid)
		stream->consumeData(msg.messageSize);
	else if (!msg.messageSize)
		stream->consumeData(data.size());

	return msg;
}

mtt::PeerCommunication::~PeerCommunication()
{
	stream->close();
}

void mtt::PeerCommunication::setInterested(bool enabled)
{
	if (!isEstablished())
		return;

	if (state.amInterested == enabled)
		return;

	state.amInterested = enabled;
	stream->write(mtt::bt::createStateMessage(enabled ? Interested : NotInterested));
}

void mtt::PeerCommunication::setChoke(bool enabled)
{
	if (!isEstablished())
		return;

	if (state.amChoking == enabled)
		return;

	state.amChoking = enabled;
	stream->write(mtt::bt::createStateMessage(enabled ? Choke : Unchoke));
}

void mtt::PeerCommunication::requestPieceBlock(PieceBlockInfo& pieceInfo)
{
	if (!isEstablished())
		return;

	{
		std::lock_guard<std::mutex> guard(requestsMutex);
		requestedBlocks.push_back(pieceInfo);
	}

	stream->write(mtt::bt::createBlockRequest(pieceInfo));
}

bool mtt::PeerCommunication::isEstablished()
{
	return state.action == PeerCommunicationState::Established;
}

void mtt::PeerCommunication::sendHave(uint32_t pieceIdx)
{
	if (!isEstablished())
		return;

	stream->write(mtt::bt::createHave(pieceIdx));
}

void mtt::PeerCommunication::sendPieceBlock(PieceBlock& block)
{
	if (!isEstablished())
		return;

	stream->write(mtt::bt::createPiece(block));
}

void mtt::PeerCommunication::sendBitfield(DataBuffer& bitfield)
{
	if (!isEstablished())
		return;

	stream->write(mtt::bt::createBitfield(bitfield));
}

void mtt::PeerCommunication::resetState()
{
	state = PeerCommunicationState();
	info = PeerInfo();
}

void mtt::PeerCommunication::sendPort(uint16_t port)
{
	if (!isEstablished())
		return;

	stream->write(mtt::bt::createPort(port));
}

void mtt::PeerCommunication::handleMessage(PeerMessage& message)
{
	if (message.id != Piece)
		BT_LOG("MSG_ID:" << (int)message.id << ", size: " << message.messageSize);

	if (message.id == Bitfield)
	{
		info.pieces.fromBitfield(message.bitfield, torrent.pieces.size());

		BT_LOG("new percentage: " << std::to_string(info.pieces.getPercentage()) << "\n");

		{
			std::lock_guard<std::mutex> guard(listenerMutex);
			listener->progressUpdated(this);
		}
	}
	else if (message.id == Have)
	{
		info.pieces.addPiece(message.havePieceIndex);

		BT_LOG("new percentage: " << std::to_string(info.pieces.getPercentage()) << "\n");

		{
			std::lock_guard<std::mutex> guard(listenerMutex);
			listener->progressUpdated(this);
		}
	}
	else if (message.id == Piece)
	{
		bool success = false;

		{
			std::lock_guard<std::mutex> guard(requestsMutex);

			for (auto it = requestedBlocks.begin(); it != requestedBlocks.end(); it++)
			{
				if (it->index == message.piece.info.index && it->begin == message.piece.info.begin)
				{
					success = true;
					requestedBlocks.erase(it);
					break;
				}
			}
		}

		if (!success)
		{
			BT_LOG("Unrequested block!");
			message.piece.info.index = -1;
		}
	}
	else if (message.id == Unchoke)
	{
		state.peerChoking = false;
	}
	else if (message.id == Choke)
	{
		state.peerChoking = true;
	}
	else if (message.id == NotInterested)
	{
		state.peerInterested = true;
	}
	else if (message.id == Interested)
	{
		state.peerInterested = false;
	}
	else if (message.id == Extended)
	{
		auto type = ext.load(message.extended.id, message.extended.data);

		BT_LOG("ext message " << (int)type);

		if (type == mtt::ext::HandshakeEx)
		{
			std::lock_guard<std::mutex> guard(listenerMutex);
			listener->extHandshakeFinished(this);
		}
	}
	else if (message.id == Handshake)
	{
		if (state.action == PeerCommunicationState::Handshake || state.action == PeerCommunicationState::Connected)
		{
			if (!state.finishedHandshake)
			{
				if(state.action == PeerCommunicationState::Connected)
					stream->write(mtt::bt::createHandshake(torrent.hash, mtt::config::internal_.hashId));

				state.action = PeerCommunicationState::Established;
				state.finishedHandshake = true;
				BT_LOG("finished handshake");

				memcpy(info.id, message.handshake.peerId, 20);
				memcpy(info.protocol, message.handshake.reservedBytes, 8);

				if (info.supportsExtensions())
					ext.sendHandshake();

				if (info.supportsDht() && mtt::config::external.enableDht)
					sendPort(mtt::config::external.udpPort);

				{
					std::lock_guard<std::mutex> guard(listenerMutex);
					listener->handshakeFinished(this);
				}
			}
			else
				state.action = PeerCommunicationState::Established;
		}
	}

	{
		std::lock_guard<std::mutex> guard(listenerMutex);
		listener->messageReceived(this, message);
	}
}
