/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#pragma once

#include <tiffio.h>
#include <functional>

#include "ImageFormat.h"

namespace io {

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
