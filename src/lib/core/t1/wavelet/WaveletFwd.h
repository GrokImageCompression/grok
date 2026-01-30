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

#pragma once

#include <cstdint>
#include "TileComponent.h"

namespace grk::t1
{
class dwt53
{
public:
  void encode_v(int32_t* res, int32_t* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                uint32_t cols);

  void encode_h(int32_t* row, int32_t* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                uint32_t rows);
};

class dwt97
{
public:
  void encode_v(float* res, float* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                uint32_t cols);

  void encode_h(float* row, float* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                uint32_t rows);
};

class WaveletFwdImpl
{
public:
  virtual ~WaveletFwdImpl() = default;
  bool compress(TileComponent* tile_comp, uint8_t qmfbid);
};

} // namespace grk::t1
