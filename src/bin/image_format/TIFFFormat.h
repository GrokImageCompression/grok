/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

#include "grk_apps_config.h"
#ifdef GROK_HAVE_LIBTIFF

#ifdef GROK_HAVE_URING
#include "FileUringIO.h"
#endif

#include "ImageFormat.h"
#include <tiffio.h>
#include "convert.h"

/* TIFF conversion*/
void tiffSetErrorAndWarningHandlers(bool verbose);

struct ClientData {
	ClientData();
#ifdef GROK_HAVE_URING
	bool write(uint8_t* buf, size_t len);
	FileUringIO uring;
#endif
	int fd;
	bool incomingPixelWrite;
	uint32_t maxPixelWrites;
private:
	uint32_t numPixelWrites;
	bool active;
};


class TIFFFormat : public ImageFormat
{
  public:
	TIFFFormat();
	~TIFFFormat();
	bool encodeHeader(grk_image* image) override;
	bool encodeRows(uint32_t rows) override;
	bool encodePixels(uint8_t *data,
							uint64_t dataLen,
							grk_serialize_buf** reclaimed,
							uint32_t max_reclaimed,
							uint32_t *num_reclaimed, uint32_t strip) override;
	bool encodeFinish(void) override;
	grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

  private:
#ifndef _WIN32
	TIFF* MyTIFFOpen(const char* name, const char* mode);
#endif
	ClientData clientData;
	TIFF* tif;
	uint8_t *packedBuf;
	uint32_t chroma_subsample_x;
	uint32_t chroma_subsample_y;
	uint32_t rowsWritten;
	uint32_t strip;
	size_t units;
	uint64_t bytesToWrite;
	uint16_t numcomps;
};

#endif
