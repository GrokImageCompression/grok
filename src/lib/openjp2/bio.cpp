/*
*    Copyright (C) 2016 Grok Image Compression Inc.
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

#include "opj_includes.h"

/** @defgroup BIO BIO - Individual bit input-output stream */
/*@{*/

/** @name Local static functions */
/*@{*/

/**
Write a bit
@param bio BIO handle
@param b Bit to write (0 or 1)
*/
static void opj_bio_putbit(opj_bio_t *bio, uint8_t b);
/**
Read a bit
@param bio BIO handle
@return Returns the read bit
*/
static uint32_t opj_bio_getbit(opj_bio_t *bio);
/**
Write a byte
@param bio BIO handle
@return Returns true if successful, returns false otherwise
*/
static bool opj_bio_byteout(opj_bio_t *bio);
/**
Read a byte
@param bio BIO handle
@return Returns true if successful, returns false otherwise
*/
static bool opj_bio_bytein(opj_bio_t *bio);

/*@}*/

/*@}*/

/*
==========================================================
   local functions
==========================================================
*/

static bool opj_bio_byteout(opj_bio_t *bio)
{
    bio->ct = bio->buf == 0xff ? 7 : 8;
    if ((size_t)bio->bp >= (size_t)bio->end) {
        return false;
    }
    if (!bio->sim_out)
        *bio->bp = bio->buf;
    bio->bp++;
    bio->buf = 0;
    return true;
}

static bool opj_bio_bytein(opj_bio_t *bio)
{
    bio->ct = bio->buf == 0xff ? 7 : 8;
    if ((size_t)bio->bp >= (size_t)bio->end) {
        return false;
    }
    bio->buf = *bio->bp++;
    return true;
}

static void opj_bio_putbit(opj_bio_t *bio, uint8_t b)
{
    if (bio->ct == 0) {
        opj_bio_byteout(bio); /* MSD: why not check the return value of this function ? */
    }
    bio->ct--;
    bio->buf |= (uint8_t)(b << bio->ct);
}

static uint32_t opj_bio_getbit(opj_bio_t *bio)
{
    if (bio->ct == 0) {
        opj_bio_bytein(bio); /* MSD: why not check the return value of this function ? */
    }
    bio->ct--;
    return (bio->buf >> bio->ct) & 1;
}

/*
==========================================================
   Bit Input/Output interface
==========================================================
*/

opj_bio_t* opj_bio_create(void)
{
    opj_bio_t *bio = (opj_bio_t*)opj_malloc(sizeof(opj_bio_t));
    return bio;
}

void opj_bio_destroy(opj_bio_t *bio)
{
    if(bio) {
        opj_free(bio);
    }
}

ptrdiff_t opj_bio_numbytes(opj_bio_t *bio)
{
    return (bio->bp - bio->start);
}

void opj_bio_init_enc(opj_bio_t *bio, uint8_t *bp, uint32_t len)
{
    bio->start = bp;
    bio->end = bp + len;
    bio->bp = bp;
    bio->buf = 0;
    bio->ct = 8;
    bio->sim_out = false;
}

void opj_bio_init_dec(opj_bio_t *bio, uint8_t *bp, uint32_t len)
{
    bio->start = bp;
    bio->end = bp + len;
    bio->bp = bp;
    bio->buf = 0;
    bio->ct = 0;
}

void opj_bio_write(opj_bio_t *bio, uint32_t v, uint32_t n)
{
    uint32_t i;
    for (i = n - 1; i < n; i--) {
        opj_bio_putbit(bio, (v >> i) & 1);
    }
}

uint32_t opj_bio_read(opj_bio_t *bio, uint32_t n)
{
    uint32_t i;
    uint32_t v;
    v = 0;
    for (i = n - 1; i < n; i--) {
        v += opj_bio_getbit(bio) << i;
    }
    return v;
}

bool opj_bio_flush(opj_bio_t *bio)
{
    if (! opj_bio_byteout(bio)) {
        return false;
    }
    if (bio->ct == 7) {
        if (! opj_bio_byteout(bio)) {
            return false;
        }
    }
    return true;
}

bool opj_bio_inalign(opj_bio_t *bio)
{
    if ((bio->buf & 0xff) == 0xff) {
        if (! opj_bio_bytein(bio)) {
            return false;
        }
    }
    bio->ct = 0;
    return true;
}
