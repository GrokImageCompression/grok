/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <t1_common.h>
#include "plugin_interface.h"
namespace grk
{

// the next line must be uncommented in order to support debugging
// for plugin encode
// #define PLUGIN_DEBUG_ENCODE

struct mqc_state;
struct mqcoder;

struct mqc_state
{
   /** the probability of the Least Probable Symbol (0.75->0x8000, 1.5->0xffff) */
   uint32_t qeval;
   /** the Most Probable Symbol (0 or 1) */
   uint32_t mps;
   /** next state if the next encoded symbol is the MPS */
   const mqc_state* nmps;
   /** next state if the next encoded symbol is the LPS */
   const mqc_state* nlps;
};

#define MQC_NUMCTXS 19
struct mqcoder
{
   /** temporary buffer where bits are coded or decoded */
   uint32_t c;
   /** only used by MQ decoder */
   uint32_t a;
   /** number of bits already read or free to write */
   uint32_t ct;
   /* only used by decoder, to count the number of times a terminating 0xFF >0x8F marker is read */
   uint32_t end_of_byte_stream_counter;
   /** pointer to the current position in the buffer */
   uint8_t* bp;
   /** pointer to the start of the buffer */
   uint8_t* start;
   /** pointer to the end of the buffer */
   uint8_t* end;
   /** Array of contexts */
   const mqc_state* ctxs[MQC_NUMCTXS];
   /** Active context */
   const mqc_state** curctx;
   /* lut_ctxno_zc shifted by (1 << 9) * bandIndex */
   const uint8_t* lut_ctxno_zc_orient;
   /** Original value of the 2 bytes at end[0] and end[1] */
   uint8_t backup[grk_cblk_dec_compressed_data_pad_right];
#ifdef PLUGIN_DEBUG_ENCODE
   grk_plugin_debug_mqc debug_mqc;
#endif
};

const uint32_t A_MIN = 0x8000;

#include "mqc_inl.h"
#include "mqc_dec_inl.h"
#include "mqc_enc_inl.h"

uint32_t mqc_numbytes_enc(mqcoder* mqc);
void mqc_resetstates(mqcoder* mqc);

/* ENCODE */

void mqc_init_enc(mqcoder* mqc, uint8_t* bp);
void mqc_encode(mqcoder* mqc, uint32_t d);
void mqc_flush_enc(mqcoder* mqc);
void mqc_bypass_init_enc(mqcoder* mqc);
uint32_t mqc_bypass_get_extra_bytes_enc(mqcoder* mqc, bool erterm);
void mqc_bypass_enc(mqcoder* mqc, uint32_t d);
void mqc_bypass_flush_enc(mqcoder* mqc, bool erterm);
void mqc_restart_init_enc(mqcoder* mqc);
void mqc_erterm_enc(mqcoder* mqc);
void mqc_segmark_enc(mqcoder* mqc);

/* DECODE */

/**
Initialize the decoder for MQ decoding.

mqc_finish_dec() must be absolutely called after finishing the decoding
passes, so as to restore the bytes temporarily overwritten.

@param mqc MQC handle
@param bp Pointer to the start of the buffer from which the bytes will be read
	  Note that OPJ_COMMON_CBLK_DATA_EXTRA bytes at the end of the buffer
	  will be temporarily overwritten with an artificial 0xFF 0xFF marker.
	  (they will be backuped in the mqc structure to be restored later)
	  So bp must be at least len + OPJ_COMMON_CBLK_DATA_EXTRA large, and
	  writable.
@param len Length of the input buffer
*/
void mqc_init_dec(mqcoder* mqc, uint8_t* bp, uint32_t len);

/**
Initialize the decoder for RAW decoding.

mqc_finish_dec() must be absolutely called after finishing the decoding
passes, so as to restore the bytes temporarily overwritten.

@param mqc MQC handle
@param bp Pointer to the start of the buffer from which the bytes will be read
	  Note that OPJ_COMMON_CBLK_DATA_EXTRA bytes at the end of the buffer
	  will be temporarily overwritten with an artificial 0xFF 0xFF marker.
	  (they will be backuped in the mqc structure to be restored later)
	  So bp must be at least len + OPJ_COMMON_CBLK_DATA_EXTRA large, and
	  writable.
@param len Length of the input buffer
*/
void mqc_raw_init_dec(mqcoder* mqc, uint8_t* bp, uint32_t len);

/**
Terminate RAW/MQC decoding

This restores the bytes temporarily overwritten by mqc_init_dec()/
mqc_raw_init_dec()

@param mqc MQC handle
*/
void mqc_finish_dec(mqcoder* mqc);

} // namespace grk
