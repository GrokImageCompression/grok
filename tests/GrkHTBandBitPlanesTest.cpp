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

// an HT 9/7 codestream whose QCD gives a band more bit planes than the HT block
// coder can hold must be rejected, not dequantized with a negative shift

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t WIDTH = 64;
const uint32_t HEIGHT = 64;
const uint8_t PRECISION = 8;
const uint8_t GUARD_BITS = 7;
// band bit planes = exponent + guard bits - 1, two past the 31 the HT coder holds
const uint8_t EXPONENT = 27;

const uint16_t MARKER_SOT = 0xFF90;
const uint16_t MARKER_QCD = 0xFF5C;
const uint16_t MARKER_QCC = 0xFF5D;
const uint8_t QUANT_STYLE_NONE = 0;
const uint8_t QUANT_STYLE_MASK = 0x1F;
const uint16_t MANTISSA_MASK = 0x07FF;

uint16_t readBigEndian16(const std::vector<uint8_t>& bytes, size_t offset)
{
  return (uint16_t)((bytes[offset] << 8) | bytes[offset + 1]);
}

grk_image* makeImage()
{
  grk_image_comp params[1] = {};
  params[0].dx = 1;
  params[0].dy = 1;
  params[0].w = WIDTH;
  params[0].h = HEIGHT;
  params[0].prec = PRECISION;
  params[0].sgnd = false;
  grk_image* image = grk_image_new(1, params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  auto* data = static_cast<int32_t*>(image->comps[0].data);
  if(!data)
  {
    grk_object_unref(&image->obj);
    return nullptr;
  }
  uint32_t stride = image->comps[0].stride;
  for(uint32_t y = 0; y < HEIGHT; ++y)
    for(uint32_t x = 0; x < WIDTH; ++x)
      data[(size_t)y * stride + x] = (int32_t)((x * 3 + y * 5) & 255);
  return image;
}

bool compressHT97(const std::string& path)
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
  parameters.irreversible = true;
  parameters.cblk_sty = GRK_CBLKSTY_HT_ONLY;
  parameters.numlayers = 1;

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

// rewrites the guard bits and every band exponent of one QCD or QCC segment,
// whose quantization values start at offset
bool patchQuantization(std::vector<uint8_t>& bytes, size_t offset, size_t end)
{
  uint8_t style = bytes[offset] & QUANT_STYLE_MASK;
  if(style == QUANT_STYLE_NONE)
  {
    fprintf(stderr, "expected an irreversible quantization segment\n");
    return false;
  }
  bytes[offset] = (uint8_t)((GUARD_BITS << 5) | style);
  for(size_t pos = offset + 1; pos + 1 < end; pos += 2)
  {
    uint16_t mantissa = readBigEndian16(bytes, pos) & MANTISSA_MASK;
    uint16_t value = (uint16_t)((EXPONENT << 11) | mantissa);
    bytes[pos] = (uint8_t)(value >> 8);
    bytes[pos + 1] = (uint8_t)value;
  }
  return true;
}

bool patchMainHeader(std::vector<uint8_t>& bytes)
{
  size_t pos = 2;
  bool patched = false;
  while(pos + 4 <= bytes.size())
  {
    uint16_t marker = readBigEndian16(bytes, pos);
    if(marker == MARKER_SOT)
      break;
    uint16_t length = readBigEndian16(bytes, pos + 2);
    size_t end = pos + 2 + length;
    if(end > bytes.size())
    {
      fprintf(stderr, "marker %04x runs past the end of the codestream\n", marker);
      return false;
    }
    if(marker == MARKER_QCD)
    {
      if(!patchQuantization(bytes, pos + 4, end))
        return false;
      patched = true;
    }
    else if(marker == MARKER_QCC)
    {
      if(!patchQuantization(bytes, pos + 5, end))
        return false;
      patched = true;
    }
    pos = end;
  }
  if(!patched)
    fprintf(stderr, "no QCD marker in the main header\n");
  return patched;
}

bool rewriteWithOversizedBands(const std::string& source, const std::string& target)
{
  std::ifstream in(source, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  if(bytes.empty())
  {
    fprintf(stderr, "could not read %s\n", source.c_str());
    return false;
  }
  if(!patchMainHeader(bytes))
    return false;
  std::ofstream out(target, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
  return out.good();
}

bool decodes(const std::string& path)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return false;
  grk_header_info headerInfo = {};
  bool ok = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr) &&
            grk_decompress_get_image(codec) != nullptr;
  grk_object_unref(codec);
  return ok;
}
} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  const std::string source = "ht_band_bit_planes.j2k";
  const std::string oversized = "ht_band_bit_planes_oversized.j2k";
  bool ok = compressHT97(source) && rewriteWithOversizedBands(source, oversized);
  if(ok && !decodes(source))
  {
    fprintf(stderr, "the unpatched HT codestream failed to decode\n");
    ok = false;
  }
  if(ok && decodes(oversized))
  {
    fprintf(stderr, "decoded an HT codestream whose bands have %u bit planes\n",
            EXPONENT + GUARD_BITS - 1);
    ok = false;
  }
  remove(source.c_str());
  remove(oversized.c_str());
  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
