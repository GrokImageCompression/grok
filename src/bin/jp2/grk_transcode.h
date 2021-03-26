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

struct TranscodeInitParams {
	TranscodeInitParams() :initialized(false),
							transferExifTags(false) 	{
		pluginPath[0] = 0;
		memset(&inputFolder, 0, sizeof(inputFolder));
		memset(&outFolder, 0, sizeof(outFolder));
	}
	~TranscodeInitParams() {
		if (inputFolder.imgdirpath)
			free(inputFolder.imgdirpath);
		if (outFolder.imgdirpath)
			free(outFolder.imgdirpath);
	}
	bool initialized;
	grk_decompress_parameters parameters;
	char pluginPath[GRK_PATH_LEN];
	grk_img_fol inputFolder;
	grk_img_fol outFolder;
	bool transferExifTags;
};

class GrkTranscode{
public:
	GrkTranscode(void);
	~GrkTranscode(void);
	int main(int argc, char **argv);
	int preProcess(grk_plugin_decompress_callback_info *info);
	int postProcess(grk_plugin_decompress_callback_info *info);
private:
	int pluginMain(int argc, char **argv, TranscodeInitParams *initParams);

	// returns 0 for failure, 1 for success, and 2 if file is not suitable for decoding
	int decompress(const char *fileName, TranscodeInitParams *initParams);
	bool parsePrecision(const char *option,	grk_decompress_parameters *parameters);
	int loadImages(grk_dircnt *dirptr, char *imgdirpath);
	char nextFile(std::string file_name, grk_img_fol *inputFolder,
					grk_img_fol *outFolder, grk_decompress_parameters *parameters);
	int parseCommandLine(int argc, char **argv,TranscodeInitParams *initParams);
	uint32_t getCompressionCode(const std::string &compressionString);
	void setDefaultParams(grk_decompress_parameters *parameters);
	void destoryParams(grk_decompress_parameters *parameters);
	void printTiming(uint32_t num_images, std::chrono::duration<double> elapsed);

	bool storeToDisk;
	IImageFormat *imageFormat;
};

}
