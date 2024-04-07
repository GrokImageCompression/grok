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

/**
 Load a single image component encoded in PGX file format
 @param filename Name of the PGX file to load
 @param parameters *List ?*
 @return a greyscale image if successful, returns nullptr otherwise
 */
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include "grk_apps_config.h"
#include "grok.h"
#include "spdlog/spdlog.h"
#include "PGXFormat.h"
#include "convert.h"
#include <cstring>
#include <cassert>

template<typename T>
T readchar(FILE* f)
{
   T c1;
   if(!fread(&c1, 1, 1, f))
   {
	  spdlog::error(" fread return a number of element different from the expected.");
	  return 0;
   }
   return c1;
}

template<typename T>
T readshort(FILE* f, bool bigendian)
{
   uint8_t c1, c2;
   if(!fread(&c1, 1, 1, f))
   {
	  spdlog::error("  fread return a number of element different from the expected.");
	  return 0;
   }
   if(!fread(&c2, 1, 1, f))
   {
	  spdlog::error("  fread return a number of element different from the expected.");
	  return 0;
   }
   if(bigendian)
	  return (T)((c1 << 8) + c2);
   else
	  return (T)((c2 << 8) + c1);
}

static grk_image* pgxtoimage(const char* filename, grk_cparameters* parameters)
{
   uint32_t w, stride_diff, h;
   uint16_t numcomps = 1;
   uint32_t prec;
   uint64_t i = 0, index = 0;
   GRK_COLOR_SPACE color_space = GRK_CLRSPC_GRAY;
   grk_image* image = nullptr;
   int c;
   char endian1, endian2;
   char signtmp[32];
   bool sign = false;
   char temp[32];
   bool bigendian;
   grk_image_comp cmptparm; /* maximum of 1 component  */
   uint8_t shift = 0;

   memset(&cmptparm, 0, sizeof(grk_image_comp));
   auto f = fopen(filename, "rb");
   if(!f)
   {
	  spdlog::error("Failed to open {} for reading.", filename);
	  return nullptr;
   }
   if(fscanf(f, "PG%31[ \t]%c%c%31[ \t+-]%u%31[ \t]%u%31[ \t]%u", temp, &endian1, &endian2, signtmp,
			 &prec, temp, &w, temp, &h) != 9)
   {
	  spdlog::error(" Failed to read the right number of element from the fscanf() function.");
	  goto cleanup;
   }

   if(prec < 4)
   {
	  spdlog::error("Precision must be >= 4");
	  goto cleanup;
   }
   while(signtmp[i] != '\0')
   {
	  if(signtmp[i] == '-')
	  {
		 sign = true;
		 break;
	  }
	  i++;
   }

   c = fgetc(f);
   if(c == EOF)
	  goto cleanup;
   if(endian1 == 'M' && endian2 == 'L')
   {
	  bigendian = true;
   }
   else if(endian2 == 'M' && endian1 == 'L')
   {
	  bigendian = false;
   }
   else
   {
	  spdlog::error("Bad pgx header, please check input file");
	  goto cleanup;
   }
   cmptparm.x0 = parameters->image_offset_x0;
   cmptparm.y0 = parameters->image_offset_y0;
   cmptparm.w = !cmptparm.x0 ? ((w - 1) * parameters->subsampling_dx + 1)
							 : cmptparm.x0 + (uint32_t)(w - 1) * parameters->subsampling_dx + 1;
   cmptparm.h = !cmptparm.y0 ? ((h - 1) * parameters->subsampling_dy + 1)
							 : cmptparm.y0 + (uint32_t)(h - 1) * parameters->subsampling_dy + 1;
   cmptparm.sgnd = sign;
   cmptparm.prec = (uint8_t)prec;
   cmptparm.dx = parameters->subsampling_dx;
   cmptparm.dy = parameters->subsampling_dy;

   /* create the image */
   image = grk_image_new(numcomps, &cmptparm, color_space, true);
   if(!image)
	  goto cleanup;

   /* set image offset and reference grid */
   image->x0 = cmptparm.x0;
   image->y0 = cmptparm.x0;
   image->x1 = cmptparm.w;
   image->y1 = cmptparm.h;

   /* set image data */
   stride_diff = image->comps->stride - w;
   shift = (uint8_t)(32 - prec);
   for(uint32_t j = 0; j < h; ++j)
   {
	  for(uint32_t k = 0; k < w; ++k)
	  {
		 int32_t v = 0;
		 if(prec < 8)
		 {
			v = sign ? sign_extend((int32_t)readchar<int8_t>(f), shift) : readchar<int8_t>(f);
		 }
		 else
		 {
			if(image->comps->prec == 8)
			{
			   if(!image->comps->sgnd)
				  v = (int32_t)readchar<uint8_t>(f);
			   else
				  v = (int32_t)readchar<int8_t>(f);
			}
			else
			{
			   if(!image->comps->sgnd)
				  v = (int32_t)readshort<uint16_t>(f, bigendian);
			   else
				  v = (int32_t)readshort<int16_t>(f, bigendian);
			}
		 }
		 image->comps->data[index++] = v;
	  }
	  index += stride_diff;
   }
cleanup:
   if(!grk::safe_fclose(f))
   {
	  grk_object_unref(&image->obj);
	  image = nullptr;
   }

   return image;
}

