/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */


#pragma once

#include "common.h"
#include "IImageFormat.h"

namespace grk {

struct DecompressInitParams {
	DecompressInitParams() :initialized(false),
							transferExifTags(false) 	{
		plugin_path[0] = 0;
		memset(&img_fol, 0, sizeof(img_fol));
		memset(&out_fol, 0, sizeof(out_fol));
	}
	~DecompressInitParams() {
		if (img_fol.imgdirpath)
			free(img_fol.imgdirpath);
		if (out_fol.imgdirpath)
			free(out_fol.imgdirpath);
	}
	bool initialized;
	grk_decompress_parameters parameters;
	char plugin_path[GRK_PATH_LEN];
	grk_img_fol img_fol;
	grk_img_fol out_fol;
	bool transferExifTags;
};

class GrkTranscode{
public:
	GrkTranscode(void);
	~GrkTranscode(void);
	int main(int argc, char **argv);
	int preDecompress(grk_plugin_decompress_callback_info *info);
	int postDecompress(grk_plugin_decompress_callback_info *info);
private:
	bool store_file_to_disk;
	IImageFormat *imageFormat;

	int plugin_main(int argc, char **argv, DecompressInitParams *initParams);

	// returns 0 for failure, 1 for success, and 2 if file is not suitable for decoding
	int decompress(const char *fileName, DecompressInitParams *initParams);

	bool parse_precision(const char *option,
			grk_decompress_parameters *parameters);
	int load_images(grk_dircnt *dirptr, char *imgdirpath);
	char get_next_file(std::string file_name, grk_img_fol *img_fol,
			grk_img_fol *out_fol, grk_decompress_parameters *parameters);
	int parse_cmdline_decompressor(int argc, char **argv,DecompressInitParams *initParams);
	uint32_t getCompressionCode(const std::string &compressionString);
	void set_default_parameters(grk_decompress_parameters *parameters);
	void destroy_parameters(grk_decompress_parameters *parameters);
	void print_timing(uint32_t num_images, std::chrono::duration<double> elapsed);



};


}
