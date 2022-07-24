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

#include "grok_codec.h"
#include "GrkDump.h"
#include "GrkDecompress.h"
#include "GrkCompress.h"
#include "GrkCompareDumpFiles.h"
#include "GrkCompareImages.h"
#include "GrkCompareRawFiles.h"
#include "GrkRandomTileAccess.h"
#include "GrkTestTileEncoder.h"
#include "GrkTestTileDecoder.h"

GRK_API int grk_dump_main(int argc, char* argv[]){
	grk::GrkDump prog;
	return prog.main(argc, argv);
}
GRK_API int grk_compress_main(int argc, char* argv[]){
	grk::GrkCompress prog;
	return prog.main(argc, argv);
}
GRK_API int grk_decompress_main(int argc, char* argv[]){
	grk::GrkDecompress prog;
	return prog.main(argc, argv);
}
GRK_API int grk_compare_dump_files_main(int argc, char* argv[]){
	grk::GrkCompareDumpFiles prog;
	return prog.main(argc, argv);
}
GRK_API int grk_compare_images_main(int argc, char* argv[]){
	grk::GrkCompareImages prog;
	return prog.main(argc, argv);
}
GRK_API int grk_compare_raw_files_main(int argc, char* argv[]){
	grk::GrkCompareRawFiles prog;
	return prog.main(argc, argv);
}
GRK_API int grk_random_tile_access_main(int argc, char* argv[]){
	grk::GrkRandomTileAccess prog;
	return prog.main(argc, argv);
}
GRK_API int grk_test_tile_encoder_main(int argc, char* argv[]){
	grk::GrkTestTileEncoder prog;
	return prog.main(argc, argv);
}
GRK_API int grk_test_tile_decoder_main(int argc, char* argv[]){
	grk::GrkTestTileDecoder prog;
	return prog.main(argc, argv);
}
