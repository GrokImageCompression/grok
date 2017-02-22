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
 * Copyright (c) 2010-2011, Kaori Hagihara
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
#include "opj_includes.h"

/** @defgroup JP2 JP2 - JPEG-2000 file format reader/writer */
/*@{*/

#define OPJ_BOX_SIZE	1024
#define OPJ_RESOLUTION_BOX_SIZE (4+4+10)

/** @name Local static functions */
/*@{*/

/*static void jp2_write_url(opj_cio_t *cio, char *Idx_file);*/

/**
 * Reads a IHDR box - Image Header box
 *
 * @param	p_image_header_data			pointer to actual data (already read from file)
 * @param	jp2							the jpeg2000 file codec.
 * @param	p_image_header_size			the size of the image header
 * @param	p_manager					the user event manager.
 *
 * @return	true if the image header is valid, false else.
 */
static bool opj_jp2_read_ihdr(  opj_jp2_t *jp2,
                                uint8_t *p_image_header_data,
                                uint32_t p_image_header_size,
                                opj_event_mgr_t * p_manager );

/**
 * Writes the Image Header box - Image Header box.
 *
 * @param jp2					jpeg2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
*/
static uint8_t * opj_jp2_write_ihdr(opj_jp2_t *jp2,
                                    uint32_t * p_nb_bytes_written );


/**
* Reads an XML box
*
* @param	jp2					the jpeg2000 file codec.
* @param	p_xml_data			pointer to actual data (already read from file)
* @param	p_xml_size			the size of the image header
* @param	p_manager			the user event manager.
*
* @return	true if the image header is valid, false else.
*/
static bool opj_jp2_read_xml(opj_jp2_t *jp2,
	uint8_t *p_xml_data,
	uint32_t p_xml_size,
	opj_event_mgr_t * p_manager);

/**
* Writes the XML box
*
* @param jp2					jpeg2000 file codec.
* @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
*
* @return	the data being copied.
*/
static uint8_t * opj_jp2_write_xml(opj_jp2_t *jp2,
									uint32_t * p_nb_bytes_written);

/**
* Reads a Resolution box
*
* @param	p_resolution_data			pointer to actual data (already read from file)
* @param	jp2							the jpeg2000 file codec.
* @param	p_resolution_size			the size of the image header
* @param	p_manager					the user event manager.
*
* @return	true if the image header is valid, false else.
*/
static bool opj_jp2_read_res(opj_jp2_t *jp2,
	uint8_t *p_resolution_data,
	uint32_t p_resolution_size,
	opj_event_mgr_t * p_manager);

/**
* Writes the Resolution box 
*
* @param jp2					jpeg2000 file codec.
* @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
*
* @return	the data being copied.
*/
static uint8_t * opj_jp2_write_res(opj_jp2_t *jp2,
	uint32_t * p_nb_bytes_written);



/**
 * Writes the Bit per Component box.
 *
 * @param	jp2						jpeg2000 file codec.
 * @param	p_nb_bytes_written		pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
*/
static uint8_t * opj_jp2_write_bpcc(	opj_jp2_t *jp2,
                                        uint32_t * p_nb_bytes_written );

/**
 * Reads a Bit per Component box.
 *
 * @param	p_bpc_header_data			pointer to actual data (already read from file)
 * @param	jp2							the jpeg2000 file codec.
 * @param	p_bpc_header_size			the size of the bpc header
 * @param	p_manager					the user event manager.
 *
 * @return	true if the bpc header is valid, false else.
 */
static bool opj_jp2_read_bpcc(  opj_jp2_t *jp2,
                                uint8_t * p_bpc_header_data,
                                uint32_t p_bpc_header_size,
                                opj_event_mgr_t * p_manager );

static bool opj_jp2_read_cdef(	opj_jp2_t * jp2,
                                uint8_t * p_cdef_header_data,
                                uint32_t p_cdef_header_size,
                                opj_event_mgr_t * p_manager );

static void opj_jp2_apply_cdef(opj_image_t *image, opj_jp2_color_t *color, opj_event_mgr_t *);

/**
 * Writes the Channel Definition box.
 *
 * @param jp2					jpeg2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t * opj_jp2_write_cdef(   opj_jp2_t *jp2,
                                       uint32_t * p_nb_bytes_written );

/**
 * Writes the Colour Specification box.
 *
 * @param jp2					jpeg2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
*/
static uint8_t * opj_jp2_write_colr(   opj_jp2_t *jp2,
                                       uint32_t * p_nb_bytes_written );

/**
 * Writes a FTYP box - File type box
 *
 * @param	cio			the stream to write data to.
 * @param	jp2			the jpeg2000 file codec.
 * @param	p_manager	the user event manager.
 *
 * @return	true if writing was successful.
 */
static bool opj_jp2_write_ftyp(	opj_jp2_t *jp2,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager );

/**
 * Reads a a FTYP box - File type box
 *
 * @param	p_header_data	the data contained in the FTYP box.
 * @param	jp2				the jpeg2000 file codec.
 * @param	p_header_size	the size of the data contained in the FTYP box.
 * @param	p_manager		the user event manager.
 *
 * @return true if the FTYP box is valid.
 */
static bool opj_jp2_read_ftyp(	opj_jp2_t *jp2,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                opj_event_mgr_t * p_manager );

static bool opj_jp2_skip_jp2c(	opj_jp2_t *jp2,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager );

/**
 * Reads the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param	p_header_data	the data contained in the file header box.
 * @param	jp2				the jpeg2000 file codec.
 * @param	p_header_size	the size of the data contained in the file header box.
 * @param	p_manager		the user event manager.
 *
 * @return true if the JP2 Header box was successfully recognized.
*/
static bool opj_jp2_read_jp2h(  opj_jp2_t *jp2,
                                uint8_t *p_header_data,
                                uint32_t p_header_size,
                                opj_event_mgr_t * p_manager );

/**
 * Writes the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param  jp2      the jpeg2000 file codec.
 * @param  stream      the stream to write data to.
 * @param  p_manager  user event manager.
 *
 * @return true if writing was successful.
 */
static bool opj_jp2_write_jp2h(opj_jp2_t *jp2,
                               opj_stream_private_t *stream,
                               opj_event_mgr_t * p_manager );

/**
 * Writes the Jpeg2000 codestream Header box - JP2C Header box. This function must be called AFTER the coding has been done.
 *
 * @param	cio			the stream to write data to.
 * @param	jp2			the jpeg2000 file codec.
 * @param	p_manager	user event manager.
 *
 * @return true if writing was successful.
*/
static bool opj_jp2_write_jp2c(	opj_jp2_t *jp2,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager );

/**
 * Reads a jpeg2000 file signature box.
 *
 * @param	p_header_data	the data contained in the signature box.
 * @param	jp2				the jpeg2000 file codec.
 * @param	p_header_size	the size of the data contained in the signature box.
 * @param	p_manager		the user event manager.
 *
 * @return true if the file signature box is valid.
 */
static bool opj_jp2_read_jp(opj_jp2_t *jp2,
                            uint8_t * p_header_data,
                            uint32_t p_header_size,
                            opj_event_mgr_t * p_manager);

/**
 * Writes a jpeg2000 file signature box.
 *
 * @param cio the stream to write data to.
 * @param	jp2			the jpeg2000 file codec.
 * @param p_manager the user event manager.
 *
 * @return true if writing was successful.
 */
static bool opj_jp2_write_jp(	opj_jp2_t *jp2,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager );

/**
Apply collected palette data
@param color Collector for profile, cdef and pclr data
@param image
*/
static void opj_jp2_apply_pclr(opj_image_t *image, opj_jp2_color_t *color);

static void opj_jp2_free_pclr(opj_jp2_color_t *color);

/**
 * Collect palette data
 *
 * @param jp2 JP2 handle
 * @param p_pclr_header_data    FIXME DOC
 * @param p_pclr_header_size    FIXME DOC
 * @param p_manager
 *
 * @return Returns true if successful, returns false otherwise
*/
static bool opj_jp2_read_pclr(	opj_jp2_t *jp2,
                                uint8_t * p_pclr_header_data,
                                uint32_t p_pclr_header_size,
                                opj_event_mgr_t * p_manager );

/**
 * Collect component mapping data
 *
 * @param jp2                 JP2 handle
 * @param p_cmap_header_data  FIXME DOC
 * @param p_cmap_header_size  FIXME DOC
 * @param p_manager           FIXME DOC
 *
 * @return Returns true if successful, returns false otherwise
*/

static bool opj_jp2_read_cmap(	opj_jp2_t * jp2,
                                uint8_t * p_cmap_header_data,
                                uint32_t p_cmap_header_size,
                                opj_event_mgr_t * p_manager );

/**
 * Reads the Color Specification box.
 *
 * @param	p_colr_header_data			pointer to actual data (already read from file)
 * @param	jp2							the jpeg2000 file codec.
 * @param	p_colr_header_size			the size of the color header
 * @param	p_manager					the user event manager.
 *
 * @return	true if the bpc header is valid, false else.
*/
static bool opj_jp2_read_colr(  opj_jp2_t *jp2,
                                uint8_t * p_colr_header_data,
                                uint32_t p_colr_header_size,
                                opj_event_mgr_t * p_manager );

/*@}*/

/*@}*/

/**
 * Sets up the procedures to do on writing header after the codestream.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool opj_jp2_setup_end_header_writing (opj_jp2_t *jp2, opj_event_mgr_t * p_manager);

/**
 * Sets up the procedures to do on reading header after the codestream.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool opj_jp2_setup_end_header_reading (opj_jp2_t *jp2, opj_event_mgr_t * p_manager);

/**
 * Reads a jpeg2000 file header structure.
 *
 * @param jp2 the jpeg2000 file header structure.
 * @param stream the stream to read data from.
 * @param p_manager the user event manager.
 *
 * @return true if the box is valid.
 */
static bool opj_jp2_read_header_procedure(  opj_jp2_t *jp2,
        opj_stream_private_t *stream,
        opj_event_mgr_t * p_manager );

/**
 * Executes the given procedures on the given codec.
 *
 * @param	p_procedure_list	the list of procedures to execute
 * @param	jp2					the jpeg2000 file codec to execute the procedures on.
 * @param	stream					the stream to execute the procedures on.
 * @param	p_manager			the user manager.
 *
 * @return	true				if all the procedures were successfully executed.
 */
static bool opj_jp2_exec (  opj_jp2_t * jp2,
                            opj_procedure_list_t * p_procedure_list,
                            opj_stream_private_t *stream,
                            opj_event_mgr_t * p_manager );

