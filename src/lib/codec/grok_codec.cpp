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
#include "grok_codec.h"
#include "GrkDump.h"
#include "GrkDecompress.h"
#include "GrkCompress.h"
#include "GrkCompareImages.h"

int GRK_CALLCONV grk_codec_dump(int argc, char* argv[])
{
   return grk::GrkDump().main(argc, argv);
}
int GRK_CALLCONV grk_codec_compress(int argc, char* argv[], grk_image* in_image,
									grk_stream_params* stream)
{
   return grk::GrkCompress().main(argc, argv, in_image, stream);
}
int GRK_CALLCONV grk_codec_decompress(int argc, char* argv[])
{
   return grk::GrkDecompress().main(argc, argv);
}
int GRK_CALLCONV grk_codec_compare_images(int argc, char* argv[])
{
   return grk::GrkCompareImages().main(argc, argv);
}
