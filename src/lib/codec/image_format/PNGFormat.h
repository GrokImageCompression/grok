/*
 *    Copyright (C) 2016 Grok Image Compression Inc.
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
#pragma once

#include "ImageFormat.h"
#include <png.h>
#include <string>

void pngSetVerboseFlag(bool verbose);

class PNGFormat : public ImageFormat
{
public:
  PNGFormat();
  bool encodeHeader(void) override;
  bool encodePixels() override;
  using ImageFormat::encodePixels;
  bool encodeFinish(void) override;
  grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

private:
  grk_image* do_decode(grk_cparameters* params);

  png_infop info_;
  png_structp png;
  uint8_t* row_buf;
  uint8_t** row_buf_array;
  int32_t* row32s;
  GRK_COLOR_SPACE colorSpace_;
  uint8_t prec;
  uint16_t nr_comp;
};
