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

#include "grok_includes.h"
namespace grk {


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
static bool jp2_read_ihdr(  jp2_t *jp2,
                                uint8_t *p_image_header_data,
                                uint32_t p_image_header_size,
                                event_mgr_t * p_manager );

/**
 * Writes the Image Header box - Image Header box.
 *
 * @param jp2					jpeg2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
*/
static uint8_t * jp2_write_ihdr(jp2_t *jp2,
                                    uint32_t * p_nb_bytes_written );


/**
* Read XML box
*
* @param	jp2					jpeg2000 file codec.
* @param	p_xml_data			pointer to actual data (already read from file)
* @param	p_xml_size			size of the xml data
* @param	p_manager			user event manager.
*
* @return	true if the image header is valid, false else.
*/
static bool jp2_read_xml(jp2_t *jp2,
	uint8_t *p_xml_data,
	uint32_t p_xml_size,
	event_mgr_t * p_manager);

/**
* Write XML box
*
* @param jp2					jpeg2000 file codec.
* @param p_nb_bytes_written		pointer to store the nb of bytes written by the function.
*
* @return	the data being copied.
*/
static uint8_t * jp2_write_xml(jp2_t *jp2,
									uint32_t * p_nb_bytes_written);


/**
* Write buffer box
*
* @param boxId					box id.
* @param buffer					buffer with data
* @param jp2					jpeg2000 file codec.
* @param p_nb_bytes_written		pointer to store the nb of bytes written by the function.
*
* @return	the data being copied.
*/
static uint8_t * jp2_write_buffer(uint32_t boxId, 
									jp2_buffer_t* buffer,
									 uint32_t * p_nb_bytes_written);

/**
* Read a UUID box
*
* @param	jp2					jpeg2000 file codec.
* @param	p_header_data		pointer to actual data (already read from file)
* @param	p_header_data_size	size of data
* @param	p_manager			user event manager.
*
* @return	true if the image header is valid, false else.
*/
static bool jp2_read_uuid(jp2_t *jp2,
	uint8_t *p_header_data,
	uint32_t p_header_data_size,
	event_mgr_t * p_manager);

/**
* Writes a UUID box
*
* @param jp2					jpeg2000 file codec.
* @param p_nb_bytes_written		pointer to store the nb of bytes written by the function.
*
* @return	the data being copied.
*/
static uint8_t * jp2_write_uuids(jp2_t *jp2,
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
static bool jp2_read_res(jp2_t *jp2,
	uint8_t *p_resolution_data,
	uint32_t p_resolution_size,
	event_mgr_t * p_manager);

/**
* Writes the Resolution box 
*
* @param jp2					jpeg2000 file codec.
* @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
*
* @return	the data being copied.
*/
static uint8_t * jp2_write_res(jp2_t *jp2,
	uint32_t * p_nb_bytes_written);



/**
 * Writes the Bit per Component box.
 *
 * @param	jp2						jpeg2000 file codec.
 * @param	p_nb_bytes_written		pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
*/
static uint8_t * jp2_write_bpcc(	jp2_t *jp2,
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
static bool jp2_read_bpcc(  jp2_t *jp2,
                                uint8_t * p_bpc_header_data,
                                uint32_t p_bpc_header_size,
                                event_mgr_t * p_manager );

static bool jp2_read_cdef(	jp2_t * jp2,
                                uint8_t * p_cdef_header_data,
                                uint32_t p_cdef_header_size,
                                event_mgr_t * p_manager );

static void jp2_apply_cdef(opj_image_t *image, jp2_color_t *color, event_mgr_t *);

/**
 * Writes the Channel Definition box.
 *
 * @param jp2					jpeg2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t * jp2_write_cdef(   jp2_t *jp2,
                                       uint32_t * p_nb_bytes_written );

/**
 * Writes the Colour Specification box.
 *
 * @param jp2					jpeg2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
*/
static uint8_t * jp2_write_colr(   jp2_t *jp2,
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
static bool jp2_write_ftyp(	jp2_t *jp2,
                                GrokStream *cio,
                                event_mgr_t * p_manager );

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
static bool jp2_read_ftyp(	jp2_t *jp2,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

static bool jp2_skip_jp2c(	jp2_t *jp2,
                                GrokStream *cio,
                                event_mgr_t * p_manager );

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
static bool jp2_read_jp2h(  jp2_t *jp2,
                                uint8_t *p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

/**
 * Writes the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param  jp2      the jpeg2000 file codec.
 * @param  stream      the stream to write data to.
 * @param  p_manager  user event manager.
 *
 * @return true if writing was successful.
 */
static bool jp2_write_jp2h(jp2_t *jp2,
                               GrokStream *stream,
                               event_mgr_t * p_manager );

/**
 * Writes the Jpeg2000 codestream Header box - JP2C Header box. This function must be called AFTER the coding has been done.
 *
 * @param	cio			the stream to write data to.
 * @param	jp2			the jpeg2000 file codec.
 * @param	p_manager	user event manager.
 *
 * @return true if writing was successful.
*/
static bool jp2_write_jp2c(	jp2_t *jp2,
                                GrokStream *cio,
                                event_mgr_t * p_manager );

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
static bool jp2_read_jp(jp2_t *jp2,
                            uint8_t * p_header_data,
                            uint32_t p_header_size,
                            event_mgr_t * p_manager);

/**
 * Writes a jpeg2000 file signature box.
 *
 * @param cio the stream to write data to.
 * @param	jp2			the jpeg2000 file codec.
 * @param p_manager the user event manager.
 *
 * @return true if writing was successful.
 */
static bool jp2_write_jp(	jp2_t *jp2,
                                GrokStream *cio,
                                event_mgr_t * p_manager );

/**
Apply collected palette data
@param color Collector for profile, cdef and pclr data
@param image
*/
static bool jp2_apply_pclr(opj_image_t *image, jp2_color_t *color, event_mgr_t * p_manager);

static void jp2_free_pclr(jp2_color_t *color);

/**
 * Collect palette data
 *
 * @param jp2 JP2 handle
 * @param p_pclr_header_data    FIXME DOC
 * @param p_pclr_header_size    FIXME DOC
 * @param p_manager
 *
 * @return true if successful, returns false otherwise
*/
static bool jp2_read_pclr(	jp2_t *jp2,
                                uint8_t * p_pclr_header_data,
                                uint32_t p_pclr_header_size,
                                event_mgr_t * p_manager );

/**
 * Collect component mapping data
 *
 * @param jp2                 JP2 handle
 * @param p_cmap_header_data  FIXME DOC
 * @param p_cmap_header_size  FIXME DOC
 * @param p_manager           FIXME DOC
 *
 * @return true if successful, returns false otherwise
*/

static bool jp2_read_cmap(	jp2_t * jp2,
                                uint8_t * p_cmap_header_data,
                                uint32_t p_cmap_header_size,
                                event_mgr_t * p_manager );

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
static bool jp2_read_colr(  jp2_t *jp2,
                                uint8_t * p_colr_header_data,
                                uint32_t p_colr_header_size,
                                event_mgr_t * p_manager );

/*@}*/

/*@}*/

/**
 * Sets up the procedures to do on writing header after the codestream.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_setup_end_header_writing (jp2_t *jp2, event_mgr_t * p_manager);

/**
 * Sets up the procedures to do on reading header after the codestream.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_setup_end_header_reading (jp2_t *jp2, event_mgr_t * p_manager);

/**
 * Reads a jpeg2000 file header structure.
 *
 * @param jp2 the jpeg2000 file header structure.
 * @param stream the stream to read data from.
 * @param p_manager the user event manager.
 *
 * @return true if the box is valid.
 */
static bool jp2_read_header_procedure(  jp2_t *jp2,
        GrokStream *stream,
        event_mgr_t * p_manager );

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
static bool jp2_exec (  jp2_t * jp2,
                            procedure_list_t * p_procedure_list,
                            GrokStream *stream,
                            event_mgr_t * p_manager );

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
static bool jp2_read_boxhdr(jp2_box_t *box,
                                uint32_t * p_number_bytes_read,
                                GrokStream *cio,
                                event_mgr_t * p_manager);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool jp2_setup_encoding_validation (jp2_t *jp2, event_mgr_t * p_manager);

/**
 * Sets up the procedures to do on writing header. Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_setup_header_writing (jp2_t *jp2, event_mgr_t * p_manager);

static bool jp2_default_validation (	jp2_t * jp2,
        GrokStream *cio,
        event_mgr_t * p_manager );

/**
 * Finds the image execution function related to the given box id.
 *
 * @param	p_id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const jp2_header_handler_t * jp2_img_find_handler (uint32_t p_id);

/**
 * Finds the execution function related to the given box id.
 *
 * @param	p_id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const jp2_header_handler_t * jp2_find_handler (uint32_t p_id );

static const jp2_header_handler_t jp2_header [] = {
    {JP2_JP,jp2_read_jp},
    {JP2_FTYP,jp2_read_ftyp},
    {JP2_JP2H,jp2_read_jp2h},
	{JP2_XML, jp2_read_xml},
	{ JP2_UUID, jp2_read_uuid}
};

static const jp2_header_handler_t jp2_img_header [] = {
    {JP2_IHDR,jp2_read_ihdr},
    {JP2_COLR,jp2_read_colr},
    {JP2_BPCC,jp2_read_bpcc},
    {JP2_PCLR,jp2_read_pclr},
    {JP2_CMAP,jp2_read_cmap},
    {JP2_CDEF,jp2_read_cdef},
	{JP2_RES, jp2_read_res}

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
static bool jp2_read_boxhdr_char(   jp2_box_t *box,
                                        uint8_t * p_data,
                                        uint32_t * p_number_bytes_read,
                                        int64_t p_box_max_size,
                                        event_mgr_t * p_manager );

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool jp2_setup_decoding_validation (jp2_t *jp2, event_mgr_t * p_manager);

/**
 * Sets up the procedures to do on reading header.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_setup_header_reading (jp2_t *jp2, event_mgr_t * p_manager);

/* ----------------------------------------------------------------------- */
static bool jp2_read_boxhdr(jp2_box_t *box,
                                uint32_t * p_number_bytes_read,
                                GrokStream *cio,
                                event_mgr_t * p_manager )
{
    uint8_t l_data_header [8];
    
    assert(cio != nullptr);
    assert(box != nullptr);
    assert(p_number_bytes_read != nullptr);
    assert(p_manager != nullptr);

    *p_number_bytes_read = (uint32_t)cio->read(l_data_header,8,p_manager);
    if (*p_number_bytes_read != 8) {
        return false;
    }

    /* process read data */
	uint32_t L = 0;
    grok_read_bytes(l_data_header,&L, 4);
	box->length = L;
    grok_read_bytes(l_data_header+4,&(box->type), 4);

    if(box->length == 0) { /* last box */
        const int64_t bleft = cio->get_number_byte_left();
        box->length = bleft + 8U;
        assert( box->length == (uint64_t)(bleft + 8) );
        return true;
    }

    /* read XL  */
    if (box->length == 1) {
        uint32_t l_nb_bytes_read = (uint32_t)cio->read(l_data_header,8,p_manager);
        if (l_nb_bytes_read != 8) {
            if (l_nb_bytes_read > 0) {
                *p_number_bytes_read += l_nb_bytes_read;
            }
            return false;
        }
        grok_read_64(l_data_header,&box->length, 8);
		*p_number_bytes_read += l_nb_bytes_read;
	}
	return true;
}

#if 0
static void jp2_write_url(opj_cio_t *cio, char *Idx_file)
{
    uint32_t i;
    jp2_box_t box;

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

static bool jp2_read_ihdr( jp2_t *jp2,
                               uint8_t *p_image_header_data,
                               uint32_t p_image_header_size,
                               event_mgr_t * p_manager )
{
    
    assert(p_image_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

	if (jp2->comps != nullptr) {
		event_msg(p_manager, EVT_WARNING, "Ignoring ihdr box. First ihdr box already read\n");
		return OPJ_TRUE;
	}
	
	if (p_image_header_size != 14) {
        event_msg(p_manager, EVT_ERROR, "Bad image header box (bad size)\n");
        return false;
    }

    grok_read_bytes(p_image_header_data,&(jp2->h),4);			/* HEIGHT */
    p_image_header_data += 4;
    grok_read_bytes(p_image_header_data,&(jp2->w),4);			/* WIDTH */
    p_image_header_data += 4;
    grok_read_bytes(p_image_header_data,&(jp2->numcomps),2);		/* NC */
    p_image_header_data += 2;

	if ((jp2->numcomps == 0) ||
		(jp2->numcomps > max_num_components)) {
		event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: num components=%d does not conform to standard\n", jp2->numcomps);
		return false;
	}

    /* allocate memory for components */
    jp2->comps = (jp2_comps_t*) grok_calloc(jp2->numcomps, sizeof(jp2_comps_t));
    if (jp2->comps == 0) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to handle image header (ihdr)\n");
        return false;
    }

    grok_read_bytes(p_image_header_data,&(jp2->bpc),1);			/* BPC */
    ++ p_image_header_data;

	///////////////////////////////////////////////////
	// (bits per component == precision -1)
	// Value of 0xFF indicates that bits per component
	// varies by component

	// Otherwise, low 7 bits of bpc determine bits per component,
	// and high bit set indicates signed data,
	// unset indicates unsigned data
	if (((jp2->bpc != 0xFF) &&
		((jp2->bpc & 0x7F) > (max_supported_precision - 1)))) {
		event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: bpc=%d not supported.\n", jp2->bpc);
		return false;
	}

    grok_read_bytes(p_image_header_data,&(jp2->C),1);			/* C */
    ++ p_image_header_data;

    /* Should be equal to 7 cf. chapter about image header box of the norm */
    if (jp2->C != 7) {
        event_msg(p_manager, EVT_WARNING, "JP2 IHDR box: compression type indicate that the file is not a conforming JP2 file (%d) \n", jp2->C);
    }

    grok_read_bytes(p_image_header_data,&(jp2->UnkC),1);			/* UnkC */
    ++ p_image_header_data;

	// UnkC must be binary : {0,1}
	if ((jp2->UnkC > 1)) {
		event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: UnkC=%d does not conform to standard\n", jp2->UnkC);
		return false;
	}

    grok_read_bytes(p_image_header_data,&(jp2->IPR),1);			/* IPR */
    ++ p_image_header_data;

	// IPR must be binary : {0,1}
	if ((jp2->IPR > 1) ) {
		event_msg(p_manager, EVT_ERROR, "JP2 IHDR box: IPR=%d does not conform to standard\n",jp2->IPR);
		return false;
	}

    return true;
}

static uint8_t * jp2_write_ihdr(jp2_t *jp2,
                                    uint32_t * p_nb_bytes_written
                                   )
{
    uint8_t * l_ihdr_data,* l_current_ihdr_ptr;

    
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);

    /* default image header is 22 bytes wide */
    l_ihdr_data = (uint8_t *) grok_calloc(1,22);
    if (l_ihdr_data == nullptr) {
        return nullptr;
    }

    l_current_ihdr_ptr = l_ihdr_data;

	/* write box size */
    grok_write_bytes(l_current_ihdr_ptr,22,4);				
    l_current_ihdr_ptr+=4;

	/* IHDR */
    grok_write_bytes(l_current_ihdr_ptr,JP2_IHDR, 4);		
    l_current_ihdr_ptr+=4;

	/* HEIGHT */
    grok_write_bytes(l_current_ihdr_ptr,jp2->h, 4);		
    l_current_ihdr_ptr+=4;

	/* WIDTH */
    grok_write_bytes(l_current_ihdr_ptr, jp2->w, 4);		
    l_current_ihdr_ptr+=4;

	/* NC */
    grok_write_bytes(l_current_ihdr_ptr, jp2->numcomps, 2);		
    l_current_ihdr_ptr+=2;

	/* BPC */
    grok_write_bytes(l_current_ihdr_ptr, jp2->bpc, 1);		
    ++l_current_ihdr_ptr;

	/* C : Always 7 */
    grok_write_bytes(l_current_ihdr_ptr, jp2->C, 1);		
    ++l_current_ihdr_ptr;

	/* UnkC, colorspace unknown */
    grok_write_bytes(l_current_ihdr_ptr, jp2->UnkC, 1);		
    ++l_current_ihdr_ptr;

	/* IPR, no intellectual property */
    grok_write_bytes(l_current_ihdr_ptr, jp2->IPR, 1);		
    ++l_current_ihdr_ptr;

    *p_nb_bytes_written = 22;

    return l_ihdr_data;
}


static uint8_t * jp2_write_buffer(uint32_t boxId,
									jp2_buffer_t* buffer,
									uint32_t * p_nb_bytes_written){

	
	assert(p_nb_bytes_written != nullptr);

	/* need 8 bytes for box plus buffer->len bytes for buffer*/
	uint32_t total_size = 8 + (uint32_t)buffer->len;
	auto l_data = (uint8_t *)grok_calloc(1, total_size);
	if (l_data == nullptr) {
		return nullptr;
	}

	uint8_t * l_current_ptr = l_data;

	/* write box size */
	grok_write_bytes(l_current_ptr, total_size, 4);			
	l_current_ptr += 4;

	/* write box id */
	grok_write_bytes(l_current_ptr, boxId, 4);					
	l_current_ptr += 4;

	/* write buffer data */
	memcpy(l_current_ptr, buffer->buffer, buffer->len);				

	*p_nb_bytes_written = total_size;
	return l_data;

}


static bool jp2_read_xml(jp2_t *jp2,
							uint8_t *p_xml_data,
							uint32_t p_xml_size,
							event_mgr_t * p_manager) {

	(void)p_manager;
	if (!p_xml_data || !p_xml_size) {
		return false;
	}
	jp2->xml.alloc(p_xml_size);
	if (!jp2->xml.buffer) {
		jp2->xml.len = 0;
		return false;
	}
	memcpy(jp2->xml.buffer, p_xml_data, p_xml_size);
	return true;
}

static uint8_t * jp2_write_xml(jp2_t *jp2,
									uint32_t * p_nb_bytes_written) {

	
	assert(jp2 != nullptr);
	return jp2_write_buffer(JP2_XML, &jp2->xml, p_nb_bytes_written);
}


static bool jp2_read_uuid(jp2_t *jp2,
								uint8_t *p_header_data,
								uint32_t p_header_size,
								event_mgr_t * p_manager) {

	if (!p_header_data || !p_header_size || p_header_size < 16) {
		return false;
	}

	if (jp2->numUuids == JP2_MAX_NUM_UUIDS) {
		event_msg(p_manager, EVT_WARNING, "Reached maximum (%d) number of UUID boxes read - ignoring UUID box\n", JP2_MAX_NUM_UUIDS);
		return false;
	}
	auto uuid = jp2->uuids + jp2->numUuids;
	memcpy(uuid->uuid, p_header_data, 16);
	p_header_data += 16;
	if (uuid->alloc(p_header_size - 16)) {
		memcpy(uuid->buffer, p_header_data, uuid->len);
		jp2->numUuids++;
		return true;
	}
	return false;

}

static uint8_t * jp2_write_uuids(jp2_t *jp2,
									uint32_t * p_nb_bytes_written) {

	
	assert(jp2 != nullptr);
	assert(p_nb_bytes_written != nullptr);

	// calculate total size needed for all uuids
	size_t total_uuid_size = 0;
	for (size_t i = 0; i < jp2->numUuids; ++i) {
		auto uuid = jp2->uuids + i;
		if (uuid->buffer && uuid->len) {
			total_uuid_size += 8 + 16 + uuid->len;
		}
	}
	auto l_uuid_data = (uint8_t *)grok_calloc(1, total_uuid_size);
	if (l_uuid_data == nullptr) {
		return nullptr;
	}
	uint8_t *l_current_uuid_ptr = l_uuid_data;

	// write the uuids
	for (size_t i = 0; i < jp2->numUuids; ++i) {
		auto uuid = jp2->uuids + i;
		if (uuid->buffer && uuid->len) {
			/* write box size */
			grok_write_bytes(l_current_uuid_ptr, (uint32_t)(8 + 16 + uuid->len), 4);
			l_current_uuid_ptr += 4;

			/* JP2_UUID */
			grok_write_bytes(l_current_uuid_ptr, JP2_UUID, 4);					
			l_current_uuid_ptr += 4;

			/* uuid  */
			memcpy(l_current_uuid_ptr, uuid->uuid, 16);							
			l_current_uuid_ptr += 16;

			/* uuid data */
			memcpy(l_current_uuid_ptr, uuid->buffer, (uint32_t)uuid->len);	
			l_current_uuid_ptr += uuid->len;
		}
	}
	*p_nb_bytes_written = (uint32_t)total_uuid_size;
	return l_uuid_data;
}


double calc_res(uint16_t num, uint16_t den, int8_t exponent) {
	if (den == 0)
		return 0;
	return ((double)num / den) * pow(10, exponent);
}

static bool jp2_read_res_box(uint32_t *id,
								 uint32_t *num,
								 uint32_t *den,
								 uint32_t *exponent,
								uint8_t **p_resolution_data,
								event_mgr_t * p_manager) {
	(void)p_manager;
	uint32_t box_size = 4 + 4 + 10;

	uint32_t size = 0;
	grok_read_bytes(*p_resolution_data, &size, 4);
	*p_resolution_data += 4;
	if (size != box_size)
		return false;

	grok_read_bytes(*p_resolution_data, id, 4);
	*p_resolution_data += 4;

	grok_read_bytes(*p_resolution_data, num+1, 2);
	*p_resolution_data += 2;

	grok_read_bytes(*p_resolution_data, den+1, 2);
	*p_resolution_data += 2;

	grok_read_bytes(*p_resolution_data, num, 2);
	*p_resolution_data += 2;

	grok_read_bytes(*p_resolution_data, den, 2);
	*p_resolution_data += 2;

	grok_read_bytes(*p_resolution_data, exponent+1, 1);
	*p_resolution_data += 1;

	grok_read_bytes(*p_resolution_data, exponent, 1);
	*p_resolution_data += 1;

	return true;

}

static bool jp2_read_res(jp2_t *jp2,
							uint8_t *p_resolution_data,
							uint32_t p_resolution_size,
							event_mgr_t * p_manager){
	assert(p_resolution_data != nullptr);
	assert(jp2 != nullptr);
	assert(p_manager != nullptr);

	uint32_t num_boxes = p_resolution_size / OPJ_RESOLUTION_BOX_SIZE;
	if (num_boxes == 0 || 
		num_boxes > 2 || 
		(p_resolution_size % OPJ_RESOLUTION_BOX_SIZE) ) {
			event_msg(p_manager, EVT_ERROR, "Bad resolution box (bad size)\n");
			return false;
	}

	while (p_resolution_size > 0) {

		uint32_t id;
		uint32_t num[2];
		uint32_t den[2];
		uint32_t exponent[2];

		if (!jp2_read_res_box(&id,
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
			res[i] = calc_res((uint16_t)num[i], (uint16_t)den[i], (uint8_t)exponent[i]);

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
		if (fabs(x - (double)a[i])<eps || (p[i] > USHRT_MAX) || (q[i] > USHRT_MAX))
			break;
		x = 1.0 / (x - (double)a[i]);
	}
	*num = (uint32_t)p[i - 1];
	*den = (uint32_t)q[i - 1];
}

static void jp2_write_res_box( double resx, double resy,
									uint32_t box_id,
									uint8_t **l_current_res_ptr) {

	/* write box size */
	grok_write_bytes(*l_current_res_ptr, OPJ_RESOLUTION_BOX_SIZE, 4);		
	*l_current_res_ptr += 4;

	/* Box ID */
	grok_write_bytes(*l_current_res_ptr, box_id, 4);		
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
		grok_write_bytes(*l_current_res_ptr, num[i], 2);
		*l_current_res_ptr += 2;
		grok_write_bytes(*l_current_res_ptr, den[i], 2);
		*l_current_res_ptr += 2;
	}
	for (size_t i = 0; i < 2; ++i) {
		grok_write_bytes(*l_current_res_ptr, exponent[i], 1);
		*l_current_res_ptr += 1;
	}
}

static uint8_t * jp2_write_res(jp2_t *jp2,
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

	l_res_data = (uint8_t *)grok_calloc(1, size);
	if (l_res_data == nullptr) {
		return nullptr;
	}

	l_current_res_ptr = l_res_data;

	/* write super-box size */
	grok_write_bytes(l_current_res_ptr, size, 4);		
	l_current_res_ptr += 4;

	/* Super-box ID */
	grok_write_bytes(l_current_res_ptr, JP2_RES, 4);		
	l_current_res_ptr += 4;

	if (storeCapture) {
		jp2_write_res_box(jp2->capture_resolution[0],
								jp2->capture_resolution[1],
								JP2_CAPTURE_RES,
								&l_current_res_ptr);
	}
	if (storeDisplay) {
		jp2_write_res_box(jp2->display_resolution[0],
								jp2->display_resolution[1],
								JP2_DISPLAY_RES,
								&l_current_res_ptr);
	}
	*p_nb_bytes_written = size;
	return l_res_data;
}

static uint8_t * jp2_write_bpcc(	jp2_t *jp2,
                                        uint32_t * p_nb_bytes_written
                                   )
{
    uint32_t i;
    /* room for 8 bytes for box and 1 byte for each component */
    uint32_t l_bpcc_size = 8 + jp2->numcomps;
    uint8_t * l_bpcc_data,* l_current_bpcc_ptr;

    
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);

    l_bpcc_data = (uint8_t *) grok_calloc(1,l_bpcc_size);
    if (l_bpcc_data == nullptr) {
        return nullptr;
    }

    l_current_bpcc_ptr = l_bpcc_data;

	/* write box size */
    grok_write_bytes(l_current_bpcc_ptr,l_bpcc_size,4);				
    l_current_bpcc_ptr += 4;

	/* BPCC */
    grok_write_bytes(l_current_bpcc_ptr,JP2_BPCC,4);					
    l_current_bpcc_ptr += 4;

    for (i = 0; i < jp2->numcomps; ++i)  {
		/* write each component information */
        grok_write_bytes(l_current_bpcc_ptr, jp2->comps[i].bpcc, 1); 
        ++l_current_bpcc_ptr;
    }

    *p_nb_bytes_written = l_bpcc_size;

    return l_bpcc_data;
}

static bool jp2_read_bpcc( jp2_t *jp2,
                               uint8_t * p_bpc_header_data,
                               uint32_t p_bpc_header_size,
                               event_mgr_t * p_manager
                             )
{
    uint32_t i;

    
    assert(p_bpc_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);


    if (jp2->bpc != 255 ) {
        event_msg(p_manager, EVT_WARNING, "A BPCC header box is available although BPC given by the IHDR box (%d) indicate components bit depth is constant\n",jp2->bpc);
    }

    /* and length is relevant */
    if (p_bpc_header_size != jp2->numcomps) {
        event_msg(p_manager, EVT_ERROR, "Bad BPCC header box (bad size)\n");
        return false;
    }

    /* read info for each component */
    for (i = 0; i < jp2->numcomps; ++i) {
        grok_read_bytes(p_bpc_header_data,&jp2->comps[i].bpcc ,1);	/* read each BPCC component */
        ++p_bpc_header_data;
    }

    return true;
}
static uint8_t * jp2_write_cdef(jp2_t *jp2, uint32_t * p_nb_bytes_written)
{
    /* room for 8 bytes for box, 2 for n */
    uint32_t l_cdef_size = 10;
    uint8_t * l_cdef_data,* l_current_cdef_ptr;
    uint32_t l_value;
    uint16_t i;

    
    assert(jp2 != nullptr);
    assert(p_nb_bytes_written != nullptr);
    assert(jp2->color.jp2_cdef != nullptr);
    assert(jp2->color.jp2_cdef->info != nullptr);
    assert(jp2->color.jp2_cdef->n > 0U);

    l_cdef_size += 6U * jp2->color.jp2_cdef->n;

    l_cdef_data = (uint8_t *) grok_malloc(l_cdef_size);
    if (l_cdef_data == nullptr) {
        return nullptr;
    }

    l_current_cdef_ptr = l_cdef_data;

	/* write box size */
    grok_write_bytes(l_current_cdef_ptr,l_cdef_size,4);			
    l_current_cdef_ptr += 4;

	/* BPCC */
    grok_write_bytes(l_current_cdef_ptr,JP2_CDEF,4);					
    l_current_cdef_ptr += 4;

    l_value = jp2->color.jp2_cdef->n;
	/* N */
    grok_write_bytes(l_current_cdef_ptr,l_value,2);					
    l_current_cdef_ptr += 2;

    for (i = 0U; i < jp2->color.jp2_cdef->n; ++i) {
        l_value = jp2->color.jp2_cdef->info[i].cn;
		/* Cni */
        grok_write_bytes(l_current_cdef_ptr,l_value,2);					
        l_current_cdef_ptr += 2;
        l_value = jp2->color.jp2_cdef->info[i].typ;
		/* Typi */
        grok_write_bytes(l_current_cdef_ptr,l_value,2);					
        l_current_cdef_ptr += 2;
        l_value = jp2->color.jp2_cdef->info[i].asoc;
		/* Asoci */
        grok_write_bytes(l_current_cdef_ptr,l_value,2);					
        l_current_cdef_ptr += 2;
    }
    *p_nb_bytes_written = l_cdef_size;

    return l_cdef_data;
}

static uint8_t * jp2_write_colr(  jp2_t *jp2,
                                      uint32_t * p_nb_bytes_written
                                   )
{
    /* room for 8 bytes for box 3 for common data and variable upon profile*/
    uint32_t l_colr_size = 11;
    uint8_t * l_colr_data,* l_current_colr_ptr;

    
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

    l_colr_data = (uint8_t *) grok_calloc(1,l_colr_size);
    if (l_colr_data == nullptr) {
        return nullptr;
    }

    l_current_colr_ptr = l_colr_data;

	/* write box size */
    grok_write_bytes(l_current_colr_ptr,l_colr_size,4);				
    l_current_colr_ptr += 4;

	/* BPCC */
    grok_write_bytes(l_current_colr_ptr,JP2_COLR,4);					
    l_current_colr_ptr += 4;

	/* METH */
    grok_write_bytes(l_current_colr_ptr, jp2->meth,1);				
    ++l_current_colr_ptr;

	/* PRECEDENCE */
    grok_write_bytes(l_current_colr_ptr, jp2->precedence,1);			
    ++l_current_colr_ptr;

	/* APPROX */
    grok_write_bytes(l_current_colr_ptr, jp2->approx,1);				
    ++l_current_colr_ptr;

	/* Meth value is restricted to 1 or 2 (Table I.9 of part 1) */
    if (jp2->meth == 1) { 
		/* EnumCS */
        grok_write_bytes(l_current_colr_ptr, jp2->enumcs,4);
    }       
    else {
		/* ICC profile */
        if (jp2->meth == 2) {                                     
            uint32_t i;
            for(i = 0; i < jp2->color.icc_profile_len; ++i) {
                grok_write_bytes(l_current_colr_ptr, jp2->color.icc_profile_buf[i], 1);
                ++l_current_colr_ptr;
            }
        }
    }

    *p_nb_bytes_written = l_colr_size;
    return l_colr_data;
}

static void jp2_free_pclr(jp2_color_t *color)
{
	if (color) {
		if (color->jp2_pclr) {
			if (color->jp2_pclr->channel_sign) {
				grok_free(color->jp2_pclr->channel_sign);
			}
			if (color->jp2_pclr->channel_size) {
				grok_free(color->jp2_pclr->channel_size);
			}
			if (color->jp2_pclr->entries) {
				grok_free(color->jp2_pclr->entries);
			}

			if (color->jp2_pclr->cmap) {
				grok_free(color->jp2_pclr->cmap);
			}
			grok_free(color->jp2_pclr);
			color->jp2_pclr = nullptr;
		}
	}
}

static bool jp2_check_color(opj_image_t *image, jp2_color_t *color, event_mgr_t *p_manager)
{
    uint16_t i;

    /* testcase 4149.pdf.SIGSEGV.cf7.3501 */
    if (color->jp2_cdef) {
        jp2_cdef_info_t *info = color->jp2_cdef->info;
        uint16_t n = color->jp2_cdef->n;
        uint32_t nr_channels = image->numcomps; /* FIXME image->numcomps == jp2->numcomps before color is applied ??? */

        /* cdef applies to cmap channels if any */
        if (color->jp2_pclr && color->jp2_pclr->cmap) {
            nr_channels = (uint32_t)color->jp2_pclr->nr_channels;
        }

        for (i = 0; i < n; i++) {
            if (info[i].cn >= nr_channels) {
                event_msg(p_manager, EVT_ERROR, "Invalid component index %d (>= %d).\n", info[i].cn, nr_channels);
                return false;
            }
            if (info[i].asoc == 65535U) continue;

            if (info[i].asoc > 0 && (uint32_t)(info[i].asoc - 1) >= nr_channels) {
                event_msg(p_manager, EVT_ERROR, "Invalid component index %d (>= %d).\n", info[i].asoc - 1, nr_channels);
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
                event_msg(p_manager, EVT_ERROR, "Incomplete channel definitions.\n");
                return false;
            }
            --nr_channels;
        }
    }

    /* testcases 451.pdf.SIGSEGV.f4c.3723, 451.pdf.SIGSEGV.5b5.3723 and
       66ea31acbb0f23a2bbc91f64d69a03f5_signal_sigsegv_13937c0_7030_5725.pdf */
    if (color->jp2_pclr && color->jp2_pclr->cmap) {
        uint16_t nr_channels = color->jp2_pclr->nr_channels;
        jp2_cmap_comp_t *cmap = color->jp2_pclr->cmap;
		bool *pcol_usage = nullptr;
		bool is_sane = true;

        /* verify that all original components match an existing one */
        for (i = 0; i < nr_channels; i++) {
            if (cmap[i].cmp >= image->numcomps) {
                event_msg(p_manager, EVT_ERROR, "Invalid component index %d (>= %d).\n", cmap[i].cmp, image->numcomps);
                is_sane = false;
				goto cleanup;
            }
        }

        pcol_usage = (bool *) grok_calloc(nr_channels, sizeof(bool));
        if (!pcol_usage) {
            event_msg(p_manager, EVT_ERROR, "Unexpected OOM.\n");
            return false;
        }
        /* verify that no component is targeted more than once */
        for (i = 0; i < nr_channels; i++) {
            uint16_t pcol = cmap[i].pcol;
			if (cmap[i].mtyp != 0 && cmap[i].mtyp != 1) {
				event_msg(p_manager, EVT_ERROR, "Unexpected MTYP value.\n");
				is_sane = false;
				goto cleanup;
			}
            if (pcol >= nr_channels) {
                event_msg(p_manager, EVT_ERROR, "Invalid component/palette index for direct mapping %d.\n", pcol);
                is_sane = false;
				goto cleanup;
            } else if (pcol_usage[pcol] && cmap[i].mtyp == 1) {
                event_msg(p_manager, EVT_ERROR, "Component %d is mapped twice.\n", pcol);
                is_sane = false;
				goto cleanup;
            } else if (cmap[i].mtyp == 0 && cmap[i].pcol != 0) {
                /* I.5.3.5 PCOL: If the value of the MTYP field for this channel is 0, then
                 * the value of this field shall be 0. */
                event_msg(p_manager, EVT_ERROR, "Direct use at #%d however pcol=%d.\n", i, pcol);
                is_sane = false;
				goto cleanup;
            } else
                pcol_usage[pcol] = true;
        }
        /* verify that all components are targeted at least once */
        for (i = 0; i < nr_channels; i++) {
            if (!pcol_usage[i] && cmap[i].mtyp != 0) {
                event_msg(p_manager, EVT_ERROR, "Component %d doesn't have a mapping.\n", i);
                is_sane = false;
				goto cleanup;
            }
        }
        /* Issue 235/447 weird cmap */
        if (1 && is_sane && (image->numcomps==1U)) {
            for (i = 0; i < nr_channels; i++) {
                if (!pcol_usage[i]) {
                    is_sane = 0U;
                    event_msg(p_manager, EVT_WARNING, "Component mapping seems wrong. Trying to correct.\n", i);
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
	cleanup:
		if (pcol_usage)
			grok_free(pcol_usage);
        if (!is_sane) {
            return false;
        }
    }

    return true;
}

static bool jp2_apply_pclr(opj_image_t *image, jp2_color_t *color, event_mgr_t * p_manager)
{
    opj_image_comp_t *old_comps, *new_comps;
    uint8_t *channel_size, *channel_sign;
    uint32_t *entries;
    jp2_cmap_comp_t *cmap;
    int32_t *src, *dst;
    uint32_t j, max;
    uint16_t i, nr_channels, cmp, pcol;
    int32_t k, top_k;

    channel_size = color->jp2_pclr->channel_size;
    channel_sign = color->jp2_pclr->channel_sign;
    entries = color->jp2_pclr->entries;
    cmap = color->jp2_pclr->cmap;
    nr_channels = color->jp2_pclr->nr_channels;


	for (i = 0; i < nr_channels; ++i) {
		/* Palette mapping: */
		cmp = cmap[i].cmp;
		if (image->comps[cmp].data == nullptr) {
			event_msg(p_manager, EVT_ERROR,
				"image->comps[%d].data == nullptr in opj_jp2_apply_pclr().\n", i);
			return false;
		}
	}


    old_comps = image->comps;
    new_comps = (opj_image_comp_t*)grok_malloc(nr_channels * sizeof(opj_image_comp_t));
    if (!new_comps) {
		event_msg(p_manager, EVT_ERROR,
			"Memory allocation failure in opj_jp2_apply_pclr().\n");
        return false;
    }
    for(i = 0; i < nr_channels; ++i) {
        pcol = cmap[i].pcol;
        cmp = cmap[i].cmp;

        /* Direct use */
        if(cmap[i].mtyp == 0) {
            assert( pcol == 0 );
            new_comps[i] = old_comps[cmp];
            new_comps[i].data = nullptr;
        } else {
            assert( i == pcol );
            new_comps[pcol] = old_comps[cmp];
            new_comps[pcol].data = nullptr;
        }

        /* Palette mapping: */
        if (!opj_image_single_component_data_alloc(new_comps + i)) {
			while (i > 0) {
				--i;
				grok_aligned_free (new_comps[i].data);
			}
			grok_free(new_comps);
			event_msg(p_manager, EVT_ERROR,	"Memory allocation failure in opj_jp2_apply_pclr().\n");
			return false;
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
                if((k = src[j]) < 0) 
					k = 0;
                else if(k > top_k) 
					k = top_k;

                /* The colour */
                dst[j] = (int32_t)entries[k * nr_channels + pcol];
            }
        }
    }

    max = image->numcomps;
    for (i = 0; i < max; ++i) {
        opj_image_single_component_data_free(old_comps + i);
    }
    grok_free(old_comps);
    image->comps = new_comps;
    image->numcomps = nr_channels;
	return true;

}/* apply_pclr() */

static bool jp2_read_pclr(	jp2_t *jp2,
                                uint8_t * p_pclr_header_data,
                                uint32_t p_pclr_header_size,
                                event_mgr_t * p_manager
                             )
{
    jp2_pclr_t *jp2_pclr;
    uint8_t *channel_size, *channel_sign;
    uint32_t *entries;
    uint16_t nr_entries,nr_channels;
    uint16_t i, j;
    uint32_t l_value;
    uint8_t *orig_header_data = p_pclr_header_data;

    
    assert(p_pclr_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);
    (void)p_pclr_header_size;

    if(jp2->color.jp2_pclr)
        return false;

    if (p_pclr_header_size < 3)
        return false;

    grok_read_bytes(p_pclr_header_data, &l_value , 2);	/* NE */
    p_pclr_header_data += 2;
    nr_entries = (uint16_t) l_value;
    if ((nr_entries == 0U) || (nr_entries > 1024U)) {
        event_msg(p_manager, EVT_ERROR, "Invalid PCLR box. Reports %d entries\n", (int)nr_entries);
        return false;
    }

    grok_read_bytes(p_pclr_header_data, &l_value , 1);	/* NPC */
    ++p_pclr_header_data;
    nr_channels = (uint16_t) l_value;
    if (nr_channels == 0U) {
        event_msg(p_manager, EVT_ERROR, "Invalid PCLR box. Reports 0 palette columns\n");
        return false;
    }

    if (p_pclr_header_size < 3 + (uint32_t)nr_channels)
        return false;

    entries = (uint32_t*) grok_malloc((size_t)nr_channels * nr_entries * sizeof(uint32_t));
    if (!entries)
        return false;
    channel_size = (uint8_t*) grok_malloc(nr_channels);
    if (!channel_size) {
        grok_free(entries);
        return false;
    }
    channel_sign = (uint8_t*) grok_malloc(nr_channels);
    if (!channel_sign) {
        grok_free(entries);
        grok_free(channel_size);
        return false;
    }

    jp2_pclr = (jp2_pclr_t*)grok_malloc(sizeof(jp2_pclr_t));
    if (!jp2_pclr) {
        grok_free(entries);
        grok_free(channel_size);
        grok_free(channel_sign);
        return false;
    }

    jp2_pclr->channel_sign = channel_sign;
    jp2_pclr->channel_size = channel_size;
    jp2_pclr->entries = entries;
    jp2_pclr->nr_entries = nr_entries;
    jp2_pclr->nr_channels = (uint8_t) l_value;
    jp2_pclr->cmap = nullptr;

    jp2->color.jp2_pclr = jp2_pclr;

    for(i = 0; i < nr_channels; ++i) {
        grok_read_bytes(p_pclr_header_data, &l_value , 1);	/* Bi */
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

            grok_read_bytes(p_pclr_header_data, &l_value , bytes_to_read);	/* Cji */
            p_pclr_header_data += bytes_to_read;
            *entries = (uint32_t) l_value;
            entries++;
        }
    }

    return true;
}

static bool jp2_read_cmap(	jp2_t * jp2,
                                uint8_t * p_cmap_header_data,
                                uint32_t p_cmap_header_size,
                                event_mgr_t * p_manager
                             )
{
    jp2_cmap_comp_t *cmap;
    uint8_t i, nr_channels;
    uint32_t l_value;

    
    assert(jp2 != nullptr);
    assert(p_cmap_header_data != nullptr);
    assert(p_manager != nullptr);
    (void)p_cmap_header_size;

    /* Need nr_channels: */
    if(jp2->color.jp2_pclr == nullptr) {
        event_msg(p_manager, EVT_ERROR, "Need to read a PCLR box before the CMAP box.\n");
        return false;
    }

    /* Part 1, I.5.3.5: 'There shall be at most one Component Mapping box
     * inside a JP2 Header box' :
    */
    if(jp2->color.jp2_pclr->cmap) {
        event_msg(p_manager, EVT_ERROR, "Only one CMAP box is allowed.\n");
        return false;
    }

    nr_channels = jp2->color.jp2_pclr->nr_channels;
    if (p_cmap_header_size < (uint32_t)nr_channels * 4) {
        event_msg(p_manager, EVT_ERROR, "Insufficient data for CMAP box.\n");
        return false;
    }

    cmap = (jp2_cmap_comp_t*) grok_malloc(nr_channels * sizeof(jp2_cmap_comp_t));
    if (!cmap)
        return false;


    for(i = 0; i < nr_channels; ++i) {
        grok_read_bytes(p_cmap_header_data, &l_value, 2);			/* CMP^i */
        p_cmap_header_data +=2;
        cmap[i].cmp = (uint16_t) l_value;

        grok_read_bytes(p_cmap_header_data, &l_value, 1);			/* MTYP^i */
        ++p_cmap_header_data;
        cmap[i].mtyp = (uint8_t) l_value;

        grok_read_bytes(p_cmap_header_data, &l_value, 1);			/* PCOL^i */
        ++p_cmap_header_data;
        cmap[i].pcol = (uint8_t) l_value;
    }

    jp2->color.jp2_pclr->cmap = cmap;

    return true;
}

static void jp2_apply_cdef(opj_image_t *image, jp2_color_t *color, event_mgr_t *manager)
{
    jp2_cdef_info_t *info;
    uint16_t i, n, cn, asoc, acn;

    info = color->jp2_cdef->info;
    n = color->jp2_cdef->n;

    for(i = 0; i < n; ++i) {
        /* WATCH: acn = asoc - 1 ! */
        asoc = info[i].asoc;
        cn = info[i].cn;

        if( cn >= image->numcomps) {
            event_msg(manager, EVT_WARNING, "jp2_apply_cdef: cn=%d, numcomps=%d\n", cn, image->numcomps);
            continue;
        }
        if(asoc == 0 || asoc == 65535) {
            image->comps[cn].alpha = info[i].typ;
            continue;
        }

        acn = (uint16_t)(asoc - 1);
        if( acn >= image->numcomps ) {
            event_msg(manager, EVT_WARNING, "jp2_apply_cdef: acn=%d, numcomps=%d\n", acn, image->numcomps);
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

    if(color->jp2_cdef->info) grok_free(color->jp2_cdef->info);

    grok_free(color->jp2_cdef);
    color->jp2_cdef = nullptr;

}/* jp2_apply_cdef() */

static bool jp2_read_cdef(	jp2_t * jp2,
                                uint8_t * p_cdef_header_data,
                                uint32_t p_cdef_header_size,
                                event_mgr_t * p_manager
                             )
{
    jp2_cdef_info_t *cdef_info;
    uint16_t i;
    uint32_t l_value;

    
    assert(jp2 != nullptr);
    assert(p_cdef_header_data != nullptr);
    assert(p_manager != nullptr);
    (void)p_cdef_header_size;

    /* Part 1, I.5.3.6: 'The shall be at most one Channel Definition box
     * inside a JP2 Header box.'*/
    if(jp2->color.jp2_cdef) return false;

    if (p_cdef_header_size < 2) {
        event_msg(p_manager, EVT_ERROR, "CDEF box: Insufficient data.\n");
        return false;
    }

    grok_read_bytes(p_cdef_header_data,&l_value ,2);			/* N */
    p_cdef_header_data+= 2;

    if ( (uint16_t)l_value == 0) { /* szukw000: FIXME */
        event_msg(p_manager, EVT_ERROR, "CDEF box: Number of channel description is equal to zero.\n");
        return false;
    }

    if (p_cdef_header_size < 2 + (uint32_t)(uint16_t)l_value * 6) {
        event_msg(p_manager, EVT_ERROR, "CDEF box: Insufficient data.\n");
        return false;
    }

    cdef_info = (jp2_cdef_info_t*) grok_malloc(l_value * sizeof(jp2_cdef_info_t));
    if (!cdef_info)
        return false;

    jp2->color.jp2_cdef = (jp2_cdef_t*)grok_malloc(sizeof(jp2_cdef_t));
    if(!jp2->color.jp2_cdef) {
        grok_free(cdef_info);
        return false;
    }
    jp2->color.jp2_cdef->info = cdef_info;
    jp2->color.jp2_cdef->n = (uint16_t) l_value;

    for(i = 0; i < jp2->color.jp2_cdef->n; ++i) {
        grok_read_bytes(p_cdef_header_data, &l_value, 2);			/* Cn^i */
        p_cdef_header_data +=2;
        cdef_info[i].cn = (uint16_t) l_value;

        grok_read_bytes(p_cdef_header_data, &l_value, 2);			/* Typ^i */
        p_cdef_header_data +=2;
        cdef_info[i].typ = (uint16_t) l_value;

        grok_read_bytes(p_cdef_header_data, &l_value, 2);			/* Asoc^i */
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
				event_msg(p_manager, EVT_ERROR, "CDEF box : multiple descriptions of component, %d, with differing types : %d and %d.\n", infoi.cn, infoi.typ, infoj.typ);
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
				event_msg(p_manager, EVT_ERROR, "CDEF box : components %d and %d share same type/association pair (%d,%d).\n", infoi.cn, infoj.cn, infoj.typ, infoj.asoc);
				return false;
			}
		}
	}

    return true;
}

static bool jp2_read_colr( jp2_t *jp2,
                               uint8_t * p_colr_header_data,
                               uint32_t p_colr_header_size,
                               event_mgr_t * p_manager
                             )
{
    uint32_t l_value;

    
    assert(jp2 != nullptr);
    assert(p_colr_header_data != nullptr);
    assert(p_manager != nullptr);

    if (p_colr_header_size < 3) {
        event_msg(p_manager, EVT_ERROR, "Bad COLR header box (bad size)\n");
        return false;
    }

    /* Part 1, I.5.3.3 : 'A conforming JP2 reader shall ignore all colour
     * specification boxes after the first.'
    */
    if(jp2->color.jp2_has_colour_specification_box) {
        event_msg(p_manager, EVT_WARNING, "A conforming JP2 reader shall ignore all colour specification boxes after the first, so we ignore this one.\n");
        p_colr_header_data += p_colr_header_size;
        return true;
    }

    grok_read_bytes(p_colr_header_data,&jp2->meth ,1);			/* METH */
    ++p_colr_header_data;

    grok_read_bytes(p_colr_header_data,&jp2->precedence ,1);		/* PRECEDENCE */
    ++p_colr_header_data;

    grok_read_bytes(p_colr_header_data,&jp2->approx ,1);			/* APPROX */
    ++p_colr_header_data;

    if (jp2->meth == 1) {
        if (p_colr_header_size < 7) {
            event_msg(p_manager, EVT_ERROR, "Bad COLR header box (bad size: %d)\n", p_colr_header_size);
            return false;
        }
        if ((p_colr_header_size > 7) && (jp2->enumcs != 14)) { /* handled below for CIELab) */
            /* testcase Altona_Technical_v20_x4.pdf */
            event_msg(p_manager, EVT_WARNING, "Bad COLR header box (bad size: %d)\n", p_colr_header_size);
        }

        grok_read_bytes(p_colr_header_data,&jp2->enumcs ,4);			/* EnumCS */

        p_colr_header_data += 4;

        if(jp2->enumcs == 14) { /* CIELab */
            uint32_t *cielab;
            uint32_t rl, ol, ra, oa, rb, ob, il;

            cielab = (uint32_t*)grok_malloc(9 * sizeof(uint32_t));
			if (cielab == nullptr) {
				event_msg(p_manager, EVT_ERROR, "Not enough memory for cielab\n");
				return false;
			}
            cielab[0] = 14; /* enumcs */

            /* default values */
            rl = ra = rb = ol = oa = ob = 0;
            il = 0x00443530; /* D50 */
            cielab[1] = 0x44454600;/* DEF */

            if(p_colr_header_size == 35) {
                grok_read_bytes(p_colr_header_data, &rl, 4);
                p_colr_header_data += 4;
                grok_read_bytes(p_colr_header_data, &ol, 4);
                p_colr_header_data += 4;
                grok_read_bytes(p_colr_header_data, &ra, 4);
                p_colr_header_data += 4;
                grok_read_bytes(p_colr_header_data, &oa, 4);
                p_colr_header_data += 4;
                grok_read_bytes(p_colr_header_data, &rb, 4);
                p_colr_header_data += 4;
                grok_read_bytes(p_colr_header_data, &ob, 4);
                p_colr_header_data += 4;
                grok_read_bytes(p_colr_header_data, &il, 4);
                p_colr_header_data += 4;

                cielab[1] = 0;
            } else if(p_colr_header_size != 7) {
                event_msg(p_manager, EVT_WARNING, "Bad COLR header box (CIELab, bad size: %d)\n", p_colr_header_size);
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
        jp2->color.jp2_has_colour_specification_box = 1;
    } else if (jp2->meth == 2) {
        /* ICC profile */
        int32_t it_icc_value = 0;
        int32_t icc_len = (int32_t)p_colr_header_size - 3;

        jp2->color.icc_profile_len = (uint32_t)icc_len;
        jp2->color.icc_profile_buf = (uint8_t*) grok_calloc(1,(size_t)icc_len);
        if (!jp2->color.icc_profile_buf) {
            jp2->color.icc_profile_len = 0;
            return false;
        }

        for (it_icc_value = 0; it_icc_value < icc_len; ++it_icc_value) {
            grok_read_bytes(p_colr_header_data,&l_value,1);		/* icc values */
            ++p_colr_header_data;
            jp2->color.icc_profile_buf[it_icc_value] = (uint8_t) l_value;
        }

        jp2->color.jp2_has_colour_specification_box = 1;
    } else if (jp2->meth > 2) {
        /*	ISO/IEC 15444-1:2004 (E), Table I.9 Legal METH values:
        conforming JP2 reader shall ignore the entire Colour Specification box.*/
        event_msg(p_manager, EVT_WARNING, "COLR BOX meth value is not a regular value (%d), "
                      "so we will ignore the entire Colour Specification box. \n", jp2->meth);
    }
    return true;
}

bool jp2_decode(jp2_t *jp2,
					grok_plugin_tile_t* tile,
                    GrokStream *p_stream,
                    opj_image_t* p_image,
                    event_mgr_t * p_manager)
{
    if (!p_image)
        return false;

    /* J2K decoding */
    if( ! j2k_decode(jp2->j2k, tile, p_stream, p_image, p_manager) ) {
        event_msg(p_manager, EVT_ERROR, "Failed to decode the codestream in the JP2 file\n");
        return false;
    }

    if (!jp2->ignore_pclr_cmap_cdef) {
        if (!jp2_check_color(p_image, &(jp2->color), p_manager)) {
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
                jp2_free_pclr(&(jp2->color));
			else {
				if (!jp2_apply_pclr(p_image, &(jp2->color), p_manager))
					return false;
			}
        }

        /* Apply channel definitions if needed */
        if(jp2->color.jp2_cdef) {
            jp2_apply_cdef(p_image, &(jp2->color), p_manager);
        }

		// retrieve icc profile
        if(jp2->color.icc_profile_buf) {
            p_image->icc_profile_buf = jp2->color.icc_profile_buf;
            p_image->icc_profile_len = jp2->color.icc_profile_len;
            jp2->color.icc_profile_buf = nullptr;
        }

		// retrieve special uuids
		for (uint32_t i = 0; i < jp2->numUuids; ++i) {
			auto uuid = jp2->uuids + i;
			if (memcmp(uuid->uuid, IPTC_UUID, 16)==0) {
				p_image->iptc_buf = uuid->buffer;
				p_image->iptc_len = uuid->len;
				uuid->buffer = nullptr;
				uuid->len = 0;
			}
			else if (memcmp(uuid->uuid, XMP_UUID, 16)==0) {
				p_image->xmp_buf = uuid->buffer;
				p_image->xmp_len = uuid->len;
				uuid->buffer = nullptr;
				uuid->len = 0;
			}
		}
    }

    return true;
}

static bool jp2_write_jp2h(jp2_t *jp2,
                               GrokStream *stream,
                               event_mgr_t * p_manager
                              )
{
    jp2_img_header_writer_handler_t l_writers [32];
    jp2_img_header_writer_handler_t * l_current_writer;

    int32_t i, l_nb_writers=0;
    /* size of data for super box*/
    uint32_t l_jp2h_size = 8;
    bool l_result = true;
 
    assert(stream != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    memset(l_writers,0,sizeof(l_writers));

    if (jp2->bpc == 255) {
        l_writers[l_nb_writers++].handler = jp2_write_ihdr;
        l_writers[l_nb_writers++].handler = jp2_write_bpcc;
        l_writers[l_nb_writers++].handler = jp2_write_colr;
    } else {
        l_writers[l_nb_writers++].handler = jp2_write_ihdr;
        l_writers[l_nb_writers++].handler = jp2_write_colr;
    }

    if (jp2->color.jp2_cdef != nullptr) {
        l_writers[l_nb_writers++].handler = jp2_write_cdef;
    }

	if (jp2->write_display_resolution || jp2->write_capture_resolution) {
		bool storeCapture = jp2->capture_resolution[0] > 0 &&
			jp2->capture_resolution[1] > 0;

		bool storeDisplay = jp2->display_resolution[0] > 0 &&
			jp2->display_resolution[1] > 0;

		if (storeCapture || storeDisplay)
			l_writers[l_nb_writers++].handler = jp2_write_res;
	}
	if (jp2->xml.buffer && jp2->xml.len) {
		l_writers[l_nb_writers++].handler = jp2_write_xml;
	}
	if (jp2->numUuids) {
		l_writers[l_nb_writers++].handler = jp2_write_uuids;
	}

    l_current_writer = l_writers;
    for (i=0; i<l_nb_writers; ++i) {
        l_current_writer->m_data = l_current_writer->handler(jp2,&(l_current_writer->m_size));
        if (l_current_writer->m_data == nullptr) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to hold JP2 Header data\n");
            l_result = false;
            break;
        }

        l_jp2h_size += l_current_writer->m_size;
        ++l_current_writer;
    }

    if (! l_result) {
        l_current_writer = l_writers;
        for (i=0; i<l_nb_writers; ++i) {
            if (l_current_writer->m_data != nullptr) {
                grok_free(l_current_writer->m_data );
            }
            ++l_current_writer;
        }

        return false;
    }

    /* write super box size */
	if (!stream->write_int(l_jp2h_size, p_manager)) {
		event_msg(p_manager, EVT_ERROR, "Stream error while writing JP2 Header box\n");
		l_result = false;
	}
	if (!stream->write_int(JP2_JP2H, p_manager)) {
		event_msg(p_manager, EVT_ERROR, "Stream error while writing JP2 Header box\n");
		l_result = false;
	}

    if (l_result) {
        l_current_writer = l_writers;
        for (i=0; i<l_nb_writers; ++i) {
            if (stream->write_bytes(l_current_writer->m_data,l_current_writer->m_size,p_manager) != l_current_writer->m_size) {
                event_msg(p_manager, EVT_ERROR, "Stream error while writing JP2 Header box\n");
                l_result = false;
                break;
            }
            ++l_current_writer;
        }
    }

    l_current_writer = l_writers;

    /* cleanup */
    for (i=0; i<l_nb_writers; ++i) {
        if (l_current_writer->m_data != nullptr) {
            grok_free(l_current_writer->m_data );
        }
        ++l_current_writer;
    }

    return l_result;
}

static bool jp2_write_ftyp(jp2_t *jp2,
                               GrokStream *cio,
                               event_mgr_t * p_manager )
{
    uint32_t i;
    uint32_t l_ftyp_size = 16 + 4 * jp2->numcl;
	bool l_result = true;
    
    assert(cio != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

	if (!cio->write_int(l_ftyp_size, p_manager)) {
		l_result = false;
		goto end;
	}
	if (!cio->write_int(JP2_FTYP, p_manager)) {
		l_result = false;
		goto end;
	}
	if (!cio->write_int(jp2->brand, p_manager)) {
		l_result = false;
		goto end;
	}
	/* MinV */
	if (!cio->write_int(jp2->minversion, p_manager)) {
		l_result = false;
		goto end;
	}

	/* CL */
    for (i = 0; i < jp2->numcl; i++)  {
		if (!cio->write_int(jp2->cl[i], p_manager)) {
			l_result = false;
			goto end;
		}
    }

end:
	if (!l_result)
		event_msg(p_manager, EVT_ERROR, "Error while writing ftyp data to stream\n");
    return l_result;
}

static bool jp2_write_jp2c(jp2_t *jp2,
                               GrokStream *cio,
                               event_mgr_t * p_manager )
{
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);
    assert(cio->has_seek());

	int64_t j2k_codestream_exit = cio->tell();
	if (!cio->seek(jp2->j2k_codestream_offset, p_manager)) {
		event_msg(p_manager, EVT_ERROR, "Failed to seek in the stream.\n");
		return false;
	}

	/* size of codestream */
	uint32_t length = jp2->needs_xl_jp2c_box_length ? 1 : (uint32_t)(j2k_codestream_exit - jp2->j2k_codestream_offset);
	if (!cio->write_int(length, p_manager)) {
		return false;
	}
	if (!cio->write_int(JP2_JP2C, p_manager)) {
		return false;
	}
	// XL box
	if (length == 1) {
		if (!cio->write_64(j2k_codestream_exit - jp2->j2k_codestream_offset, p_manager)) {
			return false;
		}
	}
    if (! cio->seek(j2k_codestream_exit,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Failed to seek in the stream.\n");
        return false;
    }

    return true;
}

static bool jp2_write_jp(	jp2_t *jp2,
                                GrokStream *cio,
                                event_mgr_t * p_manager )
{
	(void)jp2;
  
    assert(cio != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    /* write box length */
	if (!cio->write_int(12, p_manager))
		return false;
    /* writes box type */
	if (!cio->write_int(JP2_JP, p_manager))
		return false;
    /* writes magic number*/
	if (!cio->write_int(0x0d0a870a, p_manager))
		return false;
    return true;
}

/* ----------------------------------------------------------------------- */
/* JP2 decoder interface                                             */
/* ----------------------------------------------------------------------- */

void jp2_setup_decoder(void *jp2_void, opj_dparameters_t *parameters)
{
	jp2_t *jp2 = (jp2_t*)jp2_void;
    /* setup the J2K codec */
    j2k_setup_decoder(jp2->j2k, parameters);

    /* further JP2 initializations go here */
    jp2->color.jp2_has_colour_specification_box = 0;
    jp2->ignore_pclr_cmap_cdef = parameters->flags & OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG;
}

/* ----------------------------------------------------------------------- */
/* JP2 encoder interface                                             */
/* ----------------------------------------------------------------------- */

bool jp2_setup_encoder(	jp2_t *jp2,
                            opj_cparameters_t *parameters,
                            opj_image_t *image,
                            event_mgr_t * p_manager)
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
    if (j2k_setup_encoder(jp2->j2k, parameters, image, p_manager ) == false) {
        return false;
    }

    /* setup the JP2 codec */
    /* ------------------- */

    /* Profile box */

    jp2->brand = JP2_JP2;	/* BR */
    jp2->minversion = 0;	/* MinV */
    jp2->numcl = 1;
    jp2->cl = (uint32_t*) grok_malloc(jp2->numcl * sizeof(uint32_t));
    if (!jp2->cl) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory when setup the JP2 encoder\n");
        return false;
    }
    jp2->cl[0] = JP2_JP2;	/* CL0 : JP2 */

    /* Image Header box */
    jp2->numcomps = image->numcomps;	/* NC */
    jp2->comps = (jp2_comps_t*) grok_malloc(jp2->numcomps * sizeof(jp2_comps_t));
    if (!jp2->comps) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory when setup the JP2 encoder\n");
        /* Memory of jp2->cl will be freed by jp2_destroy */
        return false;
    }

    jp2->h = image->y1 - image->y0;		/* HEIGHT */
    jp2->w = image->x1 - image->x0;		/* WIDTH */
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
				grok_free(jp2->color.icc_profile_buf);
				jp2->color.icc_profile_buf = nullptr;
			}
			// copy icc profile from image to jp2 struct
			jp2->color.icc_profile_len = image->icc_profile_len;
			jp2->color.icc_profile_buf = (uint8_t*)grok_malloc(jp2->color.icc_profile_len);
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

	//transfer buffer to uuid
	if (image->iptc_len && image->iptc_buf ) {
		jp2->uuids[jp2->numUuids++] = jp2_uuid_t(IPTC_UUID, image->iptc_buf, image->iptc_len,true);
		image->iptc_buf = nullptr;
		image->iptc_len = 0;
	}

	//transfer buffer to uuid
	if (image->xmp_len && image->xmp_buf) {
		jp2->uuids[jp2->numUuids++] = jp2_uuid_t(XMP_UUID, image->xmp_buf, image->xmp_len,true);
		image->xmp_buf = nullptr;
		image->xmp_len = 0;
	}

	
    /* Component Definition box */
    /* FIXME not provided by parameters */
    /* We try to do what we can... */
    alpha_count = 0U;
    for (i = 0; i < image->numcomps; i++) {
        if (image->comps[i].alpha != 0) {
            alpha_count++;
            alpha_channel = i;
        }
    }
	// We can handle a single alpha channel - in this case we assume that alpha applies to the entire image
	// If there are multiple alpha channels, then we don't know how to apply them, so no cdef box
	// gets created in this case
    if (alpha_count == 1U) {
        switch (jp2->enumcs) {
        case 16:
        case 18:
            color_channels = 3;
            break;
        case 17:
            color_channels = 1;
            break;
        default:
			// assume that last channel is alpha
			if (image->numcomps > 1)
				color_channels = image->numcomps-1;
			else
				alpha_count = 0U;
            break;
        }
        if (alpha_count == 0U) {
            event_msg(p_manager, EVT_WARNING, "Alpha channel specified but unknown enumcs. No cdef box will be created.\n");
        } else if (image->numcomps < (color_channels+1)) {
            event_msg(p_manager, EVT_WARNING, "Alpha channel specified but not enough image components for an automatic cdef box creation.\n");
            alpha_count = 0U;
        } else if ((uint32_t)alpha_channel < color_channels) {
            event_msg(p_manager, EVT_WARNING, "Alpha channel position conflicts with color channel. No cdef box will be created.\n");
            alpha_count = 0U;
        }
    } else if (alpha_count > 1) {
        event_msg(p_manager, EVT_WARNING, "Multiple alpha channels specified. No cdef box will be created.\n");
    }
    if (alpha_count == 1U) { /* if here, we know what we can do */
        jp2->color.jp2_cdef = (jp2_cdef_t*)grok_malloc(sizeof(jp2_cdef_t));
        if(!jp2->color.jp2_cdef) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to setup the JP2 encoder\n");
            return false;
        }
        /* no memset needed, all values will be overwritten except if jp2->color.jp2_cdef->info allocation fails, */
        /* in which case jp2->color.jp2_cdef->info will be nullptr => valid for destruction */
        jp2->color.jp2_cdef->info = (jp2_cdef_info_t*) grok_malloc(image->numcomps * sizeof(jp2_cdef_info_t));
        if (!jp2->color.jp2_cdef->info) {
            /* memory will be freed by jp2_destroy */
            event_msg(p_manager, EVT_ERROR, "Not enough memory to setup the JP2 encoder\n");
            return false;
        }
        jp2->color.jp2_cdef->n = (uint16_t) image->numcomps; /* cast is valid : image->numcomps [1,16384] */
        for (i = 0U; i < color_channels; i++) {
            jp2->color.jp2_cdef->info[i].cn = (uint16_t)i; /* cast is valid : image->numcomps [1,16384] */
            jp2->color.jp2_cdef->info[i].typ = 0U;
            jp2->color.jp2_cdef->info[i].asoc = (uint16_t)(i+1U); /* No overflow + cast is valid : image->numcomps [1,16384] */
        }
        for (; i < image->numcomps; i++) {
            if (image->comps[i].alpha) { 
                jp2->color.jp2_cdef->info[i].cn = (uint16_t)i; /* cast is valid : image->numcomps [1,16384] */
                jp2->color.jp2_cdef->info[i].typ = image->comps[i].alpha; /* Opacity channel */
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
		for (i = 0; i < 2; ++i) {
			jp2->capture_resolution[i] = parameters->capture_resolution[i];
		}
	}

	if (parameters->write_display_resolution) {
		jp2->write_display_resolution = true;
		for (i = 0; i < 2; ++i) {
			jp2->display_resolution[i] = parameters->display_resolution[i];
		}
	}
    return true;
}

bool jp2_encode(jp2_t *jp2,
					grok_plugin_tile_t* tile,
                    GrokStream *stream,
                    event_mgr_t * p_manager)
{
    return j2k_encode(jp2->j2k, tile,stream, p_manager);
}

bool jp2_end_decompress(jp2_t *jp2,
                            GrokStream *cio,
                            event_mgr_t * p_manager
                           )
{
    
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);

    /* customization of the end encoding */
    if (! jp2_setup_end_header_reading(jp2, p_manager)) {
        return false;
    }

    /* write header */
    if (! jp2_exec (jp2,jp2->m_procedure_list,cio,p_manager)) {
        return false;
    }

    return j2k_end_decompress(jp2->j2k, cio, p_manager);
}

bool jp2_end_compress(	jp2_t *jp2,
                            GrokStream *cio,
                            event_mgr_t * p_manager
                         )
{
    
    assert(jp2 != nullptr);
    assert(cio != nullptr);
    assert(p_manager != nullptr);

    /* customization of the end encoding */
    if (! jp2_setup_end_header_writing(jp2, p_manager)) {
        return false;
    }

    if (! j2k_end_compress(jp2->j2k,cio,p_manager)) {
        return false;
    }

    /* write header */
    return jp2_exec(jp2,jp2->m_procedure_list,cio,p_manager);
}

static bool jp2_setup_end_header_writing (jp2_t *jp2, event_mgr_t * p_manager)
{
    
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_write_jp2c, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom procedures */
    return true;
}

static bool jp2_setup_end_header_reading (jp2_t *jp2, event_mgr_t * p_manager)
{
    
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_read_header_procedure, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom procedures */

    return true;
}

static bool jp2_default_validation (	jp2_t * jp2,
        GrokStream *cio,
        event_mgr_t * p_manager
                                       )
{
	(void)p_manager;
    bool l_is_valid = true;
    uint32_t i;

    
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
    /* precision */
    for (i = 0; i < jp2->numcomps; ++i)	{
        l_is_valid &= ((jp2->comps[i].bpcc & 0x7FU) < 38U); /* 0 is valid, ignore sign for check */
    }

    /* METH */
    l_is_valid &= ((jp2->meth > 0) && (jp2->meth < 3));

    /* stream validation */
    /* back and forth is needed */
    l_is_valid &= cio->has_seek();

    return l_is_valid;
}

static bool jp2_read_header_procedure(  jp2_t *jp2,
        GrokStream *stream,
        event_mgr_t * p_manager
                                         )
{
    jp2_box_t box;
    uint32_t l_nb_bytes_read;
    const jp2_header_handler_t * l_current_handler;
    const jp2_header_handler_t * l_current_handler_misplaced;
    uint32_t l_last_data_size = OPJ_BOX_SIZE;
    uint32_t l_current_data_size;
    uint8_t * l_current_data = nullptr;

    
    assert(stream != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    l_current_data = (uint8_t*)grok_calloc(1,l_last_data_size);

    if (l_current_data == nullptr) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to handle jpeg2000 file header\n");
        return false;
    }

    while (jp2_read_boxhdr(&box,&l_nb_bytes_read,stream,p_manager)) {
        /* is it the codestream box ? */
        if (box.type == JP2_JP2C) {
            if (jp2->jp2_state & JP2_STATE_HEADER) {
                jp2->jp2_state |= JP2_STATE_CODESTREAM;
                grok_free(l_current_data);
                return true;
            } else {
                event_msg(p_manager, EVT_ERROR, "bad placed jpeg codestream\n");
                grok_free(l_current_data);
                return false;
            }
        } else if	(box.length == 0) {
            event_msg(p_manager, EVT_ERROR, "Cannot handle box of undefined sizes\n");
            grok_free(l_current_data);
            return false;
        }
        /* testcase 1851.pdf.SIGSEGV.ce9.948 */
        else if (box.length < l_nb_bytes_read) {
            event_msg(p_manager, EVT_ERROR, "invalid box size %d (%x)\n", box.length, box.type);
            grok_free(l_current_data);
            return false;
        }

        l_current_handler = jp2_find_handler(box.type);
        l_current_handler_misplaced = jp2_img_find_handler(box.type);
        l_current_data_size = (uint32_t)(box.length - l_nb_bytes_read);

        if ((l_current_handler != nullptr) || (l_current_handler_misplaced != nullptr)) {
            if (l_current_handler == nullptr) {
                event_msg(p_manager, EVT_WARNING, "Found a misplaced '%c%c%c%c' box outside jp2h box\n", (uint8_t)(box.type>>24), (uint8_t)(box.type>>16), (uint8_t)(box.type>>8), (uint8_t)(box.type>>0));
                if (jp2->jp2_state & JP2_STATE_HEADER) {
                    /* read anyway, we already have jp2h */
                    l_current_handler = l_current_handler_misplaced;
                } else {
                    event_msg(p_manager, EVT_WARNING, "JPEG2000 Header box not read yet, '%c%c%c%c' box will be ignored\n", (uint8_t)(box.type>>24), (uint8_t)(box.type>>16), (uint8_t)(box.type>>8), (uint8_t)(box.type>>0));
                    jp2->jp2_state |= JP2_STATE_UNKNOWN;
                    if (!stream->skip(l_current_data_size,p_manager)) {
						event_msg(p_manager, EVT_WARNING,
							"Problem with skipping JPEG2000 box, stream error\n");
						grok_free(l_current_data);
						// ignore error and return true if code stream box has already been read
						// (we don't worry about any boxes after code stream)
                        return jp2->jp2_state & JP2_STATE_CODESTREAM ? true : false;
                    }
                    continue;
                }
            }
            if ((int64_t)l_current_data_size > stream->get_number_byte_left()) {
                /* do not even try to malloc if we can't read */
                event_msg(p_manager, EVT_ERROR, "Invalid box size %d for box '%c%c%c%c'. Need %d bytes, %d bytes remaining \n", box.length, (uint8_t)(box.type>>24), (uint8_t)(box.type>>16), (uint8_t)(box.type>>8), (uint8_t)(box.type>>0), l_current_data_size, (uint32_t)stream->get_number_byte_left());
                grok_free(l_current_data);
                return false;
            }
            if (l_current_data_size > l_last_data_size) {
                uint8_t* new_current_data = (uint8_t*)grok_realloc(l_current_data,l_current_data_size);
                if (!new_current_data) {
                    grok_free(l_current_data);
                    event_msg(p_manager, EVT_ERROR, "Not enough memory to handle jpeg2000 box\n");
                    return false;
                }
                l_current_data = new_current_data;
                l_last_data_size = l_current_data_size;
            }

            l_nb_bytes_read = (uint32_t)stream->read(l_current_data,l_current_data_size,p_manager);
            if (l_nb_bytes_read != l_current_data_size) {
                event_msg(p_manager, EVT_ERROR, "Problem with reading JPEG2000 box, stream error\n");
                grok_free(l_current_data);
                return false;
            }

            if (! l_current_handler->handler(jp2,l_current_data,l_current_data_size,p_manager)) {
                grok_free(l_current_data);
                return false;
            }
        } else {
            if (!(jp2->jp2_state & JP2_STATE_SIGNATURE)) {
                event_msg(p_manager, EVT_ERROR, "Malformed JP2 file format: first box must be JPEG 2000 signature box\n");
                grok_free(l_current_data);
                return false;
            }
            if (!(jp2->jp2_state & JP2_STATE_FILE_TYPE)) {
                event_msg(p_manager, EVT_ERROR, "Malformed JP2 file format: second box must be file type box\n");
                grok_free(l_current_data);
                return false;
            }
            jp2->jp2_state |= JP2_STATE_UNKNOWN;
            if (!stream->skip(l_current_data_size,p_manager)) {
				event_msg(p_manager, EVT_WARNING,
					"Problem with skipping JPEG2000 box, stream error\n");
				grok_free(l_current_data);
				// ignore error and return true if code stream box has already been read
				// (we don't worry about any boxes after code stream)
				return jp2->jp2_state & JP2_STATE_CODESTREAM ? true : false;
            }
        }
    }

    grok_free(l_current_data);

    return true;
}

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
static bool jp2_exec (  jp2_t * jp2,
                            procedure_list_t * p_procedure_list,
                            GrokStream *stream,
                            event_mgr_t * p_manager
                         )

{
    bool (** l_procedure) (jp2_t * jp2, GrokStream *, event_mgr_t *) = nullptr;
    bool l_result = true;
    uint32_t l_nb_proc, i;

    
    assert(p_procedure_list != nullptr);
    assert(jp2 != nullptr);
    assert(stream != nullptr);
    assert(p_manager != nullptr);

    l_nb_proc = procedure_list_get_nb_procedures(p_procedure_list);
    l_procedure = (bool (**) (jp2_t * jp2, GrokStream *, event_mgr_t *)) procedure_list_get_first_procedure(p_procedure_list);

    for	(i=0; i<l_nb_proc; ++i) {
        l_result = l_result && (*l_procedure) (jp2,stream,p_manager);
        ++l_procedure;
    }

    /* and clear the procedure list at the end. */
    procedure_list_clear(p_procedure_list);
    return l_result;
}

bool jp2_start_compress(jp2_t *jp2,
                            GrokStream *stream,
                            opj_image_t * p_image,
                            event_mgr_t * p_manager
                           )
{
	if (!p_image)
		return false;
    assert(jp2 != nullptr);
    assert(stream != nullptr);
    assert(p_manager != nullptr);

    /* customization of the validation */
    if (! jp2_setup_encoding_validation (jp2, p_manager)) {
        return false;
    }

    /* validation of the parameters codec */
    if (! jp2_exec(jp2,jp2->m_validation_list,stream,p_manager)) {
        return false;
    }

    /* customization of the encoding */
    if (! jp2_setup_header_writing(jp2, p_manager)) {
        return false;
    }

	uint64_t image_size = 0;
	for (auto i = 0U; i < p_image->numcomps; ++i) {
		auto comp = p_image->comps + i;
		image_size += (uint64_t)((comp->w * comp->h) * (comp->prec/8.0));
	}
	jp2->needs_xl_jp2c_box_length = (image_size > (uint64_t)1 << 30) ? true : false;

    /* write header */
    if (! jp2_exec (jp2,jp2->m_procedure_list,stream,p_manager)) {
        return false;
    }

    return j2k_start_compress(jp2->j2k,stream,p_image,p_manager);
}

static const jp2_header_handler_t * jp2_find_handler (uint32_t p_id)
{
    auto l_handler_size = sizeof(jp2_header) / sizeof(jp2_header_handler_t);
    for (uint32_t i=0; i<l_handler_size; ++i) {
        if (jp2_header[i].id == p_id) {
            return &jp2_header[i];
        }
    }
    return nullptr;
}

/**
 * Finds the image execution function related to the given box id.
 *
 * @param	p_id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const jp2_header_handler_t * jp2_img_find_handler (uint32_t p_id)
{
    auto l_handler_size = sizeof(jp2_img_header) / sizeof(jp2_header_handler_t);
    for (uint32_t i=0; i<l_handler_size; ++i) {
        if (jp2_img_header[i].id == p_id) {
            return &jp2_img_header[i];
        }
    }

    return nullptr;
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
static bool jp2_read_jp(jp2_t *jp2,
                            uint8_t * p_header_data,
                            uint32_t p_header_size,
                            event_mgr_t * p_manager
                           )

{
    uint32_t l_magic_number;

    
    assert(p_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (jp2->jp2_state != JP2_STATE_NONE) {
        event_msg(p_manager, EVT_ERROR, "The signature box must be the first box in the file.\n");
        return false;
    }

    /* assure length of data is correct (4 -> magic number) */
    if (p_header_size != 4) {
        event_msg(p_manager, EVT_ERROR, "Error with JP signature Box size\n");
        return false;
    }

    /* rearrange data */
    grok_read_bytes(p_header_data,&l_magic_number,4);
    if (l_magic_number != 0x0d0a870a ) {
        event_msg(p_manager, EVT_ERROR, "Error with JP Signature : bad magic number\n");
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
static bool jp2_read_ftyp(	jp2_t *jp2,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    uint32_t i, l_remaining_bytes;

    
    assert(p_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (jp2->jp2_state != JP2_STATE_SIGNATURE) {
        event_msg(p_manager, EVT_ERROR, "The ftyp box must be the second box in the file.\n");
        return false;
    }

    /* assure length of data is correct */
    if (p_header_size < 8) {
        event_msg(p_manager, EVT_ERROR, "Error with FTYP signature Box size\n");
        return false;
    }

    grok_read_bytes(p_header_data,&jp2->brand,4);		/* BR */
    p_header_data += 4;

    grok_read_bytes(p_header_data,&jp2->minversion,4);		/* MinV */
    p_header_data += 4;

    l_remaining_bytes = p_header_size - 8;

    /* the number of remaining bytes should be a multiple of 4 */
    if ((l_remaining_bytes & 0x3) != 0) {
        event_msg(p_manager, EVT_ERROR, "Error with FTYP signature Box size\n");
        return false;
    }

    /* div by 4 */
    jp2->numcl = l_remaining_bytes >> 2;
    if (jp2->numcl) {
        jp2->cl = (uint32_t *) grok_calloc(jp2->numcl, sizeof(uint32_t));
        if (jp2->cl == nullptr) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory with FTYP Box\n");
            return false;
        }
    }

    for (i = 0; i < jp2->numcl; ++i) {
        grok_read_bytes(p_header_data,&jp2->cl[i],4);		/* CLi */
        p_header_data += 4;
    }

    jp2->jp2_state |= JP2_STATE_FILE_TYPE;

    return true;
}

static bool jp2_skip_jp2c(	jp2_t *jp2,
                                GrokStream *stream,
                                event_mgr_t * p_manager )
{
    
    assert(jp2 != nullptr);
    assert(stream != nullptr);
    assert(p_manager != nullptr);

    jp2->j2k_codestream_offset = stream->tell();

	int64_t skip_bytes = jp2->needs_xl_jp2c_box_length ? 16 : 8;
    if (!stream->skip(skip_bytes,p_manager)) {
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
static bool jp2_read_jp2h(  jp2_t *jp2,
                                uint8_t *p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    uint32_t l_box_size=0, l_current_data_size = 0;
    jp2_box_t box;
    const jp2_header_handler_t * l_current_handler;
    bool l_has_ihdr = 0;

    
    assert(p_header_data != nullptr);
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    /* make sure the box is well placed */
    if ((jp2->jp2_state & JP2_STATE_FILE_TYPE) != JP2_STATE_FILE_TYPE ) {
        event_msg(p_manager, EVT_ERROR, "The  box must be the first box in the file.\n");
        return false;
    }

    jp2->jp2_img_state = JP2_IMG_STATE_NONE;

	int64_t header_size = p_header_size;
    /* iterate while remaining data */
    while (header_size > 0) {

        if (! jp2_read_boxhdr_char(&box,p_header_data,&l_box_size,header_size, p_manager)) {
            event_msg(p_manager, EVT_ERROR, "Stream error while reading JP2 Header box\n");
            return false;
        }

        if (box.length > (uint64_t)header_size) {
            event_msg(p_manager, EVT_ERROR, "Stream error while reading JP2 Header box: box length is inconsistent.\n");
            return false;
        }

        l_current_handler = jp2_img_find_handler(box.type);
        l_current_data_size = (uint32_t)(box.length - l_box_size);
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
			event_msg(p_manager, EVT_ERROR, "Error reading JP2 header box\n");
			return false;
		}
    }

    if (l_has_ihdr == 0) {
        event_msg(p_manager, EVT_ERROR, "Stream error while reading JP2 Header box: no 'ihdr' box.\n");
        return false;
    }

    jp2->jp2_state |= JP2_STATE_HEADER;

    return true;
}

static bool jp2_read_boxhdr_char(   jp2_box_t *box,
                                        uint8_t * p_data,
                                        uint32_t * p_number_bytes_read,
                                        int64_t p_box_max_size,
                                        event_mgr_t * p_manager
                                    )
{
    assert(p_data != nullptr);
    assert(box != nullptr);
    assert(p_number_bytes_read != nullptr);
    assert(p_manager != nullptr);

    if (p_box_max_size < 8) {
        event_msg(p_manager, EVT_ERROR, "Cannot handle box of less than 8 bytes\n");
        return false;
    }

    /* process read data */
	uint32_t L = 0;
    grok_read_bytes(p_data, &L, 4);
	box->length = L;
    p_data += 4;

    grok_read_bytes(p_data, &box->type, 4);
    p_data += 4;

	*p_number_bytes_read = 8;

    /* read XL parameter */
    if (box->length == 1) {
        if (p_box_max_size < 16) {
            event_msg(p_manager, EVT_ERROR, "Cannot handle XL box of less than 16 bytes\n");
            return false;
        }

		grok_read_64(p_data,&box->length,8);
        p_data += 8;
        *p_number_bytes_read += 8;

        if (box->length == 0) {
            event_msg(p_manager, EVT_ERROR, "Cannot handle box of undefined sizes\n");
            return false;
        }
    } else if (box->length == 0) {
        event_msg(p_manager, EVT_ERROR, "Cannot handle box of undefined sizes\n");
        return false;
    }
    if (box->length < *p_number_bytes_read) {
        event_msg(p_manager, EVT_ERROR, "Box length is inconsistent.\n");
        return false;
    }
    return true;
}

bool jp2_read_header(	GrokStream *p_stream,
                            jp2_t *jp2,
							opj_header_info_t* header_info,
                            opj_image_t ** p_image,
                            event_mgr_t * p_manager
                        )
{
    
    assert(jp2 != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    /* customization of the validation */
    if (! jp2_setup_decoding_validation (jp2, p_manager)) {
        return false;
    }

    /* customization of the encoding */
    if (! jp2_setup_header_reading(jp2, p_manager)) {
        return false;
    }

    /* validation of the parameters codec */
    if (! jp2_exec(jp2,jp2->m_validation_list,p_stream,p_manager)) {
        return false;
    }

    /* read header */
    if (! jp2_exec (jp2,jp2->m_procedure_list,p_stream,p_manager)) {
        return false;
    }

	if (header_info) {
		header_info->enumcs = jp2->enumcs;
		header_info->color = jp2->color;

		header_info->xml_data		= jp2->xml.buffer;
		header_info->xml_data_len	= jp2->xml.len;
	}

    bool rc =  j2k_read_header(	p_stream,
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

static bool jp2_setup_encoding_validation (jp2_t *jp2, event_mgr_t * p_manager)
{
    
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(jp2->m_validation_list, (procedure)jp2_default_validation, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom validation procedure */

    return true;
}

static bool jp2_setup_decoding_validation (jp2_t *jp2, event_mgr_t * p_manager)
{
	(void)jp2;
	(void)p_manager;
    
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    /* DEVELOPER CORNER, add your custom validation procedure */

    return true;
}

static bool jp2_setup_header_writing (jp2_t *jp2, event_mgr_t * p_manager)
{
    
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_write_jp, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_write_ftyp, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_write_jp2h, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_skip_jp2c,p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, insert your custom procedures */

    return true;
}

static bool jp2_setup_header_reading (jp2_t *jp2, event_mgr_t * p_manager)
{
    
    assert(jp2 != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(jp2->m_procedure_list,(procedure)jp2_read_header_procedure, p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, add your custom procedures */

    return true;
}

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
                                event_mgr_t * p_manager
                              )
{
    return j2k_read_tile_header(p_jp2->j2k,
                                    p_tile_index,
                                    p_data_size,
                                    p_tile_x0, p_tile_y0,
                                    p_tile_x1, p_tile_y1,
                                    p_nb_comps,
                                    p_go_on,
                                    p_stream,
                                    p_manager);
}

bool jp2_write_tile (	jp2_t *p_jp2,
                            uint32_t p_tile_index,
                            uint8_t * p_data,
                            uint64_t p_data_size,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager
                        )

{
    return j2k_write_tile (p_jp2->j2k,p_tile_index,p_data,p_data_size,p_stream,p_manager);
}

bool jp2_decode_tile (  jp2_t * p_jp2,
                            uint32_t p_tile_index,
                            uint8_t * p_data,
                            uint64_t p_data_size,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager
                         )
{
	bool rc = false;
	try {
		rc = j2k_decode_tile(p_jp2->j2k, p_tile_index, p_data, p_data_size, p_stream, p_manager);
	}
	catch (DecodeUnknownMarkerAtEndOfTileException e) {
		//suppress exception
	}
	return rc;
}

void jp2_destroy(jp2_t *jp2)
{
    if (jp2) {
        /* destroy the J2K codec */
        j2k_destroy(jp2->j2k);
        jp2->j2k = nullptr;

        if (jp2->comps) {
            grok_free(jp2->comps);
            jp2->comps = nullptr;
        }

        if (jp2->cl) {
            grok_free(jp2->cl);
            jp2->cl = nullptr;
        }

        if (jp2->color.icc_profile_buf) {
            grok_free(jp2->color.icc_profile_buf);
            jp2->color.icc_profile_buf = nullptr;
        }

        if (jp2->color.jp2_cdef) {
            if (jp2->color.jp2_cdef->info) {
                grok_free(jp2->color.jp2_cdef->info);
                jp2->color.jp2_cdef->info = nullptr;
            }

            grok_free(jp2->color.jp2_cdef);
            jp2->color.jp2_cdef = nullptr;
        }

		jp2_free_pclr(&jp2->color);

        if (jp2->m_validation_list) {
            procedure_list_destroy(jp2->m_validation_list);
            jp2->m_validation_list = nullptr;
        }

        if (jp2->m_procedure_list) {
            procedure_list_destroy(jp2->m_procedure_list);
            jp2->m_procedure_list = nullptr;
        }

		jp2->xml.dealloc();

		for (uint32_t i = 0; i < jp2->numUuids; ++i) {
			(jp2->uuids + i)->dealloc();
		}
		jp2->numUuids = 0;

        grok_free(jp2);
    }
}

bool jp2_set_decode_area(	jp2_t *p_jp2,
                                opj_image_t* p_image,
                                uint32_t p_start_x, uint32_t p_start_y,
                                uint32_t p_end_x, uint32_t p_end_y,
                                event_mgr_t * p_manager
                            )
{
    return j2k_set_decode_area(p_jp2->j2k, p_image, p_start_x, p_start_y, p_end_x, p_end_y, p_manager);
}

bool jp2_get_tile(	jp2_t *p_jp2,
                        GrokStream *p_stream,
                        opj_image_t* p_image,
                        event_mgr_t * p_manager,
                        uint32_t tile_index
                     )
{
    if (!p_image)
        return false;

    event_msg(p_manager, EVT_WARNING, "JP2 box which are after the codestream will not be read by this function.\n");

    if (! j2k_get_tile(p_jp2->j2k, p_stream, p_image, p_manager, tile_index) ) {
        event_msg(p_manager, EVT_ERROR, "Failed to decode the codestream in the JP2 file\n");
        return false;
    }

    if (!jp2_check_color(p_image, &(p_jp2->color), p_manager)) {
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
            jp2_free_pclr(&(p_jp2->color));
		else {
			if (!jp2_apply_pclr(p_image, &(p_jp2->color), p_manager)) 
				return false;
		}
    }

	/* Apply channel definitions if needed */
    if(p_jp2->color.jp2_cdef) {
        jp2_apply_cdef(p_image, &(p_jp2->color), p_manager);
    }

    if(p_jp2->color.icc_profile_buf) {
        p_image->icc_profile_buf = p_jp2->color.icc_profile_buf;
        p_image->icc_profile_len = p_jp2->color.icc_profile_len;
        p_jp2->color.icc_profile_buf = nullptr;
		p_jp2->color.icc_profile_len = 0;
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/* JP2 encoder interface                                             */
/* ----------------------------------------------------------------------- */

jp2_t* jp2_create(bool p_is_decoder)
{
    jp2_t *jp2 = (jp2_t*)grok_calloc(1,sizeof(jp2_t));
    if (jp2) {

        /* create the J2K codec */
        if (! p_is_decoder) {
            jp2->j2k = j2k_create_compress();
        } else {
            jp2->j2k = j2k_create_decompress();
        }

        if (jp2->j2k == nullptr) {
            jp2_destroy(jp2);
            return nullptr;
        }

        /* Color structure */
        jp2->color.icc_profile_buf = nullptr;
        jp2->color.icc_profile_len = 0;
        jp2->color.jp2_cdef = nullptr;
        jp2->color.jp2_pclr = nullptr;
        jp2->color.jp2_has_colour_specification_box = 0;

        /* validation list creation */
        jp2->m_validation_list = procedure_list_create();
        if (! jp2->m_validation_list) {
            jp2_destroy(jp2);
            return nullptr;
        }

        /* execution list creation */
        jp2->m_procedure_list = procedure_list_create();
        if (! jp2->m_procedure_list) {
            jp2_destroy(jp2);
            return nullptr;
        }
    }

    return jp2;
}

void jp2_dump(jp2_t* p_jp2, int32_t flag, FILE* out_stream)
{
    
    assert(p_jp2 != nullptr);

    j2k_dump(p_jp2->j2k,
             flag,
             out_stream);
}

opj_codestream_index_t* jp2_get_cstr_index(jp2_t* p_jp2)
{
    return j2k_get_cstr_index(p_jp2->j2k);
}

opj_codestream_info_v2_t* jp2_get_cstr_info(jp2_t* p_jp2)
{
    return j2k_get_cstr_info(p_jp2->j2k);
}

bool jp2_set_decoded_resolution_factor(jp2_t *p_jp2,
        uint32_t res_factor,
        event_mgr_t * p_manager)
{
    return j2k_set_decoded_resolution_factor(p_jp2->j2k, res_factor, p_manager);
}


}
