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

// a region decode of an HT codestream has to match the same rectangle cropped out of
// a whole image decode. the image width leaves band-edge code blocks whose width is
// not a multiple of 8, so the sparse canvas write has to honour the HT coder's
// 8-aligned row stride. 14-bit precision keeps the decode on the int32 path.
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
const uint8_t PRECISION = 14;

struct Plane
{
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<int32_t> samples;
};

grk_image* makeImage(void)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_WIDTH;
  params.h = IMAGE_HEIGHT;
  params.prec = PRECISION;
  params.sgnd = false;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image || !image->comps[0].data)
  {
    if(image)
      grk_object_unref(&image->obj);
    return nullptr;
  }
  auto* data = static_cast<int32_t*>(image->comps[0].data);
  uint32_t stride = image->comps[0].stride;
  // rows must differ from each other so a write that picks the wrong rows shows up
  for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
    for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
      data[(size_t)y * stride + x] =
          (int32_t)((x * 37 + y * 4111 + ((x * y) % 41) * 53) % (1u << PRECISION));
  return image;
}

bool compress(const std::string& path)
{
  grk_image* image = makeImage();
  if(!image)
  {
    fprintf(stderr, "could not build the source image\n");
    return false;
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = false;
  parameters.cblk_sty = GRK_CBLKSTY_HT_ONLY;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "grk_compress_init failed\n");
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "grk_compress failed\n");
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool capture(grk_image* image, Plane& out)
{
  const auto& comp = image->comps[0];
  if(!comp.data || comp.w == 0 || comp.h == 0)
  {
    fprintf(stderr, "decoded component is empty: %ux%u\n", comp.w, comp.h);
    return false;
  }
  out.width = comp.w;
  out.height = comp.h;
  out.samples.resize((size_t)comp.w * comp.h);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      out.samples[(size_t)y * comp.w + x] = sampleAt(comp, (uint64_t)y * comp.stride + x);
  return true;
}

// window == nullptr decodes the whole image
bool decode(const std::string& path, const uint32_t* window, Plane& out)
{
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
    fprintf(stderr, "grk_decompress_init failed\n");
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "grk_decompress_read_header failed\n");
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "grk_decompress_get_image returned null\n");
    else
      ok = capture(image, out);
  }
  grk_object_unref(codec);
  return ok;
}

bool checkWindow(const std::string& path, const Plane& full, uint32_t x0, uint32_t y0, uint32_t x1,
                 uint32_t y1)
{
  const uint32_t window[4] = {x0, y0, x1, y1};
  Plane decoded;
  if(!decode(path, window, decoded))
  {
    fprintf(stderr, "window (%u,%u,%u,%u) failed to decode\n", x0, y0, x1, y1);
    return false;
  }
  if(decoded.width != x1 - x0 || decoded.height != y1 - y0)
  {
    fprintf(stderr, "window (%u,%u,%u,%u) decoded as %ux%u\n", x0, y0, x1, y1, decoded.width,
            decoded.height);
    return false;
  }
  for(uint32_t y = 0; y < decoded.height; ++y)
    for(uint32_t x = 0; x < decoded.width; ++x)
    {
      int32_t got = decoded.samples[(size_t)y * decoded.width + x];
      int32_t expected = full.samples[(size_t)(y0 + y) * full.width + x0 + x];
      if(got != expected)
      {
        fprintf(stderr, "window (%u,%u,%u,%u): sample (%u,%u) is %d, whole image decode has %d\n",
                x0, y0, x1, y1, x0 + x, y0 + y, got, expected);
        return false;
      }
    }
  return true;
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

  int status = 1;
  std::string path = "region_decompress_ht.j2k";
  if(compress(path))
  {
    Plane full;
    if(decode(path, nullptr, full) && full.width == IMAGE_WIDTH && full.height == IMAGE_HEIGHT)
    {
      // right and bottom edges hold the band-edge code blocks whose width is not
      // a multiple of 8, plus one interior window for the lower resolutions
      bool ok = checkWindow(path, full, 150, 100, IMAGE_WIDTH, IMAGE_HEIGHT) &&
                checkWindow(path, full, 64, 64, IMAGE_WIDTH, 130) &&
                checkWindow(path, full, 20, 15, 90, 70);
      status = ok ? 0 : 1;
    }
    remove(path.c_str());
  }

  grk_deinitialize();
  return status;
}
