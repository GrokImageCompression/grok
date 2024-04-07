/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

#if !defined(_WIN32) && defined(GRK_BUILD_LIBTIFF)
#define GRK_CUSTOM_TIFF_IO
#endif

#include "ImageFormat.h"
#include <tiffio.h>
#include "convert.h"

// #define GRK_NEW_IO

#ifdef GRK_NEW_IO
#include "../io/TIFFFormat.h"
#endif

/* TIFF conversion*/
void tiffSetErrorAndWarningHandlers(bool verbose);

class TIFFFormat : public ImageFormat
{
 public:
   TIFFFormat();
   ~TIFFFormat();
   void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
								   void* user_data) override;

   bool encodeInit(grk_image* image, const std::string& filename, uint32_t compressionLevel,
				   uint32_t concurrency) override;
   bool encodeHeader(void) override;
   /***
	* application-orchestrated pixel encoding
	*/
   bool encodePixels() override;
   /***
	* library-orchestrated pixel encoding
	*/
   virtual bool encodePixels(uint32_t threadId, grk_io_buf pixels) override;
   bool encodeFinish(void) override;
   grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

#ifdef GRK_NEW_IO
   bool ioReclaim(uint32_t threadId, io::io_buf* buffer);
#endif

 private:
#ifdef GRK_CUSTOM_TIFF_IO
   TIFF* MyTIFFOpen(const char* name, const char* mode);
#endif
   bool encodeHeader(TIFF* tif);
   /***
	* Common core pixel encoding write to disk
	*/
   bool encodePixelsCoreWrite(grk_io_buf pixels) override;
   TIFF* tif_;
   uint32_t chroma_subsample_x;
   uint32_t chroma_subsample_y;
   size_t units;
#ifdef GRK_NEW_IO
   io::TIFFFormat ioTiffFormat;
#endif
   grk_io_callback grkReclaimCallback_;
   void* grkReclaimUserData_;
};

#endif
