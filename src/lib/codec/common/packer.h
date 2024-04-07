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
const uint8_t packer16BitBE = 0xFF;



#define PUTBITS2(s, nb)   {                                         \
	trailing <<= remaining;                                          \
	trailing |= (uint32_t)((s) >> (nb - remaining));                 \
	*destPtr++ = (uint8_t)trailing;                                  \
	trailing = (uint32_t)((s) & ((1U << (nb - remaining)) - 1U));    \
	if(nb >= (remaining + 8))                                        \
	{                                                                \
		*destPtr++ = (uint8_t)(trailing >> (nb - (remaining + 8)));  \
		trailing &= (uint32_t)((1U << (nb - (remaining + 8))) - 1U); \
		remaining += 16 - nb;                                        \
	}                                                                \
	else                                                             \
	{                                                                \
		remaining += 8 - nb;                                         \
	}																 \
}
#define PUTBITS(s, nb)            \
	if(nb >= remaining)            \
	{                              \
		PUTBITS2(s, nb)           \
	}                              \
	else                           \
	{                              \
		trailing <<= nb;           \
		trailing |= (uint32_t)(s); \
		remaining -= nb;           \
	}
#define FLUSHBITS()                 \
	if(remaining != 8)               \
	{                                \
		trailing <<= remaining;      \
		*destPtr++ = (uint8_t)trailing; \
	}

#define NEXT_PACK() { \
	next = (uint32_t)(src[k++][j] + adjust); \
	if (k == numPlanes) { \
		k = 0; \
		j++; \
	} \
	ct++; \
}

#define NEXT_8 \
		NEXT_PACK() \
		uint32_t src0 = next; \
		NEXT_PACK() \
		uint32_t src1 = next; \
		NEXT_PACK() \
		uint32_t src2 = next; \
		NEXT_PACK() \
		uint32_t src3 = next; \
		NEXT_PACK() \
		uint32_t src4 = next; \
		NEXT_PACK() \
		uint32_t src5 = next; \
		NEXT_PACK() \
		uint32_t src6 = next; \
		NEXT_PACK() \
		uint32_t src7 = next;

#define NEXT_4 \
		NEXT_PACK() \
		uint32_t src0 = next; \
		NEXT_PACK() \
		uint32_t src1 = next; \
		NEXT_PACK() \
		uint32_t src2 = next; \
		NEXT_PACK() \
		uint32_t src3 = next;

template <typename T> class PlanarToInterleaved {
public:
	virtual ~PlanarToInterleaved() = default;
	virtual void interleave(T ** src,
							const uint32_t numPlanes,
							uint8_t* dest,
							const uint32_t srcWidth,
							const uint32_t srcStride,
							const uint64_t destStride,
							const uint32_t h,
							const int32_t adjust) = 0;
	static uint64_t getPackedBytes(uint16_t numcomps, uint32_t w, uint8_t prec){
		return ((uint64_t)w * numcomps * prec + 7U) / 8U;
	}
};


template <typename T> class PlanarToInterleaved1: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		uint64_t lengthLeftover = length & 7;
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) |
									(src5 << 2) | (src6 << 1) | src7);
			}
			if (lengthLeftover){
				ct = 0;
				NEXT_PACK()
				uint32_t src0 = next;
				uint32_t src1 = 0U;
				uint32_t src2 = 0U;
				uint32_t src3 = 0U;
				uint32_t src4 = 0U;
				uint32_t src5 = 0U;
				uint32_t src6 = 0U;

				if(ct < lengthLeftover)
				{
					NEXT_PACK()
					src1 = next;
					if(ct < lengthLeftover)
					{
						NEXT_PACK()
						src2 = next;
						if(ct < lengthLeftover)
						{
							NEXT_PACK()
							src3 = next;
							if(ct < lengthLeftover)
							{
								NEXT_PACK()
								src4 = next;
								if(ct < lengthLeftover)
								{
									NEXT_PACK()
									src5 = next;
									if(ct < lengthLeftover) {
										NEXT_PACK()
										src6 = next;
									}
								}
							}
						}
					}
				}
				*destPtr++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) |
									(src5 << 2) | (src6 << 1));
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};


