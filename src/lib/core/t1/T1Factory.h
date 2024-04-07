/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

#include "T1Interface.h"

namespace grk
{
class T1Factory
{
 public:
   static T1Interface* makeT1(bool isCompressor, TileCodingParams* tcp, uint32_t maxCblkW,
							  uint32_t maxCblkH);
   static Quantizer* makeQuantizer(bool ht, bool reversible, uint8_t guardBits);
};

} // namespace grk
