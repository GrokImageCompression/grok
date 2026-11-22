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
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
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
  std::mutex mutex;
  std::condition_variable changed;
  uint32_t stripsRead = 0;
  uint32_t stripsPushed = 0;
  bool readFailed = false;
  bool stopReading = false;
  std::exception_ptr readError;
  std::thread reader([&] {
    for(uint32_t strip = 0; strip < strips; ++strip)
    {
      {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return stopReading || strip < stripsPushed + 2; });
        if(stopReading)
          return;
      }
      bool readOk = false;
      try
      {
        readOk = readStrip(strip, stripPlanes[strip % 2]);
      }
      catch(...)
      {
        readError = std::current_exception();
      }
      std::lock_guard<std::mutex> lock(mutex);
      readFailed = !readOk;
      if(readOk)
        stripsRead = strip + 1;
      changed.notify_all();
      if(!readOk)
        return;
    }
  });
  bool pushed = true;
  std::vector<const void*> planePointers(numcomps);
  for(uint32_t strip = 0; strip < strips && pushed; ++strip)
  {
    {
      std::unique_lock<std::mutex> lock(mutex);
      changed.wait(lock, [&] { return readFailed || stripsRead > strip; });
      pushed = !readFailed;
    }
    if(!pushed)
      break;
    auto& planes = stripPlanes[strip % 2];
    for(uint16_t c = 0; c < numcomps; ++c)
      planePointers[c] = planes[c].data();
    uint32_t rows = (std::min)(rowsPerStrip, height - strip * rowsPerStrip);
    grk_strip stripRows = {rows, planePointers.data(), width};
    pushed = grk_compress_push_strip(codec, &stripRows);
    std::lock_guard<std::mutex> lock(mutex);
    stripsPushed = strip + 1;
    changed.notify_all();
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopReading = true;
    changed.notify_all();
  }
  reader.join();
  if(readError)
    std::rethrow_exception(readError);
  return pushed;
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
