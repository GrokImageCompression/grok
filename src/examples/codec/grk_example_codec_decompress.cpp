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

#include <string>
#include <cstring>
#include <vector>
#include <filesystem>

#include "grok_codec.h"
#include "grk_examples_config.h"

const std::string dataRoot = GRK_DATA_ROOT;

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	//1. form vector of command line args
	std::vector<std::string> argString;
	argString.push_back("grk_example_codec_decompress");
	argString.push_back("-v");
	std::string temp = "-i " + dataRoot + std::filesystem::path::preferred_separator +
			"input" +  std::filesystem::path::preferred_separator +
			"nonregression" + std::filesystem::path::preferred_separator + "boats_cprl.j2k";
	argString.push_back(temp);
	argString.push_back("-o boats_cprl.tif");

	// 2. convert to array of c strings
	std::vector<char *> args;
	for (auto& s : argString){
	  char *arg = new char[s.size() + 1];
	  copy(s.begin(), s.end(), arg);
	  arg[s.size()] = '\0';
	  args.push_back(arg);
	}

	// 3. decompress
	int rc =  grk_codec_decompress((int)args.size(),&args[0]);

	//4. cleanup
	for (auto& s : args)
	  delete[] s;

	return rc;
}
