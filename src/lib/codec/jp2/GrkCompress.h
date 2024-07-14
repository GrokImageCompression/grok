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
struct CompressInitParams
{
   CompressInitParams();
   ~CompressInitParams();
   bool initialized;
   grk_cparameters parameters;
   char pluginPath[GRK_PATH_LEN];
   grk_img_fol inputFolder;
   grk_img_fol outFolder;
   bool transfer_exif_tags;
   grk_image* in_image;
   grk_stream_params* stream_;
   std::string license_;
   std::string server_;
};

class GrkCompress
{
 public:
   GrkCompress(void) = default;
   ~GrkCompress(void) = default;
   int main(int argc, char** argv, grk_image* in_image, grk_stream_params* out_buffer);

 private:
   int pluginBatchCompress(CompressInitParams* initParams);
   GrkRC pluginMain(int argc, char** argv, CompressInitParams* initParams);
   GrkRC parseCommandLine(int argc, char** argv, CompressInitParams* initParams);
   int compress(const std::string& inputFile, CompressInitParams* initParams);
};

} // namespace grk
