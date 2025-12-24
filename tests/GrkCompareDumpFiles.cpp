/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
#include <iostream>
#include <CLI/CLI.hpp>
#include "grk_apps_config.h"
#include "spdlog/spdlog.h"
#include "GrkCompareDumpFiles.h"

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

static void compare_dump_files_help_display()
{
  std::cout << "\nList of parameters for the compare_dump_files utility\n\n"
            << "-b  REQUIRED  Reference/baseline dump file\n"
            << "-t  REQUIRED  Test dump file\n"
            << "\n";
}

static int parse_cmdline_cmp(int argc, char** argv, TestCmpParameters& param)
{
  CLI::App app{"compare_dump_files command line"};

  app.add_option("-b,--base", param.base_filename, "Base file")->required();
  app.add_option("-t,--test", param.test_filename, "Test file")->required();

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

int GrkCompareDumpFiles::main(int argc, char** argv)
{
#ifndef NDEBUG
  std::string out;
  for(int i = 0; i < argc; ++i)
  {
    out += std::string(" ") + argv[i];
  }
  out += "\n";
  printf("%s", out.c_str());
#endif

  TestCmpParameters params;
  if(parse_cmdline_cmp(argc, argv, params) != 0)
  {
    compare_dump_files_help_display();
    return EXIT_FAILURE;
  }

  // Log parameters using spdlog
  spdlog::info("******Parameters*********");
  spdlog::info("Base_filename = {}", params.base_filename);
  spdlog::info("Test_filename = {}", params.test_filename);

#ifdef COPY_TEST_FILES_TO_REPO
  if(!fs::exists(params.base_filename))
  {
    fs::rename(params.test_filename, params.base_filename);
  }
#endif

  // Open files with RAII
  spdlog::info("Try to open: {} for reading ...", params.base_filename);
  FilePtr fbase(fopen(params.base_filename.c_str(), "rb"));
  if(!fbase)
  {
    spdlog::error("Failed to open base file: {}", params.base_filename);
    return EXIT_FAILURE;
  }
  spdlog::info("Ok");

  spdlog::info("Try to open: {} for reading ...", params.test_filename);
  FilePtr ftest(fopen(params.test_filename.c_str(), "rb"));
  if(!ftest)
  {
    spdlog::error("Failed to open test file: {}", params.test_filename);
    return EXIT_FAILURE;
  }
  spdlog::info("Ok");

  // Compare files line by line
  char lbase[512];
  char ltest[512];
  while(fgets(lbase, sizeof(lbase), fbase.get()) && fgets(ltest, sizeof(ltest), ftest.get()))
  {
    // Remove trailing newlines
    std::string base_str(lbase);
    std::string test_str(ltest);
    base_str.erase(std::remove(base_str.begin(), base_str.end(), '\n'), base_str.end());
    base_str.erase(std::remove(base_str.begin(), base_str.end(), '\r'), base_str.end());
    test_str.erase(std::remove(test_str.begin(), test_str.end(), '\n'), test_str.end());
    test_str.erase(std::remove(test_str.begin(), test_str.end(), '\r'), test_str.end());

    if(base_str != test_str)
    {
      spdlog::error("Mismatch found:");
      spdlog::error("Base: <{}>", base_str);
      spdlog::error("Test: <{}>", test_str);
      return EXIT_FAILURE;
    }
  }

  // Check if one file has more lines than the other
  if(fgets(lbase, sizeof(lbase), fbase.get()) || fgets(ltest, sizeof(ltest), ftest.get()))
  {
    spdlog::error("Files have different number of lines");
    return EXIT_FAILURE;
  }

  spdlog::info("***** TEST SUCCEEDED: Files are identical *****");
  return EXIT_SUCCESS;
}

} // namespace grk