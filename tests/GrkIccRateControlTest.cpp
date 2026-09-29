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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
constexpr uint32_t kWidth = 128;
constexpr uint32_t kHeight = 128;
constexpr uint16_t kComponents = 3;
constexpr uint32_t kRawBytes = kWidth * kHeight * kComponents;
constexpr size_t kStreamBytes = 1u << 20;
// larger than the first layer below and smaller than the final layer
constexpr uint32_t kOversizeProfile = 8000;
// large enough that appending it misses an 8:1 budget
constexpr uint32_t kCountedProfile = 2500;
// between one tile's header share and the whole-file budget
constexpr uint32_t kSharedProfile = 9000;

int g_failures = 0;
std::string g_errors;

void recordError(const char* msg, void* /* client_data */)
{
  if(msg)
  {
    g_errors.append(msg);
    g_errors.push_back('\n');
  }
}

int32_t noiseAt(uint16_t component, uint32_t x, uint32_t y)
{
  uint32_t n = (uint32_t)component * 0x9E3779B9u ^ x * 747796405u ^ y * 2891336453u;
  n ^= n >> 16;
  n *= 0x7FEB352Du;
  n ^= n >> 15;
  return (int32_t)(n & 255u);
}

void storeSample(grk_image_comp* comp, uint32_t x, uint32_t y, int32_t value)
{
  size_t index = (size_t)y * comp->stride + x;
  if(comp->data_type == GRK_INT_16)
    static_cast<int16_t*>(comp->data)[index] = (int16_t)value;
  else
    static_cast<int32_t*>(comp->data)[index] = value;
}

int32_t loadSample(const grk_image_comp* comp, uint32_t x, uint32_t y)
{
  size_t index = (size_t)y * comp->stride + x;
  if(comp->data_type == GRK_INT_16)
    return static_cast<const int16_t*>(comp->data)[index];
  return static_cast<const int32_t*>(comp->data)[index];
}

grk_image* makeImage(uint32_t profileBytes)
{
  grk_image_comp components[kComponents] = {};
  for(auto& comp : components)
  {
    comp.w = kWidth;
    comp.h = kHeight;
    comp.dx = 1;
    comp.dy = 1;
    comp.prec = 8;
    comp.sgnd = false;
  }
  auto image = grk_image_new(kComponents, components, GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    auto comp = image->comps + c;
    for(uint32_t y = 0; y < comp->h; ++y)
      for(uint32_t x = 0; x < comp->w; ++x)
        storeSample(comp, x, y, noiseAt(c, x, y));
  }
  if(profileBytes == 0)
    return image;

  image->meta = grk_image_meta_new();
  if(!image->meta)
  {
    grk_object_unref(&image->obj);
    return nullptr;
  }
  // color_space ICC skips parsing and stores the buffer as the profile
  image->color_space = GRK_CLRSPC_ICC;
  image->meta->color.icc_profile_buf = new uint8_t[profileBytes];
  std::memset(image->meta->color.icc_profile_buf, 0x5A, profileBytes);
  image->meta->color.icc_profile_len = profileBytes;
  return image;
}

uint64_t compressImage(GRK_SUPPORTED_FILE_FMT format, uint32_t profileBytes,
                       std::initializer_list<double> rates, uint32_t tileSize, bool irreversible,
                       std::vector<uint8_t>* stream)
{
  auto image = makeImage(profileBytes);
  if(!image)
    return 0;

  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = format;
  params.numresolution = 4;
  params.cblockw_init = 32;
  params.cblockh_init = 32;
  params.irreversible = irreversible;
  params.allocation_by_rate_distortion = true;
  params.numlayers = 0;
  for(double rate : rates)
    params.layer_rate[params.numlayers++] = rate;
  if(tileSize != 0)
  {
    params.tile_size_on = true;
    params.t_width = tileSize;
    params.t_height = tileSize;
  }

  stream->assign(kStreamBytes, 0);
  grk_stream_params streamParams = {};
  streamParams.buf = stream->data();
  streamParams.buf_len = stream->size();
  auto codec = grk_compress_init(&streamParams, &params, image);
  uint64_t length = 0;
  if(codec)
    length = grk_compress(codec, nullptr);
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  return length;
}

size_t countDifferences(std::vector<uint8_t>& stream, uint64_t length)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = length;
  auto codec = grk_decompress_init(&streamParams, &params);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header) || !grk_decompress(codec, nullptr))
  {
    grk_object_unref(codec);
    return SIZE_MAX;
  }
  auto image = grk_decompress_get_image(codec);
  if(!image || image->numcomps != kComponents)
  {
    grk_object_unref(codec);
    return SIZE_MAX;
  }
  size_t differences = 0;
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    auto comp = image->comps + c;
    if(comp->w != kWidth || comp->h != kHeight)
    {
      grk_object_unref(codec);
      return SIZE_MAX;
    }
    for(uint32_t y = 0; y < comp->h; ++y)
      for(uint32_t x = 0; x < comp->w; ++x)
        if(loadSample(comp, x, y) != noiseAt(c, x, y))
          ++differences;
  }
  grk_object_unref(codec);
  return differences;
}

