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

#include <memory>
#include <string>
#include <filesystem>
#include "test_common.h"
#include <CLI/CLI.hpp>
#include "grk_apps_config.h"
#include "spdlog/spdlog.h"
#include "GrkCompareRawFiles.h"

namespace grk
{

namespace fs = std::filesystem;

struct TestCmpParameters
{
  std::string base_filename;
  std::string test_filename;
};

// RAII wrapper for FILE*
struct FileCloser
{
  void operator()(FILE* file) const
  {
    if(file)
    {
      fclose(file);
    }
  }
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;

static void compare_raw_files_help_display()
{
  std::cout << "\nList of parameters for the compare_raw_files utility\n\n"
            << "-b  REQUIRED  Reference/baseline RAW image file\n"
            << "-t  REQUIRED  Test RAW image file\n"
            << "\n";
}

static int parse_cmdline_cmp(int argc, char** argv, TestCmpParameters& param)
{
  CLI::App app{"compare_raw_files command line"};

  app.add_option("-b,--base", param.base_filename, "Base file")
      ->required()
      ->check(CLI::ExistingFile);
  app.add_option("-t,--test", param.test_filename, "Test file")
      ->required()
      ->check(CLI::ExistingFile);

  try
  {
    app.parse(argc, argv);
  }
  catch(const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  return 0;
}

int GrkCompareRawFiles::main(int argc, char** argv)
{
#ifndef NDEBUG
  std::string out;
  for(int i = 0; i < argc; ++i)
  {
    out += std::string(" ") + argv[i];
  }
  out += "\n";
  spdlog::debug(out);
#endif

  TestCmpParameters params;
  if(parse_cmdline_cmp(argc, argv, params) != 0)
  {
    compare_raw_files_help_display();
    return EXIT_FAILURE;
  }

  // Log parameters
  spdlog::info("******Parameters*********");
  spdlog::info("Base_filename = {}", params.base_filename);
  spdlog::info("Test_filename = {}", params.test_filename);

#ifdef COPY_TEST_FILES_TO_REPO
  if(!fs::exists(params.base_filename))
  {
    try
    {
      fs::rename(params.test_filename, params.base_filename);
    }
    catch(const fs::filesystem_error& e)
    {
      spdlog::error("Failed to rename test file to base file: {}", e.what());
      return EXIT_FAILURE;
    }
  }
#endif

  // Open files with RAII
  FilePtr file_base(fopen(params.base_filename.c_str(), "rb"));
  if(!file_base)
  {
    spdlog::error("Failed to open base file for reading: {}", params.base_filename);
    return EXIT_FAILURE;
  }

  FilePtr file_test(fopen(params.test_filename.c_str(), "rb"));
  if(!file_test)
  {
    spdlog::error("Failed to open test file for reading: {}", params.test_filename);
    return EXIT_FAILURE;
  }

  // Compare files byte by byte
  uint32_t pos = 0;
  while(true)
  {
    uint8_t value_test = 0;
    uint8_t value_base = 0;
    bool eof_test = (fread(&value_test, 1, 1, file_test.get()) != 1);
    bool eof_base = (fread(&value_base, 1, 1, file_base.get()) != 1);

    if(eof_test && eof_base)
    {
      // Both files reached EOF simultaneously
      break;
    }

    if(eof_test != eof_base)
    {
      spdlog::error("Files have different sizes at position {}", pos);
      return EXIT_FAILURE;
    }

    if(value_test != value_base)
    {
      spdlog::error("Binary values differ at position {}: 0x{:02x} vs 0x{:02x}", pos, value_test,
                    value_base);
      return EXIT_FAILURE;
    }
    pos++;
  }

  spdlog::info("---- TEST SUCCEEDED: Files are identical ----");
  return EXIT_SUCCESS;
}

} // namespace grk