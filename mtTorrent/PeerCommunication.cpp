#include "PeerCommunication.h"
#include "PacketHelper.h"
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

PeerCommunication::PeerCommunication(TorrentInfo& t, IPeerListener& l, boost::asio::io_service& io_service, std::shared_ptr<TcpAsyncStream> s) : torrent(t), listener(l)
{
	if(!s)
		stream = std::make_shared<TcpAsyncStream>(io_service);
	else
	{
		stream = s;
		state.action = PeerCommunicationState::Connected;
	}

	stream->onConnectCallback = std::bind(&PeerCommunication::connectionOpened, this);
	stream->onCloseCallback = std::bind(&PeerCommunication::connectionClosed, this);
	stream->onReceiveCallback = std::bind(&PeerCommunication::dataReceived, this);

	ext.pex.onPexMessage = std::bind(&mtt::IPeerListener::pexReceived, &listener, this, std::placeholders::_1);
	ext.utm.onUtMetadataMessage = std::bind(&mtt::IPeerListener::metadataPieceReceived, &listener, this, std::placeholders::_1);
	ext.stream = stream;

	if (s)
		dataReceived();
}

void PeerCommunication::sendHandshake(Addr& address)
{
	if (state.action == PeerCommunicationState::Disconnected)
	{
		resetState();

		state.action = PeerCommunicationState::Connecting;
		stream->connect(address.addrBytes, address.port, address.ipv6);
	}
}

void mtt::PeerCommunication::sendHandshake()
{
	if (!state.finishedHandshake && state.action == PeerCommunicationState::Connected)
	{
		state.action = PeerCommunicationState::Handshake;
		stream->write(mtt::bt::createHandshake(torrent.hash, mtt::config::internal.hashId));
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

void mtt::PeerCommunication::connectionClosed()
{
	state.action = PeerCommunicationState::Disconnected;
	listener.connectionClosed(this);
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
	if (state.action <= PeerCommunicationState::Handshake)
		return;

	if (state.amInterested == enabled)
		return;

	state.amInterested = enabled;
	stream->write(mtt::bt::createStateMessage(enabled ? Interested : NotInterested));
}

void mtt::PeerCommunication::setChoke(bool enabled)
{
	if (state.action <= PeerCommunicationState::Handshake)
		return;

	if (state.amChoking == enabled)
		return;

	state.amChoking = enabled;
	stream->write(mtt::bt::createStateMessage(enabled ? Choke : Unchoke));
}

bool mtt::PeerCommunication::requestPiece(PieceDownloadInfo& pieceInfo)
{
	if (state.action != PeerCommunicationState::Idle)
		return false;

	if (!info.pieces.hasPiece(pieceInfo.index))
		return false;

	std::lock_guard<std::mutex> guard(schedule_mutex);

	scheduledPieceInfo = pieceInfo;
	downloadingPiece.reset(torrent.pieceSize);
	downloadingPiece.index = scheduledPieceInfo.index;

	requestPieceBlock();

	return true;
}

bool mtt::PeerCommunication::isDownloading()
{
	return state.action == PeerCommunicationState::TransferringData;
}

void mtt::PeerCommunication::sendHave(uint32_t pieceIdx)
{
	if (state.action <= PeerCommunicationState::Handshake)
		return;

	stream->write(mtt::bt::createHave(pieceIdx));
}

void mtt::PeerCommunication::sendPieceBlock(PieceBlock& block)
{
	if (state.action <= PeerCommunicationState::Handshake)
		return;

	stream->write(mtt::bt::createPiece(block));
}

void mtt::PeerCommunication::sendBitfield(DataBuffer& bitfield)
{
	if (state.action <= PeerCommunicationState::Handshake)
		return;

	stream->write(mtt::bt::createBitfield(bitfield));
}

void mtt::PeerCommunication::requestPieceBlock()
{
	auto& b = scheduledPieceInfo.blocksLeft.back();
	scheduledPieceInfo.blocksLeft.pop_back();

	state.action = PeerCommunicationState::TransferringData;
	stream->write(mtt::bt::createBlockRequest(b));
}

void mtt::PeerCommunication::resetState()
{
	state = PeerCommunicationState();
	info = PeerInfo();
}

void mtt::PeerCommunication::handleMessage(PeerMessage& message)
{
	if (message.id != Piece)
		BT_LOG("MSG_ID:" << (int)message.id << ", size: " << message.messageSize);

	if (message.id == KeepAlive)
	{
	}
	else if (message.id == Bitfield)
	{
		info.pieces.fromBitfield(message.bitfield, torrent.pieces.size());

		BT_LOG("new percentage: " << std::to_string(info.pieces.getPercentage()) << "\n");

		listener.progressUpdated(this);
	}
	else if (message.id == Have)
	{
		info.pieces.addPiece(message.havePieceIndex);

		BT_LOG("new percentage: " << std::to_string(info.pieces.getPercentage()) << "\n");

		listener.progressUpdated(this);
	}
	else if (message.id == Piece)
	{
		bool finished = false;
		bool success = false;

		{
			std::lock_guard<std::mutex> guard(schedule_mutex);

			if (message.piece.info.index != downloadingPiece.index)
			{
				BT_LOG("Invalid block!")
				finished = true;
			}
			else
			{
				BT_LOG("Piece " << message.piece.info.index << ", offset: " << message.piece.info.begin << ", size: " << message.piece.info.length);
				downloadingPiece.addBlock(message.piece);

				if (downloadingPiece.receivedBlocks == scheduledPieceInfo.blocksCount)
				{
					finished = success = true;
					BT_LOG("Finished piece " << message.piece.info.index);
				}
				else
				{
					requestPieceBlock();
				}
			}
		}

		if (finished)
		{
			state.action = PeerCommunicationState::Idle;
			listener.pieceReceiveFinished(this, success ? &downloadingPiece : nullptr);
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
			listener.extHandshakeFinished(this);
	}
	else if (message.id == Handshake)
	{
		if (state.action == PeerCommunicationState::Handshake || state.action == PeerCommunicationState::Connected)
		{
			if (!state.finishedHandshake)
			{
				if(state.action == PeerCommunicationState::Connected)
					stream->write(mtt::bt::createHandshake(torrent.hash, mtt::config::internal.hashId));

				state.finishedHandshake = true;
				BT_LOG("finished handshake");

				memcpy(info.id, message.handshake.peerId, 20);
				memcpy(info.protocol, message.handshake.reservedBytes, 8);

				if (info.supportsExtensions())
					ext.sendHandshake();

				state.action = PeerCommunicationState::Idle;

				listener.handshakeFinished(this);
			}
			else
				state.action = PeerCommunicationState::Idle;
		}
	}
	else if (message.id == Request)
	{
	}
	else if (message.id == Cancel)
	{
	}

	listener.messageReceived(this, message);
}
