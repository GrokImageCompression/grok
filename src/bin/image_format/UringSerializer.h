#pragma once

#include "grk_apps_config.h"
#include "grok.h"

#ifdef GROK_HAVE_URING
#include "FileUringIO.h"
#endif

#include <cstdint>

struct UringSerializer {
	UringSerializer(void);
	bool isActive(void);
	uint64_t getAsynchFileLength(void);
#ifdef GROK_HAVE_URING
	bool write(void);
	FileUringIO uring;
#endif
	GrkSerializeBuf scheduled;
	grk_serialize_buf* reclaimed;
	uint32_t max_reclaimed;
	uint32_t *num_reclaimed;
	uint32_t maxPixelRequests;
private:
	uint32_t numPixelRequests;
	bool active_;
	uint64_t off_;
};