/**
 * Reads a box header. The box is the way data is packed inside a jpeg2000 file structure.
 *
 * @param	cio						the input stream to read data from.
 * @param	box						the box structure to fill.
 * @param	p_number_bytes_read		pointer to an int that will store the number of bytes read from the stream (should usually be 2).
 * @param	p_manager				user event manager.
 *
 * @return	true if the box is recognized, false otherwise
*/
static bool opj_jp2_read_boxhdr(opj_jp2_box_t *box,
                                uint32_t * p_number_bytes_read,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool opj_jp2_setup_encoding_validation (opj_jp2_t *jp2, opj_event_mgr_t * p_manager);

/**
 * Sets up the procedures to do on writing header. Developers wanting to extend the library can add their own writing procedures.
 */
static bool opj_jp2_setup_header_writing (opj_jp2_t *jp2, opj_event_mgr_t * p_manager);

static bool opj_jp2_default_validation (	opj_jp2_t * jp2,
        opj_stream_private_t *cio,
        opj_event_mgr_t * p_manager );

/**
 * Finds the image execution function related to the given box id.
 *
 * @param	p_id	the id of the handler to fetch.
 *
 * @return	the given handler or NULL if it could not be found.
 */
static const opj_jp2_header_handler_t * opj_jp2_img_find_handler (uint32_t p_id);

/**
 * Finds the execution function related to the given box id.
 *
 * @param	p_id	the id of the handler to fetch.
 *
 * @return	the given handler or NULL if it could not be found.
 */
static const opj_jp2_header_handler_t * opj_jp2_find_handler (uint32_t p_id );

static const opj_jp2_header_handler_t jp2_header [] = {
    {JP2_JP,opj_jp2_read_jp},
    {JP2_FTYP,opj_jp2_read_ftyp},
    {JP2_JP2H,opj_jp2_read_jp2h},
	{ JP2_XML, opj_jp2_read_xml}
};

static const opj_jp2_header_handler_t jp2_img_header [] = {
    {JP2_IHDR,opj_jp2_read_ihdr},
    {JP2_COLR,opj_jp2_read_colr},
    {JP2_BPCC,opj_jp2_read_bpcc},
    {JP2_PCLR,opj_jp2_read_pclr},
    {JP2_CMAP,opj_jp2_read_cmap},
    {JP2_CDEF,opj_jp2_read_cdef},
	{JP2_RES, opj_jp2_read_res}

};

/**
 * Reads a box header. The box is the way data is packed inside a jpeg2000 file structure. Data is read from a character string
 *
 * @param	box						the box structure to fill.
 * @param	p_data					the character string to read data from.
 * @param	p_number_bytes_read		pointer to an int that will store the number of bytes read from the stream (should usually be 2).
 * @param	p_box_max_size			the maximum number of bytes in the box.
 * @param	p_manager         FIXME DOC
 *
 * @return	true if the box is recognized, false otherwise
*/
static bool opj_jp2_read_boxhdr_char(   opj_jp2_box_t *box,
                                        uint8_t * p_data,
                                        uint32_t * p_number_bytes_read,
                                        int64_t p_box_max_size,
                                        opj_event_mgr_t * p_manager );

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool opj_jp2_setup_decoding_validation (opj_jp2_t *jp2, opj_event_mgr_t * p_manager);

/**
 * Sets up the procedures to do on reading header.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool opj_jp2_setup_header_reading (opj_jp2_t *jp2, opj_event_mgr_t * p_manager);

/* ----------------------------------------------------------------------- */
static bool opj_jp2_read_boxhdr(opj_jp2_box_t *box,
                                uint32_t * p_number_bytes_read,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager )
{
    /* read header from file */
    uint8_t l_data_header [8];

    /* preconditions */
    assert(cio != nullptr);
    assert(box != nullptr);
    assert(p_number_bytes_read != nullptr);
    assert(p_manager != nullptr);

    *p_number_bytes_read = (uint32_t)opj_stream_read_data(cio,l_data_header,8,p_manager);
    if (*p_number_bytes_read != 8) {
        return false;
    }

    /* process read data */
    opj_read_bytes(l_data_header,&(box->length), 4);
    opj_read_bytes(l_data_header+4,&(box->type), 4);

    if(box->length == 0) { /* last box */
        const int64_t bleft = opj_stream_get_number_byte_left(cio);
        if (bleft > (int64_t)(0xFFFFFFFFU - 8U)) {
            opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box sizes higher than 2^32\n");
            return false;
        }
        box->length = (uint32_t)bleft + 8U;
        assert( (int64_t)box->length == bleft + 8 );
        return true;
    }

    /* do we have a "special very large box ?" */
    /* read then the XLBox */
    if (box->length == 1) {
        uint32_t l_xl_part_size;

        uint32_t l_nb_bytes_read = (uint32_t)opj_stream_read_data(cio,l_data_header,8,p_manager);
        if (l_nb_bytes_read != 8) {
            if (l_nb_bytes_read > 0) {
                *p_number_bytes_read += l_nb_bytes_read;
            }

            return false;
        }

        *p_number_bytes_read = 16;
        opj_read_bytes(l_data_header,&l_xl_part_size, 4);
        if (l_xl_part_size != 0) {
            opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box sizes higher than 2^32\n");
            return false;
        }
        opj_read_bytes(l_data_header+4,&(box->length), 4);
    }
    return true;
}

#if 0
static void jp2_write_url(opj_cio_t *cio, char *Idx_file)
{
    uint32_t i;
    opj_jp2_box_t box;

    box.init_pos = cio_tell(cio);
    cio_skip(cio, 4);
    cio_write(cio, JP2_URL, 4);	/* DBTL */
    cio_write(cio, 0, 1);		/* VERS */
    cio_write(cio, 0, 3);		/* FLAG */

    if(Idx_file) {
        for (i = 0; i < strlen(Idx_file); i++) {
            cio_write(cio, Idx_file[i], 1);
        }
    }

    box.length = cio_tell(cio) - box.init_pos;
    cio_seek(cio, box.init_pos);
    cio_write(cio, box.length, 4);	/* L */
    cio_seek(cio, box.init_pos + box.length);
}
#endif

static bool opj_jp2_read_ihdr( opj_jp2_t *jp2,
                               uint8_t *p_image_header_data,
                               uint32_t p_image_header_size,
                               opj_event_mgr_t * p_manager )
{
    /* preconditions */
    assert(p_image_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

	if (jp2->comps != NULL) {
		opj_event_msg(p_manager, EVT_WARNING, "Ignoring ihdr box. First ihdr box already read\n");
		return OPJ_TRUE;
	}
	
	if (p_image_header_size != 14) {
        opj_event_msg(p_manager, EVT_ERROR, "Bad image header box (bad size)\n");
        return false;
    }

    opj_read_bytes(p_image_header_data,&(jp2->h),4);			/* HEIGHT */
    p_image_header_data += 4;
    opj_read_bytes(p_image_header_data,&(jp2->w),4);			/* WIDTH */
    p_image_header_data += 4;
    opj_read_bytes(p_image_header_data,&(jp2->numcomps),2);		/* NC */
    p_image_header_data += 2;

	if ((jp2->numcomps == 0) ||
		(jp2->numcomps > OPJ_MAX_NUM_COMPONENTS)) {
		opj_event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: num components=%d does not conform to standard\n", jp2->numcomps);
		return false;
	}

    /* allocate memory for components */
    jp2->comps = (opj_jp2_comps_t*) opj_calloc(jp2->numcomps, sizeof(opj_jp2_comps_t));
    if (jp2->comps == 0) {
        opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to handle image header (ihdr)\n");
        return false;
    }

    opj_read_bytes(p_image_header_data,&(jp2->bpc),1);			/* BPC */
    ++ p_image_header_data;

	///////////////////////////////////////////////////
	// (bits per component == precision -1)
	// Value of 0xFF indicates that bits per component
	// varies by component

	// Otherwise, low 7 bits of bpc determine bits per component,
	// and high bit set indicates signed data,
	// unset indicates unsigned data
	if (((jp2->bpc != 0xFF) &&
		((jp2->bpc & 0x7F) > (OPJ_MAX_PRECISION - 1)))) {
		opj_event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: bpc=%d does not conform to standard\n", jp2->bpc);
		return false;
	}

    opj_read_bytes(p_image_header_data,&(jp2->C),1);			/* C */
    ++ p_image_header_data;

    /* Should be equal to 7 cf. chapter about image header box of the norm */
    if (jp2->C != 7) {
        opj_event_msg(p_manager, EVT_INFO, "JP2 IHDR box: compression type indicate that the file is not a conforming JP2 file (%d) \n", jp2->C);
    }

    opj_read_bytes(p_image_header_data,&(jp2->UnkC),1);			/* UnkC */
    ++ p_image_header_data;

	// UnkC must be binary : {0,1}
	if ((jp2->UnkC > 1)) {
		opj_event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: UnkC=%d does not conform to standard\n", jp2->UnkC);
		return false;
	}

    opj_read_bytes(p_image_header_data,&(jp2->IPR),1);			/* IPR */
    ++ p_image_header_data;

	// IPR must be binary : {0,1}
	if ((jp2->IPR > 1) ) {
		opj_event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: IPR=%d does not conform to standard\n",jp2->IPR);
		return false;
	}

    return true;
}

static uint8_t * opj_jp2_write_ihdr(opj_jp2_t *jp2,
                                    uint32_t * p_nb_bytes_written
                                   )
{
    uint8_t * l_ihdr_data,* l_current_ihdr_ptr;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);

    /* default image header is 22 bytes wide */
    l_ihdr_data = (uint8_t *) opj_calloc(1,22);
    if (l_ihdr_data == nullptr) {
        return nullptr;
    }

    l_current_ihdr_ptr = l_ihdr_data;

    opj_write_bytes(l_current_ihdr_ptr,22,4);				/* write box size */
    l_current_ihdr_ptr+=4;

    opj_write_bytes(l_current_ihdr_ptr,JP2_IHDR, 4);		/* IHDR */
    l_current_ihdr_ptr+=4;

    opj_write_bytes(l_current_ihdr_ptr,jp2->h, 4);		/* HEIGHT */
    l_current_ihdr_ptr+=4;

    opj_write_bytes(l_current_ihdr_ptr, jp2->w, 4);		/* WIDTH */
    l_current_ihdr_ptr+=4;

    opj_write_bytes(l_current_ihdr_ptr, jp2->numcomps, 2);		/* NC */
    l_current_ihdr_ptr+=2;

    opj_write_bytes(l_current_ihdr_ptr, jp2->bpc, 1);		/* BPC */
    ++l_current_ihdr_ptr;

    opj_write_bytes(l_current_ihdr_ptr, jp2->C, 1);		/* C : Always 7 */
    ++l_current_ihdr_ptr;

    opj_write_bytes(l_current_ihdr_ptr, jp2->UnkC, 1);		/* UnkC, colorspace unknown */
    ++l_current_ihdr_ptr;

    opj_write_bytes(l_current_ihdr_ptr, jp2->IPR, 1);		/* IPR, no intellectual property */
    ++l_current_ihdr_ptr;

    *p_nb_bytes_written = 22;

    return l_ihdr_data;
}


static bool opj_jp2_read_xml(opj_jp2_t *jp2,
							uint8_t *p_xml_data,
							uint32_t p_xml_size,
							opj_event_mgr_t * p_manager) {

	if (!p_xml_data || !p_xml_size) {
		return false;
	}
	jp2->xmlSize = p_xml_size;
	if (jp2->xml)
		opj_free(jp2->xml);
	jp2->xml = (uint8_t*)opj_malloc(p_xml_size);
	if (!jp2->xml)
		return false;
	memcpy(jp2->xml, p_xml_data, p_xml_size);
	return true;
}

static uint8_t * opj_jp2_write_xml(opj_jp2_t *jp2,
	uint32_t * p_nb_bytes_written) {

	/* room for 8 bytes for box and jp2->xmlSize bytes */
	uint32_t total_xml_size = 8 + (uint32_t)jp2->xmlSize;
	uint8_t * l_xml_data=nullptr, *l_current_xml_ptr=nullptr;

	/* preconditions */
	assert(jp2 != nullptr);
	assert(p_nb_bytes_written != nullptr);

	l_xml_data = (uint8_t *)opj_calloc(1, total_xml_size);
	if (l_xml_data == nullptr) {
		return nullptr;
	}

	l_current_xml_ptr = l_xml_data;

	opj_write_bytes(l_current_xml_ptr, total_xml_size, 4);			/* write box size */
	l_current_xml_ptr += 4;

	opj_write_bytes(l_current_xml_ptr, JP2_XML, 4);					/* JP2_XML */
	l_current_xml_ptr += 4;

	memcpy(l_current_xml_ptr, jp2->xml, jp2->xmlSize);				/* xml data */

	*p_nb_bytes_written = total_xml_size;
	return l_xml_data;

}


double calc_res(uint16_t num, uint16_t den, int8_t exponent) {
	if (den == 0)
		return 0;
	return ((double)num / den) * pow(10, exponent);
}

static bool opj_jp2_read_res_box(uint32_t *id,
								 uint32_t *num,
								 uint32_t *den,
								 uint32_t *exponent,
								uint8_t **p_resolution_data,
								opj_event_mgr_t * p_manager) {

	uint32_t box_size = 4 + 4 + 10;

	uint32_t size = 0;
	opj_read_bytes(*p_resolution_data, &size, 4);
	*p_resolution_data += 4;
	if (size != box_size)
		return false;

	opj_read_bytes(*p_resolution_data, id, 4);
	*p_resolution_data += 4;

	opj_read_bytes(*p_resolution_data, num+1, 2);
	*p_resolution_data += 2;

	opj_read_bytes(*p_resolution_data, den+1, 2);
	*p_resolution_data += 2;

	opj_read_bytes(*p_resolution_data, num, 2);
	*p_resolution_data += 2;

	opj_read_bytes(*p_resolution_data, den, 2);
	*p_resolution_data += 2;

	opj_read_bytes(*p_resolution_data, exponent+1, 1);
	*p_resolution_data += 1;

	opj_read_bytes(*p_resolution_data, exponent, 1);
	*p_resolution_data += 1;

	return true;

}

static bool opj_jp2_read_res(opj_jp2_t *jp2,
							uint8_t *p_resolution_data,
							uint32_t p_resolution_size,
							opj_event_mgr_t * p_manager){
	assert(p_resolution_data != nullptr);
	assert(jp2 != nullptr);
	assert(p_manager != nullptr);

	uint32_t num_boxes = p_resolution_size / OPJ_RESOLUTION_BOX_SIZE;
	if (num_boxes == 0 || 
		num_boxes > 2 || 
		(p_resolution_size % OPJ_RESOLUTION_BOX_SIZE) ) {
			opj_event_msg(p_manager, EVT_ERROR, "Bad resolution box (bad size)\n");
			return false;
	}

	while (p_resolution_size > 0) {

		uint32_t id;
		uint32_t num[2];
		uint32_t den[2];
		uint32_t exponent[2];

		if (!opj_jp2_read_res_box(&id,
			num,
			den,
			exponent,
			&p_resolution_data,
			p_manager)) {
				return false;
		}

		double* res;
		switch (id) {
		case JP2_CAPTURE_RES:
			res = jp2->capture_resolution;
			break;
		case JP2_DISPLAY_RES:
			res = jp2->display_resolution;
			break;
		default:
			return false;
		}
		for (int i = 0; i < 2; ++i)
			res[i] = calc_res(num[i], den[i], exponent[i]);

		p_resolution_size -= OPJ_RESOLUTION_BOX_SIZE;
	}
	return true;
}

void find_cf(double x, uint32_t* num, uint32_t* den) {
	// number of terms in continued fraction.
	// 15 is the max without precision errors for M_PI
	#define MAX 15
	const double eps = 1.0 / USHRT_MAX;
	long p[MAX], q[MAX], a[MAX];

	int i;
	//The first two convergents are 0/1 and 1/0
	p[0] = 0; 
	q[0] = 1;

	p[1] = 1;
	q[1] = 0;
	//The rest of the convergents (and continued fraction)
	for (i = 2; i<MAX; ++i) {
		a[i] = lrint(floor(x));
		p[i] = a[i] * p[i - 1] + p[i - 2];
		q[i] = a[i] * q[i - 1] + q[i - 2];
		//printf("%ld:  %ld/%ld\n", a[i], p[i], q[i]);
		if (fabs(x - a[i])<eps || (p[i] > USHRT_MAX) || (q[i] > USHRT_MAX))
			break;
		x = 1.0 / (x - a[i]);
	}
	*num = (uint32_t)p[i - 1];
	*den = (uint32_t)q[i - 1];
}

static void opj_jp2_write_res_box( double resx, double resy,
									uint32_t box_id,
									uint8_t **l_current_res_ptr) {

	opj_write_bytes(*l_current_res_ptr, OPJ_RESOLUTION_BOX_SIZE, 4);		/* write box size */
	*l_current_res_ptr += 4;

	opj_write_bytes(*l_current_res_ptr, JP2_CAPTURE_RES, 4);		/* Box ID */
	*l_current_res_ptr += 4;

	double res[2];
	// y is written first, then x
	res[0] = resy;
	res[1] = resx;

	uint32_t num[2];
	uint32_t den[2];
	int32_t exponent[2];

	for (size_t i = 0; i < 2; ++i) {
		exponent[i] = (int32_t)log(res[i]);
		if (exponent[i] < 1)
			exponent[i] = 0;
		if (exponent[i] >= 1) {
			res[i] /= pow(10, exponent[i]);
		}
		find_cf(res[i], num + i, den + i);
	}
	for (size_t i = 0; i < 2; ++i) {
		opj_write_bytes(*l_current_res_ptr, num[i], 2);
		*l_current_res_ptr += 2;
		opj_write_bytes(*l_current_res_ptr, den[i], 2);
		*l_current_res_ptr += 2;
	}
	for (size_t i = 0; i < 2; ++i) {
		opj_write_bytes(*l_current_res_ptr, exponent[i], 1);
		*l_current_res_ptr += 1;
	}
}

static uint8_t * opj_jp2_write_res(opj_jp2_t *jp2,
									uint32_t * p_nb_bytes_written)
{
	uint8_t * l_res_data, *l_current_res_ptr;
	assert(jp2);
	assert(p_nb_bytes_written);

	bool storeCapture = jp2->capture_resolution[0] > 0 &&
							jp2->capture_resolution[1] > 0;

	bool storeDisplay = jp2->display_resolution[0] > 0 &&
							jp2->display_resolution[1] > 0;

	uint32_t size = (4 + 4) + OPJ_RESOLUTION_BOX_SIZE;
	if (storeCapture && storeDisplay ) {
		size += OPJ_RESOLUTION_BOX_SIZE;
	}

	l_res_data = (uint8_t *)opj_calloc(1, size);
	if (l_res_data == nullptr) {
		return nullptr;
	}

	l_current_res_ptr = l_res_data;

	opj_write_bytes(l_current_res_ptr, size, 4);		/* write super-box size */
	l_current_res_ptr += 4;

	opj_write_bytes(l_current_res_ptr, JP2_RES, 4);		/* Super-box ID */
	l_current_res_ptr += 4;

	if (storeCapture) {
		opj_jp2_write_res_box(jp2->capture_resolution[0],
								jp2->capture_resolution[1],
								JP2_CAPTURE_RES,
								&l_current_res_ptr);
	}
	if (storeDisplay) {
		opj_jp2_write_res_box(jp2->display_resolution[0],
								jp2->display_resolution[1],
								JP2_DISPLAY_RES,
								&l_current_res_ptr);
	}
	*p_nb_bytes_written = size;
	return l_res_data;
}

static uint8_t * opj_jp2_write_bpcc(	opj_jp2_t *jp2,
                                        uint32_t * p_nb_bytes_written
                                   )
{
    uint32_t i;
    /* room for 8 bytes for box and 1 byte for each component */
    uint32_t l_bpcc_size = 8 + jp2->numcomps;
    uint8_t * l_bpcc_data,* l_current_bpcc_ptr;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);

    l_bpcc_data = (uint8_t *) opj_calloc(1,l_bpcc_size);
    if (l_bpcc_data == nullptr) {
        return nullptr;
    }

    l_current_bpcc_ptr = l_bpcc_data;

    opj_write_bytes(l_current_bpcc_ptr,l_bpcc_size,4);				/* write box size */
    l_current_bpcc_ptr += 4;

    opj_write_bytes(l_current_bpcc_ptr,JP2_BPCC,4);					/* BPCC */
    l_current_bpcc_ptr += 4;

    for (i = 0; i < jp2->numcomps; ++i)  {
        opj_write_bytes(l_current_bpcc_ptr, jp2->comps[i].bpcc, 1); /* write each component information */
        ++l_current_bpcc_ptr;
    }

    *p_nb_bytes_written = l_bpcc_size;

    return l_bpcc_data;
}

