/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
 * Copyright (c) 2002-2003, Yannick Verschueren
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
namespace grk {

/**
@file jp2.h
@brief The JPEG-2000 file format Reader/Writer (JP2)

*/

/** @defgroup JP2 JP2 - JPEG-2000 file format reader/writer */
/*@{*/

#define     JP2_JP   0x6a502020    /**< JPEG 2000 signature box */
#define     JP2_FTYP 0x66747970    /**< File type box */
#define     JP2_JP2H 0x6a703268    /**< JP2 header box (super-box) */
#define     JP2_IHDR 0x69686472    /**< Image header box */
#define     JP2_COLR 0x636f6c72    /**< Colour specification box */
#define     JP2_JP2C 0x6a703263    /**< Contiguous codestream box */
#define     JP2_URL  0x75726c20    /**< Data entry URL box */
#define     JP2_PCLR 0x70636c72    /**< Palette box */
#define     JP2_CMAP 0x636d6170    /**< Component Mapping box */
#define     JP2_CDEF 0x63646566    /**< Channel Definition box */
#define     JP2_DTBL 0x6474626c    /**< Data Reference box */
#define     JP2_BPCC 0x62706363    /**< Bits per component box */
#define     JP2_JP2  0x6a703220    /**< File type fields */


#define JP2_RES			0x72657320   /**< Resolution box (super-box) */
#define JP2_CAPTURE_RES 0x72657363   /**< Capture resolution box */
#define JP2_DISPLAY_RES 0x72657364   /**< Display resolution box */

#define JP2_JP2I 0x6a703269   /**< Intellectual property box */
#define JP2_XML  0x786d6c20   /**< XML box */
#define JP2_UUID 0x75756994   /**< UUID box */
#define JP2_UINF 0x75696e66   /**< UUID info box (super-box) */
#define JP2_ULST 0x756c7374   /**< UUID list box */

/* ----------------------------------------------------------------------- */

#define JP2_MAX_NUM_UUIDS	128

const uint8_t IPTC_UUID[16] = { 0x33,0xC7,0xA4,0xD2,0xB8,0x1D,0x47,0x23,0xA0,0xBA,0xF1,0xA3,0xE0,0x97,0xAD,0x38 };
const uint8_t XMP_UUID[16] = { 0xBE,0x7A,0xCF,0xCB,0x97,0xA9,0x42,0xE8,0x9C,0x71,0x99,0x94,0x91,0xE3,0xAF,0xAC };

enum JP2_STATE {
    JP2_STATE_NONE            = 0x0,
    JP2_STATE_SIGNATURE       = 0x1,
    JP2_STATE_FILE_TYPE       = 0x2,
    JP2_STATE_HEADER          = 0x4,
    JP2_STATE_CODESTREAM      = 0x8,
    JP2_STATE_END_CODESTREAM  = 0x10,
    JP2_STATE_UNKNOWN         = 0x7fffffff /* ISO C restricts enumerator values to range of 'int' */
};

enum JP2_IMG_STATE {
    JP2_IMG_STATE_NONE        = 0x0,
    JP2_IMG_STATE_UNKNOWN     = 0x7fffffff
};


/**
JP2 component
*/
struct jp2_comps_t {
    uint32_t depth;
    uint32_t sgnd;
    uint32_t bpcc;
} ;


struct jp2_buffer_t {
	jp2_buffer_t(uint8_t* buf, size_t size, bool owns) : buffer(buf), len(size), ownsData(owns)
	{
	}
	jp2_buffer_t() : jp2_buffer_t(nullptr,0,false)
	{
	}
	bool alloc(size_t length) {
		dealloc();
		buffer = (uint8_t*)grok_malloc(length);
		ownsData = buffer != nullptr;
		return buffer ? true : false;
	}
	void dealloc() {
		if (ownsData && buffer)
			grok_free(buffer);
		buffer = nullptr;
		ownsData = false;
	}
	uint8_t* buffer;
	size_t len;
	bool ownsData;
};


struct jp2_uuid_t : public jp2_buffer_t {
	jp2_uuid_t(const uint8_t myuuid[16], uint8_t* buf, size_t size, bool owns) :jp2_buffer_t(buf,size,owns) {
		for (int i = 0; i < 16; ++i) {
			uuid[i] = myuuid[i];
		}
	}
	uint8_t uuid[16];
};



/**
JPEG-2000 file format reader/writer
*/
struct jp2_t {
    /** handle to the J2K codec  */
    j2k_t *j2k;
    /** list of validation procedures */
	procedure_list_t * m_validation_list;
    /** list of execution procedures */
    procedure_list_t * m_procedure_list;

