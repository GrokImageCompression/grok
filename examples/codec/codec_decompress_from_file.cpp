/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <string>
#include <cstring>
#include <filesystem>

#include "grok_codec.h"
#include "grk_examples_config.h"
#include "arg_converter.h"

const std::string dataRoot = GRK_DATA_ROOT;

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
  auto cvt = std::make_unique<ArgConverter>("codec_decompress_from_file");
  cvt->push("-v");
  // Define input and output file paths
  std::string inputFile = dataRoot + std::filesystem::path::preferred_separator + "input" +
                          std::filesystem::path::preferred_separator + "nonregression" +
                          std::filesystem::path::preferred_separator + "boats_cprl.j2k";
  std::string outputFile = "boats_cprl.tif";

  // If argc > 1, use the provided input file from the command line
  if(argc > 1)
  {
    inputFile = argv[1];
    outputFile = inputFile + ".tif";
  }
  cvt->push("-i", inputFile);
  cvt->push("-o", outputFile);

  // 3. Decompress
  int rc = grk_codec_decompress(cvt->argc(), cvt->argv());
  if(rc)
  {
    std::cerr << "Failed to decompress" << std::endl;
  }

  return rc;
}
