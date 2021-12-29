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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <cstddef>

namespace grk {

const uint32_t maxNumPackComponents = 10;

#define PUTBITS2_(s, nb)   {                                         \
	trailing <<= remaining;                                          \
	trailing |= (uint32_t)((s) >> (nb - remaining));                 \
	*(*dest)++ = (uint8_t)trailing;                                  \
	trailing = (uint32_t)((s) & ((1U << (nb - remaining)) - 1U));    \
	if(nb >= (remaining + 8))                                        \
	{                                                                \
		*(*dest)++ = (uint8_t)(trailing >> (nb - (remaining + 8)));  \
		trailing &= (uint32_t)((1U << (nb - (remaining + 8))) - 1U); \
		remaining += 16 - nb;                                        \
	}                                                                \
	else                                                             \
	{                                                                \
		remaining += 8 - nb;                                         \
	}																 \
}
#define PUTBITS_(s, nb)            \
	if(nb >= remaining)            \
	{                              \
		PUTBITS2_(s, nb)           \
	}                              \
	else                           \
	{                              \
		trailing <<= nb;           \
		trailing |= (uint32_t)(s); \
		remaining -= nb;           \
	}
#define FLUSHBITS_()                 \
	if(remaining != 8)               \
	{                                \
		trailing <<= remaining;      \
		*(*dest)++ = (uint8_t)trailing; \
	}
template <typename T> class Pack1  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) |
							(src5 << 2) | (src6 << 1) | src7);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		uint32_t src3 = 0U;
		uint32_t src4 = 0U;
		uint32_t src5 = 0U;
		uint32_t src6 = 0U;

		if(w > 1U)
		{
			src1 = (uint32_t)src[1];
			if(w > 2U)
			{
				src2 = (uint32_t)src[2];
				if(w > 3U)
				{
					src3 = (uint32_t)src[3];
					if(w > 4U)
					{
						src4 = (uint32_t)src[4];
						if(w > 5U)
						{
							src5 = (uint32_t)src[5];
							if(w > 6U)
								src6 = (uint32_t)src[6];
						}
					}
				}
			}
		}
		*(*dest)++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) |
							(src5 << 2) | (src6 << 1));
	}
};
template <typename T> class Pack2  {
public:
	static constexpr uint8_t srcChk = 4;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];

		*(*dest)++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2) | src3);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;

		if(w > 1U)
		{
			src1 = (uint32_t)src[1];
			if(w > 2U)
				src2 = (uint32_t)src[2];
		}
		*(*dest)++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2));
	}
};
template <typename T> class Pack3  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 << 5) | (src1 << 2) | (src2 >> 1));
		*(*dest)++ = (uint8_t)((src2 << 7) | (src3 << 4) | (src4 << 1) | (src5 >> 2));
		*(*dest)++ = (uint8_t)((src5 << 6) | (src6 << 3) | (src7));
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS_((uint32_t)src[j], 3);
		FLUSHBITS_()
	}
};