static bool opj_jp2_read_bpcc( opj_jp2_t *jp2,
                               uint8_t * p_bpc_header_data,
                               uint32_t p_bpc_header_size,
                               opj_event_mgr_t * p_manager
                             )
{
    uint32_t i;

    /* preconditions */
    assert(p_bpc_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);


    if (jp2->bpc != 255 ) {
        opj_event_msg(p_manager, EVT_WARNING, "A BPCC header box is available although BPC given by the IHDR box (%d) indicate components bit depth is constant\n",jp2->bpc);
    }

    /* and length is relevant */
    if (p_bpc_header_size != jp2->numcomps) {
        opj_event_msg(p_manager, EVT_ERROR, "Bad BPCC header box (bad size)\n");
        return false;
    }

    /* read info for each component */
    for (i = 0; i < jp2->numcomps; ++i) {
        opj_read_bytes(p_bpc_header_data,&jp2->comps[i].bpcc ,1);	/* read each BPCC component */
        ++p_bpc_header_data;
    }

    return true;
}
static uint8_t * opj_jp2_write_cdef(opj_jp2_t *jp2, uint32_t * p_nb_bytes_written)
{
    /* room for 8 bytes for box, 2 for n */
    uint32_t l_cdef_size = 10;
    uint8_t * l_cdef_data,* l_current_cdef_ptr;
    uint32_t l_value;
    uint16_t i;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);
    assert(jp2->color.jp2_cdef != nullptr);
    assert(jp2->color.jp2_cdef->info != nullptr);
    assert(jp2->color.jp2_cdef->n > 0U);

    l_cdef_size += 6U * jp2->color.jp2_cdef->n;

    l_cdef_data = (uint8_t *) opj_malloc(l_cdef_size);
    if (l_cdef_data == nullptr) {
        return nullptr;
    }

    l_current_cdef_ptr = l_cdef_data;

    opj_write_bytes(l_current_cdef_ptr,l_cdef_size,4);			/* write box size */
    l_current_cdef_ptr += 4;

    opj_write_bytes(l_current_cdef_ptr,JP2_CDEF,4);					/* BPCC */
    l_current_cdef_ptr += 4;

    l_value = jp2->color.jp2_cdef->n;
    opj_write_bytes(l_current_cdef_ptr,l_value,2);					/* N */
    l_current_cdef_ptr += 2;

    for (i = 0U; i < jp2->color.jp2_cdef->n; ++i) {
        l_value = jp2->color.jp2_cdef->info[i].cn;
        opj_write_bytes(l_current_cdef_ptr,l_value,2);					/* Cni */
        l_current_cdef_ptr += 2;
        l_value = jp2->color.jp2_cdef->info[i].typ;
        opj_write_bytes(l_current_cdef_ptr,l_value,2);					/* Typi */
        l_current_cdef_ptr += 2;
        l_value = jp2->color.jp2_cdef->info[i].asoc;
        opj_write_bytes(l_current_cdef_ptr,l_value,2);					/* Asoci */
        l_current_cdef_ptr += 2;
    }
    *p_nb_bytes_written = l_cdef_size;

    return l_cdef_data;
}

static uint8_t * opj_jp2_write_colr(  opj_jp2_t *jp2,
                                      uint32_t * p_nb_bytes_written
                                   )
{
    /* room for 8 bytes for box 3 for common data and variable upon profile*/
    uint32_t l_colr_size = 11;
    uint8_t * l_colr_data,* l_current_colr_ptr;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);
    assert(jp2->meth == 1 || jp2->meth == 2);

    switch (jp2->meth) {
    case 1 :
        l_colr_size += 4; /* EnumCS */
        break;
    case 2 :
        assert(jp2->color.icc_profile_len);	/* ICC profile */
        l_colr_size += jp2->color.icc_profile_len;
        break;
    default :
        return nullptr;
    }

    l_colr_data = (uint8_t *) opj_calloc(1,l_colr_size);
    if (l_colr_data == nullptr) {
        return nullptr;
    }

    l_current_colr_ptr = l_colr_data;

    opj_write_bytes(l_current_colr_ptr,l_colr_size,4);				/* write box size */
    l_current_colr_ptr += 4;

    opj_write_bytes(l_current_colr_ptr,JP2_COLR,4);					/* BPCC */
    l_current_colr_ptr += 4;

    opj_write_bytes(l_current_colr_ptr, jp2->meth,1);				/* METH */
    ++l_current_colr_ptr;

    opj_write_bytes(l_current_colr_ptr, jp2->precedence,1);			/* PRECEDENCE */
    ++l_current_colr_ptr;

    opj_write_bytes(l_current_colr_ptr, jp2->approx,1);				/* APPROX */
    ++l_current_colr_ptr;

    if (jp2->meth == 1) { /* Meth value is restricted to 1 or 2 (Table I.9 of part 1) */
        opj_write_bytes(l_current_colr_ptr, jp2->enumcs,4);
    }       /* EnumCS */
    else {
        if (jp2->meth == 2) {                                      /* ICC profile */
            uint32_t i;
            for(i = 0; i < jp2->color.icc_profile_len; ++i) {
                opj_write_bytes(l_current_colr_ptr, jp2->color.icc_profile_buf[i], 1);
                ++l_current_colr_ptr;
            }
        }
    }

    *p_nb_bytes_written = l_colr_size;

    return l_colr_data;
}

static void opj_jp2_free_pclr(opj_jp2_color_t *color)
{
	if (color) {
		if (color->jp2_pclr) {
			if (color->jp2_pclr->channel_sign) {
				opj_free(color->jp2_pclr->channel_sign);
			}
			if (color->jp2_pclr->channel_size) {
				opj_free(color->jp2_pclr->channel_size);
			}
			if (color->jp2_pclr->entries) {
				opj_free(color->jp2_pclr->entries);
			}

			if (color->jp2_pclr->cmap) {
				opj_free(color->jp2_pclr->cmap);
			}
			opj_free(color->jp2_pclr);
			color->jp2_pclr = nullptr;
		}
	}
}

static bool opj_jp2_check_color(opj_image_t *image, opj_jp2_color_t *color, opj_event_mgr_t *p_manager)
{
    uint16_t i;

    /* testcase 4149.pdf.SIGSEGV.cf7.3501 */
    if (color->jp2_cdef) {
        opj_jp2_cdef_info_t *info = color->jp2_cdef->info;
        uint16_t n = color->jp2_cdef->n;
        uint32_t nr_channels = image->numcomps; /* FIXME image->numcomps == jp2->numcomps before color is applied ??? */

        /* cdef applies to cmap channels if any */
        if (color->jp2_pclr && color->jp2_pclr->cmap) {
            nr_channels = (uint32_t)color->jp2_pclr->nr_channels;
        }

        for (i = 0; i < n; i++) {
            if (info[i].cn >= nr_channels) {
                opj_event_msg(p_manager, EVT_ERROR, "Invalid component index %d (>= %d).\n", info[i].cn, nr_channels);
                return false;
            }
            if (info[i].asoc == 65535U) continue;

            if (info[i].asoc > 0 && (uint32_t)(info[i].asoc - 1) >= nr_channels) {
                opj_event_msg(p_manager, EVT_ERROR, "Invalid component index %d (>= %d).\n", info[i].asoc - 1, nr_channels);
                return false;
            }
        }

        /* issue 397 */
        /* ISO 15444-1 states that if cdef is present, it shall contain a complete list of channel definitions. */
        while (nr_channels > 0) {
            for(i = 0; i < n; ++i) {
                if ((uint32_t)info[i].cn == (nr_channels - 1U)) {
                    break;
                }
            }
            if (i == n) {
                opj_event_msg(p_manager, EVT_ERROR, "Incomplete channel definitions.\n");
                return false;
            }
            --nr_channels;
        }
    }

    /* testcases 451.pdf.SIGSEGV.f4c.3723, 451.pdf.SIGSEGV.5b5.3723 and
       66ea31acbb0f23a2bbc91f64d69a03f5_signal_sigsegv_13937c0_7030_5725.pdf */
    if (color->jp2_pclr && color->jp2_pclr->cmap) {
        uint16_t nr_channels = color->jp2_pclr->nr_channels;
        opj_jp2_cmap_comp_t *cmap = color->jp2_pclr->cmap;
        bool *pcol_usage, is_sane = true;

        /* verify that all original components match an existing one */
        for (i = 0; i < nr_channels; i++) {
            if (cmap[i].cmp >= image->numcomps) {
                opj_event_msg(p_manager, EVT_ERROR, "Invalid component index %d (>= %d).\n", cmap[i].cmp, image->numcomps);
                is_sane = false;
            }
        }

        pcol_usage = (bool *) opj_calloc(nr_channels, sizeof(bool));
        if (!pcol_usage) {
            opj_event_msg(p_manager, EVT_ERROR, "Unexpected OOM.\n");
            return false;
        }
        /* verify that no component is targeted more than once */
        for (i = 0; i < nr_channels; i++) {
            uint16_t pcol = cmap[i].pcol;
            assert(cmap[i].mtyp == 0 || cmap[i].mtyp == 1);
            if (pcol >= nr_channels) {
                opj_event_msg(p_manager, EVT_ERROR, "Invalid component/palette index for direct mapping %d.\n", pcol);
                is_sane = false;
            } else if (pcol_usage[pcol] && cmap[i].mtyp == 1) {
                opj_event_msg(p_manager, EVT_ERROR, "Component %d is mapped twice.\n", pcol);
                is_sane = false;
            } else if (cmap[i].mtyp == 0 && cmap[i].pcol != 0) {
                /* I.5.3.5 PCOL: If the value of the MTYP field for this channel is 0, then
                 * the value of this field shall be 0. */
                opj_event_msg(p_manager, EVT_ERROR, "Direct use at #%d however pcol=%d.\n", i, pcol);
                is_sane = false;
            } else
                pcol_usage[pcol] = true;
        }
        /* verify that all components are targeted at least once */
        for (i = 0; i < nr_channels; i++) {
            if (!pcol_usage[i] && cmap[i].mtyp != 0) {
                opj_event_msg(p_manager, EVT_ERROR, "Component %d doesn't have a mapping.\n", i);
                is_sane = false;
            }
        }
        /* Issue 235/447 weird cmap */
        if (1 && is_sane && (image->numcomps==1U)) {
            for (i = 0; i < nr_channels; i++) {
                if (!pcol_usage[i]) {
                    is_sane = 0U;
                    opj_event_msg(p_manager, EVT_WARNING, "Component mapping seems wrong. Trying to correct.\n", i);
                    break;
                }
            }
            if (!is_sane) {
                is_sane = true;
                for (i = 0; i < nr_channels; i++) {
                    cmap[i].mtyp = 1U;
                    cmap[i].pcol = (uint8_t) i;
                }
            }
        }
        opj_free(pcol_usage);
        if (!is_sane) {
            return false;
        }
    }

    return true;
}

