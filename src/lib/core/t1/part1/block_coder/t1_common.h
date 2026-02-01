/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include <cstdint>
#include <math.h>
#include <cassert>

namespace grk::t1
{

enum eBandOrientation
{
  BAND_ORIENT_LL,
  BAND_ORIENT_HL,
  BAND_ORIENT_LH,
  BAND_ORIENT_HH,
  BAND_NUM_ORIENTATIONS
};
// LL band index when resolution == 0
const uint32_t BAND_RES_ZERO_INDEX_LL = 0;

// band indices when resolution > 0
enum eBandIndex
{
  BAND_INDEX_HL,
  BAND_INDEX_LH,
  BAND_INDEX_HH,
  BAND_NUM_INDICES
};

const uint32_t T1_NUMCTXS_ZC = 9;
const uint32_t T1_NUMCTXS_SC = 5;
const uint32_t T1_NUMCTXS_MAG = 3;
const uint32_t T1_NUMCTXS_AGG = 1;
const uint32_t T1_NUMCTXS_UNI = 1;

const uint32_t T1_CTXNO_ZC = 0;
const uint32_t T1_CTXNO_SC = T1_CTXNO_ZC + T1_NUMCTXS_ZC;
const uint32_t T1_CTXNO_MAG = T1_CTXNO_SC + T1_NUMCTXS_SC;
const uint32_t T1_CTXNO_AGG = T1_CTXNO_MAG + T1_NUMCTXS_MAG;
const uint32_t T1_CTXNO_UNI = T1_CTXNO_AGG + T1_NUMCTXS_AGG;
const uint32_t T1_NUMCTXS = T1_CTXNO_UNI + T1_NUMCTXS_UNI;

struct pass_enc
{
  uint16_t rate;
  double distortiondec;
  uint16_t len;
  bool term;
};

struct cblk_enc
{
  uint8_t* data;
  pass_enc* passes;
  uint32_t x0, y0, x1, y1;
  uint8_t numbps;
  uint8_t numPassesTotal;
#ifdef PLUGIN_DEBUG_ENCODE
  uint32_t* context_stream;
#endif
  pass_enc* getPass(uint8_t passno)
  {
    return passes + passno;
  }
};

/* Macros to deal with signed integer with just MSB bit set for
 * negative values (smr = signed magnitude representation) */
#define smr_abs(x) (((uint32_t)(x)) & 0x7FFFFFFFU)
#define smr_sign(x) (uint8_t)(((uint32_t)(x)) >> 31)
#define to_smr(x) ((x) >= 0 ? (uint32_t)(x) : ((uint32_t)(-x) | 0x80000000U))

} // namespace grk::t1
