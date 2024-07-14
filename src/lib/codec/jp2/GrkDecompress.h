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

#include "common.h"
#include "IImageFormat.h"

namespace grk
{
struct DecompressInitParams
{
   DecompressInitParams() : initialized(false), transfer_exif_tags(false)
   {
	  pluginPath[0] = 0;
	  memset(&inputFolder, 0, sizeof(inputFolder));
	  memset(&outFolder, 0, sizeof(outFolder));
	  memset(&parameters, 0, sizeof(grk_decompress_parameters));
   }
   ~DecompressInitParams()
   {
	  free(inputFolder.imgdirpath);
	  free(outFolder.imgdirpath);
   }
   bool initialized;
   grk_decompress_parameters parameters;
   char pluginPath[GRK_PATH_LEN];
   grk_img_fol inputFolder;
   grk_img_fol outFolder;
   bool transfer_exif_tags;
};

class GrkDecompress
{
 public:
   GrkDecompress(void);
   ~GrkDecompress(void);
   int main(int argc, char** argv);
   int preProcess(grk_plugin_decompress_callback_info* info);
   int postProcess(grk_plugin_decompress_callback_info* info);

 private:
   bool encodeHeader(grk_plugin_decompress_callback_info* info);
   bool encodeInit(grk_plugin_decompress_callback_info* info);
   // returns 0 for failure, 1 for success, and 2 if file is not suitable for decoding
   int decompress(const std::string& fileName, DecompressInitParams* initParams);
   GrkRC pluginMain(int argc, char** argv, DecompressInitParams* initParams);
   bool parsePrecision(const char* option, grk_decompress_parameters* parameters);
   char nextFile(const std::string& file_name, grk_img_fol* inputFolder, grk_img_fol* outFolder,
				 grk_decompress_parameters* parameters);
   GrkRC parseCommandLine(int argc, char** argv, DecompressInitParams* initParams);
   uint32_t getCompressionCode(const std::string& compressionString);
   void setDefaultParams(grk_decompress_parameters* parameters);
   void destoryParams(grk_decompress_parameters* parameters);
   void printTiming(uint32_t num_images, std::chrono::duration<double> elapsed);

   bool storeToDisk;
   IImageFormat* imageFormat;
};

} // namespace grk
