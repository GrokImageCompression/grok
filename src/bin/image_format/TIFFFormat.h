/*
*    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
# error GROK_HAVE_LIBTIFF_NOT_DEFINED
#endif /* GROK_HAVE_LIBTIFF */

#include <tiffio.h>
#include "convert.h"

const size_t maxNumComponents = 10;

 /* TIFF conversion*/
void tiffSetErrorAndWarningHandlers(bool verbose);

class TIFFFormat: public ImageFormat {
public:
	TIFFFormat();
	~TIFFFormat();
	bool encodeHeader(grk_image *  image, const std::string &filename, uint32_t compressionParam) override;
	bool encodeStrip(uint32_t rows) override;
	bool encodeFinish(void) override;
	grk_image *  decode(const std::string &filename,  grk_cparameters  *parameters) override;
private:
	TIFF *tif;
	uint32_t chroma_subsample_x;
	uint32_t chroma_subsample_y;
	int32_t const *planes[maxNumComponents];
	cvtPlanarToInterleaved cvtPxToCx;
	cvtFrom32 cvt32sToTif;
};