template <typename T> class PlanarToInterleaved2: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~3));
		uint64_t lengthLeftover = length & 3;
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_4
				*destPtr++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2) | src3);
			}
			if (lengthLeftover){
				ct = 0;
				NEXT_PACK()
				uint32_t src0 = next;
				uint32_t src1 = 0U;
				uint32_t src2 = 0U;
				if(ct < lengthLeftover)
				{
					NEXT_PACK()
					src1 = next;
					if(ct < lengthLeftover) {
						NEXT_PACK()
						src2 = next;
					}
				}
				*destPtr++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2));
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved3: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 << 5) | (src1 << 2) | (src2 >> 1));
				*destPtr++ = (uint8_t)((src2 << 7) | (src3 << 4) | (src4 << 1) | (src5 >> 2));
				*destPtr++ = (uint8_t)((src5 << 6) | (src6 << 3) | (src7));
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS(next, 3)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};


template <typename T> class PlanarToInterleaved4: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~1));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_PACK()
				uint32_t src0 = next;
				NEXT_PACK()
				uint32_t src1 = next;
				// IMPORTANT NOTE: we need to mask src1 to 4 bits,
				// to prevent sign extension bits of negative src1,
				// extending beyond 4 bits,
				// from contributing to destination value
				*destPtr++ = (uint8_t)((src0 << 4) | (src1 & 0xF));
			}
			if (ct < length){
				NEXT_PACK()
				*destPtr++ = (uint8_t)((next << 4));
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved5: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 << 3) | (src1 >> 2));
				*destPtr++ = (uint8_t)((src1 << 6) | (src2 << 1) | (src3 >> 4));
				*destPtr++ = (uint8_t)((src3 << 4) | (src4 >> 1));
				*destPtr++ = (uint8_t)((src4 << 7) | (src5 << 2) | (src6 >> 3));
				*destPtr++ = (uint8_t)((src6 << 5) | (src7));
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS(next, 5)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved6: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~3));
		uint64_t lengthLeftover = length & 3;
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_4
				*destPtr++ = (uint8_t)((src0 << 2) | (src1 >> 4));
				*destPtr++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
				*destPtr++ = (uint8_t)(((src2 & 0x3U) << 6) | src3);
			}
			if (lengthLeftover){
				ct = 0;
				NEXT_PACK()
				uint32_t src0 = next;
				uint32_t src1 = 0U;
				uint32_t src2 = 0U;
				if(ct < lengthLeftover)
				{
					NEXT_PACK()
					src1 = next;
					if(ct < lengthLeftover) {
						NEXT_PACK()
						src2 = next;
					}
				}
				*destPtr++ = (uint8_t)((src0 << 2) | (src1 >> 4));
				if(ct > 1U)
				{
					*destPtr++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
					if(ct > 2U)
						*destPtr++ = (uint8_t)(((src2 & 0x3U) << 6));
				}

			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved7: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 << 1) | (src1 >> 6));
				*destPtr++ = (uint8_t)((src1 << 2) | (src2 >> 5));
				*destPtr++ = (uint8_t)((src2 << 3) | (src3 >> 4));
				*destPtr++ = (uint8_t)((src3 << 4) | (src4 >> 3));
				*destPtr++ = (uint8_t)((src4 << 5) | (src5 >> 2));
				*destPtr++ = (uint8_t)((src5 << 6) | (src6 >> 1));
				*destPtr++ = (uint8_t)((src6 << 7) | (src7));
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS(next, 7)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};


template <typename T> class PlanarToInterleaved8: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			auto destPtr = dest;
			for(size_t j = 0; j < srcWidth; j++)
				for(size_t k = 0; k < numPlanes; ++k)
					*destPtr++ = (uint8_t)(src[k][j] + adjust);
			dest += destStride;
			for(size_t k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};


template <typename T> class PlanarToInterleaved9: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 >> 1));
				*destPtr++ = (uint8_t)((src0 << 7) | (src1 >> 2));
				*destPtr++ = (uint8_t)((src1 << 6) | (src2 >> 3));
				*destPtr++ = (uint8_t)((src2 << 5) | (src3 >> 4));
				*destPtr++ = (uint8_t)((src3 << 4) | (src4 >> 5));
				*destPtr++ = (uint8_t)((src4 << 3) | (src5 >> 6));
				*destPtr++ = (uint8_t)((src5 << 2) | (src6 >> 7));
				*destPtr++ = (uint8_t)((src6 << 1) | (src7 >> 8));
				*destPtr++ = (uint8_t)(src7);
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS2(next, 9)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};


