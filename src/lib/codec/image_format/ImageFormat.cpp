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

#include "ImageFormat.h"
#include "convert.h"
#include <algorithm>
#include "spdlog/spdlog.h"
#include "common.h"
#include "FileStreamIO.h"

static bool grkReclaimCallback([[maybe_unused]] uint32_t threadId, grk_io_buf buffer,
							   void* io_user_data)
{
   auto pool = static_cast<BufferPool*>(io_user_data);
   if(pool)
	  pool->put(GrkIOBuf(buffer));

   return true;
}

ImageFormat::ImageFormat()
	: image_(nullptr), fileIO_(new FileStreamIO()), fileStream_(nullptr), fileName_(""),
	  compressionLevel_(GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT), useStdIO_(false),
	  encodeState(IMAGE_FORMAT_UNENCODED)
{
   grk_io_init init;
   init.maxPooledRequests_ = 0;
   registerGrkReclaimCallback(init, grkReclaimCallback, &pool);
}
ImageFormat::~ImageFormat()
{
   delete fileIO_;
}
void ImageFormat::registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
											 void* user_data)
{
   serializer.registerGrkReclaimCallback(io_init, reclaim_callback, user_data);
   if(io_init.maxPooledRequests_)
	  serializer.setMaxPooledRequests(io_init.maxPooledRequests_);
}
void ImageFormat::ioReclaimBuffer(uint32_t threadId, grk_io_buf buffer)
{
   auto cb = serializer.getIOReclaimCallback();
   if(cb)
	  cb(threadId, buffer, serializer.getIOReclaimUserData());
}
#ifndef GROK_HAVE_URING
void ImageFormat::reclaim(uint32_t threadId, grk_io_buf pixels)
{
   // for synchronous encode, we immediately return the pixel buffer to the pool
   ioReclaimBuffer(threadId, GrkIOBuf(pixels));
}
#endif
bool ImageFormat::encodeInit(grk_image* image, const std::string& filename,
							 uint32_t compressionLevel, [[maybe_unused]] uint32_t concurrency)
{
   compressionLevel_ = compressionLevel;
   fileName_ = filename;
   image_ = image;
   useStdIO_ = grk::useStdio(fileName_);

   return true;
}
/***
 * library-orchestrated pixel encoding
 */
bool ImageFormat::encodePixels(uint32_t threadId, grk_io_buf pixels)
{
   std::unique_lock<std::mutex> lk(encodePixelmutex);
   if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
	  return true;
   if(!isHeaderEncoded() && !encodeHeader())
	  return false;

   return encodePixelsCore(threadId, pixels);
}
/***
 * Common core pixel encoding
 */
bool ImageFormat::encodePixelsCore([[maybe_unused]] uint32_t threadId, grk_io_buf pixels)
{
#ifdef GROK_HAVE_URING
   serializer.initPooledRequest();
#endif
   bool success = encodePixelsCoreWrite(pixels);
   if(success)
   {
#ifndef GROK_HAVE_URING
	  serializer.incrementPooled();
	  // for synchronous encode, we immediately return the pixel buffer to the pool
	  reclaim(threadId, GrkIOBuf(pixels));
#endif
	  if(serializer.allPooledRequestsComplete())
		 encodeFinish();
   }
   else
   {
	  spdlog::error("TIFFFormat::encodePixelsCore: error in pixels encode");
	  encodeState |= IMAGE_FORMAT_ERROR;
   }

   return success;
}
// reclaim to local pool if library reclamation is not enabled
void ImageFormat::applicationOrchestratedReclaim([[maybe_unused]] GrkIOBuf buf)
{
#ifndef GROK_HAVE_URING
   if(!serializer.getIOReclaimCallback())
   {
	  pool.put(buf);
   }
#endif
}
/***
 * Common core pixel encoding write to disk
 */
