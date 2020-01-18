#pragma once

#include <stdbool.h>

#include "shared.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include "opj_malloc.h"
#include "opj_common.h"
#include <stddef.h>
#include <stdarg.h>

typedef struct opj_event_mgr {
    /** Data to call the event manager upon */
    void *          m_error_data;
    /** Data to call the event manager upon */
    void *          m_warning_data;
    /** Data to call the event manager upon */
    void *          m_info_data;
    /** Error message callback if available, NULL otherwise */
    opj_msg_callback error_handler;
    /** Warning message callback if available, NULL otherwise */
    opj_msg_callback warning_handler;
    /** Debug message callback if available, NULL otherwise */
    opj_msg_callback info_handler;
} opj_event_mgr_t;


#include "mqc.h"

typedef struct opj_bio {
    /** pointer to the start of the buffer */
    OPJ_BYTE *start;
    /** pointer to the end of the buffer */
    OPJ_BYTE *end;
    /** pointer to the present position in the buffer */
    OPJ_BYTE *bp;
    /** temporary place where each byte is read or written */
    OPJ_UINT32 buf;
    /** coder : number of bits free to write. decoder : number of bits read */
    OPJ_UINT32 ct;
} opj_bio_t;

/**
Tag node
*/
typedef struct opj_tgt_node {
    struct opj_tgt_node *parent;
    OPJ_INT32 value;
    OPJ_INT32 low;
    OPJ_UINT32 known;
} opj_tgt_node_t;

/**
Tag tree
*/
typedef struct opj_tgt_tree {
    OPJ_UINT32  numleafsh;
    OPJ_UINT32  numleafsv;
    OPJ_UINT32 numnodes;
    opj_tgt_node_t *nodes;
    OPJ_UINT32  nodes_size;     /* maximum size taken by nodes */
} opj_tgt_tree_t;


#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS-1)
#include "opj_intmath.h"


typedef struct opj_mutex_t opj_mutex_t;
typedef struct opj_cond_t opj_cond_t;
typedef struct opj_thread_t opj_thread_t;
typedef struct opj_tls_t opj_tls_t;
typedef struct opj_thread_pool_t opj_thread_pool_t;


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

#define OPJ_TLS_KEY_T1  0

/* Type to use for bit-fields in internal headers */
typedef unsigned int OPJ_BITFIELD;


#define J2K_CP_CSTY_PRT 0x01
#define J2K_CP_CSTY_SOP 0x02
#define J2K_CP_CSTY_EPH 0x04
#define J2K_CCP_CSTY_PRT 0x01
#define J2K_CCP_CBLKSTY_LAZY 0x01     /**< Selective arithmetic coding bypass */
#define J2K_CCP_CBLKSTY_RESET 0x02    /**< Reset context probabilities on coding pass boundaries */
#define J2K_CCP_CBLKSTY_TERMALL 0x04  /**< Termination on each coding pass */
#define J2K_CCP_CBLKSTY_VSC 0x08      /**< Vertically stripe causal context */
#define J2K_CCP_CBLKSTY_PTERM 0x10    /**< Predictable termination */
#define J2K_CCP_CBLKSTY_SEGSYM 0x20   /**< Segmentation symbols are used */
#define J2K_CCP_QNTSTY_NOQNT 0
#define J2K_CCP_QNTSTY_SIQNT 1
#define J2K_CCP_QNTSTY_SEQNT 2


/**
 * Type of elements storing in the MCT data
 */
typedef enum MCT_ELEMENT_TYPE {
    MCT_TYPE_INT16 = 0,     /** MCT data is stored as signed shorts*/
    MCT_TYPE_INT32 = 1,     /** MCT data is stored as signed integers*/
    MCT_TYPE_FLOAT = 2,     /** MCT data is stored as floats*/
    MCT_TYPE_DOUBLE = 3     /** MCT data is stored as doubles*/
} J2K_MCT_ELEMENT_TYPE;

/**
 * Type of MCT array
 */
typedef enum MCT_ARRAY_TYPE {
    MCT_TYPE_DEPENDENCY = 0,
    MCT_TYPE_DECORRELATION = 1,
    MCT_TYPE_OFFSET = 2
} J2K_MCT_ARRAY_TYPE;

/* ----------------------------------------------------------------------- */

