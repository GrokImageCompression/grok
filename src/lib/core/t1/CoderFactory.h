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

#include "ICoder.h"

namespace grk::t1
{
class CoderFactory
{
public:
  static ICoder* makeCoder(bool isHT, bool isCompressor, uint16_t maxCblkW, uint16_t maxCblkH,
                           uint32_t cacheStrategy);
  static Quantizer* makeQuantizer(bool ht, bool reversible, uint8_t guardBits);
};

} // namespace grk::t1
