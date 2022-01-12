#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include "grok.h"

namespace grk {


struct GrkSerializeBuf : public grk_serialize_buf {
public:
	GrkSerializeBuf() : GrkSerializeBuf(nullptr,0,0,0,false)
	{
	}
	GrkSerializeBuf(uint8_t *data,
					uint64_t offset,
					uint64_t dataLength,
					uint64_t maxDataLength,
					bool pooled)
	{
		this->data = data;
		this->offset = offset;
		this->dataLength = dataLength;
		this->maxDataLength = maxDataLength;
		this->pooled = pooled;
	}
	explicit GrkSerializeBuf(const grk_serialize_buf rhs){
		data = rhs.data;
		offset = rhs.offset;
		dataLength = rhs.dataLength;
		maxDataLength = rhs.maxDataLength;
		pooled = rhs.pooled;
	}
	bool alloc(uint64_t len){
		dealloc();
		data = (uint8_t*)grkAlignedMalloc(len);
		if (data) {
			dataLength = len;
			maxDataLength = len;
		}

		return (data != nullptr);
	}
	void dealloc(){
		grkAlignedFree(data);
		data = nullptr;
	}
};


struct Strip {
	Strip(GrkImage *outputImage, uint16_t id, uint32_t tileHeight);
	~Strip(void);
	GrkImage* stripImg;
	std::atomic<uint32_t> tileCounter;
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
	std::vector<GrkSerializeBuf> pool;

	Strip **strips;
	uint16_t m_tgrid_w;
	uint32_t m_y0;
	uint32_t m_th;
	uint16_t m_tgrid_h;
	uint64_t m_packedRowBytes;

	mutable std::mutex poolMutex;

	void* serialize_data;
	grk_serialize_pixels serializeBufferCallback;
};

}
