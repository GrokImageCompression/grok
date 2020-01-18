#pragma once

#include <stdbool.h>

#include "shared.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include "grok_malloc.h"
#include "opj_common.h"
#include "mqc.h"

#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS-1)
#include "opj_intmath.h"


/* Are restricted pointers available? (C99) */
#if (__STDC_VERSION__ >= 199901L)
#define OPJ_RESTRICT restrict
#else
/* Not a C99 compiler */
#if defined(__GNUC__)
#define OPJ_RESTRICT __restrict__

/*
  vc14 (2015) outputs wrong results.
  Need to check OPJ_RESTRICT usage (or a bug in vc14)
    #elif defined(_MSC_VER) && (_MSC_VER >= 1400)
        #define OPJ_RESTRICT __restrict
*/
#else
#define OPJ_RESTRICT /* restrict */
#endif
#endif


/* Type to use for bit-fields in internal headers */
typedef unsigned int OPJ_BITFIELD;


#define J2K_CCP_CBLKSTY_LAZY 0x01     /**< Selective arithmetic coding bypass */
#define J2K_CCP_CBLKSTY_RESET 0x02    /**< Reset context probabilities on coding pass boundaries */
#define J2K_CCP_CBLKSTY_TERMALL 0x04  /**< Termination on each coding pass */
#define J2K_CCP_CBLKSTY_VSC 0x08      /**< Vertically stripe causal context */
#define J2K_CCP_CBLKSTY_PTERM 0x10    /**< Predictable termination */
#define J2K_CCP_CBLKSTY_SEGSYM 0x20   /**< Segmentation symbols are used */


/* <summary>                                                              */
/* This table contains the norms of the 5-3 wavelets for different bands. */
/* </summary>                                                             */
/* FIXME! the array should really be extended up to 33 resolution levels */
/* See https://github.com/uclouvain/openjpeg/issues/493 */
static const double opj_dwt_norms[4][10] = {
    {1.000, 1.500, 2.750, 5.375, 10.68, 21.34, 42.67, 85.33, 170.7, 341.3},
    {1.038, 1.592, 2.919, 5.703, 11.33, 22.64, 45.25, 90.48, 180.9},
    {1.038, 1.592, 2.919, 5.703, 11.33, 22.64, 45.25, 90.48, 180.9},
    {.7186, .9218, 1.586, 3.043, 6.019, 12.01, 24.00, 47.97, 95.93}
};

/* <summary>                                                              */
/* This table contains the norms of the 9-7 wavelets for different bands. */
/* </summary>                                                             */
/* FIXME! the array should really be extended up to 33 resolution levels */
/* See https://github.com/uclouvain/openjpeg/issues/493 */
static const double opj_dwt_norms_real[4][10] = {
    {1.000, 1.965, 4.177, 8.403, 16.90, 33.84, 67.69, 135.3, 270.6, 540.9},
    {2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
    {2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
    {2.080, 3.865, 8.307, 17.18, 34.71, 69.59, 139.3, 278.6, 557.2}
};


/**
FIXME DOC
*/
typedef struct opj_tcd_pass {
    uint32_t rate;
    double distortiondec;
    uint32_t len;
    OPJ_BITFIELD term : 1;
} opj_tcd_pass_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_layer {
    uint32_t numpasses;       /* Number of passes in the layer */
    uint32_t len;             /* len of information */
    double disto;          /* add for index (Cfr. Marcela) */
    uint8_t *data;             /* data */
} opj_tcd_layer_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_cblk_enc {
    uint8_t* data;               /* Data */
    opj_tcd_layer_t* layers;      /* layer information */
    opj_tcd_pass_t* passes;       /* information about the passes */
    int32_t x0, y0, x1,
              y1;     /* dimension of the code-blocks : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t numbps;
    uint32_t numlenbits;
    uint32_t data_size;         /* Size of allocated data buffer */
    uint32_t
    numpasses;         /* number of pass already done for the code-blocks */
    uint32_t numpassesinlayers; /* number of passes in the layer */
    uint32_t totalpasses;       /* total number of passes */
} opj_tcd_cblk_enc_t;


/** Chunk of codestream data that is part of a code block */
typedef struct opj_tcd_seg_data_chunk {
    /* Point to tilepart buffer. We don't make a copy !
       So the tilepart buffer must be kept alive
       as long as we need to decode the codeblocks */
    uint8_t * data;
    uint32_t len;                 /* Usable length of data */
} opj_tcd_seg_data_chunk_t;

/** Segment of a code-block.
 * A segment represent a number of consecutive coding passes, without termination
 * of MQC or RAW between them. */
typedef struct opj_tcd_seg {
    uint32_t len;      /* Size of data related to this segment */
    /* Number of passes decoded. Including those that we skip */
    uint32_t numpasses;
    /* Number of passes actually to be decoded. To be used for code-block decoding */
    uint32_t real_num_passes;
    /* Maximum number of passes for this segment */
    uint32_t maxpasses;
} opj_tcd_seg_t;

/** Code-block for decoding */
typedef struct opj_tcd_cblk_dec {
    opj_tcd_seg_t* segs;            /* segments information */
    opj_tcd_seg_data_chunk_t* chunks; /* Array of chunks */
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
    int32_t* decoded_data;
} opj_tcd_cblk_dec_t;


#include "t1.h"
