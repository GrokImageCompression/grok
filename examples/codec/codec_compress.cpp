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

#include <string>
#include <cstring>
#include <filesystem>
#include <inttypes.h>

#include "grok_codec.h"
#include "grk_examples_config.h"
#include "arg_converter.h"

const std::string dataRoot = GRK_DATA_ROOT;

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
  auto cvt = std::make_unique<ArgConverter>("codec_decompress_from_memory");
  cvt->push("-v");

  int rc = 1;
  // perform two identical compressions
  for(uint32_t i = 0; i < 2; ++i)
  {
    const uint32_t dimX = 640;
    const uint32_t dimY = 480;
    const uint16_t numComps = 3;
    const uint8_t precision = 8;
    grk_image* image = nullptr;
    grk_stream_params streamParams = {};
    bool inputFromImage = true;
    bool outputToBuffer = true;
    if(outputToBuffer)
    {
      streamParams.buf_len = (size_t)numComps * (precision / 8) * dimX * dimY;
      streamParams.buf = new uint8_t[streamParams.buf_len];
    }
    std::vector<std::string> argString;
    std::vector<char*> args;
    std::string inputFile, outputFile;

    if(inputFromImage)
    {
      // create blank image
      auto compParams = std::make_unique<grk_image_comp[]>(numComps);
      for(uint16_t compno = 0; compno < numComps; ++compno)
      {
        auto c = &compParams[compno];
        memset(c, 0, sizeof(grk_image_comp));
        c->w = dimX;
        c->h = dimY;
        c->dx = 1;
        c->dy = 1;
        c->prec = precision;
        c->sgnd = false;
      }
      image = grk_image_new(numComps, compParams.get(), GRK_CLRSPC_SRGB, true);

      // fill in component data
      // see grok.h header for full details of image structure
      for(uint16_t compno = 0; compno < image->numcomps; ++compno)
      {
        auto comp = image->comps + compno;
        auto compWidth = comp->w;
        auto compHeight = comp->h;
        auto compData = (int32_t*)comp->data;
        if(!compData)
        {
          fprintf(stderr, "Image has null data for component %d\n", compno);
          goto beach;
        }
        // fill in component data, taking component stride into account
        // Note: in this example, we just fill the buffer with a constant value whose precision
        // matches the precision specified above.
        // !! do not pass in data whose precision exceeds the precision specified above
        auto srcData = new int32_t[compWidth * compHeight];
        for(uint32_t k = 0; k < compWidth * compHeight; ++k)
          srcData[k] = 0xFF;
        auto srcPtr = srcData;
        for(uint32_t j = 0; j < compHeight; ++j)
        {
          memcpy(compData, srcPtr, (size_t)compWidth * sizeof(int32_t));
          srcPtr += compWidth;
          compData += comp->stride;
        }
        delete[] srcData;
      }
    }

    // a file can be passed in as a command line argument
    // example:
    // $ codec_compress foo.tif
    // otherwise a file from the Grok test suite, specified below, will be used.

    inputFile = dataRoot + std::filesystem::path::preferred_separator + "input" +
                std::filesystem::path::preferred_separator + "nonregression" +
                std::filesystem::path::preferred_separator + "basn6a08.tif";
    outputFile = "basn6a08.jp2";
    if(argc > 1)
    {
      inputFile = argv[1];
      outputFile = inputFile + ".tif";
    }
    if(!inputFromImage)
      cvt->push("-i", inputFile);

    if(outputToBuffer)
    {
      cvt->push("--out-fmt", "jp2");
    }
    else
    {
      cvt->push("-o", outputFile);
    }
    // 3. decompress
    rc = grk_codec_compress(cvt->argc(), cvt->argv(), image,
                            outputToBuffer ? &streamParams : nullptr);
    if(rc)
      fprintf(stderr, "Failed to compress\n");

    if(outputToBuffer)
    {
      printf("Compressed to memory : %lu bytes\n", streamParams.buf_compressed_len);
      auto fp = fopen(outputFile.c_str(), "wb");
      if(!fp)
      {
        fprintf(stderr, "Buffer compress: failed to open file %s for writing", outputFile.c_str());
      }
      else
      {
        size_t written = fwrite(streamParams.buf, 1, streamParams.buf_compressed_len, fp);
        if(written != streamParams.buf_compressed_len)
        {
          fprintf(stderr, "Buffer compress: only %" PRIu64 " bytes written out of %lu total",
                  streamParams.buf_compressed_len, written);
        }
        fclose(fp);
      }
    }

  beach:
    // 4. cleanup
    delete[] streamParams.buf;
    grk_object_unref(&image->obj);
  }

  return rc;
}
