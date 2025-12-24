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

#include "buffer.h"
#include "mqc_base.h"

namespace grk
{

const uint8_t BACKUP_DISABLED = 0xFF;

/**
 * @struct mqcoder_backup
 * MQ coder base class used to manage backup/restore
 */
struct mqcoder_backup : public mqcoder_base
{
  /**
   * @brief Creates an mqcoder_backup
   * This struct uses @ref mqcoder_base to store cacheable mqcoder variables,
   * and cacheable local variables such as c, a, and ct
   */
  mqcoder_backup(void);

  /**
   * @brief Destroys a mqcoder_backup
   */
  ~mqcoder_backup();

  // Copy constructor
  mqcoder_backup(const mqcoder_backup& other);

  // Assignment operator for mqcoder_backup
  mqcoder_backup& operator=(const mqcoder_backup& other);

  bool operator==(const mqcoder_backup& other) const;

  /**
   * @brief Prints internal state
   */
  void print(const std::string& msg);

  /**
   * @brief @ref BlockCoder flags backup
   */
  grk_flag* flagsBackup_;

  /**
   * @brief @ref BlockCoder frame buffer backup
   */
  Buffer2dAligned32 uncompressedBufBackup_;

  /**
   * @brief local position of backup in loop
   */
  uint8_t position;

  /**
   * @brief local loop variables
   */
  uint8_t i, j, k;

  /**
   * @brief local partial
   */
  bool partial;

  /**
   * @brief local runlen
   */
  uint8_t runlen;

  /**
   * @brief local data pointer
   */
  int32_t* dataPtr_;

  /**
   * @brief local flags pointer
   */
  grk_flag* flagsPtr_;

  /**
   * @brief current flags
   */
  grk_flag _flags;

  /**
   * @brief code block number of bit planes to decompress
   */
  uint8_t numBpsToDecompress_;

  /**
   * @brief code block pass number
   */
  uint8_t passno_;

  /**
   * @brief code block pass type
   */
  uint8_t passtype_;

  uint16_t layer_;
};

} // namespace grk
