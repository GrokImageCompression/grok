/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
SOTMarker::SOTMarker(void) : psot_location_(0) {}
bool SOTMarker::write_psot(IBufferedStream* stream, uint32_t tileLength)
{
	if(psot_location_)
	{
		auto currentLocation = stream->tell();
		stream->seek(psot_location_);
		if(!stream->writeInt(tileLength))
			return false;
		stream->seek(currentLocation);
	}

	return true;
}

bool SOTMarker::write(TileProcessor* proc, uint32_t tileLength)
{
	auto stream = proc->getStream();
	/* SOT */
	if(!stream->writeShort(J2K_MS_SOT))
		return false;

	/* Lsot */
	if(!stream->writeShort(10))
		return false;
	/* Isot */
	if(!stream->writeShort((uint16_t)proc->getIndex()))
		return false;

	/* Psot  */
	if(tileLength)
	{
		if(!stream->writeInt(tileLength))
			return false;
	}
	else
	{
		psot_location_ = stream->tell();
		if(!stream->skip(4))
			return false;
	}

	/* TPsot */
	if(!stream->writeByte(proc->tilePartCounter_))
		return false;

	/* TNsot */
	if(!stream->writeByte(proc->cp_->tcps[proc->getIndex()].numTileParts_))
		return false;

	return true;
}

bool SOTMarker::read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint32_t headerSize,
					 uint32_t* tilePartLength, uint16_t* tileIndex, uint8_t* tilePartIndex,
					 uint8_t* numTileParts)
{
	assert(headerData != nullptr);
	if(headerSize != sot_marker_segment_len - grk_marker_length)
	{
		GRK_ERROR("Error reading next SOT marker");
		return false;
	}
	uint32_t len;
	uint16_t index;
	uint8_t tp_index, num_tile_parts;
	grk_read<uint16_t>(headerData, &index);
	headerData += 2;
	grk_read<uint32_t>(headerData, &len);
	headerData += 4;
	grk_read<uint8_t>(headerData++, &tp_index);
	grk_read<uint8_t>(headerData++, &num_tile_parts);

	if(num_tile_parts && (tp_index >= num_tile_parts))
	{
		GRK_ERROR("Tile part index (%u) must be less than number of tile parts (%u)", tp_index,
				  num_tile_parts);
		return false;
	}

	if(!codeStream->allocateProcessor(index))
		return false;
	*tilePartLength = len;
	*tileIndex = index;
	*tilePartIndex = tp_index;
	*numTileParts = num_tile_parts;

	return true;
}

