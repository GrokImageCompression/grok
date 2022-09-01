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
 */
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wparentheses"
#endif

#include "simd.h"
#include "coding_units.hpp"
#include "ht_block_decoding.hpp"
#include "ht_block_encoding.hpp"
#include "T1OpenHTJ2K.h"
#include "grk_includes.h"

const uint8_t grk_cblk_dec_compressed_data_pad_ht = 8;

namespace openhtj2k
{
T1OpenHTJ2K::T1OpenHTJ2K(bool isCompressor, [[maybe_unused]] grk::TileCodingParams* tcp,
						 uint32_t maxCblkW, uint32_t maxCblkH)
	: coded_data_size(isCompressor ? 0 : (uint32_t)(maxCblkW * maxCblkH * sizeof(int32_t))),
	  coded_data(isCompressor ? nullptr : new uint8_t[coded_data_size]),
	  unencoded_data_size(maxCblkW * maxCblkH), unencoded_data(new int32_t[unencoded_data_size])
{}
T1OpenHTJ2K::~T1OpenHTJ2K()
{
	delete[] coded_data;
	delete[] unencoded_data;
}
void T1OpenHTJ2K::preCompress([[maybe_unused]] grk::CompressBlockExec* block,
							  [[maybe_unused]] grk::Tile* tile)
{
	auto cblk = block->cblk;
	uint16_t w = (uint16_t)cblk->width();
	uint16_t h = (uint16_t)cblk->height();
	uint32_t tile_width =
		(tile->comps + block->compno)->getWindow()->getResWindowBufferHighestStride();
	auto tileLineAdvance = tile_width - w;
	uint32_t cblk_index = 0;

	// convert to sign-magnitude
	if(block->qmfbid == 1)
	{
		auto tiledp = block->tiledp;
		for(auto j = 0U; j < h; ++j)
		{
			for(auto i = 0U; i < w; ++i)
			{
				unencoded_data[cblk_index] = *tiledp++;
				cblk_index++;
			}
			tiledp += tileLineAdvance;
		}
	}
	else
	{
		auto tiledp = (float*)block->tiledp;
		for(auto j = 0U; j < h; ++j)
		{
			for(auto i = 0U; i < w; ++i)
			{
				unencoded_data[cblk_index] = (int32_t)((float)*tiledp++ * block->inv_step_ht);
				cblk_index++;
			}
			tiledp += tileLineAdvance;
		}
	}
}
bool T1OpenHTJ2K::compress(grk::CompressBlockExec* block)
{
	preCompress(block, block->tile);
	auto cblk = block->cblk;
	uint32_t idx;
	uint16_t numlayers = 1;
	uint16_t codelbock_style = block->cblk_sty;
	const element_siz p0;
	const element_siz p1;
	const element_siz s(cblk->width(), cblk->height());
	auto j2k_block =
		new j2k_codeblock(idx, block->bandOrientation, 0, 0, 0, 0, cblk->width(), unencoded_data,
						  (float*)unencoded_data, 0, numlayers, codelbock_style, p0, p1, s);
	auto len = htj2k_encode(j2k_block, 0);
	cblk->numPassesTotal = 1;
	cblk->passes[0].len = (uint16_t)len;
	cblk->passes[0].rate = (uint16_t)len;
	cblk->numbps = 1;
	assert(cblk->paddedCompressedStream);
	memcpy(cblk->paddedCompressedStream, j2k_block->get_compressed_data(), (size_t)len);
	delete j2k_block;

	return true;
}
bool T1OpenHTJ2K::decompress(grk::DecompressBlockExec* block)
{
	auto cblk = block->cblk;
	if(!cblk->area())
		return true;
	uint16_t stride = (uint16_t)cblk->width();
	if(!cblk->seg_buffers.empty())
	{
		size_t total_seg_len = cblk->getSegBuffersLen();
		if(coded_data_size < total_seg_len)
		{
			delete[] coded_data;
			coded_data = new uint8_t[total_seg_len];
			coded_data_size = (uint32_t)total_seg_len;
		}
		size_t offset = 0;
		for(auto& b : cblk->seg_buffers)
		{
			memcpy(coded_data + offset, b->buf, b->len);
			offset += b->len;
		}

		size_t num_passes = 0;
		for(uint32_t i = 0; i < cblk->getNumSegments(); ++i)
		{
			auto sgrk = cblk->getSegment(i);
			num_passes += sgrk->numpasses;
		}

		if(num_passes && offset)
		{
			auto cblk = block->cblk;
			uint32_t idx;
			uint16_t numlayers = 1;
			uint16_t codelbock_style = block->cblk_sty;
			const element_siz p0;
			const element_siz p1;
			const element_siz s(cblk->width(), cblk->height());
			auto j2k_block =
				new j2k_codeblock(idx, block->bandOrientation, block->k_msbs + 1U, block->R_b,
								  block->qmfbid, block->stepsize, cblk->width(), unencoded_data,
								  (float*)unencoded_data, 0, numlayers, codelbock_style, p0, p1, s);
			j2k_block->num_passes = num_passes;
			j2k_block->num_ZBP = block->k_msbs;
			j2k_block->length = offset;
			j2k_block->pass_length[0] = offset;
			j2k_block->pass_length[1] = 0;
			j2k_block->pass_length[2] = 0;
			j2k_block->set_compressed_data(coded_data, offset);
			htj2k_decode(j2k_block, 0);
			delete j2k_block;
		}
		else
		{
			memset(unencoded_data, 0, stride * cblk->height() * sizeof(int32_t));
		}
	}

	block->tilec->postProcessHT(unencoded_data, block, stride);

	return true;
}
} // namespace openhtj2k

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
