#pragma once

#include <vector>
#include <mutex>

namespace grk {

struct Strip {
	Strip(GrkImage *outputImage, uint16_t id, uint32_t tileHeight);
	~Strip(void);
	GrkImage* stripImg;
	uint16_t tileCounter;
};

class StripCache {
public:
	StripCache(void);
	virtual ~StripCache();

	void init(uint32_t th,
			  uint16_t tgrid_h,
			  GrkImage *outputImg,
			  void* serialize_d,
			  grk_serialize_pixels serializeBufferCb,
			  grk_reclaim_buffers reclaimCb);
	bool composite(GrkImage *tileImage);
private:
	grk_simple_buf getBuffer(uint64_t len);
	void putBuffer(grk_simple_buf b);
	std::vector<grk_simple_buf> bufCache;

	Strip **strips;
	uint32_t m_y0;
	uint32_t m_th;
	uint16_t m_tgrid_h;
	uint64_t m_packedRowBytes;

	mutable std::mutex bufCacheMutex;

	void* serialize_data;
	grk_serialize_pixels serializeBufferCallback;
	grk_reclaim_buffers reclaimCallback;
};

}