/* file9.jp2 */
static void opj_jp2_apply_pclr(opj_image_t *image, opj_jp2_color_t *color)
{
    opj_image_comp_t *old_comps, *new_comps;
    uint8_t *channel_size, *channel_sign;
    uint32_t *entries;
    opj_jp2_cmap_comp_t *cmap;
    int32_t *src, *dst;
    uint32_t j, max;
    uint16_t i, nr_channels, cmp, pcol;
    int32_t k, top_k;

    channel_size = color->jp2_pclr->channel_size;
    channel_sign = color->jp2_pclr->channel_sign;
    entries = color->jp2_pclr->entries;
    cmap = color->jp2_pclr->cmap;
    nr_channels = color->jp2_pclr->nr_channels;

    old_comps = image->comps;
    new_comps = (opj_image_comp_t*)
                opj_malloc(nr_channels * sizeof(opj_image_comp_t));
    if (!new_comps) {
        /* FIXME no error code for opj_jp2_apply_pclr */
        /* FIXME event manager error callback */
        return;
    }
    for(i = 0; i < nr_channels; ++i) {
        pcol = cmap[i].pcol;
        cmp = cmap[i].cmp;

        /* Direct use */
        if(cmap[i].mtyp == 0) {
            assert( pcol == 0 );
            new_comps[i] = old_comps[cmp];
            new_comps[i].data = NULL;
        } else {
            assert( i == pcol );
            new_comps[pcol] = old_comps[cmp];
            new_comps[pcol].data = NULL;
        }

        /* Palette mapping: */
        if (!opj_image_single_component_data_alloc(new_comps + i)) {
            opj_free(new_comps);
            new_comps = NULL;
            /* FIXME no error code for opj_jp2_apply_pclr */
            /* FIXME event manager error callback */
            return;
        }
        new_comps[i].prec = channel_size[i];
        new_comps[i].sgnd = channel_sign[i];
    }

    top_k = color->jp2_pclr->nr_entries - 1;

    for(i = 0; i < nr_channels; ++i) {
        /* Palette mapping: */
        cmp = cmap[i].cmp;
        pcol = cmap[i].pcol;
        src = old_comps[cmp].data;
        assert( src );
        max = new_comps[pcol].w * new_comps[pcol].h;

        /* Direct use: */
        if(cmap[i].mtyp == 0) {
            assert( cmp == 0 );
            dst = new_comps[i].data;
            assert( dst );
            for(j = 0; j < max; ++j) {
                dst[j] = src[j];
            }
        } else {
            assert( i == pcol );
            dst = new_comps[pcol].data;
            assert( dst );
            for(j = 0; j < max; ++j) {
                /* The index */
                if((k = src[j]) < 0) k = 0;
                else if(k > top_k) k = top_k;

                /* The colour */
                dst[j] = (int32_t)entries[k * nr_channels + pcol];
            }
        }
    }

    max = image->numcomps;
    for (i = 0; i < max; ++i) {
        opj_image_single_component_data_free(old_comps + i);
    }
    opj_free(old_comps);
    image->comps = new_comps;
    image->numcomps = nr_channels;

}/* apply_pclr() */

static bool opj_jp2_read_pclr(	opj_jp2_t *jp2,
                                uint8_t * p_pclr_header_data,
                                uint32_t p_pclr_header_size,
                                opj_event_mgr_t * p_manager
                             )
{
    opj_jp2_pclr_t *jp2_pclr;
    uint8_t *channel_size, *channel_sign;
    uint32_t *entries;
    uint16_t nr_entries,nr_channels;
    uint16_t i, j;
    uint32_t l_value;
    uint8_t *orig_header_data = p_pclr_header_data;

    /* preconditions */
    assert(p_pclr_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);
    (void)p_pclr_header_size;

    if(jp2->color.jp2_pclr)
        return false;

    if (p_pclr_header_size < 3)
        return false;

    opj_read_bytes(p_pclr_header_data, &l_value , 2);	/* NE */
    p_pclr_header_data += 2;
    nr_entries = (uint16_t) l_value;
    if ((nr_entries == 0U) || (nr_entries > 1024U)) {
        opj_event_msg(p_manager, EVT_ERROR, "Invalid PCLR box. Reports %d entries\n", (int)nr_entries);
        return false;
    }

    opj_read_bytes(p_pclr_header_data, &l_value , 1);	/* NPC */
    ++p_pclr_header_data;
    nr_channels = (uint16_t) l_value;
    if (nr_channels == 0U) {
        opj_event_msg(p_manager, EVT_ERROR, "Invalid PCLR box. Reports 0 palette columns\n");
        return false;
    }

    if (p_pclr_header_size < 3 + (uint32_t)nr_channels)
        return false;

    entries = (uint32_t*) opj_malloc((size_t)nr_channels * nr_entries * sizeof(uint32_t));
    if (!entries)
        return false;
    channel_size = (uint8_t*) opj_malloc(nr_channels);
    if (!channel_size) {
        opj_free(entries);
        return false;
    }
    channel_sign = (uint8_t*) opj_malloc(nr_channels);
    if (!channel_sign) {
        opj_free(entries);
        opj_free(channel_size);
        return false;
    }

    jp2_pclr = (opj_jp2_pclr_t*)opj_malloc(sizeof(opj_jp2_pclr_t));
    if (!jp2_pclr) {
        opj_free(entries);
        opj_free(channel_size);
        opj_free(channel_sign);
        return false;
    }

    jp2_pclr->channel_sign = channel_sign;
    jp2_pclr->channel_size = channel_size;
    jp2_pclr->entries = entries;
    jp2_pclr->nr_entries = nr_entries;
    jp2_pclr->nr_channels = (uint8_t) l_value;
    jp2_pclr->cmap = NULL;

    jp2->color.jp2_pclr = jp2_pclr;

    for(i = 0; i < nr_channels; ++i) {
        opj_read_bytes(p_pclr_header_data, &l_value , 1);	/* Bi */
        ++p_pclr_header_data;

        channel_size[i] = (uint8_t)((l_value & 0x7f) + 1);
        channel_sign[i] = (l_value & 0x80) ? 1 : 0;
    }

    for(j = 0; j < nr_entries; ++j) {
        for(i = 0; i < nr_channels; ++i) {
            uint32_t bytes_to_read = (uint32_t)((channel_size[i]+7)>>3);

            if (bytes_to_read > sizeof(uint32_t))
                bytes_to_read = sizeof(uint32_t);
            if ((ptrdiff_t)p_pclr_header_size < (ptrdiff_t)(p_pclr_header_data - orig_header_data) + (ptrdiff_t)bytes_to_read)
                return false;

            opj_read_bytes(p_pclr_header_data, &l_value , bytes_to_read);	/* Cji */
            p_pclr_header_data += bytes_to_read;
            *entries = (uint32_t) l_value;
            entries++;
        }
    }

    return true;
}

static bool opj_jp2_read_cmap(	opj_jp2_t * jp2,
                                uint8_t * p_cmap_header_data,
                                uint32_t p_cmap_header_size,
                                opj_event_mgr_t * p_manager
                             )
{
    opj_jp2_cmap_comp_t *cmap;
    uint8_t i, nr_channels;
    uint32_t l_value;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_cmap_header_data != nullptr);
    assert(p_manager != nullptr);
    (void)p_cmap_header_size;

    /* Need nr_channels: */
    if(jp2->color.jp2_pclr == NULL) {
        opj_event_msg(p_manager, EVT_ERROR, "Need to read a PCLR box before the CMAP box.\n");
        return false;
    }

    /* Part 1, I.5.3.5: 'There shall be at most one Component Mapping box
     * inside a JP2 Header box' :
    */
    if(jp2->color.jp2_pclr->cmap) {
        opj_event_msg(p_manager, EVT_ERROR, "Only one CMAP box is allowed.\n");
        return false;
    }

    nr_channels = jp2->color.jp2_pclr->nr_channels;
    if (p_cmap_header_size < (uint32_t)nr_channels * 4) {
        opj_event_msg(p_manager, EVT_ERROR, "Insufficient data for CMAP box.\n");
        return false;
    }

    cmap = (opj_jp2_cmap_comp_t*) opj_malloc(nr_channels * sizeof(opj_jp2_cmap_comp_t));
    if (!cmap)
        return false;


    for(i = 0; i < nr_channels; ++i) {
        opj_read_bytes(p_cmap_header_data, &l_value, 2);			/* CMP^i */
        p_cmap_header_data +=2;
        cmap[i].cmp = (uint16_t) l_value;

        opj_read_bytes(p_cmap_header_data, &l_value, 1);			/* MTYP^i */
        ++p_cmap_header_data;
        cmap[i].mtyp = (uint8_t) l_value;

        opj_read_bytes(p_cmap_header_data, &l_value, 1);			/* PCOL^i */
        ++p_cmap_header_data;
        cmap[i].pcol = (uint8_t) l_value;
    }

    jp2->color.jp2_pclr->cmap = cmap;

    return true;
}

static void opj_jp2_apply_cdef(opj_image_t *image, opj_jp2_color_t *color, opj_event_mgr_t *manager)
{
    opj_jp2_cdef_info_t *info;
    uint16_t i, n, cn, asoc, acn;

    info = color->jp2_cdef->info;
    n = color->jp2_cdef->n;

    for(i = 0; i < n; ++i) {
        /* WATCH: acn = asoc - 1 ! */
        asoc = info[i].asoc;
        cn = info[i].cn;

        if( cn >= image->numcomps) {
            opj_event_msg(manager, EVT_WARNING, "opj_jp2_apply_cdef: cn=%d, numcomps=%d\n", cn, image->numcomps);
            continue;
        }
        if(asoc == 0 || asoc == 65535) {
            image->comps[cn].alpha = info[i].typ;
            continue;
        }

        acn = (uint16_t)(asoc - 1);
        if( acn >= image->numcomps ) {
            opj_event_msg(manager, EVT_WARNING, "opj_jp2_apply_cdef: acn=%d, numcomps=%d\n", acn, image->numcomps);
            continue;
        }

        /* Swap only if color channel */
        if((cn != acn) && (info[i].typ == 0)) {
            opj_image_comp_t saved;
            uint16_t j;

            memcpy(&saved, &image->comps[cn], sizeof(opj_image_comp_t));
            memcpy(&image->comps[cn], &image->comps[acn], sizeof(opj_image_comp_t));
            memcpy(&image->comps[acn], &saved, sizeof(opj_image_comp_t));

            /* Swap channels in following channel definitions, don't bother with j <= i that are already processed */
            for (j = (uint16_t)(i + 1U); j < n ; ++j) {
                if (info[j].cn == cn) {
                    info[j].cn = acn;
                } else if (info[j].cn == acn) {
                    info[j].cn = cn;
                }
                /* asoc is related to color index. Do not update. */
            }
        }

        image->comps[cn].alpha = info[i].typ;
    }

    if(color->jp2_cdef->info) opj_free(color->jp2_cdef->info);

    opj_free(color->jp2_cdef);
    color->jp2_cdef = NULL;

}/* jp2_apply_cdef() */

static bool opj_jp2_read_cdef(	opj_jp2_t * jp2,
                                uint8_t * p_cdef_header_data,
                                uint32_t p_cdef_header_size,
                                opj_event_mgr_t * p_manager
                             )
{
    opj_jp2_cdef_info_t *cdef_info;
    uint16_t i;
    uint32_t l_value;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_cdef_header_data != nullptr);
    assert(p_manager != nullptr);
    (void)p_cdef_header_size;

    /* Part 1, I.5.3.6: 'The shall be at most one Channel Definition box
     * inside a JP2 Header box.'*/
    if(jp2->color.jp2_cdef) return false;

    if (p_cdef_header_size < 2) {
        opj_event_msg(p_manager, EVT_ERROR, "CDEF box: Insufficient data.\n");
        return false;
    }

    opj_read_bytes(p_cdef_header_data,&l_value ,2);			/* N */
    p_cdef_header_data+= 2;

    if ( (uint16_t)l_value == 0) { /* szukw000: FIXME */
        opj_event_msg(p_manager, EVT_ERROR, "CDEF box: Number of channel description is equal to zero.\n");
        return false;
    }

    if (p_cdef_header_size < 2 + (uint32_t)(uint16_t)l_value * 6) {
        opj_event_msg(p_manager, EVT_ERROR, "CDEF box: Insufficient data.\n");
        return false;
    }

    cdef_info = (opj_jp2_cdef_info_t*) opj_malloc(l_value * sizeof(opj_jp2_cdef_info_t));
    if (!cdef_info)
        return false;

    jp2->color.jp2_cdef = (opj_jp2_cdef_t*)opj_malloc(sizeof(opj_jp2_cdef_t));
    if(!jp2->color.jp2_cdef) {
        opj_free(cdef_info);
        return false;
    }
    jp2->color.jp2_cdef->info = cdef_info;
    jp2->color.jp2_cdef->n = (uint16_t) l_value;

    for(i = 0; i < jp2->color.jp2_cdef->n; ++i) {
        opj_read_bytes(p_cdef_header_data, &l_value, 2);			/* Cn^i */
        p_cdef_header_data +=2;
        cdef_info[i].cn = (uint16_t) l_value;

        opj_read_bytes(p_cdef_header_data, &l_value, 2);			/* Typ^i */
        p_cdef_header_data +=2;
        cdef_info[i].typ = (uint16_t) l_value;

        opj_read_bytes(p_cdef_header_data, &l_value, 2);			/* Asoc^i */
        p_cdef_header_data +=2;
        cdef_info[i].asoc = (uint16_t) l_value;
    }

	// cdef sanity check
	// 1. check for multiple descriptions of the same component with different types
	for (i = 0; i < jp2->color.jp2_cdef->n; ++i) {
		auto infoi = cdef_info[i];
		for (uint16_t j = 0; j < jp2->color.jp2_cdef->n; ++j) {
			auto infoj = cdef_info[j];
			if (i != j && infoi.cn == infoj.cn && infoi.typ != infoj.typ) {
				opj_event_msg(p_manager, EVT_ERROR, "CDEF box : multiple descriptions of component, %d, with differing types : %d and %d.\n", infoi.cn, infoi.typ, infoj.typ);
				return false;
			}
		}
	}

	// 2. check that type/association pairs are unique
	for (i = 0; i < jp2->color.jp2_cdef->n; ++i) {
		auto infoi = cdef_info[i];
		for (uint16_t j = 0; j < jp2->color.jp2_cdef->n; ++j) {
			auto infoj = cdef_info[j];
			if (i != j && 
				infoi.cn != infoj.cn &&
					infoi.typ == infoj.typ &&
						infoi.asoc == infoj.asoc) {
				opj_event_msg(p_manager, EVT_ERROR, "CDEF box : components %d and %d share same type/association pair (%d,%d).\n", infoi.cn, infoj.cn, infoj.typ, infoj.asoc);
				return false;
			}
		}
	}

    return true;
}

