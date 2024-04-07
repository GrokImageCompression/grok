/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
#include "simd.h"
#include "../OJPH/common/ojph_mem.h"
#include "coding/ojph_block_decoder.h"
#include "coding/ojph_block_encoder.h"
#include "ojph_mem.h"
#include "T1OJPH.h"

#include "grk_includes.h"

const uint8_t grk_cblk_dec_compressed_data_pad_ht = 8;

namespace ojph
{
T1OJPH::T1OJPH(bool isCompressor, [[maybe_unused]] grk::TileCodingParams* tcp, uint32_t maxCblkW,
			   uint32_t maxCblkH)
	: coded_data_size(isCompressor ? 0 : (uint32_t)(maxCblkW * maxCblkH * sizeof(int32_t))),
	  coded_data(isCompressor ? nullptr : new uint8_t[coded_data_size]),
	  unencoded_data_size(maxCblkW * maxCblkH), unencoded_data(new int32_t[unencoded_data_size]),
	  allocator(new mem_fixed_allocator), elastic_alloc(new mem_elastic_allocator(1048576))
{
   if(!isCompressor)
	  memset(coded_data, 0, grk_cblk_dec_compressed_data_pad_ht);
}
T1OJPH::~T1OJPH()
{
   delete[] coded_data;
   delete[] unencoded_data;
   delete allocator;
   delete elastic_alloc;
}
void T1OJPH::preCompress([[maybe_unused]] grk::CompressBlockExec* block,
						 [[maybe_unused]] grk::Tile* tile)
{
   auto cblk = block->cblk;
   uint16_t w = (uint16_t)cblk->width();
   uint16_t h = (uint16_t)cblk->height();
   uint32_t tile_width =
	   (tile->comps + block->compno)->getWindow()->getResWindowBufferHighestStride();
   auto tileLineAdvance = tile_width - w;
   uint32_t cblk_index = 0;
   int32_t shift = 31 - (block->k_msbs + 1);

   // convert to sign-magnitude
   if(block->qmfbid == 1)
   {
	  auto tiledp = block->tiledp;
	  for(auto j = 0U; j < h; ++j)
	  {
		 for(auto i = 0U; i < w; ++i)
		 {
			int32_t temp = *tiledp++;
			int32_t val = temp >= 0 ? temp : -temp;
			int32_t sign = (int32_t)((temp >= 0) ? 0U : 0x80000000);
			int32_t res = sign | (val << shift);
			unencoded_data[cblk_index] = res;
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
			int32_t t = (int32_t)((float)*tiledp++ * block->inv_step_ht * (float)(1 << shift));
			int32_t val = t >= 0 ? t : -t;
			int32_t sign = t >= 0 ? 0 : (int32_t)0x80000000;
			int32_t res = sign | val;
			unencoded_data[cblk_index] = res;
			cblk_index++;
		 }
		 tiledp += tileLineAdvance;
	  }
   }
}
bool T1OJPH::compress(grk::CompressBlockExec* block)
{
   preCompress(block, block->tile);

   coded_lists* next_coded = nullptr;
   auto cblk = block->cblk;
   cblk->numbps = 0;
   // optimization below was causing errors in compressing
   // if (maximum >= (uint32_t)1<<(31 - (block->k_msbs+1)))
   uint16_t w = (uint16_t)cblk->width();
   uint16_t h = (uint16_t)cblk->height();

   uint32_t pass_length[2] = {0, 0};
   ojph::local::ojph_encode_codeblock((uint32_t*)unencoded_data, block->k_msbs, 1, w, h, w,
									  pass_length, elastic_alloc, next_coded);

   cblk->numPassesTotal = 1;
   cblk->passes[0].len = (uint16_t)pass_length[0];
   cblk->passes[0].rate = (uint16_t)pass_length[0];
   cblk->numbps = 1;
   assert(cblk->paddedCompressedStream);
   memcpy(cblk->paddedCompressedStream, next_coded->buf, (size_t)pass_length[0]);

   return true;
}
bool T1OJPH::decompress(grk::DecompressBlockExec* block)
{
   auto cblk = block->cblk;
   if(!cblk->area())
	  return true;
   uint16_t stride = (uint16_t)cblk->width();
   if(!cblk->seg_buffers.empty())
   {
	  size_t total_seg_len = 2 * grk_cblk_dec_compressed_data_pad_ht + cblk->getSegBuffersLen();
	  if(coded_data_size < total_seg_len)
	  {
		 delete[] coded_data;
		 coded_data = new uint8_t[total_seg_len];
		 coded_data_size = (uint32_t)total_seg_len;
		 memset(coded_data, 0, grk_cblk_dec_compressed_data_pad_ht);
	  }
	  memset(coded_data + grk_cblk_dec_compressed_data_pad_ht + cblk->getSegBuffersLen(), 0,
			 grk_cblk_dec_compressed_data_pad_ht);
	  uint8_t* actual_coded_data = coded_data + grk_cblk_dec_compressed_data_pad_ht;
	  size_t offset = 0;
	  for(auto& b : cblk->seg_buffers)
	  {
		 memcpy(actual_coded_data + offset, b->buf, b->len);
		 offset += b->len;
	  }

	  size_t num_passes = 0;
	  for(uint32_t i = 0; i < cblk->getNumSegments(); ++i)
	  {
		 auto sgrk = cblk->getSegment(i);
		 num_passes += sgrk->numpasses;
	  }

	  bool rc = false;
	  if(num_passes && offset)
	  {
		 rc = ojph::local::ojph_decode_codeblock(
			 actual_coded_data, (uint32_t*)unencoded_data, block->k_msbs, (uint32_t)num_passes,
			 (uint32_t)offset, 0, cblk->width(), cblk->height(), stride, false);
	  }
	  else
	  {
		 memset(unencoded_data, 0, stride * cblk->height() * sizeof(int32_t));
	  }
	  if(!rc)
	  {
		 grk::Logger::logger_.error("Error in HT block coder");
		 return false;
	  }
   }

   block->tilec->postProcessHT(unencoded_data, block, stride);

   return true;
}
} // namespace ojph
