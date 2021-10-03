/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
SOTMarker::SOTMarker(void) : m_psot_location(0) {}
bool SOTMarker::write_psot(IBufferedStream* stream, uint32_t tileLength)
{
	if(m_psot_location)
	{
		auto currentLocation = stream->tell();
		stream->seek(m_psot_location);
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
	if(!stream->writeShort((uint16_t)proc->m_tileIndex))
		return false;

	/* Psot  */
	if(tileLength)
	{
		if(!stream->writeInt(tileLength))
			return false;
	}
	else
	{
		m_psot_location = stream->tell();
		if(!stream->skip(4))
			return false;
	}

	/* TPsot */
	if(!stream->writeByte(proc->m_tilePartIndexCounter))
		return false;

	/* TNsot */
	if(!stream->writeByte(proc->m_cp->tcps[proc->m_tileIndex].m_numTileParts))
		return false;

	return true;
}

bool SOTMarker::read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint32_t headerSize,
					 uint32_t* tilePartLength, uint8_t* tilePartIndex, uint8_t* numTileParts)
{
	assert(headerData != nullptr);
	if(headerSize != sot_marker_segment_len - grk_marker_length)
	{
		GRK_ERROR("Error reading next SOT marker");
		return false;
	}
	uint32_t len;
	uint16_t tileIndex;
	uint8_t tile_part_index, num_tile_parts;
	grk_read<uint16_t>(headerData, &tileIndex);
	headerData += 2;
	grk_read<uint32_t>(headerData, &len);
	headerData += 4;
	grk_read<uint8_t>(headerData++, &tile_part_index);
	grk_read<uint8_t>(headerData++, &num_tile_parts);

	if(num_tile_parts && (tile_part_index == num_tile_parts))
	{
		GRK_ERROR("Tile part index (%d) must be less than number of tile parts (%d)",
				  tile_part_index, num_tile_parts);
		return false;
	}

	if(!codeStream->allocateProcessor(tileIndex))
		return false;
	*tilePartLength = len;
	*tilePartIndex = tile_part_index;
	*numTileParts = num_tile_parts;

	return true;
}

bool SOTMarker::read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint16_t header_size)
{
	uint32_t tilePartLength = 0;
	uint8_t numTileParts = 0;
	uint8_t currentTilePart;
	uint32_t tile_x, tile_y;

	if(!read(codeStream, headerData, header_size, &tilePartLength, &currentTilePart, &numTileParts))
	{
		GRK_ERROR("Error reading SOT marker");
		return false;
	}
	auto tileIndex = codeStream->currentProcessor()->m_tileIndex;
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
	if(uint8_t(tcp->m_tilePartIndexCounter + 1) != currentTilePart)
	{
		GRK_ERROR("Invalid tile part index for tile number %u. "
				  "Got %u, expected %u",
				  tileIndex, currentTilePart, tcp->m_tilePartIndexCounter + 1);
		return false;
	}
	tcp->m_tilePartIndexCounter++;
	/* PSot should be equal to zero or >=14 or <= 2^32-1 */
	if((tilePartLength != 0) && (tilePartLength < 14))
	{
		if(tilePartLength == sot_marker_segment_len)
		{
			GRK_WARN("Empty SOT marker detected: Psot=%u.", tilePartLength);
		}
		else
		{
			GRK_ERROR("Psot value is not correct regards to the JPEG2000 norm: %u.",
					  tilePartLength);
			return false;
		}
	}

	/* Ref A.4.2: Psot may equal zero if it is the last tile-part of the code stream.*/
	if(!tilePartLength)
		codeStream->getDecompressorState()->lastTilePartInCodeStream = true;

	// ensure that current tile part number read from SOT marker
	// is not larger than total number of tile parts
	if(tcp->m_numTileParts != 0 && currentTilePart >= tcp->m_numTileParts)
	{
		/* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
		GRK_ERROR("Current tile part number (%u) read from SOT marker is greater\n than total "
				  "number of tile-parts (%u).",
				  currentTilePart, tcp->m_numTileParts);
		codeStream->getDecompressorState()->lastTilePartInCodeStream = true;
		return false;
	}

	if(numTileParts != 0)
	{ /* Number of tile-part header is provided by this tile-part header */
		/* Useful to manage the case of textGBR.jp2 file because two values
		 * of TNSot are allowed: the correct numbers of
		 * tile-parts for that tile and zero (A.4.2 of 15444-1 : 2002). */
		if(tcp->m_numTileParts)
		{
			if(currentTilePart >= tcp->m_numTileParts)
			{
				GRK_ERROR("In SOT marker, TPSot (%u) is not valid with regards to the current "
						  "number of tile-part (%u)",
						  currentTilePart, tcp->m_numTileParts);
				codeStream->getDecompressorState()->lastTilePartInCodeStream = true;
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
		tcp->m_numTileParts = numTileParts;
	}

	/* If we know the number of tile part header we check whether we have read the last one*/
	if(tcp->m_numTileParts && (tcp->m_numTileParts == (currentTilePart + 1)))
		/* indicate that we are now ready to read the tile data */
		codeStream->getDecompressorState()->lastTilePartWasRead = true;

	if(!codeStream->getDecompressorState()->lastTilePartInCodeStream)
		/* Keep the size of data to skip after this marker */
		codeStream->currentProcessor()->tilePartDataLength =
			tilePartLength - sot_marker_segment_len;
	else
		codeStream->currentProcessor()->tilePartDataLength = 0;
	codeStream->getDecompressorState()->setState(DECOMPRESS_STATE_TPH);

	/* Check if the current tile is outside the area we want
	 *  to decompress or not, corresponding to the tile index*/
	if(codeStream->tileIndexToDecode() == -1)
		codeStream->getDecompressorState()->skipTileData =
			(tile_x < codeStream->getDecompressorState()->m_start_tile_x_index) ||
			(tile_x >= codeStream->getDecompressorState()->m_end_tile_x_index) ||
			(tile_y < codeStream->getDecompressorState()->m_start_tile_y_index) ||
			(tile_y >= codeStream->getDecompressorState()->m_end_tile_y_index);
	else
		codeStream->getDecompressorState()->skipTileData =
			(tileIndex != (uint32_t)codeStream->tileIndexToDecode());
	auto codeStreamInfo = codeStream->getCodeStreamInfo();

	return !codeStreamInfo ||
		   codeStreamInfo->updateTileInfo(tileIndex, currentTilePart, numTileParts);
}

} /* namespace grk */
