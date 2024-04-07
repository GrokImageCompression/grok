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

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
#include "test_common.h"
#include "GrkCompareDumpFiles.h"

namespace grk
{

typedef struct test_cmp_parameters
{
   /**  */
   char* base_filename;
   /**  */
   char* test_filename;
} test_cmp_parameters;

/*******************************************************************************
 * Command line help function
 *******************************************************************************/
static void compare_dump_files_help_display(void)
{
   fprintf(stdout, "\nList of parameters for the compare_dump_files function  \n");
   fprintf(stdout, "\n");
   fprintf(stdout, "  -b \t REQUIRED \t filename to the reference/baseline dump file \n");
   fprintf(stdout, "  -t \t REQUIRED \t filename to the test dump file image\n");
   fprintf(stdout, "\n");
}
/*******************************************************************************
 * Parse command line
 *******************************************************************************/
static int parse_cmdline_cmp(int argc, char** argv, test_cmp_parameters* param)
{
   size_t sizemembasefile, sizememtestfile;
   int index = 0;

   /* Init parameters */
   param->base_filename = nullptr;
   param->test_filename = nullptr;
   try
   {
	  TCLAP::CmdLine cmd("compare_dump_files command line", ' ', "");

	  TCLAP::ValueArg<std::string> baseArg("b", "base", "base file", false, "", "string", cmd);

	  TCLAP::ValueArg<std::string> testArg("t", "test", "test file", false, "", "string", cmd);

	  cmd.parse(argc, argv);

	  if(baseArg.isSet())
	  {
		 sizemembasefile = baseArg.getValue().length() + 1;
		 param->base_filename = (char*)malloc(sizemembasefile);
		 if(!param->base_filename)
		 {
			fprintf(stderr, "Out of memory");
			return 1;
		 }
		 strcpy(param->base_filename, baseArg.getValue().c_str());
		 /*printf("param->base_filename = %s [%u / %u]\n", param->base_filename,
		  * strlen(param->base_filename), sizemembasefile );*/
		 index++;
	  }

	  if(testArg.isSet())
	  {
		 sizememtestfile = testArg.getValue().length() + 1;
		 param->test_filename = (char*)malloc(sizememtestfile);
		 if(!param->test_filename)
		 {
			fprintf(stderr, "Out of memory");
			return 1;
		 }
		 strcpy(param->test_filename, testArg.getValue().c_str());
		 /*printf("param->test_filename = %s [%u / %u]\n", param->test_filename,
		  * strlen(param->test_filename), sizememtestfile);*/
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

   test_cmp_parameters inParam;
   FILE *fbase = nullptr, *ftest = nullptr;
   int same = 0;
   char lbase[512];
   char strbase[512];
   char ltest[512];
   char strtest[512];

   if(parse_cmdline_cmp(argc, argv, &inParam) == 1)
   {
	  compare_dump_files_help_display();
	  goto cleanup;
   }

   /* Display Parameters*/
   printf("******Parameters********* \n");
   printf(" base_filename = %s\n"
		  " test_filename = %s\n",
		  inParam.base_filename, inParam.test_filename);
   printf("************************* \n");

#ifdef COPY_TEST_FILES_TO_REPO
   rename(inParam.test_filename, inParam.base_filename);
#endif

   /* open base file */
   printf("Try to open: %s for reading ... ", inParam.base_filename);
   if((fbase = fopen(inParam.base_filename, "rb")) == nullptr)
   {
	  goto cleanup;
   }
   printf("Ok.\n");

   /* open test file */
   printf("Try to open: %s for reading ... ", inParam.test_filename);
   if((ftest = fopen(inParam.test_filename, "rb")) == nullptr)
   {
	  goto cleanup;
   }
   printf("Ok.\n");

   while(fgets(lbase, sizeof(lbase), fbase) && fgets(ltest, sizeof(ltest), ftest))
   {
	  int nbase = sscanf(lbase, "%511[^\r\n]", strbase);
	  int ntest = sscanf(ltest, "%511[^\r\n]", strtest);
	  assert(nbase != 511 && ntest != 511);
	  if(nbase != 1 || ntest != 1)
	  {
		 fprintf(stderr, "could not parse line from files\n");
		 goto cleanup;
	  }
	  if(strcmp(strbase, strtest) != 0)
	  {
		 fprintf(stderr, "<%s> vs. <%s>\n", strbase, strtest);
		 goto cleanup;
	  }
   }

   same = 1;
   printf("\n***** TEST SUCCEED: Files are the same. *****\n");
cleanup:
   /*Close File*/
   if(fbase)
	  fclose(fbase);
   if(ftest)
	  fclose(ftest);

   /* Free memory*/
   free(inParam.base_filename);
   free(inParam.test_filename);

   return same ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace grk
