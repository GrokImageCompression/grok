#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include "grok.h"
#include "MinHeap.h"

namespace grk {


struct GrkSerializeBuf : public grk_serialize_buf {
public:
	GrkSerializeBuf() : GrkSerializeBuf(nullptr,0,0,0,false,0)
	{
	}
	GrkSerializeBuf(uint8_t *data,
					uint64_t offset,
					uint64_t dataLen,
					uint64_t allocLen,
					bool pooled,
					uint32_t index)
	{
		this->data = data;
		this->offset = offset;
		this->dataLen = dataLen;
		this->allocLen = allocLen;
		this->pooled = pooled;
		this->index = index;
	}
	explicit GrkSerializeBuf(const grk_serialize_buf rhs){
		data = rhs.data;
		offset = rhs.offset;
		dataLen = rhs.dataLen;
		allocLen = rhs.allocLen;
		pooled = rhs.pooled;
		index = rhs.index;
	}
	uint32_t getIndex(void) const{
		return index;
	}
	bool alloc(uint64_t len){
		dealloc();
		data = (uint8_t*)grkAlignedMalloc(len);
		if (data) {
			dataLen = len;
			allocLen = len;
		}

		return (data != nullptr);
	}
	void dealloc(){
		grkAlignedFree(data);
		data = nullptr;
	}
};


struct Strip {
	Strip(GrkImage *outputImage, uint16_t index, uint32_t tileHeight);
	~Strip(void);
	uint32_t getIndex(void);
	GrkImage* stripImg;
	std::atomic<uint32_t> tileCounter;
	uint32_t m_index;
};

class StripPool {
public:
	StripPool(void);
	virtual ~StripPool();

	void init( uint16_t tgrid_w,
				uint32_t th,
			  uint16_t tgrid_h,
			  GrkImage *outputImg,
			  void* serialize_d,
			  grk_serialize_pixels serializeBufferCb);
	bool composite(GrkImage *tileImage);
private:
	GrkSerializeBuf getBuffer(uint64_t len);
	void putBuffer(GrkSerializeBuf b);
	std::map<uint8_t*, GrkSerializeBuf> pool;

	Strip **strips;
	uint16_t m_tgrid_w;
	uint32_t m_y0;
	uint32_t m_th;
	uint16_t m_tgrid_h;
	uint64_t m_packedRowBytes;

	mutable std::mutex poolMutex;

	void* serialize_data;
	grk_serialize_pixels serializeBufferCallback;
	MinHeap<GrkSerializeBuf, uint32_t, MinHeapFakeLocker> serializeHeap;
};

}
