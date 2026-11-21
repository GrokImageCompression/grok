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

// initClient for a decompression client must leave the queue holding the
// compressed slots the client submits, never the uncompressed ones. Filling
// them inside initClient closes the race where the client's first waitAndPop
// beat a follow-up drain-and-refill and handed out a stale uncompressed buffer.

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

#include "Messenger.h"

using namespace grk_plugin;

namespace
{
const uint32_t WIDTH = 64;
const uint32_t HEIGHT = 64;
const uint32_t SAMPLES = 3;
const size_t NUM_FRAMES = 8;
const size_t COMPRESSED_FRAME_SIZE = 36864;

// owns the shared memory segments the client maps, standing in for the server
struct ServerSegments
{
  grk_handle uncompressedHandle = 0;
  grk_handle compressedHandle = 0;
  char* uncompressed = nullptr;
  char* compressed = nullptr;
  size_t uncompressedBytes = Messenger::uncompressedFrameSize(WIDTH, HEIGHT, SAMPLES) * NUM_FRAMES;
  size_t compressedBytes = COMPRESSED_FRAME_SIZE * NUM_FRAMES;

  bool create()
  {
    return SharedMemoryManager::initShm(grokUncompressedBuf, uncompressedBytes, &uncompressedHandle,
                                        &uncompressed, true) &&
           SharedMemoryManager::initShm(grokCompressedBuf, compressedBytes, &compressedHandle,
                                        &compressed, true);
  }
  void destroy()
  {
    SharedMemoryManager::deinitShm(grokUncompressedBuf, uncompressedBytes, uncompressedHandle,
                                   &uncompressed);
    SharedMemoryManager::deinitShm(grokCompressedBuf, compressedBytes, compressedHandle,
                                   &compressed);
  }
};

MessengerInit clientInit()
{
  // no processing threads and no startThreads call, so no peer process is
  // needed: initClient is driven directly
  return MessengerInit(
      true, clientToGrokMessageBuf, clientSentSynch, grokReceiveReadySynch, grokToClientMessageBuf,
      grokSentSynch, clientReceiveReadySynch, [](const std::string&) {}, 0);
}
} // namespace

int main()
{
  setMessengerLogger(new MessengerLogger("[SHM-Init] "));

  ServerSegments segments;
  if(!segments.create())
  {
    fprintf(stderr, "could not create shared memory segments\n");
    return EXIT_FAILURE;
  }

  bool ok = true;
  {
    Messenger client(clientInit());
    if(!client.initClient(Messenger::uncompressedFrameSize(WIDTH, HEIGHT, SAMPLES),
                          COMPRESSED_FRAME_SIZE, NUM_FRAMES, true))
    {
      fprintf(stderr, "initClient failed\n");
      segments.destroy();
      return EXIT_FAILURE;
    }

    std::vector<BufferSrc> handedOut;
    BufferSrc slot;
    while(client.availableBuffers_.pop(slot))
      handedOut.push_back(slot);

    if(handedOut.size() != NUM_FRAMES)
    {
      fprintf(stderr, "queue holds %zu slots, expected %zu\n", handedOut.size(), NUM_FRAMES);
      ok = false;
    }
    std::set<size_t> frameIds;
    for(const auto& handed : handedOut)
    {
      if(handed.framePtr_ != client.getCompressedFrame(handed.frameId_))
      {
        fprintf(stderr, "slot %zu is not the compressed buffer\n", handed.frameId_);
        ok = false;
      }
      if(!frameIds.insert(handed.frameId_).second)
      {
        fprintf(stderr, "slot %zu handed out twice\n", handed.frameId_);
        ok = false;
      }
    }
  }

  segments.destroy();

  if(!ok)
  {
    fprintf(stderr, "FAIL: decompress client init handed out the wrong buffers\n");
    return EXIT_FAILURE;
  }
  printf("PASS: decompress client init handed out only compressed slots\n");
  return EXIT_SUCCESS;
}
