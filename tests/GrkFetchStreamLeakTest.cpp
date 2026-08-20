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

// a network stream creates its fetcher before it can know whether the path is
// usable, and the fetcher already owns a worker thread and a curl handle by
// then. these two paths fail on either side of the size request, so repeating
// them shows whether both failure exits still hand those back.

#include <cstdio>
#include <cstring>

#include <dirent.h>

#include "grok.h"

namespace
{
  const uint32_t WARMUP_FAILURES = 4;
  const uint32_t MEASURED_FAILURES = 64;
  // the decoder starts no threads or files of its own on this path, so any
  // real growth is one leaked fetcher per iteration
  const long ALLOWED_GROWTH = 8;
  // the first fails while parsing the path, the second while asking a dead
  // local port for the object size
  const char* FAILING_PATHS[] = {"/vsis3/bucket-with-no-key",
                                 "/vsicurl/http://127.0.0.1:1/no-such-object.jp2"};
  const uint32_t NUM_FAILING_PATHS = sizeof(FAILING_PATHS) / sizeof(FAILING_PATHS[0]);

  void discardLog(const char*, void*) {}

  long countEntries(const char* directory)
  {
    DIR* dir = opendir(directory);
    if(!dir)
      return -1;
    long count = 0;
    while(struct dirent* entry = readdir(dir))
    {
      if(strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        count++;
    }
    closedir(dir);
    return count;
  }

  bool initFailsAsExpected(const char* path)
  {
    grk_decompress_parameters params = {};
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path);

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(codec)
    {
      grk_object_unref(codec);
      return false;
    }
    return true;
  }

  bool runFailingSetups(uint32_t repeats)
  {
    for(uint32_t i = 0; i < repeats; ++i)
    {
      for(uint32_t p = 0; p < NUM_FAILING_PATHS; ++p)
      {
        if(!initFailsAsExpected(FAILING_PATHS[p]))
        {
          fprintf(stderr, "decompress init unexpectedly succeeded for %s\n", FAILING_PATHS[p]);
          return false;
        }
      }
    }
    return true;
  }
} // namespace

int main(void)
{
  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.debug_callback = discardLog;
  handlers.trace_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);

  if(!runFailingSetups(WARMUP_FAILURES))
    return 1;

  long threadsBefore = countEntries("/proc/self/task");
  long descriptorsBefore = countEntries("/proc/self/fd");
  if(threadsBefore < 0 || descriptorsBefore < 0)
  {
    fprintf(stderr, "cannot read /proc/self, skipping\n");
    return 0;
  }

  if(!runFailingSetups(MEASURED_FAILURES))
    return 1;

  long threadsAfter = countEntries("/proc/self/task");
  long descriptorsAfter = countEntries("/proc/self/fd");
  long threadGrowth = threadsAfter - threadsBefore;
  long descriptorGrowth = descriptorsAfter - descriptorsBefore;
  uint32_t setups = MEASURED_FAILURES * NUM_FAILING_PATHS;
  printf("threads %ld -> %ld, descriptors %ld -> %ld over %u failed stream setups\n",
         threadsBefore, threadsAfter, descriptorsBefore, descriptorsAfter, setups);

  if(threadGrowth > ALLOWED_GROWTH)
  {
    fprintf(stderr, "leaked %ld threads over %u failed stream setups\n", threadGrowth, setups);
    return 1;
  }
  if(descriptorGrowth > ALLOWED_GROWTH)
  {
    fprintf(stderr, "leaked %ld file descriptors over %u failed stream setups\n", descriptorGrowth,
            setups);
    return 1;
  }

  return 0;
}
