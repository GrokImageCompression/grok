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
#include "ImageFormat.h"

#ifndef GROK_HAVE_LIBTIFF
#error GROK_HAVE_LIBTIFF_NOT_DEFINED
#endif /* GROK_HAVE_LIBTIFF */

#include <tiffio.h>
#include "convert.h"

/* TIFF conversion*/
void tiffSetErrorAndWarningHandlers(bool verbose);

class TIFFFormat : public ImageFormat
{
  public:
	TIFFFormat();
	~TIFFFormat();
	bool encodeHeader(grk_image* image, const std::string& filename,
					  uint32_t compressionParam) override;
	bool encodeRows(uint32_t rows) override;
	bool encodeFinish(void) override;
	grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

  private:
	bool writeStrip(void* buf, tmsize_t toWrite);
	TIFF* tif;
	uint8_t *packedBuf;
	uint32_t chroma_subsample_x;
	uint32_t chroma_subsample_y;
	int32_t const* planes[grk::maxNumPackComponents];
	uint32_t rowsWritten;
	uint32_t strip;
	tsize_t rowsPerStrip;
	tsize_t packedBufStride;
	size_t units;
	tmsize_t bytesToWrite;
};
