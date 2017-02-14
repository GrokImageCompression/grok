/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
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

/**
@file tcd.h
@brief Implementation of a tile coder/decoder (TCD)

The functions in TCD.C encode or decode each tile independently from
each other. The functions in TCD.C are used by other functions in J2K.C.
*/

/** @defgroup TCD TCD - Implementation of a tile coder/decoder */
/*@{*/

/**
FIXME DOC
*/
typedef struct opj_tcd_seg {
    uint32_t dataindex;				// segment data offset in contiguous memory block 
    uint32_t numpasses;				// number of passes in segment
    uint32_t len;
    uint32_t maxpasses;				// maximum number of passes in segment
    uint32_t numPassesInPacket;	// number of passes in segment from current packet
    uint32_t newlen;
} opj_tcd_seg_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_pass {
    uint32_t rate;
    double distortiondec;
    uint32_t len;
    uint32_t term : 1;
	uint16_t slope;  //ln(slope) in 8.8 fixed point
} opj_tcd_pass_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_layer {
    uint32_t numpasses;		/* Number of passes in the layer */
    uint32_t len;			/* len of information */
    double disto;			/* add for index (Cfr. Marcela) */
    uint8_t *data;			/* data buffer*/
} opj_tcd_layer_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_cblk_enc {
    uint8_t* data;              /* data buffer*/
	uint32_t data_size;         /* size of allocated data buffer */
	bool owns_data;				// true if code block manages data buffer, otherwise false
    opj_tcd_layer_t* layers;     
    opj_tcd_pass_t* passes;      
    uint32_t x0, y0, x1, y1;     /* dimension of the code block : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t numbps;
    uint32_t numlenbits;
    uint32_t num_passes_included_in_current_layer;  /* number of passes encoded in current layer */
    uint32_t num_passes_included_in_other_layers;	/* number of passes in other layers */
    uint32_t num_passes_encoded;					/* number of passes encoded */
    uint32_t* contextStream;
} opj_tcd_cblk_enc_t;


typedef struct opj_tcd_cblk_dec {
	uint8_t* data;					// pointer to plugin data. 
	uint32_t dataSize;				/* size of data buffer */
    opj_vec_t seg_buffers;
    opj_tcd_seg_t* segs;			/* information on segments */
    uint32_t x0, y0, x1, y1;		/* position: left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t numbps;
    uint32_t numlenbits;
    uint32_t numPassesInPacket;		/* number of passes added by current packet */
    uint32_t numSegments;			/* number of segment*/
    uint32_t numSegmentsAllocated;  // number of segments allocated for segs array
} opj_tcd_cblk_dec_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_precinct {
    uint32_t x0, y0, x1, y1;		/* dimension of the precinct : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t cw, ch;				/* number of precinct in width and height */
    union {							/* code-blocks information */
        opj_tcd_cblk_enc_t* enc;
        opj_tcd_cblk_dec_t* dec;
        void*               blocks;
    } cblks;
    uint32_t block_size;			/* size taken by cblks (in bytes) */
    TagTree *incltree;	    /* inclusion tree */
    TagTree *imsbtree;	    /* IMSB tree */
} opj_tcd_precinct_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_band {
    uint32_t x0, y0, x1, y1;		/* dimension of the subband : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t bandno;
    opj_tcd_precinct_t *precincts;	/* precinct information */
    uint32_t precincts_data_size;	/* size of data taken by precincts */
    uint32_t numbps;
    float stepsize;
} opj_tcd_band_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_resolution {
    uint32_t x0, y0, x1, y1;	/* dimension of the resolution level : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t pw, ph;
    uint32_t numbands;			/* number sub-band for the resolution level */
    opj_tcd_band_t bands[3];	/* subband information */
} opj_tcd_resolution_t;

/**
FIXME DOC
*/
typedef struct opj_tcd_tilecomp {
    uint32_t x0, y0, x1, y1;			/* dimension of component : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t numresolutions;			/* number of resolutions level */
    uint32_t minimum_num_resolutions;	/* number of resolutions level to decode (at max)*/
    opj_tcd_resolution_t *resolutions;  /* resolutions information */
    uint32_t resolutions_size;			/* size of data for resolutions (in bytes) */
    uint64_t numpix;                  
    opj_tile_buf_component_t* buf;
} opj_tcd_tilecomp_t;


/**
FIXME DOC
*/
typedef struct opj_tcd_tile {
    uint32_t x0, y0, x1, y1;	/* dimension of the tile : left upper corner (x0, y0) right low corner (x1,y1) */
    uint32_t numcomps;			/* number of components in tile */
    opj_tcd_tilecomp_t *comps;	/* Components information */
    uint64_t numpix;				
    double distotile;			
    double distolayer[100];	
    uint32_t packno;            /* packet number */
} opj_tcd_tile_t;

