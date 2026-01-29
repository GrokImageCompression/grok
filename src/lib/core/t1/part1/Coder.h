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

#include "BlockCoder.h"
#include "ICoder.h"
#include "BlockExec.h"

namespace grk::t1::part1
{

class Coder : public ICoder
{
public:
  Coder(bool isCompressor, uint16_t maxCblkW, uint16_t maxCblkH, uint32_t cacheStrategy);
  virtual ~Coder();

  bool compress(CompressBlockExec* block) override;
  bool decompress(DecompressBlockExec* block) override;

private:
  bool preCompress(CompressBlockExec* block, uint32_t& max);
  BlockCoder* blockCoder_;
  uint32_t cacheStrategy_;
};

} // namespace grk::t1::part1