/**
 * Quantization stepsize
 */
typedef struct opj_stepsize {
    /** exponent */
    OPJ_INT32 expn;
    /** mantissa */
    OPJ_INT32 mant;
} opj_stepsize_t;

/**
Tile-component coding parameters
*/
typedef struct opj_tccp {
    /** coding style */
    OPJ_UINT32 csty;
    /** number of resolutions */
    OPJ_UINT32 numresolutions;
    /** code-blocks width */
    OPJ_UINT32 cblkw;
    /** code-blocks height */
    OPJ_UINT32 cblkh;
    /** code-block coding style */
    OPJ_UINT32 cblksty;
    /** discrete wavelet transform identifier */
    OPJ_UINT32 qmfbid;
    /** quantisation style */
    OPJ_UINT32 qntsty;
    /** stepsizes used for quantization */
    //opj_stepsize_t stepsizes[OPJ_J2K_MAXBANDS];
    /** number of guard bits */
    OPJ_UINT32 numgbits;
    /** Region Of Interest shift */
    OPJ_INT32 roishift;
    /** precinct width */
   //OPJ_UINT32 prcw[OPJ_J2K_MAXRLVLS];
    /** precinct height */
    //OPJ_UINT32 prch[OPJ_J2K_MAXRLVLS];
    ///** the dc_level_shift **/
    OPJ_INT32 m_dc_level_shift;
}
opj_tccp_t;



/**
 * FIXME DOC
 */
typedef struct opj_mct_data {
    J2K_MCT_ELEMENT_TYPE m_element_type;
    J2K_MCT_ARRAY_TYPE   m_array_type;
    OPJ_UINT32           m_index;
    OPJ_BYTE *           m_data;
    OPJ_UINT32           m_data_size;
}
opj_mct_data_t;

/**
 * FIXME DOC
 */
typedef struct opj_simple_mcc_decorrelation_data {
    OPJ_UINT32           m_index;
    OPJ_UINT32           m_nb_comps;
    opj_mct_data_t *     m_decorrelation_array;
    opj_mct_data_t *     m_offset_array;
    OPJ_BITFIELD         m_is_irreversible : 1;
}
opj_simple_mcc_decorrelation_data_t;

typedef struct opj_ppx_struct {
    OPJ_BYTE*   m_data; /* m_data == NULL => Zppx not read yet */
    OPJ_UINT32  m_data_size;
} opj_ppx;

/**
Tile coding parameters :
this structure is used to store coding/decoding parameters common to all
tiles (information like COD, COC in main header)
*/
typedef struct opj_tcp {
    /** coding style */
    OPJ_UINT32 csty;
    /** progression order */
    //OPJ_PROG_ORDER prg;
    /** number of layers */
    OPJ_UINT32 numlayers;
    OPJ_UINT32 num_layers_to_decode;
    /** multi-component transform identifier */
    OPJ_UINT32 mct;
    /** rates of layers */
    OPJ_FLOAT32 rates[100];
    /** number of progression order changes */
    OPJ_UINT32 numpocs;
    /** progression order changes */
    //opj_poc_t pocs[J2K_MAX_POCS];

    /** number of ppt markers (reserved size) */
    OPJ_UINT32 ppt_markers_count;
    /** ppt markers data (table indexed by Zppt) */
    opj_ppx* ppt_markers;

    /** packet header store there for future use in t2_decode_packet */
    OPJ_BYTE *ppt_data;
    /** used to keep a track of the allocated memory */
    OPJ_BYTE *ppt_buffer;
    /** Number of bytes stored inside ppt_data*/
    OPJ_UINT32 ppt_data_size;
    /** size of ppt_data*/
    OPJ_UINT32 ppt_len;
    /** add fixed_quality */
    OPJ_FLOAT32 distoratio[100];
    /** tile-component coding parameters */
    opj_tccp_t *tccps;
    /** current tile part number or -1 if first time into this tile */
    OPJ_INT32  m_current_tile_part_number;
    /** number of tile parts for the tile. */
    OPJ_UINT32 m_nb_tile_parts;
    /** data for the tile */
    OPJ_BYTE *      m_data;
    /** size of data */
    OPJ_UINT32      m_data_size;
    /** encoding norms */
    OPJ_FLOAT64 *   mct_norms;
    /** the mct decoding matrix */
    OPJ_FLOAT32 *   m_mct_decoding_matrix;
    /** the mct coding matrix */
    OPJ_FLOAT32 *   m_mct_coding_matrix;
    /** mct records */
    opj_mct_data_t * m_mct_records;
    /** the number of mct records. */
    OPJ_UINT32 m_nb_mct_records;
    /** the max number of mct records. */
    OPJ_UINT32 m_nb_max_mct_records;
    /** mcc records */
    opj_simple_mcc_decorrelation_data_t * m_mcc_records;
    /** the number of mct records. */
    OPJ_UINT32 m_nb_mcc_records;
    /** the max number of mct records. */
    OPJ_UINT32 m_nb_max_mcc_records;


    /***** FLAGS *******/
    /** If cod == 1 --> there was a COD marker for the present tile */
    OPJ_BITFIELD cod : 1;
    /** If ppt == 1 --> there was a PPT marker for the present tile */
    OPJ_BITFIELD ppt : 1;
    /** indicates if a POC marker has been used O:NO, 1:YES */
    OPJ_BITFIELD POC : 1;
} opj_tcp_t;




