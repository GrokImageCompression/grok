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

#include "grk_apps_config.h"
#include "grok.h"
#include <string>

/**
 * @brief image format write states
 *
 */
const uint32_t IMAGE_FORMAT_UNWRITTEN = 1;
const uint32_t IMAGE_FORMAT_HEADER_WRITTEN = 2;
const uint32_t IMAGE_FORMAT_PIXELS_WRITTEN = 4;
const uint32_t IMAGE_FORMAT_ERROR = 8;

/**
 * @class IImageReader
 * @brief Interface for reading (non JPEG 2000) image formats into a grk_image
 *
 * Used by the compress pipeline to import source images (TIFF, PNG, BMP, etc.)
 */
class IImageReader
{
public:
  virtual ~IImageReader() = default;

  /**
   * @brief Read image from file into a grk_image
   *
   * @param filename file name
   * @param parameters @ref grk_cparameters
   * @return grk_image* @ref grk_image, or nullptr on failure
   */
  virtual grk_image* readImage(const std::string& filename, grk_cparameters* parameters) = 0;
};

/**
 * @class IImageWriter
 * @brief Interface for writing decompressed pixels to (non JPEG 2000) image formats
 *
 * Used by the decompress pipeline to export to TIFF, PNG, BMP, etc.
 *
 * Lifecycle:
 *   writeInit() → writeHeader() → writeImage() or writeStrip()... → writeFinish()
 */
class IImageWriter
{
public:
  virtual ~IImageWriter() = default;

  /**
   * @brief Registers callback for reclaiming buffers
   *
   * @param io_init @ref grk_io_init
   * @param reclaim_callback @ref grk_io_callback
   * @param user_data user data
   */
  virtual void registerReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
                                       void* user_data) = 0;

  /**
   * @brief Initializes writer for the given image and output file
   *
   * @param image @ref grk_image
   * @param filename output file name
   * @param compression_level compression level for the output format
   * @param concurrency concurrency hint
   * @return true if successful
   */
  virtual bool writeInit(grk_image* image, const std::string& filename, uint32_t compression_level,
                         uint32_t concurrency) = 0;

  /**
   * @brief Replace the image pointer used by subsequent write calls.
   *
   * Used for incremental band writing when the source image differs
   * from the one originally passed to writeInit().
   */
  virtual void setImage(grk_image* image) = 0;

  /**
   * @brief Writes format-specific header
   *
   * @return true if successful
   */
  virtual bool writeHeader(void) = 0;

  /**
   * @brief Writes all pixels from the in-memory image (pull-based).
   *
   * The format pulls pixel data from the grk_image set during writeInit(),
   * packs it into strips, and writes to disk. Equivalent to
   * writeImageBand(0, image_height).
   *
   * @return true if successful
   */
  virtual bool writeImage(void) = 0;

  /**
   * @brief Writes a band of pixel rows from the in-memory image (pull-based, incremental).
   *
   * Packs and writes strips covering rows [yBegin, yEnd) from the image set
   * during writeInit(). Caller must ensure rows are written in order.
   *
   * @param yBegin first row (inclusive) to write
   * @param yEnd   last row (exclusive) to write
   * @return true if successful
   */
  virtual bool writeImageBand(uint32_t yBegin, uint32_t yEnd) = 0;

  /**
   * @brief Returns true if this format supports incremental band writing via writeImageBand().
   */
  virtual bool supportsIncrementalBandWrite(void) const = 0;

  /**
   * @brief Writes a single strip of pre-packed pixel data (push-based, incremental).
   *
   * Called by the decompress pipeline to push strips as they become available.
   *
   * @param workerId worker thread id
   * @param pixels @ref grk_io_buf containing the strip data
   * @return true if successful
   */
  virtual bool writeStrip(uint32_t workerId, grk_io_buf pixels) = 0;

  /**
   * @brief Finalize writing and close the output file
   *
   * @return true if successful
   */
  virtual bool writeFinish(void) = 0;

  /**
   * @brief Gets the current write state
   *
   * @return uint32_t write state bitmask
   */
  virtual uint32_t getWriteState(void) = 0;
};

/**
 * @class IImageFormat
 * @brief Combined reader/writer interface for (non JPEG 2000) image formats
 *
 */
class IImageFormat : public IImageReader, public IImageWriter
{
public:
  virtual ~IImageFormat() = default;
};
