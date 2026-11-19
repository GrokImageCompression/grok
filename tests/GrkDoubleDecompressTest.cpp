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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
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
  _putenv_s("GRK_MERCURY", on ? "1" : "0");
#else
  setenv("GRK_MERCURY", on ? "1" : "0", 1);
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

struct StreamLayout
{
  uint16_t numLayers;
  bool tilePartPerResolution;
};

bool compress(const std::string& path, StreamLayout layout)
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
  parameters.numlayers = layout.numLayers;
  if(layout.numLayers > 1)
  {
    parameters.allocation_by_rate_distortion = true;
    for(uint16_t i = 0; i < layout.numLayers; ++i)
      parameters.layer_rate[i] = (double)(40 - i * 9);
  }
  if(layout.tilePartPerResolution)
  {
    parameters.enable_tile_part_generation = true;
    parameters.new_tile_part_progression_divider = 'R';
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

// the answer the re-decoded tile has to match: a plain decode of the same file at the layer
// budget the progression state asked for
bool decodeFromScratch(const char* label, const std::string& path, uint16_t layers,
                       uint32_t cacheStrategy, std::vector<int32_t>& samples)
{
  useMercury(false);

  grk_decompress_parameters params = {};
  params.core.layers_to_decompress = layers;
  params.core.tile_cache_strategy = cacheStrategy;
  grk_header_info headerInfo = {};
  grk_object* codec = openCodec(label, path, params, headerInfo);
  if(!codec)
    return false;

  bool ok = grk_decompress(codec, nullptr) != 0;
  if(!ok)
    fprintf(stderr, "%s: the %u layer reference grk_decompress failed\n", label, layers);
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!hasUsableData(label, "the reference grk_decompress", image))
      ok = false;
    else
      samples = capture(image);
  }
  grk_object_unref(codec);
  return ok;
}

// grk_decompress_set_progression_state marks one tile for a richer re-decode.  The second
// grk_decompress has to deliver that tile at its new layer budget, and every other tile has to
// survive untouched.
bool runProgressionState(const char* label, const std::string& path, bool mercury,
                         uint32_t cacheStrategy)
{
  useMercury(mercury);

  grk_decompress_parameters params = {};
  params.core.layers_to_decompress = FIRST_PASS_LAYERS;
  params.core.tile_cache_strategy = cacheStrategy;
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

  std::vector<int32_t> referenceSamples;
  if(stateSet)
  {
    if(!decodeFromScratch(label, path, NUM_LAYERS, cacheStrategy, referenceSamples))
    {
      grk_object_unref(codec);
      return false;
    }
    if(referenceSamples.size() != secondSamples.size())
    {
      fprintf(stderr, "%s: the reference decode has %zu samples but the codec has %zu\n", label,
              referenceSamples.size(), secondSamples.size());
      grk_object_unref(codec);
      return false;
    }
  }

  size_t index = 0;
  size_t changedInsideTile = 0;
  for(uint16_t c = 0; c < image->numcomps && ok; ++c)
  {
    const auto& comp = image->comps[c];
    for(uint32_t y = 0; y < comp.h && ok; ++y)
    {
      for(uint32_t x = 0; x < comp.w; ++x, ++index)
      {
        if(!insideProgressionTile(x, y))
        {
          if(firstSamples[index] == secondSamples[index])
            continue;
          fprintf(stderr,
                  "%s: component %u sample (%u,%u) outside the requested tile changed from %d "
                  "to %d\n",
                  label, c, x, y, firstSamples[index], secondSamples[index]);
          ok = false;
          break;
        }
        if(firstSamples[index] != secondSamples[index])
          changedInsideTile++;
        if(stateSet && secondSamples[index] != referenceSamples[index])
        {
          fprintf(stderr,
                  "%s: component %u sample (%u,%u) in tile %u is %d, but a %u layer decode "
                  "gives %d\n",
                  label, c, x, y, PROGRESSION_TILE_INDEX, secondSamples[index], NUM_LAYERS,
                  referenceSamples[index]);
          ok = false;
          break;
        }
      }
    }
  }

  if(ok && stateSet && changedInsideTile == 0)
  {
    fprintf(stderr,
            "%s: the grk_decompress after the progression state left every sample in "
            "tile %u unchanged\n",
            label, PROGRESSION_TILE_INDEX);
    ok = false;
  }

  grk_object_unref(codec);
  if(ok)
    printf("%s: state %s, %zu samples changed inside tile %u, none outside it\n", label,
           stateSet ? "set" : "not cached", changedInsideTile, PROGRESSION_TILE_INDEX);
  return ok;
}

