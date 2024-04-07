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
   GrkIOBuf(uint8_t* data, size_t offset, size_t dataLen, size_t allocLen, bool pooled,
			uint32_t index)
   {
	  data_ = data;
	  offset_ = offset;
	  len_ = dataLen;
	  allocLen_ = allocLen;
	  pooled_ = pooled;
	  index_ = index;
   }
   explicit GrkIOBuf(const grk_io_buf rhs)
   {
	  data_ = rhs.data_;
	  offset_ = rhs.offset_;
	  len_ = rhs.len_;
	  allocLen_ = rhs.allocLen_;
	  pooled_ = rhs.pooled_;
	  index_ = rhs.index_;
   }
   uint32_t getIndex(void) const
   {
	  return index_;
   }
   bool alloc(size_t len)
   {
	  dealloc();
	  data_ = (uint8_t*)grk_aligned_malloc(len);
	  if(data_)
	  {
		 len_ = len;
		 allocLen_ = len;
	  }

	  return (data_ != nullptr);
   }
   void dealloc()
   {
	  grk_aligned_free(data_);
	  data_ = nullptr;
   }
};

class BufPool
{
 public:
   ~BufPool(void)
   {
	  for(auto& b : pool)
		 b.second.dealloc();
   }
   GrkIOBuf get(size_t len)
   {
	  for(auto iter = pool.begin(); iter != pool.end(); ++iter)
	  {
		 if(iter->second.allocLen_ >= len)
		 {
			auto b = iter->second;
			b.len_ = len;
			pool.erase(iter);
			return b;
		 }
	  }
	  GrkIOBuf rc;
	  rc.alloc(len);

	  return rc;
   }
   void put(GrkIOBuf b)
   {
	  assert(b.data_);
	  assert(pool.find(b.data_) == pool.end());
	  pool[b.data_] = b;
   }

 private:
   std::map<uint8_t*, GrkIOBuf> pool;
};

struct Strip
{
   Strip(GrkImage* outputImage, uint16_t index, uint32_t nominalHeight, uint8_t reduce);
   ~Strip(void);
   uint32_t getIndex(void);
   uint32_t reduceDim(uint32_t dim);
   bool allocInterleavedLocked(uint64_t len, BufPool* pool);
   bool allocInterleaved(uint64_t len, BufPool* pool);
   GrkImage* stripImg;
   std::atomic<uint32_t> tileCounter; // count number of tiles added to strip
   uint8_t reduce_; // resolution reduction
   mutable std::mutex interleaveMutex_;
   mutable std::atomic<bool> allocatedInterleaved_;
};

class StripCache
{
 public:
   StripCache(void);
   virtual ~StripCache();

   void init(uint32_t concurrency, uint16_t numTiles_, uint32_t numStrips,
			 uint32_t nominalStripHeight, uint8_t reduce, GrkImage* outputImg,
			 grk_io_pixels_callback ioBufferCallback, void* ioUserData,
			 grk_io_register_reclaim_callback grkRegisterReclaimCallback);
   bool ingestTile(uint32_t threadId, GrkImage* src);
   bool ingestTile(GrkImage* src);
   bool ingestStrip(uint32_t threadId, Tile* src, uint32_t yBegin, uint32_t yEnd);
   void returnBufferToPool(uint32_t threadId, GrkIOBuf b);
   bool isInitialized(void);
   bool isMultiTile(void);

 private:
   bool serialize(uint32_t threadId, GrkIOBuf buf);
   std::vector<BufPool*> pools_;
   Strip** strips;
   uint16_t numTiles_;
   uint32_t numStrips_;
   uint32_t nominalStripHeight_;
   uint32_t imageY0_;
   uint64_t packedRowBytes_;
   void* ioUserData_;
   grk_io_pixels_callback ioBufferCallback_;
   mutable std::mutex serializeMutex_;
   MinHeap<GrkIOBuf, uint32_t, MinHeapFakeLocker> serializeHeap;
   mutable std::mutex heapMutex_;
   bool initialized_;
   bool multiTile_;
};

} // namespace grk
