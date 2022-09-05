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

/**
 * Dump codec to file.
 *
 * Pass grk_dump command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_dump(int argc, char* argv[]);

/**
 * Compress image.
 *
 * Pass grk_compress command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_compress(int argc, char* argv[]);

/**
 * Decompress image.
 *
 * Pass grk_decompress command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_decompress(int argc, char* argv[]);


/**
 * Compare dump files
 *
 * Pass compare_dump_files command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_compare_dump_files(int argc, char* argv[]);

/**
 * Compare images
 *
 * Pass grk_compare_images command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_compare_images(int argc, char* argv[]);

/**
 * Compare raw files
 *
 * Pass grk_compare_raw_files command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_compare_raw_files(int argc, char* argv[]);

/**
 * Random tile access
 *
 * Pass grk_random_tile_access command line arguments
 *
 * @param argc
 * @param argv
 *
 * return 0 if successful
 */
GRK_API int grk_codec_random_tile_access(int argc, char* argv[]);

#ifdef __cplusplus
}
#endif