struct DecodeResult
{
  bool decoded;
  std::vector<int32_t> samples;
  bool operator==(const DecodeResult&) const = default;
};

struct ReuseStream
{
  std::string name;
  std::string path;
  bool corrupt;
};

bool reuseMatches(const ReuseStream& stream, const DecodeResult& reused, const DecodeResult& fresh)
{
  if(!stream.corrupt)
    return reused == fresh;
  return !fresh.decoded || (reused.decoded && reused.samples.size() == fresh.samples.size());
}

using DecodeStep = std::function<DecodeResult(grk_object* codec, const grk_header_info& header,
                                              size_t step)>;

bool runReuseSequence(const std::string& label, const ReuseStream& stream, bool mercury,
                      const grk_decompress_parameters& openParams, size_t numSteps,
                      const DecodeStep& decodeStep)
{
  useMercury(mercury);

  grk_decompress_parameters params = openParams;
  grk_header_info headerInfo = {};
  grk_object* reused = openCodec(label.c_str(), stream.path, params, headerInfo);
  if(!reused)
    return false;

  bool ok = true;
  for(size_t step = 0; step < numSteps && ok; ++step)
  {
    DecodeResult reusedResult = decodeStep(reused, headerInfo, step);

    grk_decompress_parameters referenceParams = openParams;
    grk_header_info referenceHeader = {};
    grk_object* reference = openCodec(label.c_str(), stream.path, referenceParams, referenceHeader);
    if(!reference)
    {
      ok = false;
      break;
    }
    DecodeResult referenceResult = decodeStep(reference, referenceHeader, step);
    grk_object_unref(reference);

    if(!reuseMatches(stream, reusedResult, referenceResult))
    {
      fprintf(stderr,
              "%s: step %zu on the reused codec gives decoded %d with %zu samples, a new codec "
              "gives decoded %d with %zu samples\n",
              label.c_str(), step, reusedResult.decoded, reusedResult.samples.size(),
              referenceResult.decoded, referenceResult.samples.size());
      ok = false;
    }
  }
  grk_object_unref(reused);
  return ok;
}

DecodeResult decodedImage(const std::string& label, const char* stage, grk_image* image)
{
  if(!hasUsableData(label.c_str(), stage, image))
    return {false, {}};
  return {true, capture(image)};
}

struct WindowFractions
{
  double x0, y0, x1, y1;
};

const WindowFractions REUSE_WINDOWS[] = {{0, 0, 0.25, 0.25},
                                         {0.75, 0.75, 1, 1},
                                         {0, 0, 0.25, 0.25},
                                         {0.3, 0.2, 0.7, 0.6},
                                         {0, 0, 1, 1}};

struct PixelWindow
{
  uint32_t x0, y0, x1, y1;
};

PixelWindow pixelWindow(const grk_image& image, const WindowFractions& fractions)
{
  const double width = image.x1 - image.x0;
  const double height = image.y1 - image.y0;
  return {(uint32_t)std::floor(image.x0 + width * fractions.x0),
          (uint32_t)std::floor(image.y0 + height * fractions.y0),
          (uint32_t)std::ceil(image.x0 + width * fractions.x1),
          (uint32_t)std::ceil(image.y0 + height * fractions.y1)};
}

bool runCompositeWindowSequence(const std::string& label, const ReuseStream& stream, bool mercury,
                                uint32_t cacheStrategy, uint8_t reduce)
{
  grk_decompress_parameters openParams = {};
  openParams.core.tile_cache_strategy = cacheStrategy;
  auto decodeWindow = [&](grk_object* codec, const grk_header_info& header, size_t step) {
    const PixelWindow window = pixelWindow(header.header_image, REUSE_WINDOWS[step]);
    grk_decompress_parameters params = openParams;
    params.core.reduce = reduce;
    params.dw_x0 = window.x0;
    params.dw_y0 = window.y0;
    params.dw_x1 = window.x1;
    params.dw_y1 = window.y1;
    if(!grk_decompress_update(&params, codec) || !grk_decompress(codec, nullptr))
      return DecodeResult{false, {}};
    return decodedImage(label, "the window grk_decompress", grk_decompress_get_image(codec));
  };
  return runReuseSequence(label, stream, mercury, openParams, std::size(REUSE_WINDOWS),
                          decodeWindow);
}

