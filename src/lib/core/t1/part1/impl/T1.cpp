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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include <Logger.h>
#include "grk_includes.h"
#include "t1_common.h"
#include "t1_luts.h"

namespace grk
{
/** We hold the state of individual data points for the T1 compressor using
 *  a single 32-bit flags word to hold the state of 4 data points.  This corresponds
 *  to the 4-point-high columns that the data is processed in.
 *  These \#defines declare the layout of a 32-bit flags word.
 */

/* SIGMA: significance state (3 cols x 6 rows)
 * CHI:   state for negative sample value (1 col x 6 rows)
 * MU:    state for visited in refinement pass (1 col x 4 rows)
 * PI:    state for visited in significance pass (1 col * 4 rows)
 */

#define T1_SIGMA_0 (1U << 0)
#define T1_SIGMA_1 (1U << 1)
#define T1_SIGMA_2 (1U << 2)
#define T1_SIGMA_3 (1U << 3)
#define T1_SIGMA_4 (1U << 4)
#define T1_SIGMA_5 (1U << 5)
#define T1_SIGMA_6 (1U << 6)
#define T1_SIGMA_7 (1U << 7)
#define T1_SIGMA_8 (1U << 8)
#define T1_SIGMA_9 (1U << 9)
#define T1_SIGMA_10 (1U << 10)
#define T1_SIGMA_11 (1U << 11)
#define T1_SIGMA_12 (1U << 12)
#define T1_SIGMA_13 (1U << 13)
#define T1_SIGMA_14 (1U << 14)
#define T1_SIGMA_15 (1U << 15)
#define T1_SIGMA_16 (1U << 16)
#define T1_SIGMA_17 (1U << 17)
#define T1_CHI_0 (1U << 18)
#define T1_CHI_0_I 18
#define T1_CHI_1 (1U << 19)
#define T1_CHI_1_I 19
#define T1_MU_0 (1U << 20)
#define T1_PI_0 (1U << 21)
#define T1_CHI_2 (1U << 22)
#define T1_CHI_2_I 22
#define T1_MU_1 (1U << 23)
#define T1_PI_1_I 24
#define T1_PI_1 (1U << T1_PI_1_I)
#define T1_CHI_3 (1U << 25)
#define T1_MU_2 (1U << 26)
#define T1_PI_2_I 27
#define T1_PI_2 (1U << T1_PI_2_I)
#define T1_CHI_4 (1U << 28)
#define T1_MU_3 (1U << 29)
#define T1_PI_3 (1U << 30)
#define T1_CHI_5 (1U << 31)
#define T1_CHI_5_I 31

/** As an example, the bits T1_SIGMA_3, T1_SIGMA_4 and T1_SIGMA_5
 *  indicate the significance state of the west neighbour of data point zero
 *  of our four, the point itself, and its east neighbour respectively.
 *  Many of the bits are arranged so that given a flags word, you can
 *  look at the values for the data point 0, then shift the flags
 *  word right by 3 bits and look at the same bit positions to see the
 *  values for data point 1.
 *
 *  The \#defines below help a bit with this; say you have a flags word
 *  f, you can do things like
 *
 *  (f & T1_SIGMA_THIS)
 *
 *  to see the significance bit of data point 0, then do
 *
 *  ((f >> 3) & T1_SIGMA_THIS)
 *
 *  to see the significance bit of data point 1.
 */

#define T1_SIGMA_NW T1_SIGMA_0
#define T1_SIGMA_N T1_SIGMA_1
#define T1_SIGMA_NE T1_SIGMA_2
#define T1_SIGMA_W T1_SIGMA_3
#define T1_SIGMA_THIS T1_SIGMA_4
#define T1_SIGMA_E T1_SIGMA_5
#define T1_SIGMA_SW T1_SIGMA_6
#define T1_SIGMA_S T1_SIGMA_7
#define T1_SIGMA_SE T1_SIGMA_8
#define T1_SIGMA_NEIGHBOURS                                                                       \
   (T1_SIGMA_NW | T1_SIGMA_N | T1_SIGMA_NE | T1_SIGMA_W | T1_SIGMA_E | T1_SIGMA_SW | T1_SIGMA_S | \
	T1_SIGMA_SE)

#define T1_CHI_THIS T1_CHI_1
#define T1_CHI_THIS_I T1_CHI_1_I
#define T1_MU_THIS T1_MU_0
#define T1_PI_THIS T1_PI_0
#define T1_CHI_S T1_CHI_2

#define T1_LUT_SGN_W (1U << 0)
#define T1_LUT_SIG_N (1U << 1)
#define T1_LUT_SGN_E (1U << 2)
#define T1_LUT_SIG_W (1U << 3)
#define T1_LUT_SGN_N (1U << 4)
#define T1_LUT_SIG_E (1U << 5)
#define T1_LUT_SGN_S (1U << 6)
#define T1_LUT_SIG_S (1U << 7)

#define T1_TYPE_MQ 0 /** Normal coding using entropy coder */
#define T1_TYPE_RAW 1 /** Raw compressing*/

#define setcurctx(curctx, ctxno) curctx = &(mqc)->ctxs[(uint32_t)(ctxno)]
static INLINE void update_flags(grk_flag* flagsp, uint32_t ci, uint32_t s, uint32_t stride,
								uint32_t vsc);
// ENCODE
static int16_t getnmsedec_sig(uint32_t x, uint32_t bitpos);
static int16_t getnmsedec_ref(uint32_t x, uint32_t bitpos);
// DECODE
static INLINE uint8_t getctxno_zc(mqcoder* mqc, uint32_t f);
static INLINE uint8_t getctxno_mag(uint32_t f);
static INLINE uint8_t getctxtno_sc_or_spb_index(uint32_t fX, uint32_t pfX, uint32_t nfX,
												uint32_t ci);
static INLINE uint8_t getspb(uint32_t lu);
static INLINE uint8_t getctxno_zc(mqcoder* mqc, uint32_t f)
{
   return mqc->lut_ctxno_zc_orient[(f & T1_SIGMA_NEIGHBOURS)];
}
static INLINE uint8_t getctxtno_sc_or_spb_index(uint32_t fX, uint32_t pfX, uint32_t nfX,
												uint32_t ci)
{
   /*
	0 pfX T1_CHI_THIS           T1_LUT_SGN_W
	1 tfX T1_SIGMA_1            T1_LUT_SIG_N
	2 nfX T1_CHI_THIS           T1_LUT_SGN_E
	3 tfX T1_SIGMA_3            T1_LUT_SIG_W
	4  fX T1_CHI_(THIS - 1)     T1_LUT_SGN_N
	5 tfX T1_SIGMA_5            T1_LUT_SIG_E
	6  fX T1_CHI_(THIS + 1)     T1_LUT_SGN_S
	7 tfX T1_SIGMA_7            T1_LUT_SIG_S
	*/
   uint32_t lu = (fX >> (ci)) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);

   lu |= (pfX >> (T1_CHI_THIS_I + (ci))) & (1U << 0);
   lu |= (nfX >> (T1_CHI_THIS_I - 2U + (ci))) & (1U << 2);
   if(ci == 0U)
   {
	  lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
   }
   else
   {
	  lu |= (fX >> (T1_CHI_1_I - 4U + ((ci - 3U)))) & (1U << 4);
   }
   lu |= (fX >> (T1_CHI_2_I - 6U + (ci))) & (1U << 6);

