#pragma once

#include <tiffio.h>
#include <functional>

#include "ImageFormat.h"

namespace iobench {

struct TIFFFormatHeaderClassic {
	TIFFFormatHeaderClassic() : tiff_magic(0x4949),
								tiff_version(42),
								tiff_diroff(0)
	{}
	uint16_t tiff_magic;      /* magic number (defines byte order) */
	uint16_t tiff_version;    /* TIFF version number */
	uint32_t tiff_diroff;     /* byte offset to first directory */
};

class TIFFFormat : public ImageFormat {
public:
	TIFFFormat(void);
	TIFFFormat(bool flushOnClose);
	virtual ~TIFFFormat() = default;
	using ImageFormat::init;
	using ImageFormat::encodeInit;
	using ImageFormat::encodePixels;
	void setHeaderWriter(std::function<bool(TIFF* tif)> writer);
	bool encodeFinish(void);
	bool close(void) override;

private:
	bool encodePixels(io_buf pixels);
	bool encodeHeader(void);
	TIFF* tif_;
	TIFFFormatHeaderClassic header_;
	std::function<bool(TIFF* tif)> headerWriter_;
};

}