std::vector<int32_t> copySwaths(grk_object* codec, const grk_header_info& header,
                                const PixelWindow& window, uint8_t reduce)
{
  const uint32_t rows = std::max(1u, header.t_height >> reduce);
  // jpeg 2000 reduced coordinates round up
  const uint32_t swathGridY0 = (uint32_t)(((uint64_t)header.ty0 + (1u << reduce) - 1) >> reduce);
  const uint16_t numcomps = header.header_image.numcomps;
  const bool sixteenBit = header.header_image.comps[0].prec > 8;
  const size_t sampleBytes = sixteenBit ? 2 : 1;
  const uint32_t width = window.x1 - window.x0;
  std::vector<int32_t> samples;
  std::vector<uint8_t> buffer;
  for(uint32_t y = window.y0; y < window.y1;)
  {
    const uint32_t y1 =
        std::min(((y - swathGridY0) / rows + 1) * rows + swathGridY0, window.y1);
    grk_wait_swath swath = {};
    swath.x0 = window.x0;
    swath.y0 = y;
    swath.x1 = window.x1;
    swath.y1 = y1;
    grk_decompress_wait(codec, &swath);
    const size_t numRows = y1 - y;
    buffer.assign((size_t)numcomps * numRows * width * sampleBytes, 0);
    grk_swath_buffer copy = {};
    copy.data = buffer.data();
    copy.prec = sixteenBit ? 16 : 8;
    copy.numcomps = numcomps;
    copy.pixel_space = (int64_t)sampleBytes;
    copy.line_space = (int64_t)(width * sampleBytes);
    copy.band_space = (int64_t)(numRows * width * sampleBytes);
    copy.promote_alpha = -1;
    copy.x0 = window.x0;
    copy.y0 = y;
    copy.x1 = window.x1;
    copy.y1 = y1;
    grk_decompress_schedule_swath_copy(codec, &swath, &copy);
    grk_decompress_wait_swath_copy(codec);
    for(size_t i = 0; i < buffer.size(); i += sampleBytes)
      samples.push_back(sixteenBit ? (int32_t)(buffer[i] | (buffer[i + 1] << 8)) : buffer[i]);
    y = y1;
  }
  return samples;
}

bool runSwathWindowSequence(const std::string& label, const ReuseStream& stream, bool mercury,
                            uint32_t cacheStrategy, uint8_t reduce)
{
  grk_decompress_parameters openParams = {};
  openParams.asynchronous = true;
  openParams.simulate_synchronous = true;
  openParams.core.tile_cache_strategy = cacheStrategy;
  auto decodeWindow = [&](grk_object* codec, const grk_header_info& header, size_t step) {
    const PixelWindow unreduced = pixelWindow(header.header_image, REUSE_WINDOWS[step]);
    auto reduced = [&](uint32_t v) {
      return (uint32_t)(((uint64_t)v + (1u << reduce) - 1) >> reduce);
    };
    const PixelWindow window = {reduced(unreduced.x0), reduced(unreduced.y0),
                                reduced(unreduced.x1), reduced(unreduced.y1)};
    grk_decompress_parameters params = openParams;
    params.core.reduce = reduce;
    // grok keeps a single tile's pixels in the composite image
    params.core.skip_allocate_composite = header.t_grid_width * header.t_grid_height > 1;
    params.dw_reduced = true;
    params.dw_x0 = window.x0;
    params.dw_y0 = window.y0;
    params.dw_x1 = window.x1;
    params.dw_y1 = window.y1;
    if(!grk_decompress_update(&params, codec) || !grk_decompress(codec, nullptr))
      return DecodeResult{false, {}};
    DecodeResult result{true, copySwaths(codec, header, window, reduce)};
    // grok joins its decompress worker only on a null swath wait
    grk_decompress_wait(codec, nullptr);
    return result;
  };
  return runReuseSequence(label, stream, mercury, openParams, std::size(REUSE_WINDOWS),
                          decodeWindow);
}

bool runTileSequence(const std::string& label, const ReuseStream& stream, bool mercury,
                     uint32_t cacheStrategy)
{
  grk_decompress_parameters openParams = {};
  openParams.core.tile_cache_strategy = cacheStrategy;
  auto decodeTile = [&](grk_object* codec, const grk_header_info& header, size_t step) {
    const uint16_t lastTile = (uint16_t)(header.t_grid_width * header.t_grid_height - 1);
    const uint16_t tiles[] = {0, lastTile, 0};
    const uint16_t tileIndex = tiles[step];
    if(!grk_decompress_tile(codec, tileIndex))
      return DecodeResult{false, {}};
    return decodedImage(label, "grk_decompress_tile",
                        grk_decompress_get_tile_image(codec, tileIndex, false));
  };
  return runReuseSequence(label, stream, mercury, openParams, 3, decodeTile);
}

