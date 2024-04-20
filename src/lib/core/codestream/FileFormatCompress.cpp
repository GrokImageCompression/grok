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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
void MycmsLogErrorHandlerFunction([[maybe_unused]] cmsContext ContextID,
								  [[maybe_unused]] cmsUInt32Number ErrorCode, const char* Text)
{
   Logger::logger_.warn(" LCMS error: {}", Text);
}

typedef std::function<uint8_t*(uint32_t* len)> WRITE_FUNC;
struct BoxWriteHandler
{
   BoxWriteHandler() : handler(nullptr), data_(nullptr), size_(0) {}
   WRITE_FUNC handler;
   uint8_t* data_;
   uint32_t size_;
};

FileFormatCompress::FileFormatCompress(BufferedStream* stream)
	: FileFormat(), codeStream(new CodeStreamCompress(stream)), needs_xl_jp2c_box_length(false),
	  j2k_codestream_offset(0), inputImage_(nullptr)
{}
FileFormatCompress::~FileFormatCompress()
{
   if(inputImage_)
	  grk_object_unref(&inputImage_->obj);
   delete codeStream;
}

grk_color* FileFormatCompress::getColour(void)
{
   if(!inputImage_ || !inputImage_->meta)
	  return nullptr;

   return &inputImage_->meta->color;
}
bool FileFormatCompress::write_jp(void)
{
   auto stream = codeStream->getStream();
   assert(stream);

   /* write box length */
   if(!stream->writeInt(12))
	  return false;
   /* writes box type */
   if(!stream->writeInt(JP2_JP))
	  return false;
   /* writes magic number*/
   if(!stream->writeInt(0x0d0a870a))
	  return false;
   return true;
}
bool FileFormatCompress::write_jp2c(void)
{
   auto stream = codeStream->getStream();
   assert(stream);

   assert(stream->hasSeek());

   uint64_t j2k_codestream_exit = stream->tell();
   if(!stream->seek(j2k_codestream_offset))
   {
	  Logger::logger_.error("Failed to seek in the stream.");
	  return false;
   }

   /* size of code stream */
   uint64_t actualLength = j2k_codestream_exit - j2k_codestream_offset;
   // initialize signalledLength to 0, indicating length was not known
   // when file was written
   uint32_t signaledLength = 0;
   if(needs_xl_jp2c_box_length)
	  signaledLength = 1;
   else
   {
	  if(actualLength < (uint64_t)1 << 32)
		 signaledLength = (uint32_t)actualLength;
   }
   if(!stream->writeInt(signaledLength))
	  return false;
   if(!stream->writeInt(JP2_JP2C))
	  return false;
   // XL box
   if(signaledLength == 1)
   {
	  if(!stream->write64(actualLength))
		 return false;
   }
   if(!stream->seek(j2k_codestream_exit))
   {
	  Logger::logger_.error("Failed to seek in the stream.");
	  return false;
   }

   return true;
}
bool FileFormatCompress::write_ftyp(void)
{
   auto stream = codeStream->getStream();
   assert(stream);

   uint32_t ftyp_size = 16 + 4 * numcl;
   bool result = true;

   if(!stream->writeInt(ftyp_size))
   {
	  result = false;
	  goto end;
   }
   if(!stream->writeInt(JP2_FTYP))
   {
	  result = false;
	  goto end;
   }
   if(!stream->writeInt(brand))
   {
	  result = false;
	  goto end;
   }
   /* MinV */
   if(!stream->writeInt(minversion))
   {
	  result = false;
	  goto end;
   }

   /* CL */
   for(uint32_t i = 0; i < numcl; i++)
   {
	  if(!stream->writeInt(cl[i]))
	  {
		 result = false;
		 goto end;
	  }
   }

end:
   if(!result)
	  Logger::logger_.error("Error while writing ftyp data to stream");
   return result;
}
bool FileFormatCompress::write_uuids(void)
{
   auto stream = codeStream->getStream();
   assert(stream != nullptr);

   // write the uuids
   for(size_t i = 0; i < numUuids; ++i)
   {
	  auto uuid = uuids + i;
	  if(uuid->buf && uuid->len)
	  {
		 /* write box size */
		 stream->writeInt((uint32_t)(8 + 16 + uuid->len));

		 /* JP2_UUID */
		 stream->writeInt(JP2_UUID);

		 /* uuid  */
		 stream->writeBytes(uuid->uuid, 16);

		 /* uuid data */
		 stream->writeBytes(uuid->buf, (uint32_t)uuid->len);
	  }
   }
   return true;
}
bool FileFormatCompress::write_jp2h(void)
{
   BoxWriteHandler writers[32];
   int32_t i, nb_writers = 0;
   /* size of data for super box*/
   uint32_t jp2h_size = 8;
   bool result = true;
   auto stream = codeStream->getStream();
   assert(stream != nullptr);

   writers[nb_writers++].handler =
	   std::bind(&FileFormatCompress::write_ihdr, this, std::placeholders::_1);
   if(bpc == 0xFF)
	  writers[nb_writers++].handler =
		  std::bind(&FileFormatCompress::write_bpc, this, std::placeholders::_1);
   writers[nb_writers++].handler =
	   std::bind(&FileFormatCompress::write_colr, this, std::placeholders::_1);
   if(inputImage_->meta)
   {
	  if(getColour()->channel_definition)
		 writers[nb_writers++].handler =
			 std::bind(&FileFormatCompress::write_channel_definition, this, std::placeholders::_1);
	  if(getColour()->palette)
	  {
		 writers[nb_writers++].handler =
			 std::bind(&FileFormatCompress::write_palette_clr, this, std::placeholders::_1);
		 writers[nb_writers++].handler =
			 std::bind(&FileFormatCompress::write_component_mapping, this, std::placeholders::_1);
	  }
   }
   if(has_display_resolution || has_capture_resolution)
   {
	  bool storeCapture = capture_resolution[0] > 0 && capture_resolution[1] > 0;
	  bool storeDisplay = display_resolution[0] > 0 && display_resolution[1] > 0;
	  if(storeCapture || storeDisplay)
		 writers[nb_writers++].handler =
			 std::bind(&FileFormatCompress::write_res, this, std::placeholders::_1);
   }
   if(xml.buf && xml.len)
	  writers[nb_writers++].handler =
		  std::bind(&FileFormatCompress::write_xml, this, std::placeholders::_1);
   for(i = 0; i < nb_writers; ++i)
   {
	  auto current_writer = writers + i;
	  current_writer->data_ = current_writer->handler(&(current_writer->size_));
	  if(current_writer->data_ == nullptr)
	  {
		 Logger::logger_.error("Not enough memory to hold JP2 Header data");
		 result = false;
		 break;
	  }
	  jp2h_size += current_writer->size_;
   }

   if(!result)
   {
	  for(i = 0; i < nb_writers; ++i)
	  {
		 auto current_writer = writers + i;
		 grk_free(current_writer->data_);
	  }
	  return false;
   }

   /* write super box size */
   if(!stream->writeInt(jp2h_size))
	  result = false;
   if(!stream->writeInt(JP2_JP2H))
	  result = false;

   if(result)
   {
	  for(i = 0; i < nb_writers; ++i)
	  {
		 auto current_writer = writers + i;
		 if(stream->writeBytes(current_writer->data_, current_writer->size_) !=
			current_writer->size_)
		 {
			result = false;
			break;
		 }
	  }
   }
   for(i = 0; i < nb_writers; ++i)
   {
	  auto current_writer = writers + i;
	  grk_free(current_writer->data_);
   }

   return result;
}
uint8_t* FileFormatCompress::write_palette_clr(uint32_t* p_nb_bytes_written)
{
   auto palette = getColour()->palette;
   assert(palette);

   uint32_t bytesPerEntry = 0;
   for(uint32_t i = 0; i < palette->num_channels; ++i)
	  bytesPerEntry += ((palette->channel_prec[i] + 7U) / 8U);

   uint32_t boxSize =
	   4U + 4U + 2U + 1U + palette->num_channels + bytesPerEntry * palette->num_entries;

   uint8_t* paletteBuf = (uint8_t*)grk_malloc(boxSize);
   uint8_t* palette_ptr = paletteBuf;

   /* box size */
   grk_write<uint32_t>(palette_ptr, boxSize);
   palette_ptr += 4;

   /* PCLR */
   grk_write<uint32_t>(palette_ptr, JP2_PCLR);
   palette_ptr += 4;

   // number of LUT entries
   grk_write<uint16_t>(palette_ptr, palette->num_entries);
   palette_ptr += 2;

   // number of channels
   grk_write<uint8_t>(palette_ptr++, palette->num_channels);

   for(uint8_t i = 0; i < palette->num_channels; ++i)
	  grk_write<uint8_t>(palette_ptr++, (uint8_t)(palette->channel_prec[i] - 1U)); // Bi

   // LUT values for all components
   auto lut_ptr = palette->lut;
   for(uint16_t j = 0; j < palette->num_entries; ++j)
   {
	  for(uint8_t i = 0; i < palette->num_channels; ++i)
	  {
		 uint32_t bytes_to_write = (uint32_t)((palette->channel_prec[i] + 7U) >> 3);
		 grk_write<int32_t>(palette_ptr, *lut_ptr, bytes_to_write); /* Cji */
		 lut_ptr++;
		 palette_ptr += bytes_to_write;
	  }
   }

   *p_nb_bytes_written = boxSize;

   return paletteBuf;
}
uint8_t* FileFormatCompress::write_component_mapping(uint32_t* p_nb_bytes_written)
{
   auto palette = getColour()->palette;
   uint32_t boxSize = 4 + 4 + palette->num_channels * 4U;

   uint8_t* cmapBuf = (uint8_t*)grk_malloc(boxSize);
   uint8_t* cmapPtr = cmapBuf;

   /* box size */
   grk_write<uint32_t>(cmapPtr, boxSize);
   cmapPtr += 4;

   /* CMAP */
   grk_write<uint32_t>(cmapPtr, JP2_CMAP);
   cmapPtr += 4;

   for(uint32_t i = 0; i < palette->num_channels; ++i)
   {
	  auto map = palette->component_mapping + i;
	  grk_write<uint16_t>(cmapPtr, map->component_index); /* CMP^i */
	  cmapPtr += 2;
	  grk_write<uint8_t>(cmapPtr++, map->mapping_type); /* MTYP^i */
	  grk_write<uint8_t>(cmapPtr++, map->palette_column); /* PCOL^i */
   }

   *p_nb_bytes_written = boxSize;

   return cmapBuf;
}
uint8_t* FileFormatCompress::write_colr(uint32_t* p_nb_bytes_written)
{
   /* room for 8 bytes for box 3 for common data and variable upon profile*/
   uint32_t colr_size = 11;
   assert(p_nb_bytes_written != nullptr);
   assert(meth == 1 || meth == 2);

   switch(meth)
   {
	  case 1:
		 colr_size += 4; /* EnumCS */
		 break;
	  case 2:
		 assert(getColour()->icc_profile_len); /* ICC profile */
		 colr_size += getColour()->icc_profile_len;
		 break;
	  default:
		 return nullptr;
   }

   auto colr_data = (uint8_t*)grk_calloc(1, colr_size);
   if(!colr_data)
	  return nullptr;

   auto current_colr_ptr = colr_data;

   /* write box size */
   grk_write<uint32_t>(current_colr_ptr, colr_size, 4);
   current_colr_ptr += 4;

   /* BPCC */
   grk_write<uint32_t>(current_colr_ptr, JP2_COLR, 4);
   current_colr_ptr += 4;

   /* METH */
   grk_write<uint8_t>(current_colr_ptr++, meth);
   /* PRECEDENCE */
   grk_write<uint8_t>(current_colr_ptr++, precedence);
   /* APPROX */
   grk_write<uint8_t>(current_colr_ptr++, approx);

   /* Meth value is restricted to 1 or 2 (Table I.9 of part 1) */
   if(meth == 1)
   {
	  /* EnumCS */
	  grk_write<uint32_t>(current_colr_ptr, enumcs, 4);
   }
   else
   {
	  /* ICC profile */
	  if(meth == 2)
	  {
		 memcpy(current_colr_ptr, getColour()->icc_profile_buf, getColour()->icc_profile_len);
		 current_colr_ptr += getColour()->icc_profile_len;
	  }
   }
   *p_nb_bytes_written = colr_size;

   return colr_data;
}
uint8_t* FileFormatCompress::write_channel_definition(uint32_t* p_nb_bytes_written)
{
   /* 8 bytes for box, 2 for n */
   uint32_t cdef_size = 10;
   assert(p_nb_bytes_written != nullptr);
   assert(getColour()->channel_definition != nullptr);
   assert(getColour()->channel_definition->descriptions != nullptr);
   assert(getColour()->channel_definition->num_channel_descriptions > 0U);

   cdef_size += 6U * getColour()->channel_definition->num_channel_descriptions;

   auto cdef_data = (uint8_t*)grk_malloc(cdef_size);
   if(!cdef_data)
	  return nullptr;

   auto current_cdef_ptr = cdef_data;

   /* write box size */
   grk_write<uint32_t>(current_cdef_ptr, cdef_size, 4);
   current_cdef_ptr += 4;

   /* BPCC */
   grk_write<uint32_t>(current_cdef_ptr, JP2_CDEF, 4);
   current_cdef_ptr += 4;

   /* N */
   grk_write<uint16_t>(current_cdef_ptr, getColour()->channel_definition->num_channel_descriptions);
   current_cdef_ptr += 2;

   for(uint16_t i = 0U; i < getColour()->channel_definition->num_channel_descriptions; ++i)
   {
	  /* Cni */
	  grk_write<uint16_t>(current_cdef_ptr,
						  getColour()->channel_definition->descriptions[i].channel);
	  current_cdef_ptr += 2;
	  /* Typi */
	  grk_write<uint16_t>(current_cdef_ptr, getColour()->channel_definition->descriptions[i].typ);
	  current_cdef_ptr += 2;
	  /* Asoci */
	  grk_write<uint16_t>(current_cdef_ptr, getColour()->channel_definition->descriptions[i].asoc);
	  current_cdef_ptr += 2;
   }
   *p_nb_bytes_written = cdef_size;

   return cdef_data;
}
uint8_t* FileFormatCompress::write_bpc(uint32_t* p_nb_bytes_written)
{
   assert(p_nb_bytes_written != nullptr);

   uint32_t i;
   /* room for 8 bytes for box and 1 byte for each component */
   uint32_t bpcc_size = 8U + numcomps;

   auto bpcc_data = (uint8_t*)grk_calloc(1, bpcc_size);
   if(!bpcc_data)
	  return nullptr;

   auto current_bpc_ptr = bpcc_data;

   /* write box size */
   grk_write<uint32_t>(current_bpc_ptr, bpcc_size, 4);
   current_bpc_ptr += 4;

   /* BPCC */
   grk_write<uint32_t>(current_bpc_ptr, JP2_BPCC, 4);
   current_bpc_ptr += 4;

   for(i = 0; i < numcomps; ++i)
	  grk_write(current_bpc_ptr++, comps[i].bpc);
   *p_nb_bytes_written = bpcc_size;

   return bpcc_data;
}
uint8_t* FileFormatCompress::write_res(uint32_t* p_nb_bytes_written)
{
   uint8_t *res_data = nullptr, *current_res_ptr = nullptr;
   assert(p_nb_bytes_written);

   bool storeCapture = capture_resolution[0] > 0 && capture_resolution[1] > 0;

   bool storeDisplay = display_resolution[0] > 0 && display_resolution[1] > 0;

   uint32_t size = (4 + 4) + GRK_RESOLUTION_BOX_SIZE;
   if(storeCapture && storeDisplay)
	  size += GRK_RESOLUTION_BOX_SIZE;

   res_data = (uint8_t*)grk_calloc(1, size);
   if(!res_data)
	  return nullptr;

   current_res_ptr = res_data;

   /* write super-box size */
   grk_write<uint32_t>(current_res_ptr, size, 4);
   current_res_ptr += 4;

   /* Super-box ID */
   grk_write<uint32_t>(current_res_ptr, JP2_RES, 4);
   current_res_ptr += 4;

   if(storeCapture)
	  write_res_box(capture_resolution[0], capture_resolution[1], JP2_CAPTURE_RES,
					&current_res_ptr);
   if(storeDisplay)
	  write_res_box(display_resolution[0], display_resolution[1], JP2_DISPLAY_RES,
					&current_res_ptr);
   *p_nb_bytes_written = size;

   return res_data;
}