template <typename T> class Pack4  {
public:
	static constexpr uint8_t srcChk = 2;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		// IMPORTANT NOTE: we need to mask src1 to 4 bits,
		// to prevent sign extension bits of negative src1,
		// extending beyond 4 bits,
		// from contributing to destination value
		*(*dest)++ = (uint8_t)((src0 << 4) | (src1 & 0xF));
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		(void)w;
		*(*dest)++ = (uint8_t)(((uint32_t)src[0] << 4));
	}
};
template <typename T> class Pack5  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 << 3) | (src1 >> 2));
		*(*dest)++ = (uint8_t)((src1 << 6) | (src2 << 1) | (src3 >> 4));
		*(*dest)++ = (uint8_t)((src3 << 4) | (src4 >> 1));
		*(*dest)++ = (uint8_t)((src4 << 7) | (src5 << 2) | (src6 >> 3));
		*(*dest)++ = (uint8_t)((src6 << 5) | (src7));
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS_((uint32_t)src[j], 5)
		FLUSHBITS_()
	}
};
template <typename T> class Pack6  {
public:
	static constexpr uint8_t srcChk = 4;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];

		*(*dest)++ = (uint8_t)((src0 << 2) | (src1 >> 4));
		*(*dest)++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
		*(*dest)++ = (uint8_t)(((src2 & 0x3U) << 6) | src3);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;

		if(w > 1U)
		{
			src1 = (uint32_t)src[1];
			if(w > 2U)
				src2 = (uint32_t)src[2];
		}
		*(*dest)++ = (uint8_t)((src0 << 2) | (src1 >> 4));
		if(w > 1U)
		{
			*(*dest)++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
			if(w > 2U)
				*(*dest)++ = (uint8_t)(((src2 & 0x3U) << 6));
		}
	}
};
template <typename T> class Pack7  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 << 1) | (src1 >> 6));
		*(*dest)++ = (uint8_t)((src1 << 2) | (src2 >> 5));
		*(*dest)++ = (uint8_t)((src2 << 3) | (src3 >> 4));
		*(*dest)++ = (uint8_t)((src3 << 4) | (src4 >> 3));
		*(*dest)++ = (uint8_t)((src4 << 5) | (src5 >> 2));
		*(*dest)++ = (uint8_t)((src5 << 6) | (src6 >> 1));
		*(*dest)++ = (uint8_t)((src6 << 7) | (src7));
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS_((uint32_t)src[j], 7)
		FLUSHBITS_()
	}
};
template <typename T> class Pack8  {
};
template <typename T> class Pack9  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 >> 1));
		*(*dest)++ = (uint8_t)((src0 << 7) | (src1 >> 2));
		*(*dest)++ = (uint8_t)((src1 << 6) | (src2 >> 3));
		*(*dest)++ = (uint8_t)((src2 << 5) | (src3 >> 4));
		*(*dest)++ = (uint8_t)((src3 << 4) | (src4 >> 5));
		*(*dest)++ = (uint8_t)((src4 << 3) | (src5 >> 6));
		*(*dest)++ = (uint8_t)((src5 << 2) | (src6 >> 7));
		*(*dest)++ = (uint8_t)((src6 << 1) | (src7 >> 8));
		*(*dest)++ = (uint8_t)(src7);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS2_((uint32_t)src[j], 9)
		FLUSHBITS_()
	}
};
template <typename T> class Pack10  {
public:
	static constexpr uint8_t srcChk = 4;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];

		*(*dest)++ = (uint8_t)(src0 >> 2);
		*(*dest)++ = (uint8_t)(((src0 & 0x3U) << 6) | (src1 >> 4));
		*(*dest)++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 6));
		*(*dest)++ = (uint8_t)(((src2 & 0x3FU) << 2) | (src3 >> 8));
		*(*dest)++ = (uint8_t)(src3);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;

		if(w > 1U)
		{
			src1 = (uint32_t)src[1];
			if(w > 2U)
				src2 = (uint32_t)src[2];
		}
		*(*dest)++ = (uint8_t)(src0 >> 2);
		*(*dest)++ = (uint8_t)(((src0 & 0x3U) << 6) | (src1 >> 4));
		if(w > 1U)
		{
			*(*dest)++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 6));
			if(w > 2U)
				*(*dest)++ = (uint8_t)(((src2 & 0x3FU) << 2));
		}
	}
};
template <typename T> class Pack11  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 >> 3));
		*(*dest)++ = (uint8_t)((src0 << 5) | (src1 >> 6));
		*(*dest)++ = (uint8_t)((src1 << 2) | (src2 >> 9));
		*(*dest)++ = (uint8_t)((src2 >> 1));
		*(*dest)++ = (uint8_t)((src2 << 7) | (src3 >> 4));
		*(*dest)++ = (uint8_t)((src3 << 4) | (src4 >> 7));
		*(*dest)++ = (uint8_t)((src4 << 1) | (src5 >> 10));
		*(*dest)++ = (uint8_t)((src5 >> 2));
		*(*dest)++ = (uint8_t)((src5 << 6) | (src6 >> 5));
		*(*dest)++ = (uint8_t)((src6 << 3) | (src7 >> 8));
		*(*dest)++ = (uint8_t)(src7);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS2_((uint32_t)src[j], 11)
		FLUSHBITS_()
	}
};
template <typename T> class Pack12  {
public:
	static constexpr uint8_t srcChk = 2;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];

		*(*dest)++ = (uint8_t)(src0 >> 4);
		*(*dest)++ = (uint8_t)(((src0 & 0xFU) << 4) | (src1 >> 8));
		*(*dest)++ = (uint8_t)(src1);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		(void)w;
		uint32_t src0 = (uint32_t)src[0];
		*(*dest)++ = (uint8_t)(src0 >> 4);
		*(*dest)++ = (uint8_t)(((src0 & 0xFU) << 4));
	}
};
template <typename T> class Pack13  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 >> 5));
		*(*dest)++ = (uint8_t)((src0 << 3) | (src1 >> 10));
		*(*dest)++ = (uint8_t)((src1 >> 2));
		*(*dest)++ = (uint8_t)((src1 << 6) | (src2 >> 7));
		*(*dest)++ = (uint8_t)((src2 << 1) | (src3 >> 12));
		*(*dest)++ = (uint8_t)((src3 >> 4));
		*(*dest)++ = (uint8_t)((src3 << 4) | (src4 >> 9));
		*(*dest)++ = (uint8_t)((src4 >> 1));
		*(*dest)++ = (uint8_t)((src4 << 7) | (src5 >> 6));
		*(*dest)++ = (uint8_t)((src5 << 2) | (src6 >> 11));
		*(*dest)++ = (uint8_t)((src6 >> 3));
		*(*dest)++ = (uint8_t)((src6 << 5) | (src7 >> 8));
		*(*dest)++ = (uint8_t)(src7);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS2_((uint32_t)src[j], 13)
		FLUSHBITS_()
	}
};
template <typename T> class Pack14  {
public:
	static constexpr uint8_t srcChk = 4;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];

		*(*dest)++ = (uint8_t)(src0 >> 6);
		*(*dest)++ = (uint8_t)(((src0 & 0x3FU) << 2) | (src1 >> 12));
		*(*dest)++ = (uint8_t)(src1 >> 4);
		*(*dest)++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 10));
		*(*dest)++ = (uint8_t)(src2 >> 2);
		*(*dest)++ = (uint8_t)(((src2 & 0x3U) << 6) | (src3 >> 8));
		*(*dest)++ = (uint8_t)(src3);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;

		if(w > 1U)
		{
			src1 = (uint32_t)src[1];
			if(w > 2U)
				src2 = (uint32_t)src[2];
		}
		*(*dest)++ = (uint8_t)(src0 >> 6);
		*(*dest)++ = (uint8_t)(((src0 & 0x3FU) << 2) | (src1 >> 12));
		if(w > 1U)
		{
			*(*dest)++ = (uint8_t)(src1 >> 4);
			*(*dest)++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 10));
			if(w > 2U)
			{
				*(*dest)++ = (uint8_t)(src2 >> 2);
				*(*dest)++ = (uint8_t)(((src2 & 0x3U) << 6));
			}
		}
	}
};
template <typename T> class Pack15  {
public:
	static constexpr uint8_t srcChk = 8;
	inline void pack(const T* src, uint8_t **dest){
		uint32_t src0 = (uint32_t)src[0];
		uint32_t src1 = (uint32_t)src[1];
		uint32_t src2 = (uint32_t)src[2];
		uint32_t src3 = (uint32_t)src[3];
		uint32_t src4 = (uint32_t)src[4];
		uint32_t src5 = (uint32_t)src[5];
		uint32_t src6 = (uint32_t)src[6];
		uint32_t src7 = (uint32_t)src[7];

		*(*dest)++ = (uint8_t)((src0 >> 7));
		*(*dest)++ = (uint8_t)((src0 << 1) | (src1 >> 14));
		*(*dest)++ = (uint8_t)((src1 >> 6));
		*(*dest)++ = (uint8_t)((src1 << 2) | (src2 >> 13));
		*(*dest)++ = (uint8_t)((src2 >> 5));
		*(*dest)++ = (uint8_t)((src2 << 3) | (src3 >> 12));
		*(*dest)++ = (uint8_t)((src3 >> 4));
		*(*dest)++ = (uint8_t)((src3 << 4) | (src4 >> 11));
		*(*dest)++ = (uint8_t)((src4 >> 3));
		*(*dest)++ = (uint8_t)((src4 << 5) | (src5 >> 10));
		*(*dest)++ = (uint8_t)((src5 >> 2));
		*(*dest)++ = (uint8_t)((src5 << 6) | (src6 >> 9));
		*(*dest)++ = (uint8_t)((src6 >> 1));
		*(*dest)++ = (uint8_t)((src6 << 7) | (src7 >> 8));
		*(*dest)++ = (uint8_t)(src7);
	}
	inline void packFinal(const T* src, uint8_t **dest, size_t w){
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < w; ++j)
			PUTBITS2_((uint32_t)src[j], 15)
		FLUSHBITS_()
	}
};
template <typename T> class Pack16  {
};

