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

// a mapped read stream detects the format at its initial offset, and detection
// reads 22 bytes from there. an offset closer than that to the end of the file
// sends the read past the mapping, but the kernel usually parks another mapping
// right behind it, so the read picks up whatever is there and nothing goes
// wrong. this test maps the file with a blocked page behind it, which turns
// that read into the fault it deserves.

#include <cstdio>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include "grok.h"

namespace
{
  const size_t NUM_IDENTIFIER_BYTES = 22;
  const uint8_t JP2_RFC3745_SIGNATURE[] = {0x00, 0x00, 0x00, 0x0c, 0x6a, 0x50,
                                           0x20, 0x20, 0x0d, 0x0a, 0x87, 0x0a};
  const size_t SIGNATURE_LENGTH = sizeof(JP2_RFC3745_SIGNATURE);
  const uint8_t JP2_BRAND[] = {0x6a, 0x70};
  const size_t BRAND_OFFSET = 20;
  const uint8_t FILLER_BYTE = 0xa5;

  void discardLog(const char*, void*) {}

  size_t pageLength(void)
  {
    return (size_t)sysconf(_SC_PAGESIZE);
  }

  // fills a whole number of pages, so the mapping ends exactly where the
  // blocked page begins
  bool writePagedFile(const std::string& path, size_t length, size_t identifierOffset,
                      bool withBrand)
  {
    std::string contents(length, (char)FILLER_BYTE);
    memcpy(&contents[identifierOffset], JP2_RFC3745_SIGNATURE, SIGNATURE_LENGTH);
    if(withBrand)
      memcpy(&contents[identifierOffset + BRAND_OFFSET], JP2_BRAND, sizeof(JP2_BRAND));

    FILE* file = fopen(path.c_str(), "wb");
    if(!file)
      return false;
    bool written = fwrite(contents.data(), 1, length, file) == length;
    fclose(file);
    return written;
  }

  grk_object* initAtOffset(const std::string& path, size_t offset)
  {
    grk_decompress_parameters params = {};
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    streamParams.initial_offset = offset;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    return grk_decompress_init(&streamParams, &params);
  }
} // namespace

using MapFunction = void* (*)(void*, size_t, int, int, int, off_t);

static MapFunction systemMap(const char* name)
{
  return (MapFunction)dlsym(RTLD_NEXT, name);
}

// the decoder maps a file for reading with exactly these arguments, and this
// is the only place in the test that does, so every such mapping gets the
// blocked page. anything else is passed straight through.
static void* mapWithBlockedPage(MapFunction system, void* addr, size_t length, int prot, int flags,
                                int fd, off_t offset)
{
  bool decoderReadMapping = !addr && fd >= 0 && prot == PROT_READ && (flags & MAP_SHARED);
  if(!system)
    return MAP_FAILED;
  if(!decoderReadMapping)
    return system(addr, length, prot, flags, fd, offset);

  size_t page = pageLength();
  size_t reserved = ((length + page - 1) / page) * page + page;
  void* region = system(nullptr, reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(region == MAP_FAILED)
    return system(addr, length, prot, flags, fd, offset);

  return system(region, length, prot, flags | MAP_FIXED, fd, offset);
}

extern "C" void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  static MapFunction system = systemMap("mmap");
  return mapWithBlockedPage(system, addr, length, prot, flags, fd, offset);
}

extern "C" void* mmap64(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  static MapFunction system = systemMap("mmap64");
  return mapWithBlockedPage(system, addr, length, prot, flags, fd, offset);
}

int main(void)
{
  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.debug_callback = discardLog;
  handlers.trace_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);

  size_t length = pageLength();
  std::string truncatedPath = "grk_mapped_offset_truncated.bin";
  std::string exactPath = "grk_mapped_offset_exact.bin";

  // the signature sits flush against the end of the file, so detection reading
  // from this offset has to go past it to find the brand
  size_t truncatedOffset = length - SIGNATURE_LENGTH;
  if(!writePagedFile(truncatedPath, length, truncatedOffset, false))
  {
    fprintf(stderr, "cannot write %s\n", truncatedPath.c_str());
    return 1;
  }
  if(!writePagedFile(exactPath, length, length - NUM_IDENTIFIER_BYTES, true))
  {
    fprintf(stderr, "cannot write %s\n", exactPath.c_str());
    return 1;
  }

  int status = 0;

  grk_object* codec = initAtOffset(truncatedPath, truncatedOffset);
  if(codec)
  {
    fprintf(stderr, "stream setup succeeded with only %zu bytes left after offset %zu\n",
            SIGNATURE_LENGTH, truncatedOffset);
    grk_object_unref(codec);
    status = 1;
  }

  // a whole identifier is left here, which is all detection reads
  codec = initAtOffset(exactPath, length - NUM_IDENTIFIER_BYTES);
  if(!codec)
  {
    fprintf(stderr, "stream setup failed with a full identifier at offset %zu\n",
            length - NUM_IDENTIFIER_BYTES);
    status = 1;
  }
  else
  {
    grk_object_unref(codec);
  }

  remove(truncatedPath.c_str());
  remove(exactPath.c_str());
  if(status == 0)
    printf("offsets within %zu bytes of the file end are rejected\n", NUM_IDENTIFIER_BYTES);

  return status;
}
