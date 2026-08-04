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
#include <cstring>
#include <memory>
#include <string>

#include "grk_apps_config.h"
#include "grok.h"
#include "spdlogwrapper.h"
#include "common.h"
#ifdef GROK_HAVE_LIBPNG
#include "PNGFormat.h"
#endif
#ifdef GROK_HAVE_LIBTIFF
#include "TIFFFormat.h"
#include <tiffio.h>
#endif
#include "GrkImageCanonDump.h"

// Serializes a decoded image to a canonical byte stream: component parameters,
// samples, and reader-surfaced metadata (ICC, resolution, XMP, IPTC, EXIF),
// written explicitly little-endian. Hashing this instead of the container file
// keeps md5 refs independent of the zlib implementation used by PNG/TIFF
// writers and of host endianness.

namespace grk
{

namespace
{
  struct CanonImageDeleter
  {
    void operator()(grk_image* img) const
    {
      if(img)
        grk_object_unref(&img->obj);
    }
  };
  using CanonImagePtr = std::unique_ptr<grk_image, CanonImageDeleter>;

  void put_u8(FILE* f, uint8_t v)
  {
    fwrite(&v, 1, 1, f);
  }
  void put_le32(FILE* f, uint32_t v)
  {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
  }
  void put_le64(FILE* f, uint64_t v)
  {
    put_le32(f, (uint32_t)v);
    put_le32(f, (uint32_t)(v >> 32));
  }
  void put_f64(FILE* f, double d)
  {
    uint64_t v;
    memcpy(&v, &d, sizeof(v));
    put_le64(f, v);
  }
  void put_block(FILE* f, const uint8_t* buf, uint64_t len)
  {
    put_le64(f, buf ? len : 0);
    if(buf && len)
      fwrite(buf, 1, len, f);
  }

  bool writeCanonical(const grk_image* image, const char* outPath)
  {
    auto f = fopen(outPath, "wb");
    if(!f)
    {
      spdlog::error("image_canon_dump: cannot open {} for writing", outPath);
      return false;
    }
    fwrite("GRKCANON1", 1, 9, f);
    put_le32(f, image->numcomps);
    put_le32(f, (uint32_t)image->color_space);
    put_u8(f, image->has_capture_resolution ? 1 : 0);
    put_f64(f, image->capture_resolution[0]);
    put_f64(f, image->capture_resolution[1]);
    if(image->meta)
    {
      put_block(f, image->meta->color.icc_profile_buf, image->meta->color.icc_profile_len);
      put_block(f, image->meta->iptc_buf, image->meta->iptc_len);
      put_block(f, image->meta->xmp_buf, image->meta->xmp_len);
      put_block(f, image->meta->exif_buf, image->meta->exif_len);
    }
    else
    {
      for(int i = 0; i < 4; ++i)
        put_le64(f, 0);
    }
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto& comp = image->comps[c];
      put_le32(f, comp.dx);
      put_le32(f, comp.dy);
      put_le32(f, comp.w);
      put_le32(f, comp.h);
      put_u8(f, comp.prec);
      put_u8(f, comp.sgnd ? 1 : 0);
      for(uint32_t y = 0; y < comp.h; ++y)
      {
        auto row = (const int32_t*)comp.data + (size_t)y * comp.stride;
        for(uint32_t x = 0; x < comp.w; ++x)
          put_le32(f, (uint32_t)row[x]);
      }
    }
    bool ok = fclose(f) == 0;
    if(!ok)
      spdlog::error("image_canon_dump: failed to write {}", outPath);
    return ok;
  }

  CanonImagePtr readCanonicalInput(const std::string& filename)
  {
    grk_cparameters parameters{};
    grk_compress_set_default_params(&parameters);
    auto ext = filename.substr(filename.find_last_of('.') + 1);
    for(auto& ch : ext)
      ch = (char)tolower(ch);
    if(ext == "png")
    {
#ifdef GROK_HAVE_LIBPNG
      parameters.decod_format = GRK_FMT_PNG;
      PNGFormat<int32_t> png;
      return CanonImagePtr(png.readImage(filename, &parameters));
#else
      spdlog::error("image_canon_dump: PNG support not compiled in");
      return CanonImagePtr(nullptr);
#endif
    }
    else if(ext == "tif" || ext == "tiff")
    {
#ifdef GROK_HAVE_LIBTIFF
      TIFFSetWarningHandler(nullptr);
      TIFFSetErrorHandler(nullptr);
      parameters.decod_format = GRK_FMT_TIF;
      TIFFFormat<int32_t> tif;
      return CanonImagePtr(tif.readImage(filename, &parameters));
#else
      spdlog::error("image_canon_dump: TIFF support not compiled in");
      return CanonImagePtr(nullptr);
#endif
    }
    spdlog::error("image_canon_dump: unsupported extension .{}", ext);
    return CanonImagePtr(nullptr);
  }
} // namespace

int GrkImageCanonDump::main(int argc, const char* argv[])
{
  if(argc != 3)
  {
    fprintf(stderr, "usage: image_canon_dump <input image> <output dump>\n");
    return 1;
  }
  auto image = readCanonicalInput(argv[1]);
  if(!image)
    return 1;
  return writeCanonical(image.get(), argv[2]) ? 0 : 1;
}

} // namespace grk
