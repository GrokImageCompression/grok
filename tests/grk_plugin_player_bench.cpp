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

// usage: grk_plugin_player_bench <frame.j2k> <frames> [rgb8]
// pulls one code stream through the in-memory decompress batch and prints the frame rate
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>
#include "grok.h"

namespace
{
using Clock = std::chrono::steady_clock;

struct Source
{
  std::vector<uint8_t> codestream;
  size_t frames;
  std::atomic<size_t> pulled{0};
  std::atomic<size_t> returned{0};
  std::atomic<size_t> failed{0};
  std::mutex mutex;
  Clock::time_point firstReturn;
  Clock::time_point lastReturn;
};

bool pull(void* user, const uint8_t** codestream, size_t* length, void** frameUser)
{
  auto source = (Source*)user;
  auto index = source->pulled.fetch_add(1);
  if(index >= source->frames)
    return false;
  *codestream = source->codestream.data();
  *length = source->codestream.size();
  *frameUser = (void*)(index + 1);
  return true;
}

void frameDone(void* user, void*, const grk_image* image)
{
  auto source = (Source*)user;
  auto now = Clock::now();
  if(!image)
    source->failed++;
  std::lock_guard<std::mutex> lock(source->mutex);
  if(source->returned++ == 0)
    source->firstReturn = now;
  source->lastReturn = now;
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 3)
  {
    std::fprintf(stderr, "usage: %s <frame.j2k> <frames> [rgb8]\n", argv[0]);
    return 1;
  }
  Source source;
  {
    std::ifstream file(argv[1], std::ios::binary);
    source.codestream.assign(std::istreambuf_iterator<char>(file), {});
  }
  if(source.codestream.empty())
  {
    std::fprintf(stderr, "could not read %s\n", argv[1]);
    return 1;
  }
  source.frames = (size_t)std::atol(argv[2]);
  bool rgb8 = argc > 3 && std::strcmp(argv[3], "rgb8") == 0;

  bool loaded = false;
  grk_initialize(nullptr, 0, &loaded);
  if(!loaded)
  {
    std::fprintf(stderr, "no plugin loaded, set GRK_PLUGIN_PATH\n");
    return 1;
  }
  grk_plugin_init_info init = {};
  init.device_id = 0;
  init.license = std::getenv("GRK_PLUGIN_LICENSE");
  init.server = std::getenv("GRK_PLUGIN_SERVER");
  if(!grk_plugin_init(init))
  {
    std::fprintf(stderr, "the plugin refused device 0\n");
    return 1;
  }

  grk_plugin_batch_decompress_memory_info info = {};
  info.codestream = source.codestream.data();
  info.codestream_length = source.codestream.size();
  info.pull = pull;
  info.callback = frameDone;
  info.user = &source;
  info.srgb8_output = rgb8;
  bool onDevice = false;
  info.rgb8_on_device = &onDevice;

  auto start = Clock::now();
  int32_t rc = grk_plugin_batch_decompress_memory_begin(info);
  if(rc != 0)
  {
    std::fprintf(stderr, "begin returned %d\n", (int)rc);
    return 1;
  }
  while(source.returned.load() < source.frames)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  bool drained = grk_plugin_batch_decompress_memory_end();
  auto end = Clock::now();
  grk_deinitialize();

  auto seconds = [](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
  };
  double steady = seconds(source.firstReturn, source.lastReturn);
  std::printf("%s: %zu frames, %zu failed, drained %d, rgb8 on device %d\n", argv[1],
              source.returned.load(), source.failed.load(), (int)drained, (int)onDevice);
  std::printf("wall %.2f s, first frame after %.2f s, steady %.1f fps (%.1f ms per frame)\n",
              seconds(start, end), seconds(start, source.firstReturn),
              (source.returned - 1) / steady, steady * 1000 / (source.returned - 1));
  return source.failed ? 1 : 0;
}
