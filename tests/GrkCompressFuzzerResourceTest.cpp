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

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "GrkPeakResidentBytes.h"

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static const size_t maximum_peak_resident_bytes = 384ULL * 1024ULL * 1024ULL;

int main(int argc, char** argv)
{
  if(argc != 2)
  {
    fprintf(stderr, "usage: %s <fuzzer input>\n", argv[0]);
    return 1;
  }

  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> data(std::istreambuf_iterator<char>(input), {});
  if(data.empty())
  {
    fprintf(stderr, "could not read %s\n", argv[1]);
    return 1;
  }

  LLVMFuzzerInitialize(&argc, &argv);
  if(LLVMFuzzerTestOneInput(data.data(), data.size()) != 0)
    return 1;

  auto peakResidentMemory = peakResidentBytes();
  printf("compress fuzzer peak rss %zu bytes on %s\n", peakResidentMemory, argv[1]);
  if(peakResidentMemory > maximum_peak_resident_bytes)
  {
    fprintf(stderr, "peak rss %zu bytes exceeds %zu bytes\n", peakResidentMemory,
            maximum_peak_resident_bytes);
    return 1;
  }

  return 0;
}
