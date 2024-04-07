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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 */

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
#include "test_common.h"
#include "GrkCompareRawFiles.h"

namespace grk
{

struct test_cmp_parameters
{
   char base_filename[4096];
   char test_filename[4096];
};

static void compare_raw_files_help_display(void)
{
   fprintf(stdout, "\nList of parameters for the compare_raw_files function  \n");
   fprintf(stdout, "\n");
   fprintf(stdout, "  -b \t REQUIRED \t filename to the reference/baseline RAW image \n");
   fprintf(stdout, "  -t \t REQUIRED \t filename to the test RAW image\n");
   fprintf(stdout, "\n");
}
static int parse_cmdline_cmp(int argc, char** argv, test_cmp_parameters* param)
{
   int index = 0;
   try
   {
	  TCLAP::CmdLine cmd("compare_raw_files command line", ' ', "");

	  TCLAP::ValueArg<std::string> baseArg("b", "base", "base file", false, "", "string", cmd);
	  TCLAP::ValueArg<std::string> testArg("t", "test", "test file", false, "", "string", cmd);
	  cmd.parse(argc, argv);
	  if(baseArg.isSet())
	  {
		 strcpy(param->base_filename, baseArg.getValue().c_str());
		 index++;
	  }
	  if(testArg.isSet())
	  {
		 strcpy(param->test_filename, testArg.getValue().c_str());
		 index++;
	  }
   }
   catch(const TCLAP::ArgException& e) // catch any exceptions
   {
	  std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
	  return 0;
   }

   return index;
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
   printf("%s", out.c_str());
#endif
   int pos = 0;
   test_cmp_parameters inParam;
   FILE *file_test = nullptr, *file_base = nullptr;
   bool equal = false;
   if(parse_cmdline_cmp(argc, argv, &inParam) == 1)
   {
	  compare_raw_files_help_display();
	  goto cleanup;
   }

#ifdef COPY_TEST_FILES_TO_REPO
   rename(inParam.test_filename, inParam.base_filename);
#endif

   file_test = fopen(inParam.test_filename, "rb");
   if(!file_test)
   {
	  fprintf(stderr, "Failed to open %s for reading !!\n", inParam.test_filename);
	  goto cleanup;
   }
   file_base = fopen(inParam.base_filename, "rb");
   if(!file_base)
   {
	  fprintf(stderr, "Failed to open %s for reading !!\n", inParam.base_filename);
	  goto cleanup;
   }
   equal = true;
   while(equal)
   {
	  bool value_test = false;
	  bool eof_test = false;
	  bool value_base = false;
	  bool eof_base = false;

	  if(!fread(&value_test, 1, 1, file_test))
		 eof_test = true;
	  if(!fread(&value_base, 1, 1, file_base))
		 eof_base = true;
	  if(eof_test && eof_base)
		 break;

	  /* End of file reached only by one file?*/
	  if(eof_test || eof_base)
	  {
		 fprintf(stdout, "Files have different sizes.\n");
		 equal = false;
	  }
	  if(value_test != value_base)
	  {
		 fprintf(stdout,
				 "Binary values read in the file are different %x vs %x at "
				 "position %u.\n",
				 value_test, value_base, pos);
		 equal = false;
	  }
	  pos++;
   }
   if(equal)
	  fprintf(stdout, "---- TEST SUCCEED: Files are equal ----\n");
cleanup:
   if(file_test)
	  fclose(file_test);
   if(file_base)
	  fclose(file_base);

   return equal ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace grk