bool runReuseSequences(const ReuseStream& stream)
{
  const struct
  {
    uint32_t strategy;
    const char* name;
  } cacheStrategies[] = {{GRK_TILE_CACHE_NONE, "cache none"}, {GRK_TILE_CACHE_IMAGE, "cache image"}};
  const uint8_t reductions[] = {0, 1};

  bool ok = true;
  for(bool mercury : {false, true})
  {
    for(const auto& cache : cacheStrategies)
    {
      std::string label = stream.name + (mercury ? " (mercury) " : " (classic) ") + cache.name;
      for(uint8_t reduce : reductions)
      {
        std::string reduceLabel = " reduce " + std::to_string(reduce);
        ok &= runCompositeWindowSequence(label + " composite windows" + reduceLabel, stream,
                                         mercury, cache.strategy, reduce);
        ok &= runSwathWindowSequence(label + " swath windows" + reduceLabel, stream, mercury,
                                     cache.strategy, reduce);
      }
      ok &= runTileSequence(label + " tiles", stream, mercury, cache.strategy);
    }
  }
  return ok;
}
} // namespace

int main(int argc, char** argv)
{
  grk_initialize(nullptr, 0, nullptr);

  std::string wholePath = "double_decompress_whole.j2k";
  std::string truncatedPath = "double_decompress_truncated.j2k";
  std::string layeredPath = "double_decompress_layered.j2k";
  std::string tilePartsPath = "double_decompress_tile_parts.j2k";
  if(!compress(wholePath, {.numLayers = 1, .tilePartPerResolution = false}) ||
     !truncate(wholePath, truncatedPath) ||
     !compress(layeredPath, {.numLayers = NUM_LAYERS, .tilePartPerResolution = false}) ||
     !compress(tilePartsPath, {.numLayers = 1, .tilePartPerResolution = true}))
  {
    fprintf(stderr, "could not build the test codestreams\n");
    grk_deinitialize();
    return 1;
  }

  const struct
  {
    uint32_t strategy;
    const char* name;
  } cacheStrategies[] = {{GRK_TILE_CACHE_NONE, "cache none"},
                         {GRK_TILE_CACHE_IMAGE, "cache image"},
                         {GRK_TILE_CACHE_ALL, "cache all"}};

  int result = 0;
  const bool mercurySettings[] = {false, true};
  for(bool mercury : mercurySettings)
  {
    std::string suffix = mercury ? " (mercury)" : " (classic)";
    if(!runWholeStream(("whole stream" + suffix).c_str(), wholePath, mercury))
      result = 1;
    if(!runTruncatedStream(("truncated stream" + suffix).c_str(), truncatedPath, mercury))
      result = 1;
    for(const auto& cache : cacheStrategies)
    {
      std::string label = "progression state" + suffix + " " + cache.name;
      if(!runProgressionState(label.c_str(), layeredPath, mercury, cache.strategy))
        result = 1;
    }
  }

  std::vector<ReuseStream> reuseStreams = {
      {.name = "whole stream", .path = wholePath, .corrupt = false},
      {.name = "truncated stream", .path = truncatedPath, .corrupt = true},
      {.name = "tile part per resolution", .path = tilePartsPath, .corrupt = false}};
  if(argc > 1)
  {
    const std::string input = std::string(argv[1]) + "/input/";
    reuseStreams.push_back(
        {.name = "ppt stream", .path = input + "conformance/p1_02.j2k", .corrupt = false});
    reuseStreams.push_back({.name = "missing tile stream",
                            .path = input + "nonregression/issue395.jp2",
                            .corrupt = true});
    reuseStreams.push_back({.name = "corrupt tile-part header stream",
                            .path = input + "nonregression/2236.pdf.SIGSEGV.398.1376.jp2",
                            .corrupt = true});
  }
  for(const auto& stream : reuseStreams)
    if(!runReuseSequences(stream))
      result = 1;

  remove(wholePath.c_str());
  remove(truncatedPath.c_str());
  remove(layeredPath.c_str());
  remove(tilePartsPath.c_str());
  grk_deinitialize();
  if(result == 0)
    printf("double decompress passed\n");
  return result;
}
