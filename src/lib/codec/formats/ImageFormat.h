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

#pragma once

#include <algorithm>
#include <functional>
#include <future>
#include <mutex>
#include <vector>

#include "FileStandardIO.h"
#include "IImageFormat.h"
#include "IFileIO.h"
#include "BufferPool.h"
#include "FileOrchestratorIO.h"
#include "spdlogwrapper.h"

const uint32_t reclaimSize = 5;

template<typename ReadSamples>
bool readSamplesAs(grk_data_type dataType, ReadSamples readSamples)
{
  switch(dataType)
  {
    case GRK_INT_16:
      return readSamples(int16_t{});
    case GRK_INT_32:
      return readSamples(int32_t{});
    default:
      spdlog::error("image readers support only 16 and 32 bit samples, not data type {}",
                    (int)dataType);
      return false;
  }
}

// 64 rows encoded fastest against 16, 256 and 1024 on 5 and 120 megapixel RGB PNM
const uint32_t stripReadRows = 64;

template<typename T, typename ReadStrip>
bool pushStripsReadAhead(grk_object* codec, uint16_t numcomps, uint32_t width, uint32_t height,
                         uint32_t rowsPerStrip, ReadStrip readStrip)
{
  uint32_t strips = (height + rowsPerStrip - 1) / rowsPerStrip;
  std::vector<std::vector<T>> stripPlanes[2];
  for(auto& planes : stripPlanes)
    planes.assign(numcomps, std::vector<T>((size_t)rowsPerStrip * width));
  if(!readStrip(0, stripPlanes[0]))
    return false;
  std::vector<const void*> planePointers(numcomps);
  for(uint32_t strip = 0; strip < strips; ++strip)
  {
    auto& planes = stripPlanes[strip % 2];
    std::future<bool> nextRead;
    if(strip + 1 < strips)
      nextRead = std::async(std::launch::async, readStrip, strip + 1,
                            std::ref(stripPlanes[(strip + 1) % 2]));
    for(uint16_t c = 0; c < numcomps; ++c)
      planePointers[c] = planes[c].data();
    uint32_t rows = (std::min)(rowsPerStrip, height - strip * rowsPerStrip);
    grk_strip pushed = {rows, planePointers.data(), width};
    bool pushOk = grk_compress_push_strip(codec, &pushed);
    bool readOk = !nextRead.valid() || nextRead.get();
    if(!pushOk || !readOk)
      return false;
  }
  return true;
}

class ImageFormat : public IImageFormat
{
public:
  ImageFormat();
  virtual ~ImageFormat() override;
  ImageFormat(const ImageFormat&) = delete;
  ImageFormat& operator=(const ImageFormat&) = delete;
  virtual void registerReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
                                       void* user_data) override;
  void ioReclaimBuffer(uint32_t workerId, grk_io_buf buffer);
  void reclaim(uint32_t workerId, grk_io_buf pixels);
  virtual bool writeInit(grk_image* image, const std::string& filename, uint32_t compression_level,
                         uint32_t concurrency) override;
  virtual void setImage(grk_image* image) override;
  bool supportsIncrementalBandWrite(void) const override;
  virtual bool writeStrip(uint32_t workerId, grk_io_buf pixels) override;
  virtual bool writeFinish(void) override;
  uint32_t getWriteState(void) override;
  bool openFile(void);
  bool useStdIO(void);

protected:
  void applicationOrchestratedReclaim(GrkIOBuf buf);
  /***
   * Common core pixel encoding
   */
  virtual bool writeStripCore(uint32_t workerId, grk_io_buf pixels);
  /***
   * Common core pixel encoding write to disk
   */
  virtual bool writeStripToDisk(grk_io_buf pixels);
  bool open(const std::string& fname, const std::string& mode);
  uint64_t write(GrkIOBuf buffer);
  bool read(uint8_t* buf, size_t len);
  bool seek(int64_t pos, int whence);
  uint32_t maxY(uint32_t rows);
  int getMode(const char* mode);

  void allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries);
  void copyICC(grk_image* dest, const uint8_t* iccbuf, uint32_t icclen);
  void createMeta(grk_image* img);
  bool allComponentsSanityCheck(const grk_image* image, bool equalPrecision);
  bool isFinalOutputSubsampled(grk_image* image);
  bool isChromaSubsampled(grk_image* image);
  bool areAllComponentsSameSubsampling(grk_image* image);
  bool isHeaderWritten(void);

  grk_image* image_;

  FileStandardIO* fileIO_;
  std::string fileName_;
  uint32_t compressionLevel_;

  uint32_t writeState_;
  mutable std::mutex writeStripMutex_;
  BufferPool pool;
  FileOrchestratorIO orchestrator;
};
