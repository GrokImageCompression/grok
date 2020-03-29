/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <t1_common.h>
namespace grk {

struct mqc_state;
struct mqcoder;

struct mqc_state {
    /** the probability of the Least Probable Symbol (0.75->0x8000, 1.5->0xffff) */
    uint32_t qeval;
    /** the Most Probable Symbol (0 or 1) */
    uint32_t mps;
    /** next state if the next encoded symbol is the MPS */
    const mqc_state *nmps;
    /** next state if the next encoded symbol is the LPS */
    const mqc_state *nlps;
} ;

#define MQC_NUMCTXS 19
struct mqcoder {
    /** temporary buffer where bits are coded or decoded */
    uint32_t c;
    /** only used by MQ decoder */
    uint32_t a;
    /** number of bits already read or free to write */
    uint32_t ct;
    /* only used by decoder, to count the number of times a terminating 0xFF >0x8F marker is read */
    uint32_t end_of_byte_stream_counter;
    /** pointer to the current position in the buffer */
    uint8_t *bp;
    /** pointer to the start of the buffer */
    uint8_t *start;
    /** pointer to the end of the buffer */
    uint8_t *end;
    /** Array of contexts */
    const mqc_state *ctxs[MQC_NUMCTXS];
    /** Active context */
    const mqc_state **curctx;
    /* lut_ctxno_zc shifted by (1 << 9) * bandno */
    const uint8_t* lut_ctxno_zc_orient;
    /** Original value of the 2 bytes at end[0] and end[1] */
    uint8_t backup[GRK_FAKE_MARKER_BYTES];
} ;

const uint32_t A_MIN = 0x8000;

#include <mqc_inl.h>
#include <mqc_dec_inl.h>
#include <mqc_enc_inl.h>

uint32_t mqc_numbytes_enc(mqcoder *mqc);
void mqc_resetstates(mqcoder *mqc);

/* ENCODE */

void mqc_init_enc(mqcoder *mqc, uint8_t *bp);
void mqc_encode(mqcoder *mqc, uint32_t d);
void mqc_flush_enc(mqcoder *mqc);
void mqc_bypass_init_enc(mqcoder *mqc);
uint32_t mqc_bypass_get_extra_bytes_enc(mqcoder *mqc, bool erterm);
void mqc_bypass_enc(mqcoder *mqc, uint32_t d);
void mqc_bypass_flush_enc(mqcoder *mqc, bool erterm);
void mqc_restart_init_enc(mqcoder *mqc);
void mqc_erterm_enc(mqcoder *mqc);
void mqc_segmark_enc(mqcoder *mqc);

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
@param extra_writable_bytes Indicate how many bytes after len are writable.
                            This is to indicate your consent that bp must be
                            large enough.
*/
void mqc_init_dec(mqcoder *mqc, uint8_t *bp, uint32_t len,
                      uint32_t extra_writable_bytes);

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
@param extra_writable_bytes Indicate how many bytes after len are writable.
                            This is to indicate your consent that bp must be
                            large enough.
*/
void mqc_raw_init_dec(mqcoder *mqc, uint8_t *bp, uint32_t len,
                          uint32_t extra_writable_bytes);


/**
Terminate RAW/MQC decoding

This restores the bytes temporarily overwritten by mqc_init_dec()/
mqc_raw_init_dec()

@param mqc MQC handle
*/
void opq_mqc_finish_dec(mqcoder *mqc);

}
