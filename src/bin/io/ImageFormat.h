#pragma once

#include <string>

#include "ImageStripper.h"
#include "Serializer.h"
#include "BufferPool.h"

namespace iobench {

const uint32_t IMAGE_FORMAT_UNENCODED = 1;
const uint32_t IMAGE_FORMAT_ENCODED_HEADER = 2;
const uint32_t IMAGE_FORMAT_ENCODED_PIXELS = 4;
const uint32_t IMAGE_FORMAT_ERROR = 8;

class ImageFormat {
public:
	ImageFormat(bool flushOnClose,
				uint8_t *header,
				size_t headerLength);
	virtual ~ImageFormat();
	virtual bool close(void);
	virtual void init(uint32_t width,
						uint32_t height,
						uint16_t numcomps,
						uint64_t packedByteWidth,
						uint32_t nominalStripHeight,
						bool chunked);
	virtual bool encodeInit(std::string filename,
							bool direct,
							uint32_t concurrency,
							bool asynch);
	virtual bool encodePixels(uint32_t threadId,
								IOBuf **buffers,
								uint32_t numBuffers);
	virtual bool encodePixels(uint32_t threadId,StripChunkArray * chunkArray);
	IOBuf* getPoolBuffer(uint32_t threadId,uint32_t strip);
	StripChunkArray* getStripChunkArray(uint32_t threadId,uint32_t strip);
	ImageStripper* getImageStripper(void);
protected:
	bool isHeaderEncoded(void);
	uint8_t *header_;
	size_t headerLength_;
	uint32_t encodeState_;
	Serializer serializer_;
	ImageStripper* imageStripper_;
	std::string filename_;
	std::string mode_;
	uint32_t concurrency_;
	Serializer **workerSerializers_;
	std::atomic<uint32_t> numPixelWrites_;
};

}
