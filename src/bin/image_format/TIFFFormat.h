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

#if !defined(_WIN32) && defined(GROK_BUILD_THIRDPARTY)
#define GRK_CUSTOM_TIFF_IO
#endif

#include "ImageFormat.h"
#include <tiffio.h>
#include "convert.h"

/* TIFF conversion*/
void tiffSetErrorAndWarningHandlers(bool verbose);

class TIFFFormat : public ImageFormat
{
  public:
	TIFFFormat();
	~TIFFFormat();
	void ioRegisterClientCallback(grk_io_callback reclaim_callback,
										 void* user_data) override;
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

  private:
#ifdef GRK_CUSTOM_TIFF_IO
	TIFF* MyTIFFOpen(const char* name, const char* mode);
#endif
	/***
	 * Common core pixel encoding write to disk
	 */
	bool encodePixelsCoreWrite(grk_io_buf pixels) override;
	TIFF* tif;
	uint32_t chroma_subsample_x;
	uint32_t chroma_subsample_y;
	size_t units;
};

#endif
