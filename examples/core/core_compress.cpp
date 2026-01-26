/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

/*********************************************************************

Example demonstrating compression from a memory buffer using one
of three approaches for the destination:

1) memory buffer
2) memory buffer using callbacks
3) file

**********************************************************************/

#include <cstdio>
#include <cstring>
#include <memory>
#include <inttypes.h>

#include "grok.h"
#include "core.h"

/**
 * @struct WriteCallbackInfo
 * @brief used by write callbacks to get information about stream
 *
 */
struct WriteCallbackInfo
{
  explicit WriteCallbackInfo(grk_stream_params* streamParams)
      : streamParams_(streamParams), data_(nullptr), dataLen_(0), offset_(0)
  {}
  grk_stream_params* streamParams_;
  uint8_t* data_;
  size_t dataLen_;
  size_t offset_;
};

/**
 * @brief stream write callback
 *
 * @param buffer //buffer of data to write
 * @param numBytes  // number of bytes to write
 * @param user_data  // user data
 * @return size_t // number of bytes written
 */
size_t stream_write_fn(const uint8_t* buffer, size_t numBytes, void* user_data)
{
  auto sinfo = (WriteCallbackInfo*)user_data;
  if(sinfo->offset_ + numBytes <= sinfo->dataLen_)
    memcpy(sinfo->data_ + sinfo->offset_, buffer, numBytes);

  return numBytes;
}

/**
 * @brief stream seek callback
 *
 * @param offset //offset to seek to
 * @param user_data  // user dat
 * @return true if seek successful, otherwise false
 */
bool stream_seek_fn(uint64_t offset, void* user_data)
{
  auto sinfo = (WriteCallbackInfo*)user_data;
  if(offset <= sinfo->dataLen_)
    sinfo->offset_ = offset;
  else
    sinfo->offset_ = sinfo->dataLen_;

  return true;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
  const uint32_t dimX = 640;
  const uint32_t dimY = 480;
  const uint32_t numComps = 3;
  const uint32_t precision = 8;
  const char* outFile = "test.jp2";

  grk_object* codec = nullptr;
  grk_image* inputImage = nullptr;
  int32_t rc = EXIT_FAILURE;

  uint64_t compressedLength = 0;

  // 1. initialize compress parameters
  grk_cparameters compressParams;
  grk_compress_set_default_params(&compressParams);
  compressParams.cod_format = GRK_FMT_JP2;

  // 2.initialize output stream
  enum eStreamOutput
  {
    STREAM_OUTPUT_BUFFER, // output to buffer
    STREAM_OUTPUT_CALLBACK, // output using user-defined callback
    STREAM_OUTPUT_FILE // output to file
  };
  eStreamOutput output = STREAM_OUTPUT_BUFFER;

  grk_stream_params outputStreamParams = {};
  WriteCallbackInfo writeCallbackInfo(&outputStreamParams);

  std::unique_ptr<uint8_t[]> data;
  size_t bufLen = (size_t)numComps * ((precision + 7) / 8) * dimX * dimY;
  if(output != STREAM_OUTPUT_FILE)
  {
    data = std::make_unique<uint8_t[]>(bufLen);
  }
  if(output == STREAM_OUTPUT_CALLBACK)
  {
    outputStreamParams.seek_fn = stream_seek_fn;
    outputStreamParams.write_fn = stream_write_fn;
    outputStreamParams.user_data = &writeCallbackInfo;
    writeCallbackInfo.data_ = data.get();
    writeCallbackInfo.dataLen_ = bufLen;
  }
  else if(output == STREAM_OUTPUT_BUFFER)
  {
    outputStreamParams.buf = data.get();
    outputStreamParams.buf_len = bufLen;
  }
  else
  {
    safe_strcpy(outputStreamParams.file, outFile);
  }

  // 3. create input image (blank)
  auto components = std::make_unique<grk_image_comp[]>(numComps);
  for(uint32_t i = 0; i < numComps; ++i)
  {
    auto c = &components[i];
    c->w = dimX;
    c->h = dimY;
    c->dx = 1;
    c->dy = 1;
    c->prec = precision;
    c->sgnd = false;
  }
  inputImage = grk_image_new(numComps, components.get(), GRK_CLRSPC_SRGB, true);

  // fill in component data: see grok.h header for full details of image structure
  for(uint16_t compno = 0; compno < inputImage->numcomps; ++compno)
  {
    auto comp = inputImage->comps + compno;
    auto compWidth = comp->w;
    auto compHeight = comp->h;
    auto compData = (int32_t*)comp->data;
    if(!compData)
    {
      fprintf(stderr, "Image has null data for component %d\n", compno);
      goto beach;
    }
    // fill in component data, taking component stride into account
    // in this example, we just zero out each component
    auto srcData = new int32_t[(size_t)compWidth * compHeight];
    memset(srcData, 0, (size_t)compWidth * compHeight * sizeof(int32_t));
    auto srcPtr = srcData;
    for(uint32_t j = 0; j < compHeight; ++j)
    {
      memcpy(compData, srcPtr, (size_t)compWidth * sizeof(int32_t));
      srcPtr += compWidth;
      compData += comp->stride;
    }
    delete[] srcData;
  }

  // 4. initialize compressor
  codec = grk_compress_init(&outputStreamParams, &compressParams, inputImage);
  if(!codec)
  {
    fprintf(stderr, "Failed to initialize compressor\n");
    goto beach;
  }

  // 5. compress
  compressedLength = grk_compress(codec, nullptr);
  if(compressedLength == 0)
  {
    fprintf(stderr, "Failed to compress\n");
    goto beach;
  }
  printf("Compression succeeded: %" PRIu64 " bytes used.\n", compressedLength);

  // 6. write buffer to file if needed
  if(output == STREAM_OUTPUT_FILE)
  {
    auto fp = fopen(outFile, "wb");
    if(!fp)
    {
      fprintf(stderr, "Buffer compress: failed to open file %s for writing", outFile);
    }
    else
    {
      size_t written = fwrite(outputStreamParams.buf, 1, compressedLength, fp);
      if(written != compressedLength)
      {
        fprintf(stderr, "Buffer compress: only %" PRIu64 " bytes written out of %zu total",
                compressedLength, written);
      }
      fclose(fp);
    }
  }

  rc = EXIT_SUCCESS;
beach:

  // cleanup
  grk_object_unref(codec);
  grk_object_unref(&inputImage->obj);

  return rc;
}