bool SOTMarker::read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint16_t header_size)
{
	uint32_t tilePartLength = 0;
	uint8_t numTileParts = 0;
	uint16_t tileIndex;
	uint8_t currentTilePart;
	uint32_t tile_x, tile_y;

	if(!read(codeStream, headerData, header_size, &tilePartLength, &tileIndex, &currentTilePart,
			 &numTileParts))
	{
		GRK_ERROR("Error reading SOT marker");
		return false;
	}
	auto cp = codeStream->getCodingParams();

	/* testcase 2.pdf.SIGFPE.706.1112 */
	if(tileIndex >= cp->t_grid_width * cp->t_grid_height)
	{
		GRK_ERROR("Invalid tile number %u", tileIndex);
		return false;
	}

	auto tcp = &cp->tcps[tileIndex];
	tile_x = tileIndex % cp->t_grid_width;
	tile_y = tileIndex / cp->t_grid_width;

	/* Fixes issue with id_000020,sig_06,src_001958,op_flip4,pos_149 */
	/* of https://github.com/uclouvain/openjpeg/issues/939 */
	/* We must avoid reading the same tile part number twice for a given tile */
	/* to avoid various issues, like grk_j2k_merge_ppt being called several times. */
	/* ISO 15444-1 A.4.2 Start of tile-part (SOT) mandates that tile parts */
	/* should appear in increasing order. */
	if(uint8_t(tcp->tilePartCounter_) != currentTilePart)
	{
		GRK_ERROR("Invalid tile part index for tile number %u. "
				  "Got %u, expected %u",
				  tileIndex, currentTilePart, tcp->tilePartCounter_);
		return false;
	}
	tcp->tilePartCounter_++;

	/* PSot should be equal to zero or >=14 and <= 2^32-1 */
	if((tilePartLength != 0) && (tilePartLength < 14))
	{
		if(tilePartLength != sot_marker_segment_len)
		{
			GRK_ERROR("Illegal Psot value %u", tilePartLength);
			return false;
		}
	}

	/* Ref A.4.2: Psot may equal zero if it is the last tile-part of the code stream.*/
	if(!tilePartLength)
		codeStream->getDecompressorState()->lastTilePartInCodeStream = true;

	// ensure that current tile part number read from SOT marker
	// is not larger than total number of tile parts
	if(tcp->numTileParts_ != 0 && currentTilePart >= tcp->numTileParts_)
	{
		/* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
		GRK_ERROR("Current tile part number (%u) read from SOT marker is greater\n than total "
				  "number of tile-parts (%u).",
				  currentTilePart, tcp->numTileParts_);
		codeStream->getDecompressorState()->lastTilePartInCodeStream = true;
		return false;
	}

	if(numTileParts)
	{ /* Number of tile-part header is provided by this tile-part header */
		/* Useful to manage the case of textGBR.jp2 file because two values
		 * of TNSot are allowed: the correct numbers of
		 * tile-parts for that tile and zero (A.4.2 of 15444-1 : 2002). */
		if(tcp->numTileParts_)
		{
			if(currentTilePart >= tcp->numTileParts_)
			{
				GRK_ERROR("In SOT marker, TPSot (%u) is not valid with regards to the current "
						  "number of tile-part (%u)",
						  currentTilePart, tcp->numTileParts_);
				codeStream->getDecompressorState()->lastTilePartInCodeStream = true;
				return false;
			}
			if(numTileParts != tcp->numTileParts_)
			{
				GRK_ERROR("Invalid number of tile parts for tile number %u. "
						  "Got %u, expected %u as signalled in previous tile part(s).",
						  tileIndex, numTileParts, tcp->numTileParts_);
				return false;
			}
		}
		if(currentTilePart >= numTileParts)
		{
			/* testcase 451.pdf.SIGSEGV.ce9.3723 */
			GRK_ERROR("In SOT marker, TPSot (%u) is not valid with regards to the current "
					  "number of tile-part (header) (%u)",
					  currentTilePart, numTileParts);
			codeStream->getDecompressorState()->lastTilePartInCodeStream = true;
			return false;
		}
		tcp->numTileParts_ = numTileParts;
	}

	/* If we know the number of tile part header we check whether we have read the last one*/
	if(tcp->numTileParts_ && (tcp->numTileParts_ == (currentTilePart + 1)))
		/* indicate that we are now ready to read the tile data */
		codeStream->getDecompressorState()->lastTilePartWasRead = true;

	codeStream->currentProcessor()->setTilePartDataLength(
		tilePartLength, codeStream->getDecompressorState()->lastTilePartInCodeStream);
	codeStream->getDecompressorState()->setState(DECOMPRESS_STATE_TPH);

	/* Check if the current tile is outside the area we want
	 *  to decompress or not, corresponding to the tile index*/
	if(codeStream->tileIndexToDecode() == -1)
		codeStream->getDecompressorState()->skipTileData =
			(tile_x < codeStream->getDecompressorState()->start_tile_x_index_) ||
			(tile_x >= codeStream->getDecompressorState()->end_tile_x_index_) ||
			(tile_y < codeStream->getDecompressorState()->start_tile_y_index_) ||
			(tile_y >= codeStream->getDecompressorState()->end_tile_y_index_);
	else
		codeStream->getDecompressorState()->skipTileData =
			(tileIndex != (uint32_t)codeStream->tileIndexToDecode());
	auto codeStreamInfo = codeStream->getCodeStreamInfo();

	return !codeStreamInfo ||
		   codeStreamInfo->updateTileInfo(tileIndex, currentTilePart, numTileParts);
}

} /* namespace grk */