typedef struct opj_encoding_param {
    /** Maximum rate for each component. If == 0, component size limitation is not considered */
    OPJ_UINT32 m_max_comp_size;
    /** Position of tile part flag in progression order*/
    OPJ_INT32 m_tp_pos;
    /** fixed layer */
    OPJ_INT32 *m_matrice;
    /** Flag determining tile part generation*/
    OPJ_BYTE m_tp_flag;
    /** allocation by rate/distortion */
    OPJ_BITFIELD m_disto_alloc : 1;
    /** allocation by fixed layer */
    OPJ_BITFIELD m_fixed_alloc : 1;
    /** add fixed_quality */
    OPJ_BITFIELD m_fixed_quality : 1;
    /** Enabling Tile part generation*/
    OPJ_BITFIELD m_tp_on : 1;
}
opj_encoding_param_t;

typedef struct opj_decoding_param {
    /** if != 0, then original dimension divided by 2^(reduce); if == 0 or not used, image is decoded to the full resolution */
    OPJ_UINT32 m_reduce;
    /** if != 0, then only the first "layer" layers are decoded; if == 0 or not used, all the quality layers are decoded */
    OPJ_UINT32 m_layer;
}
opj_decoding_param_t;


/**
 * Coding parameters
 */
typedef struct opj_cp {
    /** Size of the image in bits*/
    /*int img_size;*/
    /** Rsiz*/
    OPJ_UINT16 rsiz;
    /** XTOsiz */
    OPJ_UINT32 tx0; /* MSD see norm */
    /** YTOsiz */
    OPJ_UINT32 ty0; /* MSD see norm */
    /** XTsiz */
    OPJ_UINT32 tdx;
    /** YTsiz */
    OPJ_UINT32 tdy;
    /** comment */
    OPJ_CHAR *comment;
    /** number of tiles in width */
    OPJ_UINT32 tw;
    /** number of tiles in height */
    OPJ_UINT32 th;

    /** number of ppm markers (reserved size) */
    OPJ_UINT32 ppm_markers_count;
    /** ppm markers data (table indexed by Zppm) */
    opj_ppx* ppm_markers;

    /** packet header store there for future use in t2_decode_packet */
    OPJ_BYTE *ppm_data;
    /** size of the ppm_data*/
    OPJ_UINT32 ppm_len;
    /** size of the ppm_data*/
    OPJ_UINT32 ppm_data_read;

    OPJ_BYTE *ppm_data_current;

    /** packet header storage original buffer */
    OPJ_BYTE *ppm_buffer;
    /** pointer remaining on the first byte of the first header if ppm is used */
    OPJ_BYTE *ppm_data_first;
    /** Number of bytes actually stored inside the ppm_data */
    OPJ_UINT32 ppm_data_size;
    /** use in case of multiple marker PPM (number of info already store) */
    OPJ_INT32 ppm_store;
    /** use in case of multiple marker PPM (case on non-finished previous info) */
    OPJ_INT32 ppm_previous;

    /** tile coding parameters */
    opj_tcp_t *tcps;

    union {
        opj_decoding_param_t m_dec;
        opj_encoding_param_t m_enc;
    }
    m_specific_param;

    /******** FLAGS *********/
    /** if ppm == 1 --> there was a PPM marker*/
    OPJ_BITFIELD ppm : 1;
    /** tells if the parameter is a coding or decoding one */
    OPJ_BITFIELD m_is_decoder : 1;
    /** whether different bit depth or sign per component is allowed. Decoder only for ow */
    OPJ_BITFIELD allow_different_bit_depth_sign : 1;
    /* <<UniPG */
} opj_cp_t;


