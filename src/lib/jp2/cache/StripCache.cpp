#include "grk_includes.h"

namespace grk
{
static bool reclaimCallback(grk_io_buf buffer, void* io_user_data)
{
	auto pool = (StripCache*)io_user_data;
	if(pool)
		pool->returnBufferToPool(GrkIOBuf(buffer));

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
	for(auto& b : pool)
		b.second.dealloc();
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
void StripCache::init(uint16_t numTilesX, uint32_t numStrips, uint32_t stripHeight, uint8_t reduce,
					  GrkImage* outputImage, grk_io_pixels_callback ioBufferCallback,
					  void* ioUserData,
					  grk_io_register_client_callback ioRegisterClientCallback)
{
	assert(outputImage);
	if(!numStrips || !outputImage)
		return;
	multiTile_ = outputImage->hasMultipleTiles;
	ioBufferCallback_ = ioBufferCallback;
	ioUserData_ = ioUserData;
	if(ioRegisterClientCallback)
		ioRegisterClientCallback(reclaimCallback, ioUserData, this);
	numTilesX_ = numTilesX;
	numStrips_ = numStrips;
	imageY0_ = outputImage->y0;
	stripHeight_ = stripHeight;
	packedRowBytes_ = outputImage->packedRowBytes;
	strips = new Strip*[numStrips];
	for(uint16_t i = 0; i < numStrips_; ++i)
		strips[i] = new Strip(outputImage, i, stripHeight_, reduce);
	initialized_ = true;
}
bool StripCache::ingestStrip(Tile* src, uint32_t yBegin, uint32_t yEnd)
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
		std::unique_lock<std::mutex> lk(poolMutex_);
		if(!dest->interleavedData.data_)
		{
			dest->interleavedData = getBufferFromPool(dataLen);
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
				if(!ioBufferCallback_(0,b, ioUserData_))
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
		std::unique_lock<std::mutex> lk(poolMutex_);
		if(!dest->interleavedData.data_)
		{
			dest->interleavedData = getBufferFromPool(dataLen);
			if(!dest->interleavedData.data_)
				return false;
		}
	}
	uint32_t tileCount = 0;
	{
		std::unique_lock<std::mutex> lk(interleaveMutex_);
		tileCount = ++strip->tileCounter;
		if(!dest->compositeInterleaved(src))
			return false;
	}

	if(tileCount == numTilesX_)
	{
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
/***
 * Get buffer from pool
 *
 * not thread safe, but it is always called from within a lock
 */
GrkIOBuf StripCache::getBufferFromPool(uint64_t len)
{
	for(auto iter = pool.begin(); iter != pool.end(); ++iter)
	{
		if(iter->second.allocLen_ >= len)
		{
			auto b = iter->second;
			b.dataLen_ = len;
			pool.erase(iter);
			return b;
		}
	}
	GrkIOBuf rc;
	rc.alloc(len);

	return rc;
}
/***
 * return buffer to pool
 *
 * thread-safe
 */
void StripCache::returnBufferToPool(GrkIOBuf b)
{
	std::unique_lock<std::mutex> lk(poolMutex_);
	assert(b.data_);
	assert(pool.find(b.data_) == pool.end());
	pool[b.data_] = b;
}

} // namespace grk
