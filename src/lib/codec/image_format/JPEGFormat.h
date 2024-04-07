/*
 *    Copyright (C) 2016 Grok Image Compression Inc.
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
 *
 */
#pragma once

#include "ImageFormat.h"
#ifdef _WIN32
#define HAVE_BOOLEAN
typedef unsigned char boolean;
#include <basetsd.h>
#endif
#include "jpeglib.h"
#include "iccjpeg.h"
#include "convert.h"

class JPEGFormat : public ImageFormat
{
 public:
   JPEGFormat(void);
   bool encodeHeader(void) override;
   bool encodePixels() override;
   using ImageFormat::encodePixels;
   bool encodeFinish(void) override;
   grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

 private:
   grk_image* jpegtoimage(const char* filename, grk_cparameters* parameters);
   bool imagetojpeg(grk_image* image, const char* filename, uint32_t compressionLevel);

   bool success;
   uint8_t* buffer;
   int32_t* buffer32s;
   J_COLOR_SPACE color_space;
   int32_t adjust;
   bool readFromStdin;

   /* This struct contains the JPEG compression parameters and pointers to
	* working space (which is allocated as needed by the JPEG library).
	* It is possible to have several such structures, representing multiple
	* compression/decompression processes, in existence at once.  We refer
	* to any one struct (and its associated working data) as a "JPEG object".
	*/
   struct jpeg_compress_struct cinfo;
   int32_t const* planes[3];
};
