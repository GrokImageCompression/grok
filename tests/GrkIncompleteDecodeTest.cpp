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

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#include "grok_codec.h"

namespace
{
bool fileExists(const std::string& path)
{
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}

// ctest runs the registered cases in parallel in one directory, so each needs its own output
std::string outputNameFor(const std::string& input, const std::string& suffix)
{
  auto slash = input.find_last_of("/\\");
  std::string base = slash == std::string::npos ? input : input.substr(slash + 1);
  auto dot = base.find_last_of('.');
  if(dot != std::string::npos)
    base.erase(dot);

  return base + suffix + ".tif";
}

int runDecompress(const std::string& input, const std::string& output)
{
  const char* argv[] = {"grk_decompress", "-i", input.c_str(), "-o", output.c_str()};
  return grk_codec_decompress(5, argv);
}

// a codestream whose tile rows never complete must not leave a file that looks decoded
bool expectTruncatedInputRejected(const std::string& input, const std::string& output)
{
  remove(output.c_str());
  int rc = runDecompress(input, output);
  bool ok = true;
  if(rc == EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: grk_decompress reported success on an incomplete decode\n", input.c_str());
    ok = false;
  }
  if(fileExists(output))
  {
    fprintf(stderr, "%s: incomplete decode left output file %s on disk\n", input.c_str(),
            output.c_str());
    ok = false;
  }
  remove(output.c_str());

  return ok;
}

bool expectCompleteInputAccepted(const std::string& input, const std::string& output)
{
  remove(output.c_str());
  int rc = runDecompress(input, output);
  bool ok = true;
  if(rc != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: grk_decompress failed on a complete codestream\n", input.c_str());
    ok = false;
  }
  if(!fileExists(output))
  {
    fprintf(stderr, "%s: complete decode produced no output file %s\n", input.c_str(),
            output.c_str());
    ok = false;
  }
  remove(output.c_str());

  return ok;
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 3)
  {
    fprintf(stderr, "Usage: %s <truncated-codestream> <complete-codestream>\n", argv[0]);
    return EXIT_FAILURE;
  }
  std::string truncatedInput = argv[1];
  std::string completeInput = argv[2];
  bool ok = expectTruncatedInputRejected(truncatedInput, outputNameFor(truncatedInput, "_partial"));
  ok = expectCompleteInputAccepted(completeInput, outputNameFor(truncatedInput, "_complete")) && ok;

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
