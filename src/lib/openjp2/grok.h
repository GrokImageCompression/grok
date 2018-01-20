/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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

#include "openjpeg.h"

namespace grk {

// 64 Giga Pixels
const uint64_t max_tile_area = 67108864000;
const uint32_t max_supported_precision = 16; // maximum supported precision for Grok library
const uint32_t max_precision_jpeg_2000 = 38; // maximum number of magnitude bits, according to ISO standard
const uint32_t max_num_components = 16384;	// maximum allowed number components


const uint32_t default_numbers_segments = 10;
const uint32_t  stream_chunk_size = 0x100000;
const uint32_t  default_header_size = 1000;
const uint32_t  default_number_mcc_records = 10;
const uint32_t  default_number_mct_records = 10;

//exceptions
class PluginDecodeUnsupportedException : public std::exception {
};

}