   return (uint8_t)lu;
}
static INLINE uint8_t getctxno_sc(uint32_t lu)
{
   return lut_ctxno_sc[lu];
}
static INLINE uint8_t getctxno_mag(uint32_t f)
{
   uint32_t tmp = (f & T1_SIGMA_NEIGHBOURS) ? T1_CTXNO_MAG + 1 : T1_CTXNO_MAG;
   uint32_t tmp2 = (f & T1_MU_0) ? T1_CTXNO_MAG + 2 : tmp;

   return (uint8_t)tmp2;
}
static INLINE uint8_t getspb(uint32_t lu)
{
   return lut_spb[lu];
}
static int16_t getnmsedec_sig(uint32_t x, uint32_t bitpos)
{
   if(bitpos > 0)
	  return lut_nmsedec_sig[(x >> (bitpos)) & ((1 << T1_NMSEDEC_BITS) - 1)];

   return lut_nmsedec_sig0[x & ((1 << T1_NMSEDEC_BITS) - 1)];
}
static int16_t getnmsedec_ref(uint32_t x, uint32_t bitpos)
{
   if(bitpos > 0)
	  return lut_nmsedec_ref[(x >> (bitpos)) & ((1 << T1_NMSEDEC_BITS) - 1)];

   return lut_nmsedec_ref0[x & ((1 << T1_NMSEDEC_BITS) - 1)];
}
#define update_flags_macro(flags, flagsp, ci, s, stride, vsc) \
   {                                                          \
	  /* east */                                              \
	  flagsp[-1] |= T1_SIGMA_5 << (ci);                       \
	  /* mark target as significant */                        \
	  flags |= ((s << T1_CHI_1_I) | T1_SIGMA_4) << (ci);      \
	  /* west */                                              \
	  flagsp[1] |= T1_SIGMA_3 << (ci);                        \
	  /* north-west, north, north-east */                     \
	  if(ci == 0U && !(vsc))                                  \
	  {                                                       \
		 auto north = flagsp - (stride);                      \
		 *north |= (s << T1_CHI_5_I) | T1_SIGMA_16;           \
		 north[-1] |= T1_SIGMA_17;                            \
		 north[1] |= T1_SIGMA_15;                             \
	  }                                                       \
	  /* south-west, south, south-east */                     \
	  if(ci == 9U)                                            \
	  {                                                       \
		 auto south = flagsp + (stride);                      \
		 *south |= (s << T1_CHI_0_I) | T1_SIGMA_1;            \
		 south[-1] |= T1_SIGMA_2;                             \
		 south[1] |= T1_SIGMA_0;                              \
	  }                                                       \
   }
static INLINE void update_flags(grk_flag* flagsp, uint32_t ci, uint32_t s, uint32_t stride,
								uint32_t vsc)
{
   update_flags_macro(*flagsp, flagsp, ci, s, stride, vsc);
}
static const double dwt_norms[4][10] = {
	{1.000, 1.500, 2.750, 5.375, 10.68, 21.34, 42.67, 85.33, 170.7, 341.3},
	{1.038, 1.592, 2.919, 5.703, 11.33, 22.64, 45.25, 90.48, 180.9},
	{1.038, 1.592, 2.919, 5.703, 11.33, 22.64, 45.25, 90.48, 180.9},
	{.7186, .9218, 1.586, 3.043, 6.019, 12.01, 24.00, 47.97, 95.93}};
