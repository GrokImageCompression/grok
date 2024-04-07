#pragma once

#include "grk_apps_config.h"
#include "grok.h"
#include "IFileIO.h"
#include "FileStreamIO.h"

#ifdef GROK_HAVE_URING
#include "FileUringIO.h"
#endif

#include <cstdint>

struct Serializer
{
   Serializer(void);
   void setMaxPooledRequests(uint32_t maxRequests);
   void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
								   void* user_data);
   grk_io_callback getIOReclaimCallback(void);
   void* getIOReclaimUserData(void);
#ifndef _WIN32
   int getFd(void);
#endif
   bool open(const std::string& name, const std::string& mode, bool asynch);
   bool close(void);
   size_t write(uint8_t* buf, size_t size);
   uint64_t seek(int64_t off, int32_t whence);
   uint32_t getNumPooledRequests(void);
   uint64_t getOffset(void);
#ifdef GROK_HAVE_URING
   void initPooledRequest(void);
#else
   void incrementPooled(void);
#endif
   bool allPooledRequestsComplete(void);

 private:
#ifndef _WIN32
#ifdef GROK_HAVE_URING
   FileUringIO uring;
   GrkIOBuf scheduled_;
#endif
   int getMode(std::string mode);
   int fd_;
#else
   FileStreamIO fileStreamIO;
#endif
   uint32_t numPooledRequests_;
   // used to detect when library-orchestrated encode is complete
   uint32_t maxPooledRequests_;
   bool asynchActive_;
   uint64_t off_;
   grk_io_callback reclaim_callback_;
   void* reclaim_user_data_;
   std::string filename_;
   std::string mode_;
};
