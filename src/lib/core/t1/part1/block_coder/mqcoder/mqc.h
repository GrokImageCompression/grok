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

#include "mqc_base.h"
#include "mqc_backup.h"

// the next line must be uncommented in order to support debugging
// for plugin encode
// #define PLUGIN_DEBUG_ENCODE

#ifdef PLUGIN_DEBUG_ENCODE
#include "plugin_interface.h"
#endif

namespace grk
{

/**
 * @struct mqcoder
 * @brief MQ coder
 */
struct mqcoder : public mqcoder_base
{
  /**
   * @brief Creates an mqcoder
   */
  mqcoder(void);
  /**
   * @brief Creates an mqcoder
   * @param cached true if in differential decompress mode
   */
  explicit mqcoder(bool cached);

  // Copy constructor
  mqcoder(const mqcoder& other);

  ~mqcoder();

  // Copy assignment operator
  mqcoder& operator=(const mqcoder& other);

  bool operator==(const mqcoder& other) const;

  /**
   * @brief Prints internal state
   */
  void print(const std::string& msg);

  void resetstates(void);

  void reinit(void);

  /**
   * @brief Backs up data to backup_
   */
  void backup();
  /**
   * @brief Restores data from backup_
   */
  void restore();

  /**
   * @brief Initializes decoder for MQ decoding
   * @param bp Pointer to the start of the buffer from which the bytes will be read
   * @param len Length of input buffer
   */
  void init_dec(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers);

  void init_dec_common(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers);
  void update_dec(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers);

  /**
   * @brief Initializes decoder for RAW decoding
   * @param bp Pointer to the start of the buffer from which the bytes will be read
   * @param len Length of input buffer
   */
  void raw_init_dec(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers);

  uint16_t numbytes_enc(void);
  void init_enc(uint8_t* bp);
  void flush_enc(void);
  void bypass_init_enc(void);
  uint16_t bypass_get_extra_bytes_enc(bool erterm);
  void bypass_flush_enc(bool erterm);
  void restart_init_enc(void);
  void erterm_enc(void);
  void segmark_enc(void);
  void setbits_enc(void);

  /**
   *  @brief Pointer to start of buffer
   */
  uint8_t* start;
  /**
   * @brief Pointer to end of buffer
   */
  uint8_t* end;

  /**
   * @brief Array of pointers to buffers
   */
  uint8_t** buffers;

  /**
   * @brief Array of buffer lengths
   */
  uint32_t* buffer_lengths;

  /**
   * @brief Number of buffers
   */
  uint16_t num_buffers;

  /**
   * @brief Index of the current buffer
   */
  uint32_t cur_buffer_index;

  /**
   * @brief @ref mqcoder_backup
   */
  mqcoder_backup* backup_;

  /**
   * @brief true if compressed buffer overflow detected
   */
  bool overflow_;

  /**
   *  @brief lut_ctxno_zc shifted by (1 << 9) * bandIndex
   */
  const uint8_t* lut_ctxno_zc_orient;
#ifdef PLUGIN_DEBUG_ENCODE
  grk_plugin_debug_mqc debug_mqc;
#endif
};

#ifdef PLUGIN_DEBUG_ENCODE
#define CODER_SETCURCTX(mqc, ctxno)        \
  (mqc)->debug_mqc.context_number = ctxno; \
  (mqc)->curctx = (mqc)->ctxs + (uint32_t)(ctxno)
#else
#define CODER_SETCURCTX(mqc, ctxno) (mqc)->curctx = (mqc)->ctxs + (uint32_t)(ctxno)
#endif
#include "mqc_dec_inl.h"
#include "mqc_enc_inl.h"

/* DECODE */

} // namespace grk