typedef struct opj_j2k_dec {
    /** locate in which part of the codestream the decoder is (main header, tile header, end) */
    OPJ_UINT32 m_state;
    /**
     * store decoding parameters common to all tiles (information like COD, COC in main header)
     */
    opj_tcp_t *m_default_tcp;
    OPJ_BYTE  *m_header_data;
    OPJ_UINT32 m_header_data_size;
    /** to tell the tile part length */
    OPJ_UINT32 m_sot_length;
    /** Only tiles index in the correct range will be decoded.*/
    OPJ_UINT32 m_start_tile_x;
    OPJ_UINT32 m_start_tile_y;
    OPJ_UINT32 m_end_tile_x;
    OPJ_UINT32 m_end_tile_y;

    /** Index of the tile to decode (used in get_tile) */
    OPJ_INT32 m_tile_ind_to_dec;
    /** Position of the last SOT marker read */
    OPJ_OFF_T m_last_sot_read_pos;

    /**
     * Indicate that the current tile-part is assume as the last tile part of the codestream.
     * It is useful in the case of PSot is equal to zero. The sot length will be compute in the
     * SOD reader function. FIXME NOT USED for the moment
     */
    bool   m_last_tile_part;

    OPJ_UINT32   m_numcomps_to_decode;
    OPJ_UINT32  *m_comps_indices_to_decode;

    /** to tell that a tile can be decoded. */
    OPJ_BITFIELD m_can_decode : 1;
    OPJ_BITFIELD m_discard_tiles : 1;
    OPJ_BITFIELD m_skip_data : 1;
    /** TNsot correction : see issue 254 **/
    OPJ_BITFIELD m_nb_tile_parts_correction_checked : 1;
    OPJ_BITFIELD m_nb_tile_parts_correction : 1;

} opj_j2k_dec_t;

typedef struct opj_j2k_enc {
    /** Tile part number, regardless of poc, for each new poc, tp is reset to 1*/
    OPJ_UINT32 m_current_poc_tile_part_number; /* tp_num */

    /** Tile part number currently coding, taking into account POC. m_current_tile_part_number holds the total number of tile parts while encoding the last tile part.*/
    OPJ_UINT32 m_current_tile_part_number; /*cur_tp_num */

    /**
    locate the start position of the TLM marker
    after encoding the tilepart, a jump (in j2k_write_sod) is done to the TLM marker to store the value of its length.
    */
    OPJ_OFF_T m_tlm_start;
    /**
     * Stores the sizes of the tlm.
     */
    OPJ_BYTE * m_tlm_sot_offsets_buffer;
    /**
     * The current offset of the tlm buffer.
     */
    OPJ_BYTE * m_tlm_sot_offsets_current;

    /** Total num of tile parts in whole image = num tiles* num tileparts in each tile*/
    /** used in TLMmarker*/
    OPJ_UINT32 m_total_tile_parts;   /* totnum_tp */

    /* encoded data for a tile */
    OPJ_BYTE * m_encoded_tile_data;

    /* size of the encoded_data */
    OPJ_UINT32 m_encoded_tile_size;

    /* encoded data for a tile */
    OPJ_BYTE * m_header_tile_data;

    /* size of the encoded_data */
    OPJ_UINT32 m_header_tile_data_size;


} opj_j2k_enc_t;