static bool opj_jp2_read_colr( opj_jp2_t *jp2,
                               uint8_t * p_colr_header_data,
                               uint32_t p_colr_header_size,
                               opj_event_mgr_t * p_manager
                             )
{
    uint32_t l_value;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_colr_header_data != nullptr);
    assert(p_manager != nullptr);

    if (p_colr_header_size < 3) {
        opj_event_msg(p_manager, EVT_ERROR, "Bad COLR header box (bad size)\n");
        return false;
    }

    /* Part 1, I.5.3.3 : 'A conforming JP2 reader shall ignore all Colour
     * Specification boxes after the first.'
    */
    if(jp2->color.jp2_has_colr) {
        opj_event_msg(p_manager, EVT_INFO, "A conforming JP2 reader shall ignore all Colour Specification boxes after the first, so we ignore this one.\n");
        p_colr_header_data += p_colr_header_size;
        return true;
    }

    opj_read_bytes(p_colr_header_data,&jp2->meth ,1);			/* METH */
    ++p_colr_header_data;

    opj_read_bytes(p_colr_header_data,&jp2->precedence ,1);		/* PRECEDENCE */
    ++p_colr_header_data;

    opj_read_bytes(p_colr_header_data,&jp2->approx ,1);			/* APPROX */
    ++p_colr_header_data;

    if (jp2->meth == 1) {
        if (p_colr_header_size < 7) {
            opj_event_msg(p_manager, EVT_ERROR, "Bad COLR header box (bad size: %d)\n", p_colr_header_size);
            return false;
        }
        if ((p_colr_header_size > 7) && (jp2->enumcs != 14)) { /* handled below for CIELab) */
            /* testcase Altona_Technical_v20_x4.pdf */
            opj_event_msg(p_manager, EVT_WARNING, "Bad COLR header box (bad size: %d)\n", p_colr_header_size);
        }

        opj_read_bytes(p_colr_header_data,&jp2->enumcs ,4);			/* EnumCS */

        p_colr_header_data += 4;

        if(jp2->enumcs == 14) { /* CIELab */
            uint32_t *cielab;
            uint32_t rl, ol, ra, oa, rb, ob, il;

            cielab = (uint32_t*)opj_malloc(9 * sizeof(uint32_t));
			if (cielab == NULL) {
				opj_event_msg(p_manager, EVT_ERROR, "Not enough memory for cielab\n");
				return false;
			}
            cielab[0] = 14; /* enumcs */

            /* default values */
            rl = ra = rb = ol = oa = ob = 0;
            il = 0x00443530; /* D50 */
            cielab[1] = 0x44454600;/* DEF */

            if(p_colr_header_size == 35) {
                opj_read_bytes(p_colr_header_data, &rl, 4);
                p_colr_header_data += 4;
                opj_read_bytes(p_colr_header_data, &ol, 4);
                p_colr_header_data += 4;
                opj_read_bytes(p_colr_header_data, &ra, 4);
                p_colr_header_data += 4;
                opj_read_bytes(p_colr_header_data, &oa, 4);
                p_colr_header_data += 4;
                opj_read_bytes(p_colr_header_data, &rb, 4);
                p_colr_header_data += 4;
                opj_read_bytes(p_colr_header_data, &ob, 4);
                p_colr_header_data += 4;
                opj_read_bytes(p_colr_header_data, &il, 4);
                p_colr_header_data += 4;

                cielab[1] = 0;
            } else if(p_colr_header_size != 7) {
                opj_event_msg(p_manager, EVT_WARNING, "Bad COLR header box (CIELab, bad size: %d)\n", p_colr_header_size);
            }
            cielab[2] = rl;
            cielab[4] = ra;
            cielab[6] = rb;
            cielab[3] = ol;
            cielab[5] = oa;
            cielab[7] = ob;
            cielab[8] = il;

            jp2->color.icc_profile_buf = (uint8_t*)cielab;
            jp2->color.icc_profile_len = 0;
        }
        jp2->color.jp2_has_colr = 1;
    } else if (jp2->meth == 2) {
        /* ICC profile */
        int32_t it_icc_value = 0;
        int32_t icc_len = (int32_t)p_colr_header_size - 3;

        jp2->color.icc_profile_len = (uint32_t)icc_len;
        jp2->color.icc_profile_buf = (uint8_t*) opj_calloc(1,(size_t)icc_len);
        if (!jp2->color.icc_profile_buf) {
            jp2->color.icc_profile_len = 0;
            return false;
        }

        for (it_icc_value = 0; it_icc_value < icc_len; ++it_icc_value) {
            opj_read_bytes(p_colr_header_data,&l_value,1);		/* icc values */
            ++p_colr_header_data;
            jp2->color.icc_profile_buf[it_icc_value] = (uint8_t) l_value;
        }

        jp2->color.jp2_has_colr = 1;
    } else if (jp2->meth > 2) {
        /*	ISO/IEC 15444-1:2004 (E), Table I.9 Legal METH values:
        conforming JP2 reader shall ignore the entire Colour Specification box.*/
        opj_event_msg(p_manager, EVT_INFO, "COLR BOX meth value is not a regular value (%d), "
                      "so we will ignore the entire Colour Specification box. \n", jp2->meth);
    }
    return true;
}

bool opj_jp2_decode(opj_jp2_t *jp2,
					opj_plugin_tile_t* tile,
                    opj_stream_private_t *p_stream,
                    opj_image_t* p_image,
                    opj_event_mgr_t * p_manager)
{
    if (!p_image)
        return false;

    /* J2K decoding */
    if( ! opj_j2k_decode(jp2->j2k, tile, p_stream, p_image, p_manager) ) {
        opj_event_msg(p_manager, EVT_ERROR, "Failed to decode the codestream in the JP2 file\n");
        return false;
    }

    if (!jp2->ignore_pclr_cmap_cdef) {
        if (!opj_jp2_check_color(p_image, &(jp2->color), p_manager)) {
            return false;
        }

        /* Set Image Color Space */
        if (jp2->enumcs == 16)
            p_image->color_space = OPJ_CLRSPC_SRGB;
        else if (jp2->enumcs == 17)
            p_image->color_space = OPJ_CLRSPC_GRAY;
        else if (jp2->enumcs == 18)
            p_image->color_space = OPJ_CLRSPC_SYCC;
        else if (jp2->enumcs == 24)
            p_image->color_space = OPJ_CLRSPC_EYCC;
        else if (jp2->enumcs == 12)
            p_image->color_space = OPJ_CLRSPC_CMYK;
        else
            p_image->color_space = OPJ_CLRSPC_UNKNOWN;

        if(jp2->color.jp2_pclr) {
            /* Part 1, I.5.3.4: Either both or none : */
            if( !jp2->color.jp2_pclr->cmap)
                opj_jp2_free_pclr(&(jp2->color));
            else
                opj_jp2_apply_pclr(p_image, &(jp2->color));
        }

        /* Apply channel definitions if needed */
        if(jp2->color.jp2_cdef) {
            opj_jp2_apply_cdef(p_image, &(jp2->color), p_manager);
        }

        if(jp2->color.icc_profile_buf) {
            p_image->icc_profile_buf = jp2->color.icc_profile_buf;
            p_image->icc_profile_len = jp2->color.icc_profile_len;
            jp2->color.icc_profile_buf = NULL;
        }
    }

    return true;
}