    /* width of image */
    uint32_t w;
    /* height of image */
    uint32_t h;
    /* number of components in the image */
    uint32_t numcomps;
    uint32_t bpc;
    uint32_t C;
    uint32_t UnkC;
    uint32_t IPR;
    uint32_t meth;
    uint32_t approx;
    uint32_t enumcs;
    uint32_t precedence;
    uint32_t brand;
    uint32_t minversion;
    uint32_t numcl;
    uint32_t *cl;
    jp2_comps_t *comps;
    /* FIXME: The following two variables are used to save offset
      as we write out a JP2 file to disk. This mechanism is not flexible
      as codec writers will need to extend those fields as new part
      of the standard are implemented.
    */
    int64_t j2k_codestream_offset;
	bool needs_xl_jp2c_box_length;
    uint32_t jp2_state;
    uint32_t jp2_img_state;
    jp2_color_t color;

    bool ignore_pclr_cmap_cdef;

	bool  write_capture_resolution;
	double capture_resolution[2];

	bool  write_display_resolution;
	double display_resolution[2];

	jp2_buffer_t xml;
	jp2_uuid_t uuids[JP2_MAX_NUM_UUIDS];
	uint32_t numUuids;
};

/**
JP2 Box
*/
struct jp2_box_t {
    uint64_t length;
    uint32_t type;
} ;

struct jp2_header_handler_t {
    /* marker value */
    uint32_t id;
    /* action linked to the marker */
    bool (*handler) (     jp2_t *jp2,
                          uint8_t *p_header_data,
                          uint32_t p_header_size,
                          event_mgr_t * p_manager);
};


struct jp2_img_header_writer_handler_t {
    /* action to perform */
    uint8_t*   (*handler) (jp2_t *jp2, uint32_t * p_data_size);
    /* result of the action : data */
    uint8_t*   m_data;
    /* size of data */
    uint32_t  m_size;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/**
Setup the decoder decoding parameters using user parameters.
Decoding parameters are returned in jp2->j2k->cp.
@param jp2 JP2 decompressor handle
@param parameters decompression parameters
*/
void jp2_setup_decoder(void *jp2_void, opj_dparameters_t *parameters);

/**
 * Decode an image from a JPEG-2000 file stream
 * @param jp2 JP2 decompressor handle
 * @param p_stream  FIXME DOC
 * @param p_image   FIXME DOC
 * @param p_manager FIXME DOC
 *
 * @return a decoded image if successful, returns nullptr otherwise
*/
bool jp2_decode(jp2_t *jp2,
					grok_plugin_tile_t* tile,
                    GrokStream *p_stream,
                    opj_image_t* p_image,
                    event_mgr_t * p_manager);

/**
 * Setup the encoder parameters using the current image and using user parameters.
 * Coding parameters are returned in jp2->j2k->cp.
 *
 * @param jp2 JP2 compressor handle
 * @param parameters compression parameters
 * @param image input filled image
 * @param p_manager  FIXME DOC
 * @return true if successful, false otherwise
*/
bool jp2_setup_encoder(  jp2_t *jp2,
                             opj_cparameters_t *parameters,
                             opj_image_t *image,
                             event_mgr_t * p_manager);

/**
Encode an image into a JPEG-2000 file stream
@param jp2      JP2 compressor handle
@param stream    Output buffer stream
@param p_manager  event manager
@return true if successful, returns false otherwise
*/
bool jp2_encode(  jp2_t *jp2,
						grok_plugin_tile_t* tile,
                      GrokStream *stream,
                      event_mgr_t * p_manager);


/**
 * Starts a compression scheme, i.e. validates the codec parameters, writes the header.
 *
 * @param  jp2    the jpeg2000 file codec.
 * @param  stream    the stream object.
 * @param  p_image   FIXME DOC
 * @param p_manager FIXME DOC
 *
 * @return true if the codec is valid.
 */
bool jp2_start_compress(jp2_t *jp2,
                            GrokStream *stream,
                            opj_image_t * p_image,
                            event_mgr_t * p_manager);


/**
 * Ends the compression procedures and possibly add data to be read after the
 * codestream.
 */
bool jp2_end_compress(  jp2_t *jp2,
                            GrokStream *cio,
                            event_mgr_t * p_manager);

/* ----------------------------------------------------------------------- */

/**
 * Ends the decompression procedures and possibly add data to be read after the
 * codestream.
 */
bool jp2_end_decompress(jp2_t *jp2,
                            GrokStream *cio,
                            event_mgr_t * p_manager);

/**
 * Reads a jpeg2000 file header structure.
 *
 * @param p_stream the stream to read data from.
 * @param jp2 the jpeg2000 file header structure.
 * @param p_image   FIXME DOC
 * @param p_manager the user event manager.
 *
 * @return true if the box is valid.
 */
bool jp2_read_header(  GrokStream *p_stream,
                           jp2_t *jp2,
							opj_header_info_t* header_info,
                           opj_image_t ** p_image,
                           event_mgr_t * p_manager );

/**
 * Reads a tile header.
 * @param  p_jp2         the jpeg2000 codec.
 * @param  p_tile_index  FIXME DOC
 * @param  p_data_size   FIXME DOC
 * @param  p_tile_x0     FIXME DOC
 * @param  p_tile_y0     FIXME DOC
 * @param  p_tile_x1     FIXME DOC
 * @param  p_tile_y1     FIXME DOC
 * @param  p_nb_comps    FIXME DOC
 * @param  p_go_on       FIXME DOC
 * @param  p_stream      the stream to write data to.
 * @param  p_manager     the user event manager.
 */
bool jp2_read_tile_header ( jp2_t * p_jp2,
                                uint32_t * p_tile_index,
                                uint64_t * p_data_size,
                                uint32_t * p_tile_x0,
                                uint32_t * p_tile_y0,
                                uint32_t * p_tile_x1,
                                uint32_t * p_tile_y1,
                                uint32_t * p_nb_comps,
                                bool * p_go_on,
                                GrokStream *p_stream,
                                event_mgr_t * p_manager );

/**
 * Writes a tile.
 *
 * @param  p_jp2    the jpeg2000 codec.
 * @param p_tile_index  FIXME DOC
 * @param p_data        FIXME DOC
 * @param p_data_size   FIXME DOC
 * @param  p_stream      the stream to write data to.
 * @param  p_manager  the user event manager.
 */
bool jp2_write_tile (  jp2_t *p_jp2,
                           uint32_t p_tile_index,
                           uint8_t * p_data,
                           uint64_t p_data_size,
                           GrokStream *p_stream,
                           event_mgr_t * p_manager );

/**
 * Decode tile data.
 * @param  p_jp2    the jpeg2000 codec.
 * @param  p_tile_index FIXME DOC
 * @param  p_data       FIXME DOC
 * @param  p_data_size  FIXME DOC
 * @param  p_stream      the stream to write data to.
 * @param  p_manager  the user event manager.
 *
 * @return FIXME DOC
 */
bool jp2_decode_tile (  jp2_t * p_jp2,
                            uint32_t p_tile_index,
                            uint8_t * p_data,
                            uint64_t p_data_size,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager );

/**
 * Creates a jpeg2000 file decompressor.
 *
 * @return  an empty jpeg2000 file codec.
 */
jp2_t* jp2_create (bool p_is_decoder);

/**
Destroy a JP2 decompressor handle
@param jp2 JP2 decompressor handle to destroy
*/
void jp2_destroy(jp2_t *jp2);


/**
 * Sets the given area to be decoded. This function should be called right after grok_read_header and before any tile header reading.
 *
 * @param  p_jp2      the jpeg2000 codec.
 * @param  p_image     FIXME DOC
 * @param  p_start_x   the left position of the rectangle to decode (in image coordinates).
 * @param  p_start_y    the up position of the rectangle to decode (in image coordinates).
 * @param  p_end_x      the right position of the rectangle to decode (in image coordinates).
 * @param  p_end_y      the bottom position of the rectangle to decode (in image coordinates).
 * @param  p_manager    the user event manager
 *
 * @return  true      if the area could be set.
 */
bool jp2_set_decode_area(  jp2_t *p_jp2,
                               opj_image_t* p_image,
                               uint32_t p_start_x, uint32_t p_start_y,
                               uint32_t p_end_x, uint32_t p_end_y,
                               event_mgr_t * p_manager );

/**
*
*/
bool jp2_get_tile(  jp2_t *p_jp2,
                        GrokStream *p_stream,
                        opj_image_t* p_image,
                        event_mgr_t * p_manager,
                        uint32_t tile_index );


/**
 *
 */
bool jp2_set_decoded_resolution_factor(jp2_t *p_jp2,
        uint32_t res_factor,
        event_mgr_t * p_manager);


/**
 * Dump some elements from the JP2 decompression structure .
 *
 *@param p_jp2        the jp2 codec.
 *@param flag        flag to describe what elements are dump.
 *@param out_stream      output stream where dump the elements.
 *
*/
void jp2_dump (jp2_t* p_jp2, int32_t flag, FILE* out_stream);

/**
 * Get the codestream info from a JPEG2000 codec.
 *
 *@param  p_jp2        jp2 codec.
 *
 *@return  the codestream information extract from the jpg2000 codec
 */
opj_codestream_info_v2_t* jp2_get_cstr_info(jp2_t* p_jp2);

/**
 * Get the codestream index from a JPEG2000 codec.
 *
 *@param  p_jp2        jp2 codec.
 *
 *@return  the codestream index extract from the jpg2000 codec
 */
opj_codestream_index_t* jp2_get_cstr_index(jp2_t* p_jp2);


/*@}*/

/*@}*/


}

