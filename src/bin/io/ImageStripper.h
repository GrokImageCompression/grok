/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
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
 */


#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>

#include "IBufferPool.h"
#include "RefCounted.h"
#include "util.h"

namespace io {

/*
 * Each strip is divided into a collection of IOChunks, and
 * each IOChunk contains an IOBuf, which is used for disk IO.
 * An IOBuf's offset is always aligned, and its length is always equal to WRTSIZE,
 * except possibly the final IOBuf of the final strip. Also, they are corrected
 * for the header bytes which are located right before the beginning of the
 * first strip - the header bytes are included in the first IOBuf of the first
 * strip.
 *
 * IOChunks can be shared between neighbouring strips if they
 * share a common seam, which happens when the boundary between two strips
 * is not aligned.
 */

struct ChunkInfo{
	ChunkInfo() : ChunkInfo(false,false,0,0,0,0,0,0)
	{}
	ChunkInfo(bool isFirstStrip,
			 bool isFinalStrip,
			 uint64_t logicalOffset,
			 uint64_t logicalLen,
			 uint64_t logicalOffsetPrev,
			 uint64_t logicalLenPrev,
			 uint64_t headerSize,
			 uint64_t writeSize) 	:
						isFirstStrip_(isFirstStrip),
						isFinalStrip_(isFinalStrip),
						writeSize_(writeSize),
						headerSize_(headerSize),
						pool_(nullptr)
	{
		if (!writeSize_)
			return;
		last_.x0_      = lastBegin(logicalOffset,logicalLen);
		assert(IOBuf::isAlignedToWriteSize(last_.x0_));
		last_.x1_      = stripEnd(logicalOffset,logicalLen);
		first_.x0_     = stripOffset(logicalOffset);
		first_.x1_     =
				(isFirstStrip_ ? 0 :
						lastBegin(logicalOffsetPrev,logicalLenPrev)) + writeSize_;
		// clamp first_ end to physical strip length
		first_.x1_ = std::min(first_.x1_,last_.x1_);
		bool firstOverlapsLast = first_.x1_ == last_.x1_;
		(void)firstOverlapsLast;
		assert(firstOverlapsLast || (last_.x0_ - first_.x1_) % writeSize_ == 0);
		assert(first_.valid());
		assert(last_.valid());
		assert(firstOverlapsLast || (first_.x1_ <= last_.x0_));
	}
	ChunkInfo(const ChunkInfo &rhs) : first_(rhs.first_),
										last_(rhs.last_),
										isFirstStrip_(rhs.isFirstStrip_),
										isFinalStrip_(rhs.isFinalStrip_),
										writeSize_(rhs.writeSize_),
										headerSize_(rhs.headerSize_),
										pool_(nullptr)
	{}
	uint64_t len(){
		return last_.x1_ - first_.x0_;
	}
	uint64_t numChunks(void){
		bool firstOverlapsLast = first_.x1_ == last_.x1_;
		if (firstOverlapsLast)
			return 1;
		uint64_t nonSeamBegin = hasFirstSeam() ? first_.x1_ : first_.x0_;
		uint64_t nonSeamEnd   = hasLastSeam()  ? last_.x0_  : last_.x1_;
		assert(nonSeamEnd >= nonSeamBegin );
		assert(IOBuf::isAlignedToWriteSize(nonSeamBegin));
		assert(isFinalStrip_ || IOBuf::isAlignedToWriteSize(nonSeamEnd));
		uint64_t rc = (nonSeamEnd - nonSeamBegin + writeSize_ - 1) / writeSize_;
		if (hasFirstSeam())
			rc++;
		if (hasLastSeam())
			rc++;
		assert(rc > 1);

		return rc;
	}
	bool hasFirstSeam(void){
		return !isFirstStrip_ && !IOBuf::isAlignedToWriteSize(first_.x0_);
	}
	bool hasLastSeam(void){
		return !isFinalStrip_ && !IOBuf::isAlignedToWriteSize(last_.x1_);
	}
	// not usually aligned
	uint64_t stripOffset(uint64_t logicalOffset){
		// header bytes added to first strip shifts all other strips
		// by that number of bytes
		return isFirstStrip_ ? 0 : headerSize_ + logicalOffset;
	}
	// not usually aligned
	uint64_t stripEnd(uint64_t logicalOffset, uint64_t logicalLen){
		uint64_t rc = stripOffset(logicalOffset) + logicalLen;
		//correct for header bytes added to first strip
		if (isFirstStrip_)
			rc += headerSize_;

		return rc;
	}
	// always aligned
	uint64_t lastBegin(uint64_t logicalOffset,uint64_t logicalLen){
		return (stripEnd(logicalOffset,logicalLen)/writeSize_) * writeSize_;
	}
	ChunkInfo operator=(const ChunkInfo& rhs){
		first_        = rhs.first_;
		last_         = rhs.last_;
		isFirstStrip_ = rhs.isFirstStrip_;
		isFinalStrip_ = rhs.isFinalStrip_;
		writeSize_    = rhs.writeSize_;
		headerSize_   = rhs.headerSize_;
		pool_         = rhs.pool_;

		return *this;
	}
	// first_ and last_ are usually disjoint, except in the case
	// where there is only one chunk in the final strip.
	// In that case, first_.x1_ == last_.x1_.
	// In all cases, last_.x0_ is aligned on writeSize_ boundary
	BufDim first_;
	BufDim last_;
	bool isFirstStrip_;
	bool isFinalStrip_;
	uint64_t writeSize_;
	uint64_t headerSize_;
	IBufferPool *pool_;
};
struct IOChunk : public RefCounted {
	IOChunk(uint64_t offset,uint64_t len, uint64_t allocLen, IBufferPool *pool) :
		offset_(offset),
		len_(len),
		allocLen_(allocLen),
		buf_(nullptr),
		acquireCount_(0),
		shareCount_(1)
	{
		if (pool){
			alloc(pool);
			share();
		}
	}
private:
	~IOChunk() {
		RefReaper::unref(buf_);
	}
public:
	IOChunk* share(void){
		shareCount_++;
		assert(buf_->data_);
		ref();

		return this;
	}
	bool isShared(void){
		return shareCount_ > 1;
	}
	void setHeader(uint8_t *headerData, uint64_t headerSize){
		memcpy(buf_->data_ , headerData, headerSize);
		buf_->skip_ = headerSize;
	}
	bool acquire(void){
		 return (++acquireCount_ == shareCount_);
	}
	IOBuf* buf(void) const{
		return buf_;
	}
	void updateLen(uint64_t len){
		assert(len <= allocLen_);
		len_ = len;
		if (buf_)
			buf_->updateLen(len);
	}
	void alloc(IBufferPool* pool){
		assert(!buf_ || buf_->data_);
		assert(pool);
		if (buf_ && buf_->data_)
			return;
		buf_ = pool->get(allocLen_);
		buf_->len_ = len_;
		buf_->offset_ = offset_;
	}
	uint64_t offset_;
	uint64_t len_;
private:
	uint64_t allocLen_;
	IOBuf *buf_;
	std::atomic<uint32_t> acquireCount_;
	uint32_t shareCount_;
};


/**
 * A strip chunk wraps an IOChunk (which may be shared with the
 * strip's neighbour), and it also stores a write offset and write length
 * for its assigned portion of a shared IOChunk.
 * If there is no sharing, then  write offset is zero,
 * and write length equals WRTSIZE.
 */
struct StripChunk : public RefCounted {
	StripChunk(IOChunk *ioChunk,
				uint64_t writeableOffset,
				uint64_t writeableLen) :
		writeableOffset_(writeableOffset),
		writeableLen_(writeableLen),
		ioChunk_(ioChunk)
	{
		// we may need to extend a shared ioChunk_'s length
		if (ioChunk_->isShared() &&
				ioChunk_->offset_ + ioChunk_->len_ == writeableOffset)
			ioChunk_->updateLen(ioChunk_->len_ + writeableLen);

		assert(writeableOffset < len());
		assert(writeableLen <= len());

	}
private:
	~StripChunk(){
		RefReaper::unref(ioChunk_);
	}
public:
	void alloc(IBufferPool* pool){
		ioChunk_->alloc(pool);
	}
	uint64_t offset(void){
		return ioChunk_->offset_;
	}
	uint64_t len(void){
		return ioChunk_->len_;
	}
	bool acquire(void){
		return ioChunk_->acquire();
	}
	bool isShared(void){
		return ioChunk_->isShared();
	}
	void setHeader(uint8_t *headerData, uint64_t headerSize){
		ioChunk_->setHeader(headerData, headerSize);
		writeableOffset_ = headerSize;
	}
	// relative to beginning of IOBuf data buffer
	uint64_t writeableOffset_;
	uint64_t writeableLen_;
	IOChunk *ioChunk_;
};


/**
 * Container for an array of buffers and an array of chunks
 *
 */
struct StripChunkArray{
	StripChunkArray(StripChunk** chunks, IOBuf **buffers,uint64_t numBuffers, IBufferPool *pool)
		: ioBufs_(buffers),
		  stripChunks_(chunks),
		  numBuffers_(numBuffers),
		  pool_(pool)
	{
		for (uint64_t i = 0; i < numBuffers_; ++i)
			assert(ioBufs_[i]->data_);
	}
	~StripChunkArray(void){
		delete[] ioBufs_;
		delete[] stripChunks_;
	}
	IOBuf ** ioBufs_;
	StripChunk ** stripChunks_;
	uint64_t numBuffers_;
	IBufferPool *pool_;
};

/**
 * A strip contains an array of StripChunks
 *
 */
struct Strip  {
	Strip(uint64_t offset,uint64_t len,Strip* neighbour) :
		logicalOffset_(offset),
		logicalLen_(len),
		stripChunks_(nullptr),
		numChunks_(0),
		leftNeighbour_(neighbour)
	{}
	~Strip(void){
		if (stripChunks_){
			for (uint32_t i = 0; i < numChunks_; ++i)
				RefReaper::unref(stripChunks_[i]);
			delete[] stripChunks_;
		}
	}
	void generateChunks(ChunkInfo chunkInfo, IBufferPool *pool){
		chunkInfo_ = chunkInfo;
		numChunks_ = chunkInfo_.numChunks();
		assert(numChunks_);
		stripChunks_ = new StripChunk*[numChunks_];
		uint64_t writeableTotal = 0;
		if (numChunks_ == 1){
			bool lastChunkOfAll  = chunkInfo.isFinalStrip_;
			bool firstSeam       = chunkInfo.hasFirstSeam();
			bool lastSeam        = !lastChunkOfAll && chunkInfo.hasLastSeam();
			IOChunk* ioChunk;
			if (firstSeam){
				ioChunk = leftNeighbour_->finalChunk()->ioChunk_;
				if (lastSeam)
					ioChunk->share();
			}
			else {
				ioChunk = 	new IOChunk(0,chunkInfo.first_.x1_,
											chunkInfo.writeSize_,
											(lastSeam ? pool : nullptr));
			}
			uint64_t writeOffset,writeLen;
			if (chunkInfo.isFirstStrip_){
				writeOffset = chunkInfo_.headerSize_;
				writeLen    = chunkInfo.first_.x1_ - chunkInfo_.headerSize_ ;
			} else {
				assert(chunkInfo.last_.x0_ <= chunkInfo.first_.x0_);
				writeOffset = 	chunkInfo.first_.x0_ - chunkInfo.last_.x0_;
				writeLen    =	chunkInfo.first_.len();
			}
			stripChunks_[0] = new StripChunk(ioChunk,writeOffset,writeLen);
			if (chunkInfo.isFinalStrip_)
				ioChunk->updateLen(chunkInfo.last_.len());
			writeableTotal = stripChunks_[0]->writeableLen_;
		}
		for (uint32_t i = 0; i < numChunks_ && numChunks_>1; ++i ){
			uint64_t off = (chunkInfo.first_.x1_ - chunkInfo_.writeSize_) +
								i * chunkInfo_.writeSize_;
			bool lastChunkOfAll  = chunkInfo.isFinalStrip_ && (i == numChunks_-1);
			uint64_t len =
					lastChunkOfAll ? (chunkInfo_.last_.len()) : chunkInfo_.writeSize_;
			uint64_t writeableOffset = 0;
			uint64_t writeableLen = len;
			bool sharedLastChunk = false;
			bool firstSeam       = (i == 0) && chunkInfo.hasFirstSeam();
			bool lastSeam        =
					(i == numChunks_-1) && !lastChunkOfAll && chunkInfo.hasLastSeam();
			if (firstSeam){
				assert(leftNeighbour_);
				off = leftNeighbour_->finalChunk()->offset();
				assert(chunkInfo_.first_.x0_  > off);

				writeableOffset = chunkInfo_.first_.x0_ - off;
				writeableLen    = chunkInfo_.first_.len();

				assert(writeableLen && writeableLen < chunkInfo_.writeSize_);
				assert(writeableOffset && writeableOffset < chunkInfo_.writeSize_);
			} else if (lastSeam){
				off   = chunkInfo_.last_.x0_;
				writeableLen = chunkInfo_.last_.len();
				assert(writeableLen && writeableLen < chunkInfo_.writeSize_);
				if (lastChunkOfAll)
					len = writeableLen;
				else
					sharedLastChunk = true;
			} else if (chunkInfo_.isFirstStrip_ && i == 0){
				writeableOffset += chunkInfo_.headerSize_;
				writeableLen -= chunkInfo_.headerSize_;
			}
			writeableTotal += writeableLen;

			// first seam can never be last seam
			assert(!lastSeam || !firstSeam);
			assert(IOBuf::isAlignedToWriteSize(off));
			assert(lastChunkOfAll || IOBuf::isAlignedToWriteSize(len));

			IOChunk* ioChunk = nullptr;
			if (firstSeam)
				ioChunk = leftNeighbour_->finalChunk()->ioChunk_;
			else
				ioChunk = new IOChunk(off,len,chunkInfo_.writeSize_,
										(sharedLastChunk ? pool : nullptr));
			assert(!firstSeam || ioChunk->isShared());

			stripChunks_[i] = new StripChunk(ioChunk,
												writeableOffset,
												writeableLen);
		}

		// validation
		assert(!chunkInfo.isFirstStrip_ || stripChunks_[0]->offset() == 0);
		assert(logicalLen_ == writeableTotal);

		uint64_t writeableEnd = 0;
		(void)writeableEnd;
		if (numChunks_ > 1) {
			writeableEnd =	stripChunks_[numChunks_-1]->offset() +
									stripChunks_[numChunks_-1]->writeableLen_;
		}
		else {
			writeableEnd = stripChunks_[0]->offset() +
							stripChunks_[0]->writeableOffset_ +
								stripChunks_[0]->writeableLen_;
		}
		assert(writeableEnd == chunkInfo_.last_.x1_);

		uint64_t writeableBegin =
				stripChunks_[0]->offset() + stripChunks_[0]->writeableOffset_;
		(void)writeableBegin;
		assert(writeableBegin ==
				chunkInfo_.first_.x0_ +
					(chunkInfo_.isFirstStrip_ ? chunkInfo_.headerSize_ : 0));
		assert(writeableEnd - writeableBegin == logicalLen_);
	}
	StripChunkArray* getStripChunkArray(IBufferPool *pool, uint8_t *header, uint64_t headerLen){
		auto buffers = new IOBuf*[numChunks_];
		auto chunks  = new StripChunk*[numChunks_];
		for (uint32_t i = 0; i < numChunks_; ++i){
			auto stripChunk = stripChunks_[i];
			auto ioChunk 	= stripChunk->ioChunk_;
			stripChunk->alloc(pool);
			// set header on first chunk of first strip
			if (header && i == 0)
				stripChunk->setHeader(header, headerLen);
			chunks[i] 	= stripChunk;
			buffers[i] 	= ioChunk->buf();
		}
		for (uint32_t i = 0; i < numChunks_; ++i){
			assert(buffers[i]->data_);
			assert(buffers[i]->len_);
		}

		return new StripChunkArray(chunks,buffers,numChunks_,pool);
	}
	StripChunk* finalChunk(void){
		return stripChunks_[numChunks_-1];
	}
	StripChunk* firstChunk(void){
		return stripChunks_[0];
	}
	uint64_t logicalOffset_;
	uint64_t logicalLen_;
	StripChunk** stripChunks_;
	uint64_t numChunks_;
	Strip *leftNeighbour_;
	ChunkInfo chunkInfo_;
};

/*
 * Divide an image into strips
 *
 */
struct ImageStripper{
	ImageStripper(uint32_t width,
				uint32_t height,
				uint16_t numcomps,
				uint64_t packedRowBytes,
				uint32_t nominalStripHeight,
				uint64_t headerSize,
				uint64_t writeSize,
				IBufferPool *pool) :
		width_(width),
		height_(height),
		numcomps_(numcomps),
		nominalStripHeight_(nominalStripHeight),
		numStrips_(nominalStripHeight ?
				(height  + nominalStripHeight - 1)/ nominalStripHeight : 0),
		packedRowBytes_(packedRowBytes),
		finalStripHeight_((nominalStripHeight && (height % nominalStripHeight != 0)) ?
							height - ((height / nominalStripHeight) * nominalStripHeight) :
								nominalStripHeight),
		headerSize_(headerSize),
		writeSize_(writeSize),
		finalStrip_(numStrips_-1),
		strips_(new Strip*[numStrips_])
	{
		for (uint32_t i = 0; i < numStrips_; ++i){
			auto neighbour = (i > 0) ? strips_[i-1] : nullptr;
			strips_[i] =
					new Strip(i * nominalStripHeight_ * packedRowBytes_,
								stripHeight(i) * packedRowBytes_,
								neighbour);
			if (pool)
				strips_[i]->generateChunks(getChunkInfo(i), pool);
		}
	}
	~ImageStripper(void){
		if (strips_){
			for (uint32_t i = 0; i < numStrips_; ++i)
				delete strips_[i];
			delete[] strips_;
		}
	}
	Strip* getStrip(uint32_t strip) const{
		return strips_[strip];
	}
	uint32_t numStrips(void) const{
		return numStrips_;
	}
	uint64_t numUniqueChunks(void) const{
		return (packedRowBytes_ * height_ + writeSize_ - 1)/writeSize_;
	}
	ChunkInfo getChunkInfo(uint32_t strip){
		return ChunkInfo(strip == 0,
						strip == finalStrip_,
						strips_[strip]->logicalOffset_,
						strips_[strip]->logicalLen_,
						strip == 0 ? 0 : strips_[strip-1]->logicalOffset_,
						strip == 0 ? 0 : strips_[strip-1]->logicalLen_,
						headerSize_,
						writeSize_);
	}
	uint32_t width_;
	uint32_t height_;
	uint16_t numcomps_;
	uint32_t nominalStripHeight_;
private:
	uint32_t stripHeight(uint32_t strip) const{
		return (strip < numStrips_-1) ? nominalStripHeight_ : finalStripHeight_;
	}
	uint32_t numStrips_;
	uint64_t packedRowBytes_;
	uint32_t finalStripHeight_;
	uint64_t headerSize_;
	uint64_t writeSize_;
	uint32_t finalStrip_;
	Strip **strips_;
};

}
