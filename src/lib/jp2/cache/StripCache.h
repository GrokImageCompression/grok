#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include "grok.h"
#include "MinHeap.h"

namespace grk
{
struct GrkSerializeBuf : public grk_serialize_buf
{
  public:
	GrkSerializeBuf() : GrkSerializeBuf(nullptr, 0, 0, 0, false, 0) {}
	GrkSerializeBuf(uint8_t* data, uint64_t offset, uint64_t dataLen, uint64_t allocLen,
					bool pooled, uint32_t index)
	{
		this->data = data;
		this->offset = offset;
		this->dataLen = dataLen;
		this->allocLen = allocLen;
		this->pooled = pooled;
		this->index = index;
	}
	explicit GrkSerializeBuf(const grk_serialize_buf rhs)
	{
		data = rhs.data;
		offset = rhs.offset;
		dataLen = rhs.dataLen;
		allocLen = rhs.allocLen;
		pooled = rhs.pooled;
		index = rhs.index;
	}
	uint32_t getIndex(void) const
	{
		return index;
	}
	bool alloc(uint64_t len)
	{
		dealloc();
		data = (uint8_t*)grkAlignedMalloc(len);
		if(data)
		{
			dataLen = len;
			allocLen = len;
		}

		return (data != nullptr);
	}
	void dealloc()
	{
		grkAlignedFree(data);
		data = nullptr;
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
			  GrkImage* outputImg, grk_serialize_pixels_callback serializeBufferCallback,
			  void* serializeUserData,
			  grk_serialize_register_client_callback serializeRegisterClientCallback);
	bool ingestTile(GrkImage* src);
	bool ingestStrip(Tile* src, uint32_t yBegin, uint32_t yEnd);
	void returnBufferToPool(GrkSerializeBuf b);
	bool isInitialized(void);

  private:
	GrkSerializeBuf getBufferFromPool(uint64_t len);
	std::map<uint8_t*, GrkSerializeBuf> pool;
	mutable std::mutex poolMutex_;
	Strip** strips;
	uint16_t numTilesX_;
	uint32_t numStrips_;
	uint32_t stripHeight_;
	uint32_t imageY0_;
	uint64_t packedRowBytes_;
	void* serializeUserData_;
	grk_serialize_pixels_callback serializeBufferCallback_;
	mutable std::mutex serializeMutex_;
	MinHeap<GrkSerializeBuf, uint32_t, MinHeapFakeLocker> serializeHeap;
	mutable std::mutex heapMutex_;
	mutable std::mutex interleaveMutex_;
	bool initialized_;
};

} // namespace grk
