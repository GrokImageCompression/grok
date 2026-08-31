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

// a region decode of a reversible 5/3 image at 12 bits or less returns int16 samples,
// through the partial wavelet and the dc level shift fused into its final read. the
// samples have to match the same rectangle cropped out of a whole image decode.
//
// mercury is turned off so the decode runs the classic partial wavelet, not
// mercury's own transform.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t IMAGE_WIDTH = 197;
const uint32_t IMAGE_HEIGHT = 143;
const uint8_t PRECISION = 12;

struct Case
{
  bool sgnd;
  bool multipleTiles;
};

struct Plane
{
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<int32_t> samples;
};

const char* describe(const Case& testCase, std::string& storage)
{
  storage = std::string(testCase.sgnd ? "signed" : "unsigned") + " " +
            (testCase.multipleTiles ? "tiled" : "single tile");
  return storage.c_str();
}

int32_t rangeMin(const Case& testCase)
{
  return testCase.sgnd ? -(1 << (PRECISION - 1)) : 0;
}

int32_t rangeMax(const Case& testCase)
{
  return testCase.sgnd ? (1 << (PRECISION - 1)) - 1 : (1 << PRECISION) - 1;
}

// high frequency content in both directions so a dropped or misaligned lifting step,
// or a shift applied to the wrong rows, shows up as a sample difference
grk_image* makeImage(const Case& testCase)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_WIDTH;
  params.h = IMAGE_HEIGHT;
  params.prec = PRECISION;
  params.sgnd = testCase.sgnd;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image || !image->comps[0].data)
  {
    if(image)
      grk_object_unref(&image->obj);
    return nullptr;
  }
  auto* data = static_cast<int32_t*>(image->comps[0].data);
  uint32_t stride = image->comps[0].stride;
  int32_t span = rangeMax(testCase) - rangeMin(testCase);
  for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
    for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
    {
      int32_t value = (int32_t)((x * 37 + y * 11 + ((x * y) % 41) * 53) % (uint32_t)(span + 1));
      data[(size_t)y * stride + x] = rangeMin(testCase) + value;
    }
  return image;
}

bool compress(const Case& testCase, const std::string& path)
{
  std::string name;
  grk_image* image = makeImage(testCase);
  if(!image)
  {
    fprintf(stderr, "%s: could not build the source image\n", describe(testCase, name));
    return false;
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = false;
  if(testCase.multipleTiles)
  {
    parameters.tile_size_on = true;
    parameters.t_width = 64;
    parameters.t_height = 48;
  }

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "%s: grk_compress_init failed\n", describe(testCase, name));
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "%s: grk_compress failed\n", describe(testCase, name));
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

bool capture(const Case& testCase, grk_image* image, Plane& out)
{
  std::string name;
  const auto& comp = image->comps[0];
  if(!comp.data || comp.w == 0 || comp.h == 0)
  {
    fprintf(stderr, "%s: decoded component is empty: %ux%u\n", describe(testCase, name), comp.w,
            comp.h);
    return false;
  }
  if(comp.data_type != GRK_INT_16)
  {
    fprintf(stderr, "%s: decoded component data type is %d, expected GRK_INT_16\n",
            describe(testCase, name), (int)comp.data_type);
    return false;
  }
  out.width = comp.w;
  out.height = comp.h;
  out.samples.resize((size_t)comp.w * comp.h);
  auto* samples = static_cast<int16_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      out.samples[(size_t)y * comp.w + x] = samples[(size_t)y * comp.stride + x];
  return true;
}

// window == nullptr decodes the whole image
bool decode(const Case& testCase, const std::string& path, const uint32_t* window, Plane& out)
{
  std::string name;
  grk_decompress_parameters params = {};
  if(window)
  {
    params.dw_x0 = window[0];
    params.dw_y0 = window[1];
    params.dw_x1 = window[2];
    params.dw_y1 = window[3];
  }
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "%s: grk_decompress_init failed\n", describe(testCase, name));
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "%s: grk_decompress_read_header failed\n", describe(testCase, name));
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "%s: grk_decompress failed\n", describe(testCase, name));
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "%s: grk_decompress_get_image returned null\n", describe(testCase, name));
    else
      ok = capture(testCase, image, out);
  }
  grk_object_unref(codec);
  return ok;
}

bool checkWindow(const Case& testCase, const std::string& path, const Plane& full, uint32_t x0,
                 uint32_t y0, uint32_t x1, uint32_t y1)
{
  std::string name;
  const uint32_t window[4] = {x0, y0, x1, y1};
  Plane decoded;
  if(!decode(testCase, path, window, decoded))
  {
    fprintf(stderr, "%s: window (%u,%u,%u,%u) failed to decode\n", describe(testCase, name), x0, y0,
            x1, y1);
    return false;
  }
  if(decoded.width != x1 - x0 || decoded.height != y1 - y0)
  {
    fprintf(stderr, "%s: window (%u,%u,%u,%u) decoded as %ux%u\n", describe(testCase, name), x0, y0,
            x1, y1, decoded.width, decoded.height);
    return false;
  }
  for(uint32_t y = 0; y < decoded.height; ++y)
    for(uint32_t x = 0; x < decoded.width; ++x)
    {
      int32_t got = decoded.samples[(size_t)y * decoded.width + x];
      int32_t expected = full.samples[(size_t)(y0 + y) * full.width + x0 + x];
      if(got != expected)
      {
        fprintf(stderr,
                "%s: window (%u,%u,%u,%u): sample (%u,%u) is %d, whole image decode has %d\n",
                describe(testCase, name), x0, y0, x1, y1, x0 + x, y0 + y, got, expected);
        return false;
      }
      if(got < rangeMin(testCase) || got > rangeMax(testCase))
      {
        fprintf(stderr, "%s: window (%u,%u,%u,%u): sample (%u,%u) = %d is outside [%d, %d]\n",
                describe(testCase, name), x0, y0, x1, y1, x0 + x, y0 + y, got, rangeMin(testCase),
                rangeMax(testCase));
        return false;
      }
    }
  return true;
}

bool runCase(const Case& testCase)
{
  std::string name;
  std::string path = std::string("region_decompress_int16_") +
                     (testCase.sgnd ? "signed_" : "unsigned_") +
                     (testCase.multipleTiles ? "tiled" : "single") + ".j2k";
  if(!compress(testCase, path))
    return false;

  Plane full;
  bool ok = decode(testCase, path, nullptr, full);
  if(ok && (full.width != IMAGE_WIDTH || full.height != IMAGE_HEIGHT))
  {
    fprintf(stderr, "%s: whole image decode is %ux%u\n", describe(testCase, name), full.width,
            full.height);
    ok = false;
  }
  if(ok)
  {
    // windows that start and end off any code block, tile or subband boundary
    ok = checkWindow(testCase, path, full, 37, 29, 133, 101) &&
         checkWindow(testCase, path, full, 0, 0, 65, 47) &&
         checkWindow(testCase, path, full, 130, 90, IMAGE_WIDTH, IMAGE_HEIGHT) &&
         checkWindow(testCase, path, full, 96, 71, 97, 72);
  }
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

  int status = 0;
  for(bool sgnd : {false, true})
    for(bool multipleTiles : {false, true})
    {
      Case testCase = {sgnd, multipleTiles};
      if(!runCase(testCase))
        status = 1;
    }

  grk_deinitialize();
  return status;
}