void fail(const char* name, const char* detail)
{
  std::fprintf(stderr, "FAIL: %s: %s\n", name, detail);
  ++g_failures;
}

void expectReject(const char* name, uint32_t profileBytes, std::initializer_list<double> rates,
                  uint32_t tileSize)
{
  g_errors.clear();
  std::vector<uint8_t> stream;
  uint64_t length = compressImage(GRK_FMT_JP2, profileBytes, rates, tileSize, true, &stream);
  if(length != 0)
  {
    char detail[128];
    std::snprintf(detail, sizeof(detail), "compressed %llu bytes", (unsigned long long)length);
    fail(name, detail);
    return;
  }
  if(g_errors.find("ICC profile") == std::string::npos)
  {
    fail(name, g_errors.empty() ? "no error was logged" : g_errors.c_str());
    return;
  }
  std::printf("ok: %s rejected\n", name);
}

void expectDecodes(const char* name, GRK_SUPPORTED_FILE_FMT format, uint32_t profileBytes,
                   std::initializer_list<double> rates, uint32_t tileSize, bool irreversible,
                   bool exact, uint64_t maxLength)
{
  std::vector<uint8_t> stream;
  uint64_t length = compressImage(format, profileBytes, rates, tileSize, irreversible, &stream);
  size_t differences = length == 0 ? SIZE_MAX : countDifferences(stream, length);
  bool profileMissing = profileBytes > 0 && format == GRK_FMT_JP2 && length <= profileBytes;
  bool overBudget = maxLength != 0 && length > maxLength;
  bool badSamples = differences == SIZE_MAX || (exact ? differences != 0 : differences == 0);
  if(length == 0 || profileMissing || overBudget || badSamples)
  {
    char detail[192];
    std::snprintf(detail, sizeof(detail), "length %llu max %llu, %zu samples differ",
                  (unsigned long long)length, (unsigned long long)maxLength, differences);
    fail(name, detail);
    return;
  }
  std::printf("ok: %s is %llu bytes, %zu samples differ\n", name, (unsigned long long)length,
              differences);
}
} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  grk_msg_handlers handlers = {};
  handlers.error_callback = recordError;
  grk_set_msg_handlers(handlers);

  expectReject("first layer smaller than the profile", kOversizeProfile, {40.0, 4.0}, 0);
  expectReject("first layer smaller than the profile, four tiles", kOversizeProfile, {40.0, 4.0},
               64);

  constexpr uint64_t kFinalBudget = kRawBytes / 4;
  expectDecodes("jp2 without a profile", GRK_FMT_JP2, 0, {40.0, 4.0}, 0, true, false,
                kFinalBudget + 1024);
  expectDecodes("j2k does not store the profile", GRK_FMT_J2K, kOversizeProfile, {40.0, 4.0}, 0,
                true, false, kFinalBudget + 1024);

  // a rate of 0 is unconstrained
  expectDecodes("lossless jp2 keeps the profile", GRK_FMT_JP2, kOversizeProfile, {0.0}, 0, false,
                true, 0);

  std::vector<uint8_t> plain;
  std::vector<uint8_t> withProfile;
  constexpr double kBindingRatio = 8.0;
  constexpr uint64_t kBindingBudget = kRawBytes / 8;
  uint64_t plainLen = compressImage(GRK_FMT_JP2, 0, {kBindingRatio}, 0, true, &plain);
  uint64_t withLen =
      compressImage(GRK_FMT_JP2, kCountedProfile, {kBindingRatio}, 0, true, &withProfile);
  size_t plainDiffer = plainLen == 0 ? SIZE_MAX : countDifferences(plain, plainLen);
  size_t withDiffer = withLen == 0 ? SIZE_MAX : countDifferences(withProfile, withLen);
  bool rateBinds = plainLen > kBindingBudget * 2 / 3;
  bool counted = withLen > kCountedProfile && withLen <= kBindingBudget + 1024 &&
                 withLen < plainLen + kCountedProfile / 2;
  if(!rateBinds || !counted || plainDiffer == 0 || plainDiffer == SIZE_MAX || withDiffer == 0 ||
     withDiffer == SIZE_MAX)
  {
    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "plain %llu, with profile %llu, budget %llu (rate %s, profile %s)",
                  (unsigned long long)plainLen, (unsigned long long)withLen,
                  (unsigned long long)kBindingBudget, rateBinds ? "binds" : "does not bind",
                  counted ? "counted" : "not counted");
    fail("profile is counted against a binding rate", detail);
  }
  else
  {
    std::printf("ok: profile is counted, plain %llu bytes, with profile %llu bytes, budget %llu\n",
                (unsigned long long)plainLen, (unsigned long long)withLen,
                (unsigned long long)kBindingBudget);
  }

  constexpr uint64_t kSharedBudget = (uint64_t)(kRawBytes / 2.5);
  expectDecodes("header is shared across tiles", GRK_FMT_JP2, kSharedProfile, {2.5}, 64, true,
                false, kSharedBudget + 2048);

  grk_deinitialize();
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("icc rate control OK\n");
  return 0;
}