static const double dwt_norms_real[4][10] = {
	{1.000, 1.965, 4.177, 8.403, 16.90, 33.84, 67.69, 135.3, 270.6, 540.9},
	{2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
	{2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
	{2.080, 3.865, 8.307, 17.18, 34.71, 69.59, 139.3, 278.6, 557.2}};
T1::T1(bool isCompressor, uint32_t maxCblkW, uint32_t maxCblkH)
	: uncompressedData(nullptr), uncompressedDataLen(0), ownsUncompressedData(false), w(0), h(0),
	  uncompressedDataStride(0), compressedData(nullptr), compressedDataLen(0), flags(nullptr),
	  flagssize(0), compressor(isCompressor)
{
   memset(&coder, 0, sizeof(coder));
   if(!isCompressor)
	  allocCompressedData(maxCblkW * maxCblkH * (uint32_t)sizeof(int32_t));
}
T1::~T1()
{
   deallocUncompressedData();
   grk_aligned_free(flags);
   delete[] compressedData;
}
double T1::getnorm(uint32_t level, uint8_t orientation, bool reversible)
{
   assert(orientation <= 3);
   if(orientation == 0 && level > 9)
	  level = 9;
   else if(orientation > 0 && level > 8)
	  level = 8;

   return reversible ? dwt_norms[orientation][level] : dwt_norms_real[orientation][level];
}
/* <summary>                */
/* Get norm of 5-3 wavelet. */
/* </summary>               */
double T1::getnorm_53(uint32_t level, uint8_t orientation)
{
   return getnorm(level, orientation, true);
}
/* <summary>                */
/* Get norm of 9-7 wavelet. */
/* </summary>               */
double T1::getnorm_97(uint32_t level, uint8_t orientation)
{
   return getnorm(level, orientation, false);
}
uint8_t* T1::getCompressedDataBuffer(void)
{
   return compressedData;
}
void T1::allocCompressedData(size_t len)
{
   if(compressedData && compressedDataLen > len)
	  return;
   delete[] compressedData;
   compressedData = new uint8_t[len];
   compressedDataLen = len;
}
int32_t* T1::getUncompressedData(void)
{
   return uncompressedData;
}
bool T1::allocUncompressedData(size_t len)
{
   if(len == 0)
   {
	  Logger::logger_.error("Unable to allocated zero-length memory");
	  return false;
   }
   if(uncompressedData && uncompressedDataLen > len)
	  return true;
   deallocUncompressedData();
   uncompressedData = (int32_t*)grk::grk_aligned_malloc(len);
   if(!uncompressedData)
   {
	  Logger::logger_.error("Out of memory");
	  return false;
   }
   ownsUncompressedData = true;
   uncompressedDataLen = len;

   return true;
}
void T1::deallocUncompressedData(void)
{
   if(ownsUncompressedData)
	  grk::grk_aligned_free(uncompressedData);
   uncompressedData = nullptr;
   ownsUncompressedData = false;
}
void T1::attachUncompressedData(int32_t* data, uint32_t width, uint32_t height)
{
   deallocUncompressedData();
   uncompressedData = data;
   alloc(width, height);
}
bool T1::alloc(uint32_t width, uint32_t height)
{
   if(width == 0 || height == 0)
   {
	  Logger::logger_.error(
		  "Unable to allocated memory for degenerate code block of dimensions %ux%u", width,
		  height);
	  return false;
   }
   uint32_t newflagssize;
   uint32_t flags_stride;
   if(compressor)
   {
	  uint32_t newDataSize = width * height * sizeof(int32_t);
	  if(!allocUncompressedData(newDataSize))
		 return false;
	  if(!compressor)
		 memset(uncompressedData, 0, newDataSize);
   }
   w = width;
   h = height;
   uncompressedDataStride = width;
   flags_stride = width + 2U; /* can't be 0U */
   newflagssize = (height + 3U) / 4U + 2U;
   newflagssize *= flags_stride;
   uint32_t x;
   uint32_t flags_height = (height + 3U) / 4U;
   if(newflagssize > flagssize)
   {
	  grk::grk_aligned_free(flags);
	  flags = (grk_flag*)grk::grk_aligned_malloc(newflagssize * sizeof(grk_flag));
	  if(!flags)
	  {
		 Logger::logger_.error("Out of memory");
		 return false;
	  }
   }
   flagssize = newflagssize;
   memset(flags, 0, newflagssize * sizeof(grk_flag));
   auto p = &flags[0];
   /* magic value to stop any passes being interested in this entry */
   for(x = 0; x < flags_stride; ++x)
	  *p++ = (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
   p = &flags[((flags_height + 1) * flags_stride)];
   /* magic value to stop any passes being interested in this entry */
   for(x = 0; x < flags_stride; ++x)
	  *p++ = (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
   if(height % 4)
   {
	  uint32_t v = 0;
	  p = &flags[((flags_height)*flags_stride)];
	  if((height & 3) == 1)
		 v |= T1_PI_1 | T1_PI_2 | T1_PI_3;
	  else if((height & 3) == 2)
		 v |= T1_PI_2 | T1_PI_3;
	  else if((height & 3) == 3)
		 v |= T1_PI_3;
	  for(x = 0; x < flags_stride; ++x)
		 *p++ = v;
   }

   return true;
}
/// ENCODE ////////////////////////////////////////////////////
/**
 * Deallocate the compressing data of the given precinct.
 */
void T1::code_block_enc_deallocate(cblk_enc* code_block)
{
   delete[] code_block->passes;
   code_block->passes = nullptr;
}
void T1::code_block_enc_allocate(cblk_enc* p_code_block)
{
   if(!p_code_block->passes)
	  p_code_block->passes = new pass_enc[100];
}
double T1::getwmsedec(int32_t nmsedec, uint16_t compno, uint32_t level, uint8_t orientation,
					  int32_t bpno, uint32_t qmfbid, double stepsize, const double* mct_norms,
					  uint32_t mct_numcomps)
{
   double w1 = 1, w2, wmsedec;

   if(mct_norms && (compno < mct_numcomps))
	  w1 = mct_norms[compno];

   if(qmfbid == 1)
	  w2 = getnorm_53(level, orientation);
   else
	  w2 = getnorm_97(level, orientation);

   wmsedec = w1 * w2 * stepsize * (1 << bpno);
   wmsedec *= wmsedec * nmsedec / 8192.0;

   return wmsedec;
}
int T1::enc_is_term_pass(cblk_enc* cblk, uint32_t cblksty, int32_t bpno, uint32_t passtype)
{
   /* Is it the last cleanup pass ? */
   if(passtype == 2 && bpno == 0)
	  return true;

   if(cblksty & GRK_CBLKSTY_TERMALL)
	  return true;

   if((cblksty & GRK_CBLKSTY_LAZY))
   {
	  /* For bypass, terminate the 4th cleanup pass */
	  if((bpno == ((int32_t)cblk->numbps - 4)) && (passtype == 2))
		 return true;
	  /* and beyond terminate all the magnitude refinement passes (in raw) */
	  /* and cleanup passes (in MQC) */
	  if((bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype > 0))
		 return true;
   }

   return false;
}
#define enc_sigpass_step_macro(datap, ci, vsc)                                                     \
   {                                                                                               \
	  uint32_t v;                                                                                  \
	  if((*flagsp & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == 0U &&                               \
		 (*flagsp & (T1_SIGMA_NEIGHBOURS << (ci))) != 0U)                                          \
	  {                                                                                            \
		 uint8_t ctxno = getctxno_zc(mqc, *flagsp >> (ci));                                        \
		 v = !!(smr_abs(*(datap)) & (uint32_t)one);                                                \
		 curctx = mqc->ctxs + ctxno;                                                               \
		 if(type == T1_TYPE_RAW)                                                                   \
			mqc_bypass_enc_macro(mqc, c, ct, v) else mqc_encode_macro(mqc, curctx, a, c, ct,       \
																	  v) if(v)                     \
			{                                                                                      \
			   uint32_t lu = getctxtno_sc_or_spb_index(*flagsp, flagsp[-1], flagsp[1], ci);        \
			   ctxno = getctxno_sc(lu);                                                            \
			   v = smr_sign(*(datap));                                                             \
			   if(nmsedec)                                                                         \
				  *nmsedec += getnmsedec_sig((uint32_t)smr_abs(*(datap)), (uint32_t)bpno);         \
			   curctx = mqc->ctxs + ctxno;                                                         \
			   if(type == T1_TYPE_RAW)                                                             \
				  mqc_bypass_enc_macro(mqc, c, ct, v) else mqc_encode_macro(mqc, curctx, a, c, ct, \
																			v ^ getspb(lu))        \
					  update_flags(flagsp, ci, v, w + 2, vsc);                                     \
			}                                                                                      \
		 *flagsp |= T1_PI_THIS << (ci);                                                            \
	  }                                                                                            \
   }
void T1::enc_sigpass(int32_t bpno, int32_t* nmsedec, uint8_t type, uint32_t cblksty)
{
   uint32_t i, k;
   int32_t const one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
   // flags top left hand corner
   auto flagsp = flags + 1 + (w + 2);
   auto mqc = &(coder);
   PUSH_MQC();
   uint32_t const extra = 2;
   if(nmsedec)
	  *nmsedec = 0;
   for(k = 0; k < (h & ~3U); k += 4)
   {
	  for(i = 0; i < w; ++i)
	  {
		 if(*flagsp == 0U)
		 {
			/* Nothing to do for any of the 4 data points */
			flagsp++;
			continue;
		 }
		 enc_sigpass_step_macro(uncompressedData + (k + 0) * uncompressedDataStride + i, 0,
								cblksty & GRK_CBLKSTY_VSC);
		 enc_sigpass_step_macro(uncompressedData + (k + 1) * uncompressedDataStride + i, 3, 0);
		 enc_sigpass_step_macro(uncompressedData + (k + 2) * uncompressedDataStride + i, 6, 0);
		 enc_sigpass_step_macro(uncompressedData + (k + 3) * uncompressedDataStride + i, 9, 0);
		 ++flagsp;
	  }
	  flagsp += extra;
   }
   if(k < h)
   {
	  for(i = 0; i < w; ++i)
	  {
		 if(*flagsp == 0U)
		 {
			/* Nothing to do for any of the 4 data points */
			flagsp++;
			continue;
		 }
		 auto pdata = uncompressedData + k * uncompressedDataStride + i;
		 for(uint32_t j = k; j < h; ++j)
		 {
			enc_sigpass_step_macro(pdata, 3 * (j - k),
								   (j == k && (cblksty & GRK_CBLKSTY_VSC) != 0));
			pdata += uncompressedDataStride;
		 }
		 ++flagsp;
	  }
   }
   POP_MQC();
}
#define enc_refpass_step_macro(datap, ci)                                            \
   {                                                                                 \
	  uint32_t v;                                                                    \
	  uint32_t const shift_flags = (*flagsp >> (ci));                                \
	  if((shift_flags & (T1_SIGMA_THIS | T1_PI_THIS)) == T1_SIGMA_THIS)              \
	  {                                                                              \
		 uint8_t ctxno = getctxno_mag(shift_flags);                                  \
		 if(nmsedec)                                                                 \
			*nmsedec += getnmsedec_ref((uint32_t)smr_abs(*(datap)), (uint32_t)bpno); \
		 v = !!(smr_abs(*(datap)) & (uint32_t)one);                                  \
		 curctx = mqc->ctxs + ctxno;                                                 \
		 if(type == T1_TYPE_RAW)                                                     \
			mqc_bypass_enc_macro(mqc, c, ct, v) else mqc_encode_macro(               \
				mqc, curctx, a, c, ct, v)* flagsp |= T1_MU_THIS << (ci);             \
	  }                                                                              \
   }
void T1::enc_refpass(int32_t bpno, int32_t* nmsedec, uint8_t type)
{
   const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
   auto flagsp = flags + 1 + (w + 2);
   auto mqc = &(coder);
   PUSH_MQC();
   const uint32_t extra = 2U;
   if(nmsedec)
	  *nmsedec = 0;
   uint32_t k;
   for(k = 0; k < (h & ~3U); k += 4)
   {
	  for(uint32_t i = 0; i < w; ++i)
	  {
		 if((*flagsp & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0)
		 { /* none significant */
			flagsp++;
			continue;
		 }
		 if((*flagsp & (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) ==
			(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3))
		 { /* all processed by sigpass */
			flagsp++;
			continue;
		 }
		 enc_refpass_step_macro(uncompressedData + (k + 0) * uncompressedDataStride + i, 0);
		 enc_refpass_step_macro(uncompressedData + (k + 1) * uncompressedDataStride + i, 3);
		 enc_refpass_step_macro(uncompressedData + (k + 2) * uncompressedDataStride + i, 6);
		 enc_refpass_step_macro(uncompressedData + (k + 3) * uncompressedDataStride + i, 9);
		 ++flagsp;
	  }
	  flagsp += extra;
   }
   if(k < h)
   {
	  for(uint32_t i = 0; i < w; ++i)
	  {
		 if((*flagsp & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0)
		 {
			/* none significant */
			flagsp++;
			continue;
		 }
		 for(uint32_t j = k; j < h; ++j)
			enc_refpass_step_macro(uncompressedData + j * uncompressedDataStride + i, 3 * (j - k));
		 ++flagsp;
	  }
   }
   POP_MQC();
}
void T1::enc_clnpass(int32_t bpno, int32_t* nmsedec, uint32_t cblksty)
{
   const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
   auto mqc = &coder;
   PUSH_MQC();
   if(nmsedec)
	  *nmsedec = 0;
   auto flagsp = flags + 1 + (w + 2);
   uint32_t k;
   for(k = 0; k < (h & ~3U); k += 4, flagsp += 2)
   {
	  for(uint32_t i = 0; i < w; ++i, ++flagsp)
	  {
		 uint32_t agg = !(*flagsp);
		 uint32_t runlen = 0;
		 if(agg)
		 {
			for(; runlen < 4; ++runlen)
			{
			   if(smr_abs(uncompressedData[((k + runlen) * uncompressedDataStride) + i]) &
				  (uint32_t)one)
				  break;
			}
			uint8_t ctxno = T1_CTXNO_AGG;
			curctx = mqc->ctxs + ctxno;
			mqc_encode_macro(mqc, curctx, a, c, ct, runlen != 4);
			if(runlen == 4)
			   continue;
			ctxno = T1_CTXNO_UNI;
			curctx = mqc->ctxs + ctxno;
			mqc_encode_macro(mqc, curctx, a, c, ct, runlen >> 1);
			mqc_encode_macro(mqc, curctx, a, c, ct, runlen & 1);
		 }
		 auto datap = uncompressedData + ((k + runlen) * uncompressedDataStride) + i;
		 const uint32_t check = (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13 | T1_PI_0 |
								 T1_PI_1 | T1_PI_2 | T1_PI_3);
		 bool stage_2 = true;
		 if((*flagsp & check) == check)
		 {
			switch(runlen)
			{
			   case 0:
				  *flagsp &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
				  break;
			   case 1:
				  *flagsp &= ~(T1_PI_1 | T1_PI_2 | T1_PI_3);
				  break;
			   case 2:
				  *flagsp &= ~(T1_PI_2 | T1_PI_3);
				  break;
			   case 3:
				  *flagsp &= ~(T1_PI_3);
				  break;
			   default:
				  stage_2 = false;
				  break;
			}
		 }
		 for(uint32_t ci = 3 * runlen; ci < 3 * 4 && stage_2; ci += 3)
		 {
			bool goto_PARTIAL = false;
			if((agg != 0) && (ci == 3 * runlen))
			   goto_PARTIAL = true;
			else if(!(*flagsp & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))))
			{
			   uint8_t ctxno = getctxno_zc(mqc, *flagsp >> (ci));
			   curctx = mqc->ctxs + ctxno;
			   uint32_t v = !!(smr_abs(*datap) & (uint32_t)one);
			   mqc_encode_macro(mqc, curctx, a, c, ct, v);
			   goto_PARTIAL = v;
			}
			if(goto_PARTIAL)
			{
			   uint32_t lu = getctxtno_sc_or_spb_index(*flagsp, *(flagsp - 1), *(flagsp + 1), ci);
			   if(nmsedec)
				  *nmsedec += getnmsedec_sig((uint32_t)smr_abs(*datap), (uint32_t)bpno);
			   uint8_t ctxno = getctxno_sc(lu);
			   curctx = mqc->ctxs + ctxno;
			   uint32_t v = smr_sign(*datap);
			   uint32_t spb = getspb(lu);
			   mqc_encode_macro(mqc, curctx, a, c, ct, v ^ spb);
			   uint32_t vsc = ((cblksty & GRK_CBLKSTY_VSC) && (ci == 0)) ? 1 : 0;
			   update_flags(flagsp, ci, v, w + 2U, vsc);
			}
			*flagsp &= ~(T1_PI_THIS << (ci));
			datap += uncompressedDataStride;
		 }
	  }
   }
   if(k < h)
   {
	  uint32_t runlen = 0;
	  for(uint32_t i = 0; i < w; ++i, ++flagsp)
	  {
		 auto datap = uncompressedData + ((k + runlen) * uncompressedDataStride) + i;
		 const uint32_t check = (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13 | T1_PI_0 |
								 T1_PI_1 | T1_PI_2 | T1_PI_3);
		 bool stage_2 = true;
		 if((*flagsp & check) == check)
		 {
			switch(runlen)
			{
			   case 0:
				  *flagsp &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
				  break;
			   case 1:
				  *flagsp &= ~(T1_PI_1 | T1_PI_2 | T1_PI_3);
				  break;
			   case 2:
				  *flagsp &= ~(T1_PI_2 | T1_PI_3);
				  break;
			   case 3:
				  *flagsp &= ~(T1_PI_3);
				  break;
			   default:
				  stage_2 = false;
				  break;
			}
		 }
		 const uint32_t lim = 3 * (h - k);
		 for(uint32_t ci = 3 * runlen; ci < lim && stage_2; ci += 3)
		 {
			bool goto_PARTIAL = false;
			if(!(*flagsp & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))))
			{
			   uint8_t ctxno = getctxno_zc(mqc, *flagsp >> (ci));
			   curctx = mqc->ctxs + ctxno;
			   uint32_t v = !!(smr_abs(*datap) & (uint32_t)one);
			   mqc_encode_macro(mqc, curctx, a, c, ct, v);
			   goto_PARTIAL = v;
			}
			if(goto_PARTIAL)
			{
			   uint32_t lu = getctxtno_sc_or_spb_index(*flagsp, *(flagsp - 1), *(flagsp + 1), ci);
			   if(nmsedec)
				  *nmsedec += getnmsedec_sig((uint32_t)smr_abs(*datap), (uint32_t)bpno);
			   uint8_t ctxno = getctxno_sc(lu);
			   curctx = mqc->ctxs + ctxno;
			   uint32_t v = smr_sign(*datap);
			   uint32_t spb = getspb(lu);
			   mqc_encode_macro(mqc, curctx, a, c, ct, v ^ spb);
			   uint32_t vsc = ((cblksty & GRK_CBLKSTY_VSC) && (ci == 0)) ? 1 : 0;
			   update_flags(flagsp, ci, v, w + 2U, vsc);
			}
			*flagsp &= ~(T1_PI_THIS << (ci));
			datap += uncompressedDataStride;
		 }
	  }
   }

   POP_MQC();
}
double T1::compress_cblk(cblk_enc* cblk, uint32_t max, uint8_t orientation, uint16_t compno,
						 uint8_t level, uint8_t qmfbid, double stepsize, uint32_t cblksty,
						 const double* mct_norms, uint16_t mct_numcomps, bool doRateControl)
{
   code_block_enc_allocate(cblk);
   auto mqc = &coder;
   mqc_init_enc(mqc, cblk->data);

   int32_t nmsedec = 0;
   auto p_nmsdedec = doRateControl ? &nmsedec : nullptr;
   double tempwmsedec;

   mqc->lut_ctxno_zc_orient = lut_ctxno_zc + (orientation << 9);
   cblk->numbps = 0;
   if(max)
   {
	  uint8_t temp = floorlog2(max) + 1;
	  if(temp <= T1_NMSEDEC_FRACBITS)
		 cblk->numbps = 0;
	  else
		 cblk->numbps = temp - T1_NMSEDEC_FRACBITS;
   }
   if(cblk->numbps == 0)
   {
	  cblk->numPassesTotal = 0;
	  return 0;
   }
   int32_t bpno = (int32_t)(cblk->numbps - 1);
   uint32_t passtype = 2;
   mqc_resetstates(mqc);
   mqc_init_enc(mqc, cblk->data);
#ifdef PLUGIN_DEBUG_ENCODE
   // uint32_t state = Plugin::getDebugState();
   // if (state & GROK_PLUGIN_STATE_DEBUG) {
   mqc->debug_mqc.contextStream = cblk->contextStream;
   mqc->debug_mqc.orientation = orientation;
   mqc->debug_mqc.compno = compno;
   mqc->debug_mqc.level = level;
   //}
#endif

   double cumwmsedec = 0.0;
   uint32_t passno;
   for(passno = 0; bpno >= 0; ++passno)
   {
	  auto* pass = cblk->passes + passno;
	  uint8_t type =
		  ((bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype < 2) && (cblksty & GRK_CBLKSTY_LAZY))
			  ? T1_TYPE_RAW
			  : T1_TYPE_MQ;

	  /* If the previous pass was terminating, we need to reset the compressor */
	  if(passno > 0 && cblk->passes[passno - 1].term)
	  {
		 if(type == T1_TYPE_RAW)
			mqc_bypass_init_enc(mqc);
		 else
			mqc_restart_init_enc(mqc);
	  }

	  switch(passtype)
	  {
		 case 0:
			enc_sigpass(bpno, p_nmsdedec, type, cblksty);
			break;
		 case 1:
			enc_refpass(bpno, p_nmsdedec, type);
			break;
		 case 2:
			enc_clnpass(bpno, p_nmsdedec, cblksty);
			if(cblksty & GRK_CBLKSTY_SEGSYM)
			   mqc_segmark_enc(mqc);
#ifdef PLUGIN_DEBUG_ENCODE
			// if (state & GROK_PLUGIN_STATE_DEBUG) {
			mqc_next_plane(&mqc->debug_mqc);
			//}
#endif
			break;
	  }
	  if(doRateControl)
	  {
		 tempwmsedec = getwmsedec(nmsedec, compno, level, orientation, bpno, qmfbid, stepsize,
								  mct_norms, mct_numcomps);
		 cumwmsedec += tempwmsedec;
		 pass->distortiondec = cumwmsedec;
	  }
	  if(enc_is_term_pass(cblk, cblksty, bpno, passtype))
	  {
		 if(type == T1_TYPE_RAW)
		 {
			mqc_bypass_flush_enc(mqc, cblksty & GRK_CBLKSTY_PTERM);
		 }
		 else
		 {
			if(cblksty & GRK_CBLKSTY_PTERM)
			   mqc_erterm_enc(mqc);
			else
			   mqc_flush_enc(mqc);
		 }
		 pass->term = true;
		 pass->rate = mqc_numbytes_enc(mqc);
	  }
	  else
	  {
		 /* Non terminated pass */
		 // correction term is used for non-terminated passes,
		 // to ensure that maximal bits are
		 // extracted from the partial segment when code block
		 // is truncated at this pass
		 // See page 498 of Taubman and Marcellin for more details
		 // note: we add 1 because rates for non-terminated passes
		 // are based on mqc_numbytes(mqc),
		 // which is always 1 less than actual rate
		 uint32_t rate_extra_bytes;
		 if(type == T1_TYPE_RAW)
		 {
			rate_extra_bytes = mqc_bypass_get_extra_bytes_enc(mqc, (cblksty & GRK_CBLKSTY_PTERM));
		 }
		 else
		 {
			rate_extra_bytes = 4 + 1;
			if(mqc->ct < 5)
			   rate_extra_bytes++;
		 }
		 pass->term = false;
		 pass->rate = mqc_numbytes_enc(mqc) + rate_extra_bytes;
	  }
	  if(++passtype == 3)
	  {
		 passtype = 0;
		 bpno--;
	  }
	  if(cblksty & GRK_CBLKSTY_RESET)
		 mqc_resetstates(mqc);
   }
   cblk->numPassesTotal = passno;
   if(cblk->numPassesTotal)
   {
	  /* Make sure that pass rates are increasing */
	  uint32_t last_pass_rate = mqc_numbytes_enc(mqc);
	  for(passno = cblk->numPassesTotal; passno > 0;)
	  {
		 auto* pass = cblk->passes + --passno;
		 if(pass->rate > last_pass_rate)
			pass->rate = last_pass_rate;
		 else
			last_pass_rate = pass->rate;
	  }
   }
   for(passno = 0; passno < cblk->numPassesTotal; passno++)
   {
	  auto pass = cblk->passes + passno;

	  /* Prevent generation of FF as last data byte of a pass*/
	  /* For terminating passes, the flushing procedure ensured this already */
	  assert(pass->rate > 0);
	  if(cblk->data[pass->rate - 1] == 0xFF)
		 pass->rate--;
	  pass->len = pass->rate - (passno == 0 ? 0 : cblk->passes[passno - 1].rate);
   }
   return cumwmsedec;
}
////// DECODE  ///////////////////////////
#define dec_clnpass_step_macro(check_flags, partial, flags, flagsp, flags_stride, data, \
							   data_stride, ciorig, ci, vsc)                            \
   {                                                                                    \
	  if(!check_flags || !(flags & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))))             \
	  {                                                                                 \
		 do                                                                             \
		 {                                                                              \
			uint32_t v;                                                                 \
			if(!partial)                                                                \
			{                                                                           \
			   uint32_t ctxt1 = getctxno_zc(mqc, flags >> (ci));                        \
			   setcurctx(curctx, ctxt1);                                                \
			   decompress_macro(v, mqc, curctx, a, c, ct);                              \
			   if(!v)                                                                   \
				  break;                                                                \
			}                                                                           \
			uint32_t lu = getctxtno_sc_or_spb_index(flags, flagsp[-1], flagsp[1], ci);  \
			setcurctx(curctx, getctxno_sc(lu));                                         \
			decompress_macro(v, mqc, curctx, a, c, ct);                                 \
			v = v ^ getspb(lu);                                                         \
			(data)[ciorig * data_stride] = v ? -oneplushalf : oneplushalf;              \
			update_flags_macro(flags, flagsp, ci, v, flags_stride, vsc);                \
		 } while(0);                                                                    \
	  }                                                                                 \
   }
#define dec_clnpass_internal(t1, bpno, vsc, w, h, flags_stride)                                   \
   {                                                                                              \
	  const uint32_t l_w = w;                                                                     \
	  auto mqc = &(t1->coder);                                                                    \
	  auto data = t1->uncompressedData;                                                           \
	  auto flagsp = &t1->flags[flags_stride + 1];                                                 \
	  PUSH_MQC();                                                                                 \
	  int32_t one = 1 << bpno;                                                                    \
	  int32_t half = one >> 1;                                                                    \
	  int32_t oneplushalf = one | half;                                                           \
	  uint32_t k;                                                                                 \
	  for(k = 0; k < (h & ~3u); k += 4, data += 3 * l_w, flagsp += 2)                             \
	  {                                                                                           \
		 for(uint32_t i = 0; i < l_w; ++i, ++data, ++flagsp)                                      \
		 {                                                                                        \
			auto _flags = *flagsp;                                                                \
			if(_flags == 0)                                                                       \
			{                                                                                     \
			   uint32_t partial = true;                                                           \
			   setcurctx(curctx, T1_CTXNO_AGG);                                                   \
			   uint32_t v;                                                                        \
			   decompress_macro(v, mqc, curctx, a, c, ct);                                        \
			   if(!v)                                                                             \
				  continue;                                                                       \
			   setcurctx(curctx, T1_CTXNO_UNI);                                                   \
			   uint32_t runlen;                                                                   \
			   decompress_macro(runlen, mqc, curctx, a, c, ct);                                   \
			   decompress_macro(v, mqc, curctx, a, c, ct);                                        \
			   runlen = (runlen << 1) | v;                                                        \
			   switch(runlen)                                                                     \
			   {                                                                                  \
				  case 0:                                                                         \
					 dec_clnpass_step_macro(false, true, _flags, flagsp, flags_stride, data, l_w, \
											0, 0, vsc);                                           \
					 partial = false;                                                             \
					 /* FALLTHRU */                                                               \
				  case 1:                                                                         \
					 dec_clnpass_step_macro(false, partial, _flags, flagsp, flags_stride, data,   \
											l_w, 1, 3, false);                                    \
					 partial = false;                                                             \
					 /* FALLTHRU */                                                               \
				  case 2:                                                                         \
					 dec_clnpass_step_macro(false, partial, _flags, flagsp, flags_stride, data,   \
											l_w, 2, 6, false);                                    \
					 partial = false;                                                             \
					 /* FALLTHRU */                                                               \
				  case 3:                                                                         \
					 dec_clnpass_step_macro(false, partial, _flags, flagsp, flags_stride, data,   \
											l_w, 3, 9, false);                                    \
					 break;                                                                       \
			   }                                                                                  \
			}                                                                                     \
			else                                                                                  \
			{                                                                                     \
			   dec_clnpass_step_macro(true, false, _flags, flagsp, flags_stride, data, l_w, 0, 0, \
									  vsc);                                                       \
			   dec_clnpass_step_macro(true, false, _flags, flagsp, flags_stride, data, l_w, 1, 3, \
									  false);                                                     \
			   dec_clnpass_step_macro(true, false, _flags, flagsp, flags_stride, data, l_w, 2, 6, \
									  false);                                                     \
			   dec_clnpass_step_macro(true, false, _flags, flagsp, flags_stride, data, l_w, 3, 9, \
									  false);                                                     \
			}                                                                                     \
			*flagsp = _flags & ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                          \
		 }                                                                                        \
	  }                                                                                           \
	  if(k < h)                                                                                   \
	  {                                                                                           \
		 for(uint32_t i = 0; i < l_w; ++i, ++flagsp, ++data)                                      \
		 {                                                                                        \
			for(uint32_t j = 0; j < h - k; ++j)                                                   \
			   dec_clnpass_step_macro(true, false, *flagsp, flagsp, w + 2U, data + j * l_w, 0, j, \
									  j * 3, vsc);                                                \
			*flagsp &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                                  \
		 }                                                                                        \
	  }                                                                                           \
	  POP_MQC();                                                                                  \
   }
void T1::dec_clnpass_check_segsym(int32_t cblksty)
{
   if(cblksty & GRK_CBLKSTY_SEGSYM)
   {
	  auto mqc = &coder;
	  mqc_setcurctx(mqc, T1_CTXNO_UNI);
	  uint32_t v, v2;
	  mqc_decode(v, mqc);
	  mqc_decode(v2, mqc);
	  v = (v << 1) | v2;
	  mqc_decode(v2, mqc);
	  v = (v << 1) | v2;
	  mqc_decode(v2, mqc);
	  v = (v << 1) | v2;
	  if(v != 0xa)
		 Logger::logger_.warn("Bad segmentation symbol %x", v);
   }
}
template<uint32_t w, uint32_t h, bool vsc>
void T1::dec_clnpass(int32_t bpno)
{
   dec_clnpass_internal(this, bpno, vsc, w, h, w + 2);
}
void T1::dec_clnpass(int32_t bpno, int32_t cblksty)
{
   if(w == 64 && h == 64)
   {
	  if(cblksty & GRK_CBLKSTY_VSC)
		 dec_clnpass<64, 64, true>(bpno);
	  else
		 dec_clnpass<64, 64, false>(bpno);
   }
   else
   {
	  dec_clnpass_internal(this, bpno, cblksty & GRK_CBLKSTY_VSC, w, h, w + 2);
   }
   dec_clnpass_check_segsym(cblksty);
}
inline void T1::dec_sigpass_step_raw(grk_flag* flagsp, int32_t* datap, int32_t oneplushalf,
									 uint32_t vsc, uint32_t ci)
{
   auto mqc = &(coder);
   if((*flagsp & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == 0U &&
	  (*flagsp & (T1_SIGMA_NEIGHBOURS << (ci))) != 0U)
   {
	  if(mqc_raw_decode(mqc))
	  {
		 uint32_t v = mqc_raw_decode(mqc);
		 *datap = v ? -oneplushalf : oneplushalf;
		 update_flags(flagsp, ci, v, w + 2, vsc);
	  }
	  *flagsp |= T1_PI_THIS << (ci);
   }
}
#define dec_sigpass_step_mqc_macro(flags, flagsp, flags_stride, data, data_stride, ciorig, ci, \
								   vsc)                                                        \
   {                                                                                           \
	  if((flags & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == 0U &&                             \
		 (flags & (T1_SIGMA_NEIGHBOURS << (ci))) != 0U)                                        \
	  {                                                                                        \
		 uint32_t ctxt1 = getctxno_zc(mqc, flags >> (ci));                                     \
		 setcurctx(curctx, ctxt1);                                                             \
		 uint32_t v;                                                                           \
		 decompress_macro(v, mqc, curctx, a, c, ct);                                           \
		 if(v)                                                                                 \
		 {                                                                                     \
			uint32_t lu = getctxtno_sc_or_spb_index(flags, flagsp[-1], flagsp[1], ci);         \
			uint32_t ctxt2 = getctxno_sc(lu);                                                  \
			uint32_t spb = getspb(lu);                                                         \
			setcurctx(curctx, ctxt2);                                                          \
			decompress_macro(v, mqc, curctx, a, c, ct);                                        \
			v = v ^ spb;                                                                       \
			(data)[(ciorig) * data_stride] = v ? -oneplushalf : oneplushalf;                   \
			update_flags_macro(flags, flagsp, ci, v, flags_stride, vsc);                       \
		 }                                                                                     \
		 flags |= T1_PI_THIS << (ci);                                                          \
	  }                                                                                        \
   }
void T1::dec_sigpass_raw(int32_t bpno, int32_t cblksty)
{
   int32_t one, half, oneplushalf;
   auto flagsp = flags + 1 + (w + 2);
   const uint32_t l_w = w;
   auto dataPtr = uncompressedData;

   one = 1 << bpno;
   half = one >> 1;
   oneplushalf = one | half;

   uint32_t k;
   for(k = 0; k < (h & ~3U); k += 4, flagsp += 2, dataPtr += 3 * l_w)
   {
	  for(uint32_t i = 0; i < l_w; ++i, ++flagsp, ++dataPtr)
	  {
		 if(*flagsp != 0)
		 {
			dec_sigpass_step_raw(flagsp, dataPtr, oneplushalf, cblksty & GRK_CBLKSTY_VSC, 0U);
			dec_sigpass_step_raw(flagsp, dataPtr + l_w, oneplushalf, false, 3U);
			dec_sigpass_step_raw(flagsp, dataPtr + 2 * l_w, oneplushalf, false, 6U);
			dec_sigpass_step_raw(flagsp, dataPtr + 3 * l_w, oneplushalf, false, 9U);
		 }
	  }
   }
   if(k < h)
	  for(uint32_t i = 0; i < l_w; ++i, ++flagsp, ++dataPtr)
		 for(uint32_t j = 0; j < h - k; ++j)
			dec_sigpass_step_raw(flagsp, dataPtr + j * l_w, oneplushalf, cblksty & GRK_CBLKSTY_VSC,
								 3 * j);
}
#define dec_sigpass_mqc_internal(bpno, vsc, w, h, flags_stride)                                   \
   {                                                                                              \
	  auto dataPtr = uncompressedData;                                                            \
	  auto flagsp = &flags[(flags_stride) + 1];                                                   \
	  const uint32_t l_w = w;                                                                     \
	  auto mqc = &(coder);                                                                        \
	  PUSH_MQC();                                                                                 \
	  int32_t one = 1 << bpno;                                                                    \
	  int32_t half = one >> 1;                                                                    \
	  int32_t oneplushalf = one | half;                                                           \
	  uint32_t k;                                                                                 \
	  for(k = 0; k < (h & ~3u); k += 4, dataPtr += 3 * l_w, flagsp += 2)                          \
	  {                                                                                           \
		 for(uint32_t i = 0; i < l_w; ++i, ++dataPtr, ++flagsp)                                   \
		 {                                                                                        \
			grk_flag _flags = *flagsp;                                                            \
			if(_flags != 0)                                                                       \
			{                                                                                     \
			   dec_sigpass_step_mqc_macro(_flags, flagsp, flags_stride, dataPtr, l_w, 0, 0, vsc); \
			   dec_sigpass_step_mqc_macro(_flags, flagsp, flags_stride, dataPtr, l_w, 1, 3,       \
										  false);                                                 \
			   dec_sigpass_step_mqc_macro(_flags, flagsp, flags_stride, dataPtr, l_w, 2, 6,       \
										  false);                                                 \
			   dec_sigpass_step_mqc_macro(_flags, flagsp, flags_stride, dataPtr, l_w, 3, 9,       \
										  false);                                                 \
			   *flagsp = _flags;                                                                  \
			}                                                                                     \
		 }                                                                                        \
	  }                                                                                           \
	  if(k < h)                                                                                   \
		 for(uint32_t i = 0; i < l_w; ++i, ++dataPtr, ++flagsp)                                   \
			for(uint32_t j = 0; j < h - k; ++j)                                                   \
			   dec_sigpass_step_mqc_macro(*flagsp, flagsp, flags_stride, dataPtr + j * l_w, 0, j, \
										  3 * j, vsc);                                            \
	  POP_MQC();                                                                                  \
   }
void T1::dec_sigpass_mqc(int32_t bpno, int32_t cblksty)
{
   if(w == 64 && h == 64)
	  dec_sigpass_mqc_internal(bpno, cblksty & GRK_CBLKSTY_VSC, 64, 64,
							   66) else dec_sigpass_mqc_internal(bpno, cblksty & GRK_CBLKSTY_VSC, w,
																 h, w + 2U)
}
inline void T1::dec_refpass_step_raw(grk_flag* flagsp, int32_t* datap, int32_t poshalf, uint32_t ci)
{
   auto mqc = &(coder);

   if((*flagsp & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == (T1_SIGMA_THIS << (ci)))
   {
	  uint32_t v = mqc_raw_decode(mqc);
	  *datap += (v ^ (*datap < 0)) ? poshalf : -poshalf;
	  *flagsp |= T1_MU_THIS << (ci);
   }
}
#define dec_refpass_step_mqc_macro(flags, data, data_stride, ciorig, ci)              \
   {                                                                                  \
	  if((flags & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == (T1_SIGMA_THIS << (ci))) \
	  {                                                                               \
		 uint32_t ctxt = getctxno_mag(flags >> (ci));                                 \
		 setcurctx(curctx, ctxt);                                                     \
		 uint32_t v;                                                                  \
		 decompress_macro(v, mqc, curctx, a, c, ct);                                  \
		 (data)[ciorig * data_stride] +=                                              \
			 (v ^ ((data)[ciorig * data_stride] < 0)) ? poshalf : -poshalf;           \
		 flags |= T1_MU_THIS << (ci);                                                 \
	  }                                                                               \
   }
void T1::dec_refpass_raw(int32_t bpno)
{
   auto dataPtr = uncompressedData;
   auto flagsp = flags + 1 + (w + 2);
   const uint32_t l_w = w;

   int32_t one = 1 << bpno;
   int32_t poshalf = one >> 1;
   uint32_t k;
   for(k = 0; k < (h & ~3U); k += 4, flagsp += 2, dataPtr += 3 * l_w)
   {
	  for(uint32_t i = 0; i < l_w; ++i, ++flagsp, ++dataPtr)
	  {
		 if(*flagsp != 0)
		 {
			dec_refpass_step_raw(flagsp, dataPtr, poshalf, 0U);
			dec_refpass_step_raw(flagsp, dataPtr + l_w, poshalf, 3U);
			dec_refpass_step_raw(flagsp, dataPtr + 2 * l_w, poshalf, 6U);
			dec_refpass_step_raw(flagsp, dataPtr + 3 * l_w, poshalf, 9U);
		 }
	  }
   }
   if(k < h)
	  for(uint32_t i = 0; i < l_w; ++i, ++flagsp, ++dataPtr)
		 for(uint32_t j = 0; j < h - k; ++j)
			dec_refpass_step_raw(flagsp, dataPtr + j * l_w, poshalf, 3 * j);
}
#define dec_refpass_mqc_internal(bpno, w, h, flags_stride)                         \
   {                                                                               \
	  auto dataPtr = uncompressedData;                                             \
	  auto flagsp = flags + flags_stride + 1;                                      \
	  const uint32_t l_w = w;                                                      \
	  auto mqc = &(coder);                                                         \
	  PUSH_MQC();                                                                  \
	  int32_t one = 1 << bpno;                                                     \
	  int32_t poshalf = one >> 1;                                                  \
	  uint32_t k;                                                                  \
	  for(k = 0; k < (h & ~3u); k += 4, dataPtr += 3 * l_w, flagsp += 2)           \
	  {                                                                            \
		 for(uint32_t i = 0; i < l_w; ++i, ++dataPtr, ++flagsp)                    \
		 {                                                                         \
			auto _flags = *flagsp;                                                 \
			if(_flags != 0)                                                        \
			{                                                                      \
			   dec_refpass_step_mqc_macro(_flags, dataPtr, l_w, 0, 0);             \
			   dec_refpass_step_mqc_macro(_flags, dataPtr, l_w, 1, 3);             \
			   dec_refpass_step_mqc_macro(_flags, dataPtr, l_w, 2, 6);             \
			   dec_refpass_step_mqc_macro(_flags, dataPtr, l_w, 3, 9);             \
			   *flagsp = _flags;                                                   \
			}                                                                      \
		 }                                                                         \
	  }                                                                            \
	  if(k < h)                                                                    \
		 for(uint32_t i = 0; i < l_w; ++i, ++dataPtr, ++flagsp)                    \
			for(uint32_t j = 0; j < h - k; ++j)                                    \
			{                                                                      \
			   dec_refpass_step_mqc_macro(*flagsp, dataPtr + j * l_w, 0, j, j * 3) \
			}                                                                      \
	  POP_MQC();                                                                   \
   }
void T1::dec_refpass_mqc(int32_t bpno)
{
   if(w == 64 && h == 64)
	  dec_refpass_mqc_internal(bpno, 64, 64, 66) else dec_refpass_mqc_internal(bpno, w, h, w + 2U)
}
bool T1::decompress_cblk(DecompressCodeblock* cblk, uint8_t* compressedData, uint8_t orientation,
						 uint32_t cblksty)
{
   auto mqc = &coder;
   uint32_t cblkdataindex = 0;
   bool check_pterm = cblksty & GRK_CBLKSTY_PTERM;
   mqc->lut_ctxno_zc_orient = lut_ctxno_zc + (orientation << 9);
   int32_t bpno_plus_one = (int32_t)(cblk->numbps);
   if(bpno_plus_one >= (int32_t)maxBitPlanesGRK)
   {
	  grk::Logger::logger_.error("unsupported number of bit planes: %u > %u", bpno_plus_one,
								 maxBitPlanesGRK);
	  return false;
   }
   uint32_t passtype = 2;
   mqc_resetstates(mqc);

   for(uint32_t segno = 0; segno < cblk->getNumSegments(); ++segno)
   {
	  auto seg = cblk->getSegment(segno);
	  /* BYPASS mode */
	  uint8_t type = ((bpno_plus_one <= ((int32_t)(cblk->numbps)) - 4) && (passtype < 2) &&
					  (cblksty & GRK_CBLKSTY_LAZY))
						 ? T1_TYPE_RAW
						 : T1_TYPE_MQ;
	  if(type == T1_TYPE_RAW)
		 mqc_raw_init_dec(mqc, compressedData + cblkdataindex, seg->len);
	  else
		 mqc_init_dec(mqc, compressedData + cblkdataindex, seg->len);
	  cblkdataindex += seg->len;
	  for(uint32_t passno = 0; (passno < seg->numpasses) && (bpno_plus_one >= 1); ++passno)
	  {
		 switch(passtype)
		 {
			case 0:
			   if(type == T1_TYPE_RAW)
				  dec_sigpass_raw(bpno_plus_one, (int32_t)cblksty);
			   else
				  dec_sigpass_mqc(bpno_plus_one, (int32_t)cblksty);
			   break;
			case 1:
			   if(type == T1_TYPE_RAW)
				  dec_refpass_raw(bpno_plus_one);
			   else
				  dec_refpass_mqc(bpno_plus_one);
			   break;
			case 2:
			   dec_clnpass(bpno_plus_one, (int32_t)cblksty);
			   break;
		 }

		 if((cblksty & GRK_CBLKSTY_RESET) && type == T1_TYPE_MQ)
			mqc_resetstates(mqc);
		 if(++passtype == 3)
		 {
			passtype = 0;
			bpno_plus_one--;
		 }
	  }
	  mqc_finish_dec(mqc);
   }
   if(check_pterm)
   {
	  if(mqc->bp + 2 < mqc->end)
		 grk::Logger::logger_.warn(
			 "PTERM check failure: %u remaining bytes in code block (%u used / %u)",
			 (int)(mqc->end - mqc->bp) - 2, (int)(mqc->bp - mqc->start),
			 (int)(mqc->end - mqc->start));
	  else if(mqc->end_of_byte_stream_counter > 2)
		 grk::Logger::logger_.warn("PTERM check failure: %u synthesized 0xFF markers read",
								   mqc->end_of_byte_stream_counter);
   }

   return true;
}

} // namespace grk
