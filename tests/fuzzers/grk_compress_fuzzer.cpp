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

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "grok.h"

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* buf, size_t len);

struct Initializer
{
  Initializer()
  {
    grk_initialize(nullptr, 0, nullptr);
  }
};

int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  static Initializer init;
  return 0;
}

/* Helper to consume bytes from the fuzz input as a simple data provider */
struct FuzzStream
{
  const uint8_t* data;
  size_t size;
  size_t pos;

  uint8_t u8()
  {
    if(pos >= size)
      return 0;
    return data[pos++];
  }
  uint16_t u16()
  {
    uint16_t v = u8();
    v |= (uint16_t)u8() << 8;
    return v;
  }
  uint32_t u32()
  {
    uint32_t v = u16();
    v |= (uint32_t)u16() << 16;
    return v;
  }
  const uint8_t* bytes(size_t n)
  {
    if(pos + n > size)
      return nullptr;
    const uint8_t* p = data + pos;
    pos += n;
    return p;
  }
  size_t remaining() const
  {
    return size - pos;
  }
};

int LLVMFuzzerTestOneInput(const uint8_t* buf, size_t len)
{
  /* Need at least a header of parameter bytes + some pixel data */
  if(len < 20)
    return 0;

  FuzzStream fs = {buf, len, 0};

  /* Derive image dimensions and component count from fuzz input.
   * Keep dimensions small to avoid excessive memory and CPU usage. */
  uint16_t numcomps = 1 + (fs.u8() % 10); /* 1-10 components */
  uint32_t width = 1 + (fs.u16() % 256); /* 1-256 */
  uint32_t height = 1 + (fs.u16() % 256); /* 1-256 */
  uint8_t prec = 1 + (fs.u8() % 16); /* 1-16 bit depth */
  bool sgnd = fs.u8() & 1;

  /* Compression parameter knobs */
  uint8_t codec_fmt_byte = fs.u8();
  uint8_t prog_order_byte = fs.u8();
  uint8_t num_res_byte = fs.u8();
  uint8_t cblk_w_byte = fs.u8();
  uint8_t cblk_h_byte = fs.u8();
  uint8_t cblk_sty_byte = fs.u8();
  uint8_t irreversible_byte = fs.u8();
  uint8_t numlayers_byte = fs.u8();
  uint8_t tile_byte = fs.u8();
  uint8_t mct_byte = fs.u8();
  uint8_t quality_mode_byte = fs.u8();
  uint8_t tile_parts_byte = fs.u8();
  uint8_t ht_byte = fs.u8();

  /* Check we still have some bytes left for pixel data */
  size_t pixels_needed = (size_t)width * height * numcomps * sizeof(int32_t);
  if(pixels_needed > 256 * 256 * 10 * sizeof(int32_t))
    return 0;

  /* Set up image components */
  grk_image_comp cmptparms[10] = {};
  for(uint16_t i = 0; i < numcomps; i++)
  {
    cmptparms[i].dx = 1;
    cmptparms[i].dy = 1;
    cmptparms[i].w = width;
    cmptparms[i].h = height;
    cmptparms[i].prec = prec;
    cmptparms[i].sgnd = sgnd;
  }

  GRK_COLOR_SPACE clrspc;
  if(numcomps >= 3)
    clrspc = GRK_CLRSPC_SRGB;
  else
    clrspc = GRK_CLRSPC_GRAY;

  grk_image* image = grk_image_new(numcomps, cmptparms, clrspc, true);
  if(!image)
    return 0;

  /* Fill pixel data from remaining fuzz bytes, zero-pad if insufficient.
   * Use stride-based indexing since the image buffer may have padding. */
  for(uint16_t c = 0; c < numcomps; c++)
  {
    auto* comp_data = (int32_t*)image->comps[c].data;
    if(!comp_data)
      goto cleanup;
    uint32_t stride = image->comps[c].stride;
    for(uint32_t row = 0; row < height; row++)
    {
      for(uint32_t col = 0; col < width; col++)
      {
        int32_t val = 0;
        if(fs.remaining() >= 2)
        {
          val = (int16_t)fs.u16();
          /* Clamp to valid range for the precision */
          if(sgnd)
          {
            int32_t lo = -(1 << (prec - 1));
            int32_t hi = (1 << (prec - 1)) - 1;
            if(val < lo)
              val = lo;
            if(val > hi)
              val = hi;
          }
          else
          {
            int32_t hi = (1 << prec) - 1;
            if(val < 0)
              val = -val;
            if(val > hi)
              val = val & hi;
          }
        }
        comp_data[row * stride + col] = val;
      }
    }
  }

  {
    /* Configure compression parameters */
    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);

    /* Output format: J2K or JP2 */
    parameters.cod_format = (codec_fmt_byte & 1) ? GRK_FMT_JP2 : GRK_FMT_J2K;

    /* Progression order */
    const GRK_PROG_ORDER prog_orders[] = {GRK_LRCP, GRK_RLCP, GRK_RPCL, GRK_PCRL, GRK_CPRL};
    parameters.prog_order = prog_orders[prog_order_byte % 5];

    /* Number of resolutions: 1-10 */
    parameters.numresolution = 1 + (num_res_byte % 10);

    /* Code block dimensions: must be power of 2 in [4,1024], and w*h <= 4096 */
    const uint32_t cblk_sizes[] = {4, 8, 16, 32, 64};
    parameters.cblockw_init = cblk_sizes[cblk_w_byte % 5];
    parameters.cblockh_init = cblk_sizes[cblk_h_byte % 5];
    /* Ensure product does not exceed 4096 */
    while(parameters.cblockw_init * parameters.cblockh_init > 4096)
      parameters.cblockh_init >>= 1;

    /* Code block style flags (bits 0-5 for classic J2K modes) */
    parameters.cblk_sty = cblk_sty_byte & 0x3F;

    /* HTJ2K mode: overrides code block style and disables rate/quality allocation */
    bool isHT = (ht_byte & 1) != 0;
    if(isHT)
    {
      parameters.cblk_sty = GRK_CBLKSTY_HT_ONLY;
      parameters.numgbits = 1;
    }

    /* Irreversible (lossy 9-7) vs reversible (lossless 5-3) */
    parameters.irreversible = (irreversible_byte & 1) != 0;

    /* Number of quality layers: 1-15 */
    parameters.numlayers = 1 + (numlayers_byte % 15);

    /* Rate/quality allocation (not supported for HTJ2K) */
    if(!isHT && (quality_mode_byte & 1))
    {
      parameters.allocation_by_quality = true;
      for(uint16_t i = 0; i < parameters.numlayers; i++)
        parameters.layer_distortion[i] = 20.0 + (double)(i * 10);
    }
    else if(!isHT && parameters.irreversible)
    {
      parameters.allocation_by_rate_distortion = true;
      for(uint16_t i = 0; i < parameters.numlayers; i++)
        parameters.layer_rate[i] = 1.0 + (double)(i + 1) * 5.0;
    }

    /* Tiling */
    if(tile_byte & 1)
    {
      parameters.tile_size_on = true;
      parameters.t_width = 16 + (tile_byte % 64);
      parameters.t_height = 16 + ((tile_byte >> 2) % 64);
    }

    /* MCT: 0 or 1 for 3+ component images */
    if(numcomps >= 3)
      parameters.mct = mct_byte & 1;
    else
      parameters.mct = 0;

    /* Tile part generation: split by Resolution, Component, or Layer */
    if(parameters.tile_size_on && (tile_parts_byte & 1))
    {
      parameters.enable_tile_part_generation = true;
      const uint8_t dividers[] = {'R', 'C', 'L'};
      parameters.new_tile_part_progression_divider = dividers[tile_parts_byte % 3];
    }

    /* Compress to a memory buffer */
    size_t out_buf_size = pixels_needed + 65536;
    auto* out_buf = (uint8_t*)malloc(out_buf_size);
    if(!out_buf)
      goto cleanup;

    grk_stream_params stream_params = {};
    stream_params.buf = out_buf;
    stream_params.buf_len = out_buf_size;

    grk_object* codec = grk_compress_init(&stream_params, &parameters, image);
    if(!codec)
    {
      free(out_buf);
      goto cleanup;
    }

    grk_compress(codec, nullptr);
    grk_object_unref(codec);
    free(out_buf);
  }

cleanup:
  grk_object_unref(&image->obj);

  return 0;
}
