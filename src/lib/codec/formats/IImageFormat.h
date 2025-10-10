/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
 * @brief image format encode states
 *
 */
const uint32_t IMAGE_FORMAT_UNENCODED = 1;
const uint32_t IMAGE_FORMAT_ENCODED_HEADER = 2;
const uint32_t IMAGE_FORMAT_ENCODED_PIXELS = 4;
const uint32_t IMAGE_FORMAT_ERROR = 8;

/**
 * @class IImageFormat
 * @brief Interface to (non JPEG 2000) image format
 *
 */
class IImageFormat
{
public:
  /**
   * @brief Destroy the IImageFormat object
   *
   */
  virtual ~IImageFormat() = default;
  /**
   * @brief Registers callback for reclaiming buffers after io uring is finished with them
   *
   * @param io_init @ref grk_io_init
   * @param reclaim_callback @ref grk_io_callback
   * @param user_data user data
   */
  virtual void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
                                          void* user_data) = 0;

  /**
   * @brief Initializes encoding for format
   *
   * @param image @ref grk_image
   * @param filename file name
   * @param compression_level compression level for format
   * @param concurrency concurrency
   * @return true if successful
   */
  virtual bool encodeInit(grk_image* image, const std::string& filename, uint32_t compression_level,
                          uint32_t concurrency) = 0;

  /**
   * @brief Encodes header
   *
   * @return true if successful
   */
  virtual bool encodeHeader(void) = 0;

  /**
   * @brief Encodes pixels. This method is called by the application when it is orchestrating
   * pixel storage
   *
   * @return true if successful
   */
  virtual bool encodePixels(void) = 0;
  /***
   * library-orchestrated pixel encoding
   */

  /**
   * @brief Encodes pixels. This method is called by the library when it is orchestrating
   * pixel storage
   *
   * @param workerId worker id
   * @param pixels @ref grk_io_buf
   * @return true if successful
   */
  virtual bool encodePixels(uint32_t workerId, grk_io_buf pixels) = 0;

  /**
   * @brief Finished encode
   *
   * @return true if successful
   */
  virtual bool encodeFinish(void) = 0;

  /**
   * @brief Gets the encode state
   *
   * @return uint32_t encode state
   */
  virtual uint32_t getEncodeState(void) = 0;

  /**
   * @brief Decodes from image format
   *
   * @param filename file name
   * @param parameters @ref grk_cparameters
   * @return grk_image* @ref grk_image
   */
  virtual grk_image* decode(const std::string& filename, grk_cparameters* parameters) = 0;
};