static bool opj_jp2_write_jp2h(opj_jp2_t *jp2,
                               opj_stream_private_t *stream,
                               opj_event_mgr_t * p_manager
                              )
{
    opj_jp2_img_header_writer_handler_t l_writers [4];
    opj_jp2_img_header_writer_handler_t * l_current_writer;

    int32_t i, l_nb_pass=0;
    /* size of data for super box*/
    uint32_t l_jp2h_size = 8;
    bool l_result = true;

    /* to store the data of the super box */
    uint8_t l_jp2h_data [8];

    /* preconditions */
    assert(stream != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    memset(l_writers,0,sizeof(l_writers));

    if (jp2->bpc == 255) {
        l_writers[l_nb_pass++].handler = opj_jp2_write_ihdr;
        l_writers[l_nb_pass++].handler = opj_jp2_write_bpcc;
        l_writers[l_nb_pass++].handler = opj_jp2_write_colr;
    } else {
        l_writers[l_nb_pass++].handler = opj_jp2_write_ihdr;
        l_writers[l_nb_pass++].handler = opj_jp2_write_colr;
    }

    if (jp2->color.jp2_cdef != NULL) {
        l_writers[l_nb_pass++].handler = opj_jp2_write_cdef;
    }

	if (jp2->write_display_resolution || jp2->write_capture_resolution) {
		bool storeCapture = jp2->capture_resolution[0] > 0 &&
			jp2->capture_resolution[1] > 0;

		bool storeDisplay = jp2->display_resolution[0] > 0 &&
			jp2->display_resolution[1] > 0;

		if (storeCapture || storeDisplay)
			l_writers[l_nb_pass++].handler = opj_jp2_write_res;
	}
	if (jp2->xml && jp2->xmlSize) {
		l_writers[l_nb_pass++].handler = opj_jp2_write_xml;
	}


    /* write box header */
    /* write JP2H type */
    opj_write_bytes(l_jp2h_data+4,JP2_JP2H,4);

    l_current_writer = l_writers;
    for (i=0; i<l_nb_pass; ++i) {
        l_current_writer->m_data = l_current_writer->handler(jp2,&(l_current_writer->m_size));
        if (l_current_writer->m_data == nullptr) {
            opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to hold JP2 Header data\n");
            l_result = false;
            break;
        }

        l_jp2h_size += l_current_writer->m_size;
        ++l_current_writer;
    }

    if (! l_result) {
        l_current_writer = l_writers;
        for (i=0; i<l_nb_pass; ++i) {
            if (l_current_writer->m_data != nullptr) {
                opj_free(l_current_writer->m_data );
            }
            ++l_current_writer;
        }

        return false;
    }

    /* write super box size */
    opj_write_bytes(l_jp2h_data,l_jp2h_size,4);

    /* write super box data on stream */
    if (opj_stream_write_data(stream,l_jp2h_data,8,p_manager) != 8) {
        opj_event_msg(p_manager, EVT_ERROR, "Stream error while writing JP2 Header box\n");
        l_result = false;
    }

    if (l_result) {
        l_current_writer = l_writers;
        for (i=0; i<l_nb_pass; ++i) {
            if (opj_stream_write_data(stream,l_current_writer->m_data,l_current_writer->m_size,p_manager) != l_current_writer->m_size) {
                opj_event_msg(p_manager, EVT_ERROR, "Stream error while writing JP2 Header box\n");
                l_result = false;
                break;
            }
            ++l_current_writer;
        }
    }

    l_current_writer = l_writers;

    /* cleanup */
    for (i=0; i<l_nb_pass; ++i) {
        if (l_current_writer->m_data != nullptr) {
            opj_free(l_current_writer->m_data );
        }
        ++l_current_writer;
    }

    return l_result;
}

static bool opj_jp2_write_ftyp(opj_jp2_t *jp2,
                               opj_stream_private_t *cio,
                               opj_event_mgr_t * p_manager )
{
    uint32_t i;
    uint32_t l_ftyp_size = 16 + 4 * jp2->numcl;
    uint8_t * l_ftyp_data, * l_current_data_ptr;
    bool l_result;

    /* preconditions */
    assert(cio != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    l_ftyp_data = (uint8_t *) opj_calloc(1,l_ftyp_size);

    if (l_ftyp_data == nullptr) {
        opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to handle ftyp data\n");
        return false;
    }

    l_current_data_ptr = l_ftyp_data;

    opj_write_bytes(l_current_data_ptr, l_ftyp_size,4); /* box size */
    l_current_data_ptr += 4;

    opj_write_bytes(l_current_data_ptr, JP2_FTYP,4); /* FTYP */
    l_current_data_ptr += 4;

    opj_write_bytes(l_current_data_ptr, jp2->brand,4); /* BR */
    l_current_data_ptr += 4;

    opj_write_bytes(l_current_data_ptr, jp2->minversion,4); /* MinV */
    l_current_data_ptr += 4;

    for (i = 0; i < jp2->numcl; i++)  {
        opj_write_bytes(l_current_data_ptr, jp2->cl[i],4);	/* CL */
    }

    l_result = (opj_stream_write_data(cio,l_ftyp_data,l_ftyp_size,p_manager) == l_ftyp_size);
    if (! l_result) {
        opj_event_msg(p_manager, EVT_ERROR, "Error while writing ftyp data to stream\n");
    }

    opj_free(l_ftyp_data);

    return l_result;
}

static bool opj_jp2_write_jp2c(opj_jp2_t *jp2,
                               opj_stream_private_t *cio,
                               opj_event_mgr_t * p_manager )
{
    int64_t j2k_codestream_exit;
    uint8_t l_data_header [8];

    /* preconditions */
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);
    assert(opj_stream_has_seek(cio));

    j2k_codestream_exit = opj_stream_tell(cio);
    opj_write_bytes(l_data_header,
                    (uint32_t) (j2k_codestream_exit - jp2->j2k_codestream_offset),
                    4); /* size of codestream */
    opj_write_bytes(l_data_header + 4,JP2_JP2C,4);									   /* JP2C */

    if (! opj_stream_seek(cio,jp2->j2k_codestream_offset,p_manager)) {
        opj_event_msg(p_manager, EVT_ERROR, "Failed to seek in the stream.\n");
        return false;
    }

    if (opj_stream_write_data(cio,l_data_header,8,p_manager) != 8) {
        opj_event_msg(p_manager, EVT_ERROR, "Failed to seek in the stream.\n");
        return false;
    }

    if (! opj_stream_seek(cio,j2k_codestream_exit,p_manager)) {
        opj_event_msg(p_manager, EVT_ERROR, "Failed to seek in the stream.\n");
        return false;
    }

    return true;
}

static bool opj_jp2_write_jp(	opj_jp2_t *jp2,
                                opj_stream_private_t *cio,
                                opj_event_mgr_t * p_manager )
{
    /* 12 bytes will be read */
    uint8_t l_signature_data [12];

    /* preconditions */
    assert(cio != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    /* write box length */
    opj_write_bytes(l_signature_data,12,4);
    /* writes box type */
    opj_write_bytes(l_signature_data+4,JP2_JP,4);
    /* writes magic number*/
    opj_write_bytes(l_signature_data+8,0x0d0a870a,4);

    if (opj_stream_write_data(cio,l_signature_data,12,p_manager) != 12) {
        return false;
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/* JP2 decoder interface                                             */
/* ----------------------------------------------------------------------- */

void opj_jp2_setup_decoder(void *jp2_void, opj_dparameters_t *parameters)
{
	opj_jp2_t *jp2 = (opj_jp2_t*)jp2_void;
    /* setup the J2K codec */
    opj_j2k_setup_decoder(jp2->j2k, parameters);

    /* further JP2 initializations go here */
    jp2->color.jp2_has_colr = 0;
    jp2->ignore_pclr_cmap_cdef = parameters->flags & OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG;
}

/* ----------------------------------------------------------------------- */
/* JP2 encoder interface                                             */
/* ----------------------------------------------------------------------- */

bool opj_jp2_setup_encoder(	opj_jp2_t *jp2,
                            opj_cparameters_t *parameters,
                            opj_image_t *image,
                            opj_event_mgr_t * p_manager)
{
    uint32_t i;
    uint32_t depth_0;
    uint32_t sign;
    uint32_t alpha_count;
    uint32_t color_channels = 0U;
    uint32_t alpha_channel = 0U;


    if(!jp2 || !parameters || !image)
        return false;

    /* setup the J2K codec */
    /* ------------------- */

    /* Check if number of components respects standard */
    if (image->numcomps < 1 || image->numcomps > OPJ_MAX_NUM_COMPONENTS) {
        opj_event_msg(p_manager, EVT_ERROR, "Invalid number of components specified while setting up JP2 encoder\n");
        return false;
    }

    if (opj_j2k_setup_encoder(jp2->j2k, parameters, image, p_manager ) == false) {
        return false;
    }

    /* setup the JP2 codec */
    /* ------------------- */

    /* Profile box */

    jp2->brand = JP2_JP2;	/* BR */
    jp2->minversion = 0;	/* MinV */
    jp2->numcl = 1;
    jp2->cl = (uint32_t*) opj_malloc(jp2->numcl * sizeof(uint32_t));
    if (!jp2->cl) {
        opj_event_msg(p_manager, EVT_ERROR, "Not enough memory when setup the JP2 encoder\n");
        return false;
    }
    jp2->cl[0] = JP2_JP2;	/* CL0 : JP2 */

    /* Image Header box */

    jp2->numcomps = image->numcomps;	/* NC */
    jp2->comps = (opj_jp2_comps_t*) opj_malloc(jp2->numcomps * sizeof(opj_jp2_comps_t));
    if (!jp2->comps) {
        opj_event_msg(p_manager, EVT_ERROR, "Not enough memory when setup the JP2 encoder\n");
        /* Memory of jp2->cl will be freed by opj_jp2_destroy */
        return false;
    }

    jp2->h = image->y1 - image->y0;		/* HEIGHT */
    jp2->w = image->x1 - image->x0;		/* WIDTH */
    /* BPC */
    depth_0 = image->comps[0].prec - 1;
    sign = image->comps[0].sgnd;
    jp2->bpc = depth_0 + (sign << 7);
    for (i = 1; i < image->numcomps; i++) {
        uint32_t depth = image->comps[i].prec - 1;
        sign = image->comps[i].sgnd;
        if (depth_0 != depth)
            jp2->bpc = 255;
    }
    jp2->C = 7;			/* C : Always 7 */
    jp2->UnkC = 0;		/* UnkC, colorspace specified in colr box */
    jp2->IPR = 0;		/* IPR, no intellectual property */

    /* BitsPerComponent box */
    for (i = 0; i < image->numcomps; i++) {
        jp2->comps[i].bpcc = image->comps[i].prec - 1 + (image->comps[i].sgnd << 7);
    }

    /* Colour Specification box */
    if(image->icc_profile_len) {
        jp2->meth = 2;
        jp2->enumcs = 0;
		if (image->icc_profile_buf) {
			// clean up existing icc profile in jp2 struct
			if (jp2->color.icc_profile_buf) {
				opj_free(jp2->color.icc_profile_buf);
				jp2->color.icc_profile_buf = NULL;
			}
			// copy icc profile from image to jp2 struct
			jp2->color.icc_profile_len = image->icc_profile_len;
			jp2->color.icc_profile_buf = (uint8_t*)opj_malloc(jp2->color.icc_profile_len);
			if (!jp2->color.icc_profile_buf)
				return false;
			memcpy(jp2->color.icc_profile_buf, image->icc_profile_buf, jp2->color.icc_profile_len);
		}
    } else {
        jp2->meth = 1;
        if (image->color_space == 1)
            jp2->enumcs = 16;	/* sRGB as defined by IEC 61966-2-1 */
        else if (image->color_space == 2)
            jp2->enumcs = 17;	/* greyscale */
        else if (image->color_space == 3)
            jp2->enumcs = 18;	/* YUV */
    }

    /* Channel Definition box */
    /* FIXME not provided by parameters */
    /* We try to do what we can... */
    alpha_count = 0U;
    for (i = 0; i < image->numcomps; i++) {
        if (image->comps[i].alpha != 0) {
            alpha_count++;
            alpha_channel = i;
        }
    }
    if (alpha_count == 1U) { /* no way to deal with more than 1 alpha channel */
        switch (jp2->enumcs) {
        case 16:
        case 18:
            color_channels = 3;
            break;
        case 17:
            color_channels = 1;
            break;
        default:
            alpha_count = 0U;
            break;
        }
        if (alpha_count == 0U) {
            opj_event_msg(p_manager, EVT_WARNING, "Alpha channel specified but unknown enumcs. No cdef box will be created.\n");
        } else if (image->numcomps < (color_channels+1)) {
            opj_event_msg(p_manager, EVT_WARNING, "Alpha channel specified but not enough image components for an automatic cdef box creation.\n");
            alpha_count = 0U;
        } else if ((uint32_t)alpha_channel < color_channels) {
            opj_event_msg(p_manager, EVT_WARNING, "Alpha channel position conflicts with color channel. No cdef box will be created.\n");
            alpha_count = 0U;
        }
    } else if (alpha_count > 1) {
        opj_event_msg(p_manager, EVT_WARNING, "Multiple alpha channels specified. No cdef box will be created.\n");
    }
    if (alpha_count == 1U) { /* if here, we know what we can do */
        jp2->color.jp2_cdef = (opj_jp2_cdef_t*)opj_malloc(sizeof(opj_jp2_cdef_t));
        if(!jp2->color.jp2_cdef) {
            opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to setup the JP2 encoder\n");
            return false;
        }
        /* no memset needed, all values will be overwritten except if jp2->color.jp2_cdef->info allocation fails, */
        /* in which case jp2->color.jp2_cdef->info will be NULL => valid for destruction */
        jp2->color.jp2_cdef->info = (opj_jp2_cdef_info_t*) opj_malloc(image->numcomps * sizeof(opj_jp2_cdef_info_t));
        if (!jp2->color.jp2_cdef->info) {
            /* memory will be freed by opj_jp2_destroy */
            opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to setup the JP2 encoder\n");
            return false;
        }
        jp2->color.jp2_cdef->n = (uint16_t) image->numcomps; /* cast is valid : image->numcomps [1,16384] */
        for (i = 0U; i < color_channels; i++) {
            jp2->color.jp2_cdef->info[i].cn = (uint16_t)i; /* cast is valid : image->numcomps [1,16384] */
            jp2->color.jp2_cdef->info[i].typ = 0U;
            jp2->color.jp2_cdef->info[i].asoc = (uint16_t)(i+1U); /* No overflow + cast is valid : image->numcomps [1,16384] */
        }
        for (; i < image->numcomps; i++) {
            if (image->comps[i].alpha != 0) { /* we'll be here exactly once */
                jp2->color.jp2_cdef->info[i].cn = (uint16_t)i; /* cast is valid : image->numcomps [1,16384] */
                jp2->color.jp2_cdef->info[i].typ = 1U; /* Opacity channel */
                jp2->color.jp2_cdef->info[i].asoc = 0U; /* Apply alpha channel to the whole image */
            } else {
                /* Unknown channel */
                jp2->color.jp2_cdef->info[i].cn = (uint16_t)i; /* cast is valid : image->numcomps [1,16384] */
                jp2->color.jp2_cdef->info[i].typ = 65535U;
                jp2->color.jp2_cdef->info[i].asoc = 65535U;
            }
        }
    }

    jp2->precedence = 0;	/* PRECEDENCE */
    jp2->approx = 0;		/* APPROX */

	if (parameters->write_capture_resolution) {
		jp2->write_capture_resolution = true;
		for (int i = 0; i < 2; ++i) {
			jp2->capture_resolution[i] = parameters->capture_resolution[i];
		}
	}

	if (parameters->write_display_resolution) {
		jp2->write_display_resolution = true;
		for (int i = 0; i < 2; ++i) {
			jp2->display_resolution[i] = parameters->display_resolution[i];
		}
	}
    return true;
}

bool opj_jp2_encode(opj_jp2_t *jp2,
					opj_plugin_tile_t* tile,
                    opj_stream_private_t *stream,
                    opj_event_mgr_t * p_manager)
{
    return opj_j2k_encode(jp2->j2k, tile,stream, p_manager);
}

bool opj_jp2_end_decompress(opj_jp2_t *jp2,
                            opj_stream_private_t *cio,
                            opj_event_mgr_t * p_manager
                           )
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);

    /* customization of the end encoding */
    if (! opj_jp2_setup_end_header_reading(jp2, p_manager)) {
        return false;
    }

    /* write header */
    if (! opj_jp2_exec (jp2,jp2->m_procedure_list,cio,p_manager)) {
        return false;
    }

    return opj_j2k_end_decompress(jp2->j2k, cio, p_manager);
}

bool opj_jp2_end_compress(	opj_jp2_t *jp2,
                            opj_stream_private_t *cio,
                            opj_event_mgr_t * p_manager
                         )
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);

    /* customization of the end encoding */
    if (! opj_jp2_setup_end_header_writing(jp2, p_manager)) {
        return false;
    }

    if (! opj_j2k_end_compress(jp2->j2k,cio,p_manager)) {
        return false;
    }

    /* write header */
    return opj_jp2_exec(jp2,jp2->m_procedure_list,cio,p_manager);
}

static bool opj_jp2_setup_end_header_writing (opj_jp2_t *jp2, opj_event_mgr_t * p_manager)
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_write_jp2c, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom procedures */
    return true;
}

static bool opj_jp2_setup_end_header_reading (opj_jp2_t *jp2, opj_event_mgr_t * p_manager)
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_read_header_procedure, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom procedures */

    return true;
}

static bool opj_jp2_default_validation (	opj_jp2_t * jp2,
        opj_stream_private_t *cio,
        opj_event_mgr_t * p_manager
                                       )
{
    bool l_is_valid = true;
    uint32_t i;

    /* preconditions */
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);

    /* JPEG2000 codec validation */

    /* STATE checking */
    /* make sure the state is at 0 */
    l_is_valid &= (jp2->jp2_state == JP2_STATE_NONE);

    /* make sure not reading a jp2h ???? WEIRD */
    l_is_valid &= (jp2->jp2_img_state == JP2_IMG_STATE_NONE);

    /* POINTER validation */
    /* make sure a j2k codec is present */
    l_is_valid &= (jp2->j2k != nullptr);

    /* make sure a procedure list is present */
    l_is_valid &= (jp2->m_procedure_list != nullptr);

    /* make sure a validation list is present */
    l_is_valid &= (jp2->m_validation_list != nullptr);

    /* PARAMETER VALIDATION */
    /* number of components */
    l_is_valid &= (jp2->numcl > 0);
    /* width */
    l_is_valid &= (jp2->h > 0);
    /* height */
    l_is_valid &= (jp2->w > 0);
    /* precision */
    for (i = 0; i < jp2->numcomps; ++i)	{
        l_is_valid &= ((jp2->comps[i].bpcc & 0x7FU) < 38U); /* 0 is valid, ignore sign for check */
    }

    /* METH */
    l_is_valid &= ((jp2->meth > 0) && (jp2->meth < 3));

    /* stream validation */
    /* back and forth is needed */
    l_is_valid &= opj_stream_has_seek(cio);

    return l_is_valid;
}