template <typename T> class Pack16BE  {
};

template <typename T> class PtoI {
public:
	virtual ~PtoI() = default;
	virtual void interleave(T ** src,
							const uint32_t numPlanes,
							uint8_t* dest,
							const uint32_t w,
							const uint32_t srcStride,
							const uint64_t destStride,
							const uint32_t h,
							const int32_t adjust) = 0;
	static uint64_t getPackedBytes(uint16_t numcomps, uint32_t w, uint8_t prec){
		return ((uint64_t)w * numcomps * prec + 7U) / 8U;
	}
};

template <typename T, typename P> class PlanarToInterleaved : public PtoI<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t w,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			{
				auto destPtr = dest;
				P packer;
				T srcBuf[2 * maxNumPackComponents];
				size_t srcOff = 0;
				for(size_t j = 0; j < w; j++)
				{
					for(size_t k = 0; k < numPlanes; ++k)
						srcBuf[srcOff + k] = src[k][j] + adjust;
					srcOff    += numPlanes;
					size_t packOff = 0;
					while (srcOff >= packer.srcChk) {
						packer.pack(srcBuf + packOff, &destPtr);
						srcOff -= packer.srcChk;
						packOff += packer.srcChk;
					}
					for(size_t k = 0; k < srcOff; ++k)
						srcBuf[k] = srcBuf[packOff+k];
				}
				if (srcOff)
					packer.packFinal(srcBuf, &destPtr,srcOff);
			}
			dest += destStride;
			for(size_t k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved<T, Pack8<T> > : public PtoI<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t w,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			auto destPtr = dest;
			for(size_t j = 0; j < w; j++)
				for(size_t k = 0; k < numPlanes; ++k)
					*destPtr++ = (uint8_t)(src[k][j] + adjust);
			dest += destStride;
			for(size_t k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved<T, Pack16<T> > : public PtoI<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t w,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			auto destPtr = dest;
			for(size_t j = 0; j < w; j++)
				for(size_t k = 0; k < numPlanes; ++k) {
					*(uint16_t*)destPtr = (uint16_t)(src[k][j] + adjust);
					destPtr+=2;
				}
			dest += destStride;
			for(size_t k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved<T, Pack16BE<T> > : public PtoI<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t w,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			auto destPtr = dest;
			for(size_t j = 0; j < w; j++)
				for(size_t k = 0; k < numPlanes; ++k) {
					uint32_t val = (uint32_t)(src[k][j] + adjust);
					*(destPtr)++ = (uint8_t)(val >> 8);
					*(destPtr)++ = (uint8_t)val;
				}
			dest += destStride;
			for(size_t k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template<typename T> class InterleaverFactory {
public:
	static PtoI<T>* makeInterleaver(uint8_t prec){
		switch(prec){
		case 1:
			return new PlanarToInterleaved<T, Pack1<T>>();
		case 2:
			return new PlanarToInterleaved<T, Pack2<T>>();
		case 3:
			return new PlanarToInterleaved<T, Pack3<T>>();
		case 4:
			return new PlanarToInterleaved<T, Pack4<T>>();
		case 5:
			return new PlanarToInterleaved<T, Pack5<T>>();
		case 6:
			return new PlanarToInterleaved<T, Pack6<T>>();
		case 7:
			return new PlanarToInterleaved<T, Pack7<T>>();
		case 8:
			return new PlanarToInterleaved<T, Pack8<T>>();
		case 9:
			return new PlanarToInterleaved<T, Pack9<T>>();
		case 10:
			return new PlanarToInterleaved<T, Pack10<T>>();
		case 11:
			return new PlanarToInterleaved<T, Pack11<T>>();
		case 12:
			return new PlanarToInterleaved<T, Pack12<T>>();
		case 13:
			return new PlanarToInterleaved<T, Pack13<T>>();
		case 14:
			return new PlanarToInterleaved<T, Pack14<T>>();
		case 15:
			return new PlanarToInterleaved<T, Pack15<T>>();
		case 16:
			return new PlanarToInterleaved<T, Pack16<T>>();
		case 0xFF:
			return new PlanarToInterleaved<T, Pack16BE<T>>();
		default:
			return nullptr;
		}
	}
};

}