/**
Tile coder/decoder
*/
typedef struct opj_tcd {
    /** Position of the tilepart flag in Progression order*/
    uint32_t tp_pos;
    /** Tile part number*/
    uint32_t tp_num;
    /** Current tile part number*/
    uint32_t cur_tp_num;
    /** Total number of tileparts of the current tile*/
    uint32_t cur_totnum_tp;
    /** Current Packet iterator number */
    uint32_t cur_pino;
    /** info on image tile */
    opj_tcd_tile_t* tile;
    /** image header */
    opj_image_t *image;
    /** coding parameters */
    opj_cp_t *cp;
    /** coding/decoding parameters common to all tiles */
    opj_tcp_t *tcp;
    /** current encoded tile (not used for decode) */
    uint32_t tcd_tileno;
    /** indicate if the tcd is a decoder. */
    uint32_t m_is_decoder : 1;
    opj_plugin_tile_t* current_plugin_tile;
	uint32_t numThreads;
} opj_tcd_t;

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/**
Create a new TCD handle
@param p_is_decoder FIXME DOC
@return Returns a new TCD handle if successful returns NULL otherwise
*/
opj_tcd_t* opj_tcd_create(bool p_is_decoder);

/**
Destroy a previously created TCD handle
@param tcd TCD handle to destroy
*/
void opj_tcd_destroy(opj_tcd_t *tcd);

/**
 * Initialize the tile coder and may reuse some memory.
 * @param	p_tcd		TCD handle.
 * @param	p_image		raw image.
 * @param	p_cp		coding parameters.
 *
 * @return true if the encoding values could be set (false otherwise).
*/
bool opj_tcd_init(	opj_tcd_t *p_tcd,
                    opj_image_t * p_image,
                    opj_cp_t * p_cp ,
					uint32_t numThreads);

/**
 * Allocates memory for decoding a specific tile.
 *
 * @param	p_tcd		the tile decoder.
 * @param	output_image output image - stores the decode region of interest
 * @param	p_tile_no	the index of the tile received in sequence. This not necessarily lead to the
 * tile at index p_tile_no.
 * @param p_manager the event manager.
 *
 * @return	true if the remaining data is sufficient.
 */
bool opj_tcd_init_decode_tile(opj_tcd_t *p_tcd,
                              opj_image_t* output_image,
                              uint32_t p_tile_no,
                              opj_event_mgr_t* p_manager);

/**
 * Gets the maximum tile size that will be taken by the tile once decoded.
 */
uint64_t opj_tcd_get_decoded_tile_size (opj_tcd_t *p_tcd );

/**
 * Encodes a tile from the raw image into the given buffer.
 * @param	p_tcd			Tile Coder handle
 * @param	p_tile_no		Index of the tile to encode.
 * @param	p_dest			Destination buffer
 * @param	p_data_written	pointer to an int that is incremented by the number of bytes really written on p_dest
 * @param	p_len			Maximum length of the destination buffer
 * @param	p_cstr_info		Codestream information structure
 * @return  true if the coding is successful.
*/
bool opj_tcd_encode_tile(   opj_tcd_t *p_tcd,
                            uint32_t p_tile_no,
                            uint8_t *p_dest,
                            uint64_t * p_data_written,
                            uint64_t p_len,
                            opj_codestream_info_t *p_cstr_info);


/**
* Allocates memory for a decoding code block (but not data)
*/
bool opj_tcd_code_block_dec_allocate(opj_tcd_cblk_dec_t * p_code_block);

/**
Decode a tile from a buffer into a raw image
@param tcd TCD handle
@param src Source buffer
@param len Length of source buffer
@param tileno Number that identifies one of the tiles to be decoded
@param cstr_info  FIXME DOC
@param manager the event manager.
*/
bool opj_tcd_decode_tile(   opj_tcd_t *tcd,
                            opj_seg_buf_t* src_buf,
                            uint32_t tileno,
                            opj_event_mgr_t *manager);


/**
 * Copies tile data from the system onto the given memory block.
 */
bool opj_tcd_update_tile_data (	opj_tcd_t *p_tcd,
                                uint8_t * p_dest,
                                uint64_t p_dest_length );

/**
 *
 */
uint64_t opj_tcd_get_encoded_tile_size ( opj_tcd_t *p_tcd );

/**
 * Initialize the tile coder and may reuse some meory.
 *
 * @param	p_tcd		TCD handle.
 * @param	p_tile_no	current tile index to encode.
 * @param p_manager the event manager.
 *
 * @return true if the encoding values could be set (false otherwise).
*/
bool opj_tcd_init_encode_tile (	opj_tcd_t *p_tcd,
                                uint32_t p_tile_no, opj_event_mgr_t* p_manager );

/**
 * Copies tile data from the given memory block onto the system.
 */
bool opj_tcd_copy_tile_data (opj_tcd_t *p_tcd,
                             uint8_t * p_src,
                             uint64_t p_src_length );

/**
 * Allocates tile component data
 *
 *
 */
bool opj_tile_buf_create_component(opj_tcd_tilecomp_t * tilec,
                                   bool irreversible,
                                   uint32_t cblkw,
                                   uint32_t cblkh,
                                   opj_image_t* output_image,
                                   uint32_t dx,
                                   uint32_t dy);


bool opj_tcd_needs_rate_control(opj_tcp_t *tcd_tcp, opj_encoding_param_t* enc_params);

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/


