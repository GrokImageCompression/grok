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
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
  const uint32_t IMAGE_WIDTH = 61;
  const uint32_t IMAGE_HEIGHT = 69;
  const uint16_t NUM_COMPONENTS = 3;
  const uint8_t PRECISION = 8;
  const uint32_t TILE_WIDTH = 32;
  const uint32_t TILE_HEIGHT = 32;
  const uint16_t NUM_LAYERS = 4;
  const uint16_t FIRST_PASS_LAYERS = 1;
  const uint16_t PROGRESSION_TILE_INDEX = 0;

  // enough of the codestream to get past the main header and into tile data,
  // so the first decompress starts work and then runs out of bytes
  const double TRUNCATED_FRACTION = 0.4;

  void useMercury(bool on)
  {
#if defined(_WIN32)
    _putenv_s("GRK_MERCURY", on ? "1" : "");
#else
    if(on)
      setenv("GRK_MERCURY", "1", 1);
    else
      unsetenv("GRK_MERCURY");
#endif
  }

  int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
  {
    if(comp.data_type == GRK_INT_16)
      return static_cast<int16_t*>(comp.data)[index];
    return static_cast<int32_t*>(comp.data)[index];
  }

  grk_image* makeImage(void)
  {
    grk_image_comp params[NUM_COMPONENTS] = {};
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      params[c].dx = 1;
      params[c].dy = 1;
      params[c].w = IMAGE_WIDTH;
      params[c].h = IMAGE_HEIGHT;
      params[c].prec = PRECISION;
      params[c].sgnd = false;
    }
    grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
    if(!image)
      return nullptr;
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      auto* data = static_cast<int32_t*>(image->comps[c].data);
      if(!data)
      {
        grk_object_unref(&image->obj);
        return nullptr;
      }
      uint32_t stride = image->comps[c].stride;
      for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
        for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
          data[(size_t)y * stride + x] =
              (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
    }
    return image;
  }

  bool compress(const std::string& path, uint16_t numLayers)
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
    parameters.tile_size_on = true;
    parameters.t_width = TILE_WIDTH;
    parameters.t_height = TILE_HEIGHT;
    parameters.numlayers = numLayers;
    if(numLayers > 1)
    {
      parameters.allocation_by_rate_distortion = true;
      for(uint16_t i = 0; i < numLayers; ++i)
        parameters.layer_rate[i] = (double)(40 - i * 9);
    }

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

  bool truncate(const std::string& source, const std::string& target)
  {
    FILE* in = fopen(source.c_str(), "rb");
    if(!in)
      return false;
    std::vector<uint8_t> bytes;
    uint8_t chunk[4096];
    size_t read = 0;
    while((read = fread(chunk, 1, sizeof(chunk), in)) > 0)
      bytes.insert(bytes.end(), chunk, chunk + read);
    fclose(in);
    if(bytes.size() < 64)
      return false;
    size_t keep = (size_t)((double)bytes.size() * TRUNCATED_FRACTION);
    FILE* out = fopen(target.c_str(), "wb");
    if(!out)
      return false;
    bool ok = fwrite(bytes.data(), 1, keep, out) == keep;
    fclose(out);
    return ok;
  }

  // a true return has to mean every component carries a readable buffer
  bool hasUsableData(const char* label, const char* stage, grk_image* image)
  {
    if(!image)
    {
      fprintf(stderr, "%s: %s reported success but the image is null\n", label, stage);
      return false;
    }
    if(image->numcomps == 0)
    {
      fprintf(stderr, "%s: %s reported success but the image has no components\n", label, stage);
      return false;
    }
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      const auto& comp = image->comps[c];
      if(!comp.data || comp.w == 0 || comp.h == 0)
      {
        fprintf(stderr, "%s: %s reported success but component %u is %ux%u with data %p\n", label,
                stage, c, comp.w, comp.h, comp.data);
        return false;
      }
    }
    return true;
  }

  std::vector<int32_t> capture(grk_image* image)
  {
    std::vector<int32_t> samples;
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      const auto& comp = image->comps[c];
      for(uint32_t y = 0; y < comp.h; ++y)
        for(uint32_t x = 0; x < comp.w; ++x)
          samples.push_back(sampleAt(comp, (uint64_t)y * comp.stride + x));
    }
    return samples;
  }

  grk_object* openCodec(const char* label, const std::string& path, grk_decompress_parameters& params,
                        grk_header_info& headerInfo)
  {
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "%s: grk_decompress_init failed\n", label);
      return nullptr;
    }
    if(!grk_decompress_read_header(codec, &headerInfo))
    {
      fprintf(stderr, "%s: grk_decompress_read_header failed\n", label);
      grk_object_unref(codec);
      return nullptr;
    }
    grk_decompress_update(&params, codec);
    return codec;
  }

  // a fully decoded codec must answer a repeat grk_decompress with the same image
  bool runWholeStream(const char* label, const std::string& path, bool mercury)
  {
    useMercury(mercury);

    grk_decompress_parameters params = {};
    grk_header_info headerInfo = {};
    grk_object* codec = openCodec(label, path, params, headerInfo);
    if(!codec)
      return false;

    bool ok = true;
    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "%s: the first grk_decompress failed\n", label);
      grk_object_unref(codec);
      return false;
    }
    grk_image* image = grk_decompress_get_image(codec);
    if(!hasUsableData(label, "the first grk_decompress", image))
    {
      grk_object_unref(codec);
      return false;
    }
    std::vector<int32_t> firstSamples = capture(image);

    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "%s: the second grk_decompress failed\n", label);
      ok = false;
    }
    image = grk_decompress_get_image(codec);
    if(!hasUsableData(label, "the second grk_decompress", image))
      ok = false;
    else if(capture(image) != firstSamples)
    {
      fprintf(stderr, "%s: the second grk_decompress changed the first image's samples\n", label);
      ok = false;
    }

    grk_object_unref(codec);
    if(ok)
      printf("%s: repeat decompress kept all %zu samples\n", label, firstSamples.size());
    return ok;
  }

  // a codec whose first decompress failed must not turn that into a success
  bool runTruncatedStream(const char* label, const std::string& path, bool mercury)
  {
    useMercury(mercury);

    grk_decompress_parameters params = {};
    grk_header_info headerInfo = {};
    grk_object* codec = openCodec(label, path, params, headerInfo);
    if(!codec)
      return false;

    bool ok = true;
    bool firstOk = grk_decompress(codec, nullptr);
    std::vector<int32_t> firstSamples;
    if(firstOk)
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!hasUsableData(label, "the first grk_decompress", image))
        ok = false;
      else
        firstSamples = capture(image);
    }

    bool secondOk = grk_decompress(codec, nullptr);
    if(secondOk && !firstOk)
    {
      fprintf(stderr, "%s: the second grk_decompress claimed success after the first failed\n",
              label);
      ok = false;
    }
    if(secondOk)
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!hasUsableData(label, "the second grk_decompress", image))
        ok = false;
      else if(firstOk && capture(image) != firstSamples)
      {
        fprintf(stderr, "%s: the second grk_decompress changed the first image's samples\n", label);
        ok = false;
      }
    }

    grk_object_unref(codec);
    if(ok)
      printf("%s: first %s, second %s\n", label, firstOk ? "true" : "false",
             secondOk ? "true" : "false");
    return ok;
  }

  bool insideProgressionTile(uint32_t x, uint32_t y)
  {
    return x < TILE_WIDTH && y < TILE_HEIGHT;
  }

  // grk_decompress_set_progression_state marks one tile for a richer re-decode.  Whatever the
  // second grk_decompress does with that request, every other tile has to survive it untouched.
  bool runProgressionState(const char* label, const std::string& path, bool mercury)
  {
    useMercury(mercury);

    grk_decompress_parameters params = {};
    params.core.layers_to_decompress = FIRST_PASS_LAYERS;
    grk_header_info headerInfo = {};
    grk_object* codec = openCodec(label, path, params, headerInfo);
    if(!codec)
      return false;

    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "%s: the first grk_decompress failed\n", label);
      grk_object_unref(codec);
      return false;
    }
    grk_image* image = grk_decompress_get_image(codec);
    if(!hasUsableData(label, "the first grk_decompress", image))
    {
      grk_object_unref(codec);
      return false;
    }
    std::vector<int32_t> firstSamples = capture(image);

    grk_progression_state state = {};
    state.single_tile = true;
    state.tile_index = PROGRESSION_TILE_INDEX;
    state.num_resolutions = headerInfo.numresolutions;
    for(uint8_t r = 0; r < state.num_resolutions; ++r)
      state.layers_per_resolution[r] = NUM_LAYERS;
    // the mercury fast path decodes without filling the tile cache, so it has no cached tile
    // to attach the new layer budget to
    bool stateSet = grk_decompress_set_progression_state(codec, state);
    if(!stateSet && !mercury)
    {
      fprintf(stderr, "%s: grk_decompress_set_progression_state failed\n", label);
      grk_object_unref(codec);
      return false;
    }

    bool ok = true;
    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "%s: the grk_decompress after the progression state failed\n", label);
      ok = false;
    }
    image = grk_decompress_get_image(codec);
    if(!hasUsableData(label, "the grk_decompress after the progression state", image))
    {
      grk_object_unref(codec);
      return false;
    }

    std::vector<int32_t> secondSamples = capture(image);
    if(secondSamples.size() != firstSamples.size())
    {
      fprintf(stderr, "%s: the image changed size from %zu to %zu samples\n", label,
              firstSamples.size(), secondSamples.size());
      grk_object_unref(codec);
      return false;
    }

    size_t index = 0;
    size_t changedInsideTile = 0;
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      const auto& comp = image->comps[c];
      for(uint32_t y = 0; y < comp.h; ++y)
      {
        for(uint32_t x = 0; x < comp.w; ++x, ++index)
        {
          if(firstSamples[index] == secondSamples[index])
            continue;
          if(insideProgressionTile(x, y))
            changedInsideTile++;
          else
          {
            fprintf(stderr,
                    "%s: component %u sample (%u,%u) outside the requested tile changed from %d "
                    "to %d\n",
                    label, c, x, y, firstSamples[index], secondSamples[index]);
            ok = false;
            y = comp.h;
            break;
          }
        }
      }
    }

    grk_object_unref(codec);
    if(ok)
      printf("%s: state %s, %zu samples changed inside tile %u, none outside it\n", label,
             stateSet ? "set" : "not cached", changedInsideTile, PROGRESSION_TILE_INDEX);
    return ok;
  }
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  std::string wholePath = "double_decompress_whole.j2k";
  std::string truncatedPath = "double_decompress_truncated.j2k";
  std::string layeredPath = "double_decompress_layered.j2k";
  if(!compress(wholePath, 1) || !truncate(wholePath, truncatedPath) ||
     !compress(layeredPath, NUM_LAYERS))
  {
    fprintf(stderr, "could not build the test codestreams\n");
    grk_deinitialize();
    return 1;
  }

  int result = 0;
  const bool mercurySettings[] = {false, true};
  for(bool mercury : mercurySettings)
  {
    std::string suffix = mercury ? " (mercury)" : " (classic)";
    if(!runWholeStream(("whole stream" + suffix).c_str(), wholePath, mercury))
      result = 1;
    if(!runTruncatedStream(("truncated stream" + suffix).c_str(), truncatedPath, mercury))
      result = 1;
    if(!runProgressionState(("progression state" + suffix).c_str(), layeredPath, mercury))
      result = 1;
  }

  remove(wholePath.c_str());
  remove(truncatedPath.c_str());
  remove(layeredPath.c_str());
  grk_deinitialize();
  if(result == 0)
    printf("double decompress passed\n");
  return result;
}
