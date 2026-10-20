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

namespace grk::t1::ht::local
{
// same arguments and output as the OpenJPH ojph_decode_codeblock functions
bool ht_decode_codeblock_avx2(uint8_t* coded_data, uint32_t* decoded_data, uint32_t missing_msbs,
                              uint32_t num_passes, uint32_t lengths1, uint32_t lengths2,
                              uint32_t width, uint32_t height, uint32_t stride, bool stripe_causal);
} // namespace grk::t1::ht::local