bool ImageFormat::encodePixelsCoreWrite(grk_io_buf pixels)
{
   return (serializer.write(pixels.data_, pixels.len_) == pixels.len_);
}
bool ImageFormat::encodeFinish(void)
{
   bool rc = fileIO_->close();
   delete fileIO_;
   fileIO_ = nullptr;
   fileStream_ = nullptr;
   fileName_ = "";

   return rc;
}
bool ImageFormat::isHeaderEncoded(void)
{
   return ((encodeState & IMAGE_FORMAT_ENCODED_HEADER) == IMAGE_FORMAT_ENCODED_HEADER);
}
bool ImageFormat::open(const std::string& fileName, const std::string& mode)
{
   return fileIO_->open(fileName, mode);
}
uint64_t ImageFormat::write(GrkIOBuf buffer)
{
   auto rc = fileIO_->write(buffer);
#ifndef GROK_HAVE_URING
   if(buffer.pooled_)
	  pool.put(buffer);
#endif

   return rc;
}
bool ImageFormat::read(uint8_t* buf, size_t len)
{
   return fileIO_->read(buf, len);
}
bool ImageFormat::seek(int64_t pos, int whence)
{
   return fileIO_->seek(pos, whence) == 0U;
}
uint32_t ImageFormat::getEncodeState(void)
{
   return encodeState;
}
bool ImageFormat::openFile(void)
{
   bool rc = fileIO_->open(fileName_, "w");
   if(rc)
	  fileStream_ = static_cast<FileStreamIO*>(fileIO_)->getFileStream();

   return rc;
}
uint32_t ImageFormat::maxY(uint32_t rows)
{
   return std::min<uint32_t>(rows, image_->decompressHeight);
}
void ImageFormat::scaleComponent(grk_image_comp* component, uint8_t precision)
{
   if(component->prec == precision)
	  return;
   uint32_t stride_diff = component->stride - component->w;
   auto data = component->data;
   if(component->prec < precision)
   {
	  int32_t scale = 1 << (uint32_t)(precision - component->prec);
	  size_t index = 0;
	  for(uint32_t j = 0; j < component->h; ++j)
	  {
		 for(uint32_t i = 0; i < component->w; ++i)
		 {
			data[index] = data[index] * scale;
			index++;
		 }
		 index += stride_diff;
	  }
   }
   else
   {
	  int32_t scale = 1 << (uint32_t)(component->prec - precision);
	  size_t index = 0;
	  for(uint32_t j = 0; j < component->h; ++j)
	  {
		 for(uint32_t i = 0; i < component->w; ++i)
		 {
			data[index] = data[index] / scale;
			index++;
		 }
		 index += stride_diff;
	  }
   }
   component->prec = precision;
}
void ImageFormat::allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries)
{
   assert(color);
   assert(num_channels);
   assert(num_entries);

   auto jp2_pclr = new grk_palette_data();
   jp2_pclr->channel_sign = new bool[num_channels];
   jp2_pclr->channel_prec = new uint8_t[num_channels];
   jp2_pclr->lut = new int32_t[num_channels * num_entries];
   jp2_pclr->num_entries = num_entries;
   jp2_pclr->num_channels = num_channels;
   jp2_pclr->component_mapping = nullptr;
   color->palette = jp2_pclr;
}
void ImageFormat::copy_icc(grk_image* dest, uint8_t* iccbuf, uint32_t icclen)
{
   create_meta(dest);
   dest->meta->color.icc_profile_buf = new uint8_t[icclen];
   memcpy(dest->meta->color.icc_profile_buf, iccbuf, icclen);
   dest->meta->color.icc_profile_len = icclen;
}
void ImageFormat::create_meta(grk_image* img)
{
   if(img && !img->meta)
	  img->meta = grk_image_meta_new();
}
/**
 * return false if :
 * 1. any component's precision is either 0 or greater than GRK_MAX_SUPPORTED_IMAGE_PRECISION
 * 2. any component's signedness does not match another component's signedness
 * 3. any component's precision does not match another component's precision
 *    (if equalPrecision is true)
 *
 */
bool ImageFormat::allComponentsSanityCheck(grk_image* image, bool checkEqualPrecision)
{
   assert(image);
   if(image->decompressNumComps == 0)
	  return false;
   if(image_->precision)
	  checkEqualPrecision = false;
   auto comp0 = image->comps;
   if(comp0->prec == 0 || comp0->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
   {
	  spdlog::warn("component 0 precision {} is not supported.", 0, comp0->prec);
	  return false;
   }
   for(uint16_t i = 1U; i < image->decompressNumComps; ++i)
   {
	  auto comp_i = image->comps + i;
	  if(checkEqualPrecision && comp0->prec != comp_i->prec)
	  {
		 spdlog::warn("precision {} of component {}"
					  " differs from precision {} of component 0.",
					  comp_i->prec, i, comp0->prec);
		 return false;
	  }
	  if(comp0->sgnd != comp_i->sgnd)
	  {
		 spdlog::warn("signedness {} of component {}"
					  " differs from signedness {} of component 0.",
					  comp_i->sgnd, i, comp0->sgnd);
		 return false;
	  }
   }

   return true;
}
bool ImageFormat::areAllComponentsSameSubsampling(grk_image* image)
{
   assert(image);
   if(image->decompressNumComps == 1)
	  return true;
   if(image->upsample || image->forceRGB)
	  return true;
   auto comp0 = image->comps;
   for(uint32_t i = 0; i < image->decompressNumComps; ++i)
   {
	  auto comp = image->comps + i;
	  if(comp->dx != comp0->dx || comp->dy != comp0->dy)
	  {
		 spdlog::error("Not all components have same sub-sampling");
		 return false;
	  }
   }

   return true;
}

bool ImageFormat::isFinalOutputSubsampled(grk_image* image)
{
   assert(image);
   if(image->upsample || image->forceRGB)
	  return false;
   for(uint32_t i = 0; i < image->decompressNumComps; ++i)
   {
	  if(image->comps[i].dx != 1 || image->comps[i].dy != 1)
		 return true;
   }

   return false;
}
bool ImageFormat::isChromaSubsampled(grk_image* image)
{
   assert(image);
   if(image->decompressNumComps < 3 || image->forceRGB || image->upsample)
	  return false;
   for(uint32_t i = 0; i < image->decompressNumComps; ++i)
   {
	  auto comp = image->comps + i;
	  switch(i)
	  {
		 case 1:
		 case 2:
			if(comp->type != GRK_CHANNEL_TYPE_COLOUR)
			   return false;
			break;
		 default:
			if(comp->dx != 1 || comp->dy != 1)
			   return false;
			break;
	  }
   }
   auto compB = image->comps + 1;
   auto compR = image->comps + 2;

   return (compB->dx == compR->dx && compB->dy == compR->dy);
}
