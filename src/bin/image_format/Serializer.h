#pragma once

#include "grk_apps_config.h"
#include "grok.h"
#include "IFileIO.h"

#ifdef GROK_HAVE_URING
#include "FileUringIO.h"
#endif

#include <cstdint>

struct Serializer {
	Serializer(void);
	void init(grk_image *image);
	bool isAsynchActive(void);
	uint64_t getAsynchFileLength(void);
#ifndef _WIN32
	int getFd(void);
	bool open(const char* name, const char* mode, bool readOp);
	bool close(void);
#endif
#ifdef GROK_HAVE_URING
	bool write(uint8_t *buf, uint64_t size);
#endif
	void clear(void);
	void initPixelRequest(grk_serialize_buf* reclaimed,
						uint32_t max_reclaimed,
						uint32_t *num_reclaimed);
	void incrementPixelRequest(void);
	bool allPixelRequestsComplete(void);
private:
#ifdef GROK_HAVE_URING
	FileUringIO uring;
	GrkSerializeBuf scheduled_;
#endif
	grk_serialize_buf* reclaimed_;
	uint32_t max_reclaimed_;
	uint32_t *num_reclaimed_;
	uint32_t numPixelRequests;
	uint32_t maxPixelRequests;
#ifndef _WIN32
	int getMode(const char* mode);
	int fd_;
#endif
	bool asynchActive_;
	uint64_t off_;
};
