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

#include <mutex>

#include "FileStandardIO.h"
#include "IImageFormat.h"
#include "IFileIO.h"
#include "BufferPool.h"
#include "FileOrchestratorIO.h"

const uint32_t reclaimSize = 5;

class ImageFormat : public IImageFormat
{
public:
  ImageFormat();
  virtual ~ImageFormat() override;
  virtual void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
                                          void* user_data) override;
  void ioReclaimBuffer(uint32_t workerId, grk_io_buf buffer);
  void reclaim(uint32_t workerId, grk_io_buf pixels);
  virtual bool encodeInit(grk_image* image, const std::string& filename, uint32_t compression_level,
                          uint32_t concurrency) override;
  virtual bool encodePixels(uint32_t workerId, grk_io_buf pixels) override;
  virtual bool encodeFinish(void) override;
  uint32_t getEncodeState(void) override;
  bool openFile(void);
  bool useStdIO(void);

protected:
  void applicationOrchestratedReclaim(GrkIOBuf buf);
  /***
   * Common core pixel encoding
   */
  virtual bool encodePixelsCore(uint32_t workerId, grk_io_buf pixels);
  /***
   * Common core pixel encoding write to disk
   */
  virtual bool encodePixelsCoreWrite(grk_io_buf pixels);
  bool open(const std::string& fname, const std::string& mode);
  uint64_t write(GrkIOBuf buffer);
  bool read(uint8_t* buf, size_t len);
  bool seek(int64_t pos, int whence);
  uint32_t maxY(uint32_t rows);
  int getMode(const char* mode);

  void allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries);
  void copyICC(grk_image* dest, const uint8_t* iccbuf, uint32_t icclen);
  void createMeta(grk_image* img);
  bool allComponentsSanityCheck(grk_image* image, bool equalPrecision);
  bool isFinalOutputSubsampled(grk_image* image);
  bool isChromaSubsampled(grk_image* image);
  bool areAllComponentsSameSubsampling(grk_image* image);
  bool isHeaderEncoded(void);

  grk_image* image_;

  FileStandardIO* fileIO_;
  std::string fileName_;
  uint32_t compressionLevel_;

  uint32_t encodeState;
  mutable std::mutex encodePixelmutex;
  BufferPool pool;
  FileOrchestratorIO orchestrator;
};
