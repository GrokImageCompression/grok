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
*/


#pragma once


#define GRK_FAKE_MARKER_BYTES   2    /**< Margin for a fake FFFF marker */

#include "grok.h"
#include <stdbool.h>

#include "shared.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include <t1_common.h>
#include "grok_malloc.h"
#include "mqc.h"

namespace grk {

#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS-1)

/* Not a C99 compiler */
#if defined(__GNUC__)
#define GRK_RESTRICT __restrict__
#else
#define GRK_RESTRICT /* restrict */
#endif



/* Type to use for bit-fields in internal headers */
typedef unsigned int GRK_BITFIELD;


/**
FIXME DOC
*/
typedef struct tcd_pass {
    uint32_t rate;
    double distortiondec;
    uint32_t len;
    GRK_BITFIELD term : 1;
} tcd_pass_t;

/**
FIXME DOC
*/
typedef struct tcd_layer {
    uint32_t numpasses;       /* Number of passes in the layer */
    uint32_t len;             /* len of information */
    double disto;          /* add for index (Cfr. Marcela) */
    uint8_t *data;             /* data */
} tcd_layer_t;

/**
FIXME DOC
*/
typedef struct tcd_cblk_enc {
    uint8_t* data;               /* Data */
    tcd_layer_t* layers;      /* layer information */
    tcd_pass_t* passes;       /* information about the passes */
    int32_t x0, y0, x1,
              y1;     /* dimension of the code-blocks : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t numbps;
    uint32_t numlenbits;
    uint32_t data_size;         /* Size of allocated data buffer */
    uint32_t
    numpasses;         /* number of pass already done for the code-blocks */
    uint32_t numpassesinlayers; /* number of passes in the layer */
    uint32_t totalpasses;       /* total number of passes */
} tcd_cblk_enc_t;


/** Chunk of codestream data that is part of a code block */
typedef struct tcd_seg_data_chunk {
    /* Point to tilepart buffer. We don't make a copy !
       So the tilepart buffer must be kept alive
       as long as we need to decode the codeblocks */
    uint8_t * data;
    uint32_t len;                 /* Usable length of data */
} tcd_seg_data_chunk_t;

/** Segment of a code-block.
 * A segment represent a number of consecutive coding passes, without termination
 * of MQC or RAW between them. */
typedef struct tcd_seg {
    uint32_t len;      /* Size of data related to this segment */
    /* Number of passes decoded. Including those that we skip */
    uint32_t numpasses;
    /* Number of passes actually to be decoded. To be used for code-block decoding */
    uint32_t real_num_passes;
    /* Maximum number of passes for this segment */
    uint32_t maxpasses;
} tcd_seg_t;

/** Code-block for decoding */
typedef struct tcd_cblk_dec {
    tcd_seg_t* segs;            /* segments information */
    tcd_seg_data_chunk_t* chunks; /* Array of chunks */
    /* position of the code-blocks : left upper corner (x0, y0) right low corner (x1,y1) */
    int32_t x0, y0, x1, y1;
    uint32_t numbps;
    /* number of segments, including those of packet we skip */
    uint32_t numsegs;
    /* number of segments, to be used for code block decoding */
    uint32_t real_num_segs;
    uint32_t m_current_max_segs;  /* allocated number of segs[] items */
    uint32_t numchunks;           /* Number of valid chunks items */
    uint32_t numchunksalloc;      /* Number of chunks item allocated */
    /* Decoded code-block. Only used for subtile decoding. Otherwise tilec->data is directly updated */
    int32_t* unencoded_data;
} tcd_cblk_dec_t;

}

#include "t1.h"
