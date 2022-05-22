#include "grk_includes.h"

namespace grk
{
static bool grkReclaimCallback(uint32_t threadId, grk_io_buf buffer, void* io_user_data)
{
	auto stripCache = (StripCache*)io_user_data;
	if(stripCache)
		stripCache->returnBufferToPool(threadId, GrkIOBuf(buffer));

	return true;
}

Strip::Strip(GrkImage* outputImage, uint16_t index, uint32_t height, uint8_t reduce)
	: stripImg(nullptr), tileCounter(0), index_(index), reduce_(reduce)
{
	stripImg = new GrkImage();
	outputImage->copyHeader(stripImg);

	stripImg->y0 = outputImage->y0 + index * height;
	stripImg->y1 = std::min<uint32_t>(outputImage->y1, stripImg->y0 + height);
	stripImg->comps->y0 = reduceDim(stripImg->y0);
	stripImg->comps->h = reduceDim(stripImg->y1 - stripImg->y0);
}
Strip::~Strip(void)
{
	grk_object_unref(&stripImg->obj);
}
uint32_t Strip::getIndex(void)
{
	return index_;
}
uint32_t Strip::reduceDim(uint32_t dim)
{
	return reduce_ ? ceildivpow2<uint32_t>(dim, reduce_) : dim;
}
StripCache::StripCache()
	: strips(nullptr), numTilesX_(0), numStrips_(0), stripHeight_(0), imageY0_(0),
	  packedRowBytes_(0), ioUserData_(nullptr), ioBufferCallback_(nullptr),
	  initialized_(false), multiTile_(true)
{}
StripCache::~StripCache()
{
	for (auto& p : pools_)
		delete p;
	for(uint16_t i = 0; i < numStrips_; ++i)
		delete strips[i];
	delete[] strips;
}
bool StripCache::isInitialized(void)
{
	return initialized_;
}
bool StripCache::isMultiTile(void)
{
	return multiTile_;
}
void StripCache::init(uint32_t concurrency,
					uint16_t numTilesX, uint32_t numStrips, uint32_t stripHeight, uint8_t reduce,
					  GrkImage* outputImage, grk_io_pixels_callback ioBufferCallback,
					  void* ioUserData,
					  grk_io_register_reclaim_callback registerGrkReclaimCallback)
{
	assert(outputImage);
	if(!numStrips || !outputImage)
		return;
	multiTile_ = outputImage->hasMultipleTiles;
	ioBufferCallback_ = ioBufferCallback;
	ioUserData_ = ioUserData;
	if(registerGrkReclaimCallback)
		registerGrkReclaimCallback(grkReclaimCallback, ioUserData, this);
	numTilesX_ = numTilesX;
	numStrips_ = numStrips;
	imageY0_ = outputImage->y0;
	stripHeight_ = stripHeight;
	packedRowBytes_ = outputImage->packedRowBytes;
	strips = new Strip*[numStrips];
	for(uint16_t i = 0; i < numStrips_; ++i)
		strips[i] = new Strip(outputImage, i, stripHeight_, reduce);
	initialized_ = true;
	for (uint32_t i = 0; i < concurrency; ++i)
		pools_.push_back(new BufPool());
}
bool StripCache::ingestStrip(uint32_t threadId, Tile* src, uint32_t yBegin, uint32_t yEnd)
{
	if(!initialized_)
		return false;

	uint16_t stripId = (uint16_t)((yBegin + stripHeight_ - 1) / stripHeight_);
	assert(stripId < numStrips_);
	auto strip = strips[stripId];
	auto dest = strip->stripImg;
	// use height of first component, because no subsampling
	uint64_t dataLen = packedRowBytes_ * (yEnd - yBegin);
	{
		if(!dest->interleavedData.data_)
		{
			dest->interleavedData = pools_[threadId]->get(dataLen);
			if(!dest->interleavedData.data_)
				return false;
		}
	}

	if(!dest->compositeInterleaved(src, yBegin, yEnd))
		return false;

	auto buf = GrkIOBuf(dest->interleavedData);
	buf.index_ = stripId;
	buf.dataLen_ = dataLen;
	dest->interleavedData.data_ = nullptr;
	std::queue<GrkIOBuf> buffersToSerialize;
	{
		std::unique_lock<std::mutex> lk(heapMutex_);
		// 1. push to heap
		serializeHeap.push(buf);
		// 2. get all sequential buffers in heap
		buf = serializeHeap.pop();
		while(buf.data_)
		{
			buffersToSerialize.push(buf);
			buf = serializeHeap.pop();
		}
	}
	// 3. serialize buffers
	if(!buffersToSerialize.empty())
	{
		{
			std::unique_lock<std::mutex> lk(serializeMutex_);
			while(!buffersToSerialize.empty())
			{
				auto b = buffersToSerialize.front();
				if(!ioBufferCallback_(threadId,b, ioUserData_))
					break;
				buffersToSerialize.pop();
			}
		}
		// if non empty, then there has been a serialize failure
		if(!buffersToSerialize.empty())
		{
			// cleanup
			while(!buffersToSerialize.empty())
			{
				auto b = buffersToSerialize.front();
				b.dealloc();
				buffersToSerialize.pop();
			}
			return false;
		}
	}

	return true;
}
bool StripCache::ingestTile(uint32_t threadId,GrkImage* src)
{
	if(!initialized_)
		return false;

	uint16_t stripId = (uint16_t)((src->y0 - imageY0_ + stripHeight_ - 1) / stripHeight_);
	assert(stripId < numStrips_);
	auto strip = strips[stripId];
	auto dest = strip->stripImg;
	// use height of first component, because no subsampling
	uint64_t dataLen = packedRowBytes_ * src->comps->h;
	{
		if(!dest->interleavedData.data_)
		{
			dest->interleavedData = pools_[threadId]->get(dataLen);
			if(!dest->interleavedData.data_)
				return false;
		}
	}
	if(!dest->compositeInterleaved(src))
		return false;

	if(++strip->tileCounter == numTilesX_)
	{
		auto buf = GrkIOBuf(dest->interleavedData);
		buf.index_ = stripId;
		buf.dataLen_ = dataLen;
		dest->interleavedData.data_ = nullptr;
		//return ioBufferCallback_(threadId, buf, ioUserData_);

		std::queue<GrkIOBuf> buffersToSerialize;
		{
			std::unique_lock<std::mutex> lk(heapMutex_);
			// 1. push to heap
			serializeHeap.push(buf);
			// 2. get all sequential buffers in heap
			buf = serializeHeap.pop();
			while(buf.data_)
			{
				buffersToSerialize.push(buf);
				buf = serializeHeap.pop();
			}
		}
		// 3. serialize buffers
		if(!buffersToSerialize.empty())
		{
			{
				std::unique_lock<std::mutex> lk(serializeMutex_);
				while(!buffersToSerialize.empty())
				{
					auto b = buffersToSerialize.front();
					if(!ioBufferCallback_(threadId, b, ioUserData_))
						break;
					buffersToSerialize.pop();
				}
			}
			// if non empty, then there has been a serialize failure
			if(!buffersToSerialize.empty())
			{
				// cleanup
				while(!buffersToSerialize.empty())
				{
					auto b = buffersToSerialize.front();
					b.dealloc();
					buffersToSerialize.pop();
				}
				return false;
			}
		}
	}

	return true;
}

void StripCache::returnBufferToPool(uint32_t threadId, GrkIOBuf b){
	pools_[threadId]->put(b);
}

} // namespace grk
