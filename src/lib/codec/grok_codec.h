/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

#include "grok.h"

#ifdef __cplusplus
extern "C" {
#endif

GRK_API int grk_codec_dump(int argc, char* argv[]);
GRK_API int grk_codec_compress(int argc, char* argv[]);
GRK_API int grk_codec_decompress(int argc, char* argv[]);
GRK_API int grk_codec_compare_dump_files(int argc, char* argv[]);
GRK_API int grk_codec_compare_images(int argc, char* argv[]);
GRK_API int grk_codec_compare_raw_files(int argc, char* argv[]);
GRK_API int grk_codec_random_tile_access(int argc, char* argv[]);
GRK_API int grk_codec_test_tile_encoder(int argc, char* argv[]);
GRK_API int grk_codec_test_tile_decoder(int argc, char* argv[]);

#ifdef __cplusplus
}
#endif
