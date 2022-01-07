#include "grk_includes.h"

namespace grk {

Strip::Strip(GrkImage* outputImage, uint16_t id, uint32_t tileHeight) : stripImg(nullptr),
												   	   	   	   	   	   tileCounter(0){
	stripImg = new GrkImage();
	outputImage->copyHeader(stripImg);
	stripImg->y0 = outputImage->y0 + id * tileHeight;
	stripImg->y1 = std::min<uint32_t>(outputImage->y1, stripImg->y0 + tileHeight);
	stripImg->comps->y0 = stripImg->y0;
	stripImg->comps->h  = stripImg->y1 - stripImg->y0;
}
Strip::~Strip(void){
	grk_object_unref(&stripImg->obj);
}

StripCache::StripCache() :  strips(nullptr),
							m_tgrid_w(0),
							m_y0(0),
							m_th(0),
							m_tgrid_h(0),
							m_packedRowBytes(0),
							serialize_data(nullptr),
							serializeBufferCallback(nullptr)
{
}
StripCache::~StripCache()
{
	for (auto iter = bufCache.begin(); iter != bufCache.end(); ++iter)
		grkAlignedFree(iter->data);
	for (uint16_t i = 0; i < m_tgrid_h; ++i)
		delete strips[i];
	delete[] strips;
}
void StripCache::init(uint16_t tgrid_w,
						uint32_t th,
					  uint16_t tgrid_h,
					  GrkImage *outputImage,
					  void* serialize_d,
					  grk_serialize_pixels serializeBufferCb){
	assert(outputImage);
	if (!tgrid_h || !outputImage)
		return;
	serialize_data = serialize_d;
	serializeBufferCallback = serializeBufferCb;
	m_tgrid_w = tgrid_w;
	m_y0 = outputImage->y0;
	m_th = th;
	m_tgrid_h = tgrid_h;
	m_packedRowBytes = outputImage->packedRowBytes;
	strips = new Strip*[tgrid_h];
	for (uint16_t i = 0; i < m_tgrid_h; ++i)
		strips[i] = new Strip(outputImage, i, m_th);
}
bool StripCache::composite(GrkImage *tileImage){
	uint16_t stripId = (uint16_t)(((tileImage->y0 - m_y0) + m_th - 1) / m_th);
	assert(stripId < m_tgrid_h);
	auto strip = strips[stripId];
	auto img = strip->stripImg;
	uint64_t dataLength = m_packedRowBytes * (tileImage->y1 - tileImage->y0);
	if (strip->tileCounter == 0)
	{
		std::unique_lock<std::mutex> lk(bufCacheMutex);
		if (!img->interleavedData)
			img->interleavedData = getBuffer(dataLength).data;
	}
	bool rc =  img->compositeInterleaved(tileImage);
	if (!rc)
		return false;
	if (++(strip->tileCounter) == m_tgrid_w){
		grk_serialize_buf buf;
		buf.data = img->interleavedData;
		buf.dataLength = dataLength;
		grk_serialize_buf* reclaimed[5];
		uint32_t num_reclaimed;
		serializeBufferCallback(&buf,stripId, reclaimed, 5, &num_reclaimed,serialize_data);
		{
			std::unique_lock<std::mutex> lk(bufCacheMutex);
			putBuffer(buf);
			img->interleavedData = nullptr;
		}
	}

	return rc;
}
grk_serialize_buf StripCache::getBuffer(uint64_t len){
	for (auto iter = bufCache.begin(); iter != bufCache.end(); ++iter){
		if (iter->maxDataLength >= len){
			auto b = *iter;
			b.dataLength = len;
			bufCache.erase(iter);
			return b;
		}
	}
	grk_serialize_buf rc;
	rc.data = (uint8_t*)grkAlignedMalloc(len);
	rc.maxDataLength = len;

	return rc;
}
void StripCache::putBuffer(grk_serialize_buf b){
	bufCache.push_back(b);
}


}