/* <summary>                                                              */
/* This table contains the norms of the 5-3 wavelets for different bands. */
/* </summary>                                                             */
/* FIXME! the array should really be extended up to 33 resolution levels */
/* See https://github.com/uclouvain/openjpeg/issues/493 */
static const OPJ_FLOAT64 opj_dwt_norms[4][10] = {
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
static const OPJ_FLOAT64 opj_dwt_norms_real[4][10] = {
    {1.000, 1.965, 4.177, 8.403, 16.90, 33.84, 67.69, 135.3, 270.6, 540.9},
    {2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
    {2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
    {2.080, 3.865, 8.307, 17.18, 34.71, 69.59, 139.3, 278.6, 557.2}
};

OPJ_FLOAT64 opj_dwt_getnorm(OPJ_UINT32 level, OPJ_UINT32 orient);
OPJ_FLOAT64 opj_dwt_getnorm_real(OPJ_UINT32 level, OPJ_UINT32 orient);

/**
FIXME DOC
*/
typedef struct opj_tcd_pass {
    OPJ_UINT32 rate;
    OPJ_FLOAT64 distortiondec;
    OPJ_UINT32 len;
    OPJ_BITFIELD term : 1;
} opj_tcd_pass_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_layer {
    OPJ_UINT32 numpasses;       /* Number of passes in the layer */
    OPJ_UINT32 len;             /* len of information */
    OPJ_FLOAT64 disto;          /* add for index (Cfr. Marcela) */
    OPJ_BYTE *data;             /* data */
} opj_tcd_layer_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_cblk_enc {
    OPJ_BYTE* data;               /* Data */
    opj_tcd_layer_t* layers;      /* layer information */
    opj_tcd_pass_t* passes;       /* information about the passes */
    OPJ_INT32 x0, y0, x1,
              y1;     /* dimension of the code-blocks : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_UINT32 numbps;
    OPJ_UINT32 numlenbits;
    OPJ_UINT32 data_size;         /* Size of allocated data buffer */
    OPJ_UINT32
    numpasses;         /* number of pass already done for the code-blocks */
    OPJ_UINT32 numpassesinlayers; /* number of passes in the layer */
    OPJ_UINT32 totalpasses;       /* total number of passes */
} opj_tcd_cblk_enc_t;


/** Chunk of codestream data that is part of a code block */
typedef struct opj_tcd_seg_data_chunk {
    /* Point to tilepart buffer. We don't make a copy !
       So the tilepart buffer must be kept alive
       as long as we need to decode the codeblocks */
    OPJ_BYTE * data;
    OPJ_UINT32 len;                 /* Usable length of data */
} opj_tcd_seg_data_chunk_t;

/** Segment of a code-block.
 * A segment represent a number of consecutive coding passes, without termination
 * of MQC or RAW between them. */
typedef struct opj_tcd_seg {
    OPJ_UINT32 len;      /* Size of data related to this segment */
    /* Number of passes decoded. Including those that we skip */
    OPJ_UINT32 numpasses;
    /* Number of passes actually to be decoded. To be used for code-block decoding */
    OPJ_UINT32 real_num_passes;
    /* Maximum number of passes for this segment */
    OPJ_UINT32 maxpasses;
    /* Number of new passes for current packed. Transitory value */
    OPJ_UINT32 numnewpasses;
    /* Codestream length for this segment for current packed. Transitory value */
    OPJ_UINT32 newlen;
} opj_tcd_seg_t;

/** Code-block for decoding */
typedef struct opj_tcd_cblk_dec {
    opj_tcd_seg_t* segs;            /* segments information */
    opj_tcd_seg_data_chunk_t* chunks; /* Array of chunks */
    /* position of the code-blocks : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_INT32 x0, y0, x1, y1;
    OPJ_UINT32 numbps;
    /* number of bits for len, for the current packet. Transitory value */
    OPJ_UINT32 numlenbits;
    /* number of pass added to the code-blocks, for the current packet. Transitory value */
    OPJ_UINT32 numnewpasses;
    /* number of segments, including those of packet we skip */
    OPJ_UINT32 numsegs;
    /* number of segments, to be used for code block decoding */
    OPJ_UINT32 real_num_segs;
    OPJ_UINT32 m_current_max_segs;  /* allocated number of segs[] items */
    OPJ_UINT32 numchunks;           /* Number of valid chunks items */
    OPJ_UINT32 numchunksalloc;      /* Number of chunks item allocated */
    /* Decoded code-block. Only used for subtile decoding. Otherwise tilec->data is directly updated */
    OPJ_INT32* decoded_data;
} opj_tcd_cblk_dec_t;

/** Precinct structure */
typedef struct opj_tcd_precinct {
    /* dimension of the precinct : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_INT32 x0, y0, x1, y1;
    OPJ_UINT32 cw, ch;              /* number of code-blocks, in width and height */
    union {                         /* code-blocks information */
        opj_tcd_cblk_enc_t* enc;
        opj_tcd_cblk_dec_t* dec;
        void*               blocks;
    } cblks;
    OPJ_UINT32 block_size;          /* size taken by cblks (in bytes) */
    opj_tgt_tree_t *incltree;       /* inclusion tree */
    opj_tgt_tree_t *imsbtree;       /* IMSB tree */
} opj_tcd_precinct_t;

/** Sub-band structure */
typedef struct opj_tcd_band {
    /* dimension of the subband : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_INT32 x0, y0, x1, y1;
    /* band number: for lowest resolution level (0=LL), otherwise (1=HL, 2=LH, 3=HH) */
    OPJ_UINT32 bandno;
    /* precinct information */
    opj_tcd_precinct_t *precincts;
    /* size of data taken by precincts */
    OPJ_UINT32 precincts_data_size;
    OPJ_INT32 numbps;
    OPJ_FLOAT32 stepsize;
} opj_tcd_band_t;

/** Tile-component resolution structure */
typedef struct opj_tcd_resolution {
    /* dimension of the resolution level : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_INT32 x0, y0, x1, y1;
    /* number of precincts, in width and height, for this resolution level */
    OPJ_UINT32 pw, ph;
    /* number of sub-bands for the resolution level (1 for lowest resolution level, 3 otherwise) */
    OPJ_UINT32 numbands;
    /* subband information */
    opj_tcd_band_t bands[3];

    /* dimension of the resolution limited to window of interest. Only valid if tcd->whole_tile_decoding is set */
    OPJ_UINT32 win_x0;
    OPJ_UINT32 win_y0;
    OPJ_UINT32 win_x1;
    OPJ_UINT32 win_y1;
} opj_tcd_resolution_t;

/** Tile-component structure */
typedef struct opj_tcd_tilecomp {
    /* dimension of component : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_INT32 x0, y0, x1, y1;
    /* component number */
    OPJ_UINT32 compno;
    /* number of resolutions level */
    OPJ_UINT32 numresolutions;
    /* number of resolutions level to decode (at max)*/
    OPJ_UINT32 minimum_num_resolutions;
    /* resolutions information */
    opj_tcd_resolution_t *resolutions;
    /* size of data for resolutions (in bytes) */
    OPJ_UINT32 resolutions_size;

    /* data of the component. For decoding, only valid if tcd->whole_tile_decoding is set (so exclusive of data_win member) */
    OPJ_INT32 *data;
    /* if true, then need to free after usage, otherwise do not free */
    bool  ownsData;
    /* we may either need to allocate this amount of data, or re-use image data and ignore this value */
    size_t data_size_needed;
    /* size of the data of the component */
    size_t data_size;

    /** data of the component limited to window of interest. Only valid for decoding and if tcd->whole_tile_decoding is NOT set (so exclusive of data member) */
    OPJ_INT32 *data_win;
    /* dimension of the component limited to window of interest. Only valid for decoding and  if tcd->whole_tile_decoding is NOT set */
    OPJ_UINT32 win_x0;
    OPJ_UINT32 win_y0;
    OPJ_UINT32 win_x1;
    OPJ_UINT32 win_y1;

    /* add fixed_quality */
    OPJ_INT32 numpix;
} opj_tcd_tilecomp_t;


/**
FIXME DOC
*/
typedef struct opj_tcd_tile {
    /* dimension of the tile : left upper corner (x0, y0) right low corner (x1,y1) */
    OPJ_INT32 x0, y0, x1, y1;
    OPJ_UINT32 numcomps;            /* number of components in tile */
    opj_tcd_tilecomp_t *comps;  /* Components information */
    OPJ_INT32 numpix;               /* add fixed_quality */
    OPJ_FLOAT64 distotile;          /* add fixed_quality */
    OPJ_FLOAT64 distolayer[100];    /* add fixed_quality */
    OPJ_UINT32 packno;              /* packet number */
} opj_tcd_tile_t;

#include "t1.h"
