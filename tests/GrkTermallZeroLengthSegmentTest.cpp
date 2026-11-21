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

// a termall pass signalled with zero bytes made the MQ decoder read past mercury's coded data

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{

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

bool decode(const std::string& path, bool mercury, std::vector<int32_t>& samples)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  useMercury(mercury);
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
    {
      samples.clear();
      for(uint16_t c = 0; c < image->numcomps; ++c)
      {
        const auto& comp = image->comps[c];
        for(uint32_t y = 0; y < comp.h; ++y)
          for(uint32_t x = 0; x < comp.w; ++x)
            samples.push_back(sampleAt(comp, (uint64_t)y * comp.stride + x));
      }
      ok = true;
    }
  }
  grk_object_unref(codec);
  return ok;
}

} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <codestream>\n", argv[0]);
    return 1;
  }
  std::string path = argv[1];

  grk_initialize(nullptr, 0, nullptr);

  std::vector<int32_t> classic;
  std::vector<int32_t> mercury;
  int result = 0;
  if(!decode(path, false, classic) || !decode(path, true, mercury))
    result = 1;
  else if(classic != mercury)
  {
    fprintf(stderr, "mercury and classic decodes differ\n");
    result = 1;
  }

  grk_deinitialize();
  if(result == 0)
    printf("termall zero length segment test passed on %s\n", path.c_str());
  return result;
}
