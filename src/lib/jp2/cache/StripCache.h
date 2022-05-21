#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include "grok.h"
#include "MinHeap.h"

namespace grk
{
struct GrkIOBuf : public grk_io_buf
{
  public:
	GrkIOBuf() : GrkIOBuf(nullptr, 0, 0, 0, false, 0) {}
	GrkIOBuf(uint8_t* data, uint64_t offset, uint64_t dataLen, uint64_t allocLen,
					bool pooled, uint32_t index)
	{
		this->data_ = data;
		this->offset_ = offset;
		this->dataLen_ = dataLen;
		this->allocLen_ = allocLen;
		this->pooled_ = pooled;
		this->index_ = index;
	}
	explicit GrkIOBuf(const grk_io_buf rhs)
	{
		data_ = rhs.data_;
		offset_ = rhs.offset_;
		dataLen_ = rhs.dataLen_;
		allocLen_ = rhs.allocLen_;
		pooled_ = rhs.pooled_;
		index_ = rhs.index_;
	}
	uint32_t getIndex(void) const
	{
		return index_;
	}
	bool alloc(uint64_t len)
	{
		dealloc();
		data_ = (uint8_t*)grkAlignedMalloc(len);
		if(data_)
		{
			dataLen_ = len;
			allocLen_ = len;
		}

		return (data_ != nullptr);
	}
	void dealloc()
	{
		grkAlignedFree(data_);
		data_ = nullptr;
	}
};

struct Strip
{
	Strip(GrkImage* outputImage, uint16_t index, uint32_t tileHeight, uint8_t reduce);
	~Strip(void);
	uint32_t getIndex(void);
	uint32_t reduceDim(uint32_t dim);
	GrkImage* stripImg;
	uint32_t tileCounter; // count number of tiles added to strip
	uint32_t index_; // index of strip
	uint8_t reduce_; // resolution reduction
};

class StripCache
{
  public:
	StripCache(void);
	virtual ~StripCache();

	void init(uint16_t numTilesX_, uint32_t numStrips, uint32_t stripHeight, uint8_t reduce,
			  GrkImage* outputImg, grk_io_pixels_callback ioBufferCallback,
			  void* ioUserData,
			  grk_io_register_client_callback ioRegisterClientCallback);
	bool ingestTile(uint32_t threadId, GrkImage* src);
	bool ingestStrip(Tile* src, uint32_t yBegin, uint32_t yEnd);
	void returnBufferToPool(GrkIOBuf b);
	bool isInitialized(void);
	bool isMultiTile(void);

  private:
	GrkIOBuf getBufferFromPool(uint64_t len);
	std::map<uint8_t*, GrkIOBuf> pool;
	mutable std::mutex poolMutex_;
	Strip** strips;
	uint16_t numTilesX_;
	uint32_t numStrips_;
	uint32_t stripHeight_;
	uint32_t imageY0_;
	uint64_t packedRowBytes_;
	void* ioUserData_;
	grk_io_pixels_callback ioBufferCallback_;
	mutable std::mutex serializeMutex_;
	MinHeap<GrkIOBuf, uint32_t, MinHeapFakeLocker> serializeHeap;
	mutable std::mutex heapMutex_;
	mutable std::mutex interleaveMutex_;
	bool initialized_;
	bool multiTile_;
};

} // namespace grk
