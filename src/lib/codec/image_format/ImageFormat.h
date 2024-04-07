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
 *
 */

#pragma once

#include "IImageFormat.h"
#include "IFileIO.h"
#include "BufferPool.h"
#include "Serializer.h"

#include <mutex>

const uint32_t reclaimSize = 5;

class ImageFormat : public IImageFormat
{
 public:
   ImageFormat();
   virtual ~ImageFormat();
   virtual void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
										   void* user_data) override;
   void ioReclaimBuffer(uint32_t threadId, grk_io_buf buffer);
#ifndef GROK_HAVE_URING
   void reclaim(uint32_t threadId, grk_io_buf pixels);
#endif
   virtual bool encodeInit(grk_image* image, const std::string& filename, uint32_t compressionLevel,
						   uint32_t concurrency) override;
   /***
	* library-orchestrated pixel encoding
	*/
   virtual bool encodePixels(uint32_t threadId, grk_io_buf pixels) override;
   virtual bool encodeFinish(void) override;
   uint32_t getEncodeState(void) override;
   bool openFile(void);

 protected:
   void applicationOrchestratedReclaim(GrkIOBuf buf);
   /***
	* Common core pixel encoding
	*/
   virtual bool encodePixelsCore(uint32_t threadId, grk_io_buf pixels);
   /***
	* Common core pixel encoding write to disk
	*/
   virtual bool encodePixelsCoreWrite(grk_io_buf pixels);
   bool open(const std::string& fname, const std::string& mode);
   uint64_t write(GrkIOBuf buffer);
   bool read(uint8_t* buf, size_t len);
   bool seek(int64_t pos, int whence);
   uint32_t maxY(uint32_t rows);
   int getMode(const char* mode);
   void scaleComponent(grk_image_comp* component, uint8_t precision);

   void allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries);
   void copy_icc(grk_image* dest, uint8_t* iccbuf, uint32_t icclen);
   void create_meta(grk_image* img);
   bool allComponentsSanityCheck(grk_image* image, bool equalPrecision);
   bool isFinalOutputSubsampled(grk_image* image);
   bool isChromaSubsampled(grk_image* image);
   bool areAllComponentsSameSubsampling(grk_image* image);
   bool isHeaderEncoded(void);

   grk_image* image_;
   IFileIO* fileIO_;
   FILE* fileStream_;
   std::string fileName_;
   uint32_t compressionLevel_;

   bool useStdIO_;
   uint32_t encodeState;
   mutable std::mutex encodePixelmutex;
   BufferPool pool;
   Serializer serializer;
};