// https://shreevatsa.wordpress.com/2011/01/10/not-all-best-rational-approximations-are-the-convergents-of-the-continued-fraction/
void FileFormatCompress::find_cf(double x, uint16_t* num, uint16_t* den)
{
   // number of terms in continued fraction.
   // 15 is the max without precision errors for M_PI
   const size_t MAX_ITER = 15;
   const double eps = 1.0 / USHRT_MAX;
   long p[MAX_ITER], q[MAX_ITER], a[MAX_ITER];

   size_t i;
   // The first two convergents are 0/1 and 1/0
   p[0] = 0;
   q[0] = 1;

   p[1] = 1;
   q[1] = 0;
   // The rest of the convergents (and continued fraction)
   for(i = 2; i < MAX_ITER; ++i)
   {
	  a[i] = lrint(floor(x));
	  p[i] = a[i] * p[i - 1] + p[i - 2];
	  q[i] = a[i] * q[i - 1] + q[i - 2];
	  bool overflow = (p[i] > USHRT_MAX) || (q[i] > USHRT_MAX);
	  if(fabs(x - (double)a[i]) < eps || overflow)
		 break;
	  x = 1.0 / (x - (double)a[i]);
   }

   *num = (uint16_t)(p[i - 1]);
   *den = (uint16_t)(q[i - 1]);
}
void FileFormatCompress::write_res_box(double resx, double resy, uint32_t box_id,
									   uint8_t** current_res_ptr)
{
   /* write box size */
   grk_write<uint32_t>(*current_res_ptr, GRK_RESOLUTION_BOX_SIZE, 4);
   *current_res_ptr += 4;

   /* Box ID */
   grk_write<uint32_t>(*current_res_ptr, box_id, 4);
   *current_res_ptr += 4;

   double res[2];
   // y is written first, then x
   res[0] = resy;
   res[1] = resx;

   uint16_t num[2];
   uint16_t den[2];
   int32_t exponent[2];

   for(size_t i = 0; i < 2; ++i)
   {
	  // special case when res[i] is a whole number.
	  exponent[i] = 0;
	  double r = res[i];
	  while(floor(r) == r)
	  {
		 if(r <= USHRT_MAX)
			break;
		 r /= 10;
		 exponent[i]++;
	  }
	  if(floor(r) == r)
	  {
		 num[i] = (uint16_t)r;
		 den[i] = 1;
		 continue;
	  }
	  //////////////////////////////////////////

	  exponent[i] = (int32_t)log10(res[i]);
	  if(exponent[i] < 1)
		 exponent[i] = 0;
	  if(exponent[i] >= 1)
		 res[i] /= pow(10, exponent[i]);
	  find_cf(res[i], num + i, den + i);
   }
   for(size_t i = 0; i < 2; ++i)
   {
	  grk_write<uint16_t>(*current_res_ptr, num[i]);
	  *current_res_ptr += 2;
	  grk_write<uint16_t>(*current_res_ptr, den[i]);
	  *current_res_ptr += 2;
   }
   for(size_t i = 0; i < 2; ++i)
   {
	  grk_write<uint8_t>(*current_res_ptr, (uint8_t)exponent[i]);
	  *current_res_ptr += 1;
   }
}
uint8_t* FileFormatCompress::write_xml(uint32_t* p_nb_bytes_written)
{
   return write_buffer(JP2_XML, &xml, p_nb_bytes_written);
}
uint8_t* FileFormatCompress::write_buffer(uint32_t boxId, grk_buf8* buffer,
										  uint32_t* p_nb_bytes_written)
{
   assert(p_nb_bytes_written);

   /* need 8 bytes for box plus buffer->len bytes for buffer*/
   uint32_t total_size = 8 + (uint32_t)buffer->len;
   auto data = (uint8_t*)grk_calloc(1, total_size);
   if(!data)
	  return nullptr;

   uint8_t* current_ptr = data;

   /* write box size */
   grk_write<uint32_t>(current_ptr, total_size, 4);
   current_ptr += 4;

   /* write box id */
   grk_write<uint32_t>(current_ptr, boxId, 4);
   current_ptr += 4;

   /* write buffer data */
   memcpy(current_ptr, buffer->buf, buffer->len);

   *p_nb_bytes_written = total_size;

   return data;
}
uint8_t* FileFormatCompress::write_ihdr(uint32_t* p_nb_bytes_written)
{
   assert(p_nb_bytes_written);

   /* default image header is 22 bytes wide */
   auto ihdr_data = (uint8_t*)grk_calloc(1, 22);
   if(ihdr_data == nullptr)
	  return nullptr;

   auto current_ihdr_ptr = ihdr_data;

   /* write box size */
   grk_write<uint32_t>(current_ihdr_ptr, 22, 4);
   current_ihdr_ptr += 4;

   /* IHDR */
   grk_write<uint32_t>(current_ihdr_ptr, JP2_IHDR, 4);
   current_ihdr_ptr += 4;

   /* HEIGHT */
   grk_write<uint32_t>(current_ihdr_ptr, h, 4);
   current_ihdr_ptr += 4;

   /* WIDTH */
   grk_write<uint32_t>(current_ihdr_ptr, w, 4);
   current_ihdr_ptr += 4;

   /* NC */
   grk_write<uint16_t>(current_ihdr_ptr, numcomps);
   current_ihdr_ptr += 2;

   /* BPC */
   grk_write<uint8_t>(current_ihdr_ptr++, bpc);

   /* C : Always 7 */
   grk_write<uint8_t>(current_ihdr_ptr++, C);

   /* UnkC, colorspace unknown */
   grk_write<uint8_t>(current_ihdr_ptr++, UnkC);

   /* IPR, no intellectual property */
   grk_write<uint8_t>(current_ihdr_ptr++, IPR);

   *p_nb_bytes_written = 22;

   return ihdr_data;
}
bool FileFormatCompress::start(void)
{
   /* customization of the validation */
   init_compressValidation();

   /* validation of the parameters codec */
   if(!exec(validation_list_))
	  return false;

   /* customization of the compressing */
   init_header_writing();

   // estimate if codec stream may be larger than 2^32 bytes
   auto p_image = codeStream->getHeaderImage();
   uint64_t image_size = 0;
   for(auto i = 0U; i < p_image->numcomps; ++i)
   {
	  auto comp = p_image->comps + i;
	  image_size += (uint64_t)comp->w * comp->h * ((comp->prec + 7U) / 8);
   }
   needs_xl_jp2c_box_length = (image_size > (uint64_t)1 << 30) ? true : false;

   /* write header */
   if(!exec(procedure_list_))
	  return false;

   return codeStream->start();
}
bool FileFormatCompress::init(grk_cparameters* parameters, GrkImage* image)
{
   uint16_t i;
   uint8_t depth_0;
   uint32_t sign = 0;
   uint32_t alpha_count = 0;
   uint16_t color_channels = 0U;

   if(!parameters || !image)
	  return false;

   inputImage_ = image;
   grk_object_ref(&image->obj);

   cmsSetLogErrorHandler(MycmsLogErrorHandlerFunction);

   if(codeStream->init(parameters, inputImage_) == false)
	  return false;

   /* Profile box */
   brand = parameters->cblk_sty == GRK_CBLKSTY_HT ? JP2_JPH : JP2_JP2; /* BR */
   minversion = 0; /* MinV */
   numcl = 1;
   cl = (uint32_t*)grk_malloc(sizeof(uint32_t) * numcl);
   if(!cl)
   {
	  Logger::logger_.error("Not enough memory when set up the JP2 compressor");
	  return false;
   }
   cl[0] = brand;

   /* Image Header box */
   numcomps = inputImage_->numcomps; /* NC */
   comps = new ComponentInfo[numcomps];

   h = inputImage_->y1 - inputImage_->y0;
   w = inputImage_->x1 - inputImage_->x0;
   depth_0 = (uint8_t)(inputImage_->comps[0].prec - 1);
   sign = inputImage_->comps[0].sgnd;
   bpc = (uint8_t)(depth_0 + (sign << 7));
   for(i = 1; i < inputImage_->numcomps; i++)
   {
	  uint32_t depth = inputImage_->comps[i].prec - 1U;
	  sign = inputImage_->comps[i].sgnd;
	  if(depth_0 != depth)
		 bpc = 0xFF;
   }
   C = 7; /* C : Always 7 */
   UnkC = 0; /* UnkC, colorspace specified in colr box */
   IPR = 0; /* IPR, no intellectual property */

   /* bit per component box */
   for(i = 0; i < inputImage_->numcomps; i++)
   {
	  comps[i].bpc = (uint8_t)(inputImage_->comps[i].prec - 1);
	  if(inputImage_->comps[i].sgnd)
		 comps[i].bpc = (uint8_t)(comps[i].bpc + (1 << 7));
   }

   inputImage_->validateICC();

   /* Colour Specification box */
   if(inputImage_->color_space == GRK_CLRSPC_ICC)
   {
	  meth = 2;
	  enumcs = GRK_ENUM_CLRSPC_UNKNOWN;
   }
   else
   {
	  meth = 1;
	  if(inputImage_->color_space == GRK_CLRSPC_CMYK)
		 enumcs = GRK_ENUM_CLRSPC_CMYK;
	  else if(inputImage_->color_space == GRK_CLRSPC_DEFAULT_CIE)
		 enumcs = GRK_ENUM_CLRSPC_CIE;
	  else if(inputImage_->color_space == GRK_CLRSPC_SRGB)
		 enumcs = GRK_ENUM_CLRSPC_SRGB; /* sRGB as defined by IEC 61966-2-1 */
	  else if(inputImage_->color_space == GRK_CLRSPC_GRAY)
		 enumcs = GRK_ENUM_CLRSPC_GRAY; /* greyscale */
	  else if(inputImage_->color_space == GRK_CLRSPC_SYCC)
		 enumcs = GRK_ENUM_CLRSPC_SYCC; /* YUV */
	  else if(inputImage_->color_space == GRK_CLRSPC_EYCC)
		 enumcs = GRK_ENUM_CLRSPC_EYCC; /* YUV */
	  else
	  {
		 Logger::logger_.error("Unsupported colour space enumeration %u", inputImage_->color_space);
		 return false;
	  }
   }

   // transfer buffer to uuid
   if(inputImage_->meta)
   {
	  if(inputImage_->meta->iptc_len && inputImage_->meta->iptc_buf)
		 uuids[numUuids++] =
			 UUIDBox(IPTC_UUID, inputImage_->meta->iptc_buf, inputImage_->meta->iptc_len);

	  if(inputImage_->meta->xmp_len && inputImage_->meta->xmp_buf)
		 uuids[numUuids++] =
			 UUIDBox(XMP_UUID, inputImage_->meta->xmp_buf, inputImage_->meta->xmp_len);
   }
   /* Channel Definition box */
   for(i = 0; i < inputImage_->numcomps; i++)
   {
	  if(inputImage_->comps[i].type != GRK_CHANNEL_TYPE_COLOUR)
	  {
		 alpha_count++;
		 // technically, this is an error, but we will let it pass
		 if(inputImage_->comps[i].sgnd)
			Logger::logger_.warn("signed alpha channel %u", i);
	  }
   }
   switch(enumcs)
   {
	  case GRK_ENUM_CLRSPC_CMYK:
		 color_channels = 4;
		 break;
	  case GRK_ENUM_CLRSPC_CIE:
	  case GRK_ENUM_CLRSPC_SRGB:
	  case GRK_ENUM_CLRSPC_SYCC:
	  case GRK_ENUM_CLRSPC_EYCC:
		 color_channels = 3;
		 break;
	  case GRK_ENUM_CLRSPC_GRAY:
		 color_channels = 1;
		 break;
	  default:
		 break;
   }
   if(alpha_count)
   {
	  if(!inputImage_->meta)
		 inputImage_->meta = grk_image_meta_new();
	  getColour()->channel_definition = new grk_channel_definition();
	  getColour()->channel_definition->descriptions =
		  new grk_channel_description[inputImage_->numcomps];
	  getColour()->channel_definition->num_channel_descriptions = inputImage_->numcomps;
	  for(i = 0U; i < color_channels; i++)
	  {
		 getColour()->channel_definition->descriptions[i].channel = i;
		 getColour()->channel_definition->descriptions[i].typ = GRK_CHANNEL_TYPE_COLOUR;
		 getColour()->channel_definition->descriptions[i].asoc = (uint16_t)(i + 1U);
	  }
	  for(; i < inputImage_->numcomps; i++)
	  {
		 getColour()->channel_definition->descriptions[i].channel = i;
		 getColour()->channel_definition->descriptions[i].typ = inputImage_->comps[i].type;
		 getColour()->channel_definition->descriptions[i].asoc = inputImage_->comps[i].association;
	  }
   }
   precedence = 0; /* PRECEDENCE */
   approx = 0; /* APPROX */
   has_capture_resolution =
	   parameters->write_capture_resolution || parameters->write_capture_resolution_from_file;
   if(parameters->write_capture_resolution)
   {
	  for(i = 0; i < 2; ++i)
		 capture_resolution[i] = parameters->capture_resolution[i];
   }
   else if(parameters->write_capture_resolution_from_file)
   {
	  for(i = 0; i < 2; ++i)
		 capture_resolution[i] = parameters->capture_resolution_from_file[i];
   }
   if(parameters->write_display_resolution)
   {
	  has_display_resolution = true;
	  display_resolution[0] = parameters->display_resolution[0];
	  display_resolution[1] = parameters->display_resolution[1];
	  // if display resolution equals (0,0), then use capture resolution
	  // if available
	  if(parameters->display_resolution[0] == 0 && parameters->display_resolution[1] == 0)
	  {
		 if(has_capture_resolution)
		 {
			display_resolution[0] = parameters->capture_resolution[0];
			display_resolution[1] = parameters->capture_resolution[1];
		 }
		 else
		 {
			has_display_resolution = false;
		 }
	  }
   }

   return true;
}
uint64_t FileFormatCompress::compress(grk_plugin_tile* tile)
{
   auto rc = codeStream->compress(tile);
   if(rc && !end())
	  return 0;

   return rc;
}
bool FileFormatCompress::end(void)
{
   /* write header */
   init_end_header_writing();

   return exec(procedure_list_);
}
void FileFormatCompress::init_end_header_writing(void)
{
   procedure_list_->push_back(std::bind(&FileFormatCompress::write_jp2c, this));
}
void FileFormatCompress::init_compressValidation(void)
{
   validation_list_->push_back(std::bind(&FileFormatCompress::default_validation, this));
}
void FileFormatCompress::init_header_writing(void)
{
   procedure_list_->push_back(std::bind(&FileFormatCompress::write_jp, this));
   procedure_list_->push_back(std::bind(&FileFormatCompress::write_ftyp, this));
   procedure_list_->push_back(std::bind(&FileFormatCompress::write_jp2h, this));
   procedure_list_->push_back(std::bind(&FileFormatCompress::write_uuids, this));
   procedure_list_->push_back(std::bind(&FileFormatCompress::skip_jp2c, this));
}
bool FileFormatCompress::skip_jp2c(void)
{
   auto stream = codeStream->getStream();
   assert(stream != nullptr);

   j2k_codestream_offset = stream->tell();
   int64_t skip_bytes = needs_xl_jp2c_box_length ? 16 : 8;

   return stream->skip(skip_bytes);
}
bool FileFormatCompress::default_validation(void)
{
   bool valid = true;
   uint32_t i;
   auto stream = codeStream->getStream();
   assert(stream != nullptr);

   /* JPEG2000 codec validation */

   /* POINTER validation */
   /* make sure a j2k codec is present */
   valid &= (codeStream != nullptr);

   /* make sure a procedure list is present */
   valid &= (procedure_list_ != nullptr);

   /* make sure a validation list is present */
   valid &= (validation_list_ != nullptr);

   /* PARAMETER VALIDATION */
   /* number of components */
   /* precision */
   for(i = 0; i < numcomps; ++i)
	  valid &= ((comps[i].bpc & 0x7FU) < maxPrecisionJ2K); /* 0 is valid, ignore sign for check */

   /* METH */
   valid &= ((meth > 0) && (meth < 3));

   valid &= stream->hasSeek();

   return valid;
}
} // namespace grk