static bool opj_jp2_read_header_procedure(  opj_jp2_t *jp2,
        opj_stream_private_t *stream,
        opj_event_mgr_t * p_manager
                                         )
{
    opj_jp2_box_t box;
    uint32_t l_nb_bytes_read;
    const opj_jp2_header_handler_t * l_current_handler;
    const opj_jp2_header_handler_t * l_current_handler_misplaced;
    uint32_t l_last_data_size = OPJ_BOX_SIZE;
    uint32_t l_current_data_size;
    uint8_t * l_current_data = nullptr;

    /* preconditions */
    assert(stream != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    l_current_data = (uint8_t*)opj_calloc(1,l_last_data_size);

    if (l_current_data == nullptr) {
        opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to handle jpeg2000 file header\n");
        return false;
    }

    while (opj_jp2_read_boxhdr(&box,&l_nb_bytes_read,stream,p_manager)) {
        /* is it the codestream box ? */
        if (box.type == JP2_JP2C) {
            if (jp2->jp2_state & JP2_STATE_HEADER) {
                jp2->jp2_state |= JP2_STATE_CODESTREAM;
                opj_free(l_current_data);
                return true;
            } else {
                opj_event_msg(p_manager, EVT_ERROR, "bad placed jpeg codestream\n");
                opj_free(l_current_data);
                return false;
            }
        } else if	(box.length == 0) {
            opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box of undefined sizes\n");
            opj_free(l_current_data);
            return false;
        }
        /* testcase 1851.pdf.SIGSEGV.ce9.948 */
        else if (box.length < l_nb_bytes_read) {
            opj_event_msg(p_manager, EVT_ERROR, "invalid box size %d (%x)\n", box.length, box.type);
            opj_free(l_current_data);
            return false;
        }

        l_current_handler = opj_jp2_find_handler(box.type);
        l_current_handler_misplaced = opj_jp2_img_find_handler(box.type);
        l_current_data_size = box.length - l_nb_bytes_read;

        if ((l_current_handler != nullptr) || (l_current_handler_misplaced != nullptr)) {
            if (l_current_handler == nullptr) {
                opj_event_msg(p_manager, EVT_WARNING, "Found a misplaced '%c%c%c%c' box outside jp2h box\n", (uint8_t)(box.type>>24), (uint8_t)(box.type>>16), (uint8_t)(box.type>>8), (uint8_t)(box.type>>0));
                if (jp2->jp2_state & JP2_STATE_HEADER) {
                    /* read anyway, we already have jp2h */
                    l_current_handler = l_current_handler_misplaced;
                } else {
                    opj_event_msg(p_manager, EVT_WARNING, "JPEG2000 Header box not read yet, '%c%c%c%c' box will be ignored\n", (uint8_t)(box.type>>24), (uint8_t)(box.type>>16), (uint8_t)(box.type>>8), (uint8_t)(box.type>>0));
                    jp2->jp2_state |= JP2_STATE_UNKNOWN;
                    if (!opj_stream_skip(stream,l_current_data_size,p_manager)) {
                        opj_event_msg(p_manager, EVT_ERROR, "Problem with skipping JPEG2000 box, stream error\n");
                        opj_free(l_current_data);
                        return false;
                    }
                    continue;
                }
            }
            if ((int64_t)l_current_data_size > opj_stream_get_number_byte_left(stream)) {
                /* do not even try to malloc if we can't read */
                opj_event_msg(p_manager, EVT_ERROR, "Invalid box size %d for box '%c%c%c%c'. Need %d bytes, %d bytes remaining \n", box.length, (uint8_t)(box.type>>24), (uint8_t)(box.type>>16), (uint8_t)(box.type>>8), (uint8_t)(box.type>>0), l_current_data_size, (uint32_t)opj_stream_get_number_byte_left(stream));
                opj_free(l_current_data);
                return false;
            }
            if (l_current_data_size > l_last_data_size) {
                uint8_t* new_current_data = (uint8_t*)opj_realloc(l_current_data,l_current_data_size);
                if (!new_current_data) {
                    opj_free(l_current_data);
                    opj_event_msg(p_manager, EVT_ERROR, "Not enough memory to handle jpeg2000 box\n");
                    return false;
                }
                l_current_data = new_current_data;
                l_last_data_size = l_current_data_size;
            }

            l_nb_bytes_read = (uint32_t)opj_stream_read_data(stream,l_current_data,l_current_data_size,p_manager);
            if (l_nb_bytes_read != l_current_data_size) {
                opj_event_msg(p_manager, EVT_ERROR, "Problem with reading JPEG2000 box, stream error\n");
                opj_free(l_current_data);
                return false;
            }

            if (! l_current_handler->handler(jp2,l_current_data,l_current_data_size,p_manager)) {
                opj_free(l_current_data);
                return false;
            }
        } else {
            if (!(jp2->jp2_state & JP2_STATE_SIGNATURE)) {
                opj_event_msg(p_manager, EVT_ERROR, "Malformed JP2 file format: first box must be JPEG 2000 signature box\n");
                opj_free(l_current_data);
                return false;
            }
            if (!(jp2->jp2_state & JP2_STATE_FILE_TYPE)) {
                opj_event_msg(p_manager, EVT_ERROR, "Malformed JP2 file format: second box must be file type box\n");
                opj_free(l_current_data);
                return false;
            }
            jp2->jp2_state |= JP2_STATE_UNKNOWN;
            if (!opj_stream_skip(stream,l_current_data_size,p_manager)) {
                opj_event_msg(p_manager, EVT_ERROR, "Problem with skipping JPEG2000 box, stream error\n");
                opj_free(l_current_data);
                return false;
            }
        }
    }

    opj_free(l_current_data);

    return true;
}

/**
 * Excutes the given procedures on the given codec.
 *
 * @param	p_procedure_list	the list of procedures to execute
 * @param	jp2					the jpeg2000 file codec to execute the procedures on.
 * @param	stream					the stream to execute the procedures on.
 * @param	p_manager			the user manager.
 *
 * @return	true				if all the procedures were successfully executed.
 */
static bool opj_jp2_exec (  opj_jp2_t * jp2,
                            opj_procedure_list_t * p_procedure_list,
                            opj_stream_private_t *stream,
                            opj_event_mgr_t * p_manager
                         )

{
    bool (** l_procedure) (opj_jp2_t * jp2, opj_stream_private_t *, opj_event_mgr_t *) = nullptr;
    bool l_result = true;
    uint32_t l_nb_proc, i;

    /* preconditions */
    assert(p_procedure_list != nullptr);
    assert(jp2 != nullptr);
    assert(stream != nullptr);
    assert(p_manager != nullptr);

    l_nb_proc = opj_procedure_list_get_nb_procedures(p_procedure_list);
    l_procedure = (bool (**) (opj_jp2_t * jp2, opj_stream_private_t *, opj_event_mgr_t *)) opj_procedure_list_get_first_procedure(p_procedure_list);

    for	(i=0; i<l_nb_proc; ++i) {
        l_result = l_result && (*l_procedure) (jp2,stream,p_manager);
        ++l_procedure;
    }

    /* and clear the procedure list at the end. */
    opj_procedure_list_clear(p_procedure_list);
    return l_result;
}

bool opj_jp2_start_compress(opj_jp2_t *jp2,
                            opj_stream_private_t *stream,
                            opj_image_t * p_image,
                            opj_event_mgr_t * p_manager
                           )
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(stream != nullptr);
    assert(p_manager != nullptr);

    /* customization of the validation */
    if (! opj_jp2_setup_encoding_validation (jp2, p_manager)) {
        return false;
    }

    /* validation of the parameters codec */
    if (! opj_jp2_exec(jp2,jp2->m_validation_list,stream,p_manager)) {
        return false;
    }

    /* customization of the encoding */
    if (! opj_jp2_setup_header_writing(jp2, p_manager)) {
        return false;
    }

    /* write header */
    if (! opj_jp2_exec (jp2,jp2->m_procedure_list,stream,p_manager)) {
        return false;
    }

    return opj_j2k_start_compress(jp2->j2k,stream,p_image,p_manager);
}

static const opj_jp2_header_handler_t * opj_jp2_find_handler (uint32_t p_id)
{
    uint32_t i, l_handler_size = sizeof(jp2_header) / sizeof(opj_jp2_header_handler_t);

    for (i=0; i<l_handler_size; ++i) {
        if (jp2_header[i].id == p_id) {
            return &jp2_header[i];
        }
    }
    return NULL;
}

/**
 * Finds the image execution function related to the given box id.
 *
 * @param	p_id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const opj_jp2_header_handler_t * opj_jp2_img_find_handler (uint32_t p_id)
{
    uint32_t i, l_handler_size = sizeof(jp2_img_header) / sizeof(opj_jp2_header_handler_t);
    for (i=0; i<l_handler_size; ++i) {
        if (jp2_img_header[i].id == p_id) {
            return &jp2_img_header[i];
        }
    }

    return NULL;
}

/**
 * Reads a jpeg2000 file signature box.
 *
 * @param	p_header_data	the data contained in the signature box.
 * @param	jp2				the jpeg2000 file codec.
 * @param	p_header_size	the size of the data contained in the signature box.
 * @param	p_manager		the user event manager.
 *
 * @return true if the file signature box is valid.
 */
static bool opj_jp2_read_jp(opj_jp2_t *jp2,
                            uint8_t * p_header_data,
                            uint32_t p_header_size,
                            opj_event_mgr_t * p_manager
                           )

