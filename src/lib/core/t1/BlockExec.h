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
#pragma once
#include "grk_includes.h"

namespace grk
{
struct BlockExec
{
   BlockExec()
	   : tilec(nullptr), bandIndex(0), bandNumbps(0), bandOrientation(BAND_ORIENT_LL), stepsize(0),
		 cblk_sty(0), qmfbid(0), x(0), y(0), k_msbs(0), R_b(0)
   {}
   virtual bool open(T1Interface* t1) = 0;
   virtual ~BlockExec() = default;
   TileComponent* tilec;
   uint8_t bandIndex;
   uint8_t bandNumbps;
   eBandOrientation bandOrientation;
   float stepsize;
   uint8_t cblk_sty;
   uint8_t qmfbid;
   /* code block offset in buffer coordinates*/
   uint32_t x;
   uint32_t y;
   // missing bit planes for all blocks in band
   uint8_t k_msbs;
   uint8_t R_b;
};
struct DecompressBlockExec : public BlockExec
{
   DecompressBlockExec() : cblk(nullptr), resno(0), roishift(0) {}
   bool open(T1Interface* t1)
   {
	  return t1->decompress(this);
   }
   void close(void) {}
   DecompressCodeblock* cblk;
   uint8_t resno;
   uint8_t roishift;
};
struct CompressBlockExec : public BlockExec
{
   CompressBlockExec()
	   : cblk(nullptr), tile(nullptr), doRateControl(false), distortion(0), tiledp(nullptr),
		 compno(0), resno(0), precinctIndex(0), cblkno(0), inv_step_ht(0), mct_norms(nullptr),
#ifdef DEBUG_LOSSLESS_T1
		 unencodedData(nullptr),
#endif
		 mct_numcomps(0)
   {}
   bool open(T1Interface* t1)
   {
	  return t1->compress(this);
   }
   void close(void) {}
   CompressCodeblock* cblk;
   Tile* tile;
   bool doRateControl;
   double distortion;
   int32_t* tiledp;
   uint16_t compno;
   uint8_t resno;
   uint64_t precinctIndex;
   uint64_t cblkno;
   float inv_step_ht;
   const double* mct_norms;
#ifdef DEBUG_LOSSLESS_T1
   int32_t* unencodedData;
#endif
   uint16_t mct_numcomps;
};

} // namespace grk