template <typename T> class PlanarToInterleaved10: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~3));
		uint64_t lengthLeftover = length & 3;
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_4
				*destPtr++ = (uint8_t)(src0 >> 2);
				*destPtr++ = (uint8_t)(((src0 & 0x3U) << 6) | (src1 >> 4));
				*destPtr++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 6));
				*destPtr++ = (uint8_t)(((src2 & 0x3FU) << 2) | (src3 >> 8));
				*destPtr++ = (uint8_t)(src3);
			}
			if (lengthLeftover){
				ct = 0;
				NEXT_PACK()
				uint32_t src0 = next;
				uint32_t src1 = 0U;
				uint32_t src2 = 0U;
				if(ct < lengthLeftover)
				{
					NEXT_PACK()
					src1 = next;
					if(ct < lengthLeftover) {
						NEXT_PACK()
						src2 = next;
					}
				}
				*destPtr++ = (uint8_t)(src0 >> 2);
				*destPtr++ = (uint8_t)(((src0 & 0x3U) << 6) | (src1 >> 4));
				if(ct > 1U)
				{
					*destPtr++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 6));
					if(ct > 2U)
						*destPtr++ = (uint8_t)(((src2 & 0x3FU) << 2));
				}
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};


template <typename T> class PlanarToInterleaved11: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 >> 3));
				*destPtr++ = (uint8_t)((src0 << 5) | (src1 >> 6));
				*destPtr++ = (uint8_t)((src1 << 2) | (src2 >> 9));
				*destPtr++ = (uint8_t)((src2 >> 1));
				*destPtr++ = (uint8_t)((src2 << 7) | (src3 >> 4));
				*destPtr++ = (uint8_t)((src3 << 4) | (src4 >> 7));
				*destPtr++ = (uint8_t)((src4 << 1) | (src5 >> 10));
				*destPtr++ = (uint8_t)((src5 >> 2));
				*destPtr++ = (uint8_t)((src5 << 6) | (src6 >> 5));
				*destPtr++ = (uint8_t)((src6 << 3) | (src7 >> 8));
				*destPtr++ = (uint8_t)(src7);
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS2(next, 11)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved12: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~1));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_PACK()
				uint32_t src0 = next;
				NEXT_PACK()
				uint32_t src1 = next;

				*destPtr++ = (uint8_t)(src0 >> 4);
				*destPtr++ = (uint8_t)(((src0 & 0xFU) << 4) | (src1 >> 8));
				*destPtr++ = (uint8_t)(src1);
			}
			if (ct < length){
				NEXT_PACK()
				uint32_t src0 = next;
				*destPtr++ = (uint8_t)(src0 >> 4);
				*destPtr = (uint8_t)(((src0 & 0xFU) << 4));
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved13: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 >> 5));
				*destPtr++ = (uint8_t)((src0 << 3) | (src1 >> 10));
				*destPtr++ = (uint8_t)((src1 >> 2));
				*destPtr++ = (uint8_t)((src1 << 6) | (src2 >> 7));
				*destPtr++ = (uint8_t)((src2 << 1) | (src3 >> 12));
				*destPtr++ = (uint8_t)((src3 >> 4));
				*destPtr++ = (uint8_t)((src3 << 4) | (src4 >> 9));
				*destPtr++ = (uint8_t)((src4 >> 1));
				*destPtr++ = (uint8_t)((src4 << 7) | (src5 >> 6));
				*destPtr++ = (uint8_t)((src5 << 2) | (src6 >> 11));
				*destPtr++ = (uint8_t)((src6 >> 3));
				*destPtr++ = (uint8_t)((src6 << 5) | (src7 >> 8));
				*destPtr++ = (uint8_t)(src7);
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS2(next, 13)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved14: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~3));
		uint64_t lengthLeftover = length & 3;
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_4
				*destPtr++ = (uint8_t)(src0 >> 6);
				*destPtr++ = (uint8_t)(((src0 & 0x3FU) << 2) | (src1 >> 12));
				*destPtr++ = (uint8_t)(src1 >> 4);
				*destPtr++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 10));
				*destPtr++ = (uint8_t)(src2 >> 2);
				*destPtr++ = (uint8_t)(((src2 & 0x3U) << 6) | (src3 >> 8));
				*destPtr++ = (uint8_t)(src3);
			}
			if (lengthLeftover){
				ct = 0;
				NEXT_PACK()
				uint32_t src0 = next;
				uint32_t src1 = 0U;
				uint32_t src2 = 0U;
				if(ct < lengthLeftover)
				{
					NEXT_PACK()
					src1 = next;
					if(ct < lengthLeftover) {
						NEXT_PACK()
						src2 = next;
					}
				}
				*destPtr++ = (uint8_t)(src0 >> 6);
				*destPtr++ = (uint8_t)(((src0 & 0x3FU) << 2) | (src1 >> 12));
				if(ct > 1U)
				{
					*destPtr++ = (uint8_t)(src1 >> 4);
					*destPtr++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 10));
					if(ct > 2U)
					{
						*destPtr++ = (uint8_t)(src2 >> 2);
						*destPtr++ = (uint8_t)(((src2 & 0x3U) << 6));
					}
				}
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved15: public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		uint64_t length = (uint64_t)srcWidth * numPlanes;
		uint64_t lengthTrunc = (uint64_t)(length & (uint64_t)(~7));
		for(size_t i = 0; i < h; i++) {
			uint32_t next = 0;
			auto destPtr = dest;
			uint64_t ct = 0;
			size_t k = 0;
			size_t j = 0;
			while(ct < lengthTrunc) {
				NEXT_8
				*destPtr++ = (uint8_t)((src0 >> 7));
				*destPtr++ = (uint8_t)((src0 << 1) | (src1 >> 14));
				*destPtr++ = (uint8_t)((src1 >> 6));
				*destPtr++ = (uint8_t)((src1 << 2) | (src2 >> 13));
				*destPtr++ = (uint8_t)((src2 >> 5));
				*destPtr++ = (uint8_t)((src2 << 3) | (src3 >> 12));
				*destPtr++ = (uint8_t)((src3 >> 4));
				*destPtr++ = (uint8_t)((src3 << 4) | (src4 >> 11));
				*destPtr++ = (uint8_t)((src4 >> 3));
				*destPtr++ = (uint8_t)((src4 << 5) | (src5 >> 10));
				*destPtr++ = (uint8_t)((src5 >> 2));
				*destPtr++ = (uint8_t)((src5 << 6) | (src6 >> 9));
				*destPtr++ = (uint8_t)((src6 >> 1));
				*destPtr++ = (uint8_t)((src6 << 7) | (src7 >> 8));
				*destPtr++ = (uint8_t)(src7);
			}
			if (ct < length){
				uint32_t trailing = 0U;
				int remaining = 8U;
				while (ct < length) {
					NEXT_PACK()
					PUTBITS2(next, 15)
				}
				FLUSHBITS()
			}
			dest += destStride;
			for(k = 0; k < numPlanes; ++k)
				src[k] += srcStride;
		}
	}
};

