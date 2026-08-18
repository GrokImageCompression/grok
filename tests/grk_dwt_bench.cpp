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
 */

// A/B benchmark of the inverse DWT implementations: grok's classic kernels
// (grk_bench_dwt_* hooks) vs mercury's streaming synthesis engine
// (mercury_bench_dwt). Single-threaded, synthetic data, whole pyramids.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" double grk_bench_dwt_97(uint32_t width, uint32_t height, uint8_t numres, uint32_t iters);
extern "C" double grk_bench_dwt_16_97(uint32_t width, uint32_t height, uint8_t numres,
                                      uint32_t iters);
extern "C" double grk_bench_dwt_53(uint32_t width, uint32_t height, uint8_t numres, uint32_t iters);
extern "C" double grk_bench_dwt_16_53(uint32_t width, uint32_t height, uint8_t numres,
                                      uint32_t iters);
// present only when the library was built with mercury (cargo available)
#if defined(__GNUC__) && !defined(_WIN32)
extern "C" double mercury_bench_dwt(int32_t kind, int32_t width, int32_t height, int32_t levels,
                                    int32_t iters) __attribute__((weak));
#else
static double (*mercury_bench_dwt)(int32_t, int32_t, int32_t, int32_t, int32_t) = nullptr;
#endif

static void formatRate(char* buf, size_t len, double megapixels, double seconds)
{
  if(seconds > 0)
    snprintf(buf, len, "%.0f", megapixels / seconds);
  else
    snprintf(buf, len, "n/a");
}

int main(int argc, char** argv)
{
  uint32_t iters = 7;
  if(argc > 1)
    iters = (uint32_t)atoi(argv[1]);
  const uint8_t numres = 6; // 5 transform levels, the codec default
  const uint32_t sizes[][2] = {{2048, 2048}, {4096, 4096}, {8192, 8192}};

  printf("inverse DWT, single thread, %u resolutions, best of %u runs, megapixels/s\n", numres,
         iters);
  printf("%-11s %-8s %12s %12s\n", "size", "lane", "classic", "mercury");
  for(auto& wh : sizes)
  {
    uint32_t w = wh[0], h = wh[1];
    double mp = (double)w * h / 1e6;
    char sizeStr[24];
    snprintf(sizeStr, sizeof(sizeStr), "%ux%u", w, h);
    struct Lane
    {
      const char* name;
      double (*classic)(uint32_t, uint32_t, uint8_t, uint32_t);
      int32_t mercuryKind; // -1 = no mercury counterpart
    } lanes[] = {
        {"9/7 f32", grk_bench_dwt_97, 0},
        {"9/7 i16", grk_bench_dwt_16_97, -1},
        {"5/3 i32", grk_bench_dwt_53, 2},
        {"5/3 i16", grk_bench_dwt_16_53, 1},
    };
    for(auto& lane : lanes)
    {
      char cbuf[32], mbuf[32];
      formatRate(cbuf, sizeof(cbuf), mp, lane.classic(w, h, numres, iters));
      if(lane.mercuryKind >= 0 && mercury_bench_dwt)
        formatRate(mbuf, sizeof(mbuf), mp,
                   mercury_bench_dwt(lane.mercuryKind, (int32_t)w, (int32_t)h, numres - 1,
                                     (int32_t)iters));
      else
        snprintf(mbuf, sizeof(mbuf), "-");
      printf("%-11s %-8s %12s %12s\n", sizeStr, lane.name, cbuf, mbuf);
    }
  }
  return 0;
}
