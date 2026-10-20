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

// CIELab to sRGB conversion handed over its planes without setting owns_data

#include <cstdio>
#include <cstring>

#include "grok.h"

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <CIELab jp2>\n", argv[0]);
    return 1;
  }
  grk_initialize(nullptr, 0, nullptr);

  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", argv[1]);

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    grk_deinitialize();
    return 1;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  // PGX cannot store CIELab, so the decoder converts to sRGB
  headerInfo.decompress_fmt = GRK_FMT_PGX;
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "grk_decompress_read_header failed\n");
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    ok = image && image->numcomps == 3 && image->color_space == GRK_CLRSPC_SRGB;
    if(!ok)
      fprintf(stderr, "decoded image was not converted to three sRGB components\n");
    for(uint16_t c = 0; ok && c < image->numcomps; ++c)
    {
      const auto& comp = image->comps[c];
      if(!comp.data || !comp.owns_data)
      {
        fprintf(stderr, "component %u data %p owns_data %d after CIELab conversion\n", c, comp.data,
                (int)comp.owns_data);
        ok = false;
      }
    }
  }
  grk_object_unref(codec);
  grk_deinitialize();
  return ok ? 0 : 1;
}
