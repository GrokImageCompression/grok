#include "grk_includes.h"

const bool grokNewIO = false;

namespace grk
{
static bool grkReclaimCallback(uint32_t threadId, grk_io_buf buffer, void* io_user_data)
{
   auto stripCache = (StripCache*)io_user_data;
   if(stripCache)
	  stripCache->returnBufferToPool(threadId, GrkIOBuf(buffer));

   return true;
}

Strip::Strip(GrkImage* outputImage, uint16_t index, uint32_t nominalHeight, uint8_t reduce)
	: stripImg(new GrkImage()), tileCounter(0), reduce_(reduce), allocatedInterleaved_(false)
{
   outputImage->copyHeader(stripImg);

   stripImg->y0 = outputImage->y0 + index * nominalHeight;
   stripImg->y1 = std::min<uint32_t>(outputImage->y1, stripImg->y0 + nominalHeight);
   stripImg->comps->y0 = stripImg->y0;
   stripImg->comps->h = stripImg->y1 - stripImg->y0;
   if(outputImage->hasMultipleTiles)
   {
	  stripImg->comps->y0 = reduceDim(stripImg->comps->y0);
	  stripImg->comps->h = reduceDim(stripImg->comps->h);
   }
}
Strip::~Strip(void)
{
   grk_object_unref(&stripImg->obj);
}
uint32_t Strip::reduceDim(uint32_t dim)
{
   return reduce_ ? ceildivpow2<uint32_t>(dim, reduce_) : dim;
}
bool Strip::allocInterleavedLocked(uint64_t len, BufPool* pool)
{
   if(allocatedInterleaved_.load(std::memory_order_acquire))
	  return true;
   std::unique_lock<std::mutex> lk(interleaveMutex_);
   if(allocatedInterleaved_.load(std::memory_order_relaxed))
	  return true;
   stripImg->interleavedData = pool->get(len);
   allocatedInterleaved_.store(true, std::memory_order_release);

   return stripImg->interleavedData.data_;
}
bool Strip::allocInterleaved(uint64_t len, BufPool* pool)
{
   stripImg->interleavedData = pool->get(len);

   return stripImg->interleavedData.data_;
}
StripCache::StripCache()
	: strips(nullptr), numTiles_(0), numStrips_(0), nominalStripHeight_(0), imageY0_(0),
	  packedRowBytes_(0), ioUserData_(nullptr), ioBufferCallback_(nullptr), initialized_(false),
	  multiTile_(true)
{}
StripCache::~StripCache()
{
   for(const auto& p : pools_)
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
void StripCache::init(uint32_t concurrency, uint16_t numTiles, uint32_t numStrips,
					  uint32_t nominalStripHeight, uint8_t reduce, GrkImage* outputImage,
					  grk_io_pixels_callback ioBufferCallback, void* ioUserData,
					  grk_io_register_reclaim_callback registerGrkReclaimCallback)
{
   assert(outputImage);
   if(!numStrips || !outputImage)
	  return;
   multiTile_ = outputImage->hasMultipleTiles;
   ioBufferCallback_ = ioBufferCallback;
   ioUserData_ = ioUserData;
   grk_io_init io_init;
   // we can ignore subsampling since it is disabled for library-orchestrated encoding,
   // which is the only case where maxPooledRequests_ is utilized
   io_init.maxPooledRequests_ =
	   (outputImage->comps->h + outputImage->rowsPerStrip - 1) / outputImage->rowsPerStrip;
   if(registerGrkReclaimCallback)
	  registerGrkReclaimCallback(io_init, grkReclaimCallback, ioUserData, this);
   numTiles_ = numTiles;
   numStrips_ = numStrips;
   imageY0_ = outputImage->y0;
   nominalStripHeight_ = nominalStripHeight;
   packedRowBytes_ = outputImage->packedRowBytes;
   strips = new Strip*[numStrips];
   for(uint16_t i = 0; i < numStrips_; ++i)
	  strips[i] = new Strip(outputImage, i, nominalStripHeight_, reduce);
   initialized_ = true;
   for(uint32_t i = 0; i < concurrency; ++i)
	  pools_.push_back(new BufPool());
}
bool StripCache::ingestStrip(uint32_t threadId, Tile* src, uint32_t yBegin, uint32_t yEnd)
{
   if(!initialized_)
	  return false;

   uint16_t stripId = (uint16_t)((yBegin + nominalStripHeight_ - 1) / nominalStripHeight_);
   assert(stripId < numStrips_);
   auto strip = strips[stripId];
   auto dest = strip->stripImg;
   // use height of first component, because no subsampling
   uint64_t dataLen = packedRowBytes_ * (yEnd - yBegin);
   uint64_t dataOffset = packedRowBytes_ * yBegin;
   if(!strip->allocInterleaved(dataLen, pools_[threadId]))
	  return false;
   if(!dest->compositeInterleaved(src, yBegin, yEnd))
	  return false;

   auto buf = GrkIOBuf(dest->interleavedData);
   buf.index_ = stripId;
   buf.offset_ = dataOffset;
   buf.len_ = dataLen;
   dest->interleavedData.data_ = nullptr;

   return serialize(threadId, buf);
}
// single threaded case
bool StripCache::ingestTile(GrkImage* src)
{
   return ingestTile(0, src);
}
bool StripCache::ingestTile(uint32_t threadId, GrkImage* src)
{
   if(!initialized_)
	  return false;

   uint16_t stripId =
	   (uint16_t)((src->y0 - imageY0_ + nominalStripHeight_ - 1) / nominalStripHeight_);
   assert(stripId < numStrips_);
   auto strip = strips[stripId];
   auto dest = strip->stripImg;
   // use height of first component, because no subsampling
   uint64_t dataLen = packedRowBytes_ * dest->comps->h;
   uint64_t offset = packedRowBytes_ * dest->comps->y0;
   if(!strip->allocInterleavedLocked(dataLen, pools_[threadId]))
	  return false;
   if(!dest->compositeInterleaved(src))
	  return false;

   if(++strip->tileCounter == numTiles_)
   {
	  auto buf = GrkIOBuf(dest->interleavedData);
	  buf.index_ = stripId;
	  buf.offset_ = offset;
	  buf.len_ = dataLen;
	  dest->interleavedData.data_ = nullptr;
	  if(grokNewIO)
		 return ioBufferCallback_(threadId, buf, ioUserData_);
	  if(!serialize(threadId, buf))
		 return false;
   }

   return true;
}

bool StripCache::serialize(uint32_t threadId, GrkIOBuf buf)
{
   if(grokNewIO)
	  return ioBufferCallback_(threadId, buf, ioUserData_);

   std::queue<GrkIOBuf> buffersToSerialize;
   {
	  std::unique_lock<std::mutex> lk(heapMutex_);
	  // 1. push to heap
	  serializeHeap.push(buf);
	  // 2. get all sequential buffers in heap
	  while(serializeHeap.pop(buf))
		 buffersToSerialize.push(buf);
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

   return true;
}

void StripCache::returnBufferToPool(uint32_t threadId, GrkIOBuf b)
{
   pools_[threadId]->put(b);
}

} // namespace grk
