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

#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"

namespace
{
const uint32_t IMAGE_WIDTH = 37;
const uint32_t IMAGE_HEIGHT = 29;
const uint8_t NUM_RESOLUTIONS = 3;
const uint8_t PRECISION = 8;

struct ForceRgbCase
{
  const char* name;
  uint16_t numComps;
  uint8_t chromaSubsampling;
  GRK_COLOR_SPACE headerColourSpace;
  uint16_t expectedNumComps;
};

int32_t sourceSample(uint16_t componentNumber, uint32_t x, uint32_t y)
{
  return (int32_t)((x * 7 + y * 13 + ((x * y) % 29) * 3 + componentNumber * 61) & 0xFF);
}

int32_t sampleAt(const grk_image_comp& comp, uint32_t x, uint32_t y)
{
  uint64_t index = (uint64_t)y * comp.stride + x;
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

uint32_t subsampledLength(uint32_t length, uint8_t subsampling)
{
  return (length + subsampling - 1) / subsampling;
}

grk_image* makeImage(const ForceRgbCase& testCase)
{
  grk_image_comp params[3] = {};
  for(uint16_t c = 0; c < testCase.numComps; ++c)
  {
    uint8_t subsampling = c == 0 ? 1 : testCase.chromaSubsampling;
    params[c].dx = subsampling;
    params[c].dy = subsampling;
    params[c].w = subsampledLength(IMAGE_WIDTH, subsampling);
    params[c].h = subsampledLength(IMAGE_HEIGHT, subsampling);
    params[c].prec = PRECISION;
    params[c].sgnd = false;
  }
  GRK_COLOR_SPACE colourSpace = testCase.numComps == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY;
  grk_image* image = grk_image_new(testCase.numComps, params, colourSpace, true);
  if(!image)
    return nullptr;
  for(uint16_t c = 0; c < testCase.numComps; ++c)
  {
    const auto& comp = image->comps[c];
    auto* data = static_cast<int32_t*>(comp.data);
    if(!data)
    {
      grk_object_unref(&image->obj);
      return nullptr;
    }
    for(uint32_t y = 0; y < comp.h; ++y)
      for(uint32_t x = 0; x < comp.w; ++x)
        data[(size_t)y * comp.stride + x] = sourceSample(c, x, y);
  }
  return image;
}

bool compress(const ForceRgbCase& testCase, const std::string& path)
{
  grk_image* image = makeImage(testCase);
  if(!image)
  {
    fprintf(stderr, "%s: could not build the source image\n", testCase.name);
    return false;
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = false;
  parameters.numresolution = NUM_RESOLUTIONS;
  parameters.mct = 0;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "%s: grk_compress_init failed\n", testCase.name);
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "%s: grk_compress failed\n", testCase.name);
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

// the format writers size their output from the decompress_ fields
bool headerMatchesPlanes(const ForceRgbCase& testCase, const grk_image* image)
{
  bool ok = true;
  if(image->numcomps != image->decompress_num_comps)
  {
    fprintf(stderr, "%s: image has %u components, header advertises %u\n", testCase.name,
            image->numcomps, image->decompress_num_comps);
    ok = false;
  }
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    const auto& comp = image->comps[c];
    if(comp.w != image->decompress_width || comp.h != image->decompress_height)
    {
      fprintf(stderr, "%s: component %u is %ux%u, header advertises %ux%u\n", testCase.name, c,
              comp.w, comp.h, image->decompress_width, image->decompress_height);
      ok = false;
    }
  }
  return ok;
}

bool planeEqualsSource(const ForceRgbCase& testCase, const grk_image* image, uint16_t planeNumber,
                       uint16_t sourceComponentNumber, uint8_t subsampling)
{
  const auto& plane = image->comps[planeNumber];
  if(!plane.data || plane.dx != 1 || plane.dy != 1 || plane.w != image->x1 - image->x0 ||
     plane.h != image->y1 - image->y0)
  {
    fprintf(stderr, "%s: component %u is %ux%u with dx %u dy %u, image is %ux%u\n", testCase.name,
            planeNumber, plane.w, plane.h, plane.dx, plane.dy, image->x1 - image->x0,
            image->y1 - image->y0);
    return false;
  }
  // image origin is zero, so upsampling repeats each source sample over a subsampling block
  for(uint32_t y = 0; y < plane.h; ++y)
  {
    for(uint32_t x = 0; x < plane.w; ++x)
    {
      int32_t got = sampleAt(plane, x, y);
      int32_t expected = sourceSample(sourceComponentNumber, x / subsampling, y / subsampling);
      if(got != expected)
      {
        fprintf(stderr, "%s: component %u sample (%u,%u) is %d, source component %u gives %d\n",
                testCase.name, planeNumber, x, y, got, sourceComponentNumber, expected);
        return false;
      }
    }
  }
  return true;
}

bool planesMatchSource(const ForceRgbCase& testCase, const grk_image* image)
{
  if(image->numcomps != testCase.expectedNumComps)
  {
    fprintf(stderr, "%s: image has %u components, expected %u\n", testCase.name, image->numcomps,
            testCase.expectedNumComps);
    return false;
  }
  bool ok = true;
  if(testCase.numComps == 3)
  {
    for(uint16_t c = 0; c < 3; ++c)
      ok &= planeEqualsSource(testCase, image, c, c, c == 0 ? 1 : testCase.chromaSubsampling);
    return ok;
  }
  for(uint16_t c = 0; c < 3; ++c)
    ok &= planeEqualsSource(testCase, image, c, 0, 1);
  if(testCase.numComps == 2)
    ok &= planeEqualsSource(testCase, image, 3, 1, 1);
  return ok;
}

bool checkCase(const ForceRgbCase& testCase)
{
  std::string path = "force_rgb_geometry_test.j2k";
  if(!compress(testCase, path))
    return false;

  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "%s: grk_decompress_init failed\n", testCase.name);
    remove(path.c_str());
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  headerInfo.color_space = testCase.headerColourSpace;
  headerInfo.force_rgb = true;
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "%s: grk_decompress_read_header failed\n", testCase.name);
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "%s: grk_decompress failed\n", testCase.name);
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "%s: grk_decompress_get_image returned null\n", testCase.name);
    else
    {
      bool headerOk = headerMatchesPlanes(testCase, image);
      bool planesOk = planesMatchSource(testCase, image);
      ok = headerOk && planesOk;
    }
  }
  grk_object_unref(codec);
  remove(path.c_str());
  return ok;
}
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
  grk_initialize(nullptr, 0, nullptr);

  const ForceRgbCase cases[] = {
      {"one component, unknown colour space", 1, 1, GRK_CLRSPC_UNKNOWN, 3},
      {"two components", 2, 1, GRK_CLRSPC_UNKNOWN, 4},
      {"subsampled srgb", 3, 2, GRK_CLRSPC_SRGB, 3},
  };
  uint32_t failures = 0;
  for(const auto& testCase : cases)
  {
    if(!checkCase(testCase))
      ++failures;
  }

  grk_deinitialize();

  if(failures)
  {
    fprintf(stderr, "%u force_rgb cases deliver planes that differ from the header\n", failures);
    return 1;
  }
  printf("force_rgb output matches the advertised header in every case\n");
  return 0;
}