template <typename T> class PlanarToInterleaved16 : public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			auto destPtr = dest;
			for(size_t j = 0; j < srcWidth; j++)
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

template <typename T> class PlanarToInterleaved16BE : public PlanarToInterleaved<T>{
public:
	void interleave(T **src,
					const uint32_t numPlanes,
					uint8_t* dest,
					const uint32_t srcWidth,
					const uint32_t srcStride,
					const uint64_t destStride,
					const uint32_t h,
					const int32_t adjust) override
	{
		for(size_t i = 0; i < h; i++) {
			auto destPtr = dest;
			for(size_t j = 0; j < srcWidth; j++)
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
	static PlanarToInterleaved<T>* makeInterleaver(uint8_t prec){
		switch(prec){
		case 1:
			return new PlanarToInterleaved1<T>();
		case 2:
			return new PlanarToInterleaved2<T>();
		case 3:
			return new PlanarToInterleaved3<T>();
		case 4:
			return new PlanarToInterleaved4<T>();
		case 5:
			return new PlanarToInterleaved5<T>();
		case 6:
			return new PlanarToInterleaved6<T>();
		case 7:
			return new PlanarToInterleaved7<T>();
		case 8:
			return new PlanarToInterleaved8<T>();
		case 9:
			return new PlanarToInterleaved9<T>();
		case 10:
			return new PlanarToInterleaved10<T>();
		case 11:
			return new PlanarToInterleaved11<T>();
		case 12:
			return new PlanarToInterleaved12<T>();
		case 13:
			return new PlanarToInterleaved13<T>();
		case 14:
			return new PlanarToInterleaved14<T>();
		case 15:
			return new PlanarToInterleaved15<T>();
		case 16:
			return new PlanarToInterleaved16<T>();
		case packer16BitBE:
			return new PlanarToInterleaved16BE<T>();
		default:
			return nullptr;
		}
	}
};

}
