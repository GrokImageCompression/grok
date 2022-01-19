#include "grk_includes.h"

namespace grk {

const uint32_t reclaimSize = 5;

Strip::Strip(GrkImage* outputImage, uint16_t index, uint32_t tileHeight) : stripImg(nullptr),
												   	   	   	   	   	   tileCounter(0),
																	   index_(index){
	stripImg = new GrkImage();
	outputImage->copyHeader(stripImg);
	stripImg->y0 = outputImage->y0 + index * tileHeight;
	stripImg->y1 = std::min<uint32_t>(outputImage->y1, stripImg->y0 + tileHeight);
	stripImg->comps->y0 = stripImg->y0;
	stripImg->comps->h  = stripImg->y1 - stripImg->y0;
}
Strip::~Strip(void){
	grk_object_unref(&stripImg->obj);
}
uint32_t Strip::getIndex(void){
	return index_;
}


StripPool::StripPool() :  strips(nullptr),
							tgrid_w_(0),
							y0_(0),
							th_(0),
							tgrid_h_(0),
							packedRowBytes_(0),
							serialize_data(nullptr),
							serializeBufferCallback(nullptr)
{
}
StripPool::~StripPool()
{
	for (auto &b : pool)
		b.second.dealloc();
	for (uint16_t i = 0; i < tgrid_h_; ++i)
		delete strips[i];
	delete[] strips;
}
void StripPool::init(uint16_t tgrid_w,
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
	tgrid_w_ = tgrid_w;
	y0_ = outputImage->y0;
	th_ = th;
	tgrid_h_ = tgrid_h;
	packedRowBytes_ = outputImage->packedRowBytes;
	strips = new Strip*[tgrid_h];
	for (uint16_t i = 0; i < tgrid_h_; ++i)
		strips[i] = new Strip(outputImage, i, th_);
}
bool StripPool::composite(GrkImage *tileImage){
	uint16_t stripId = (uint16_t)(((tileImage->y0 - y0_) + th_ - 1) / th_);
	assert(stripId < tgrid_h_);
	auto strip = strips[stripId];
	auto img = strip->stripImg;
	uint64_t dataLen = packedRowBytes_ * (tileImage->y1 - tileImage->y0);
	if (strip->tileCounter == 0)
	{
		std::unique_lock<std::mutex> lk(poolMutex);
		if (!img->interleavedData.data) {
			img->interleavedData = getBuffer(dataLen);
			if (!img->interleavedData.data)
				return false;
		}
	}
	bool rc =  img->compositeInterleaved(tileImage);
	if (!rc)
		return false;
	if (++(strip->tileCounter) == tgrid_w_){
		GrkSerializeBuf buf = GrkSerializeBuf(img->interleavedData);
		buf.index = stripId;
		buf.dataLen = dataLen;
		img->interleavedData.data = nullptr;
		GrkSerializeBuf reclaimed[reclaimSize];
		uint32_t num_reclaimed = 0;
		{
			std::unique_lock<std::mutex> lk(poolMutex);
			serializeHeap.push(buf);
			buf = serializeHeap.pop();
			while(buf.data) {
				if (!serializeBufferCallback(buf,reclaimed, reclaimSize, &num_reclaimed,serialize_data))
					return false;
				for(uint32_t i = 0; i < num_reclaimed; ++i)
					putBuffer(reclaimed[i]);
				buf = serializeHeap.pop();
			}
		}
	}

	return rc;
}
GrkSerializeBuf StripPool::getBuffer(uint64_t len){
	for (auto iter = pool.begin(); iter != pool.end(); ++iter){
		if (iter->second.allocLen >= len){
			auto b = iter->second;
			b.dataLen = len;
			pool.erase(iter);
			return b;
		}
	}
	GrkSerializeBuf rc;
	rc.alloc(len);

	return rc;
}
void StripPool::putBuffer(GrkSerializeBuf b){
	assert(b.data);
	assert(pool.find(b.data) == pool.end());
	pool[b.data] = b;
}


}
