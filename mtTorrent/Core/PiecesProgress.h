#pragma once
#include "Interface.h"

namespace mtt
{
	struct PiecesProgress
	{
		void init(size_t size);

		void fromSelection(DownloadSelection& selection);
		void fromBitfield(DataBuffer& bitfield, size_t piecesCount);
		void fromList(std::vector<uint8_t>& pieces);
		DataBuffer toBitfield();

		bool empty();
		float getPercentage();

		void addPiece(uint32_t index);
		void removePiece(uint32_t index);
		bool hasPiece(uint32_t index);
		uint32_t firstEmptyPiece();

		std::map<uint32_t, bool> pieces;

	private:

		size_t piecesStartCount = 0;
	};
}