bool PGXFormat::encodeHeader(void)
{
   if(!allComponentsSanityCheck(image_, false))
   {
	  spdlog::error("PGXFormat::encodeHeader: image sanity check failed.");
	  return false;
   }

   encodeState = IMAGE_FORMAT_ENCODED_HEADER;
   return true;
}
bool PGXFormat::encodePixels(void)
{
   bool success = false;
   for(uint16_t compno = 0; compno < image_->numcomps; compno++)
   {
	  auto comp = &image_->comps[compno];
	  int nbytes = 0;
	  if(fileName_.size() < 4)
	  {
		 spdlog::error(" imagetopgx: output file name size less than 4.");
		 goto beach;
	  }
	  auto pos = fileName_.rfind(".");
	  if(pos == std::string::npos)
	  {
		 spdlog::error(" pgx was recognized but there was no dot at expected position .");
		 goto beach;
	  }
	  std::string fileOut =
		  fileName_.substr(0, pos) + std::string("_") + std::to_string(compno) + ".pgx";
	  fileStream_ = fopen(fileOut.c_str(), "wb");
	  if(!fileStream_)
	  {
		 spdlog::error("failed to open {} for writing", fileOut);
		 goto beach;
	  }

	  uint32_t w = comp->w;
	  uint32_t h = comp->h;

	  fprintf(fileStream_, "PG ML %c %u %u %u\n", comp->sgnd ? '-' : '+', comp->prec, w, h);

	  if(comp->prec <= 8)
		 nbytes = 1;
	  else if(comp->prec <= 16)
		 nbytes = 2;

	  const size_t bufSize = 4096;
	  size_t outCount = 0;
	  size_t index = 0;
	  uint32_t stride_diff = comp->stride - w;
	  if(nbytes == 1)
	  {
		 uint8_t buf[bufSize];
		 uint8_t* outPtr = buf;
		 for(uint32_t j = 0; j < h; ++j)
		 {
			for(uint32_t i = 0; i < w; ++i)
			{
			   const int val = comp->data[index++];
			   if(!grk::writeBytes<uint8_t>((uint8_t)val, buf, &outPtr, &outCount, bufSize, true,
											fileStream_))
			   {
				  spdlog::error("failed to write bytes for {}", fileOut);
				  goto beach;
			   }
			}
			index += stride_diff;
		 }
		 if(outCount)
		 {
			size_t res = fwrite(buf, sizeof(uint8_t), outCount, fileStream_);
			if(res != outCount)
			{
			   spdlog::error("failed to write bytes for {}", fileOut);
			   goto beach;
			}
		 }
	  }
	  else
	  {
		 uint16_t buf[bufSize];
		 uint16_t* outPtr = buf;
		 for(uint32_t j = 0; j < h; ++j)
		 {
			for(uint32_t i = 0; i < w; ++i)
			{
			   const int val = image_->comps[compno].data[index++];
			   if(!grk::writeBytes<uint16_t>((uint16_t)val, buf, &outPtr, &outCount, bufSize, true,
											 fileStream_))
			   {
				  spdlog::error("failed to write bytes for {}", fileOut);
				  goto beach;
			   }
			}
			index += stride_diff;
		 }
		 if(outCount)
		 {
			size_t res = fwrite(buf, sizeof(uint16_t), outCount, fileStream_);
			if(res != outCount)
			{
			   spdlog::error("failed to write bytes for {}", fileOut);
			   goto beach;
			}
		 }
	  }
	  if(!grk::safe_fclose(fileStream_))
	  {
		 fileStream_ = nullptr;
		 goto beach;
	  }
	  fileStream_ = nullptr;
   }
   success = true;
beach:
   return success;
}
bool PGXFormat::encodeFinish(void)
{
   bool success = true;

   if(!grk::safe_fclose(fileStream_))
   {
	  success = false;
   }
   fileStream_ = nullptr;

   return success;
}

grk_image* PGXFormat::decode(const std::string& filename, grk_cparameters* parameters)
{
   return pgxtoimage(filename.c_str(), parameters);
}
