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
	void init(grk_image* image);
	void serializeRegisterClientCallback(grk_serialize_callback reclaim_callback,void* user_data);
	grk_serialize_callback getSerializerReclaimCallback(void);
	void* getSerializerReclaimUserData(void);
#ifndef _WIN32
	int getFd(void);
#endif
	bool open(std::string name, std::string mode);
	bool close(void);
	size_t write(uint8_t* buf, size_t size);
	uint64_t seek(int64_t off, int32_t whence);
	uint32_t getNumPixelRequests(void);
	uint64_t getOffset(void);
#ifdef GROK_HAVE_URING
	void initPixelRequest(void);
#else
	void incrementPixelRequest(void);
#endif
	bool allPixelRequestsComplete(void);
  private:
#ifndef _WIN32
#ifdef GROK_HAVE_URING
	FileUringIO uring;
	GrkSerializeBuf scheduled_;
#endif
#else
	FileStreamIO fileStreamIO;
#endif
	uint32_t numPixelRequests_;
	uint32_t maxPixelRequests_;
#ifndef _WIN32
	int getMode(std::string mode);
	int fd_;
#endif
	bool asynchActive_;
	uint64_t off_;
	grk_serialize_callback reclaim_callback_;
	void* reclaim_user_data_;
};