{
    uint32_t l_magic_number;

    /* preconditions */
    assert(p_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (jp2->jp2_state != JP2_STATE_NONE) {
        opj_event_msg(p_manager, EVT_ERROR, "The signature box must be the first box in the file.\n");
        return false;
    }

    /* assure length of data is correct (4 -> magic number) */
    if (p_header_size != 4) {
        opj_event_msg(p_manager, EVT_ERROR, "Error with JP signature Box size\n");
        return false;
    }

    /* rearrange data */
    opj_read_bytes(p_header_data,&l_magic_number,4);
    if (l_magic_number != 0x0d0a870a ) {
        opj_event_msg(p_manager, EVT_ERROR, "Error with JP Signature : bad magic number\n");
        return false;
    }

    jp2->jp2_state |= JP2_STATE_SIGNATURE;

    return true;
}

/**
 * Reads a a FTYP box - File type box
 *
 * @param	p_header_data	the data contained in the FTYP box.
 * @param	jp2				the jpeg2000 file codec.
 * @param	p_header_size	the size of the data contained in the FTYP box.
 * @param	p_manager		the user event manager.
 *
 * @return true if the FTYP box is valid.
 */
static bool opj_jp2_read_ftyp(	opj_jp2_t *jp2,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                opj_event_mgr_t * p_manager
                             )
{
    uint32_t i, l_remaining_bytes;

    /* preconditions */
    assert(p_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (jp2->jp2_state != JP2_STATE_SIGNATURE) {
        opj_event_msg(p_manager, EVT_ERROR, "The ftyp box must be the second box in the file.\n");
        return false;
    }

    /* assure length of data is correct */
    if (p_header_size < 8) {
        opj_event_msg(p_manager, EVT_ERROR, "Error with FTYP signature Box size\n");
        return false;
    }

    opj_read_bytes(p_header_data,&jp2->brand,4);		/* BR */
    p_header_data += 4;

    opj_read_bytes(p_header_data,&jp2->minversion,4);		/* MinV */
    p_header_data += 4;

    l_remaining_bytes = p_header_size - 8;

    /* the number of remaining bytes should be a multiple of 4 */
    if ((l_remaining_bytes & 0x3) != 0) {
        opj_event_msg(p_manager, EVT_ERROR, "Error with FTYP signature Box size\n");
        return false;
    }

    /* div by 4 */
    jp2->numcl = l_remaining_bytes >> 2;
    if (jp2->numcl) {
        jp2->cl = (uint32_t *) opj_calloc(jp2->numcl, sizeof(uint32_t));
        if (jp2->cl == nullptr) {
            opj_event_msg(p_manager, EVT_ERROR, "Not enough memory with FTYP Box\n");
            return false;
        }
    }

    for (i = 0; i < jp2->numcl; ++i) {
        opj_read_bytes(p_header_data,&jp2->cl[i],4);		/* CLi */
        p_header_data += 4;
    }

    jp2->jp2_state |= JP2_STATE_FILE_TYPE;

    return true;
}

static bool opj_jp2_skip_jp2c(	opj_jp2_t *jp2,
                                opj_stream_private_t *stream,
                                opj_event_mgr_t * p_manager )
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(stream != nullptr);
    assert(p_manager != nullptr);

    jp2->j2k_codestream_offset = opj_stream_tell(stream);

    if (!opj_stream_skip(stream,8,p_manager)) {
        return false;
    }

    return true;
}

/**
 * Reads the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param	p_header_data	the data contained in the file header box.
 * @param	jp2				the jpeg2000 file codec.
 * @param	p_header_size	the size of the data contained in the file header box.
 * @param	p_manager		the user event manager.
 *
 * @return true if the JP2 Header box was successfully recognized.
*/
static bool opj_jp2_read_jp2h(  opj_jp2_t *jp2,
                                uint8_t *p_header_data,
                                uint32_t p_header_size,
                                opj_event_mgr_t * p_manager
                             )
{
    uint32_t l_box_size=0, l_current_data_size = 0;
    opj_jp2_box_t box;
    const opj_jp2_header_handler_t * l_current_handler;
    bool l_has_ihdr = 0;

    /* preconditions */
    assert(p_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    /* make sure the box is well placed */
    if ((jp2->jp2_state & JP2_STATE_FILE_TYPE) != JP2_STATE_FILE_TYPE ) {
        opj_event_msg(p_manager, EVT_ERROR, "The  box must be the first box in the file.\n");
        return false;
    }

    jp2->jp2_img_state = JP2_IMG_STATE_NONE;

	int64_t header_size = p_header_size;
    /* iterate while remaining data */
    while (header_size > 0) {

        if (! opj_jp2_read_boxhdr_char(&box,p_header_data,&l_box_size,header_size, p_manager)) {
            opj_event_msg(p_manager, EVT_ERROR, "Stream error while reading JP2 Header box\n");
            return false;
        }

        if (box.length > header_size) {
            opj_event_msg(p_manager, EVT_ERROR, "Stream error while reading JP2 Header box: box length is inconsistent.\n");
            return false;
        }

        l_current_handler = opj_jp2_img_find_handler(box.type);
        l_current_data_size = box.length - l_box_size;
        p_header_data += l_box_size;

        if (l_current_handler != nullptr) {
            if (! l_current_handler->handler(jp2,p_header_data,l_current_data_size,p_manager)) {
                return false;
            }
        } else {
            jp2->jp2_img_state |= JP2_IMG_STATE_UNKNOWN;
        }

        if (box.type == JP2_IHDR) {
            l_has_ihdr = 1;
        }

        p_header_data += l_current_data_size;
        header_size -= box.length;
		if (header_size < 0) {
			opj_event_msg(p_manager, EVT_ERROR, "Error reading JP2 header box\n");
			return false;
		}
    }

    if (l_has_ihdr == 0) {
        opj_event_msg(p_manager, EVT_ERROR, "Stream error while reading JP2 Header box: no 'ihdr' box.\n");
        return false;
    }

    jp2->jp2_state |= JP2_STATE_HEADER;

    return true;
}

static bool opj_jp2_read_boxhdr_char(   opj_jp2_box_t *box,
                                        uint8_t * p_data,
                                        uint32_t * p_number_bytes_read,
                                        int64_t p_box_max_size,
                                        opj_event_mgr_t * p_manager
                                    )
{
    uint32_t l_value;

    /* preconditions */
    assert(p_data != nullptr);
    assert(box != nullptr);
    assert(p_number_bytes_read != nullptr);
    assert(p_manager != nullptr);

    if (p_box_max_size < 8) {
        opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box of less than 8 bytes\n");
        return false;
    }

    /* process read data */
    opj_read_bytes(p_data, &l_value, 4);
    p_data += 4;
    box->length = l_value;

    opj_read_bytes(p_data, &l_value, 4);
    p_data += 4;
    box->type = l_value;

    *p_number_bytes_read = 8;

    /* do we have a "special very large box ?" */
    /* read then the XLBox */
    if (box->length == 1) {
        uint32_t l_xl_part_size;

        if (p_box_max_size < 16) {
            opj_event_msg(p_manager, EVT_ERROR, "Cannot handle XL box of less than 16 bytes\n");
            return false;
        }

        opj_read_bytes(p_data,&l_xl_part_size, 4);
        p_data += 4;
        *p_number_bytes_read += 4;

        if (l_xl_part_size != 0) {
            opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box sizes higher than 2^32\n");
            return false;
        }

        opj_read_bytes(p_data, &l_value, 4);
        *p_number_bytes_read += 4;
        box->length = (uint32_t)(l_value);

        if (box->length == 0) {
            opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box of undefined sizes\n");
            return false;
        }
    } else if (box->length == 0) {
        opj_event_msg(p_manager, EVT_ERROR, "Cannot handle box of undefined sizes\n");
        return false;
    }
    if (box->length < *p_number_bytes_read) {
        opj_event_msg(p_manager, EVT_ERROR, "Box length is inconsistent.\n");
        return false;
    }
    return true;
}

bool opj_jp2_read_header(	opj_stream_private_t *p_stream,
                            opj_jp2_t *jp2,
							opj_header_info_t* header_info,
                            opj_image_t ** p_image,
                            opj_event_mgr_t * p_manager
                        )
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    /* customization of the validation */
    if (! opj_jp2_setup_decoding_validation (jp2, p_manager)) {
        return false;
    }

    /* customization of the encoding */
    if (! opj_jp2_setup_header_reading(jp2, p_manager)) {
        return false;
    }

    /* validation of the parameters codec */
    if (! opj_jp2_exec(jp2,jp2->m_validation_list,p_stream,p_manager)) {
        return false;
    }

    /* read header */
    if (! opj_jp2_exec (jp2,jp2->m_procedure_list,p_stream,p_manager)) {
        return false;
    }

	if (header_info) {
		header_info->enumcs = jp2->enumcs;
		header_info->color = jp2->color;
	}

    bool rc =  opj_j2k_read_header(	p_stream,
                                jp2->j2k,
								header_info,
                                p_image,
                                p_manager);

	if (*p_image) {
		for (int i = 0; i < 2; ++i) {
			(*p_image)->capture_resolution[i] = jp2->capture_resolution[i];
			(*p_image)->display_resolution[i] = jp2->display_resolution[i];
		}
	}
	return rc;
}

static bool opj_jp2_setup_encoding_validation (opj_jp2_t *jp2, opj_event_mgr_t * p_manager)
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! opj_procedure_list_add_procedure(jp2->m_validation_list, (opj_procedure)opj_jp2_default_validation, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom validation procedure */

    return true;
}

static bool opj_jp2_setup_decoding_validation (opj_jp2_t *jp2, opj_event_mgr_t * p_manager)
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    /* DEVELOPER CORNER, add your custom validation procedure */

    return true;
}

static bool opj_jp2_setup_header_writing (opj_jp2_t *jp2, opj_event_mgr_t * p_manager)
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_write_jp, p_manager)) {
        return false;
    }
    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_write_ftyp, p_manager)) {
        return false;
    }
    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_write_jp2h, p_manager)) {
        return false;
    }
    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_skip_jp2c,p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, insert your custom procedures */

    return true;
}

static bool opj_jp2_setup_header_reading (opj_jp2_t *jp2, opj_event_mgr_t * p_manager)
{
    /* preconditions */
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! opj_procedure_list_add_procedure(jp2->m_procedure_list,(opj_procedure)opj_jp2_read_header_procedure, p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, add your custom procedures */

    return true;
}

bool opj_jp2_read_tile_header ( opj_jp2_t * p_jp2,
                                uint32_t * p_tile_index,
                                uint64_t * p_data_size,
                                uint32_t * p_tile_x0,
                                uint32_t * p_tile_y0,
                                uint32_t * p_tile_x1,
                                uint32_t * p_tile_y1,
                                uint32_t * p_nb_comps,
                                bool * p_go_on,
                                opj_stream_private_t *p_stream,
                                opj_event_mgr_t * p_manager
                              )
{
    return opj_j2k_read_tile_header(p_jp2->j2k,
                                    p_tile_index,
                                    p_data_size,
                                    p_tile_x0, p_tile_y0,
                                    p_tile_x1, p_tile_y1,
                                    p_nb_comps,
                                    p_go_on,
                                    p_stream,
                                    p_manager);
}

bool opj_jp2_write_tile (	opj_jp2_t *p_jp2,
                            uint32_t p_tile_index,
                            uint8_t * p_data,
                            uint64_t p_data_size,
                            opj_stream_private_t *p_stream,
                            opj_event_mgr_t * p_manager
                        )

{
    return opj_j2k_write_tile (p_jp2->j2k,p_tile_index,p_data,p_data_size,p_stream,p_manager);
}

bool opj_jp2_decode_tile (  opj_jp2_t * p_jp2,
                            uint32_t p_tile_index,
                            uint8_t * p_data,
                            uint64_t p_data_size,
                            opj_stream_private_t *p_stream,
                            opj_event_mgr_t * p_manager
                         )
{
    return opj_j2k_decode_tile (p_jp2->j2k,p_tile_index,p_data,p_data_size,p_stream,p_manager);
}

void opj_jp2_destroy(opj_jp2_t *jp2)
{
    if (jp2) {
        /* destroy the J2K codec */
        opj_j2k_destroy(jp2->j2k);
        jp2->j2k = nullptr;

        if (jp2->comps) {
            opj_free(jp2->comps);
            jp2->comps = nullptr;
        }

        if (jp2->cl) {
            opj_free(jp2->cl);
            jp2->cl = nullptr;
        }

        if (jp2->color.icc_profile_buf) {
            opj_free(jp2->color.icc_profile_buf);
            jp2->color.icc_profile_buf = nullptr;
        }

        if (jp2->color.jp2_cdef) {
            if (jp2->color.jp2_cdef->info) {
                opj_free(jp2->color.jp2_cdef->info);
                jp2->color.jp2_cdef->info = NULL;
            }

            opj_free(jp2->color.jp2_cdef);
            jp2->color.jp2_cdef = nullptr;
        }

		opj_jp2_free_pclr(&jp2->color);

        if (jp2->m_validation_list) {
            opj_procedure_list_destroy(jp2->m_validation_list);
            jp2->m_validation_list = nullptr;
        }

        if (jp2->m_procedure_list) {
            opj_procedure_list_destroy(jp2->m_procedure_list);
            jp2->m_procedure_list = nullptr;
        }

		if (jp2->xml) {
			opj_free(jp2->xml);
			jp2->xml = nullptr;
		}

        opj_free(jp2);
    }
}

bool opj_jp2_set_decode_area(	opj_jp2_t *p_jp2,
                                opj_image_t* p_image,
                                uint32_t p_start_x, uint32_t p_start_y,
                                uint32_t p_end_x, uint32_t p_end_y,
                                opj_event_mgr_t * p_manager
                            )
{
    return opj_j2k_set_decode_area(p_jp2->j2k, p_image, p_start_x, p_start_y, p_end_x, p_end_y, p_manager);
}

bool opj_jp2_get_tile(	opj_jp2_t *p_jp2,
                        opj_stream_private_t *p_stream,
                        opj_image_t* p_image,
                        opj_event_mgr_t * p_manager,
                        uint32_t tile_index
                     )
{
    if (!p_image)
        return false;

    opj_event_msg(p_manager, EVT_WARNING, "JP2 box which are after the codestream will not be read by this function.\n");

    if (! opj_j2k_get_tile(p_jp2->j2k, p_stream, p_image, p_manager, tile_index) ) {
        opj_event_msg(p_manager, EVT_ERROR, "Failed to decode the codestream in the JP2 file\n");
        return false;
    }

    if (!opj_jp2_check_color(p_image, &(p_jp2->color), p_manager)) {
        return false;
    }

    /* Set Image Color Space */
    if (p_jp2->enumcs == 16)
        p_image->color_space = OPJ_CLRSPC_SRGB;
    else if (p_jp2->enumcs == 17)
        p_image->color_space = OPJ_CLRSPC_GRAY;
    else if (p_jp2->enumcs == 18)
        p_image->color_space = OPJ_CLRSPC_SYCC;
    else if (p_jp2->enumcs == 24)
        p_image->color_space = OPJ_CLRSPC_EYCC;
    else if (p_jp2->enumcs == 12)
        p_image->color_space = OPJ_CLRSPC_CMYK;
    else
        p_image->color_space = OPJ_CLRSPC_UNKNOWN;

    if(p_jp2->color.jp2_pclr) {
        /* Part 1, I.5.3.4: Either both or none : */
        if( !p_jp2->color.jp2_pclr->cmap)
            opj_jp2_free_pclr(&(p_jp2->color));
        else
            opj_jp2_apply_pclr(p_image, &(p_jp2->color));
    }

	/* Apply channel definitions if needed */
    if(p_jp2->color.jp2_cdef) {
        opj_jp2_apply_cdef(p_image, &(p_jp2->color), p_manager);
    }

    if(p_jp2->color.icc_profile_buf) {
        p_image->icc_profile_buf = p_jp2->color.icc_profile_buf;
        p_image->icc_profile_len = p_jp2->color.icc_profile_len;
        p_jp2->color.icc_profile_buf = NULL;
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/* JP2 encoder interface                                             */
/* ----------------------------------------------------------------------- */

opj_jp2_t* opj_jp2_create(bool p_is_decoder)
{
    opj_jp2_t *jp2 = (opj_jp2_t*)opj_calloc(1,sizeof(opj_jp2_t));
    if (jp2) {

        /* create the J2K codec */
        if (! p_is_decoder) {
            jp2->j2k = opj_j2k_create_compress();
        } else {
            jp2->j2k = opj_j2k_create_decompress();
        }

        if (jp2->j2k == nullptr) {
            opj_jp2_destroy(jp2);
            return nullptr;
        }

        /* Color structure */
        jp2->color.icc_profile_buf = NULL;
        jp2->color.icc_profile_len = 0;
        jp2->color.jp2_cdef = NULL;
        jp2->color.jp2_pclr = NULL;
        jp2->color.jp2_has_colr = 0;

        /* validation list creation */
        jp2->m_validation_list = opj_procedure_list_create();
        if (! jp2->m_validation_list) {
            opj_jp2_destroy(jp2);
            return nullptr;
        }

        /* execution list creation */
        jp2->m_procedure_list = opj_procedure_list_create();
        if (! jp2->m_procedure_list) {
            opj_jp2_destroy(jp2);
            return nullptr;
        }
    }

    return jp2;
}

void jp2_dump(opj_jp2_t* p_jp2, int32_t flag, FILE* out_stream)
{
    /* preconditions */
    assert(p_jp2 != nullptr);

    j2k_dump(p_jp2->j2k,
             flag,
             out_stream);
}

opj_codestream_index_t* jp2_get_cstr_index(opj_jp2_t* p_jp2)
{
    return j2k_get_cstr_index(p_jp2->j2k);
}

opj_codestream_info_v2_t* jp2_get_cstr_info(opj_jp2_t* p_jp2)
{
    return j2k_get_cstr_info(p_jp2->j2k);
}

bool opj_jp2_set_decoded_resolution_factor(opj_jp2_t *p_jp2,
        uint32_t res_factor,
        opj_event_mgr_t * p_manager)
{
    return opj_j2k_set_decoded_resolution_factor(p_jp2->j2k, res_factor, p_manager);
}
