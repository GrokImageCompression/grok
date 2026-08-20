// the qfactor quantization model must match the OpenHTJ2K reference byte for byte:
// its QCD and QCC golden dump for Qfactor=90 on an 8-bit 4:4:4 colour image with
// five decomposition levels and one guard bit is reproduced here
#include <cstdio>
#include <cstring>
#include <vector>
#include "grok.h"

namespace
{
const uint16_t NUM_COMPONENTS = 3;
const uint32_t WIDTH = 64;
const uint32_t HEIGHT = 64;
const uint8_t PRECISION = 8;
const uint8_t NUM_RESOLUTIONS = 6;
const uint8_t GUARD_BITS = 1;
const uint8_t QFACTOR = 90;
const size_t OUTPUT_BUFFER_BYTES = (size_t)WIDTH * HEIGHT * NUM_COMPONENTS * sizeof(int32_t);
const uint16_t SOC = 0xFF4F;
const uint16_t SOT = 0xFF90;
const uint16_t QCD = 0xFF5C;
const uint16_t QCC = 0xFF5D;

const std::vector<uint8_t> GOLDEN_QCD = {0xff, 0x5c, 0x00, 0x23, 0x22, 0x66, 0xff, 0x66, 0xd2, 0x66,
                                         0xd2, 0x66, 0xa5, 0x5e, 0xe8, 0x5e, 0xe8, 0x5e, 0xca, 0x57,
                                         0x34, 0x57, 0x34, 0x57, 0x4b, 0x40, 0xaf, 0x40, 0xaf, 0x41,
                                         0xc7, 0x3e, 0xc7, 0x3e, 0xc7, 0x34, 0x6d};
const std::vector<uint8_t> GOLDEN_QCC_1 = {
    0xff, 0x5d, 0x00, 0x24, 0x01, 0x22, 0x66, 0x64, 0x58, 0x08, 0x58, 0x08, 0x58,
    0x52, 0x50, 0xd1, 0x50, 0xd1, 0x51, 0x8f, 0x4a, 0x92, 0x4a, 0x92, 0x4c, 0x8c,
    0x46, 0xe3, 0x46, 0xe3, 0x3a, 0x7a, 0x34, 0x88, 0x34, 0x88, 0x2a, 0xe7};
const std::vector<uint8_t> GOLDEN_QCC_2 = {
    0xff, 0x5d, 0x00, 0x24, 0x02, 0x22, 0x58, 0x41, 0x58, 0x66, 0x58, 0x66, 0x58,
    0x98, 0x51, 0x03, 0x51, 0x03, 0x51, 0x8d, 0x4a, 0x5d, 0x4a, 0x5d, 0x4b, 0xc7,
    0x45, 0x85, 0x45, 0x85, 0x38, 0xc2, 0x31, 0xef, 0x31, 0xef, 0x36, 0xbf};

grk_image* makeImage()
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = WIDTH;
    params[c].h = HEIGHT;
    params[c].prec = PRECISION;
    params[c].sgnd = false;
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    auto& comp = image->comps[c];
    if(!comp.data)
    {
      grk_object_unref(&image->obj);
      return nullptr;
    }
    for(uint32_t y = 0; y < comp.h; ++y)
    {
      for(uint32_t x = 0; x < comp.w; ++x)
      {
        int32_t value = (int32_t)((x * 3 + y * 5 + c * 50) & 0xFF);
        if(comp.data_type == GRK_INT_16)
          static_cast<int16_t*>(comp.data)[y * comp.stride + x] = (int16_t)value;
        else
          static_cast<int32_t*>(comp.data)[y * comp.stride + x] = value;
      }
    }
  }
  return image;
}

// main header marker segments up to the first tile part, keyed by marker
std::vector<std::vector<uint8_t>> mainHeaderSegments(const std::vector<uint8_t>& codestream,
                                                     uint16_t wanted)
{
  std::vector<std::vector<uint8_t>> segments;
  size_t pos = 0;
  while(pos + 4 <= codestream.size())
  {
    uint16_t marker = (uint16_t)((codestream[pos] << 8) | codestream[pos + 1]);
    if(marker == SOC)
    {
      pos += 2;
      continue;
    }
    if(marker == SOT)
      break;
    uint16_t length = (uint16_t)((codestream[pos + 2] << 8) | codestream[pos + 3]);
    size_t end = pos + 2 + length;
    if(end > codestream.size())
      break;
    if(marker == wanted)
      segments.emplace_back(codestream.begin() + (long)pos, codestream.begin() + (long)end);
    pos = end;
  }
  return segments;
}

bool same(const char* name, const std::vector<uint8_t>& actual, const std::vector<uint8_t>& golden)
{
  if(actual == golden)
    return true;
  fprintf(stderr, "%s differs from the golden bytes\n  actual:", name);
  for(auto b : actual)
    fprintf(stderr, " %02x", b);
  fprintf(stderr, "\n  golden:");
  for(auto b : golden)
    fprintf(stderr, " %02x", b);
  fprintf(stderr, "\n");
  return false;
}

bool checkQuantizationMarkers()
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
  parameters.mct = 1;
  parameters.qfactor = QFACTOR;
  parameters.numgbits = GUARD_BITS;
  parameters.numresolution = NUM_RESOLUTIONS;
  std::vector<uint8_t> output(OUTPUT_BUFFER_BYTES);
  grk_stream_params streamParams = {};
  streamParams.buf = output.data();
  streamParams.buf_len = output.size();
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  uint64_t written = 0;
  if(!codec)
    fprintf(stderr, "grk_compress_init failed\n");
  else
  {
    written = grk_compress(codec, nullptr);
    if(!written)
      fprintf(stderr, "grk_compress failed\n");
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  if(!written)
    return false;
  output.resize(written);

  auto qcd = mainHeaderSegments(output, QCD);
  auto qcc = mainHeaderSegments(output, QCC);
  if(qcd.size() != 1 || qcc.size() != 2)
  {
    fprintf(stderr, "expected one QCD and two QCC segments, found %zu and %zu\n", qcd.size(),
            qcc.size());
    return false;
  }
  bool ok = same("QCD", qcd[0], GOLDEN_QCD);
  ok = same("QCC component 1", qcc[0], GOLDEN_QCC_1) && ok;
  ok = same("QCC component 2", qcc[1], GOLDEN_QCC_2) && ok;
  return ok;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);
  bool ok = checkQuantizationMarkers();
  grk_deinitialize();
  if(ok)
    printf("qfactor quantization markers match the reference\n");
  return ok ? 0 : 1;
}
