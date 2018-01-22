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
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2010-2011, Kaori Hagihara
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
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

// Broadcast and IMF profile constants
const uint16_t maxMainLevel = 11;
const uint16_t maxSubLevel = 9;

/** @defgroup J2K J2K - JPEG-2000 codestream reader/writer */
/*@{*/

tcp_t::tcp_t() : csty(0),
						 prg(OPJ_PROG_UNKNOWN),
						 numlayers(0),
						num_layers_to_decode(0),
						mct(0),
						numpocs(0),
						ppt_markers_count(0),
						ppt_markers(nullptr),
						ppt_data(nullptr),
						ppt_buffer(nullptr),
						ppt_data_size(0),
						ppt_len(0),
						tccps(nullptr),
						m_current_tile_part_number(-1),
						m_nb_tile_parts(0),
						m_tile_data(nullptr),
						mct_norms(nullptr),
						m_mct_decoding_matrix(nullptr),
						m_mct_coding_matrix(nullptr),
						m_mct_records(nullptr),
						m_nb_mct_records(0),
						m_nb_max_mct_records(0),
						m_mcc_records(nullptr),
						m_nb_mcc_records(0),
						m_nb_max_mcc_records(0),
						cod(0),
						ppt(0),
						POC(0)

{

	for (auto i = 0; i < 100; ++i)
		rates[i]=0;

	for (auto i = 0; i < 100; ++i)
		distoratio[i] = 0;

	for (auto i = 0; i < 32; ++i)
		memset(pocs + i, 0, sizeof(opj_poc_t));

}

/** @name Local static functions */
/*@{*/

/**
Transfer data from src to dest for each component, and null out src data.
Assumption:  src and dest have the same number of components
*/
static void j2k_transfer_image_data(opj_image_t* src, opj_image_t* dest)
{
    uint32_t compno;
    if (!src || !dest || !src->comps || !dest->comps || src->numcomps != dest->numcomps)
        return;

    for (compno = 0; compno < src->numcomps; compno++) {
        opj_image_comp_t* src_comp = src->comps + compno;
        opj_image_comp_t* dest_comp = dest->comps + compno;
        dest_comp->resno_decoded = src_comp->resno_decoded;
        opj_image_single_component_data_free(dest_comp);
        dest_comp->data = src_comp->data;
		dest_comp->owns_data = src_comp->owns_data;
        src_comp->data = nullptr;
    }
}

/**
 * Sets up the procedures to do on reading header. Developers wanting to extend the library can add their own reading procedures.
 */
static bool j2k_setup_header_reading (j2k_t *p_j2k, event_mgr_t * p_manager);

/**
 * The read header procedure.
 */
static bool j2k_read_header_procedure(  j2k_t *p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager);

/**
 * The default encoding validation procedure without any extension.
 *
 * @param       p_j2k                   the jpeg2000 codec to validate.
 * @param       p_stream                the input stream to validate.
 * @param       p_manager               the user event manager.
 *
 * @return true if the parameters are correct.
 */
static bool j2k_encoding_validation (   j2k_t * p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * The default decoding validation procedure without any extension.
 *
 * @param       p_j2k                   the jpeg2000 codec to validate.
 * @param       p_stream                                the input stream to validate.
 * @param       p_manager               the user event manager.
 *
 * @return true if the parameters are correct.
 */
static bool j2k_decoding_validation (   j2k_t * p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_setup_encoding_validation (j2k_t *p_j2k, event_mgr_t * p_manager);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_setup_decoding_validation (j2k_t *p_j2k, event_mgr_t * p_manager);

/**
 * Sets up the validation ,i.e. adds the procedures to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_setup_end_compress (j2k_t *p_j2k, event_mgr_t * p_manager);

/**
 * The mct encoding validation procedure.
 *
 * @param       p_j2k                   the jpeg2000 codec to validate.
 * @param       p_stream                                the input stream to validate.
 * @param       p_manager               the user event manager.
 *
 * @return true if the parameters are correct.
 */
static bool j2k_mct_validation (j2k_t * p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Builds the tcd decoder to use to decode tile.
 */
static bool j2k_build_decoder ( j2k_t * p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );
/**
 * Builds the tcd encoder to use to encode tile.
 */
static bool j2k_build_encoder ( j2k_t * p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Creates a tile-coder decoder.
 *
 * @param       p_stream                        the stream to write data to.
 * @param       p_j2k                           J2K codec.
 * @param       p_manager                   the user event manager.
*/
static bool j2k_create_tcd(     j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Executes the given procedures on the given codec.
 *
 * @param       p_procedure_list        the list of procedures to execute
 * @param       p_j2k                           the jpeg2000 codec to execute the procedures on.
 * @param       p_stream                        the stream to execute the procedures on.
 * @param       p_manager                       the user manager.
 *
 * @return      true                            if all the procedures were successfully executed.
 */
static bool j2k_exec (  j2k_t * p_j2k,
                            procedure_list_t * p_procedure_list,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager);

/**
 * Updates the rates of the tcp.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_update_rates(   j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Copies the decoding tile parameters onto all the tile parameters.
 * Creates also the tile decoder.
 */
static bool j2k_copy_default_tcp_and_create_tcd (       j2k_t * p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager );


/**
 * Reads the lookup table containing all the marker, status and action, and returns the handler associated
 * with the marker value.
 * @param       p_id            Marker value to look up
 *
 * @return      the handler associated with the id.
*/
static const struct opj_dec_memory_marker_handler * j2k_get_marker_handler (uint32_t p_id);

/**
 * Destroys a tile coding parameter structure.
 *
 * @param       p_tcp           the tile coding parameter to destroy.
 */
static void j2k_tcp_destroy (tcp_t *p_tcp);

/**
 * Destroys the data inside a tile coding parameter structure.
 *
 * @param       p_tcp           the tile coding parameter which contain data to destroy.
 */
static void j2k_tcp_data_destroy (tcp_t *p_tcp);

/**
 * Destroys a coding parameter structure.
 *
 * @param       p_cp            the coding parameter to destroy.
 */
static void j2k_cp_destroy (cp_t *p_cp);

/**
 * Compare 2 a SPCod/ SPCoc elements, i.e. the coding style of a given component of a tile.
 *
 * @param       p_j2k            J2K codec.
 * @param       p_tile_no        Tile number
 * @param       p_first_comp_no  The 1st component number to compare.
 * @param       p_second_comp_no The 1st component number to compare.
 *
 * @return true if SPCdod are equals.
 */
static bool j2k_compare_SPCod_SPCoc(j2k_t *p_j2k, uint32_t p_tile_no, uint32_t p_first_comp_no, uint32_t p_second_comp_no);

/**
 * Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 *
 * @param       p_j2k           J2K codec.
 * @param       p_tile_no       FIXME DOC
 * @param       p_comp_no       the component number to output.
 * @param       p_data          FIXME DOC
 * @param       p_header_size   FIXME DOC
 * @param       p_manager       the user event manager.
 *
 * @return FIXME DOC
*/
static bool j2k_write_SPCod_SPCoc(      j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no,
		GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * Gets the size taken by writing a SPCod or SPCoc for the given tile and component.
 *
 * @param       p_j2k                   the J2K codec.
 * @param       p_tile_no               the tile index.
 * @param       p_comp_no               the component being outputted.
 *
 * @return      the number of bytes taken by the SPCod element.
 */
static uint32_t j2k_get_SPCod_SPCoc_size (j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no );

/**
 * Reads a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 * @param       p_j2k           the jpeg2000 codec.
 * @param       compno          FIXME DOC
 * @param       p_header_data   the data contained in the COM box.
 * @param       p_header_size   the size of the data contained in the COM marker.
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_SPCod_SPCoc(   j2k_t *p_j2k,
                                        uint32_t compno,
                                        uint8_t * p_header_data,
                                        uint32_t * p_header_size,
                                        event_mgr_t * p_manager );

/**
 * Gets the size taken by writing SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_tile_no               the tile index.
 * @param       p_comp_no               the component being outputted.
 * @param       p_j2k                   the J2K codec.
 *
 * @return      the number of bytes taken by the SPCod element.
 */
static uint32_t j2k_get_SQcd_SQcc_size (  j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no );

/**
 * Compares 2 SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_tile_no               the tile to output.
 * @param       p_first_comp_no         the first component number to compare.
 * @param       p_second_comp_no        the second component number to compare.
 *
 * @return true if equals.
 */
static bool j2k_compare_SQcd_SQcc(j2k_t *p_j2k, uint32_t p_tile_no, uint32_t p_first_comp_no, uint32_t p_second_comp_no);


/**
 * Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_tile_no               the tile to output.
 * @param       p_comp_no               the component number to output.
 * @param       p_data                  the data buffer.
 * @param       p_header_size   pointer to the size of the data buffer, it is changed by the function.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
 *
*/
static bool j2k_write_SQcd_SQcc(j2k_t *p_j2k,
                                    uint32_t p_tile_no,
                                    uint32_t p_comp_no,
									GrokStream *p_stream,
                                    event_mgr_t * p_manager);

/**
 * Updates the Tile Length Marker.
 */
static void j2k_update_tlm ( j2k_t * p_j2k, uint32_t p_tile_part_size);

/**
 * Reads a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       p_j2k           J2K codec.
 * @param       compno          the component number to output.
 * @param       p_header_data   the data buffer.
 * @param       p_header_size   pointer to the size of the data buffer, it is changed by the function.
 * @param       p_manager       the user event manager.
 *
*/
static bool j2k_read_SQcd_SQcc( bool isQCD,
									j2k_t *p_j2k,
                                    uint32_t compno,
                                    uint8_t * p_header_data,
                                    uint32_t * p_header_size,
                                    event_mgr_t * p_manager );

/**
 * Copies the tile component parameters of all the component from the first tile component.
 *
 * @param               p_j2k           the J2k codec.
 */
static void j2k_copy_tile_component_parameters( j2k_t *p_j2k );

/**
 * Copies the tile quantization parameters of all the component from the first tile component.
 *
 * @param               p_j2k           the J2k codec.
 */
static void j2k_copy_tile_quantization_parameters( j2k_t *p_j2k );

/**
 * Reads the tiles.
 */
static bool j2k_decode_tiles (  j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager);

static bool j2k_pre_write_tile ( j2k_t * p_j2k,
                                     uint32_t p_tile_index,
                                     event_mgr_t * p_manager );

static bool j2k_copy_decoded_tile_to_output_image (tcd_t * p_tcd,
										uint8_t * p_data,
										opj_image_t* p_output_image,
										bool clearOutputOnInit,
										event_mgr_t * p_manager);

static void get_tile_dimensions(opj_image_t * l_image,
                                    tcd_tilecomp_t * l_tilec,
                                    opj_image_comp_t * l_img_comp,
                                    uint32_t* l_size_comp,
                                    uint32_t* l_width,
                                    uint32_t* l_height,
                                    uint32_t* l_offset_x,
                                    uint32_t* l_offset_y,
                                    uint32_t* l_image_width,
                                    uint32_t* l_stride,
                                    uint64_t* l_tile_offset);

static void j2k_get_tile_data (tcd_t * p_tcd, uint8_t * p_data);

static bool j2k_post_write_tile (j2k_t * p_j2k,
                                     GrokStream *p_stream,
                                     event_mgr_t * p_manager );

/**
 * Sets up the procedures to do on writing header.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool j2k_setup_header_writing (j2k_t *p_j2k, event_mgr_t * p_manager);

static bool j2k_write_first_tile_part(  j2k_t *p_j2k,
        uint64_t * p_data_written,
        uint64_t p_total_data_size,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

static bool j2k_write_all_tile_parts(   j2k_t *p_j2k,
        uint64_t * p_data_written,
        uint64_t p_total_data_size,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * Gets the offset of the header.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_get_end_header( j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

static bool j2k_allocate_tile_element_cstr_index(j2k_t *p_j2k);

/*
 * -----------------------------------------------------------------------
 * -----------------------------------------------------------------------
 * -----------------------------------------------------------------------
 */

/**
 * Writes the SOC marker (Start Of Codestream)
 *
 * @param       p_stream                        the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_soc(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a SOC marker (Start of Codestream)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_stream        XXX needs data
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_soc(   j2k_t *p_j2k,
                                GrokStream *p_stream,
                                event_mgr_t * p_manager );

/**
 * Writes the SIZ marker (image and tile size)
 *
 * @param       p_j2k           J2K codec.
 * @param       p_stream        the stream to write data to.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_siz(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a SIZ marker (image and tile size)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the SIZ box.
 * @param       p_header_size   the size of the data contained in the SIZ marker.
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_siz(j2k_t *p_j2k,
                             uint8_t * p_header_data,
                             uint32_t p_header_size,
                             event_mgr_t * p_manager);

/**
 * Writes the COM marker (comment)
 *
 * @param       p_stream                        the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_com(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a COM marker (comments)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the COM box.
 * @param       p_header_size   the size of the data contained in the COM marker.
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_com (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );
/**
 * Writes the COD marker (Coding style default)
 *
 * @param       p_stream                        the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_cod(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a COD marker (Coding Style defaults)
 * @param       p_header_data   the data contained in the COD box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the COD marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_cod (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager);

/**
 * Compares 2 COC markers (Coding style component)
 *
 * @param       p_j2k            J2K codec.
 * @param       p_first_comp_no  the index of the first component to compare.
 * @param       p_second_comp_no the index of the second component to compare.
 *
 * @return      true if equals
 */
static bool j2k_compare_coc(j2k_t *p_j2k, uint32_t p_first_comp_no, uint32_t p_second_comp_no);

/**
 * Writes the COC marker (Coding style component)
 *
 * @param       p_j2k       J2K codec.
 * @param       p_comp_no   the index of the component to output.
 * @param       p_stream    the stream to write data to.
 * @param       p_manager   the user event manager.
*/
static bool j2k_write_coc(  j2k_t *p_j2k,
                                uint32_t p_comp_no,
                                GrokStream *p_stream,
                                event_mgr_t * p_manager );

/**
 * Writes the COC marker (Coding style component)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_comp_no               the index of the component to output.
 * @param       p_data          FIXME DOC
 * @param       p_data_written  FIXME DOC
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_coc_in_memory(j2k_t *p_j2k,
                                        uint32_t p_comp_no,
										GrokStream *p_stream,
                                        event_mgr_t * p_manager );

/**
 * Gets the maximum size taken by a coc.
 *
 * @param       p_j2k   the jpeg2000 codec to use.
 */
static uint32_t j2k_get_max_coc_size(j2k_t *p_j2k);

/**
 * Reads a COC marker (Coding Style Component)
 * @param       p_header_data   the data contained in the COC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the COC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_coc (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

/**
 * Writes the QCD marker (quantization default)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_stream                the stream to write data to.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_qcd(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a QCD marker (Quantization defaults)
 * @param       p_header_data   the data contained in the QCD box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the QCD marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_qcd (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

/**
 * Compare QCC markers (quantization component)
 *
 * @param       p_j2k                 J2K codec.
 * @param       p_first_comp_no       the index of the first component to compare.
 * @param       p_second_comp_no      the index of the second component to compare.
 *
 * @return true if equals.
 */
static bool j2k_compare_qcc(j2k_t *p_j2k, uint32_t p_first_comp_no, uint32_t p_second_comp_no);

/**
 * Writes the QCC marker (quantization component)
 *
 * @param       p_comp_no       the index of the component to output.
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_qcc(      j2k_t *p_j2k,
                                    uint32_t p_comp_no,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Writes the QCC marker (quantization component)
 *
 * @param       p_j2k           J2K codec.
 * @param       p_comp_no       the index of the component to output.
 * @param       p_data          FIXME DOC
 * @param       p_data_written  the stream to write data to.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_qcc_in_memory(j2k_t *p_j2k,
                                        uint32_t p_comp_no,
										GrokStream *p_stream,
                                        event_mgr_t * p_manager );

/**
 * Gets the maximum size taken by a qcc.
 */
static uint32_t j2k_get_max_qcc_size (j2k_t *p_j2k);

/**
 * Reads a QCC marker (Quantization component)
 * @param       p_header_data   the data contained in the QCC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the QCC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_qcc(   j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager);

static uint16_t getPocSize(uint32_t l_nb_comp, uint32_t l_nb_poc);

/**
 * Writes the POC marker (Progression Order Change)
 *
 * @param       p_stream                                the stream to write data to.
 * @param       p_j2k                           J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_poc(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );
/**
 * Writes the POC marker (Progression Order Change)
 *
 * @param       p_j2k          J2K codec.
 * @param       p_manager      the user event manager.
 */
static bool j2k_write_poc_in_memory(j2k_t *p_j2k,
										GrokStream *p_stream,
										uint64_t * p_data_written,
                                        event_mgr_t * p_manager );
/**
 * Gets the maximum size taken by the writing of a POC.
 */
static uint32_t j2k_get_max_poc_size(j2k_t *p_j2k);

/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       p_header_data   the data contained in the POC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the POC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_poc (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

/**
 * Gets the maximum size taken by the toc headers of all the tile parts of any given tile.
 */
static uint32_t j2k_get_max_toc_size (j2k_t *p_j2k);

/**
 * Gets the maximum size taken by the headers of the SOT.
 *
 * @param       p_j2k   the jpeg2000 codec to use.
 */
static uint64_t j2k_get_specific_header_sizes(j2k_t *p_j2k);

/**
 * Reads a CRG marker (Component registration)
 *
 * @param       p_header_data   the data contained in the TLM box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the TLM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_crg (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );
/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       p_header_data   the data contained in the TLM box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the TLM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_tlm (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager);

/**
 * Writes the updated tlm.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_updated_tlm(      j2k_t *p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       p_header_data   the data contained in the TLM box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the TLM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_plm (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager);
/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       p_header_data   the data contained in the PLT box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the PLT marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_plt (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

/**
 * Reads a PPM marker (Packed headers, main header)
 *
 * @param       p_header_data   the data contained in the POC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the POC marker.
 * @param       p_manager               the user event manager.
 */

static bool j2k_read_ppm (
    j2k_t *p_j2k,
    uint8_t * p_header_data,
    uint32_t p_header_size,
    event_mgr_t * p_manager );

/**
 * Merges all PPM markers read (Packed headers, main header)
 *
 * @param       p_cp      main coding parameters.
 * @param       p_manager the user event manager.
 */
static bool j2k_merge_ppm ( cp_t *p_cp, event_mgr_t * p_manager );

/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       p_header_data   the data contained in the PPT box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the PPT marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_ppt (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );

/**
 * Merges all PPT markers read (Packed headers, tile-part header)
 *
 * @param       p_tcp   the tile.
 * @param       p_manager               the user event manager.
 */
static bool j2k_merge_ppt (  tcp_t *p_tcp,
                                 event_mgr_t * p_manager );


/**
 * Writes the TLM marker (Tile Length Marker)
 *
 * @param       p_stream                                the stream to write data to.
 * @param       p_j2k                           J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_tlm(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Writes the SOT marker (Start of tile-part)
 *
 * @param       p_j2k            J2K codec.
 * @param       p_data           FIXME DOC
 * @param       p_data_written   FIXME DOC
 * @param       p_manager        the user event manager.
*/
static bool j2k_write_sot(      j2k_t *p_j2k,
									GrokStream *p_stream,
									uint64_t* psot_location,
                                    uint64_t * p_data_written,
                                    event_mgr_t * p_manager );

/**
 * Reads values from a SOT marker (Start of tile-part)
 *
 * the j2k decoder state is not affected. No side effects, no checks except for p_header_size.
 *
 * @param       p_header_data   the data contained in the SOT marker.
 * @param       p_header_size   the size of the data contained in the SOT marker.
 * @param       p_tile_no       Isot.
 * @param       p_tot_len       Psot.
 * @param       p_current_part  TPsot.
 * @param       p_num_parts     TNsot.
 * @param       p_manager       the user event manager.
 */
static bool j2k_get_sot_values(uint8_t *  p_header_data,
                                   uint32_t  p_header_size,
                                   uint32_t* p_tile_no,
                                   uint32_t* p_tot_len,
                                   uint32_t* p_current_part,
                                   uint32_t* p_num_parts,
                                   event_mgr_t * p_manager );
/**
 * Reads a SOT marker (Start of tile-part)
 *
 * @param       p_header_data   the data contained in the SOT marker.
 * @param       p_j2k           the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the PPT marker.
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_sot (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager );
/**
 * Writes the SOD marker (Start of data)
 *
 * @param       p_j2k               J2K codec.
 * @param       p_tile_coder        FIXME DOC
 * @param       p_data              FIXME DOC
 * @param       p_data_written      FIXME DOC
 * @param       p_total_data_size   FIXME DOC
 * @param       p_stream            the stream to write data to.
 * @param       p_manager           the user event manager.
*/
static bool j2k_write_sod(      j2k_t *p_j2k,
                                    tcd_t * p_tile_coder,
                                    uint64_t * p_data_written,
                                    uint64_t p_total_data_size,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a SOD marker (Start Of Data)
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_stream                FIXME DOC
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_sod(   j2k_t *p_j2k,
                                GrokStream *p_stream,
                                event_mgr_t * p_manager );


static tcp_t* j2k_get_tcp(j2k_t *p_j2k) {
	auto l_cp = &(p_j2k->m_cp);
	return (p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_TPH) ?
		&l_cp->tcps[p_j2k->m_current_tile_number] :
		p_j2k->m_specific_param.m_decoder.m_default_tcp;
}



static void j2k_update_tlm (j2k_t * p_j2k, uint32_t p_tile_part_size )
{
	/* PSOT */
    grok_write_bytes(p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current,p_j2k->m_current_tile_number,1);         
    ++p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current;

	/* PSOT */
    grok_write_bytes(p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current,p_tile_part_size,4);                                        
    p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current += 4;
}

/**
 * Writes the RGN marker (Region Of Interest)
 *
 * @param       p_tile_no               the tile to output
 * @param       p_comp_no               the component to output
 * @param       nb_comps                the number of components
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_rgn(  j2k_t *p_j2k,
                                uint32_t p_tile_no,
                                uint32_t p_comp_no,
                                uint32_t nb_comps,
                                GrokStream *p_stream,
                                event_mgr_t * p_manager );

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       p_header_data   the data contained in the POC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the POC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_rgn (j2k_t *p_j2k,
                              uint8_t * p_header_data,
                              uint32_t p_header_size,
                              event_mgr_t * p_manager );

/**
 * Writes the EOC marker (End of Codestream)
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_eoc(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Writes the CBD-MCT-MCC-MCO markers (Multi components transform)
 *
 * @param       p_stream                        the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_mct_data_group(   j2k_t *p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * Inits the Info
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_init_info(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
Add main header marker information
@param cstr_index    Codestream information structure
@param type         marker type
@param pos          byte offset of marker segment
@param len          length of marker segment
 */
static bool j2k_add_mhmarker(opj_codestream_index_t *cstr_index, uint32_t type, int64_t pos, uint32_t len) ;
/**
Add tile header marker information
@param tileno       tile index number
@param cstr_index   Codestream information structure
@param type         marker type
@param pos          byte offset of marker segment
@param len          length of marker segment
 */
static bool j2k_add_tlmarker(uint32_t tileno, opj_codestream_index_t *cstr_index, uint32_t type, int64_t pos, uint32_t len);

/**
 * Reads an unknown marker
 *
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_stream                the stream object to read from.
 * @param       output_marker           FIXME DOC
 * @param       p_manager               the user event manager.
 *
 * @return      true                    if the marker could be deduced.
*/
static bool j2k_read_unk( j2k_t *p_j2k,
                              GrokStream *p_stream,
                              uint32_t *output_marker,
                              event_mgr_t * p_manager );

/**
 * Writes the MCT marker (Multiple Component Transform)
 *
 * @param       p_j2k           J2K codec.
 * @param       p_mct_record    FIXME DOC
 * @param       p_stream        the stream to write data to.
 * @param       p_manager       the user event manager.
*/
static bool j2k_write_mct_record(   mct_data_t * p_mct_record,
        GrokStream *p_stream,
        event_mgr_t * p_manager );

/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       p_header_data   the data contained in the MCT box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the MCT marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_mct (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager );

/**
 * Writes the MCC marker (Multiple Component Collection)
 *
 * @param       p_j2k                   J2K codec.
 * @param       p_mcc_record            FIXME DOC
 * @param       p_stream                the stream to write data to.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_mcc_record(       simple_mcc_decorrelation_data_t * p_mcc_record,
                                        GrokStream *p_stream,
                                        event_mgr_t * p_manager );

/**
 * Reads a MCC marker (Multiple Component Collection)
 *
 * @param       p_header_data   the data contained in the MCC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the MCC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_mcc (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager );

/**
 * Writes the MCO marker (Multiple component transformation ordering)
 *
 * @param       p_stream                                the stream to write data to.
 * @param       p_j2k                           J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_mco(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       p_header_data   the data contained in the MCO box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the MCO marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_mco (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager );

static bool j2k_add_mct(tcp_t * p_tcp, opj_image_t * p_image, uint32_t p_index);

static void  j2k_read_int16_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_read_int32_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_read_float32_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_read_float64_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);

static void  j2k_read_int16_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_read_int32_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_read_float32_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_read_float64_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);

static void  j2k_write_float_to_int16 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_write_float_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_write_float_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);
static void  j2k_write_float_to_float64 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);

/**
 * Ends the encoding, i.e. frees memory.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_end_encoding(   j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Writes the CBD marker (Component bit depth definition)
 *
 * @param       p_stream                                the stream to write data to.
 * @param       p_j2k                           J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_cbd(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Reads a CBD marker (Component bit depth definition)
 * @param       p_header_data   the data contained in the CBD box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the CBD marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_cbd (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager);


/**
 * Writes COC marker for each component.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_all_coc( j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager );

/**
 * Writes QCC marker for each component.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_all_qcc( j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager );

/**
 * Writes regions of interests.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_regions(  j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Writes EPC ????
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_write_epc(      j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager );

/**
 * Checks the progression order changes values. Tells of the poc given as input are valid.
 * A nice message is outputted at errors.
 *
 * @param       p_pocs                  the progression order changes.
 * @param       p_nb_pocs               the number of progression order changes.
 * @param       p_nb_resolutions        the number of resolutions.
 * @param       numcomps                the number of components
 * @param       numlayers               the number of layers.
 * @param       p_manager               the user event manager.
 *
 * @return      true if the pocs are valid.
 */
static bool j2k_check_poc_val(  const opj_poc_t *p_pocs,
                                    uint32_t p_nb_pocs,
                                    uint32_t p_nb_resolutions,
                                    uint32_t numcomps,
                                    uint32_t numlayers,
                                    event_mgr_t * p_manager);

/**
 * Gets the number of tile parts used for the given change of progression (if any) and the given tile.
 *
 * @param               cp                      the coding parameters.
 * @param               pino            the offset of the given poc (i.e. its position in the coding parameter).
 * @param               tileno          the given tile.
 *
 * @return              the number of tile parts.
 */
static uint32_t j2k_get_num_tp( cp_t *cp, uint32_t pino, uint32_t tileno);

/**
 * Calculates the total number of tile parts needed by the encoder to
 * encode such an image. If not enough memory is available, then the function return false.
 *
 * @param       cp                      the coding parameters for the image.
 * @param       p_nb_tiles      pointer that will hold the number of tile parts.
 * @param       image           the image to encode.
 * @param       p_manager       the user event manager.
 *
 * @return true if the function was successful, false else.
 */
static bool j2k_calculate_tp(        cp_t *cp,
                                    uint32_t * p_nb_tiles,
                                    opj_image_t *image,
                                    event_mgr_t * p_manager);

static void j2k_dump_MH_info(j2k_t* p_j2k, FILE* out_stream);

static void j2k_dump_MH_index(j2k_t* p_j2k, FILE* out_stream);

static opj_codestream_index_t* j2k_create_cstr_index(void);

static float j2k_get_tp_stride (tcp_t * p_tcp);

static float j2k_get_default_stride (tcp_t * p_tcp);

static uint32_t j2k_initialise_4K_poc(opj_poc_t *POC, uint32_t numres);

static void j2k_set_cinema_parameters(opj_cparameters_t *parameters, opj_image_t *image, event_mgr_t *p_manager);

static bool j2k_is_cinema_compliant(opj_image_t *image, uint16_t rsiz, event_mgr_t *p_manager);

/**
 * Checks for invalid number of tile-parts in SOT marker (TPsot==TNsot). See issue 254.
 *
 * @param       p_stream            the stream to read data from.
 * @param       tile_no             tile number we're looking for.
 * @param       p_correction_needed output value. if true, non conformant codestream needs TNsot correction.
 * @param       p_manager       the user event manager.
 *
 * @return true if the function was successful, false else.
 */
static bool j2k_need_nb_tile_parts_correction(GrokStream *p_stream, uint32_t tile_no, bool* p_correction_needed, event_mgr_t * p_manager );

/*@}*/

/*@}*/

/* ----------------------------------------------------------------------- */
struct j2k_prog_order_t {
    OPJ_PROG_ORDER enum_prog;
    char str_prog[5];
} ;

static j2k_prog_order_t j2k_prog_order_list[] = {
    {OPJ_CPRL, "CPRL"},
    {OPJ_LRCP, "LRCP"},
    {OPJ_PCRL, "PCRL"},
    {OPJ_RLCP, "RLCP"},
    {OPJ_RPCL, "RPCL"},
    {(OPJ_PROG_ORDER)-1, ""}
};

/**
 * FIXME DOC
 */
static const uint32_t MCT_ELEMENT_SIZE [] = {
    2,
    4,
    4,
    8
};

typedef void (* j2k_mct_function) (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem);

static const j2k_mct_function j2k_mct_read_functions_to_float [] = {
    j2k_read_int16_to_float,
    j2k_read_int32_to_float,
    j2k_read_float32_to_float,
    j2k_read_float64_to_float
};

static const j2k_mct_function j2k_mct_read_functions_to_int32 [] = {
    j2k_read_int16_to_int32,
    j2k_read_int32_to_int32,
    j2k_read_float32_to_int32,
    j2k_read_float64_to_int32
};

static const j2k_mct_function j2k_mct_write_functions_from_float [] = {
    j2k_write_float_to_int16,
    j2k_write_float_to_int32,
    j2k_write_float_to_float,
    j2k_write_float_to_float64
};

typedef struct opj_dec_memory_marker_handler {
    /** marker value */
    uint32_t id;
    /** value of the state when the marker can appear */
    uint32_t states;
    /** action linked to the marker */
    bool (*handler) (   j2k_t *p_j2k,
                        uint8_t * p_header_data,
                        uint32_t p_header_size,
                        event_mgr_t * p_manager );
}
opj_dec_memory_marker_handler_t;

static const opj_dec_memory_marker_handler_t j2k_memory_marker_handler_tab [] = {
    {J2K_MS_SOT, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPHSOT, j2k_read_sot},
    {J2K_MS_COD, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_cod},
    {J2K_MS_COC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_coc},
    {J2K_MS_RGN, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_rgn},
    {J2K_MS_QCD, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_qcd},
    {J2K_MS_QCC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_qcc},
    {J2K_MS_POC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_poc},
    {J2K_MS_SIZ, J2K_DEC_STATE_MHSIZ, j2k_read_siz},
    {J2K_MS_TLM, J2K_DEC_STATE_MH, j2k_read_tlm},
    {J2K_MS_PLM, J2K_DEC_STATE_MH, j2k_read_plm},
    {J2K_MS_PLT, J2K_DEC_STATE_TPH, j2k_read_plt},
    {J2K_MS_PPM, J2K_DEC_STATE_MH, j2k_read_ppm},
    {J2K_MS_PPT, J2K_DEC_STATE_TPH, j2k_read_ppt},
    {J2K_MS_SOP, 0, 0},
    {J2K_MS_CRG, J2K_DEC_STATE_MH, j2k_read_crg},
    {J2K_MS_COM, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_com},
    {J2K_MS_MCT, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_mct},
    {J2K_MS_CBD, J2K_DEC_STATE_MH , j2k_read_cbd},
    {J2K_MS_MCC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_mcc},
    {J2K_MS_MCO, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_mco},
    {J2K_MS_UNK, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, 0}/*j2k_read_unk is directly used*/
};

static void  j2k_read_int16_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    float * l_dest_data = (float *) p_dest_data;
    uint32_t i;
    uint32_t l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_bytes(l_src_data,&l_temp,2);

        l_src_data+=sizeof(int16_t);

        *(l_dest_data++) = (float) l_temp;
    }
}

static void  j2k_read_int32_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    float * l_dest_data = (float *) p_dest_data;
    uint32_t i;
    uint32_t l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_bytes(l_src_data,&l_temp,4);

        l_src_data+=sizeof(int32_t);

        *(l_dest_data++) = (float) l_temp;
    }
}

static void  j2k_read_float32_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    float * l_dest_data = (float *) p_dest_data;
    uint32_t i;
    float l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_float(l_src_data,&l_temp);

        l_src_data+=sizeof(float);

        *(l_dest_data++) = l_temp;
    }
}

static void  j2k_read_float64_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    float * l_dest_data = (float *) p_dest_data;
    uint32_t i;
    double l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_double(l_src_data,&l_temp);

        l_src_data+=sizeof(double);

        *(l_dest_data++) = (float) l_temp;
    }
}

static void  j2k_read_int16_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    int32_t * l_dest_data = (int32_t *) p_dest_data;
    uint32_t i;
    uint32_t l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_bytes(l_src_data,&l_temp,2);

        l_src_data+=sizeof(int16_t);

        *(l_dest_data++) = (int32_t) l_temp;
    }
}

static void  j2k_read_int32_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    int32_t * l_dest_data = (int32_t *) p_dest_data;
    uint32_t i;
    uint32_t l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_bytes(l_src_data,&l_temp,4);

        l_src_data+=sizeof(int32_t);

        *(l_dest_data++) = (int32_t) l_temp;
    }
}

static void  j2k_read_float32_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    int32_t * l_dest_data = (int32_t *) p_dest_data;
    uint32_t i;
    float l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_float(l_src_data,&l_temp);

        l_src_data+=sizeof(float);

        *(l_dest_data++) = (int32_t) l_temp;
    }
}

static void  j2k_read_float64_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_src_data = (uint8_t *) p_src_data;
    int32_t * l_dest_data = (int32_t *) p_dest_data;
    uint32_t i;
    double l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        grok_read_double(l_src_data,&l_temp);

        l_src_data+=sizeof(double);

        *(l_dest_data++) = (int32_t) l_temp;
    }
}

static void  j2k_write_float_to_int16 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_dest_data = (uint8_t *) p_dest_data;
    float * l_src_data = (float *) p_src_data;
    uint32_t i;
    uint32_t l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        l_temp = (uint32_t)*(l_src_data++);

        grok_write_bytes(l_dest_data,l_temp,sizeof(int16_t));

        l_dest_data+=sizeof(int16_t);
    }
}

static void j2k_write_float_to_int32 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_dest_data = (uint8_t *) p_dest_data;
    float * l_src_data = (float *) p_src_data;
    uint32_t i;
    uint32_t l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        l_temp =  (uint32_t)*(l_src_data++);

        grok_write_bytes(l_dest_data,l_temp,sizeof(int32_t));

        l_dest_data+=sizeof(int32_t);
    }
}

static void  j2k_write_float_to_float (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_dest_data = (uint8_t *) p_dest_data;
    float * l_src_data = (float *) p_src_data;
    uint32_t i;
    float l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        l_temp = (float) *(l_src_data++);

        grok_write_float(l_dest_data,l_temp);

        l_dest_data+=sizeof(float);
    }
}

static void  j2k_write_float_to_float64 (const void * p_src_data, void * p_dest_data, uint32_t p_nb_elem)
{
    uint8_t * l_dest_data = (uint8_t *) p_dest_data;
    float * l_src_data = (float *) p_src_data;
    uint32_t i;
    double l_temp;

    for (i=0; i<p_nb_elem; ++i) {
        l_temp = (double) *(l_src_data++);

        grok_write_double(l_dest_data,l_temp);

        l_dest_data+=sizeof(double);
    }
}

char *j2k_convert_progression_order(OPJ_PROG_ORDER prg_order)
{
    j2k_prog_order_t *po;
    for(po = j2k_prog_order_list; po->enum_prog != -1; po++ ) {
        if(po->enum_prog == prg_order) {
            return (char*)po->str_prog;
        }
    }
    return po->str_prog;
}

static bool j2k_check_poc_val( const opj_poc_t *p_pocs,
                                   uint32_t p_nb_pocs,
                                   uint32_t p_nb_resolutions,
                                   uint32_t p_num_comps,
                                   uint32_t p_num_layers,
                                   event_mgr_t * p_manager)
{
    uint32_t* packet_array;
    uint32_t index , resno, compno, layno;
    uint32_t i;
    uint32_t step_c = 1;
    uint32_t step_r = p_num_comps * step_c;
    uint32_t step_l = p_nb_resolutions * step_r;
    bool loss = false;
    uint32_t layno0 = 0;

    packet_array = (uint32_t*) grok_calloc(step_l * p_num_layers, sizeof(uint32_t));
    if (packet_array == nullptr) {
        event_msg(p_manager , EVT_ERROR, "Not enough memory for checking the poc values.\n");
        return false;
    }

    if (p_nb_pocs == 0) {
        grok_free(packet_array);
        return true;
    }

    index = step_r * p_pocs->resno0;
    /* take each resolution for each poc */
    for (resno = p_pocs->resno0 ; resno < p_pocs->resno1 ; ++resno) {
        uint32_t res_index = index + p_pocs->compno0 * step_c;

        /* take each comp of each resolution for each poc */
        for (compno = p_pocs->compno0 ; compno < p_pocs->compno1 ; ++compno) {
            uint32_t comp_index = res_index + layno0 * step_l;

            /* and finally take each layer of each res of ... */
            for (layno = layno0; layno < p_pocs->layno1 ; ++layno) {
                /*index = step_r * resno + step_c * compno + step_l * layno;*/
                packet_array[comp_index] = 1;
                comp_index += step_l;
            }

            res_index += step_c;
        }

        index += step_r;
    }
    ++p_pocs;

    /* iterate through all the pocs */
    for (i = 1; i < p_nb_pocs ; ++i) {
        uint32_t l_last_layno1 = (p_pocs-1)->layno1 ;

        layno0 = (p_pocs->layno1 > l_last_layno1)? l_last_layno1 : 0;
        index = step_r * p_pocs->resno0;

        /* take each resolution for each poc */
        for (resno = p_pocs->resno0 ; resno < p_pocs->resno1 ; ++resno) {
            uint32_t res_index = index + p_pocs->compno0 * step_c;

            /* take each comp of each resolution for each poc */
            for (compno = p_pocs->compno0 ; compno < p_pocs->compno1 ; ++compno) {
                uint32_t comp_index = res_index + layno0 * step_l;

                /* and finally take each layer of each res of ... */
                for (layno = layno0; layno < p_pocs->layno1 ; ++layno) {
                    /*index = step_r * resno + step_c * compno + step_l * layno;*/
                    packet_array[comp_index] = 1;
                    comp_index += step_l;
                }

                res_index += step_c;
            }

            index += step_r;
        }

        ++p_pocs;
    }

    index = 0;
    for (layno = 0; layno < p_num_layers ; ++layno) {
        for (resno = 0; resno < p_nb_resolutions; ++resno) {
            for (compno = 0; compno < p_num_comps; ++compno) {
                loss |= (packet_array[index]!=1);
                /*index = step_r * resno + step_c * compno + step_l * layno;*/
                index += step_c;
            }
        }
    }

    if (loss) {
        event_msg(p_manager , EVT_ERROR, "Missing packets possible loss of data\n");
    }

    grok_free(packet_array);

    return !loss;
}

/* ----------------------------------------------------------------------- */

static uint32_t j2k_get_num_tp(cp_t *cp, uint32_t pino, uint32_t tileno){
    const char *prog = nullptr;
    int32_t i;
    uint32_t tpnum = 1;
    tcp_t *tcp = nullptr;
    opj_poc_t * l_current_poc = nullptr;

    /*  preconditions */
    assert(tileno < (cp->tw * cp->th));
    assert(pino < (cp->tcps[tileno].numpocs + 1));

    /* get the given tile coding parameter */
    tcp = &cp->tcps[tileno];
    assert(tcp != nullptr);

    l_current_poc = &(tcp->pocs[pino]);
    assert(l_current_poc != 0);

    /* get the progression order as a character string */
    prog = j2k_convert_progression_order(tcp->prg);
    assert(strlen(prog) > 0);

    if (cp->m_specific_param.m_enc.m_tp_on == 1) {
        for (i=0; i<4; ++i) {
            switch (prog[i]) {
            /* component wise */
            case 'C':
                tpnum *= l_current_poc->compE;
                break;
            /* resolution wise */
            case 'R':
                tpnum *= l_current_poc->resE;
                break;
            /* precinct wise */
            case 'P':
                tpnum *= l_current_poc->prcE;
                break;
            /* layer wise */
            case 'L':
                tpnum *= l_current_poc->layE;
                break;
            }
			//we start a new tile part with every progression change
            if ( cp->m_specific_param.m_enc.m_tp_flag == prog[i] ) {
                cp->m_specific_param.m_enc.m_tp_pos=i;
                break;
            }
        }
    } else {
        tpnum=1;
    }

    return tpnum;
}

static bool j2k_calculate_tp(cp_t *cp,
                             uint32_t * p_nb_tiles,
                             opj_image_t *image,
                             event_mgr_t * p_manager){
    uint32_t pino,tileno;
    uint32_t l_nb_tiles;
    tcp_t *tcp;
	(void)p_manager;
    
    assert(p_nb_tiles != nullptr);
    assert(cp != nullptr);
    assert(image != nullptr);
    assert(p_manager != nullptr);

    l_nb_tiles = cp->tw * cp->th;
    * p_nb_tiles = 0;
    tcp = cp->tcps;

    /* INDEX >> */
    /* TODO mergeV2: check this part which use cstr_info */
    /*if (p_j2k->cstr_info) {
            opj_tile_info_t * l_info_tile_ptr = p_j2k->cstr_info->tile;
            for (tileno = 0; tileno < l_nb_tiles; ++tileno) {
                    uint32_t cur_totnum_tp = 0;
                    pi_update_encoding_parameters(image,cp,tileno);
                    for (pino = 0; pino <= tcp->numpocs; ++pino)
                    {
                            uint32_t tp_num = j2k_get_num_tp(cp,pino,tileno);
                            *p_nb_tiles = *p_nb_tiles + tp_num;
                            cur_totnum_tp += tp_num;
                    }
                    tcp->m_nb_tile_parts = cur_totnum_tp;
                    l_info_tile_ptr->tp = (opj_tp_info_t *) grok_malloc(cur_totnum_tp * sizeof(opj_tp_info_t));
                    if (l_info_tile_ptr->tp == nullptr) {
                            return false;
                    }
                    memset(l_info_tile_ptr->tp,0,cur_totnum_tp * sizeof(opj_tp_info_t));
                    l_info_tile_ptr->num_tps = cur_totnum_tp;
                    ++l_info_tile_ptr;
                    ++tcp;
            }
    }
    else */{
        for (tileno = 0; tileno < l_nb_tiles; ++tileno) {
            uint32_t cur_totnum_tp = 0;
            pi_update_encoding_parameters(image,cp,tileno);
            for (pino = 0; pino <= tcp->numpocs; ++pino) {
                uint32_t tp_num = j2k_get_num_tp(cp,pino,tileno);
				if (tp_num > 255) {
					event_msg(p_manager, EVT_ERROR, 
						"Tile %d contains more than 255 tile parts, which is not permitted by the JPEG 2000 standard.\n",tileno);
					return false;
				}
					
                *p_nb_tiles += tp_num;
                cur_totnum_tp += tp_num;
            }
            tcp->m_nb_tile_parts = cur_totnum_tp;
            ++tcp;
        }
    }
    return true;
}

static bool j2k_write_soc(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
    assert(p_stream != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
	(void)p_j2k;
	return p_stream->write_short(J2K_MS_SOC, p_manager);
}

/**
 * Reads a SOC marker (Start of Codestream)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_stream        FIXME DOC
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_soc(   j2k_t *p_j2k,
                                GrokStream *p_stream,
                                event_mgr_t * p_manager
                            )
{
    uint8_t l_data [2];
    uint32_t l_marker;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    if (p_stream->read(l_data,2,p_manager) != 2) {
        return false;
    }

    grok_read_bytes(l_data,&l_marker,2);
    if (l_marker != J2K_MS_SOC) {
        return false;
    }

    /* Next marker should be a SIZ marker in the main header */
    p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_MHSIZ;

	if (p_j2k->cstr_index) {
		/* FIXME move it in a index structure included in p_j2k*/
		p_j2k->cstr_index->main_head_start = p_stream->tell() - 2;

		//event_msg(p_manager, EVT_INFO, "Start to read j2k main header (%d).\n", p_j2k->cstr_index->main_head_start);

		/* Add the marker to the codestream index*/
		if (false == j2k_add_mhmarker(p_j2k->cstr_index, J2K_MS_SOC, p_j2k->cstr_index->main_head_start, 2)) {
			event_msg(p_manager, EVT_ERROR, "Not enough memory to add mh marker\n");
			return false;
		}
	}
    return true;
}

static bool j2k_write_siz(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
    uint32_t i;
    uint32_t l_size_len;
    opj_image_t * l_image = nullptr;
    cp_t *cp = nullptr;
    opj_image_comp_t * l_img_comp = nullptr;

    
    assert(p_stream != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_image = p_j2k->m_private_image;
    cp = &(p_j2k->m_cp);
    l_size_len = 40 + 3 * l_image->numcomps;
    l_img_comp = l_image->comps;

    /* write SOC identifier */

	/* SIZ */
	if (!p_stream->write_short(J2K_MS_SIZ, p_manager)) {
		return false;
	}

	/* L_SIZ */
	if (!p_stream->write_short((uint16_t)(l_size_len - 2), p_manager)) {
		return false;
	}

	/* Rsiz (capabilities) */
	if (!p_stream->write_short(cp->rsiz, p_manager)) {
		return false;
	}

	/* Xsiz */
	if (!p_stream->write_int(l_image->x1, p_manager)) {
		return false;
	}

	/* Ysiz */
	if (!p_stream->write_int(l_image->y1, p_manager)) {
		return false;
	}

	/* X0siz */
	if (!p_stream->write_int(l_image->x0, p_manager)) {
		return false;
	}

	/* Y0siz */
	if (!p_stream->write_int(l_image->y0, p_manager)) {
		return false;
	}

	/* XTsiz */
	if (!p_stream->write_int(cp->tdx, p_manager)) {
		return false;
	}

	/* YTsiz */
	if (!p_stream->write_int(cp->tdy, p_manager)) {
		return false;
	}

	/* XT0siz */
	if (!p_stream->write_int(cp->tx0, p_manager)) {
		return false;
	}

	/* YT0siz */
	if (!p_stream->write_int(cp->ty0, p_manager)) {
		return false;
	}

	/* Csiz */
	if (!p_stream->write_short((uint16_t)l_image->numcomps, p_manager)) {
		return false;
	}


    for (i = 0; i < l_image->numcomps; ++i) {
        /* TODO here with MCT ? */
		/* Ssiz_i */
		if (!p_stream->write_byte((uint8_t)(l_img_comp->prec - 1 + (l_img_comp->sgnd << 7)), p_manager)) {
			return false;
		}

		/* XRsiz_i */
		if (!p_stream->write_byte((uint8_t)l_img_comp->dx, p_manager)) {
			return false;
		}

		/* YRsiz_i */
		if (!p_stream->write_byte((uint8_t)l_img_comp->dy, p_manager)) {
			return false;
		}
        ++l_img_comp;
    }
    return true;
}

/**
 * Reads a SIZ marker (image and tile size)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the SIZ box.
 * @param       p_header_size   the size of the data contained in the SIZ marker.
 * @param       p_manager       the user event manager.
*/
static bool j2k_read_siz(j2k_t *p_j2k,
                             uint8_t * p_header_data,
                             uint32_t p_header_size,
                             event_mgr_t * p_manager
                            )
{
    uint32_t i;
    uint32_t l_nb_comp;
    uint32_t l_nb_comp_remain;
    uint32_t l_remaining_size;
    uint32_t l_nb_tiles;
    uint32_t l_tmp, l_tx1, l_ty1;
    opj_image_t *l_image = nullptr;
    cp_t *l_cp = nullptr;
    opj_image_comp_t * l_img_comp = nullptr;
    tcp_t * l_current_tile_param = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_header_data != nullptr);

    l_image = p_j2k->m_private_image;
    l_cp = &(p_j2k->m_cp);

    /* minimum size == 39 - 3 (= minimum component parameter) */
    if (p_header_size < 36) {
        event_msg(p_manager, EVT_ERROR, "Error with SIZ marker size\n");
        return false;
    }

    l_remaining_size = p_header_size - 36;
    l_nb_comp = l_remaining_size / 3;
    l_nb_comp_remain = l_remaining_size % 3;
    if (l_nb_comp_remain != 0) {
        event_msg(p_manager, EVT_ERROR, "Error with SIZ marker size\n");
        return false;
    }

    grok_read_bytes(p_header_data,&l_tmp ,2);                                                /* Rsiz (capabilities) */
    p_header_data+=2;

	// sanity check on RSIZ
	uint16_t profile = 0;
	uint16_t part2_extensions = 0;
	// check for Part 2
	if (l_tmp & OPJ_PROFILE_PART2) {
		profile = OPJ_PROFILE_PART2;
		part2_extensions = l_tmp & OPJ_PROFILE_PART2_EXTENSIONS_MASK;
		(void)part2_extensions;
	}
	else {
		profile = l_tmp & OPJ_PROFILE_MASK;
		if ((profile > OPJ_PROFILE_CINEMA_LTS) && !OPJ_IS_BROADCAST(profile) && !OPJ_IS_IMF(profile) ){
			event_msg(p_manager, EVT_ERROR, "Non-compliant Rsiz value 0x%x in SIZ marker\n", l_tmp);
			return false;
		}
	}


	l_cp->rsiz = (uint16_t)l_tmp;
    grok_read_bytes(p_header_data, (uint32_t*) &l_image->x1, 4);   /* Xsiz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_image->y1, 4);   /* Ysiz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_image->x0, 4);   /* X0siz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_image->y0, 4);   /* Y0siz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_cp->tdx, 4);             /* XTsiz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_cp->tdy, 4);             /* YTsiz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_cp->tx0, 4);             /* XT0siz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_cp->ty0, 4);             /* YT0siz */
    p_header_data+=4;
    grok_read_bytes(p_header_data, (uint32_t*) &l_tmp, 2);                 /* Csiz */
    p_header_data+=2;
    if (l_tmp <= max_num_components)
        l_image->numcomps = (uint16_t) l_tmp;
    else {
        event_msg(p_manager, EVT_ERROR, "Error with SIZ marker: number of component is illegal -> %d\n", l_tmp);
        return false;
    }

    if (l_image->numcomps != l_nb_comp) {
        event_msg(p_manager, EVT_ERROR, "Error with SIZ marker: number of component is not compatible with the remaining number of parameters ( %d vs %d)\n", l_image->numcomps, l_nb_comp);
        return false;
    }

    /* testcase 4035.pdf.SIGSEGV.d8b.3375 */
    /* testcase issue427-null-image-size.jp2 */
    if ((l_image->x0 >= l_image->x1) || (l_image->y0 >= l_image->y1)) {
		std::stringstream ss;
		ss << "Error with SIZ marker: negative or zero image size (" << (int64_t)l_image->x1 - l_image->x0 << " x " << (int64_t)l_image->y1 - l_image->y0 << ")" << std::endl;
		event_msg(p_manager, EVT_ERROR, "%s",ss.str().c_str());
        return false;
    }
    /* testcase 2539.pdf.SIGFPE.706.1712 (also 3622.pdf.SIGFPE.706.2916 and 4008.pdf.SIGFPE.706.3345 and maybe more) */
	if ((l_cp->tdx == 0U) || (l_cp->tdy == 0U)) {
        event_msg(p_manager, EVT_ERROR, "Error with SIZ marker: invalid tile size (tdx: %d, tdy: %d)\n", l_cp->tdx, l_cp->tdy);
        return false;
    }

    /* testcase issue427-illegal-tile-offset.jp2 */
    l_tx1 = uint_adds(l_cp->tx0, l_cp->tdx); /* manage overflow */
    l_ty1 = uint_adds(l_cp->ty0, l_cp->tdy); /* manage overflow */
    if ((l_cp->tx0 > l_image->x0) || (l_cp->ty0 > l_image->y0) || (l_tx1 <= l_image->x0) || (l_ty1 <= l_image->y0) ) {
        event_msg(p_manager, EVT_ERROR, "Error with SIZ marker: illegal tile offset\n");
        return false;
    }

	uint64_t tileArea = (uint64_t)(l_tx1 - l_cp->tx0) *(l_ty1 - l_cp->ty0);
	if (tileArea > max_tile_area) {
		event_msg(p_manager, EVT_ERROR, "Error with SIZ marker: tile area = %llu greater than max tile area = %llu\n",tileArea, max_tile_area);
		return false;

	}

    /* Allocate the resulting image components */
    l_image->comps = (opj_image_comp_t*) grok_calloc(l_image->numcomps, sizeof(opj_image_comp_t));
    if (l_image->comps == nullptr) {
        l_image->numcomps = 0;
        event_msg(p_manager, EVT_ERROR, "Not enough memory to take in charge SIZ marker\n");
        return false;
    }

    l_img_comp = l_image->comps;

    /* Read the component information */
    for (i = 0; i < l_image->numcomps; ++i) {
        uint32_t tmp;
        grok_read_bytes(p_header_data,&tmp,1);   /* Ssiz_i */
        ++p_header_data;
        l_img_comp->prec = (tmp & 0x7f) + 1;
        l_img_comp->sgnd = tmp >> 7;
        grok_read_bytes(p_header_data,&tmp,1);   /* XRsiz_i */
        ++p_header_data;
        l_img_comp->dx = tmp; /* should be between 1 and 255 */
        grok_read_bytes(p_header_data,&tmp,1);   /* YRsiz_i */
        ++p_header_data;
        l_img_comp->dy = tmp; /* should be between 1 and 255 */
        if( l_img_comp->dx < 1 || l_img_comp->dx > 255 ||
                l_img_comp->dy < 1 || l_img_comp->dy > 255 ) {
            event_msg(p_manager, EVT_ERROR,
                          "Invalid values for comp = %d : dx=%u dy=%u\n (should be between 1 and 255 according to the JPEG2000 standard)",
                          i, l_img_comp->dx, l_img_comp->dy);
            return false;
        }

		if (l_img_comp->prec == 0 || 
				l_img_comp->prec > max_supported_precision) {
			event_msg(p_manager, EVT_ERROR,
				"Unsupported precision for comp = %d : prec=%u (Grok only supportes precision between 1 and %d)\n",
				i, l_img_comp->prec, max_supported_precision);
			return false;
		}
        l_img_comp->resno_decoded = 0;                                                          /* number of resolution decoded */
        l_img_comp->decodeScaleFactor = l_cp->m_specific_param.m_dec.m_reduce; /* reducing factor per component */
        ++l_img_comp;
    }

    /* Compute the number of tiles */
    l_cp->tw = ceildiv<uint32_t>(l_image->x1 - l_cp->tx0, l_cp->tdx);
    l_cp->th = ceildiv<uint32_t>(l_image->y1 - l_cp->ty0, l_cp->tdy);

    /* Check that the number of tiles is valid */
	if (l_cp->tw == 0 || l_cp->th == 0) {
		event_msg(p_manager, EVT_ERROR,
			"Invalid grid of tiles: %u x %u. Standard requires at least one tile in grid. \n",
			l_cp->tw, l_cp->th);
		return false;
	}
    if (l_cp->tw > 65535 / l_cp->th) {
        event_msg(  p_manager, EVT_ERROR,
                        "Invalid grid of tiles : %u x %u.  Maximum fixed by JPEG 2000 standard is 65535 tiles\n",
                        l_cp->tw, l_cp->th);
        return false;
    }
    l_nb_tiles = l_cp->tw * l_cp->th;

    /* Define the tiles which will be decoded */
    if (p_j2k->m_specific_param.m_decoder.m_discard_tiles) {
        p_j2k->m_specific_param.m_decoder.m_start_tile_x = (p_j2k->m_specific_param.m_decoder.m_start_tile_x - l_cp->tx0) / l_cp->tdx;
        p_j2k->m_specific_param.m_decoder.m_start_tile_y = (p_j2k->m_specific_param.m_decoder.m_start_tile_y - l_cp->ty0) / l_cp->tdy;
        p_j2k->m_specific_param.m_decoder.m_end_tile_x = ceildiv<uint32_t>((p_j2k->m_specific_param.m_decoder.m_end_tile_x - l_cp->tx0), l_cp->tdx);
        p_j2k->m_specific_param.m_decoder.m_end_tile_y = ceildiv<uint32_t>((p_j2k->m_specific_param.m_decoder.m_end_tile_y - l_cp->ty0), l_cp->tdy);
    } else {
        p_j2k->m_specific_param.m_decoder.m_start_tile_x = 0;
        p_j2k->m_specific_param.m_decoder.m_start_tile_y = 0;
        p_j2k->m_specific_param.m_decoder.m_end_tile_x = l_cp->tw;
        p_j2k->m_specific_param.m_decoder.m_end_tile_y = l_cp->th;
    }

    /* memory allocations */
    l_cp->tcps = (tcp_t*) grok_calloc(l_nb_tiles, sizeof(tcp_t));
    if (l_cp->tcps == nullptr) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to take in charge SIZ marker\n");
        return false;
    }

    p_j2k->m_specific_param.m_decoder.m_default_tcp->tccps =
        (tccp_t*) grok_calloc(l_image->numcomps, sizeof(tccp_t));
    if(p_j2k->m_specific_param.m_decoder.m_default_tcp->tccps  == nullptr) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to take in charge SIZ marker\n");
        return false;
    }

    p_j2k->m_specific_param.m_decoder.m_default_tcp->m_mct_records =
        (mct_data_t*)grok_calloc(default_number_mct_records ,sizeof(mct_data_t));

    if (! p_j2k->m_specific_param.m_decoder.m_default_tcp->m_mct_records) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to take in charge SIZ marker\n");
        return false;
    }
    p_j2k->m_specific_param.m_decoder.m_default_tcp->m_nb_max_mct_records = default_number_mct_records;

    p_j2k->m_specific_param.m_decoder.m_default_tcp->m_mcc_records =
        (simple_mcc_decorrelation_data_t*)
        grok_calloc(default_number_mcc_records, sizeof(simple_mcc_decorrelation_data_t));

    if (! p_j2k->m_specific_param.m_decoder.m_default_tcp->m_mcc_records) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to take in charge SIZ marker\n");
        return false;
    }
    p_j2k->m_specific_param.m_decoder.m_default_tcp->m_nb_max_mcc_records = default_number_mcc_records;

    /* set up default dc level shift */
    for (i=0; i<l_image->numcomps; ++i) {
        if (! l_image->comps[i].sgnd) {
            p_j2k->m_specific_param.m_decoder.m_default_tcp->tccps[i].m_dc_level_shift = 1 << (l_image->comps[i].prec - 1);
        }
    }

    l_current_tile_param = l_cp->tcps;
    for     (i = 0; i < l_nb_tiles; ++i) {
        l_current_tile_param->tccps = (tccp_t*) grok_calloc(l_image->numcomps, sizeof(tccp_t));
        if (l_current_tile_param->tccps == nullptr) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to take in charge SIZ marker\n");
            return false;
        }

        ++l_current_tile_param;
    }

    p_j2k->m_specific_param.m_decoder.m_state =  J2K_DEC_STATE_MH;
    opj_image_comp_header_update(l_image,l_cp);

    return true;
}

static bool j2k_write_com(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager
                             )
{
    uint32_t l_comment_size;
    uint32_t l_total_com_size;
    const char *l_comment;
    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    l_comment = p_j2k->m_cp.comment;
    l_comment_size = (uint32_t)strlen(l_comment);
    l_total_com_size = l_comment_size + 6;

	/* COM */
	if (!p_stream->write_short(J2K_MS_COM, p_manager)) {
		return false;
	}

	/* L_COM */
	if (!p_stream->write_short((uint16_t)(l_total_com_size - 2), p_manager)) {
		return false;
	}

	/* General use (IS 8859-15:1999 (Latin) values) */
	if (!p_stream->write_short(1, p_manager)) {
		return false;
	}

	if (!p_stream->write_bytes((uint8_t*)l_comment, l_comment_size, p_manager)) {
		return false;
	}
    return true;
}

/**
 * Reads a COM marker (comments)
 * @param       p_j2k           the jpeg2000 file codec.
 * @param       p_header_data   the data contained in the COM box.
 * @param       p_header_size   the size of the data contained in the COM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_com (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_header_data != nullptr);
	assert(p_header_size != 0);
	
	if (p_header_size < 2) {
		event_msg(p_manager, EVT_ERROR, "j2k_read_com: Corrupt COM segment \n");
		return false;
	}	else if (p_header_size == 2) {
		event_msg(p_manager, EVT_WARNING, "j2k_read_com: Empty COM segment. Ignoring \n");
		return true;
	}
	uint32_t commentType;
	grok_read_bytes(p_header_data, &commentType, 2);

	p_j2k->m_cp.isBinaryComment = commentType == 1;
	 if (commentType > 1) {
		event_msg(p_manager, EVT_WARNING, "j2k_read_com: Unrecognized comment type. Assuming IS 8859-15:1999 (Latin) values)\n");
	}

	p_header_data += 2;
	size_t commentSize = p_header_size - 2;
	if (p_j2k->m_cp.comment)
		grok_free(p_j2k->m_cp.comment);

	size_t commentSizeToAlloc = commentSize;
	if (!p_j2k->m_cp.isBinaryComment)
		commentSizeToAlloc++;
	p_j2k->m_cp.comment = (char*)grok_malloc(commentSizeToAlloc);
	if (!p_j2k->m_cp.comment) {
		event_msg(p_manager, EVT_ERROR, "j2k_read_com: Out of memory when allocating memory for comment \n");
		return true;
	}
	memcpy(p_j2k->m_cp.comment, p_header_data, commentSize);
	p_j2k->m_cp.comment_len = commentSize;

	// make null-terminated string
	if (!p_j2k->m_cp.isBinaryComment)
		p_j2k->m_cp.comment[commentSize] = 0;
    return true;
}

static bool j2k_write_cod(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    uint32_t l_code_size;
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_j2k->m_current_tile_number];
    l_code_size = 9 + j2k_get_SPCod_SPCoc_size(p_j2k,p_j2k->m_current_tile_number,0);

	/* COD */
	if (!p_stream->write_short(J2K_MS_COD, p_manager)) {
		return false;
	}

	/* L_COD */
	if (!p_stream->write_short((uint16_t)(l_code_size - 2), p_manager)) {
		return false;
	}

	/* Scod */
	if (!p_stream->write_byte((uint8_t)l_tcp->csty, p_manager)) {
		return false;
	}

	/* SGcod (A) */
	if (!p_stream->write_byte((uint8_t)l_tcp->prg, p_manager)) {
		return false;
	}

	/* SGcod (B) */
	if (!p_stream->write_short((uint16_t)l_tcp->numlayers, p_manager)) {
		return false;
	}

	/* SGcod (C) */
	if (!p_stream->write_byte((uint8_t)l_tcp->mct, p_manager)) {
		return false;
	}


    if (! j2k_write_SPCod_SPCoc(p_j2k,p_j2k->m_current_tile_number,0,p_stream,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error writing COD marker\n");
        return false;
    }

    return true;
}

/**
 * Reads a COD marker (Coding Style defaults)
 * @param       p_header_data   the data contained in the COD box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the COD marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_cod (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    /* loop */
    uint32_t i;
    uint32_t l_tmp;
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    opj_image_t *l_image = nullptr;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_image = p_j2k->m_private_image;
    l_cp = &(p_j2k->m_cp);

    /* If we are in the first tile-part header of the current tile */
	l_tcp = j2k_get_tcp(p_j2k);

    /* Only one COD per tile */
    if (l_tcp->cod) {
        event_msg(p_manager, EVT_WARNING, "Multiple COD markers detected for tile part %d. The JPEG 2000 standard does not allow more than one COD marker per tile.\n", l_tcp->m_current_tile_part_number);
    }
    l_tcp->cod = 1;

    /* Make sure room is sufficient */
    if (p_header_size < 5) {
        event_msg(p_manager, EVT_ERROR, "Error reading COD marker\n");
        return false;
    }

    grok_read_bytes(p_header_data,&l_tcp->csty,1);           /* Scod */
    ++p_header_data;
    /* Make sure we know how to decode this */
    if ((l_tcp->csty & ~(uint32_t)(J2K_CP_CSTY_PRT | J2K_CP_CSTY_SOP | J2K_CP_CSTY_EPH)) != 0U) {
        event_msg(p_manager, EVT_ERROR, "Unknown Scod value in COD marker\n");
        return false;
    }
    grok_read_bytes(p_header_data,&l_tmp,1);                         /* SGcod (A) */
    ++p_header_data;
    l_tcp->prg = (OPJ_PROG_ORDER) l_tmp;
    /* Make sure progression order is valid */
    if (l_tcp->prg > OPJ_CPRL ) {
        event_msg(p_manager, EVT_ERROR, "Unknown progression order in COD marker\n");
        l_tcp->prg = OPJ_PROG_UNKNOWN;
    }
    grok_read_bytes(p_header_data,&l_tcp->numlayers,2);      /* SGcod (B) */
    p_header_data+=2;

    if ((l_tcp->numlayers < 1U) || (l_tcp->numlayers > 65535U)) {
        event_msg(p_manager, EVT_ERROR, "Invalid number of layers in COD marker : %d not in range [1-65535]\n", l_tcp->numlayers);
        return false;
    }

    /* If user didn't set a number layer to decode take the max specify in the codestream. */
    if      (l_cp->m_specific_param.m_dec.m_layer) {
        l_tcp->num_layers_to_decode = l_cp->m_specific_param.m_dec.m_layer;
    } else {
        l_tcp->num_layers_to_decode = l_tcp->numlayers;
    }

    grok_read_bytes(p_header_data,&l_tcp->mct,1);            /* SGcod (C) */
    ++p_header_data;

    p_header_size -= 5;
    for     (i = 0; i < l_image->numcomps; ++i) {
        l_tcp->tccps[i].csty = l_tcp->csty & J2K_CCP_CSTY_PRT;
    }

    if (! j2k_read_SPCod_SPCoc(p_j2k,0,p_header_data,&p_header_size,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error reading COD marker\n");
        return false;
    }

    if (p_header_size != 0) {
        event_msg(p_manager, EVT_ERROR, "Error reading COD marker\n");
        return false;
    }

    /* Apply the coding style to other components of the current tile or the m_default_tcp*/
    j2k_copy_tile_component_parameters(p_j2k);

    return true;
}

static bool j2k_write_coc( j2k_t *p_j2k,
                               uint32_t p_comp_no,
                               GrokStream *p_stream,
                               event_mgr_t * p_manager )
{
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);
    return j2k_write_coc_in_memory(p_j2k,
							p_comp_no,
							p_stream,
							p_manager);

}

static bool j2k_compare_coc(j2k_t *p_j2k, uint32_t p_first_comp_no, uint32_t p_second_comp_no)
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;

    
    assert(p_j2k != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_j2k->m_current_tile_number];

    if (l_tcp->tccps[p_first_comp_no].csty != l_tcp->tccps[p_second_comp_no].csty) {
        return false;
    }


    return j2k_compare_SPCod_SPCoc(p_j2k, p_j2k->m_current_tile_number, p_first_comp_no, p_second_comp_no);
}

static bool j2k_write_coc_in_memory(   j2k_t *p_j2k,
        uint32_t p_comp_no,
		GrokStream *p_stream,
        event_mgr_t * p_manager
                                       )
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    uint32_t l_coc_size;
    opj_image_t *l_image = nullptr;
    uint32_t l_comp_room;
	    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_j2k->m_current_tile_number];
    l_image = p_j2k->m_private_image;
    l_comp_room = (l_image->numcomps <= 256) ? 1 : 2;
    l_coc_size = 5 + l_comp_room + j2k_get_SPCod_SPCoc_size(p_j2k,p_j2k->m_current_tile_number,p_comp_no);

	/* COC */
	if (!p_stream->write_short(J2K_MS_COC, p_manager)) {
		return false;
	}

	/* L_COC */
	if (!p_stream->write_short((uint16_t)(l_coc_size - 2), p_manager)) {
		return false;
	}

	/* Ccoc */
	if (l_comp_room == 2) {
		if (!p_stream->write_short((uint16_t)p_comp_no, p_manager)) {
			return false;
		}
	}
	else {
		if (!p_stream->write_byte((uint8_t)p_comp_no, p_manager)) {
			return false;
		}
	}

	/* Scoc */
	if (!p_stream->write_byte((uint8_t)l_tcp->tccps[p_comp_no].csty, p_manager)) {
		return false;
	}
    return j2k_write_SPCod_SPCoc(p_j2k,p_j2k->m_current_tile_number,0,p_stream,p_manager);
}

static uint32_t j2k_get_max_coc_size(j2k_t *p_j2k)
{
    uint32_t i,j;
    uint32_t l_nb_comp;
    uint32_t l_nb_tiles;
    uint32_t l_max = 0;

    

    l_nb_tiles = p_j2k->m_cp.tw * p_j2k->m_cp.th ;
    l_nb_comp = p_j2k->m_private_image->numcomps;

    for (i=0; i<l_nb_tiles; ++i) {
        for (j=0; j<l_nb_comp; ++j) {
            l_max = std::max<uint32_t>(l_max,j2k_get_SPCod_SPCoc_size(p_j2k,i,j));
        }
    }

    return 6 + l_max;
}

/**
 * Reads a COC marker (Coding Style Component)
 * @param       p_header_data   the data contained in the COC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the COC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_coc (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    tcp_t *l_tcp = nullptr;
    opj_image_t *l_image = nullptr;
    uint32_t l_comp_room;
    uint32_t l_comp_no;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

	l_tcp = j2k_get_tcp(p_j2k);
    l_image = p_j2k->m_private_image;

    l_comp_room = l_image->numcomps <= 256 ? 1 : 2;

    /* make sure room is sufficient*/
    if (p_header_size < l_comp_room + 1) {
        event_msg(p_manager, EVT_ERROR, "Error reading COC marker\n");
        return false;
    }
    p_header_size -= l_comp_room + 1;

    grok_read_bytes(p_header_data,&l_comp_no,l_comp_room);                   /* Ccoc */
    p_header_data += l_comp_room;
    if (l_comp_no >= l_image->numcomps) {
        event_msg(p_manager, EVT_ERROR, "Error reading COC marker (bad number of components)\n");
        return false;
    }

    grok_read_bytes(p_header_data,&l_tcp->tccps[l_comp_no].csty,1);                  /* Scoc */
    ++p_header_data ;

    if (! j2k_read_SPCod_SPCoc(p_j2k,l_comp_no,p_header_data,&p_header_size,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error reading COC marker\n");
        return false;
    }

    if (p_header_size != 0) {
        event_msg(p_manager, EVT_ERROR, "Error reading COC marker\n");
        return false;
    }
    return true;
}

static bool j2k_write_qcd(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager
                             )
{
    uint32_t l_qcd_size;
   
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_qcd_size = 4 + j2k_get_SQcd_SQcc_size(p_j2k,p_j2k->m_current_tile_number,0);

	/* QCD */
	if (!p_stream->write_short(J2K_MS_QCD, p_manager)) {
		return false;
	}

	/* L_QCD */
	if (!p_stream->write_short((uint16_t)(l_qcd_size - 2), p_manager)) {
		return false;
	}

    if (! j2k_write_SQcd_SQcc(p_j2k,p_j2k->m_current_tile_number,0,p_stream,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error writing QCD marker\n");
        return false;
    }

    return true;
}

/**
 * Reads a QCD marker (Quantization defaults)
 * @param       p_header_data   the data contained in the QCD box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the QCD marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_qcd (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! j2k_read_SQcd_SQcc(true,p_j2k,0,p_header_data,&p_header_size,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error reading QCD marker\n");
        return false;
    }

    if (p_header_size != 0) {
        event_msg(p_manager, EVT_ERROR, "Error reading QCD marker\n");
        return false;
    }

    /* Apply the quantization parameters to other components of the current tile or the m_default_tcp */
    j2k_copy_tile_quantization_parameters(p_j2k);

    return true;
}

static bool j2k_write_qcc(     j2k_t *p_j2k,
                                   uint32_t p_comp_no,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager
                             ){
   
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    return j2k_write_qcc_in_memory(p_j2k,p_comp_no, p_stream,p_manager);
}

static bool j2k_compare_qcc(j2k_t *p_j2k, uint32_t p_first_comp_no, uint32_t p_second_comp_no)
{
    return j2k_compare_SQcd_SQcc(p_j2k,p_j2k->m_current_tile_number,p_first_comp_no, p_second_comp_no);
}

static bool j2k_write_qcc_in_memory(   j2k_t *p_j2k,
        uint32_t p_comp_no,
		GrokStream *p_stream,
        event_mgr_t * p_manager
                                       )
{
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

	uint32_t l_qcc_size = 6 + j2k_get_SQcd_SQcc_size(p_j2k,p_j2k->m_current_tile_number,p_comp_no);

	/* QCC */
	if (!p_stream->write_short(J2K_MS_QCC, p_manager)) {
		return false;
	}

    if (p_j2k->m_private_image->numcomps <= 256) {
        --l_qcc_size;

		/* L_QCC */
		if (!p_stream->write_short((uint16_t)(l_qcc_size - 2), p_manager)) {
			return false;
		}

		/* Cqcc */
		if (!p_stream->write_byte((uint8_t)p_comp_no, p_manager)) {
			return false;
		}

    } else {
		/* L_QCC */
		if (!p_stream->write_short((uint16_t)(l_qcc_size - 2), p_manager)) {
			return false;
		}

		/* Cqcc */
		if (!p_stream->write_short((uint16_t)p_comp_no, p_manager)) {
			return false;
		}
    }

    return j2k_write_SQcd_SQcc(p_j2k,p_j2k->m_current_tile_number,p_comp_no,p_stream,p_manager);
}

static uint32_t j2k_get_max_qcc_size (j2k_t *p_j2k)
{
    return j2k_get_max_coc_size(p_j2k);
}

/**
 * Reads a QCC marker (Quantization component)
 * @param       p_header_data   the data contained in the QCC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the QCC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_qcc(   j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                            )
{
    uint32_t l_num_comp,l_comp_no;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_num_comp = p_j2k->m_private_image->numcomps;

    if (l_num_comp <= 256) {
        if (p_header_size < 1) {
            event_msg(p_manager, EVT_ERROR, "Error reading QCC marker\n");
            return false;
        }
        grok_read_bytes(p_header_data,&l_comp_no,1);
        ++p_header_data;
        --p_header_size;
    } else {
        if (p_header_size < 2) {
            event_msg(p_manager, EVT_ERROR, "Error reading QCC marker\n");
            return false;
        }
        grok_read_bytes(p_header_data,&l_comp_no,2);
        p_header_data+=2;
        p_header_size-=2;
    }

    if (l_comp_no >= p_j2k->m_private_image->numcomps) {
        event_msg(p_manager, EVT_ERROR,
                      "Invalid component number: %d, regarding the number of components %d\n",
                      l_comp_no, p_j2k->m_private_image->numcomps);
        return false;
    }

    if (! j2k_read_SQcd_SQcc(false,p_j2k,l_comp_no,p_header_data,&p_header_size,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error reading QCC marker\n");
        return false;
    }

    if (p_header_size != 0) {
        event_msg(p_manager, EVT_ERROR, "Error reading QCC marker\n");
        return false;
    }

    return true;
}

static uint16_t getPocSize(uint32_t l_nb_comp, uint32_t l_nb_poc) {
	uint32_t l_poc_room;
	if (l_nb_comp <= 256) {
		l_poc_room = 1;
	}
	else {
		l_poc_room = 2;
	}
	return (uint16_t)(4 + (5 + 2 * l_poc_room) * l_nb_poc);
}

static bool j2k_write_poc(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager  )
{
   
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

	uint64_t p_data_written = 0;
	return j2k_write_poc_in_memory(p_j2k, p_stream,&p_data_written, p_manager);
}

static bool j2k_write_poc_in_memory(   j2k_t *p_j2k,
		GrokStream *p_stream,
		uint64_t * p_data_written,
        event_mgr_t * p_manager)
{
	(void)p_manager;
    uint32_t i;
    uint32_t l_nb_comp;
    uint32_t l_nb_poc;
    opj_image_t *l_image = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;
    opj_poc_t *l_current_poc = nullptr;
    uint32_t l_poc_room;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_tcp = &p_j2k->m_cp.tcps[p_j2k->m_current_tile_number];
    l_tccp = &l_tcp->tccps[0];
    l_image = p_j2k->m_private_image;
    l_nb_comp = l_image->numcomps;
	l_nb_poc = l_tcp->numpocs + 1;
    if (l_nb_comp <= 256) {
        l_poc_room = 1;
    } else {
        l_poc_room = 2;
    }

    auto l_poc_size = getPocSize(l_nb_comp, 1 + l_tcp->numpocs);
	/* POC  */
	if (!p_stream->write_short(J2K_MS_POC, p_manager)) {
		return false;
	}

	/* Lpoc */
	if (!p_stream->write_short((uint16_t)(l_poc_size - 2), p_manager)) {
		return false;
	}
	l_current_poc = l_tcp->pocs;
	for (i = 0; i < l_nb_poc; ++i) {
		/* RSpoc_i */
		if (!p_stream->write_byte((uint8_t)l_current_poc->resno0, p_manager)) {
			return false;
		}

		/* CSpoc_i */
		if (!p_stream->write_byte((uint8_t)l_current_poc->compno0, p_manager)) {
			return false;
		}

		/* LYEpoc_i */
		if (!p_stream->write_short((uint16_t)l_current_poc->layno1, p_manager)) {
			return false;
		}

		/* REpoc_i */
		if (!p_stream->write_byte((uint8_t)l_current_poc->resno1, p_manager)) {
			return false;
		}

		/* CEpoc_i */
		if (l_poc_room == 2) {
			if (!p_stream->write_short((uint16_t)l_current_poc->compno1, p_manager)) {
				return false;
			}

		}
		else {
			if (!p_stream->write_byte((uint8_t)l_current_poc->compno1, p_manager)) {
				return false;
			}
		}

		/* Ppoc_i */
		if (!p_stream->write_byte((uint8_t)l_current_poc->prg, p_manager)) {
			return false;
		}

		/* change the value of the max layer according to the actual number of layers in the file, components and resolutions*/
		l_current_poc->layno1 = std::min<uint32_t>(l_current_poc->layno1, l_tcp->numlayers);
		l_current_poc->resno1 = std::min<uint32_t>(l_current_poc->resno1, l_tccp->numresolutions);
		l_current_poc->compno1 = std::min<uint32_t>(l_current_poc->compno1, l_nb_comp);

		++l_current_poc;
	}
	*p_data_written = l_poc_size;
	return true;
}

static uint32_t j2k_get_max_poc_size(j2k_t *p_j2k)
{
    tcp_t * l_tcp = nullptr;
    uint32_t l_nb_tiles = 0;
    uint32_t l_max_poc = 0;
    uint32_t i;

    l_tcp = p_j2k->m_cp.tcps;
    l_nb_tiles = p_j2k->m_cp.th * p_j2k->m_cp.tw;

    for (i=0; i<l_nb_tiles; ++i) {
        l_max_poc = std::max<uint32_t>(l_max_poc,l_tcp->numpocs);
        ++l_tcp;
    }

    ++l_max_poc;

    return 4 + 9 * l_max_poc;
}

static uint32_t j2k_get_max_toc_size (j2k_t *p_j2k)
{
    uint32_t i;
    uint32_t l_nb_tiles;
    uint32_t l_max = 0;
    tcp_t * l_tcp = nullptr;

    l_tcp = p_j2k->m_cp.tcps;
    l_nb_tiles = p_j2k->m_cp.tw * p_j2k->m_cp.th ;

    for (i=0; i<l_nb_tiles; ++i) {
        l_max = std::max<uint32_t>(l_max,l_tcp->m_nb_tile_parts);

        ++l_tcp;
    }

    return 12 * l_max;
}

static uint64_t j2k_get_specific_header_sizes(j2k_t *p_j2k)
{
    uint64_t l_nb_bytes = 0;
    uint32_t l_nb_comps;
    uint32_t l_coc_bytes,l_qcc_bytes;

    l_nb_comps = p_j2k->m_private_image->numcomps - 1;
    l_nb_bytes += j2k_get_max_toc_size(p_j2k);

    if (!(OPJ_IS_CINEMA(p_j2k->m_cp.rsiz))) {
        l_coc_bytes = j2k_get_max_coc_size(p_j2k);
        l_nb_bytes += l_nb_comps * l_coc_bytes;

        l_qcc_bytes = j2k_get_max_qcc_size(p_j2k);
        l_nb_bytes += l_nb_comps * l_qcc_bytes;
    }

    l_nb_bytes += j2k_get_max_poc_size(p_j2k);

    /*** DEVELOPER CORNER, Add room for your headers ***/

    return l_nb_bytes;
}

/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       p_header_data   the data contained in the POC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the POC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_poc (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    uint32_t i, l_nb_comp, l_tmp;
    opj_image_t * l_image = nullptr;
    uint32_t l_old_poc_nb, l_current_poc_nb, l_current_poc_remaining;
    uint32_t l_chunk_size, l_comp_room;

    tcp_t *l_tcp = nullptr;
    opj_poc_t *l_current_poc = nullptr;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_image = p_j2k->m_private_image;
    l_nb_comp = l_image->numcomps;
    if (l_nb_comp <= 256) {
        l_comp_room = 1;
    } else {
        l_comp_room = 2;
    }
    l_chunk_size = 5 + 2 * l_comp_room;
    l_current_poc_nb = p_header_size / l_chunk_size;
    l_current_poc_remaining = p_header_size % l_chunk_size;

    if ((l_current_poc_nb <= 0) || (l_current_poc_remaining != 0)) {
        event_msg(p_manager, EVT_ERROR, "Error reading POC marker\n");
        return false;
    }

	l_tcp = j2k_get_tcp(p_j2k);
    l_old_poc_nb = l_tcp->POC ? l_tcp->numpocs + 1 : 0;
    l_current_poc_nb += l_old_poc_nb;

    if(l_current_poc_nb >= 32) {
        event_msg(p_manager, EVT_ERROR, "Too many POCs %d\n", l_current_poc_nb);
        return false;
    }
    assert(l_current_poc_nb < 32);

    /* now poc is in use.*/
	l_tcp->POC = 1;

	l_current_poc = &l_tcp->pocs[l_old_poc_nb];
	for (i = l_old_poc_nb; i < l_current_poc_nb; ++i) {
		/* RSpoc_i */
		grok_read_bytes(p_header_data, &(l_current_poc->resno0), 1);                             
		++p_header_data;
		/* CSpoc_i */
		grok_read_bytes(p_header_data, &(l_current_poc->compno0), l_comp_room);  
		p_header_data += l_comp_room;
		/* LYEpoc_i */
		grok_read_bytes(p_header_data, &(l_current_poc->layno1), 2);                               
		/* make sure layer end is in acceptable bounds */
		l_current_poc->layno1 = std::min<uint32_t>(l_current_poc->layno1, l_tcp->numlayers);
		p_header_data += 2;
		/* REpoc_i */
		grok_read_bytes(p_header_data, &(l_current_poc->resno1), 1);                              
		++p_header_data;
		/* CEpoc_i */
		grok_read_bytes(p_header_data, &(l_current_poc->compno1), l_comp_room);   
		p_header_data += l_comp_room;
		/* Ppoc_i */
		grok_read_bytes(p_header_data, &l_tmp, 1);                                                               
		++p_header_data;
		l_current_poc->prg = (OPJ_PROG_ORDER)l_tmp;
		/* make sure comp is in acceptable bounds */
		l_current_poc->compno1 = std::min<uint32_t>(l_current_poc->compno1, l_nb_comp);
		++l_current_poc;
	}
	l_tcp->numpocs = l_current_poc_nb - 1;
	return true;
}

/**
 * Reads a CRG marker (Component registration)
 *
 * @param       p_header_data   the data contained in the TLM box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the TLM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_crg(j2k_t *p_j2k,
	uint8_t * p_header_data,
	uint32_t p_header_size,
	event_mgr_t * p_manager
)
{
	uint32_t l_nb_comp;
	
	assert(p_header_data != nullptr);
	assert(p_j2k != nullptr);
	assert(p_manager != nullptr);

	l_nb_comp = p_j2k->m_private_image->numcomps;

	if (p_header_size != l_nb_comp * 4) {
		event_msg(p_manager, EVT_ERROR, "Error reading CRG marker\n");
		return false;
	}
	uint32_t l_Xcrg_i, l_Ycrg_i;
	for	(uint32_t i = 0; i < l_nb_comp; ++i)
	{ 
			// Xcrg_i
			grok_read_bytes(p_header_data,&l_Xcrg_i,2);                             
			p_header_data+=2;
			// Xcrg_i
			grok_read_bytes(p_header_data,&l_Ycrg_i,2);                             
			p_header_data+=2;
	}
	return true;
}

/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       p_header_data   the data contained in the TLM box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the TLM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_tlm(j2k_t *p_j2k,
	uint8_t * p_header_data,
	uint32_t p_header_size,
	event_mgr_t * p_manager
)
{
	(void)p_j2k;
	uint32_t i_TLM, L;
	uint32_t L_iT, L_iTP, l_tot_num_tp_remaining, l_quotient, l_Ptlm_size;
	
	assert(p_header_data != nullptr);
	assert(p_j2k != nullptr);
	assert(p_manager != nullptr);

	if (p_header_size < 2) {
		event_msg(p_manager, EVT_ERROR, "Error reading TLM marker\n");
		return false;
	}
	p_header_size -= 2;

	grok_read_bytes(p_header_data, &i_TLM, 1);
	++p_header_data;
	grok_read_bytes(p_header_data, &L, 1);
	++p_header_data;

	// 0x70 ==  1110000
	if ((L & ~0x70) != 0) {
		event_msg(p_manager, EVT_ERROR, "Illegal L value in TLM marker\n");
		return false;
	}

    L_iT = ((L >> 4) & 0x3);	// 0 <= L_iT <= 2
	L_iTP = (L >> 6) & 0x1;		// 0 <= L_iTP <= 1

    l_Ptlm_size = (L_iTP + 1) * 2;

	l_quotient = l_Ptlm_size + L_iT;
	if (p_header_size % l_quotient != 0) {
	    event_msg(p_manager, EVT_ERROR, "Error reading TLM marker\n");
	   return false;
	}

    l_tot_num_tp_remaining = p_header_size / l_quotient;
	uint32_t l_Ttlm_i, l_Ptlm_i;
    for   (uint32_t i = 0; i < l_tot_num_tp_remaining; ++i)   {
		if (L_iT) {
			grok_read_bytes(p_header_data, &l_Ttlm_i, L_iT);
			p_header_data += L_iT;
		}
        grok_read_bytes(p_header_data,&l_Ptlm_i,l_Ptlm_size);           
        p_header_data += l_Ptlm_size;
    }
    return true;
}

/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       p_header_data   the data contained in the TLM box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the TLM marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_plm (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
	(void)p_j2k;
    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
	uint32_t l_Zplm, l_Nplm, l_tmp, l_packet_len = 0, i;
	int64_t header_size = p_header_size;
    if (header_size < 1) {
        event_msg(p_manager, EVT_ERROR, "Error reading PLM marker\n");
        return false;
    }

	// Zplm
    grok_read_bytes(p_header_data,&l_Zplm,1);                                        
    ++p_header_data;
    --header_size;

    while  (header_size > 0)   {
		// Nplm
        grok_read_bytes(p_header_data,&l_Nplm,1);                               
        ++p_header_data;
		header_size -= (1+l_Nplm);
        if (header_size < 0)     {
             event_msg(p_manager, EVT_ERROR, "Error reading PLM marker\n");
             return false;
        }
        for (i = 0; i < l_Nplm; ++i)       {
			// Iplm_ij
            grok_read_bytes(p_header_data,&l_tmp,1);                       
            ++p_header_data;
            // take only the last seven bytes
            l_packet_len |= (l_tmp & 0x7f);
            if  (l_tmp & 0x80)        {
                l_packet_len <<= 7;
            }  else   {
				// store packet length and proceed to next packet
                l_packet_len = 0;
            }
        }
        if (l_packet_len != 0)      {
            event_msg(p_manager, EVT_ERROR, "Error reading PLM marker\n");
            return false;
        }
    }
     return true;
}

/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       p_header_data   the data contained in the PLT box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the PLT marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_plt (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
	(void)p_j2k;
    uint32_t l_Zplt, l_tmp, l_packet_len = 0, i;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (p_header_size < 1) {
        event_msg(p_manager, EVT_ERROR, "Error reading PLT marker\n");
        return false;
    }

	/* Zplt */
    grok_read_bytes(p_header_data,&l_Zplt,1);               
    ++p_header_data;
    --p_header_size;

    for (i = 0; i < p_header_size; ++i) {
		/* Iplt_ij */
        grok_read_bytes(p_header_data,&l_tmp,1);        
        ++p_header_data;
        /* take only the last seven bytes */
        l_packet_len |= (l_tmp & 0x7f);
        if (l_tmp & 0x80) {
            l_packet_len <<= 7;
        } else {
            /* store packet length and proceed to next packet */
            l_packet_len = 0;
        }
    }

    if (l_packet_len != 0) {
        event_msg(p_manager, EVT_ERROR, "Error reading PLT marker\n");
        return false;
    }

    return true;
}

/**
 * Reads a PPM marker (Packed packet headers, main header)
 *
 * @param       p_header_data   the data contained in the POC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the POC marker.
 * @param       p_manager               the user event manager.
 */

static bool j2k_read_ppm (
    j2k_t *p_j2k,
    uint8_t * p_header_data,
    uint32_t p_header_size,
    event_mgr_t * p_manager )
{
    cp_t *l_cp = nullptr;
    uint32_t l_Z_ppm;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    /* We need to have the Z_ppm element + 1 byte of Nppm/Ippm at minimum */
    if (p_header_size < 2) {
        event_msg(p_manager, EVT_ERROR, "Error reading PPM marker\n");
        return false;
    }

    l_cp = &(p_j2k->m_cp);
    l_cp->ppm = 1;

	/* Z_ppm */
    grok_read_bytes(p_header_data,&l_Z_ppm,1);             
    ++p_header_data;
    --p_header_size;

    /* check allocation needed */
    if (l_cp->ppm_markers == nullptr) { /* first PPM marker */
        uint32_t l_newCount = l_Z_ppm + 1U; /* can't overflow, l_Z_ppm is UINT8 */
        assert(l_cp->ppm_markers_count == 0U);

        l_cp->ppm_markers = (ppx_t *) grok_calloc(l_newCount, sizeof(ppx_t));
        if (l_cp->ppm_markers == nullptr) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPM marker\n");
            return false;
        }
        l_cp->ppm_markers_count = l_newCount;
    } else if (l_cp->ppm_markers_count <= l_Z_ppm) {
        uint32_t l_newCount = l_Z_ppm + 1U; /* can't overflow, l_Z_ppm is UINT8 */
        ppx_t *new_ppm_markers;
        new_ppm_markers = (ppx_t *) grok_realloc(l_cp->ppm_markers, l_newCount * sizeof(ppx_t));
        if (new_ppm_markers == nullptr) {
            /* clean up to be done on l_cp destruction */
            event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPM marker\n");
            return false;
        }
        l_cp->ppm_markers = new_ppm_markers;
        memset(l_cp->ppm_markers + l_cp->ppm_markers_count, 0, (l_newCount - l_cp->ppm_markers_count) * sizeof(ppx_t));
        l_cp->ppm_markers_count = l_newCount;
    }

    if (l_cp->ppm_markers[l_Z_ppm].m_data != nullptr) {
        /* clean up to be done on l_cp destruction */
        event_msg(p_manager, EVT_ERROR, "Zppm %u already read\n", l_Z_ppm);
        return false;
    }

    l_cp->ppm_markers[l_Z_ppm].m_data = (uint8_t *) grok_malloc(p_header_size);
    if (l_cp->ppm_markers[l_Z_ppm].m_data == nullptr) {
        /* clean up to be done on l_cp destruction */
        event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPM marker\n");
        return false;
    }
    l_cp->ppm_markers[l_Z_ppm].m_data_size = p_header_size;
    memcpy(l_cp->ppm_markers[l_Z_ppm].m_data, p_header_data, p_header_size);

    return true;
}

/**
 * Merges all PPM markers read (Packed headers, main header)
 *
 * @param       p_cp      main coding parameters.
 * @param       p_manager the user event manager.
 */
static bool j2k_merge_ppm ( cp_t *p_cp, event_mgr_t * p_manager )
{
    uint32_t i, l_ppm_data_size, l_N_ppm_remaining;

    
    assert(p_cp != nullptr);
    assert(p_manager != nullptr);
    assert(p_cp->ppm_buffer == nullptr);

    if (p_cp->ppm == 0U) {
        return true;
    }

    l_ppm_data_size = 0U;
    l_N_ppm_remaining = 0U;
    for (i = 0U; i < p_cp->ppm_markers_count; ++i) {
        if (p_cp->ppm_markers[i].m_data != nullptr) { /* standard doesn't seem to require contiguous Zppm */
            uint32_t l_N_ppm;
            uint32_t l_data_size = p_cp->ppm_markers[i].m_data_size;
            const uint8_t* l_data = p_cp->ppm_markers[i].m_data;

            if (l_N_ppm_remaining >= l_data_size) {
                l_N_ppm_remaining -= l_data_size;
                l_data_size = 0U;
            } else {
                l_data += l_N_ppm_remaining;
                l_data_size -= l_N_ppm_remaining;
                l_N_ppm_remaining = 0U;
            }

            if (l_data_size > 0U) {
                do {
                    /* read Nppm */
                    if (l_data_size < 4U) {
                        /* clean up to be done on l_cp destruction */
                        event_msg(p_manager, EVT_ERROR, "Not enough bytes to read Nppm\n");
                        return false;
                    }
                    grok_read_bytes(l_data, &l_N_ppm, 4);
                    l_data+=4;
                    l_data_size-=4;
                    l_ppm_data_size += l_N_ppm; /* can't overflow, max 256 markers of max 65536 bytes, that is when PPM markers are not corrupted which is checked elsewhere */

                    if (l_data_size >= l_N_ppm) {
                        l_data_size -= l_N_ppm;
                        l_data += l_N_ppm;
                    } else {
                        l_N_ppm_remaining = l_N_ppm - l_data_size;
                        l_data_size = 0U;
                    }
                } while (l_data_size > 0U);
            }
        }
    }

    if (l_N_ppm_remaining != 0U) {
        /* clean up to be done on l_cp destruction */
        event_msg(p_manager, EVT_ERROR, "Corrupted PPM markers\n");
        return false;
    }

    p_cp->ppm_buffer = (uint8_t *) grok_malloc(l_ppm_data_size);
    if (p_cp->ppm_buffer == nullptr) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPM marker\n");
        return false;
    }
    p_cp->ppm_len = l_ppm_data_size;
    l_ppm_data_size = 0U;
    l_N_ppm_remaining = 0U;
    for (i = 0U; i < p_cp->ppm_markers_count; ++i) {
        if (p_cp->ppm_markers[i].m_data != nullptr) { /* standard doesn't seem to require contiguous Zppm */
            uint32_t l_N_ppm;
            uint32_t l_data_size = p_cp->ppm_markers[i].m_data_size;
            const uint8_t* l_data = p_cp->ppm_markers[i].m_data;

            if (l_N_ppm_remaining >= l_data_size) {
                memcpy(p_cp->ppm_buffer + l_ppm_data_size, l_data, l_data_size);
                l_ppm_data_size += l_data_size;
                l_N_ppm_remaining -= l_data_size;
                l_data_size = 0U;
            } else {
                memcpy(p_cp->ppm_buffer + l_ppm_data_size, l_data, l_N_ppm_remaining);
                l_ppm_data_size += l_N_ppm_remaining;
                l_data += l_N_ppm_remaining;
                l_data_size -= l_N_ppm_remaining;
                l_N_ppm_remaining = 0U;
            }

            if (l_data_size > 0U) {
                do {
                    /* read Nppm */
                    if (l_data_size < 4U) {
                        /* clean up to be done on l_cp destruction */
                        event_msg(p_manager, EVT_ERROR, "Not enough bytes to read Nppm\n");
                        return false;
                    }
                    grok_read_bytes(l_data, &l_N_ppm, 4);
                    l_data+=4;
                    l_data_size-=4;

                    if (l_data_size >= l_N_ppm) {
                        memcpy(p_cp->ppm_buffer + l_ppm_data_size, l_data, l_N_ppm);
                        l_ppm_data_size += l_N_ppm;
                        l_data_size -= l_N_ppm;
                        l_data += l_N_ppm;
                    } else {
                        memcpy(p_cp->ppm_buffer + l_ppm_data_size, l_data, l_data_size);
                        l_ppm_data_size += l_data_size;
                        l_N_ppm_remaining = l_N_ppm - l_data_size;
                        l_data_size = 0U;
                    }
                } while (l_data_size > 0U);
            }
            grok_free(p_cp->ppm_markers[i].m_data);
            p_cp->ppm_markers[i].m_data = nullptr;
            p_cp->ppm_markers[i].m_data_size = 0U;
        }
    }

    p_cp->ppm_data = p_cp->ppm_buffer;
    p_cp->ppm_data_size = p_cp->ppm_len;

    p_cp->ppm_markers_count = 0U;
    grok_free(p_cp->ppm_markers);
    p_cp->ppm_markers = nullptr;

    return true;
}

/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       p_header_data   the data contained in the PPT box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the PPT marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_ppt (  j2k_t *p_j2k,
                                uint8_t * p_header_data,
                                uint32_t p_header_size,
                                event_mgr_t * p_manager
                             )
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    uint32_t l_Z_ppt;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    /* We need to have the Z_ppt element + 1 byte of Ippt at minimum */
    if (p_header_size < 2) {
        event_msg(p_manager, EVT_ERROR, "Error reading PPT marker\n");
        return false;
    }

    l_cp = &(p_j2k->m_cp);
    if (l_cp->ppm) {
        event_msg(p_manager, EVT_ERROR, "Error reading PPT marker: packet header have been previously found in the main header (PPM marker).\n");
        return false;
    }

    l_tcp = &(l_cp->tcps[p_j2k->m_current_tile_number]);
    l_tcp->ppt = 1;

	/* Z_ppt */
    grok_read_bytes(p_header_data,&l_Z_ppt,1);             
    ++p_header_data;
    --p_header_size;

    /* check allocation needed */
    if (l_tcp->ppt_markers == nullptr) { /* first PPT marker */
        uint32_t l_newCount = l_Z_ppt + 1U; /* can't overflow, l_Z_ppt is UINT8 */
        assert(l_tcp->ppt_markers_count == 0U);

        l_tcp->ppt_markers = (ppx_t *) grok_calloc(l_newCount, sizeof(ppx_t));
        if (l_tcp->ppt_markers == nullptr) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPT marker\n");
            return false;
        }
        l_tcp->ppt_markers_count = l_newCount;
    } else if (l_tcp->ppt_markers_count <= l_Z_ppt) {
        uint32_t l_newCount = l_Z_ppt + 1U; /* can't overflow, l_Z_ppt is UINT8 */
        ppx_t *new_ppt_markers;
        new_ppt_markers = (ppx_t *) grok_realloc(l_tcp->ppt_markers, l_newCount * sizeof(ppx_t));
        if (new_ppt_markers == nullptr) {
            /* clean up to be done on l_tcp destruction */
            event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPT marker\n");
            return false;
        }
        l_tcp->ppt_markers = new_ppt_markers;
        memset(l_tcp->ppt_markers + l_tcp->ppt_markers_count, 0, (l_newCount - l_tcp->ppt_markers_count) * sizeof(ppx_t));
        l_tcp->ppt_markers_count = l_newCount;
    }

    if (l_tcp->ppt_markers[l_Z_ppt].m_data != nullptr) {
        /* clean up to be done on l_tcp destruction */
        event_msg(p_manager, EVT_ERROR, "Zppt %u already read\n", l_Z_ppt);
        return false;
    }

    l_tcp->ppt_markers[l_Z_ppt].m_data = (uint8_t *) grok_malloc(p_header_size);
    if (l_tcp->ppt_markers[l_Z_ppt].m_data == nullptr) {
        /* clean up to be done on l_tcp destruction */
        event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPT marker\n");
        return false;
    }
    l_tcp->ppt_markers[l_Z_ppt].m_data_size = p_header_size;
    memcpy(l_tcp->ppt_markers[l_Z_ppt].m_data, p_header_data, p_header_size);
    return true;
}

/**
 * Merges all PPT markers read (Packed packet headers, tile-part header)
 *
 * @param       p_tcp   the tile.
 * @param       p_manager               the user event manager.
 */
static bool j2k_merge_ppt(tcp_t *p_tcp, event_mgr_t * p_manager)
{
    uint32_t i, l_ppt_data_size;
    
    assert(p_tcp != nullptr);
    assert(p_manager != nullptr);
    assert(p_tcp->ppt_buffer == nullptr);

    if (p_tcp->ppt == 0U) {
        return true;
    }

    l_ppt_data_size = 0U;
    for (i = 0U; i < p_tcp->ppt_markers_count; ++i) {
        l_ppt_data_size += p_tcp->ppt_markers[i].m_data_size; /* can't overflow, max 256 markers of max 65536 bytes */
    }

    p_tcp->ppt_buffer = (uint8_t *) grok_malloc(l_ppt_data_size);
    if (p_tcp->ppt_buffer == nullptr) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to read PPT marker\n");
        return false;
    }
    p_tcp->ppt_len = l_ppt_data_size;
    l_ppt_data_size = 0U;
    for (i = 0U; i < p_tcp->ppt_markers_count; ++i) {
        if (p_tcp->ppt_markers[i].m_data != nullptr) { /* standard doesn't seem to require contiguous Zppt */
            memcpy(p_tcp->ppt_buffer + l_ppt_data_size, p_tcp->ppt_markers[i].m_data, p_tcp->ppt_markers[i].m_data_size);
            l_ppt_data_size += p_tcp->ppt_markers[i].m_data_size; /* can't overflow, max 256 markers of max 65536 bytes */

            grok_free(p_tcp->ppt_markers[i].m_data);
            p_tcp->ppt_markers[i].m_data = nullptr;
            p_tcp->ppt_markers[i].m_data_size = 0U;
        }
    }

    p_tcp->ppt_markers_count = 0U;
    grok_free(p_tcp->ppt_markers);
    p_tcp->ppt_markers = nullptr;

    p_tcp->ppt_data = p_tcp->ppt_buffer;
    p_tcp->ppt_data_size = p_tcp->ppt_len;
    return true;
}

static bool j2k_write_tlm(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager
                             )
{
    uint32_t l_tlm_size;
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_tlm_size = 6 + (5*p_j2k->m_specific_param.m_encoder.m_total_tile_parts);

	/* change the way data is written to avoid seeking if possible */
    /* TODO */
    p_j2k->m_specific_param.m_encoder.m_tlm_start = p_stream->tell();

	/* TLM */
	if (!p_stream->write_short(J2K_MS_TLM, p_manager)) {
		return false;
	}

	/* Lpoc */
	if (!p_stream->write_short((uint16_t)(l_tlm_size - 2), p_manager)) {
		return false;
	}

	/* Ztlm=0*/
	if (!p_stream->write_byte(0, p_manager)) {
		return false;
	}

	/* Stlm ST=1(8bits-255 tiles max),SP=1(Ptlm=32bits) */
	if (!p_stream->write_byte(0x50, p_manager)) {
		return false;
	}
	/* do nothing on the 5 * l_j2k->m_specific_param.m_encoder.m_total_tile_parts remaining data */
	if (!p_stream->skip((5 * p_j2k->m_specific_param.m_encoder.m_total_tile_parts), p_manager)) {
		return false;
	}
    return true;
}

static bool j2k_write_sot(j2k_t *p_j2k,
						GrokStream *p_stream,
						uint64_t* psot_location,
                        uint64_t * p_data_written,
                        event_mgr_t * p_manager)
{
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

	/* SOT */
	if (!p_stream->write_short(J2K_MS_SOT, p_manager)) {
		return false;
	}

	/* Lsot */
	if (!p_stream->write_short(10, p_manager)) {
		return false;
	}

	/* Isot */
	if (!p_stream->write_short((uint16_t)p_j2k->m_current_tile_number, p_manager)) {
		return false;
	}

	/* Psot  */
	*psot_location = p_stream->tell();
	if (!p_stream->skip(4, p_manager)) {
		return false;
	}


	/* TPsot */
	if (!p_stream->write_byte((uint8_t)p_j2k->m_specific_param.m_encoder.m_current_tile_part_number, p_manager)) {
		return false;
	}

	/* TNsot */
	if (!p_stream->write_byte((uint8_t)p_j2k->m_cp.tcps[p_j2k->m_current_tile_number].m_nb_tile_parts, p_manager)) {
		return false;
	}

	*p_data_written += 12;
	return true;

}

static bool j2k_get_sot_values(uint8_t *  p_header_data,
                                   uint32_t  p_header_size,
                                   uint32_t* p_tile_no,
									uint32_t* p_tot_len,
                                   uint32_t* p_current_part,
                                   uint32_t* p_num_parts,
                                   event_mgr_t * p_manager )
{
    
    assert(p_header_data != nullptr);
    assert(p_manager != nullptr);

    /* Size of this marker is fixed = 12 (we have already read marker and its size)*/
    if (p_header_size != 8) {
        event_msg(p_manager, EVT_ERROR, "Error reading SOT marker\n");
        return false;
    }
	/* Isot */
    grok_read_bytes(p_header_data,p_tile_no,2);      
    p_header_data+=2;
	/* Psot */
    grok_read_bytes(p_header_data,p_tot_len,4);
    p_header_data+=4;
	/* TPsot */
    grok_read_bytes(p_header_data,p_current_part,1);
    ++p_header_data;
	/* TNsot */
    grok_read_bytes(p_header_data,p_num_parts ,1);  
    ++p_header_data;
    return true;
}

static bool j2k_read_sot ( j2k_t *p_j2k,
                               uint8_t * p_header_data,
                               uint32_t p_header_size,
                               event_mgr_t * p_manager )
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
	uint32_t l_tot_len = 0;
	uint32_t l_num_parts = 0;
    uint32_t l_current_part;
    uint32_t l_tile_x,l_tile_y;

    

    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! j2k_get_sot_values(p_header_data, p_header_size, &(p_j2k->m_current_tile_number), &l_tot_len, &l_current_part, &l_num_parts, p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error reading SOT marker\n");
        return false;
    }

    l_cp = &(p_j2k->m_cp);

    /* testcase 2.pdf.SIGFPE.706.1112 */
    if (p_j2k->m_current_tile_number >= l_cp->tw * l_cp->th) {
        event_msg(p_manager, EVT_ERROR, "Invalid tile number %d\n", p_j2k->m_current_tile_number);
        return false;
    }

    l_tcp = &l_cp->tcps[p_j2k->m_current_tile_number];
    l_tile_x = p_j2k->m_current_tile_number % l_cp->tw;
    l_tile_y = p_j2k->m_current_tile_number / l_cp->tw;

	/* Fixes issue with id_000020,sig_06,src_001958,op_flip4,pos_149 */
	/* of https://github.com/uclouvain/openjpeg/issues/939 */
	/* We must avoid reading the same tile part number twice for a given tile */
	/* to avoid various issues, like opj_j2k_merge_ppt being called several times. */
	/* ISO 15444-1 A.4.2 Start of tile-part (SOT) mandates that tile parts */
	/* should appear in increasing order. */
	if (l_tcp->m_current_tile_part_number + 1 != (int32_t)l_current_part) {
		event_msg(p_manager, EVT_ERROR,
			"Invalid tile part index for tile number %d. "
			"Got %d, expected %d\n",
			p_j2k->m_current_tile_number,
			l_current_part,
			l_tcp->m_current_tile_part_number + 1);
		return false;
	}
	++l_tcp->m_current_tile_part_number;
    /* look for the tile in the list of already processed tile (in parts). */
    /* Optimization possible here with a more complex data structure and with the removing of tiles */
    /* since the time taken by this function can only grow at the time */

    /* PSot should be equal to zero or >=14 or <= 2^32-1 */
    if ((l_tot_len !=0 ) && (l_tot_len < 14) ) {
        if (l_tot_len == 12 ) { /* special case for the PHR data which are read by kakadu*/
            event_msg(p_manager, EVT_WARNING, "Empty SOT marker detected: Psot=%d.\n", l_tot_len);
        } else {
            event_msg(p_manager, EVT_ERROR, "Psot value is not correct regards to the JPEG2000 norm: %d.\n", l_tot_len);
            return false;
        }
    }


    /* Ref A.4.2: Psot may equal zero if it is the last tile-part of the codestream.*/
    if (!l_tot_len) {
		//event_msg(p_manager, EVT_WARNING, "Psot value of the current tile-part is equal to zero; "
        //              "we assume it is the last tile-part of the codestream.\n");
        p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
    }

	// ensure that current tile part number read from SOT marker
	// is not larger than total number of tile parts
	if (l_tcp->m_nb_tile_parts != 0 && l_current_part >= l_tcp->m_nb_tile_parts) {
		/* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
		event_msg(p_manager, EVT_ERROR,
			"Current tile part number (%d) read from SOT marker is greater than total "
			"number of tile-parts (%d).\n", l_current_part, 
			l_tcp->m_nb_tile_parts);
		p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
		return OPJ_FALSE;
	}


    if (l_num_parts != 0) { /* Number of tile-part header is provided by this tile-part header */
        l_num_parts += p_j2k->m_specific_param.m_decoder.m_nb_tile_parts_correction;
        /* Useful to manage the case of textGBR.jp2 file because two values of TNSot are allowed: the correct numbers of
         * tile-parts for that tile and zero (A.4.2 of 15444-1 : 2002). */
        if (l_tcp->m_nb_tile_parts) {
            if (l_current_part >= l_tcp->m_nb_tile_parts) {
                event_msg(p_manager, EVT_ERROR, "In SOT marker, TPSot (%d) is not valid regards to the current "
                              "number of tile-part (%d), giving up\n", l_current_part, l_tcp->m_nb_tile_parts );
                p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
                return false;
            }
        }
        if( l_current_part >= l_num_parts ) {
            /* testcase 451.pdf.SIGSEGV.ce9.3723 */
            event_msg(p_manager, EVT_ERROR, "In SOT marker, TPSot (%d) is not valid regards to the current "
                          "number of tile-part (header) (%d), giving up\n", l_current_part, l_num_parts );
            p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
            return false;
        }
        l_tcp->m_nb_tile_parts = l_num_parts;
    }

    /* If know the number of tile part header we will check if we didn't read the last*/
    if (l_tcp->m_nb_tile_parts) {
        if (l_tcp->m_nb_tile_parts == (l_current_part+1)) {
            p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data = 1; /* Process the last tile-part header*/
        }
    }

    if (!p_j2k->m_specific_param.m_decoder.m_last_tile_part) {
        /* Keep the size of data to skip after this marker */
        p_j2k->m_specific_param.m_decoder.tile_part_data_length = l_tot_len - 12; /* SOT marker size = 12 */
    } else {
        /* FIXME: need to be computed from the number of bytes remaining in the codestream */
        p_j2k->m_specific_param.m_decoder.tile_part_data_length = 0;
    }

    p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_TPH;

    /* Check if the current tile is outside the area we want decode or not corresponding to the tile index*/
    if (p_j2k->m_specific_param.m_decoder.m_tile_ind_to_dec == -1) {
        p_j2k->m_specific_param.m_decoder.m_skip_data =
            (l_tile_x < p_j2k->m_specific_param.m_decoder.m_start_tile_x)
            ||      (l_tile_x >= p_j2k->m_specific_param.m_decoder.m_end_tile_x)
            ||  (l_tile_y < p_j2k->m_specific_param.m_decoder.m_start_tile_y)
            ||      (l_tile_y >= p_j2k->m_specific_param.m_decoder.m_end_tile_y);
    } else {
        assert( p_j2k->m_specific_param.m_decoder.m_tile_ind_to_dec >= 0 );
        p_j2k->m_specific_param.m_decoder.m_skip_data =
            (p_j2k->m_current_tile_number != (uint32_t)p_j2k->m_specific_param.m_decoder.m_tile_ind_to_dec);
    }

    /* Index */
    if (p_j2k->cstr_index) {
        assert(p_j2k->cstr_index->tile_index != nullptr);
        p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tileno = p_j2k->m_current_tile_number;
        p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_tpsno = l_current_part;

        if (l_num_parts != 0) {
            p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].nb_tps = l_num_parts;
            p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps = l_num_parts;

            if (!p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index) {
                p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index =
                    (opj_tp_index_t*)grok_calloc(l_num_parts, sizeof(opj_tp_index_t));
                if (!p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index) {
                    event_msg(p_manager, EVT_ERROR, "Not enough memory to read SOT marker. Tile index allocation failed\n");
                    return false;
                }
            } else {
                opj_tp_index_t *new_tp_index = (opj_tp_index_t *) grok_realloc(
                                                   p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index, l_num_parts* sizeof(opj_tp_index_t));
                if (! new_tp_index) {
                    grok_free(p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index);
                    p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index = nullptr;
                    event_msg(p_manager, EVT_ERROR, "Not enough memory to read SOT marker. Tile index allocation failed\n");
                    return false;
                }
                p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index = new_tp_index;
            }
        } else {
            /*if (!p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index)*/ {

                if (!p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index) {
                    p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps = 10;
                    p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index =
                        (opj_tp_index_t*)grok_calloc( p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps,
                                                     sizeof(opj_tp_index_t));
                    if (!p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index) {
                        p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps = 0;
                        event_msg(p_manager, EVT_ERROR, "Not enough memory to read SOT marker. Tile index allocation failed\n");
                        return false;
                    }
                }

                if ( l_current_part >= p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps ) {
                    opj_tp_index_t *new_tp_index;
                    p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps = l_current_part + 1;
                    new_tp_index = (opj_tp_index_t *) grok_realloc(
                                       p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index,
                                       p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps * sizeof(opj_tp_index_t));
                    if (! new_tp_index) {
                        grok_free(p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index);
                        p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index = nullptr;
                        p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].current_nb_tps = 0;
                        event_msg(p_manager, EVT_ERROR, "Not enough memory to read SOT marker. Tile index allocation failed\n");
                        return false;
                    }
                    p_j2k->cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index = new_tp_index;
                }
            }

        }

    }
    return true;
}

static bool j2k_write_sod(     j2k_t *p_j2k,
                                   tcd_t * p_tile_coder,
                                   uint64_t * p_data_written,
                                   uint64_t p_total_data_size,
                                   GrokStream* p_stream,
                                   event_mgr_t * p_manager  )
{
    opj_codestream_info_t *l_cstr_info = nullptr;
    uint64_t l_remaining_data;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

	/* SOD */
	if (!p_stream->write_short(J2K_MS_SOD, p_manager)) {
		return false;
	}

	*p_data_written = 2;

    /* make room for the EOF marker */
    l_remaining_data =  p_total_data_size - 4;

    /* update tile coder */
    p_tile_coder->tp_num = p_j2k->m_specific_param.m_encoder.m_current_poc_tile_part_number ;
    p_tile_coder->cur_tp_num = p_j2k->m_specific_param.m_encoder.m_current_tile_part_number;

    if (p_j2k->m_specific_param.m_encoder.m_current_tile_part_number == 0) {
        p_tile_coder->tile->packno = 0;
        if (l_cstr_info) {
            l_cstr_info->packno = 0;
        }
    }
    if (! tcd_encode_tile(p_tile_coder,
								p_j2k->m_current_tile_number,
								p_stream,
								p_data_written,
								l_remaining_data,
								l_cstr_info,
								p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Cannot encode tile\n");
        return false;
    }


    return true;
}

static bool j2k_read_sod (j2k_t *p_j2k,
                              GrokStream *p_stream,
                              event_mgr_t * p_manager){
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

	// note: we subtract 2 to account for SOD marker
	tcp_t * l_tcp = &(p_j2k->m_cp.tcps[p_j2k->m_current_tile_number]);
    if (p_j2k->m_specific_param.m_decoder.m_last_tile_part) {
        p_j2k->m_specific_param.m_decoder.tile_part_data_length = (uint64_t)(p_stream->get_number_byte_left() - 2);
    } else {
        if (p_j2k->m_specific_param.m_decoder.tile_part_data_length >= 2 )
            p_j2k->m_specific_param.m_decoder.tile_part_data_length -= 2;
    }
    if (p_j2k->m_specific_param.m_decoder.tile_part_data_length) {
		auto bytesLeftInStream = p_stream->get_number_byte_left();
        // check that there are enough bytes in stream to fill tile data
        if ((int64_t)p_j2k->m_specific_param.m_decoder.tile_part_data_length > bytesLeftInStream) {
            event_msg(p_manager, 
						EVT_WARNING, 
						"Tile part length size %lld inconsistent with stream length %lld\n", 
						p_j2k->m_specific_param.m_decoder.tile_part_data_length, 
						p_stream->get_number_byte_left());

			// sanitize tile_part_data_length
			p_j2k->m_specific_param.m_decoder.tile_part_data_length = bytesLeftInStream;
        }
    } 
    /* Index */
	opj_codestream_index_t* l_cstr_index = p_j2k->cstr_index;
    if (l_cstr_index) {
        int64_t l_current_pos = p_stream->tell() - 2;

        uint32_t l_current_tile_part = l_cstr_index->tile_index[p_j2k->m_current_tile_number].current_tpsno;
        l_cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index[l_current_tile_part].end_header =
            l_current_pos;
        l_cstr_index->tile_index[p_j2k->m_current_tile_number].tp_index[l_current_tile_part].end_pos =
            l_current_pos + p_j2k->m_specific_param.m_decoder.tile_part_data_length + 2;

        if (false == j2k_add_tlmarker(p_j2k->m_current_tile_number,
                                          l_cstr_index,
                                          J2K_MS_SOD,
                                          l_current_pos,
                                          (uint32_t)(p_j2k->m_specific_param.m_decoder.tile_part_data_length + 2))) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to add tl marker\n");
            return false;
        }

        /*l_cstr_index->packno = 0;*/
    }
	size_t l_current_read_size = 0;
    if (p_j2k->m_specific_param.m_decoder.tile_part_data_length) {
		if (!l_tcp->m_tile_data)
			l_tcp->m_tile_data = new seg_buf_t();

		auto len = p_j2k->m_specific_param.m_decoder.tile_part_data_length;
		uint8_t* buff = nullptr;
		auto zeroCopy = p_stream->supportsZeroCopy();
		if (!zeroCopy) {
			try {
				buff = new uint8_t[len];
			}
			catch (std::bad_alloc) {
				event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate segment\n");
				return false;
			}
		} else {
			buff = p_stream->getCurrentPtr();
		}
        l_current_read_size = p_stream->read(zeroCopy ? nullptr : buff,
											len,
											p_manager);
		l_tcp->m_tile_data->add_segment(buff,len,!zeroCopy);

     } 
    if (l_current_read_size != p_j2k->m_specific_param.m_decoder.tile_part_data_length) {
        p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_NEOC;
    } else {
        p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_TPHSOT;
    }
    return true;
}

static bool j2k_write_rgn(j2k_t *p_j2k,
                              uint32_t p_tile_no,
                              uint32_t p_comp_no,
                              uint32_t nb_comps,
                              GrokStream *p_stream,
                              event_mgr_t * p_manager
                             )
{
    uint32_t l_rgn_size;
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;
    uint32_t l_comp_room;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp = &l_tcp->tccps[p_comp_no];

    if (nb_comps <= 256) {
        l_comp_room = 1;
    } else {
        l_comp_room = 2;
    }
    l_rgn_size = 6 + l_comp_room;

	/* RGN  */
	if (!p_stream->write_short(J2K_MS_RGN, p_manager)) {
		return false;
	}

	/* Lrgn */
	if (!p_stream->write_short((uint16_t)(l_rgn_size - 2), p_manager)) {
		return false;
	}

	/* Crgn */
	if (l_comp_room == 2) {
		if (!p_stream->write_short((uint16_t)p_comp_no, p_manager)) {
			return false;
		}
	}
	else {
		if (!p_stream->write_byte((uint8_t)p_comp_no, p_manager)) {
			return false;
		}
	}

	/* Srgn */
	if (!p_stream->write_byte(0, p_manager)) {
		return false;
	}

	/* SPrgn */
	if (!p_stream->write_byte((uint8_t)l_tccp->roishift, p_manager)) {
		return false;
	}
    return true;
}

static bool j2k_write_eoc(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager)
{
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);
	(void)p_j2k;
	if (!p_stream->write_short(J2K_MS_EOC, p_manager)) {
		return false;
	}
    if ( ! p_stream->flush(p_manager) ) {
        return false;
    }
    return true;
}

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       p_header_data   the data contained in the POC box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the POC marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_rgn (j2k_t *p_j2k,
                              uint8_t * p_header_data,
                              uint32_t p_header_size,
                              event_mgr_t * p_manager
                             )
{
    uint32_t l_nb_comp;
    opj_image_t * l_image = nullptr;

    tcp_t *l_tcp = nullptr;
    uint32_t l_comp_room, l_comp_no, l_roi_sty;

    /* preconditions*/
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_image = p_j2k->m_private_image;
    l_nb_comp = l_image->numcomps;

    if (l_nb_comp <= 256) {
        l_comp_room = 1;
    } else {
        l_comp_room = 2;
    }

    if (p_header_size != 2 + l_comp_room) {
        event_msg(p_manager, EVT_ERROR, "Error reading RGN marker\n");
        return false;
    }

	l_tcp = j2k_get_tcp(p_j2k);

	/* Crgn */
    grok_read_bytes(p_header_data,&l_comp_no,l_comp_room);          
    p_header_data+=l_comp_room;
	/* Srgn */
    grok_read_bytes(p_header_data,&l_roi_sty,1);                    
    ++p_header_data;

    /* testcase 3635.pdf.asan.77.2930 */
    if (l_comp_no >= l_nb_comp) {
        event_msg(p_manager, EVT_ERROR,
                      "bad component number in RGN (%d when there are only %d)\n",
                      l_comp_no, l_nb_comp);
        return false;
    }

	/* SPrgn */
    grok_read_bytes(p_header_data,(uint32_t *) (&(l_tcp->tccps[l_comp_no].roishift)),1);  
    ++p_header_data;

    return true;

}

static float j2k_get_tp_stride (tcp_t * p_tcp)
{
    return (float) ((p_tcp->m_nb_tile_parts - 1) * 14);
}

static float j2k_get_default_stride (tcp_t * p_tcp)
{
    (void)p_tcp;
    return 0;
}

static bool j2k_update_rates(  j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
	(void)p_manager;
    cp_t * l_cp = nullptr;
    opj_image_t * l_image = nullptr;
    tcp_t * l_tcp = nullptr;

    uint32_t i,j,k;
    uint32_t l_x0,l_y0,l_x1,l_y1;
    double * l_rates = 0;
    double l_sot_remove;
    uint32_t l_bits_empty, l_size_pixel;
    uint32_t l_last_res;
    float (* l_tp_stride_func)(tcp_t *) = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_image = p_j2k->m_private_image;
    l_tcp = l_cp->tcps;

    l_bits_empty = 8 * l_image->comps->dx * l_image->comps->dy;
    l_size_pixel = l_image->numcomps * l_image->comps->prec;
    l_sot_remove = (double) p_stream->tell() / (l_cp->th * l_cp->tw);

    if (l_cp->m_specific_param.m_enc.m_tp_on) {
        l_tp_stride_func = j2k_get_tp_stride;
    } else {
        l_tp_stride_func = j2k_get_default_stride;
    }

    for (i=0; i<l_cp->th; ++i) {
        for (j=0; j<l_cp->tw; ++j) {
            double l_offset = (double)(*l_tp_stride_func)(l_tcp) / l_tcp->numlayers;

            /* 4 borders of the tile rescale on the image if necessary */
            l_x0 = std::max<uint32_t>((l_cp->tx0 + j * l_cp->tdx), l_image->x0);
            l_y0 = std::max<uint32_t>((l_cp->ty0 + i * l_cp->tdy), l_image->y0);
            l_x1 = std::min<uint32_t>((l_cp->tx0 + (j + 1) * l_cp->tdx), l_image->x1);
            l_y1 = std::min<uint32_t>((l_cp->ty0 + (i + 1) * l_cp->tdy), l_image->y1);

            l_rates = l_tcp->rates;
            for (k = 0; k < l_tcp->numlayers; ++k) {
                if (*l_rates > 0.0f) {
                    *l_rates =  ((((double)l_size_pixel * (l_x1 - l_x0) * (l_y1 - l_y0)))
                                / ((*l_rates) * l_bits_empty)) - l_offset;
                }
                ++l_rates;
            }
            ++l_tcp;
        }
    }
    l_tcp = l_cp->tcps;

    for (i=0; i<l_cp->th; ++i) {
        for (j=0; j<l_cp->tw; ++j) {
            l_rates = l_tcp->rates;

            if (*l_rates > 0.0) {
                *l_rates -= l_sot_remove;

                if (*l_rates < 30.0f) {
                    *l_rates = 30.0f;
                }
            }
            ++l_rates;
			l_last_res = l_tcp->numlayers - 1;

            for (k = 1; k < l_last_res; ++k) {

                if (*l_rates > 0.0) {
                    *l_rates -= l_sot_remove;

                    if (*l_rates < *(l_rates - 1) + 10.0) {
                        *l_rates  = (*(l_rates - 1)) + 20.0;
                    }
                }
                ++l_rates;
            }

            if (*l_rates > 0.0) {
                *l_rates -= (l_sot_remove + 2.0);
                if (*l_rates < *(l_rates - 1) + 10.0) {
                    *l_rates  = (*(l_rates - 1)) + 20.0;
                }
            }
            ++l_tcp;
        }
    }


    if (OPJ_IS_CINEMA(l_cp->rsiz)) {
        p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer =
            (uint8_t *) grok_malloc(5*p_j2k->m_specific_param.m_encoder.m_total_tile_parts);
        if (! p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer) {
            return false;
        }

        p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current =
            p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer;
    }
    return true;
}

static bool j2k_get_end_header(j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
	(void)p_manager;
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    p_j2k->cstr_index->main_head_end = p_stream->tell();

    return true;
}

static bool j2k_write_mct_data_group(  j2k_t *p_j2k,
         GrokStream *p_stream,
		event_mgr_t * p_manager )
{
    uint32_t i;
    simple_mcc_decorrelation_data_t * l_mcc_record;
    mct_data_t * l_mct_record;
    tcp_t * l_tcp;

    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    if (! j2k_write_cbd(p_j2k,p_stream,p_manager)) {
        return false;
    }

    l_tcp = &(p_j2k->m_cp.tcps[p_j2k->m_current_tile_number]);
    l_mct_record = l_tcp->m_mct_records;

    for (i=0; i<l_tcp->m_nb_mct_records; ++i) {

        if (! j2k_write_mct_record(l_mct_record,p_stream,p_manager)) {
            return false;
        }

        ++l_mct_record;
    }

    l_mcc_record = l_tcp->m_mcc_records;

    for     (i=0; i<l_tcp->m_nb_mcc_records; ++i) {

        if (! j2k_write_mcc_record(l_mcc_record,p_stream,p_manager)) {
            return false;
        }

        ++l_mcc_record;
    }

    if (! j2k_write_mco(p_j2k,p_stream,p_manager)) {
        return false;
    }

    return true;
}

static bool j2k_write_all_coc(
    j2k_t *p_j2k,
    GrokStream *p_stream,
    event_mgr_t * p_manager )
{
    uint32_t compno;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    for (compno = 1; compno < p_j2k->m_private_image->numcomps; ++compno) {
        /* cod is first component of first tile */
        if (! j2k_compare_coc(p_j2k, 0, compno)) {
            if (! j2k_write_coc(p_j2k,compno,p_stream, p_manager)) {
                return false;
            }
        }
    }

    return true;
}

static bool j2k_write_all_qcc(
    j2k_t *p_j2k,
    GrokStream *p_stream,
    event_mgr_t * p_manager )
{
    uint32_t compno;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    for (compno = 1; compno < p_j2k->m_private_image->numcomps; ++compno) {
        /* qcd is first component of first tile */
        if (! j2k_compare_qcc(p_j2k, 0, compno)) {
            if (! j2k_write_qcc(p_j2k,compno,p_stream, p_manager)) {
                return false;
            }
        }
    }
    return true;
}

static bool j2k_write_regions( j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
    uint32_t compno;
    const tccp_t *l_tccp = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_tccp = p_j2k->m_cp.tcps->tccps;

    for (compno = 0; compno < p_j2k->m_private_image->numcomps; ++compno)  {
        if (l_tccp->roishift) {

            if (! j2k_write_rgn(p_j2k,0,compno,p_j2k->m_private_image->numcomps,p_stream,p_manager)) {
                return false;
            }
        }

        ++l_tccp;
    }

    return true;
}

static bool j2k_write_epc(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
	(void)p_manager;
    opj_codestream_index_t * l_cstr_index = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_cstr_index = p_j2k->cstr_index;
    if (l_cstr_index) {
        l_cstr_index->codestream_size = (uint64_t)p_stream->tell();
        /* The following adjustment is done to adjust the codestream size */
        /* if SOD is not at 0 in the buffer. Useful in case of JP2, where */
        /* the first bunch of bytes is not in the codestream              */
        l_cstr_index->codestream_size -= (uint64_t)l_cstr_index->main_head_start;

    }
    return true;
}

static bool j2k_read_unk (     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   uint32_t *output_marker,
                                   event_mgr_t * p_manager
                             )
{
    uint32_t l_unknown_marker;
    const opj_dec_memory_marker_handler_t * l_marker_handler;
    uint32_t l_size_unk = 2;

    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    event_msg(p_manager, EVT_WARNING, "Unknown marker\n");

    for (;;) {
        /* Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer*/
        if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
            event_msg(p_manager, EVT_ERROR, "Stream too short\n");
            return false;
        }

        /* read 2 bytes as the new marker ID*/
        grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_unknown_marker,2);

        if (!(l_unknown_marker < 0xff00)) {

            /* Get the marker handler from the marker ID*/
            l_marker_handler = j2k_get_marker_handler(l_unknown_marker);

            if (!(p_j2k->m_specific_param.m_decoder.m_state & l_marker_handler->states)) {
                event_msg(p_manager, EVT_ERROR, "Marker is not compliant with its position\n");
                return false;
            } else {
                if (l_marker_handler->id != J2K_MS_UNK) {
                    /* Add the marker to the codestream index*/
                    if (p_j2k->cstr_index && l_marker_handler->id != J2K_MS_SOT) {
                        bool res = j2k_add_mhmarker(p_j2k->cstr_index, J2K_MS_UNK,
                                                        (uint32_t) p_stream->tell() - l_size_unk,
                                                        l_size_unk);
                        if (res == false) {
                            event_msg(p_manager, EVT_ERROR, "Not enough memory to add mh marker\n");
                            return false;
                        }
                    }
                    break; /* next marker is known and well located */
                } else
                    l_size_unk += 2;
            }
        }
    }

    *output_marker = l_marker_handler->id ;

    return true;
}

static bool j2k_write_mct_record( 	mct_data_t * p_mct_record,
									GrokStream *p_stream,
									event_mgr_t * p_manager )
{
    uint32_t l_mct_size;
    uint32_t l_tmp;
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_mct_size = 10 + p_mct_record->m_data_size;

	/* MCT */
	if (!p_stream->write_short(J2K_MS_MCT, p_manager)) {
		return false;
	}

	/* Lmct */
	if (!p_stream->write_short((uint16_t)(l_mct_size - 2), p_manager)) {
		return false;
	}

	/* Zmct */
	if (!p_stream->write_short(0, p_manager)) {
		return false;
	}

    /* only one marker atm */
    l_tmp = (p_mct_record->m_index & 0xff) | (p_mct_record->m_array_type << 8) | (p_mct_record->m_element_type << 10);

	if (!p_stream->write_short((uint16_t)l_tmp, p_manager)) {
		return false;
	}

	/* Ymct */
	if (!p_stream->write_short(0, p_manager)) {
		return false;
	}

	if (!p_stream->write_bytes(p_mct_record->m_data, p_mct_record->m_data_size, p_manager)) {
		return false;
	}
    return true;
}

/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       p_header_data   the data contained in the MCT box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the MCT marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_mct (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager
                             )
{
    uint32_t i;
    tcp_t *l_tcp = nullptr;
    uint32_t l_tmp;
    uint32_t l_indix;
    mct_data_t * l_mct_data;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);

	l_tcp = j2k_get_tcp(p_j2k);

    if (p_header_size < 2) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCT marker\n");
        return false;
    }

    /* first marker */
	/* Zmct */
    grok_read_bytes(p_header_data,&l_tmp,2);                         
    p_header_data += 2;
    if (l_tmp != 0) {
        event_msg(p_manager, EVT_WARNING, "Cannot take in charge mct data within multiple MCT records\n");
        return true;
    }

    if(p_header_size <= 6) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCT marker\n");
        return false;
    }

    /* Imct -> no need for other values, take the first, type is double with decorrelation x0000 1101 0000 0000*/
    grok_read_bytes(p_header_data,&l_tmp,2);                         /* Imct */
    p_header_data += 2;

    l_indix = l_tmp & 0xff;
    l_mct_data = l_tcp->m_mct_records;

    for (i=0; i<l_tcp->m_nb_mct_records; ++i) {
        if (l_mct_data->m_index == l_indix) {
            break;
        }
        ++l_mct_data;
    }

	bool newmct = false;
	// NOT FOUND
    if (i == l_tcp->m_nb_mct_records) {
        if (l_tcp->m_nb_mct_records == l_tcp->m_nb_max_mct_records) {
            mct_data_t *new_mct_records;
            l_tcp->m_nb_max_mct_records += default_number_mct_records;

            new_mct_records = (mct_data_t *) grok_realloc(l_tcp->m_mct_records, l_tcp->m_nb_max_mct_records * sizeof(mct_data_t));
            if (! new_mct_records) {
                grok_free(l_tcp->m_mct_records);
                l_tcp->m_mct_records = nullptr;
                l_tcp->m_nb_max_mct_records = 0;
                l_tcp->m_nb_mct_records = 0;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to read MCT marker\n");
                return false;
            }

			/* Update m_mcc_records[].m_offset_array and m_decorrelation_array
			* to point to the new addresses */
			if (new_mct_records != l_tcp->m_mct_records) {
				for (i = 0; i < l_tcp->m_nb_mcc_records; ++i) {
					simple_mcc_decorrelation_data_t* l_mcc_record =
						&(l_tcp->m_mcc_records[i]);
					if (l_mcc_record->m_decorrelation_array) {
						l_mcc_record->m_decorrelation_array =
							new_mct_records +
							(l_mcc_record->m_decorrelation_array -
								l_tcp->m_mct_records);
					}
					if (l_mcc_record->m_offset_array) {
						l_mcc_record->m_offset_array =
							new_mct_records +
							(l_mcc_record->m_offset_array -
								l_tcp->m_mct_records);
					}
				}
			}



            l_tcp->m_mct_records = new_mct_records;
            l_mct_data = l_tcp->m_mct_records + l_tcp->m_nb_mct_records;
            memset(l_mct_data ,0,(l_tcp->m_nb_max_mct_records - l_tcp->m_nb_mct_records) * sizeof(mct_data_t));
        }

        l_mct_data = l_tcp->m_mct_records + l_tcp->m_nb_mct_records;
		newmct = true;
     }

    if (l_mct_data->m_data) {
        grok_free(l_mct_data->m_data);
        l_mct_data->m_data = nullptr;
		l_mct_data->m_data_size = 0;
    }

    l_mct_data->m_index = l_indix;
    l_mct_data->m_array_type = (J2K_MCT_ARRAY_TYPE)((l_tmp  >> 8) & 3);
    l_mct_data->m_element_type = (J2K_MCT_ELEMENT_TYPE)((l_tmp  >> 10) & 3);

	/* Ymct */
    grok_read_bytes(p_header_data,&l_tmp,2);                        
    p_header_data+=2;
    if (l_tmp != 0) {
        event_msg(p_manager, EVT_WARNING, "Cannot take in charge multiple MCT markers\n");
        return true;
    }

    p_header_size -= 6;

    l_mct_data->m_data = (uint8_t*)grok_malloc(p_header_size);
    if (! l_mct_data->m_data) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCT marker\n");
        return false;
    }
    memcpy(l_mct_data->m_data,p_header_data,p_header_size);
    l_mct_data->m_data_size = p_header_size;
	if (newmct)
		++l_tcp->m_nb_mct_records;

    return true;
}

static bool j2k_write_mcc_record(   simple_mcc_decorrelation_data_t * p_mcc_record,
        GrokStream *p_stream,
        event_mgr_t * p_manager )
{
    uint32_t i;
    uint32_t l_mcc_size;
    uint32_t l_nb_bytes_for_comp;
    uint32_t l_mask;
    uint32_t l_tmcc;

    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    if (p_mcc_record->m_nb_comps > 255 ) {
        l_nb_bytes_for_comp = 2;
        l_mask = 0x8000;
    } else {
        l_nb_bytes_for_comp = 1;
        l_mask = 0;
    }

    l_mcc_size = p_mcc_record->m_nb_comps * 2 * l_nb_bytes_for_comp + 19;

	/* MCC */
	if (!p_stream->write_short(J2K_MS_MCC, p_manager)) {
		return false;
	}

	/* Lmcc */
	if (!p_stream->write_short((uint16_t)(l_mcc_size - 2), p_manager)) {
		return false;
	}


    /* first marker */
	/* Zmcc */
	if (!p_stream->write_short(0, p_manager)) {
		return false;
	}

	/* Imcc -> no need for other values, take the first */
	if (!p_stream->write_byte((uint8_t)p_mcc_record->m_index, p_manager)) {
		return false;
	}

    /* only one marker atm */
	/* Ymcc */
	if (!p_stream->write_short(0, p_manager)) {
		return false;
	}

	/* Qmcc -> number of collections -> 1 */
	if (!p_stream->write_short(1, p_manager)) {
		return false;
	}

	/* Xmcci type of component transformation -> array based decorrelation */
	if (!p_stream->write_byte(0x1, p_manager)) {
		return false;
	}

	/* Nmcci number of input components involved and size for each component offset = 8 bits */
	if (!p_stream->write_short((uint16_t)(p_mcc_record->m_nb_comps | l_mask), p_manager)) {
		return false;
	}

    for (i=0; i<p_mcc_record->m_nb_comps; ++i) {
		/* Cmccij Component offset*/
		if (l_nb_bytes_for_comp == 2) {
			if (!p_stream->write_short((uint16_t)i, p_manager)) {
				return false;
			}
		}
		else {
			if (!p_stream->write_byte((uint8_t)i, p_manager)) {
				return false;
			}
		}
    }

	/* Mmcci number of output components involved and size for each component offset = 8 bits */
	if (!p_stream->write_short((uint16_t)(p_mcc_record->m_nb_comps | l_mask), p_manager)) {
		return false;
	}


    for (i=0; i<p_mcc_record->m_nb_comps; ++i) {
		/* Wmccij Component offset*/
		if (l_nb_bytes_for_comp == 2) {
			if (!p_stream->write_short((uint16_t)i, p_manager)) {
				return false;
			}
		}
		else {
			if (!p_stream->write_byte((uint8_t)i, p_manager)) {
				return false;
			}
		}
    }

    l_tmcc = ((uint32_t)((!p_mcc_record->m_is_irreversible)& 1U))<<16;

    if (p_mcc_record->m_decorrelation_array) {
        l_tmcc |= p_mcc_record->m_decorrelation_array->m_index;
    }

    if (p_mcc_record->m_offset_array) {
        l_tmcc |= ((p_mcc_record->m_offset_array->m_index)<<8);
    }

	/* Tmcci : use MCT defined as number 1 and irreversible array based. */
	if (!p_stream->write_24(l_tmcc,p_manager)) {
		return false;
	}
    return true;
}

static bool j2k_read_mcc (     j2k_t *p_j2k,
                                   uint8_t * p_header_data,
                                   uint32_t p_header_size,
                                   event_mgr_t * p_manager )
{
    uint32_t i,j;
    uint32_t l_tmp;
    uint32_t l_indix;
    tcp_t * l_tcp;
    simple_mcc_decorrelation_data_t * l_mcc_record;
    mct_data_t * l_mct_data;
    uint32_t l_nb_collections;
    uint32_t l_nb_comps;
    uint32_t l_nb_bytes_by_comp;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

	l_tcp = j2k_get_tcp(p_j2k);

    if (p_header_size < 2) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
        return false;
    }

    /* first marker */
	/* Zmcc */
    grok_read_bytes(p_header_data,&l_tmp,2);                       
    p_header_data += 2;
    if (l_tmp != 0) {
        event_msg(p_manager, EVT_WARNING, "Cannot take in charge multiple data spanning\n");
        return true;
    }

    if (p_header_size < 7) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
        return false;
    }

    grok_read_bytes(p_header_data,&l_indix,1); /* Imcc -> no need for other values, take the first */
    ++p_header_data;

    l_mcc_record = l_tcp->m_mcc_records;

    for(i=0; i<l_tcp->m_nb_mcc_records; ++i) {
        if (l_mcc_record->m_index == l_indix) {
            break;
        }
        ++l_mcc_record;
    }

    /** NOT FOUND */
	bool newmcc = false;
    if (i == l_tcp->m_nb_mcc_records) {
		// resize l_tcp->m_nb_mcc_records if necessary
        if (l_tcp->m_nb_mcc_records == l_tcp->m_nb_max_mcc_records) {
            simple_mcc_decorrelation_data_t *new_mcc_records;
            l_tcp->m_nb_max_mcc_records += default_number_mcc_records;

            new_mcc_records = (simple_mcc_decorrelation_data_t *) grok_realloc(
                                  l_tcp->m_mcc_records, l_tcp->m_nb_max_mcc_records * sizeof(simple_mcc_decorrelation_data_t));
            if (! new_mcc_records) {
                grok_free(l_tcp->m_mcc_records);
                l_tcp->m_mcc_records = nullptr;
                l_tcp->m_nb_max_mcc_records = 0;
                l_tcp->m_nb_mcc_records = 0;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to read MCC marker\n");
                return false;
            }
            l_tcp->m_mcc_records = new_mcc_records;
            l_mcc_record = l_tcp->m_mcc_records + l_tcp->m_nb_mcc_records;
            memset(l_mcc_record,0,(l_tcp->m_nb_max_mcc_records-l_tcp->m_nb_mcc_records) * sizeof(simple_mcc_decorrelation_data_t));
        }
		// set pointer to prospective new mcc record
        l_mcc_record = l_tcp->m_mcc_records + l_tcp->m_nb_mcc_records;
		newmcc = true;
    }
    l_mcc_record->m_index = l_indix;

    /* only one marker atm */
	/* Ymcc */
    grok_read_bytes(p_header_data,&l_tmp,2);                        
    p_header_data+=2;
    if (l_tmp != 0) {
        event_msg(p_manager, EVT_WARNING, "Cannot take in charge multiple data spanning\n");
        return true;
    }

	/* Qmcc -> number of collections -> 1 */
    grok_read_bytes(p_header_data,&l_nb_collections,2);                             
    p_header_data+=2;

    if (l_nb_collections > 1) {
        event_msg(p_manager, EVT_WARNING, "Cannot take in charge multiple collections\n");
        return true;
    }

    p_header_size -= 7;

    for (i=0; i<l_nb_collections; ++i) {
        if (p_header_size < 3) {
            event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
            return false;
        }

        grok_read_bytes(p_header_data,&l_tmp,1); /* Xmcci type of component transformation -> array based decorrelation */
        ++p_header_data;

        if (l_tmp != 1) {
            event_msg(p_manager, EVT_WARNING, "Cannot take in charge collections other than array decorrelation\n");
            return true;
        }

        grok_read_bytes(p_header_data,&l_nb_comps,2);

        p_header_data+=2;
        p_header_size-=3;

        l_nb_bytes_by_comp = 1 + (l_nb_comps>>15);
        l_mcc_record->m_nb_comps = l_nb_comps & 0x7fff;

        if (p_header_size < (l_nb_bytes_by_comp * l_mcc_record->m_nb_comps + 2)) {
            event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
            return false;
        }

        p_header_size -= (l_nb_bytes_by_comp * l_mcc_record->m_nb_comps + 2);

        for (j=0; j<l_mcc_record->m_nb_comps; ++j) {
			/* Cmccij Component offset*/
            grok_read_bytes(p_header_data,&l_tmp,l_nb_bytes_by_comp);       
            p_header_data+=l_nb_bytes_by_comp;

            if (l_tmp != j) {
                event_msg(p_manager, EVT_WARNING, "Cannot take in charge collections with indix shuffle\n");
                return true;
            }
        }

        grok_read_bytes(p_header_data,&l_nb_comps,2);
        p_header_data+=2;

        l_nb_bytes_by_comp = 1 + (l_nb_comps>>15);
        l_nb_comps &= 0x7fff;

        if (l_nb_comps != l_mcc_record->m_nb_comps) {
            event_msg(p_manager, EVT_WARNING, "Cannot take in charge collections without same number of indixes\n");
            return true;
        }

        if (p_header_size < (l_nb_bytes_by_comp * l_mcc_record->m_nb_comps + 3)) {
            event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
            return false;
        }

        p_header_size -= (l_nb_bytes_by_comp * l_mcc_record->m_nb_comps + 3);

        for (j=0; j<l_mcc_record->m_nb_comps; ++j) {
			/* Wmccij Component offset*/
            grok_read_bytes(p_header_data,&l_tmp,l_nb_bytes_by_comp);       
            p_header_data+=l_nb_bytes_by_comp;

            if (l_tmp != j) {
                event_msg(p_manager, EVT_WARNING, "Cannot take in charge collections with indix shuffle\n");
                return true;
            }
        }
		/* Wmccij Component offset*/
        grok_read_bytes(p_header_data,&l_tmp,3); 
        p_header_data += 3;

        l_mcc_record->m_is_irreversible = ! ((l_tmp>>16) & 1);
        l_mcc_record->m_decorrelation_array = nullptr;
        l_mcc_record->m_offset_array = nullptr;

        l_indix = l_tmp & 0xff;
        if (l_indix != 0) {
            l_mct_data = l_tcp->m_mct_records;
            for (j=0; j<l_tcp->m_nb_mct_records; ++j) {
                if (l_mct_data->m_index == l_indix) {
                    l_mcc_record->m_decorrelation_array = l_mct_data;
                    break;
                }
                ++l_mct_data;
            }

            if (l_mcc_record->m_decorrelation_array == nullptr) {
                event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
                return false;
            }
        }

        l_indix = (l_tmp >> 8) & 0xff;
        if (l_indix != 0) {
            l_mct_data = l_tcp->m_mct_records;
            for (j=0; j<l_tcp->m_nb_mct_records; ++j) {
                if (l_mct_data->m_index == l_indix) {
                    l_mcc_record->m_offset_array = l_mct_data;
                    break;
                }
                ++l_mct_data;
            }

            if (l_mcc_record->m_offset_array == nullptr) {
                event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
                return false;
            }
        }
    }

    if (p_header_size != 0) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCC marker\n");
        return false;
    }

	// only increment mcc record count if we are working on a new mcc
	// and everything succeeded
	if (newmcc)
	 ++l_tcp->m_nb_mcc_records;

    return true;
}

static bool j2k_write_mco(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager
                             )
{
    uint32_t l_mco_size;
    tcp_t * l_tcp = nullptr;
    simple_mcc_decorrelation_data_t * l_mcc_record;
    uint32_t i;
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_tcp =&(p_j2k->m_cp.tcps[p_j2k->m_current_tile_number]);
    l_mco_size = 5 + l_tcp->m_nb_mcc_records;


	/* MCO */
	if (!p_stream->write_short(J2K_MS_MCO, p_manager)) {
		return false;
	}

	/* Lmco */
	if (!p_stream->write_short((uint16_t)(l_mco_size - 2), p_manager)) {
		return false;
	}

	/* Nmco : only one transform stage*/
	if (!p_stream->write_byte((uint8_t)l_tcp->m_nb_mcc_records, p_manager)) {
		return false;
	}

    l_mcc_record = l_tcp->m_mcc_records;
    for (i=0; i<l_tcp->m_nb_mcc_records; ++i) {
		/* Imco -> use the mcc indicated by 1*/
		if (!p_stream->write_byte((uint8_t)l_mcc_record->m_index, p_manager)) {
			return false;
		}
        ++l_mcc_record;
    }
    return true;
}

/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       p_header_data   the data contained in the MCO box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the MCO marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_mco (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager
                             )
{
    uint32_t l_tmp, i;
    uint32_t l_nb_stages;
    tcp_t * l_tcp;
    tccp_t * l_tccp;
    opj_image_t * l_image;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_image = p_j2k->m_private_image;
	l_tcp = j2k_get_tcp(p_j2k);

    if (p_header_size < 1) {
        event_msg(p_manager, EVT_ERROR, "Error reading MCO marker\n");
        return false;
    }
	/* Nmco : only one transform stage*/
    grok_read_bytes(p_header_data,&l_nb_stages,1);                           
    ++p_header_data;

    if (l_nb_stages > 1) {
        event_msg(p_manager, EVT_WARNING, "Cannot take in charge multiple transformation stages.\n");
        return true;
    }

    if (p_header_size != l_nb_stages + 1) {
        event_msg(p_manager, EVT_WARNING, "Error reading MCO marker\n");
        return false;
    }

    l_tccp = l_tcp->tccps;

    for (i=0; i<l_image->numcomps; ++i) {
        l_tccp->m_dc_level_shift = 0;
        ++l_tccp;
    }

    if (l_tcp->m_mct_decoding_matrix) {
        grok_free(l_tcp->m_mct_decoding_matrix);
        l_tcp->m_mct_decoding_matrix = nullptr;
    }

    for (i=0; i<l_nb_stages; ++i) {
        grok_read_bytes(p_header_data,&l_tmp,1);
        ++p_header_data;

        if (! j2k_add_mct(l_tcp,p_j2k->m_private_image,l_tmp)) {
            return false;
        }
    }

    return true;
}

static bool j2k_add_mct(tcp_t * p_tcp, opj_image_t * p_image, uint32_t p_index)
{
    uint32_t i;
    simple_mcc_decorrelation_data_t * l_mcc_record;
    mct_data_t * l_deco_array, * l_offset_array;
    uint32_t l_data_size,l_mct_size, l_offset_size;
    uint32_t l_nb_elem;
    uint32_t * l_offset_data, * l_current_offset_data;
    tccp_t * l_tccp;

    
    assert(p_tcp != nullptr);

    l_mcc_record = p_tcp->m_mcc_records;

    for (i=0; i<p_tcp->m_nb_mcc_records; ++i) {
        if (l_mcc_record->m_index == p_index) {
            break;
        }
    }

    if (i==p_tcp->m_nb_mcc_records) {
        /** element discarded **/
        return true;
    }

    if (l_mcc_record->m_nb_comps != p_image->numcomps) {
        /** do not support number of comps != image */
        return true;
    }

    l_deco_array = l_mcc_record->m_decorrelation_array;

    if (l_deco_array) {
        l_data_size = MCT_ELEMENT_SIZE[l_deco_array->m_element_type] * p_image->numcomps * p_image->numcomps;
        if (l_deco_array->m_data_size != l_data_size) {
            return false;
        }

        l_nb_elem = p_image->numcomps * p_image->numcomps;
        l_mct_size = l_nb_elem * (uint32_t)sizeof(float);
        p_tcp->m_mct_decoding_matrix = (float*)grok_malloc(l_mct_size);

        if (! p_tcp->m_mct_decoding_matrix ) {
            return false;
        }

        j2k_mct_read_functions_to_float[l_deco_array->m_element_type](l_deco_array->m_data,p_tcp->m_mct_decoding_matrix,l_nb_elem);
    }

    l_offset_array = l_mcc_record->m_offset_array;

    if (l_offset_array) {
        l_data_size = MCT_ELEMENT_SIZE[l_offset_array->m_element_type] * p_image->numcomps;
        if (l_offset_array->m_data_size != l_data_size) {
            return false;
        }

        l_nb_elem = p_image->numcomps;
        l_offset_size = l_nb_elem * (uint32_t)sizeof(uint32_t);
        l_offset_data = (uint32_t*)grok_malloc(l_offset_size);

        if (! l_offset_data ) {
            return false;
        }

        j2k_mct_read_functions_to_int32[l_offset_array->m_element_type](l_offset_array->m_data,l_offset_data,l_nb_elem);

        l_tccp = p_tcp->tccps;
        l_current_offset_data = l_offset_data;

        for (i=0; i<p_image->numcomps; ++i) {
            l_tccp->m_dc_level_shift = (int32_t)*(l_current_offset_data++);
            ++l_tccp;
        }

        grok_free(l_offset_data);
    }

    return true;
}

static bool j2k_write_cbd( j2k_t *p_j2k,
                               GrokStream *p_stream,
                               event_mgr_t * p_manager )
{
    uint32_t i;
    uint32_t l_cbd_size;
    opj_image_t *l_image = nullptr;
    opj_image_comp_t * l_comp = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_image = p_j2k->m_private_image;
    l_cbd_size = 6 + p_j2k->m_private_image->numcomps;

	/* CBD */
	if (!p_stream->write_short(J2K_MS_CBD, p_manager)) {
		return false;
	}

	/* L_CBD */
	if (!p_stream->write_short((uint16_t)(l_cbd_size - 2), p_manager)) {
		return false;
	}

	/* Ncbd */
	if (!p_stream->write_short((uint16_t)l_image->numcomps, p_manager)) {
		return false;
	}

    l_comp = l_image->comps;

    for (i=0; i<l_image->numcomps; ++i) {
		/* Component bit depth */
		if (!p_stream->write_byte((uint8_t)((l_comp->sgnd << 7) | (l_comp->prec - 1)), p_manager)) {
			return false;
		}
        ++l_comp;
    }
    return true;
}

/**
 * Reads a CBD marker (Component bit depth definition)
 * @param       p_header_data   the data contained in the CBD box.
 * @param       p_j2k                   the jpeg2000 codec.
 * @param       p_header_size   the size of the data contained in the CBD marker.
 * @param       p_manager               the user event manager.
*/
static bool j2k_read_cbd (      j2k_t *p_j2k,
                                    uint8_t * p_header_data,
                                    uint32_t p_header_size,
                                    event_mgr_t * p_manager
                             )
{
    uint32_t l_nb_comp,l_num_comp;
    uint32_t l_comp_def;
    uint32_t i;
    opj_image_comp_t * l_comp = nullptr;

    
    assert(p_header_data != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_num_comp = p_j2k->m_private_image->numcomps;

    if (p_header_size != (p_j2k->m_private_image->numcomps + 2)) {
        event_msg(p_manager, EVT_ERROR, "Crror reading CBD marker\n");
        return false;
    }
	/* Ncbd */
    grok_read_bytes(p_header_data,&l_nb_comp,2);                           
    p_header_data+=2;

    if (l_nb_comp != l_num_comp) {
        event_msg(p_manager, EVT_ERROR, "Crror reading CBD marker\n");
        return false;
    }

    l_comp = p_j2k->m_private_image->comps;
    for (i=0; i<l_num_comp; ++i) {
		/* Component bit depth */
        grok_read_bytes(p_header_data,&l_comp_def,1);                   
        ++p_header_data;
        l_comp->sgnd = (l_comp_def>>7) & 1;
        l_comp->prec = (l_comp_def&0x7f) + 1;
        ++l_comp;
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/* J2K decoder interface                                             */
/* ----------------------------------------------------------------------- */

void j2k_setup_decoder(void *j2k_void, opj_dparameters_t *parameters)
{
	j2k_t *j2k = (j2k_t *)j2k_void;
    if(j2k && parameters) {
        j2k->m_cp.m_specific_param.m_dec.m_layer = parameters->cp_layer;
        j2k->m_cp.m_specific_param.m_dec.m_reduce = parameters->cp_reduce;
		j2k->numThreads = parameters->numThreads;
    }
}

/* ----------------------------------------------------------------------- */
/* J2K encoder interface                                                       */
/* ----------------------------------------------------------------------- */

j2k_t* j2k_create_compress(void)
{
    j2k_t *l_j2k = (j2k_t*) grok_calloc(1,sizeof(j2k_t));
    if (!l_j2k) {
        return nullptr;
    }

    l_j2k->m_is_decoder = 0;
    l_j2k->m_cp.m_is_decoder = 0;

    /* validation list creation*/
    l_j2k->m_validation_list = procedure_list_create();
    if (! l_j2k->m_validation_list) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    /* execution list creation*/
    l_j2k->m_procedure_list = procedure_list_create();
    if (! l_j2k->m_procedure_list) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    return l_j2k;
}

static uint32_t j2k_initialise_4K_poc(opj_poc_t *POC, uint32_t numres)
{
	assert(numres > 0);
    POC[0].tile  = 1;
    POC[0].resno0  = 0;
    POC[0].compno0 = 0;
    POC[0].layno1  = 1;
    POC[0].resno1  = (uint32_t)(numres-1);
    POC[0].compno1 = 3;
    POC[0].prg1 = OPJ_CPRL;
    POC[1].tile  = 1;
    POC[1].resno0  = (uint32_t)(numres-1);
    POC[1].compno0 = 0;
    POC[1].layno1  = 1;
    POC[1].resno1  = numres;
    POC[1].compno1 = 3;
    POC[1].prg1 = OPJ_CPRL;
    return 2;
}

static void j2k_set_cinema_parameters(opj_cparameters_t *parameters, opj_image_t *image, event_mgr_t *p_manager)
{
    /* Configure cinema parameters */
    uint32_t i;

    /* No tiling */
    parameters->tile_size_on = false;
    parameters->cp_tdx=1;
    parameters->cp_tdy=1;

    /* One tile part for each component */
    parameters->tp_flag = 'C';
    parameters->tp_on = 1;

    /* Tile and Image shall be at (0,0) */
    parameters->cp_tx0 = 0;
    parameters->cp_ty0 = 0;
    parameters->image_offset_x0 = 0;
    parameters->image_offset_y0 = 0;

    /* Codeblock size= 32*32 */
    parameters->cblockw_init = 32;
    parameters->cblockh_init = 32;

    /* Codeblock style: no mode switch enabled */
    parameters->mode = 0;

    /* No ROI */
    parameters->roi_compno = -1;

    /* No subsampling */
    parameters->subsampling_dx = 1;
    parameters->subsampling_dy = 1;

    /* 9-7 transform */
    parameters->irreversible = 1;

    /* Number of layers */
    if (parameters->tcp_numlayers > 1) {
        event_msg(p_manager, EVT_WARNING,
						"JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
                      "1 single quality layer"
                      "-> Number of layers forced to 1 (rather than %d)\n"
                      "-> Rate of the last layer (%3.1f) will be used",
                      parameters->tcp_numlayers, parameters->tcp_rates[parameters->tcp_numlayers-1]);
        parameters->tcp_rates[0] = parameters->tcp_rates[parameters->tcp_numlayers-1];
        parameters->tcp_numlayers = 1;
    }

    /* Resolution levels */
    switch (parameters->rsiz) {
    case OPJ_PROFILE_CINEMA_2K:
        if(parameters->numresolution > 6) {
            event_msg(p_manager, EVT_WARNING,
						"JPEG 2000 profile 3 (2k digital cinema) requires:\n"
                          "Number of decomposition levels <= 5\n"
                          "-> Number of decomposition levels forced to 5 (rather than %d)\n",
                          parameters->numresolution+1);
            parameters->numresolution = 6;
        }
        break;
    case OPJ_PROFILE_CINEMA_4K:
        if(parameters->numresolution < 2) {
            event_msg(p_manager, EVT_WARNING,
							"JPEG 2000 profile 4 (4k digital cinema) requires:\n"
                          "Number of decomposition levels >= 1 && <= 6\n"
                          "-> Number of decomposition levels forced to 1 (rather than %d)\n",
                          parameters->numresolution+1);
            parameters->numresolution = 1;
        } else if(parameters->numresolution > 7) {
            event_msg(p_manager, EVT_WARNING,
							"JPEG 2000 profile 4 (4k digital cinema) requires:\n"
                          "Number of decomposition levels >= 1 && <= 6\n"
                          "-> Number of decomposition levels forced to 6 (rather than %d)\n",
                          parameters->numresolution+1);
            parameters->numresolution = 7;
        }
        break;
    default :
        break;
    }

    /* Precincts */
    parameters->csty |= 0x01;
    parameters->res_spec = parameters->numresolution-1;
    for (i = 0; i<parameters->res_spec; i++) {
        parameters->prcw_init[i] = 256;
        parameters->prch_init[i] = 256;
    }

    /* The progression order shall be CPRL */
    parameters->prog_order = OPJ_CPRL;

    /* Progression order changes for 4K, disallowed for 2K */
    if (parameters->rsiz == OPJ_PROFILE_CINEMA_4K) {
        parameters->numpocs = j2k_initialise_4K_poc(parameters->POC,parameters->numresolution);
    } else {
        parameters->numpocs = 0;
    }

    /* Limit bit-rate */
    parameters->cp_disto_alloc = 1;
    if (parameters->max_cs_size == 0) {
        /* No rate has been introduced for code stream, so 24 fps is assumed */
        parameters->max_cs_size = OPJ_CINEMA_24_CS;
        event_msg(p_manager, EVT_WARNING,
                      "JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
                      "Maximum 1302083 compressed bytes @ 24fps for code stream.\n"
                      "As no rate has been given for entire code stream, this limit will be used.\n");
    } else if (parameters->max_cs_size > OPJ_CINEMA_24_CS) {
        event_msg(p_manager, EVT_WARNING,
                      "JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
                      "Maximum 1302083 compressed bytes @ 24fps for code stream.\n"
                      "The specified rate exceeds this limit, so rate will be forced to 1302083 bytes.\n");
        parameters->max_cs_size = OPJ_CINEMA_24_CS;
    }

    if (parameters->max_comp_size == 0) {
        /* No rate has been introduced for each component, so 24 fps is assumed */
        parameters->max_comp_size = OPJ_CINEMA_24_COMP;
        event_msg(p_manager, EVT_WARNING,
                      "JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
                      "Maximum 1041666 compressed bytes @ 24fps per component.\n"
                      "As no rate has been given, this limit will be used.\n");
    } else if (parameters->max_comp_size > OPJ_CINEMA_24_COMP) {
        event_msg(p_manager, EVT_WARNING,
                      "JPEG 2000 profiles 3 and 4 (2k and 4k digital cinema) require:\n"
                      "Maximum 1041666 compressed bytes @ 24fps per component.\n"
                      "The specified rate exceeds this limit, so rate will be forced to 1041666 bytes.\n");
        parameters->max_comp_size = OPJ_CINEMA_24_COMP;
    }

    parameters->tcp_rates[0] =  ((double)image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec)/
                               ((double)parameters->max_cs_size * 8 * image->comps[0].dx * image->comps[0].dy);

}

static bool j2k_is_cinema_compliant(opj_image_t *image, uint16_t rsiz, event_mgr_t *p_manager)
{
    uint32_t i;

    /* Number of components */
    if (image->numcomps != 3) {
        event_msg(p_manager, EVT_WARNING,
                      "JPEG 2000 profile 3 (2k digital cinema) requires:\n"
                      "3 components"
                      "-> Number of components of input image (%d) is not compliant\n"
                      "-> Non-profile-3 codestream will be generated\n",
                      image->numcomps);
        return false;
    }

    /* Bitdepth */
    for (i = 0; i < image->numcomps; i++) {
        if ((image->comps[i].prec != 12) | (image->comps[i].sgnd)) {
            char signed_str[] = "signed";
            char unsigned_str[] = "unsigned";
            char *tmp_str = image->comps[i].sgnd?signed_str:unsigned_str;
            event_msg(p_manager, EVT_WARNING,
                          "JPEG 2000 profile 3 (2k digital cinema) requires:\n"
                          "Precision of each component shall be 12 bits unsigned"
                          "-> At least component %d of input image (%d bits, %s) is not compliant\n"
                          "-> Non-profile-3 codestream will be generated\n",
                          i,image->comps[i].prec, tmp_str);
            return false;
        }
    }

    /* Image size */
    switch (rsiz) {
    case OPJ_PROFILE_CINEMA_2K:
        if (((image->comps[0].w > 2048) | (image->comps[0].h > 1080))) {
            event_msg(p_manager, EVT_WARNING,
                          "JPEG 2000 profile 3 (2k digital cinema) requires:\n"
                          "width <= 2048 and height <= 1080\n"
                          "-> Input image size %d x %d is not compliant\n"
                          "-> Non-profile-3 codestream will be generated\n",
                          image->comps[0].w,image->comps[0].h);
            return false;
        }
        break;
    case OPJ_PROFILE_CINEMA_4K:
        if (((image->comps[0].w > 4096) | (image->comps[0].h > 2160))) {
            event_msg(p_manager, EVT_WARNING,
                          "JPEG 2000 profile 4 (4k digital cinema) requires:\n"
                          "width <= 4096 and height <= 2160\n"
                          "-> Image size %d x %d is not compliant\n"
                          "-> Non-profile-4 codestream will be generated\n",
                          image->comps[0].w,image->comps[0].h);
            return false;
        }
        break;
    default :
        break;
    }

    return true;
}

bool j2k_setup_encoder(     j2k_t *p_j2k,
                                opj_cparameters_t *parameters,
                                opj_image_t *image,
                                event_mgr_t * p_manager)
{
    uint32_t i, j, tileno, numpocs_tile;
    cp_t *cp = nullptr;

    if(!p_j2k || !parameters || ! image) {
        return false;
    }

	//sanity check on image
	if (image->numcomps < 1 || image->numcomps > max_num_components) {
		event_msg(p_manager, EVT_ERROR, "Invalid number of components specified while setting up JP2 encoder\n");
		return false;
	}
	if ((image->x1 < image->x0) || (image->y1 < image->y0)) {
		event_msg(p_manager, EVT_ERROR, "Invalid input image dimensions found while setting up JP2 encoder\n");
		return false;
	}
	for (i = 0; i < image->numcomps; ++i) {
		auto comp = image->comps + i;
		if (comp->w == 0 || comp->h == 0) {
			event_msg(p_manager, EVT_ERROR, "Invalid input image component dimensions found while setting up JP2 encoder\n");
			return false;
		}
		if (comp->prec == 0) {
			event_msg(p_manager, EVT_ERROR, "Invalid component precision of 0 found while setting up JP2 encoder\n");
			return false;
		}
	}

    if ((parameters->numresolution == 0) || (parameters->numresolution > OPJ_J2K_MAXRLVLS)) {
        event_msg(p_manager, EVT_ERROR, "Invalid number of resolutions : %d not in range [1,%d]\n", parameters->numresolution, OPJ_J2K_MAXRLVLS);
        return false;
    }

	/* if no rate entered, lossless by default */
	if (parameters->tcp_numlayers == 0) {
		parameters->tcp_rates[0] = 0;
		parameters->tcp_numlayers=1;
		parameters->cp_disto_alloc = 1;
	}


    /* see if max_codestream_size does limit input rate */
	double image_bytes = ((double)image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec) /
																			(8 * image->comps[0].dx * image->comps[0].dy);
    if (parameters->max_cs_size == 0) {
        if (parameters->tcp_numlayers > 0 && parameters->tcp_rates[parameters->tcp_numlayers-1] > 0) {
            parameters->max_cs_size = (uint64_t) floor(image_bytes/ parameters->tcp_rates[parameters->tcp_numlayers - 1]);
        } 
    } else {
        bool cap = false;
		auto min_rate = image_bytes / (double)parameters->max_cs_size;
        for (i = 0; i <  parameters->tcp_numlayers; i++) {
            if (parameters->tcp_rates[i] < min_rate) {
                parameters->tcp_rates[i] = min_rate;
                cap = true;
            }
        }
        if (cap) {
            event_msg(p_manager, EVT_WARNING,
                          "The desired maximum codestream size has limited\n"
                          "at least one of the desired quality layers\n");
        }
    }

    /* Manage profiles and applications and set RSIZ */
    /* set cinema parameters if required */
    if (OPJ_IS_CINEMA(parameters->rsiz)) {
        if ((parameters->rsiz == OPJ_PROFILE_CINEMA_S2K)
                || (parameters->rsiz == OPJ_PROFILE_CINEMA_S4K)) {
            event_msg(p_manager, EVT_WARNING,
                          "JPEG 2000 Scalable Digital Cinema profiles not supported\n");
            parameters->rsiz = OPJ_PROFILE_NONE;
        } else {
			if (j2k_is_cinema_compliant(image, parameters->rsiz, p_manager)) {
				j2k_set_cinema_parameters(parameters, image, p_manager);
			}
			else {
				parameters->rsiz = OPJ_PROFILE_NONE;
			}
        }
    } else if (OPJ_IS_STORAGE(parameters->rsiz)) {
        event_msg(p_manager, EVT_WARNING,
                      "JPEG 2000 Long Term Storage profile not supported\n");
        parameters->rsiz = OPJ_PROFILE_NONE;
    } else if (OPJ_IS_BROADCAST(parameters->rsiz)) {
		// Todo: sanity check on tiling (must be done for each frame)
		// Standard requires either single-tile or multi-tile. Multi-tile only permits
		// 1 or 4 tiles per frame, where multiple tiles have identical
		// sizes, and are configured in either 2x2 or 1x4 layout.

		//sanity check on reversible vs. irreversible
		uint16_t profile = parameters->rsiz & 0xFF00;
		if (profile == OPJ_PROFILE_BC_MULTI_R) {
			if (parameters->irreversible) {
				event_msg(p_manager, EVT_WARNING,
					"JPEG 2000 Broadcast profile; multi-tile reversible: forcing irreversible flag to false\n");
				parameters->irreversible = 0;
			}
		}
		else {
			if (!parameters->irreversible) {
				event_msg(p_manager, EVT_WARNING,
					"JPEG 2000 Broadcast profile: forcing irreversible flag to true\n");
				parameters->irreversible = 1;
			}
		}
		// sanity check on levels
		auto level = parameters->rsiz & 0xF;
		if (level > maxMainLevel) {
			event_msg(p_manager, EVT_WARNING,
				"JPEG 2000 Broadcast profile: invalid level %d\n", level);
			parameters->rsiz = OPJ_PROFILE_NONE;
		}
    } else if (OPJ_IS_IMF(parameters->rsiz)) {
		//sanity check on reversible vs. irreversible
		uint16_t profile = parameters->rsiz & 0xFF00;
		if ((profile == OPJ_PROFILE_IMF_2K_R) || 
			(profile == OPJ_PROFILE_IMF_4K_R) || 
			(profile == OPJ_PROFILE_IMF_8K_R)) {
			if (parameters->irreversible) {
				event_msg(p_manager, EVT_WARNING,
					"JPEG 2000 IMF profile; forcing irreversible flag to false\n");
				parameters->irreversible = 0;
			}
		}
		else {
			if (!parameters->irreversible) {
				event_msg(p_manager, EVT_WARNING,
					"JPEG 2000 IMF profile: forcing irreversible flag to true\n");
				parameters->irreversible = 1;
			}
		}
		//sanity check on main and sub levels
		auto main_level = parameters->rsiz & 0xF;
		if (main_level > maxMainLevel) {
			event_msg(p_manager, EVT_WARNING,
				"JPEG 2000 IMF profile: invalid main-level %d\n", main_level);
			parameters->rsiz = OPJ_PROFILE_NONE;
		}
		auto sub_level = (parameters->rsiz >> 4) & 0xF;
		bool invalidSubLevel = sub_level > maxSubLevel;
		if (main_level > 3) {
			invalidSubLevel = invalidSubLevel || (sub_level > main_level - 2);
		}
		else {
			invalidSubLevel = invalidSubLevel || (sub_level >1);
		}
		if (invalidSubLevel) {
			event_msg(p_manager, EVT_WARNING,
				"JPEG 2000 IMF profile: invalid sub-level %d\n", sub_level);
			parameters->rsiz = OPJ_PROFILE_NONE;
		}
    } else if (OPJ_IS_PART2(parameters->rsiz)) {
        if (parameters->rsiz == ((OPJ_PROFILE_PART2) | (OPJ_EXTENSION_NONE))) {
            event_msg(p_manager, EVT_WARNING,
                          "JPEG 2000 Part-2 profile defined\n"
                          "but no Part-2 extension enabled.\n"
                          "Profile set to NONE.\n");
            parameters->rsiz = OPJ_PROFILE_NONE;
        } else if (parameters->rsiz != ((OPJ_PROFILE_PART2) | (OPJ_EXTENSION_MCT))) {
            event_msg(p_manager, EVT_WARNING,
                          "Unsupported Part-2 extension enabled\n"
                          "Profile set to NONE.\n");
            parameters->rsiz = OPJ_PROFILE_NONE;
        }
    }

	if (parameters->numpocs) {
		/* initialisation of POC */
		if (!j2k_check_poc_val(parameters->POC, parameters->numpocs, parameters->numresolution, image->numcomps, parameters->tcp_numlayers, p_manager)) {
			event_msg(p_manager, EVT_ERROR, "Failed to initialize POC\n");
			return false;
		}
	}

    /*
    copy user encoding parameters
    */

	/* keep a link to cp so that we can destroy it later in j2k_destroy_compress */
	cp = &(p_j2k->m_cp);

	/* set default values for cp */
	cp->tw = 1;
	cp->th = 1;


    cp->m_specific_param.m_enc.m_max_comp_size = parameters->max_comp_size;
    cp->rsiz = parameters->rsiz;
    cp->m_specific_param.m_enc.m_disto_alloc = parameters->cp_disto_alloc & 1u;
    cp->m_specific_param.m_enc.m_fixed_quality = parameters->cp_fixed_quality & 1u;
	cp->m_specific_param.m_enc.rateControlAlgorithm = parameters->rateControlAlgorithm;

    /* tiles */
    cp->tdx = parameters->cp_tdx;
    cp->tdy = parameters->cp_tdy;

    /* tile offset */
    cp->tx0 = parameters->cp_tx0;
    cp->ty0 = parameters->cp_ty0;

    /* comment string */
    if(parameters->cp_comment) {
		cp->comment_len = strlen(parameters->cp_comment);
        cp->comment = (char*)grok_malloc(cp->comment_len + 1U);
        if(!cp->comment) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate copy of comment string\n");
            return false;
        }
		cp->comment_len = strlen(parameters->cp_comment);
        strcpy(cp->comment, parameters->cp_comment);
    } else {
        /* Create default comment for codestream */
        const char comment[] = "Created by Grok     version ";
        const size_t clen = strlen(comment);
        const char *version = opj_version();

        cp->comment = (char*)grok_malloc(clen+strlen(version)+1);
        if(!cp->comment) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate comment string\n");
            return false;
        }
        sprintf(cp->comment,"%s%s", comment, version);
		cp->comment_len = strlen(cp->comment);
    }

    /*
    calculate other encoding parameters
    */

    if (parameters->tile_size_on) {
		// avoid divide by zero
		if (cp->tdx == 0 || cp->tdy == 0) {
			return false;
		}
        cp->tw = ceildiv<uint32_t>((image->x1 - cp->tx0), cp->tdx);
        cp->th = ceildiv<uint32_t>((image->y1 - cp->ty0), cp->tdy);
    } else {
        cp->tdx = image->x1 - cp->tx0;
        cp->tdy = image->y1 - cp->ty0;
    }

    if (parameters->tp_on) {
        cp->m_specific_param.m_enc.m_tp_flag = (uint8_t)parameters->tp_flag;
        cp->m_specific_param.m_enc.m_tp_on = 1;
    }

    /* initialize the mutiple tiles */
    /* ---------------------------- */
    cp->tcps = (tcp_t*) grok_calloc(cp->tw * cp->th, sizeof(tcp_t));
    if (!cp->tcps) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate tile coding parameters\n");
        return false;
    }


    for (tileno = 0; tileno < cp->tw * cp->th; tileno++) {
        tcp_t *tcp = &cp->tcps[tileno];
        tcp->numlayers = parameters->tcp_numlayers;

        for (j = 0; j < tcp->numlayers; j++) {
            if(OPJ_IS_CINEMA(cp->rsiz)) {
                if (cp->m_specific_param.m_enc.m_fixed_quality) {
                    tcp->distoratio[j] = parameters->tcp_distoratio[j];
                }
                tcp->rates[j] = parameters->tcp_rates[j];
            } else {
				/* add fixed_quality */
                if (cp->m_specific_param.m_enc.m_fixed_quality) {      
                    tcp->distoratio[j] = parameters->tcp_distoratio[j];
                } else {
                    tcp->rates[j] = parameters->tcp_rates[j];
                }
            }
        }

        tcp->csty = parameters->csty;
        tcp->prg = parameters->prog_order;
        tcp->mct = parameters->tcp_mct;

        numpocs_tile = 0;
        tcp->POC = 0;

        if (parameters->numpocs) {
            /* initialisation of POC */
            tcp->POC = 1;
            for (i = 0; i < parameters->numpocs; i++) {
                if (tileno + 1 == parameters->POC[i].tile )  {
                    opj_poc_t *tcp_poc = &tcp->pocs[numpocs_tile];

                    tcp_poc->resno0         = parameters->POC[numpocs_tile].resno0;
                    tcp_poc->compno0        = parameters->POC[numpocs_tile].compno0;
                    tcp_poc->layno1         = parameters->POC[numpocs_tile].layno1;
                    tcp_poc->resno1         = parameters->POC[numpocs_tile].resno1;
                    tcp_poc->compno1        = parameters->POC[numpocs_tile].compno1;
                    tcp_poc->prg1           = parameters->POC[numpocs_tile].prg1;
                    tcp_poc->tile           = parameters->POC[numpocs_tile].tile;

                    numpocs_tile++;
                }
            }
			if (numpocs_tile == 0) {
				event_msg(p_manager, EVT_ERROR, "Problem with specified progression order changes\n");
				return false;
			}
            tcp->numpocs = numpocs_tile -1 ;
        } else {
            tcp->numpocs = 0;
        }

        tcp->tccps = (tccp_t*) grok_calloc(image->numcomps, sizeof(tccp_t));
        if (!tcp->tccps) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate tile component coding parameters\n");
            return false;
        }
        if (parameters->mct_data) {

            uint32_t lMctSize = image->numcomps * image->numcomps * (uint32_t)sizeof(float);
            float * lTmpBuf = (float*)grok_malloc(lMctSize);
            int32_t * l_dc_shift = (int32_t *) ((uint8_t *) parameters->mct_data + lMctSize);

            if (!lTmpBuf) {
                event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate temp buffer\n");
                return false;
            }

            tcp->mct = 2;
            tcp->m_mct_coding_matrix = (float*)grok_malloc(lMctSize);
            if (! tcp->m_mct_coding_matrix) {
                grok_free(lTmpBuf);
                lTmpBuf = nullptr;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate encoder MCT coding matrix \n");
                return false;
            }
            memcpy(tcp->m_mct_coding_matrix,parameters->mct_data,lMctSize);
            memcpy(lTmpBuf,parameters->mct_data,lMctSize);

            tcp->m_mct_decoding_matrix = (float*)grok_malloc(lMctSize);
            if (! tcp->m_mct_decoding_matrix) {
                grok_free(lTmpBuf);
                lTmpBuf = nullptr;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate encoder MCT decoding matrix \n");
                return false;
            }
            if(matrix_inversion_f(lTmpBuf,(tcp->m_mct_decoding_matrix),image->numcomps) == false) {
                grok_free(lTmpBuf);
                lTmpBuf = nullptr;
                event_msg(p_manager, EVT_ERROR, "Failed to inverse encoder MCT decoding matrix \n");
                return false;
            }

            tcp->mct_norms = (double*)
                             grok_malloc(image->numcomps * sizeof(double));
            if (! tcp->mct_norms) {
                grok_free(lTmpBuf);
                lTmpBuf = nullptr;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to allocate encoder MCT norms \n");
                return false;
            }
            opj_calculate_norms(tcp->mct_norms,image->numcomps,tcp->m_mct_decoding_matrix);
            grok_free(lTmpBuf);

            for (i = 0; i < image->numcomps; i++) {
                tccp_t *tccp = &tcp->tccps[i];
                tccp->m_dc_level_shift = l_dc_shift[i];
            }

            if (j2k_setup_mct_encoding(tcp,image) == false) {
                /* free will be handled by j2k_destroy */
                event_msg(p_manager, EVT_ERROR, "Failed to setup j2k mct encoding\n");
                return false;
            }
        } else {
            if(tcp->mct==1 && image->numcomps >= 3) { /* RGB->YCC MCT is enabled */
                if ((image->comps[0].dx != image->comps[1].dx) ||
                        (image->comps[0].dx != image->comps[2].dx) ||
                        (image->comps[0].dy != image->comps[1].dy) ||
                        (image->comps[0].dy != image->comps[2].dy)) {
                    event_msg(p_manager, EVT_WARNING, "Cannot perform MCT on components with different sizes. Disabling MCT.\n");
                    tcp->mct = 0;
                }
            }
            for (i = 0; i < image->numcomps; i++) {
                tccp_t *tccp = tcp->tccps + i;
                opj_image_comp_t * l_comp = image->comps + i;
                if (! l_comp->sgnd) {
                    tccp->m_dc_level_shift = 1 << (l_comp->prec - 1);
                }
            }
        }

        for (i = 0; i < image->numcomps; i++) {
            tccp_t *tccp = &tcp->tccps[i];

            tccp->csty = parameters->csty & 0x01;   /* 0 => one precinct || 1 => custom precinct  */
            tccp->numresolutions = parameters->numresolution;
            tccp->cblkw = int_floorlog2(parameters->cblockw_init);
            tccp->cblkh = int_floorlog2(parameters->cblockh_init);
            tccp->cblksty = parameters->mode;
            tccp->qmfbid = parameters->irreversible ? 0 : 1;
            tccp->qntsty = parameters->irreversible ? J2K_CCP_QNTSTY_SEQNT : J2K_CCP_QNTSTY_NOQNT;
            tccp->numgbits = 2;

            if ((int32_t)i == parameters->roi_compno) {
                tccp->roishift = parameters->roi_shift;
            } else {
                tccp->roishift = 0;
            }
            if ((parameters->csty & J2K_CCP_CSTY_PRT) && parameters->res_spec){
				uint32_t p = 0;
				int32_t it_res;
                assert( tccp->numresolutions > 0 );
                for (it_res = (int32_t)tccp->numresolutions - 1; it_res >= 0; it_res--) {
                    if (p < parameters->res_spec) {
                        if (parameters->prcw_init[p] < 1) {
                            tccp->prcw[it_res] = 1;
                        } else {
                            tccp->prcw[it_res] = uint_floorlog2(parameters->prcw_init[p]);
                        }
                        if (parameters->prch_init[p] < 1) {
                            tccp->prch[it_res] = 1;
                        } else {
                            tccp->prch[it_res] = uint_floorlog2(parameters->prch_init[p]);
                        }
                    } else {
                        uint32_t res_spec = parameters->res_spec;
                        uint32_t size_prcw = 0;
                        uint32_t size_prch = 0;
                        size_prcw = parameters->prcw_init[res_spec - 1] >> (p - (res_spec - 1));
                        size_prch = parameters->prch_init[res_spec - 1] >> (p - (res_spec - 1));
                        if (size_prcw < 1) {
                            tccp->prcw[it_res] = 1;
                        } else {
                            tccp->prcw[it_res] = uint_floorlog2(size_prcw);
                        }
                        if (size_prch < 1) {
                            tccp->prch[it_res] = 1;
                        } else {
                            tccp->prch[it_res] = uint_floorlog2(size_prch);
                        }
                    }
                    p++;
                    /*printf("\nsize precinct for level %d : %d,%d\n", it_res,tccp->prcw[it_res], tccp->prch[it_res]); */
                }       /*end for*/
            } else {
                for (j = 0; j < tccp->numresolutions; j++) {
                    tccp->prcw[j] = 15;
                    tccp->prch[j] = 15;
                }
            }
            dwt_calc_explicit_stepsizes(tccp, image->comps[i].prec);
        }
    }
    if (parameters->mct_data) {
        grok_free(parameters->mct_data);
        parameters->mct_data = nullptr;
    }
	p_j2k->numThreads = parameters->numThreads;
    return true;
}

static bool j2k_add_mhmarker(opj_codestream_index_t *cstr_index, uint32_t type, int64_t pos, uint32_t len)
{
    assert(cstr_index != nullptr);

    /* expand the list? */
    if ((cstr_index->marknum + 1) > cstr_index->maxmarknum) {
        opj_marker_info_t *new_marker;
        cstr_index->maxmarknum = (uint32_t)(100 + (float) cstr_index->maxmarknum);
        new_marker = (opj_marker_info_t *) grok_realloc(cstr_index->marker, cstr_index->maxmarknum *sizeof(opj_marker_info_t));
        if (! new_marker) {
            grok_free(cstr_index->marker);
            cstr_index->marker = nullptr;
            cstr_index->maxmarknum = 0;
            cstr_index->marknum = 0;
            /* event_msg(p_manager, EVT_ERROR, "Not enough memory to add mh marker\n"); */
            return false;
        }
        cstr_index->marker = new_marker;
    }

    /* add the marker */
    cstr_index->marker[cstr_index->marknum].type = (uint16_t)type;
    cstr_index->marker[cstr_index->marknum].pos = (uint64_t)pos;
    cstr_index->marker[cstr_index->marknum].len = (uint32_t)len;
    cstr_index->marknum++;
    return true;
}

static bool j2k_add_tlmarker(uint32_t tileno, opj_codestream_index_t *cstr_index, uint32_t type, int64_t pos, uint32_t len)
{
    assert(cstr_index != nullptr);
    assert(cstr_index->tile_index != nullptr);

    /* expand the list? */
    if ((cstr_index->tile_index[tileno].marknum + 1) > cstr_index->tile_index[tileno].maxmarknum) {
        opj_marker_info_t *new_marker;
        cstr_index->tile_index[tileno].maxmarknum = (uint32_t)(100 + (float) cstr_index->tile_index[tileno].maxmarknum);
        new_marker = (opj_marker_info_t *) grok_realloc(
                         cstr_index->tile_index[tileno].marker,
                         cstr_index->tile_index[tileno].maxmarknum *sizeof(opj_marker_info_t));
        if (! new_marker) {
            grok_free(cstr_index->tile_index[tileno].marker);
            cstr_index->tile_index[tileno].marker = nullptr;
            cstr_index->tile_index[tileno].maxmarknum = 0;
            cstr_index->tile_index[tileno].marknum = 0;
            /* event_msg(p_manager, EVT_ERROR, "Not enough memory to add tl marker\n"); */
            return false;
        }
        cstr_index->tile_index[tileno].marker = new_marker;
    }

    /* add the marker */
    cstr_index->tile_index[tileno].marker[cstr_index->tile_index[tileno].marknum].type = (uint16_t)type;
    cstr_index->tile_index[tileno].marker[cstr_index->tile_index[tileno].marknum].pos = (uint64_t)pos;
    cstr_index->tile_index[tileno].marker[cstr_index->tile_index[tileno].marknum].len = (uint32_t)len;
    cstr_index->tile_index[tileno].marknum++;

    if (type == J2K_MS_SOT) {
        uint32_t l_current_tile_part = cstr_index->tile_index[tileno].current_tpsno;

        if (cstr_index->tile_index[tileno].tp_index)
            cstr_index->tile_index[tileno].tp_index[l_current_tile_part].start_pos = pos;

    }
    return true;
}

/*
 * -----------------------------------------------------------------------
 * -----------------------------------------------------------------------
 * -----------------------------------------------------------------------
 */

bool j2k_end_decompress(j2k_t *p_j2k,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager
                           )
{
    (void)p_j2k;
    (void)p_stream;
    (void)p_manager;
    return true;
}

bool j2k_read_header(   GrokStream *p_stream,
                            j2k_t* p_j2k,
							opj_header_info_t* header_info,
                            opj_image_t** p_image,
                            event_mgr_t* p_manager )
{
    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    /* create an empty image header */
    p_j2k->m_private_image = opj_image_create0();
    if (! p_j2k->m_private_image) {
        return false;
    }

    /* customization of the validation */
    if (! j2k_setup_decoding_validation(p_j2k, p_manager)) {
        opj_image_destroy(p_j2k->m_private_image);
        p_j2k->m_private_image = nullptr;
        return false;
    }

    /* validation of the parameters codec */
    if (! j2k_exec(p_j2k, p_j2k->m_validation_list, p_stream,p_manager)) {
        opj_image_destroy(p_j2k->m_private_image);
        p_j2k->m_private_image = nullptr;
        return false;
    }

    /* customization of the encoding */
    if (! j2k_setup_header_reading(p_j2k, p_manager)) {
        opj_image_destroy(p_j2k->m_private_image);
        p_j2k->m_private_image = nullptr;
        return false;
    }

    /* read header */
    if (! j2k_exec (p_j2k,p_j2k->m_procedure_list,p_stream,p_manager)) {
        opj_image_destroy(p_j2k->m_private_image);
        p_j2k->m_private_image = nullptr;
        return false;
    }

	if (header_info) {
		cp_t *l_cp = nullptr;
		tcp_t *l_tcp = nullptr;
		tccp_t *l_tccp = nullptr;

		l_cp = &(p_j2k->m_cp);
		l_tcp = &l_cp->tcps[p_j2k->m_current_tile_number];
		l_tccp = &l_tcp->tccps[0];

		header_info->cblockw_init = 1 << l_tccp->cblkw;
		header_info->cblockh_init = 1 << l_tccp->cblkh;
		header_info->irreversible = l_tccp->qmfbid == 0;
		header_info->mct = l_tcp->mct;
		header_info->rsiz = l_cp->rsiz;
		header_info->numresolutions = l_tccp->numresolutions;
		header_info->csty = l_tcp->csty;
		for (uint32_t i = 0; i < header_info->numresolutions; ++i) {
			header_info->prcw_init[i] = 1 << l_tccp->prcw[i];
			header_info->prch_init[i] = 1 << l_tccp->prch[i];
		}
		header_info->cp_tx0 = p_j2k->m_cp.tx0;
		header_info->cp_ty0 = p_j2k->m_cp.ty0;

		header_info->cp_tdx = p_j2k->m_cp.tdx;
		header_info->cp_tdy = p_j2k->m_cp.tdy;

		header_info->cp_tw = p_j2k->m_cp.tw;
		header_info->cp_th = p_j2k->m_cp.th;

		header_info->tcp_numlayers = l_tcp->numlayers;

		header_info->comment = p_j2k->m_cp.comment;
		header_info->comment_len = p_j2k->m_cp.comment_len;
		header_info->isBinaryComment = p_j2k->m_cp.isBinaryComment;
	}


    *p_image = opj_image_create0();
    if (! (*p_image)) {
        return false;
    }

    /* Copy codestream image information to the output image */
    opj_copy_image_header(p_j2k->m_private_image, *p_image);

	if (p_j2k->cstr_index) {
		/*Allocate and initialize some elements of codestrem index*/
		if (!j2k_allocate_tile_element_cstr_index(p_j2k)) {
			return false;
		}
	}

    return true;
}

static bool j2k_setup_header_reading (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_read_header_procedure, p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, add your custom procedures */
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_copy_default_tcp_and_create_tcd, p_manager))  {
        return false;
    }

    return true;
}

static bool j2k_setup_decoding_validation (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(p_j2k->m_validation_list,(procedure)j2k_build_decoder, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_validation_list,(procedure)j2k_decoding_validation, p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, add your custom validation procedure */
    return true;
}

static bool j2k_mct_validation (       j2k_t * p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager )
{
	(void)p_stream;
	(void)p_manager;
    bool l_is_valid = true;
    uint32_t i,j;

    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    if ((p_j2k->m_cp.rsiz & 0x8200) == 0x8200) {
        uint32_t l_nb_tiles = p_j2k->m_cp.th * p_j2k->m_cp.tw;
        tcp_t * l_tcp = p_j2k->m_cp.tcps;

        for (i=0; i<l_nb_tiles; ++i) {
            if (l_tcp->mct == 2) {
                tccp_t * l_tccp = l_tcp->tccps;
                l_is_valid &= (l_tcp->m_mct_coding_matrix != nullptr);

                for (j=0; j<p_j2k->m_private_image->numcomps; ++j) {
                    l_is_valid &= ! (l_tccp->qmfbid & 1);
                    ++l_tccp;
                }
            }
            ++l_tcp;
        }
    }

    return l_is_valid;
}

bool j2k_setup_mct_encoding(tcp_t * p_tcp, opj_image_t * p_image)
{
    uint32_t i;
    uint32_t l_indix = 1;
    mct_data_t * l_mct_deco_data = nullptr,* l_mct_offset_data = nullptr;
    simple_mcc_decorrelation_data_t * l_mcc_data;
    uint32_t l_mct_size,l_nb_elem;
    float * l_data, * l_current_data;
    tccp_t * l_tccp;

    
    assert(p_tcp != nullptr);

    if (p_tcp->mct != 2) {
        return true;
    }

    if (p_tcp->m_mct_decoding_matrix) {
        if (p_tcp->m_nb_mct_records == p_tcp->m_nb_max_mct_records) {
            mct_data_t *new_mct_records;
            p_tcp->m_nb_max_mct_records += default_number_mct_records;

            new_mct_records = (mct_data_t *) grok_realloc(p_tcp->m_mct_records, p_tcp->m_nb_max_mct_records * sizeof(mct_data_t));
            if (! new_mct_records) {
                grok_free(p_tcp->m_mct_records);
                p_tcp->m_mct_records = nullptr;
                p_tcp->m_nb_max_mct_records = 0;
                p_tcp->m_nb_mct_records = 0;
                /* event_msg(p_manager, EVT_ERROR, "Not enough memory to setup mct encoding\n"); */
                return false;
            }
            p_tcp->m_mct_records = new_mct_records;
            l_mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

            memset(l_mct_deco_data ,0,(p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records) * sizeof(mct_data_t));
        }
        l_mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

        if (l_mct_deco_data->m_data) {
            grok_free(l_mct_deco_data->m_data);
            l_mct_deco_data->m_data = nullptr;
        }

        l_mct_deco_data->m_index = l_indix++;
        l_mct_deco_data->m_array_type = MCT_TYPE_DECORRELATION;
        l_mct_deco_data->m_element_type = MCT_TYPE_FLOAT;
        l_nb_elem = p_image->numcomps * p_image->numcomps;
        l_mct_size = l_nb_elem * MCT_ELEMENT_SIZE[l_mct_deco_data->m_element_type];
        l_mct_deco_data->m_data = (uint8_t*)grok_malloc(l_mct_size );

        if (! l_mct_deco_data->m_data) {
            return false;
        }

        j2k_mct_write_functions_from_float[l_mct_deco_data->m_element_type](p_tcp->m_mct_decoding_matrix,l_mct_deco_data->m_data,l_nb_elem);

        l_mct_deco_data->m_data_size = l_mct_size;
        ++p_tcp->m_nb_mct_records;
    }

    if (p_tcp->m_nb_mct_records == p_tcp->m_nb_max_mct_records) {
        mct_data_t *new_mct_records;
        p_tcp->m_nb_max_mct_records += default_number_mct_records;
        new_mct_records = (mct_data_t *) grok_realloc(p_tcp->m_mct_records, p_tcp->m_nb_max_mct_records * sizeof(mct_data_t));
        if (! new_mct_records) {
            grok_free(p_tcp->m_mct_records);
            p_tcp->m_mct_records = nullptr;
            p_tcp->m_nb_max_mct_records = 0;
            p_tcp->m_nb_mct_records = 0;
            /* event_msg(p_manager, EVT_ERROR, "Not enough memory to setup mct encoding\n"); */
            return false;
        }
        p_tcp->m_mct_records = new_mct_records;
        l_mct_offset_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

        memset(l_mct_offset_data ,0,(p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records) * sizeof(mct_data_t));

        if (l_mct_deco_data) {
            l_mct_deco_data = l_mct_offset_data - 1;
        }
    }

    l_mct_offset_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

    if (l_mct_offset_data->m_data) {
        grok_free(l_mct_offset_data->m_data);
        l_mct_offset_data->m_data = nullptr;
    }

    l_mct_offset_data->m_index = l_indix++;
    l_mct_offset_data->m_array_type = MCT_TYPE_OFFSET;
    l_mct_offset_data->m_element_type = MCT_TYPE_FLOAT;
    l_nb_elem = p_image->numcomps;
    l_mct_size = l_nb_elem * MCT_ELEMENT_SIZE[l_mct_offset_data->m_element_type];
    l_mct_offset_data->m_data = (uint8_t*)grok_malloc(l_mct_size );

    if (! l_mct_offset_data->m_data) {
        return false;
    }

    l_data = (float*)grok_malloc(l_nb_elem * sizeof(float));
    if (! l_data) {
        grok_free(l_mct_offset_data->m_data);
        l_mct_offset_data->m_data = nullptr;
        return false;
    }

    l_tccp = p_tcp->tccps;
    l_current_data = l_data;

    for (i=0; i<l_nb_elem; ++i) {
        *(l_current_data++) = (float) (l_tccp->m_dc_level_shift);
        ++l_tccp;
    }

    j2k_mct_write_functions_from_float[l_mct_offset_data->m_element_type](l_data,l_mct_offset_data->m_data,l_nb_elem);

    grok_free(l_data);

    l_mct_offset_data->m_data_size = l_mct_size;

    ++p_tcp->m_nb_mct_records;

    if (p_tcp->m_nb_mcc_records == p_tcp->m_nb_max_mcc_records) {
        simple_mcc_decorrelation_data_t *new_mcc_records;
        p_tcp->m_nb_max_mcc_records += default_number_mct_records;
        new_mcc_records = (simple_mcc_decorrelation_data_t *) grok_realloc(
                              p_tcp->m_mcc_records, p_tcp->m_nb_max_mcc_records * sizeof(simple_mcc_decorrelation_data_t));
        if (! new_mcc_records) {
            grok_free(p_tcp->m_mcc_records);
            p_tcp->m_mcc_records = nullptr;
            p_tcp->m_nb_max_mcc_records = 0;
            p_tcp->m_nb_mcc_records = 0;
            /* event_msg(p_manager, EVT_ERROR, "Not enough memory to setup mct encoding\n"); */
            return false;
        }
        p_tcp->m_mcc_records = new_mcc_records;
        l_mcc_data = p_tcp->m_mcc_records + p_tcp->m_nb_mcc_records;
        memset(l_mcc_data ,0,(p_tcp->m_nb_max_mcc_records - p_tcp->m_nb_mcc_records) * sizeof(simple_mcc_decorrelation_data_t));

    }

    l_mcc_data = p_tcp->m_mcc_records + p_tcp->m_nb_mcc_records;
    l_mcc_data->m_decorrelation_array = l_mct_deco_data;
    l_mcc_data->m_is_irreversible = 1;
    l_mcc_data->m_nb_comps = p_image->numcomps;
    l_mcc_data->m_index = l_indix++;
    l_mcc_data->m_offset_array = l_mct_offset_data;
    ++p_tcp->m_nb_mcc_records;

    return true;
}

static bool j2k_build_decoder (j2k_t * p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
    /* add here initialization of cp
       copy paste of setup_decoder */
    (void)p_j2k;
    (void)p_stream;
    (void)p_manager;
    return true;
}

static bool j2k_build_encoder (j2k_t * p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
    /* add here initialization of cp
       copy paste of setup_encoder */
    (void)p_j2k;
    (void)p_stream;
    (void)p_manager;
    return true;
}

static bool j2k_encoding_validation (  j2k_t * p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager )
{
	(void)p_stream;
    bool l_is_valid = true;

    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    /* STATE checking */
    /* make sure the state is at 0 */
    l_is_valid &= (p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_NONE);

    /* POINTER validation */
    /* make sure a p_j2k codec is present */
    l_is_valid &= (p_j2k->m_procedure_list != nullptr);
    /* make sure a validation list is present */
    l_is_valid &= (p_j2k->m_validation_list != nullptr);

    /* ISO 15444-1:2004 states between 1 & 33 (decomposition levels between 0 -> 32) */
    if ((p_j2k->m_cp.tcps->tccps->numresolutions == 0) || (p_j2k->m_cp.tcps->tccps->numresolutions > OPJ_J2K_MAXRLVLS)) {
		event_msg(p_manager, EVT_ERROR, "Invalid number of resolutions : %d not in range [1,%d]\n", p_j2k->m_cp.tcps->tccps->numresolutions, OPJ_J2K_MAXRLVLS);
        return false;
    }

	if (p_j2k->m_cp.tdx == 0) {
		event_msg(p_manager, EVT_ERROR, "Tile x dimension must be greater than zero \n");
		return false;
	}

	if (p_j2k->m_cp.tdy == 0) {
		event_msg(p_manager, EVT_ERROR, "Tile y dimension must be greater than zero \n");
		return false;
	}

    /* PARAMETER VALIDATION */
    return l_is_valid;
}

static bool j2k_decoding_validation (  j2k_t *p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager
                                        )
{
	(void)p_manager;
	(void)p_stream;
    bool l_is_valid = true;

    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    /* STATE checking */
    /* make sure the state is at 0 */
    l_is_valid &= (p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_NONE);

    /* POINTER validation */
    /* make sure a p_j2k codec is present */
    /* make sure a procedure list is present */
    l_is_valid &= (p_j2k->m_procedure_list != nullptr);
    /* make sure a validation list is present */
    l_is_valid &= (p_j2k->m_validation_list != nullptr);

    /* PARAMETER VALIDATION */
    return l_is_valid;
}

static bool j2k_read_header_procedure( j2k_t *p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager)
{
    uint32_t l_current_marker;
    uint32_t l_marker_size;
    const opj_dec_memory_marker_handler_t * l_marker_handler = nullptr;
    bool l_has_siz = 0;
    bool l_has_cod = 0;
    bool l_has_qcd = 0;

    
    assert(p_stream != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    /*  We enter in the main header */
    p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_MHSOC;

    /* Try to read the SOC marker, the codestream must begin with SOC marker */
    if (! j2k_read_soc(p_j2k,p_stream,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Expected a SOC marker \n");
        return false;
    }

    /* Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer */
    if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
        event_msg(p_manager, EVT_ERROR, "Stream too short\n");
        return false;
    }

    /* Read 2 bytes as the new marker ID */
    grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_current_marker,2);

    /* Try to read until the SOT is detected */
    while (l_current_marker != J2K_MS_SOT) {

        /* Check if the current marker ID is valid */
        if (l_current_marker < 0xff00) {
            event_msg(p_manager, EVT_ERROR, "A marker ID was expected (0xff--) instead of %.8x\n", l_current_marker);
            return false;
        }

        /* Get the marker handler from the marker ID */
        l_marker_handler = j2k_get_marker_handler(l_current_marker);

        /* Manage case where marker is unknown */
        if (l_marker_handler->id == J2K_MS_UNK) {
            if (! j2k_read_unk(p_j2k, p_stream, &l_current_marker, p_manager)) {
                event_msg(p_manager, EVT_ERROR, "Unknown marker have been detected and generated error.\n");
                return false;
            }

            if (l_current_marker == J2K_MS_SOT)
                break; /* SOT marker is detected main header is completely read */
            else    /* Get the marker handler from the marker ID */
                l_marker_handler = j2k_get_marker_handler(l_current_marker);
        }

        if (l_marker_handler->id == J2K_MS_SIZ) {
            /* Mark required SIZ marker as found */
            l_has_siz = 1;
        }
        if (l_marker_handler->id == J2K_MS_COD) {
            /* Mark required COD marker as found */
            l_has_cod = 1;
        }
        if (l_marker_handler->id == J2K_MS_QCD) {
            /* Mark required QCD marker as found */
            l_has_qcd = 1;
        }

        /* Check if the marker is known and if it is the right place to find it */
        if (! (p_j2k->m_specific_param.m_decoder.m_state & l_marker_handler->states) ) {
            event_msg(p_manager, EVT_ERROR, "Marker is not compliant with its position\n");
            return false;
        }

        /* Try to read 2 bytes (the marker size) from stream and copy them into the buffer */
        if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
            event_msg(p_manager, EVT_ERROR, "Stream too short\n");
            return false;
        }

        /* read 2 bytes as the marker size */
        grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_marker_size,2);
		
		/* Check marker size (does not include marker ID but includes marker size) */
		if (l_marker_size < 2) {
			event_msg(p_manager, EVT_ERROR, "Inconsistent marker size\n");
			return false;
		}

        l_marker_size -= 2; /* Subtract the size of the marker ID already read */

        /* Check if the marker size is compatible with the header data size */
        if (l_marker_size > p_j2k->m_specific_param.m_decoder.m_header_data_size) {
            uint8_t *new_header_data = (uint8_t *) grok_realloc(p_j2k->m_specific_param.m_decoder.m_header_data, l_marker_size);
            if (! new_header_data) {
                grok_free(p_j2k->m_specific_param.m_decoder.m_header_data);
                p_j2k->m_specific_param.m_decoder.m_header_data = nullptr;
                p_j2k->m_specific_param.m_decoder.m_header_data_size = 0;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to read header\n");
                return false;
            }
            p_j2k->m_specific_param.m_decoder.m_header_data = new_header_data;
            p_j2k->m_specific_param.m_decoder.m_header_data_size = l_marker_size;
        }

        /* Try to read the rest of the marker segment from stream and copy them into the buffer */
        if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,l_marker_size,p_manager) != l_marker_size) {
            event_msg(p_manager, EVT_ERROR, "Stream too short\n");
            return false;
        }

        /* Read the marker segment with the correct marker handler */
        if (! (*(l_marker_handler->handler))(p_j2k,p_j2k->m_specific_param.m_decoder.m_header_data,l_marker_size,p_manager)) {
            event_msg(p_manager, EVT_ERROR, "Marker handler function failed to read the marker segment\n");
            return false;
        }

		if (p_j2k->cstr_index) {
			/* Add the marker to the codestream index*/
			if (false == j2k_add_mhmarker(
				p_j2k->cstr_index,
				l_marker_handler->id,
				(uint32_t)p_stream->tell() - l_marker_size - 4,
				l_marker_size + 4)) {
				event_msg(p_manager, EVT_ERROR, "Not enough memory to add mh marker\n");
				return false;
			}
		}

        /* Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer */
        if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
            event_msg(p_manager, EVT_ERROR, "Stream too short\n");
            return false;
        }

        /* read 2 bytes as the new marker ID */
        grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_current_marker,2);
    }

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//do QCD marker quantization step size sanity check
	auto l_tcp = j2k_get_tcp(p_j2k);
	if (l_tcp->qntsty != J2K_CCP_QNTSTY_SIQNT) {
		uint32_t maxDecompositions = 0;
		for (uint32_t k = 0; k < p_j2k->m_private_image->numcomps; ++k) {
			auto l_tccp = l_tcp->tccps + k;
			if (l_tccp->numresolutions == 0 || l_tccp->hasQCC)
				continue;
			auto decomps = l_tccp->numresolutions - 1;
			if (maxDecompositions < decomps)
				maxDecompositions = decomps;
		}

		// see page 553 of Taubman and Marcellin for more details on this check
		if ((l_tcp->numStepSizes < 3 * maxDecompositions + 1)) {
			event_msg(p_manager, EVT_ERROR, "From QCD marker, "
				"number of step sizes (%d) is less than 3* (max decompositions) + 1, where max decompositions = %d \n", l_tcp->numStepSizes, maxDecompositions);
			return false;
		}
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
    if (l_has_siz == 0) {
        event_msg(p_manager, EVT_ERROR, "required SIZ marker not found in main header\n");
        return false;
    }
    if (l_has_cod == 0) {
        event_msg(p_manager, EVT_ERROR, "required COD marker not found in main header\n");
        return false;
    }
    if (l_has_qcd == 0) {
        event_msg(p_manager, EVT_ERROR, "required QCD marker not found in main header\n");
        return false;
    }

    if (! j2k_merge_ppm(&(p_j2k->m_cp), p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Failed to merge PPM data\n");
        return false;
    }
	// event_msg(p_manager, EVT_INFO, "Main header has been correctly decoded.\n");
	if (p_j2k->cstr_index) {
		/* Position of the last element if the main header */
		p_j2k->cstr_index->main_head_end = (uint32_t)p_stream->tell() - 2;
	}

    /* Next step: read a tile-part header */
    p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_TPHSOT;

    return true;
}

static bool j2k_exec ( j2k_t * p_j2k,
                           procedure_list_t * p_procedure_list,
                           GrokStream *p_stream,
                           event_mgr_t * p_manager )
{
    bool (** l_procedure) (j2k_t * ,GrokStream *,event_mgr_t *) = nullptr;
    bool l_result = true;
    uint32_t l_nb_proc, i;

    /* preconditions*/
    assert(p_procedure_list != nullptr);
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    l_nb_proc = procedure_list_get_nb_procedures(p_procedure_list);
    l_procedure = (bool (**) (j2k_t * ,GrokStream *,event_mgr_t *)) procedure_list_get_first_procedure(p_procedure_list);

    for     (i=0; i<l_nb_proc; ++i) {
        l_result = l_result && ((*l_procedure) (p_j2k,p_stream,p_manager));
        ++l_procedure;
    }

    /* and clear the procedure list at the end.*/
    procedure_list_clear(p_procedure_list);
    return l_result;
}

/* FIXME DOC*/
static bool j2k_copy_default_tcp_and_create_tcd (       j2k_t * p_j2k,
        GrokStream *p_stream,
        event_mgr_t * p_manager
                                                    )
{
	(void)p_manager;
	(void)p_stream;
    tcp_t * l_tcp = nullptr;
    tcp_t * l_default_tcp = nullptr;
    uint32_t l_nb_tiles;
    uint32_t i,j;
    tccp_t *l_current_tccp = nullptr;
    uint32_t l_tccp_size;
    uint32_t l_mct_size;
    opj_image_t * l_image;
    uint32_t l_mcc_records_size,l_mct_records_size;
    mct_data_t * l_src_mct_rec, *l_dest_mct_rec;
    simple_mcc_decorrelation_data_t * l_src_mcc_rec, *l_dest_mcc_rec;
    uint32_t l_offset;

    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    l_image = p_j2k->m_private_image;
    l_nb_tiles = p_j2k->m_cp.th * p_j2k->m_cp.tw;
    l_tcp = p_j2k->m_cp.tcps;
    l_tccp_size = l_image->numcomps * (uint32_t)sizeof(tccp_t);
    l_default_tcp = p_j2k->m_specific_param.m_decoder.m_default_tcp;
    l_mct_size = l_image->numcomps * l_image->numcomps * (uint32_t)sizeof(float);

    /* For each tile */
    for (i=0; i<l_nb_tiles; ++i) {
        /* keep the tile-compo coding parameters pointer of the current tile coding parameters*/
        l_current_tccp = l_tcp->tccps;
        /*Copy default coding parameters into the current tile coding parameters*/
        memcpy(l_tcp, l_default_tcp, sizeof(tcp_t));
        /* Initialize some values of the current tile coding parameters*/
        l_tcp->cod = 0;
        l_tcp->ppt = 0;
        l_tcp->ppt_data = nullptr;
        /* Remove memory not owned by this tile in case of early error return. */
        l_tcp->m_mct_decoding_matrix = nullptr;
        l_tcp->m_nb_max_mct_records = 0;
        l_tcp->m_mct_records = nullptr;
        l_tcp->m_nb_max_mcc_records = 0;
        l_tcp->m_mcc_records = nullptr;
        /* Reconnect the tile-compo coding parameters pointer to the current tile coding parameters*/
        l_tcp->tccps = l_current_tccp;

        /* Get the mct_decoding_matrix of the dflt_tile_cp and copy them into the current tile cp*/
        if (l_default_tcp->m_mct_decoding_matrix) {
            l_tcp->m_mct_decoding_matrix = (float*)grok_malloc(l_mct_size);
            if (! l_tcp->m_mct_decoding_matrix ) {
                return false;
            }
            memcpy(l_tcp->m_mct_decoding_matrix,l_default_tcp->m_mct_decoding_matrix,l_mct_size);
        }

        /* Get the mct_record of the dflt_tile_cp and copy them into the current tile cp*/
        l_mct_records_size = l_default_tcp->m_nb_max_mct_records * (uint32_t)sizeof(mct_data_t);
        l_tcp->m_mct_records = (mct_data_t*)grok_malloc(l_mct_records_size);
        if (! l_tcp->m_mct_records) {
            return false;
        }
        memcpy(l_tcp->m_mct_records, l_default_tcp->m_mct_records,l_mct_records_size);

        /* Copy the mct record data from dflt_tile_cp to the current tile*/
        l_src_mct_rec = l_default_tcp->m_mct_records;
        l_dest_mct_rec = l_tcp->m_mct_records;

        for (j=0; j<l_default_tcp->m_nb_mct_records; ++j) {

            if (l_src_mct_rec->m_data) {

                l_dest_mct_rec->m_data = (uint8_t*) grok_malloc(l_src_mct_rec->m_data_size);
                if(! l_dest_mct_rec->m_data) {
                    return false;
                }
                memcpy(l_dest_mct_rec->m_data,l_src_mct_rec->m_data,l_src_mct_rec->m_data_size);
            }

            ++l_src_mct_rec;
            ++l_dest_mct_rec;
            /* Update with each pass to free exactly what has been allocated on early return. */
            l_tcp->m_nb_max_mct_records += 1;
        }

        /* Get the mcc_record of the dflt_tile_cp and copy them into the current tile cp*/
        l_mcc_records_size = l_default_tcp->m_nb_max_mcc_records * (uint32_t)sizeof(simple_mcc_decorrelation_data_t);
        l_tcp->m_mcc_records = (simple_mcc_decorrelation_data_t*) grok_malloc(l_mcc_records_size);
        if (! l_tcp->m_mcc_records) {
            return false;
        }
        memcpy(l_tcp->m_mcc_records,l_default_tcp->m_mcc_records,l_mcc_records_size);
        l_tcp->m_nb_max_mcc_records = l_default_tcp->m_nb_max_mcc_records;

        /* Copy the mcc record data from dflt_tile_cp to the current tile*/
        l_src_mcc_rec = l_default_tcp->m_mcc_records;
        l_dest_mcc_rec = l_tcp->m_mcc_records;

        for (j=0; j<l_default_tcp->m_nb_max_mcc_records; ++j) {

            if (l_src_mcc_rec->m_decorrelation_array) {
                l_offset = (uint32_t)(l_src_mcc_rec->m_decorrelation_array - l_default_tcp->m_mct_records);
                l_dest_mcc_rec->m_decorrelation_array = l_tcp->m_mct_records + l_offset;
            }

            if (l_src_mcc_rec->m_offset_array) {
                l_offset = (uint32_t)(l_src_mcc_rec->m_offset_array - l_default_tcp->m_mct_records);
                l_dest_mcc_rec->m_offset_array = l_tcp->m_mct_records + l_offset;
            }

            ++l_src_mcc_rec;
            ++l_dest_mcc_rec;
        }

        /* Copy all the dflt_tile_compo_cp to the current tile cp */
        memcpy(l_current_tccp,l_default_tcp->tccps,l_tccp_size);

        /* Move to next tile cp*/
        ++l_tcp;
    }

    /* Create the current tile decoder*/
    p_j2k->m_tcd = tcd_create(true); 
    if (! p_j2k->m_tcd ) {
        return false;
    }

    if ( !tcd_init(p_j2k->m_tcd, l_image, &(p_j2k->m_cp), p_j2k->numThreads) ) {
        tcd_destroy(p_j2k->m_tcd);
        p_j2k->m_tcd = nullptr;
        event_msg(p_manager, EVT_ERROR, "Cannot decode tile, memory error\n");
        return false;
    }

    return true;
}

static const opj_dec_memory_marker_handler_t * j2k_get_marker_handler (uint32_t p_id)
{
    const opj_dec_memory_marker_handler_t *e;
    for (e = j2k_memory_marker_handler_tab; e->id != 0; ++e) {
        if (e->id == p_id) {
            break; /* we find a handler corresponding to the marker ID*/
        }
    }
    return e;
}

void j2k_destroy (j2k_t *p_j2k)
{
    if (p_j2k == nullptr) {
        return;
    }

    if (p_j2k->m_is_decoder) {

        if (p_j2k->m_specific_param.m_decoder.m_default_tcp != nullptr) {
            j2k_tcp_destroy(p_j2k->m_specific_param.m_decoder.m_default_tcp);
            delete p_j2k->m_specific_param.m_decoder.m_default_tcp;
            p_j2k->m_specific_param.m_decoder.m_default_tcp = nullptr;
        }

        if (p_j2k->m_specific_param.m_decoder.m_header_data != nullptr) {
            grok_free(p_j2k->m_specific_param.m_decoder.m_header_data);
            p_j2k->m_specific_param.m_decoder.m_header_data = nullptr;
            p_j2k->m_specific_param.m_decoder.m_header_data_size = 0;
        }
    } else {

        if (p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer) {
            grok_free(p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer);
            p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer = nullptr;
            p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current = nullptr;
        }
    }

    tcd_destroy(p_j2k->m_tcd);

    j2k_cp_destroy(&(p_j2k->m_cp));
    memset(&(p_j2k->m_cp),0,sizeof(cp_t));

    procedure_list_destroy(p_j2k->m_procedure_list);
    p_j2k->m_procedure_list = nullptr;

    procedure_list_destroy(p_j2k->m_validation_list);
    p_j2k->m_procedure_list = nullptr;

    j2k_destroy_cstr_index(p_j2k->cstr_index);
    p_j2k->cstr_index = nullptr;

    opj_image_destroy(p_j2k->m_private_image);
    p_j2k->m_private_image = nullptr;

    opj_image_destroy(p_j2k->m_output_image);
    p_j2k->m_output_image = nullptr;

    grok_free(p_j2k);
}

void j2k_destroy_cstr_index (opj_codestream_index_t *p_cstr_ind)
{
    if (p_cstr_ind) {

        if (p_cstr_ind->marker) {
            grok_free(p_cstr_ind->marker);
            p_cstr_ind->marker = nullptr;
        }

        if (p_cstr_ind->tile_index) {
            uint32_t it_tile = 0;

            for (it_tile=0; it_tile < p_cstr_ind->nb_of_tiles; it_tile++) {

                if(p_cstr_ind->tile_index[it_tile].packet_index) {
                    grok_free(p_cstr_ind->tile_index[it_tile].packet_index);
                    p_cstr_ind->tile_index[it_tile].packet_index = nullptr;
                }

                if(p_cstr_ind->tile_index[it_tile].tp_index) {
                    grok_free(p_cstr_ind->tile_index[it_tile].tp_index);
                    p_cstr_ind->tile_index[it_tile].tp_index = nullptr;
                }

                if(p_cstr_ind->tile_index[it_tile].marker) {
                    grok_free(p_cstr_ind->tile_index[it_tile].marker);
                    p_cstr_ind->tile_index[it_tile].marker = nullptr;

                }
            }

            grok_free( p_cstr_ind->tile_index);
            p_cstr_ind->tile_index = nullptr;
        }

        grok_free(p_cstr_ind);
    }
}

static void j2k_tcp_destroy (tcp_t *p_tcp)
{
    if (p_tcp == nullptr) {
        return;
    }

    if (p_tcp->ppt_markers != nullptr) {
        uint32_t i;
        for (i = 0U; i < p_tcp->ppt_markers_count; ++i) {
            if (p_tcp->ppt_markers[i].m_data != nullptr) {
                grok_free(p_tcp->ppt_markers[i].m_data);
            }
        }
        p_tcp->ppt_markers_count = 0U;
        grok_free(p_tcp->ppt_markers);
        p_tcp->ppt_markers = nullptr;
    }

    if (p_tcp->ppt_buffer != nullptr) {
        grok_free(p_tcp->ppt_buffer);
        p_tcp->ppt_buffer = nullptr;
    }

    if (p_tcp->tccps != nullptr) {
        grok_free(p_tcp->tccps);
        p_tcp->tccps = nullptr;
    }

    if (p_tcp->m_mct_coding_matrix != nullptr) {
        grok_free(p_tcp->m_mct_coding_matrix);
        p_tcp->m_mct_coding_matrix = nullptr;
    }

    if (p_tcp->m_mct_decoding_matrix != nullptr) {
        grok_free(p_tcp->m_mct_decoding_matrix);
        p_tcp->m_mct_decoding_matrix = nullptr;
    }

    if (p_tcp->m_mcc_records) {
        grok_free(p_tcp->m_mcc_records);
        p_tcp->m_mcc_records = nullptr;
        p_tcp->m_nb_max_mcc_records = 0;
        p_tcp->m_nb_mcc_records = 0;
    }

    if (p_tcp->m_mct_records) {
        mct_data_t * l_mct_data = p_tcp->m_mct_records;
        uint32_t i;

        for (i=0; i<p_tcp->m_nb_mct_records; ++i) {
            if (l_mct_data->m_data) {
                grok_free(l_mct_data->m_data);
                l_mct_data->m_data = nullptr;
            }

            ++l_mct_data;
        }

        grok_free(p_tcp->m_mct_records);
        p_tcp->m_mct_records = nullptr;
    }

    if (p_tcp->mct_norms != nullptr) {
        grok_free(p_tcp->mct_norms);
        p_tcp->mct_norms = nullptr;
    }

    j2k_tcp_data_destroy(p_tcp);

}

static void j2k_tcp_data_destroy (tcp_t *p_tcp)
{
	delete p_tcp->m_tile_data;
	p_tcp->m_tile_data = nullptr;
}

static void j2k_cp_destroy (cp_t *p_cp)
{
    uint32_t l_nb_tiles;
    tcp_t * l_current_tile = nullptr;

    if (p_cp == nullptr) {
        return;
    }
    if (p_cp->tcps != nullptr) {
        uint32_t i;
        l_current_tile = p_cp->tcps;
        l_nb_tiles = p_cp->th * p_cp->tw;

        for (i = 0U; i < l_nb_tiles; ++i) {
            j2k_tcp_destroy(l_current_tile);
            ++l_current_tile;
        }
        grok_free(p_cp->tcps);
        p_cp->tcps = nullptr;
    }
    if (p_cp->ppm_markers != nullptr) {
        uint32_t i;
        for (i = 0U; i < p_cp->ppm_markers_count; ++i) {
            if (p_cp->ppm_markers[i].m_data != nullptr) {
                grok_free(p_cp->ppm_markers[i].m_data);
            }
        }
        p_cp->ppm_markers_count = 0U;
        grok_free(p_cp->ppm_markers);
        p_cp->ppm_markers = nullptr;
    }
    grok_free(p_cp->ppm_buffer);
    p_cp->ppm_buffer = nullptr;
    p_cp->ppm_data = nullptr; /* ppm_data belongs to the allocated buffer pointed by ppm_buffer */
    grok_free(p_cp->comment);
    p_cp->comment = nullptr;
}

static bool j2k_need_nb_tile_parts_correction(GrokStream *p_stream, uint32_t tile_no, bool* p_correction_needed, event_mgr_t * p_manager )
{
    uint8_t   l_header_data[10];
    int64_t  l_stream_pos_backup;
    uint32_t l_current_marker;
    uint32_t l_marker_size;
    uint32_t l_tile_no, l_current_part, l_num_parts;
	uint32_t l_tot_len;

    /* initialize to no correction needed */
    *p_correction_needed = false;

    if (!p_stream->has_seek()) {
        /* We can't do much in this case, seek is needed */
        return true;
    }

    l_stream_pos_backup = p_stream->tell();
    if (l_stream_pos_backup == -1) {
        /* let's do nothing */
        return true;
    }

    for (;;) {
        /* Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer */
        if (p_stream->read(l_header_data, 2, p_manager) != 2) {
            /* assume all is OK */
            if (! p_stream->seek( l_stream_pos_backup, p_manager)) {
                return false;
            }
            return true;
        }

        /* Read 2 bytes from buffer as the new marker ID */
        grok_read_bytes(l_header_data, &l_current_marker, 2);

        if (l_current_marker != J2K_MS_SOT) {
            /* assume all is OK */
            if (! p_stream->seek( l_stream_pos_backup, p_manager)) {
                return false;
            }
            return true;
        }

        /* Try to read 2 bytes (the marker size) from stream and copy them into the buffer */
        if (p_stream->read( l_header_data, 2, p_manager) != 2) {
            event_msg(p_manager, EVT_ERROR, "Stream too short\n");
            return false;
        }

        /* Read 2 bytes from the buffer as the marker size */
        grok_read_bytes(l_header_data, &l_marker_size, 2);

        /* Check marker size for SOT Marker */
        if (l_marker_size != 10) {
            event_msg(p_manager, EVT_ERROR, "Inconsistent marker size\n");
            return false;
        }
        l_marker_size -= 2;

        if (p_stream->read( l_header_data, l_marker_size, p_manager) != l_marker_size) {
            event_msg(p_manager, EVT_ERROR, "Stream too short\n");
            return false;
        }

        if (! j2k_get_sot_values(l_header_data, l_marker_size, &l_tile_no, &l_tot_len, &l_current_part, &l_num_parts, p_manager)) {
            return false;
        }

        if (l_tile_no == tile_no) {
            /* we found what we were looking for */
            break;
        }

        if ((l_tot_len == 0U) || (l_tot_len < 14U)) {
            /* last SOT until EOC or invalid Psot value */
            /* assume all is OK */
            if (! p_stream->seek( l_stream_pos_backup, p_manager)) {
                return false;
            }
            return true;
        }
        l_tot_len -= 12U;
        /* look for next SOT marker */
        if (!p_stream->skip( (int64_t)(l_tot_len), p_manager)) {
            /* assume all is OK */
            if (! p_stream->seek( l_stream_pos_backup, p_manager)) {
                return false;
            }
            return true;
        }
    }

    /* check for correction */
    if (l_current_part == l_num_parts) {
        *p_correction_needed = true;
    }

    if (! p_stream->seek( l_stream_pos_backup, p_manager)) {
        return false;
    }
    return true;
}

bool j2k_read_tile_header(      j2k_t * p_j2k,
                                    uint32_t * p_tile_index,
                                    uint64_t * p_data_size,
                                    uint32_t * p_tile_x0,
									uint32_t * p_tile_y0,
                                    uint32_t * p_tile_x1,
									uint32_t * p_tile_y1,
                                    uint32_t * p_nb_comps,
                                    bool * p_go_on,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager )
{
    uint32_t l_current_marker = J2K_MS_SOT;
    uint32_t l_marker_size;
    const opj_dec_memory_marker_handler_t * l_marker_handler = nullptr;
    tcp_t * l_tcp = nullptr;

    
    assert(p_stream != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    /* Reach the End Of Codestream ?*/
    if (p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_EOC) {
        l_current_marker = J2K_MS_EOC;
    }
    /* We need to encounter a SOT marker (a new tile-part header) */
    else if (p_j2k->m_specific_param.m_decoder.m_state != J2K_DEC_STATE_TPHSOT) {
        return false;
    }

    /* Read into the codestream until reach the EOC or ! can_decode ??? FIXME */
    while ( (!p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data) && (l_current_marker != J2K_MS_EOC) ) {

        /* Try to read until the Start Of Data is detected */
        while (l_current_marker != J2K_MS_SOD) {

            if(p_stream->get_number_byte_left() == 0) {
                p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_NEOC;
                break;
            }

            /* Try to read 2 bytes (the marker size) from stream and copy them into the buffer */
            if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
                event_msg(p_manager, EVT_ERROR, "Stream too short\n");
                return false;
            }

            /* Read 2 bytes from the buffer as the marker size */
            grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_marker_size,2);

            /* Check marker size (does not include marker ID but includes marker size) */
            if (l_marker_size < 2) {
                event_msg(p_manager, EVT_ERROR, "Inconsistent marker size\n");
                return false;
            }

            // subtract tile part header and header marker size
            if (p_j2k->m_specific_param.m_decoder.m_state & J2K_DEC_STATE_TPH) {
                p_j2k->m_specific_param.m_decoder.tile_part_data_length -= (l_marker_size + 2);
            }

            l_marker_size -= 2; /* Subtract the size of the marker ID already read */

            /* Get the marker handler from the marker ID */
            l_marker_handler = j2k_get_marker_handler(l_current_marker);

            /* Check if the marker is known and if it is the right place to find it */
            if (! (p_j2k->m_specific_param.m_decoder.m_state & l_marker_handler->states) ) {
                event_msg(p_manager, EVT_ERROR, "Marker is not compliant with its position\n");
                return false;
            }
            /* FIXME manage case of unknown marker as in the main header ? */

            /* Check if the marker size is compatible with the header data size */
            if (l_marker_size > p_j2k->m_specific_param.m_decoder.m_header_data_size) {
                uint8_t *new_header_data = nullptr;
                /* If we are here, this means we consider this marker as known & we will read it */
                /* Check enough bytes left in stream before allocation */
                if ((int64_t)l_marker_size >  p_stream->get_number_byte_left()) {
                    event_msg(p_manager, EVT_ERROR, "Marker size inconsistent with stream length\n");
                    return false;
                }
                new_header_data = (uint8_t *) grok_realloc(p_j2k->m_specific_param.m_decoder.m_header_data, l_marker_size);
                if (! new_header_data) {
                    grok_free(p_j2k->m_specific_param.m_decoder.m_header_data);
                    p_j2k->m_specific_param.m_decoder.m_header_data = nullptr;
                    p_j2k->m_specific_param.m_decoder.m_header_data_size = 0;
                    event_msg(p_manager, EVT_ERROR, "Not enough memory to read header\n");
                    return false;
                }
                p_j2k->m_specific_param.m_decoder.m_header_data = new_header_data;
                p_j2k->m_specific_param.m_decoder.m_header_data_size = l_marker_size;
            }

            /* Try to read the rest of the marker segment from stream and copy them into the buffer */
            if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,l_marker_size,p_manager) != l_marker_size) {
                event_msg(p_manager, EVT_ERROR, "Stream too short\n");
                return false;
            }

            if (!l_marker_handler->handler) {
                /* See issue #175 */
                event_msg(p_manager, EVT_ERROR, "Not sure how that happened.\n");
                return false;
            }
            /* Read the marker segment with the correct marker handler */
            if (! (*(l_marker_handler->handler))(p_j2k,p_j2k->m_specific_param.m_decoder.m_header_data,l_marker_size,p_manager)) {
                event_msg(p_manager, EVT_ERROR, "Fail to read the current marker segment (%#x)\n", l_current_marker);
                return false;
            }

			if (p_j2k->cstr_index) {
				/* Add the marker to the codestream index*/
				if (false == j2k_add_tlmarker(p_j2k->m_current_tile_number,
					p_j2k->cstr_index,
					l_marker_handler->id,
					(uint32_t)p_stream->tell() - l_marker_size - 4,
					l_marker_size + 4)) {
					event_msg(p_manager, EVT_ERROR, "Not enough memory to add tl marker\n");
					return false;
				}
			}

            // Cache position of last SOT marker read 
            if ( l_marker_handler->id == J2K_MS_SOT ) {
                uint32_t sot_pos = (uint32_t) p_stream->tell() - l_marker_size - 4 ;
                if (sot_pos > p_j2k->m_specific_param.m_decoder.m_last_sot_read_pos) {
                    p_j2k->m_specific_param.m_decoder.m_last_sot_read_pos = sot_pos;
                }
            }

            if (p_j2k->m_specific_param.m_decoder.m_skip_data) {
                // Skip the rest of the tile part header
                if (!p_stream->skip(p_j2k->m_specific_param.m_decoder.tile_part_data_length,p_manager)) {
                    event_msg(p_manager, EVT_ERROR, "Stream too short\n");
                    return false;
                }
                l_current_marker = J2K_MS_SOD; //We force current marker to equal SOD
            } else {
                // Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer
                if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
                    event_msg(p_manager, EVT_ERROR, "Stream too short\n");
                    return false;
                }
                // Read 2 bytes from the buffer as the new marker ID 
                grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_current_marker,2);
            }
        }

		// no bytes left and no EOC marker : we're done!
        if(!p_stream->get_number_byte_left()
				&& p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_NEOC)
            break;

        /* If we didn't skip data before, we need to read the SOD marker*/
        if (! p_j2k->m_specific_param.m_decoder.m_skip_data) {
            /* Try to read the SOD marker and skip data ? FIXME */
            if (! j2k_read_sod(p_j2k, p_stream, p_manager)) {
                return false;
            }
            if (p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data && !p_j2k->m_specific_param.m_decoder.m_nb_tile_parts_correction_checked) {
                /* Issue 254 */
                bool l_correction_needed;

                p_j2k->m_specific_param.m_decoder.m_nb_tile_parts_correction_checked = 1;
                if(!j2k_need_nb_tile_parts_correction(p_stream, p_j2k->m_current_tile_number, &l_correction_needed, p_manager)) {
                    event_msg(p_manager, EVT_ERROR, "j2k_apply_nb_tile_parts_correction error\n");
                    return false;
                }
                if (l_correction_needed) {
                    uint32_t l_nb_tiles = p_j2k->m_cp.tw * p_j2k->m_cp.th;
                    uint32_t l_tile_no;
                    p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data = 0;
                    p_j2k->m_specific_param.m_decoder.m_nb_tile_parts_correction = 1;
                    /* correct tiles */
                    for (l_tile_no = 0U; l_tile_no < l_nb_tiles; ++l_tile_no) {
                        if (p_j2k->m_cp.tcps[l_tile_no].m_nb_tile_parts != 0U) {
                            p_j2k->m_cp.tcps[l_tile_no].m_nb_tile_parts+=1;
                        }
                    }
                    event_msg(p_manager, EVT_WARNING, "Non conformant codestream TPsot==TNsot.\n");
                }
            }
            if (! p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data) {
                /* Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer */
                if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
                    event_msg(p_manager, EVT_ERROR, "Stream too short\n");
                    return false;
                }

                /* Read 2 bytes from buffer as the new marker ID */
                grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_current_marker,2);
            }
        } else {
            /* Indicate we will try to read a new tile-part header*/
            p_j2k->m_specific_param.m_decoder.m_skip_data = 0;
            p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data = 0;
            p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_TPHSOT;

            /* Try to read 2 bytes (the next marker ID) from stream and copy them into the buffer */
            if (p_stream->read(p_j2k->m_specific_param.m_decoder.m_header_data,2,p_manager) != 2) {
                event_msg(p_manager, EVT_ERROR, "Stream too short\n");
                return false;
            }
            /* Read 2 bytes from buffer as the new marker ID */
            grok_read_bytes(p_j2k->m_specific_param.m_decoder.m_header_data,&l_current_marker,2);
        }
    }

    /* Current marker is the EOC marker ?*/
    if (l_current_marker == J2K_MS_EOC) {
       p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_EOC;
    }

    //if we are not ready to decode tile part data, then skip tiles with no tile data
	// !! Why ???
    if ( ! p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data) {
        uint32_t l_nb_tiles = p_j2k->m_cp.th * p_j2k->m_cp.tw;
        l_tcp = p_j2k->m_cp.tcps + p_j2k->m_current_tile_number;
        while( (p_j2k->m_current_tile_number < l_nb_tiles) && (!l_tcp->m_tile_data) ) {
            ++p_j2k->m_current_tile_number;
            ++l_tcp;
        }
        if (p_j2k->m_current_tile_number == l_nb_tiles) {
            *p_go_on = false;
            return true;
        }
    }

    if (! j2k_merge_ppt(p_j2k->m_cp.tcps + p_j2k->m_current_tile_number, p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Failed to merge PPT data\n");
        return false;
    }
    /*FIXME ???*/
    l_tcp = p_j2k->m_specific_param.m_decoder.m_default_tcp;
    if (! tcd_init_decode_tile(p_j2k->m_tcd,
                                   p_j2k->m_output_image,
                                   p_j2k->m_current_tile_number,
                                   p_manager)) {
		event_msg(p_manager, EVT_ERROR, "Cannot decode tile %d\n", p_j2k->m_current_tile_number);
        return false;
    }

    //event_msg(p_manager, EVT_INFO, "Header of tile %d / %d has been read.\n",
    //              p_j2k->m_current_tile_number+1, (p_j2k->m_cp.th * p_j2k->m_cp.tw));

    *p_tile_index	= p_j2k->m_current_tile_number;
    *p_go_on		= true;
    *p_data_size	= tcd_get_decoded_tile_size(p_j2k->m_tcd);
    *p_tile_x0		= p_j2k->m_tcd->tile->x0;
    *p_tile_y0		= p_j2k->m_tcd->tile->y0;
    *p_tile_x1		= p_j2k->m_tcd->tile->x1;
    *p_tile_y1		= p_j2k->m_tcd->tile->y1;
    *p_nb_comps		= p_j2k->m_tcd->tile->numcomps;
    p_j2k->m_specific_param.m_decoder.m_state |= J2K_DEC_STATE_DATA;
    return true;
}

bool j2k_decode_tile (  j2k_t * p_j2k,
                            uint32_t p_tile_index,
                            uint8_t * p_data,
                            uint64_t p_data_size,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager )
{
    uint32_t l_current_marker;
    uint8_t l_data [2];
    tcp_t * l_tcp;

    
    assert(p_stream != nullptr);
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if ( !(p_j2k->m_specific_param.m_decoder.m_state & J2K_DEC_STATE_DATA)
            || (p_tile_index != p_j2k->m_current_tile_number) ) {
        return false;
    }

    l_tcp = p_j2k->m_cp.tcps + p_tile_index;
    if (!l_tcp->m_tile_data) {
        j2k_tcp_destroy(l_tcp);
        return false;
    }

    if (! tcd_decode_tile(  p_j2k->m_tcd,
                                l_tcp->m_tile_data,
                                p_tile_index,
                                p_manager) ) {
        j2k_tcp_destroy(l_tcp);
        p_j2k->m_specific_param.m_decoder.m_state |= J2K_DEC_STATE_ERR;
        event_msg(p_manager, EVT_ERROR, "Failed to decode.\n");
        return false;
    }

	if (!p_j2k->m_tcd->current_plugin_tile || (p_j2k->m_tcd->current_plugin_tile->decode_flags & GROK_DECODE_POST_T1)) {

		/* if p_data is not null, then copy decoded resolutions from tile data into p_data.
		Otherwise, simply copy tile data pointer to output image
		*/
		if (p_data) {
			if (!tcd_update_tile_data(p_j2k->m_tcd, p_data, p_data_size)) {
				return false;
			}
		}
		else {
			/* transfer data from tile component to output image */
			uint32_t compno = 0;
			for (compno = 0; compno < p_j2k->m_output_image->numcomps; compno++) {
				uint32_t l_size_comp = 0;
				uint32_t i, j;
				tcd_tilecomp_t* tilec = p_j2k->m_tcd->tile->comps + compno;
				opj_image_comp_t* comp = p_j2k->m_output_image->comps + compno;

				//transfer memory from tile component to output image
				comp->data = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
				comp->owns_data = tilec->buf->owns_data;
				tilec->buf->data = nullptr;
				tilec->buf->owns_data = false;

				comp->resno_decoded = p_j2k->m_tcd->image->comps[compno].resno_decoded;

				/* now sanitize data */
				//cast and mask in unsigned case, to avoid sign extension
				l_size_comp = (comp->prec + 7) >> 3;
				if (l_size_comp <= 2) {
					for (j = 0; j < comp->h; ++j) {
						for (i = 0; i < comp->w; ++i) {
							if (l_size_comp == 1)
								comp->data[i + j*comp->w] =
												comp->sgnd ? comp->data[i + j*comp->w] :
															(char)comp->data[i + j*comp->w] & 0xFF;
							else
								comp->data[i + j*comp->w] =
											comp->sgnd ? comp->data[i + j*comp->w] :
														(int16_t)comp->data[i + j*comp->w] & 0xFFFF;	

						}
					}
				}

			}
		}

		/* we only destroy the data, which will be re-read in read_tile_header*/
		j2k_tcp_data_destroy(l_tcp);

		p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data = 0;
		p_j2k->m_specific_param.m_decoder.m_state &= (~(J2K_DEC_STATE_DATA));

		// if there is no EOC marker and there is also no data left, then simply return true
		if (p_stream->get_number_byte_left() == 0
			&& p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_NEOC) {
			return true;
		}

		// if EOC marker has not been read yet, then try to read the next marker (should be EOC or SOT)
		if (p_j2k->m_specific_param.m_decoder.m_state != J2K_DEC_STATE_EOC) {

			// not enough data for another marker : fail decode
			if (p_stream->read( l_data, 2, p_manager) != 2) {
				event_msg(p_manager, EVT_ERROR, "Stream too short\n");
				return false;
			}

			// read marker
			grok_read_bytes(l_data, &l_current_marker, 2);

			// we found the EOC marker - set state accordingly and return true - can ignore all data after EOC
			if (l_current_marker == J2K_MS_EOC) {
				p_j2k->m_current_tile_number = 0;
				p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_EOC;
				return true;
			}

			// if we get here, we expect an SOT marker......
			if (l_current_marker != J2K_MS_SOT) {
				auto bytesLeft = p_stream->get_number_byte_left();
				// no bytes left - file ends without EOC marker
				if (bytesLeft == 0) {
					p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_NEOC;
					event_msg(p_manager, EVT_WARNING, "Stream does not end with EOC\n");
					return true;
				}
				event_msg(p_manager, EVT_WARNING, "Decode tile: expected EOC or SOT but found unknown \"marker\" %x. \n", l_current_marker);
				throw DecodeUnknownMarkerAtEndOfTileException();
			}
		}
	}
    return true;
}

/*
p_data stores the number of resolutions decoded, in the actual precision of the decoded image.

This method copies a sub-region of this region into p_output_image (which stores data in 32 bit precision)

*/
static bool j2k_copy_decoded_tile_to_output_image (tcd_t * p_tcd, 
										uint8_t * p_data, 
										opj_image_t* p_output_image,
										bool clearOutputOnInit,
										event_mgr_t * p_manager)
{
    uint32_t i=0,j=0,k = 0;
	opj_image_t * image_src = p_tcd->image;

    for (i=0; i<image_src->numcomps; i++) {

		tcd_tilecomp_t* tilec = p_tcd->tile->comps+i;
		opj_image_comp_t* img_comp_src = image_src->comps+i;
		opj_image_comp_t* img_comp_dest = p_output_image->comps+i;

		if (img_comp_dest->w * img_comp_dest->h == 0) {
			event_msg(p_manager, EVT_ERROR, "Output image has invalid dimensions %d x %d\n", img_comp_dest->w, img_comp_dest->h);
			return false;
		}

        /* Allocate output component buffer if necessary */
        if (!img_comp_dest->data) {
            if (!opj_image_single_component_data_alloc(img_comp_dest)) {
                return false;
            }
			if (clearOutputOnInit) {
				memset(img_comp_dest->data, 0, img_comp_dest->w * img_comp_dest->h * sizeof(int32_t));
			}
        }

        /* Copy info from decoded comp image to output image */
        img_comp_dest->resno_decoded = img_comp_src->resno_decoded;

        /*-----*/
        /* Compute the precision of the output buffer */
		uint32_t size_comp = (img_comp_src->prec + 7) >> 3;
		tcd_resolution_t* res = tilec->resolutions + img_comp_src->resno_decoded;

        if (size_comp == 3) {
            size_comp = 4;
        }
        /*-----*/

        /* Current tile component size*/
        /*if (i == 0) {
        fprintf(stdout, "SRC: res_x0=%d, res_x1=%d, res_y0=%d, res_y1=%d\n",
                        res->x0, res->x1, res->y0, res->y1);
        }*/

		uint32_t width_src = (res->x1 - res->x0);
		uint32_t height_src = (res->y1 - res->y0);


        /* Border of the current output component. (x0_dest,y0_dest) corresponds to origin of dest buffer */
		uint32_t x0_dest = uint_ceildivpow2(img_comp_dest->x0, img_comp_dest->decodeScaleFactor);
		uint32_t y0_dest = uint_ceildivpow2(img_comp_dest->y0, img_comp_dest->decodeScaleFactor);
		uint32_t x1_dest = x0_dest + img_comp_dest->w; /* can't overflow given that image->x1 is uint32 */
		uint32_t y1_dest = y0_dest + img_comp_dest->h;

        /*if (i == 0) {
        fprintf(stdout, "DEST: x0_dest=%d, x1_dest=%d, y0_dest=%d, y1_dest=%d (%d)\n",
                        x0_dest, x1_dest, y0_dest, y1_dest, img_comp_dest->decodeScaleFactor );
        }*/

        /*-----*/
        /* Compute the area (offset_x0_src, offset_y0_src, offset_x1_src, offset_y1_src)
         * of the input buffer (decoded tile component) which will be moved
         * to the output buffer. Compute the area of the output buffer (offset_x0_dest,
         * offset_y0_dest, width_dest, height_dest)  which will be modified
         * by this input area.
         * */
		uint32_t offset_x0_dest=0, offset_y0_dest=0;
		uint32_t offset_x0_src=0, offset_y0_src=0, offset_x1_src=0, offset_y1_src=0;
		uint32_t width_dest=0, height_dest=0;
        if ( x0_dest < res->x0 ) {
            offset_x0_dest = res->x0 - x0_dest;
            offset_x0_src = 0;

            if ( x1_dest >= res->x1 ) {
                width_dest = width_src;
                offset_x1_src = 0;
            } else {
                width_dest = x1_dest -res->x0 ;
                offset_x1_src = (width_src - width_dest);
            }
        } else {
            offset_x0_dest = 0U;
            offset_x0_src = x0_dest - res->x0;

            if ( x1_dest >= res->x1 ) {
                width_dest = width_src - offset_x0_src;
                offset_x1_src = 0;
            } else {
                width_dest = img_comp_dest->w ;
                offset_x1_src = res->x1 - x1_dest;
            }
        }

        if ( y0_dest < res->y0 ) {
            offset_y0_dest = res->y0 - y0_dest;
            offset_y0_src = 0;

            if ( y1_dest >= res->y1 ) {
                height_dest = height_src;
                offset_y1_src = 0;
            } else {
                height_dest = y1_dest - res->y0 ;
                offset_y1_src =  (height_src - height_dest);
            }
        } else {
            offset_y0_dest = 0U;
            offset_y0_src = y0_dest - res->y0;

            if ( y1_dest >= res->y1 ) {
                height_dest = height_src -offset_y0_src;
                offset_y1_src = 0;
            } else {
                height_dest = img_comp_dest->h ;
                offset_y1_src = res->y1 - y1_dest;
            }
        }


		if ((offset_x0_src >width_src) || (offset_y0_src >height_src) || (offset_x1_src > width_src) || (offset_y1_src > height_src)) {
			return false;
		}


		if (width_dest > img_comp_dest->w || height_dest > img_comp_dest->h)
			return false;

		if (width_src > img_comp_src->w || height_src > img_comp_src->h)
			return false;


        /*-----*/

        /* Compute the input buffer offset */
		size_t start_offset_src = (size_t)offset_x0_src + (size_t)offset_y0_src * (size_t)width_src;
		size_t line_offset_src  = (size_t)offset_x1_src + (size_t)offset_x0_src;
		size_t end_offset_src   = (size_t)offset_y1_src * (size_t)width_src - (size_t)offset_x0_src;

        /* Compute the output buffer offset */
		size_t start_offset_dest = (size_t)offset_x0_dest + (size_t)offset_y0_dest * (size_t)img_comp_dest->w;
		size_t line_offset_dest  = (size_t)img_comp_dest->w - (size_t)width_dest;

        /* Move the output buffer index to the first place where we will write*/
		auto dest_ind = start_offset_dest;

		/* Move to the first place where we will read*/
		auto src_ind = start_offset_src;

        /*if (i == 0) {
                fprintf(stdout, "COMPO[%d]:\n",i);
                fprintf(stdout, "SRC: start_x_src=%d, start_y_src=%d, width_src=%d, height_src=%d\n"
                                "\t tile offset:%d, %d, %d, %d\n"
                                "\t buffer offset: %d; %d, %d\n",
                                res->x0, res->y0, width_src, height_src,
                                offset_x0_src, offset_y0_src, offset_x1_src, offset_y1_src,
                                start_offset_src, line_offset_src, end_offset_src);

                fprintf(stdout, "DEST: offset_x0_dest=%d, offset_y0_dest=%d, width_dest=%d, height_dest=%d\n"
                                "\t start offset: %d, line offset= %d\n",
                                offset_x0_dest, offset_y0_dest, width_dest, height_dest, start_offset_dest, line_offset_dest);
        }*/

		switch (size_comp) {
		case 1: {
			char * src_ptr = (char*)p_data;
			if (img_comp_src->sgnd) {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] = (int32_t)src_ptr[src_ind++]; /* Copy only the data needed for the output image */
					}

					dest_ind += line_offset_dest; /* Move to the next place where we will write */
					src_ind += line_offset_src; /* Move to the next place where we will read */
				}
			}
			else {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] = (int32_t)(src_ptr[src_ind++] & 0xff);
					}

					dest_ind += line_offset_dest;
					src_ind += line_offset_src;
				}
			}

			src_ind += end_offset_src; /* Move to the end of this component-part of the input buffer */
			p_data = (uint8_t*)(src_ptr + src_ind); /* Keep the current position for the next component-part */
			}
			break;
		case 2: {
			int16_t * src_ptr = (int16_t *)p_data;

			if (img_comp_src->sgnd) {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] = src_ptr[src_ind++];
					}

					dest_ind += line_offset_dest;
					src_ind += line_offset_src;
				}
			}
			else {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] = src_ptr[src_ind++] & 0xffff;
					}

					dest_ind += line_offset_dest;
					src_ind += line_offset_src;
				}
			}

			src_ind += end_offset_src;
			p_data = (uint8_t*)(src_ptr + src_ind);
			}
			break;
		case 4: {
			int32_t * src_ptr = (int32_t *)p_data;

			for (j = 0; j < height_dest; ++j) {
				for (k = 0; k < width_dest; ++k) {
					img_comp_dest->data[dest_ind++] = src_ptr[src_ind++];
					//memcpy(img_comp_dest->data+dest_ind, src_ptr+src_ind, sizeof(int32_t)*width_dest);
				}

				dest_ind += line_offset_dest;
				src_ind += line_offset_src;
			}

			src_ind += end_offset_src;
			p_data = (uint8_t*)(src_ptr + src_ind);
			}
			break;
		}
    }

    return true;
}

bool j2k_set_decode_area(       j2k_t *p_j2k,
                                    opj_image_t* p_image,
                                    uint32_t p_start_x, uint32_t p_start_y,
                                    uint32_t p_end_x, uint32_t p_end_y,
                                    event_mgr_t * p_manager )
{
    cp_t * l_cp = &(p_j2k->m_cp);
    opj_image_t * l_image = p_j2k->m_private_image;

    uint32_t it_comp;
    uint32_t l_comp_x1, l_comp_y1;
    opj_image_comp_t* l_img_comp = nullptr;

    /* Check if we have read the main header */
    if (p_j2k->m_specific_param.m_decoder.m_state != J2K_DEC_STATE_TPHSOT) { /* FIXME J2K_DEC_STATE_TPHSOT)*/
        event_msg(p_manager, EVT_ERROR, "Need to decode the main header before setting decode area");
        return false;
    }

    if ( !p_start_x && !p_start_y && !p_end_x && !p_end_y) {
        //event_msg(p_manager, EVT_INFO, "No decoded area parameters, set the decoded area to the whole image\n");

        p_j2k->m_specific_param.m_decoder.m_start_tile_x = 0;
        p_j2k->m_specific_param.m_decoder.m_start_tile_y = 0;
        p_j2k->m_specific_param.m_decoder.m_end_tile_x = l_cp->tw;
        p_j2k->m_specific_param.m_decoder.m_end_tile_y = l_cp->th;

        return true;
    }

    /* ----- */
    /* Check if the positions provided by the user are correct */

    /* Left */
    if (p_start_x > l_image->x1 ) {
        event_msg(p_manager, EVT_ERROR,
                      "Left position of the decoded area (region_x0=%d) is outside the image area (Xsiz=%d).\n",
                      p_start_x, l_image->x1);
        return false;
    } else if (p_start_x < l_image->x0) {
        event_msg(p_manager, EVT_WARNING,
                      "Left position of the decoded area (region_x0=%d) is outside the image area (XOsiz=%d).\n",
                      p_start_x, l_image->x0);
        p_j2k->m_specific_param.m_decoder.m_start_tile_x = 0;
        p_image->x0 = l_image->x0;
    } else {
        p_j2k->m_specific_param.m_decoder.m_start_tile_x = (p_start_x - l_cp->tx0) / l_cp->tdx;
        p_image->x0 = p_start_x;
    }

    /* Up */
    if (p_start_y > l_image->y1) {
        event_msg(p_manager, EVT_ERROR,
                      "Up position of the decoded area (region_y0=%d) is outside the image area (Ysiz=%d).\n",
                      p_start_y, l_image->y1);
        return false;
    } else if (p_start_y < l_image->y0) {
        event_msg(p_manager, EVT_WARNING,
                      "Up position of the decoded area (region_y0=%d) is outside the image area (YOsiz=%d).\n",
                      p_start_y, l_image->y0);
        p_j2k->m_specific_param.m_decoder.m_start_tile_y = 0;
        p_image->y0 = l_image->y0;
    } else {
        p_j2k->m_specific_param.m_decoder.m_start_tile_y = (p_start_y - l_cp->ty0) / l_cp->tdy;
        p_image->y0 = p_start_y;
    }

    /* Right */
    assert(p_end_x > 0);
    assert(p_end_y > 0);
    if (p_end_x < l_image->x0) {
        event_msg(p_manager, EVT_ERROR,
                      "Right position of the decoded area (region_x1=%d) is outside the image area (XOsiz=%d).\n",
                      p_end_x, l_image->x0);
        return false;
    } else if (p_end_x > l_image->x1) {
        event_msg(p_manager, EVT_WARNING,
                      "Right position of the decoded area (region_x1=%d) is outside the image area (Xsiz=%d).\n",
                      p_end_x, l_image->x1);
        p_j2k->m_specific_param.m_decoder.m_end_tile_x = l_cp->tw;
        p_image->x1 = l_image->x1;
    } else {
		// avoid divide by zero
		if (l_cp->tdx == 0) {
			return false;
		}
        p_j2k->m_specific_param.m_decoder.m_end_tile_x = ceildiv<uint32_t>(p_end_x - l_cp->tx0, l_cp->tdx);
        p_image->x1 = p_end_x;
    }

    /* Bottom */
    if (p_end_y < l_image->y0) {
        event_msg(p_manager, EVT_ERROR,
                      "Bottom position of the decoded area (region_y1=%d) is outside the image area (YOsiz=%d).\n",
                      p_end_y, l_image->y0);
        return false;
    }
    if (p_end_y > l_image->y1) {
        event_msg(p_manager, EVT_WARNING,
                      "Bottom position of the decoded area (region_y1=%d) is outside the image area (Ysiz=%d).\n",
                      p_end_y, l_image->y1);
        p_j2k->m_specific_param.m_decoder.m_end_tile_y = l_cp->th;
        p_image->y1 = l_image->y1;
    } else {
		// avoid divide by zero
		if (l_cp->tdy == 0) { 
			return false;
		}
        p_j2k->m_specific_param.m_decoder.m_end_tile_y = ceildiv<uint32_t>(p_end_y - l_cp->ty0, l_cp->tdy);
        p_image->y1 = p_end_y;
    }
    /* ----- */

    p_j2k->m_specific_param.m_decoder.m_discard_tiles = 1;

    l_img_comp = p_image->comps;
    for (it_comp=0; it_comp < p_image->numcomps; ++it_comp) {
		// avoid divide by zero
		if (l_img_comp->dx == 0 || l_img_comp->dy == 0) {
			return false;
		}

        l_img_comp->x0 = ceildiv<uint32_t>(p_image->x0, l_img_comp->dx);
        l_img_comp->y0 = ceildiv<uint32_t>(p_image->y0, l_img_comp->dy);
        l_comp_x1 = ceildiv<uint32_t>(p_image->x1, l_img_comp->dx);
        l_comp_y1 = ceildiv<uint32_t>(p_image->y1, l_img_comp->dy);

		uint32_t l_x1 = uint_ceildivpow2(l_comp_x1, l_img_comp->decodeScaleFactor);
		uint32_t l_x0 = uint_ceildivpow2(l_img_comp->x0, l_img_comp->decodeScaleFactor);
        if (l_x1 < l_x0) {
            event_msg(p_manager, EVT_ERROR,
                          "Size x of the decoded component image is incorrect (comp[%d].w=%d).\n",
                          it_comp, (int32_t)l_x1 - l_x0);
            return false;
        }
		l_img_comp->w = l_x1 - l_x0;

		uint32_t l_y1 = uint_ceildivpow2(l_comp_y1, l_img_comp->decodeScaleFactor);
		uint32_t l_y0 = uint_ceildivpow2(l_img_comp->y0, l_img_comp->decodeScaleFactor);
        if (l_y1 < l_y0) {
            event_msg(p_manager, EVT_ERROR,
                          "Size y of the decoded component image is incorrect (comp[%d].h=%d).\n",
                          it_comp, (int32_t)l_y1-l_y0);
            return false;
        }
		l_img_comp->h = l_y1 - l_y0;

        l_img_comp++;
    }
    //event_msg( p_manager, EVT_INFO,"Setting decoding area to %d,%d,%d,%d\n",
    //               p_image->x0, p_image->y0, p_image->x1, p_image->y1);
    return true;
}

j2k_t* j2k_create_decompress(void)
{
    j2k_t *l_j2k = (j2k_t*) grok_calloc(1,sizeof(j2k_t));
    if (!l_j2k) {
        return nullptr;
    }

    l_j2k->m_is_decoder = 1;
    l_j2k->m_cp.m_is_decoder = 1;

#ifdef OPJ_DISABLE_TPSOT_FIX
    l_j2k->m_specific_param.m_decoder.m_nb_tile_parts_correction_checked = 1;
#endif

    l_j2k->m_specific_param.m_decoder.m_default_tcp = new tcp_t();
    if (!l_j2k->m_specific_param.m_decoder.m_default_tcp) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    l_j2k->m_specific_param.m_decoder.m_header_data = (uint8_t *) grok_calloc(1,default_header_size);
    if (! l_j2k->m_specific_param.m_decoder.m_header_data) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    l_j2k->m_specific_param.m_decoder.m_header_data_size = default_header_size;

    l_j2k->m_specific_param.m_decoder.m_tile_ind_to_dec = -1 ;

    l_j2k->m_specific_param.m_decoder.m_last_sot_read_pos = 0 ;

    /* codestream index creation */
    l_j2k->cstr_index = j2k_create_cstr_index();
    if (!l_j2k->cstr_index) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    /* validation list creation */
    l_j2k->m_validation_list = procedure_list_create();
    if (! l_j2k->m_validation_list) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    /* execution list creation */
    l_j2k->m_procedure_list = procedure_list_create();
    if (! l_j2k->m_procedure_list) {
        j2k_destroy(l_j2k);
        return nullptr;
    }

    return l_j2k;
}

static opj_codestream_index_t* j2k_create_cstr_index(void)
{
    opj_codestream_index_t* cstr_index = (opj_codestream_index_t*)
                                         grok_calloc(1,sizeof(opj_codestream_index_t));
    if (!cstr_index)
        return nullptr;

    cstr_index->maxmarknum = 100;
    cstr_index->marknum = 0;
    cstr_index->marker = (opj_marker_info_t*)
                         grok_calloc(cstr_index->maxmarknum, sizeof(opj_marker_info_t));
    if (!cstr_index-> marker) {
        grok_free(cstr_index);
        return nullptr;
    }

    cstr_index->tile_index = nullptr;

    return cstr_index;
}

static uint32_t j2k_get_SPCod_SPCoc_size (       j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no )
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;

    
    assert(p_j2k != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp = &l_tcp->tccps[p_comp_no];

    /* preconditions again */
    assert(p_tile_no < (l_cp->tw * l_cp->th));
    assert(p_comp_no < p_j2k->m_private_image->numcomps);

    if (l_tccp->csty & J2K_CCP_CSTY_PRT) {
        return 5 + l_tccp->numresolutions;
    } else {
        return 5;
    }
}

static bool j2k_compare_SPCod_SPCoc(j2k_t *p_j2k, uint32_t p_tile_no, uint32_t p_first_comp_no, uint32_t p_second_comp_no)
{
    uint32_t i;
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp0 = nullptr;
    tccp_t *l_tccp1 = nullptr;

    
    assert(p_j2k != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp0 = &l_tcp->tccps[p_first_comp_no];
    l_tccp1 = &l_tcp->tccps[p_second_comp_no];

    if (l_tccp0->numresolutions != l_tccp1->numresolutions) {
        return false;
    }
    if (l_tccp0->cblkw != l_tccp1->cblkw) {
        return false;
    }
    if (l_tccp0->cblkh != l_tccp1->cblkh) {
        return false;
    }
    if (l_tccp0->cblksty != l_tccp1->cblksty) {
        return false;
    }
    if (l_tccp0->qmfbid != l_tccp1->qmfbid) {
        return false;
    }
    if ((l_tccp0->csty & J2K_CCP_CSTY_PRT) != (l_tccp1->csty & J2K_CCP_CSTY_PRT)) {
        return false;
    }

    for (i = 0U; i < l_tccp0->numresolutions; ++i) {
        if (l_tccp0->prcw[i] != l_tccp1->prcw[i]) {
            return false;
        }
        if (l_tccp0->prch[i] != l_tccp1->prch[i]) {
            return false;
        }
    }
    return true;
}

static bool j2k_write_SPCod_SPCoc(     j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no,
		GrokStream *p_stream,
        event_mgr_t * p_manager )
{
    uint32_t i;
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp = &l_tcp->tccps[p_comp_no];

    /* preconditions again */
    assert(p_tile_no < (l_cp->tw * l_cp->th));
    assert(p_comp_no <(p_j2k->m_private_image->numcomps));


	/* SPcoc (D) */
	if (!p_stream->write_byte((uint8_t)(l_tccp->numresolutions - 1), p_manager)) {
		return false;
	}

	/* SPcoc (E) */
	if (!p_stream->write_byte((uint8_t)(l_tccp->cblkw - 2), p_manager)) {
		return false;
	}

	/* SPcoc (F) */
	if (!p_stream->write_byte((uint8_t)(l_tccp->cblkh - 2), p_manager)) {
		return false;
	}

	/* SPcoc (G) */
	if (!p_stream->write_byte((uint8_t)l_tccp->cblksty, p_manager)) {
		return false;
	}

	/* SPcoc (H) */
	if (!p_stream->write_byte((uint8_t)l_tccp->qmfbid, p_manager)) {
		return false;
	}

    if (l_tccp->csty & J2K_CCP_CSTY_PRT) {
        for (i = 0; i < l_tccp->numresolutions; ++i) {
			/* SPcoc (I_i) */
			if (!p_stream->write_byte((uint8_t)(l_tccp->prcw[i] + (l_tccp->prch[i] << 4)), p_manager)) {
				return false;
			}
        }
    }

    return true;
}

static bool j2k_read_SPCod_SPCoc(  j2k_t *p_j2k,
                                       uint32_t compno,
                                       uint8_t * p_header_data,
                                       uint32_t * p_header_size,
                                       event_mgr_t * p_manager)
{
    uint32_t i, l_tmp;
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;
    uint8_t * l_current_ptr = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_header_data != nullptr);

    l_cp = &(p_j2k->m_cp);
	l_tcp = j2k_get_tcp(p_j2k);

    /* precondition again */
    assert(compno < p_j2k->m_private_image->numcomps);

    l_tccp = &l_tcp->tccps[compno];
    l_current_ptr = p_header_data;

    /* make sure room is sufficient */
    if (*p_header_size < 5) {
        event_msg(p_manager, EVT_ERROR, "Error reading SPCod SPCoc element\n");
        return false;
    }
	/* SPcox (D) */
    grok_read_bytes(l_current_ptr, &l_tccp->numresolutions ,1);            
    ++l_tccp->numresolutions;                                                                               /* tccp->numresolutions = read() + 1 */
    if (l_tccp->numresolutions > OPJ_J2K_MAXRLVLS) {
        event_msg(p_manager, EVT_ERROR,
                      "Number of resolutions %d is greater than maximum allowed number %d\n",
                      l_tccp->numresolutions, OPJ_J2K_MAXRLVLS);
        return false;
    }
    ++l_current_ptr;

    /* If user wants to remove more resolutions than the codestream contains, return error */
    if (l_cp->m_specific_param.m_dec.m_reduce >= l_tccp->numresolutions) {
        event_msg(p_manager, EVT_ERROR, "Error decoding component %d.\nThe number of resolutions to remove is higher than the number "
                      "of resolutions of this component\nModify the cp_reduce parameter.\n\n", compno);
        p_j2k->m_specific_param.m_decoder.m_state |= 0x8000;/* FIXME J2K_DEC_STATE_ERR;*/
        return false;
    }
	/* SPcoc (E) */
    grok_read_bytes(l_current_ptr,&l_tccp->cblkw ,1);              
    ++l_current_ptr;
    l_tccp->cblkw += 2;
	/* SPcoc (F) */
    grok_read_bytes(l_current_ptr,&l_tccp->cblkh ,1);              
    ++l_current_ptr;
    l_tccp->cblkh += 2;

    if ((l_tccp->cblkw > 10) || (l_tccp->cblkh > 10) || ((l_tccp->cblkw + l_tccp->cblkh) > 12)) {
        event_msg(p_manager, EVT_ERROR, "Error reading SPCod SPCoc element, Invalid cblkw/cblkh combination\n");
        return false;
    }

	/* SPcoc (G) */
    grok_read_bytes(l_current_ptr,&l_tccp->cblksty ,1);             
    ++l_current_ptr;
	if (l_tccp->cblksty & 0xC0U) { /* 2 msb are reserved, assume we can't read */
		event_msg(p_manager, EVT_ERROR, "Error reading SPCod SPCoc element, Invalid code-block style found\n");
		return false;
	}
	/* SPcoc (H) */
    grok_read_bytes(l_current_ptr,&l_tccp->qmfbid ,1);              
    ++l_current_ptr;

    *p_header_size = *p_header_size - 5;

    /* use custom precinct size ? */
    if (l_tccp->csty & J2K_CCP_CSTY_PRT) {
        if (*p_header_size < l_tccp->numresolutions) {
            event_msg(p_manager, EVT_ERROR, "Error reading SPCod SPCoc element\n");
            return false;
        }

        for (i = 0; i < l_tccp->numresolutions; ++i) {
			/* SPcoc (I_i) */
            grok_read_bytes(l_current_ptr,&l_tmp ,1);               
            ++l_current_ptr;
            /* Precinct exponent 0 is only allowed for lowest resolution level (Table A.21) */
            if ((i != 0) && (((l_tmp & 0xf) == 0) || ((l_tmp >> 4) == 0))) {
                event_msg(p_manager, EVT_ERROR, "Invalid precinct size\n");
                return false;
            }
            l_tccp->prcw[i] = l_tmp & 0xf;
            l_tccp->prch[i] = l_tmp >> 4;
        }

        *p_header_size = *p_header_size - l_tccp->numresolutions;
    } else {
        /* set default size for the precinct width and height */
        for     (i = 0; i < l_tccp->numresolutions; ++i) {
            l_tccp->prcw[i] = 15;
            l_tccp->prch[i] = 15;
        }
    }

    return true;
}

static void j2k_copy_tile_component_parameters( j2k_t *p_j2k )
{
    /* loop */
    uint32_t i;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_ref_tccp = nullptr, *l_copied_tccp = nullptr;
    uint32_t l_prc_size;

    
    assert(p_j2k != nullptr);

	l_tcp = j2k_get_tcp(p_j2k);

    l_ref_tccp = &l_tcp->tccps[0];
    l_copied_tccp = l_ref_tccp + 1;
    l_prc_size = l_ref_tccp->numresolutions * (uint32_t)sizeof(uint32_t);

    for     (i=1; i<p_j2k->m_private_image->numcomps; ++i) {
        l_copied_tccp->numresolutions = l_ref_tccp->numresolutions;
        l_copied_tccp->cblkw = l_ref_tccp->cblkw;
        l_copied_tccp->cblkh = l_ref_tccp->cblkh;
        l_copied_tccp->cblksty = l_ref_tccp->cblksty;
        l_copied_tccp->qmfbid = l_ref_tccp->qmfbid;
        memcpy(l_copied_tccp->prcw,l_ref_tccp->prcw,l_prc_size);
        memcpy(l_copied_tccp->prch,l_ref_tccp->prch,l_prc_size);
        ++l_copied_tccp;
    }
}

static uint32_t j2k_get_SQcd_SQcc_size ( j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no )
{
    uint32_t l_num_bands;

    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;

    
    assert(p_j2k != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp = &l_tcp->tccps[p_comp_no];

    /* preconditions again */
    assert(p_tile_no < l_cp->tw * l_cp->th);
    assert(p_comp_no < p_j2k->m_private_image->numcomps);

    l_num_bands = (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (l_tccp->numresolutions * 3 - 2);

    if (l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)  {
        return 1 + l_num_bands;
    } else {
        return 1 + 2*l_num_bands;
    }
}

static bool j2k_compare_SQcd_SQcc(j2k_t *p_j2k, uint32_t p_tile_no, uint32_t p_first_comp_no, uint32_t p_second_comp_no)
{
    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp0 = nullptr;
    tccp_t *l_tccp1 = nullptr;
    uint32_t l_band_no, l_num_bands;

    
    assert(p_j2k != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp0 = &l_tcp->tccps[p_first_comp_no];
    l_tccp1 = &l_tcp->tccps[p_second_comp_no];

    if (l_tccp0->qntsty != l_tccp1->qntsty ) {
        return false;
    }
    if (l_tccp0->numgbits != l_tccp1->numgbits ) {
        return false;
    }
    if (l_tccp0->qntsty == J2K_CCP_QNTSTY_SIQNT) {
        l_num_bands = 1U;
    } else {
        l_num_bands = l_tccp0->numresolutions * 3U - 2U;
        if (l_num_bands != (l_tccp1->numresolutions * 3U - 2U)) {
            return false;
        }
    }

    for (l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
        if (l_tccp0->stepsizes[l_band_no].expn != l_tccp1->stepsizes[l_band_no].expn ) {
            return false;
        }
    }
    if (l_tccp0->qntsty != J2K_CCP_QNTSTY_NOQNT) {
        for (l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
            if (l_tccp0->stepsizes[l_band_no].mant != l_tccp1->stepsizes[l_band_no].mant ) {
                return false;
            }
        }
    }
    return true;
}


static bool j2k_write_SQcd_SQcc(       j2k_t *p_j2k,
        uint32_t p_tile_no,
        uint32_t p_comp_no,
		GrokStream *p_stream,
        event_mgr_t * p_manager )
{
    uint32_t l_band_no, l_num_bands;
    uint32_t l_expn,l_mant;

    cp_t *l_cp = nullptr;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    l_cp = &(p_j2k->m_cp);
    l_tcp = &l_cp->tcps[p_tile_no];
    l_tccp = &l_tcp->tccps[p_comp_no];

    /* preconditions again */
    assert(p_tile_no < l_cp->tw * l_cp->th);
    assert(p_comp_no <p_j2k->m_private_image->numcomps);

    l_num_bands = (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (l_tccp->numresolutions * 3 - 2);

    if (l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)  {

		/* Sqcx */
		if (!p_stream->write_byte((uint8_t)(l_tccp->qntsty + (l_tccp->numgbits << 5)), p_manager)) {
			return false;
		}

        for (l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
            l_expn = l_tccp->stepsizes[l_band_no].expn;
			/* SPqcx_i */
			if (!p_stream->write_byte((uint8_t)(l_expn << 3), p_manager)) {
				return false;
			}
        }
    } else {

		/* Sqcx */
		if (!p_stream->write_byte((uint8_t)(l_tccp->qntsty + (l_tccp->numgbits << 5)), p_manager)) {
			return false;
		}

        for (l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
            l_expn = l_tccp->stepsizes[l_band_no].expn;
            l_mant = l_tccp->stepsizes[l_band_no].mant;

			/* SPqcx_i */
			if (!p_stream->write_short((uint16_t)((l_expn << 11) + l_mant), p_manager)) {
				return false;
			}
        }
    }
    return true;
}

static bool j2k_read_SQcd_SQcc(bool isQCD,
									j2k_t *p_j2k,
                                   uint32_t p_comp_no,
                                   uint8_t* p_header_data,
                                   uint32_t * p_header_size,
                                   event_mgr_t * p_manager
                                  )
{
    /* loop*/
    uint32_t l_band_no;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_tccp = nullptr;
    uint8_t * l_current_ptr = nullptr;
    uint32_t l_tmp;

    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_header_data != nullptr);

	l_tcp = j2k_get_tcp(p_j2k);

    /* precondition again*/
    assert(p_comp_no <  p_j2k->m_private_image->numcomps);

    l_tccp = &l_tcp->tccps[p_comp_no];
    l_current_ptr = p_header_data;

    if (*p_header_size < 1) {
        event_msg(p_manager, EVT_ERROR, "Error reading SQcd or SQcc element\n");
        return false;
    }

	if (!isQCD)
		l_tccp->hasQCC = true;

    *p_header_size -= 1;
	/* Sqcx */
    grok_read_bytes(l_current_ptr, &l_tmp ,1);                     
    ++l_current_ptr;

    l_tccp->qntsty = l_tmp & 0x1f;
	if (isQCD)
		l_tcp->qntsty = l_tccp->qntsty;
    l_tccp->numgbits = l_tmp >> 5;
    if (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) {
		l_tccp->numStepSizes = 1;
    } else {
		l_tccp->numStepSizes = (l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) ?
                     (*p_header_size) :
                     (*p_header_size) / 2;

		if (!isQCD) {
			uint32_t maxDecompositions = 0;
			if (l_tccp->numresolutions > 0)
				maxDecompositions = l_tccp->numresolutions - 1;
			// see page 553 of Taubman and Marcellin for more details on this check
			if ((l_tccp->numStepSizes < 3 * maxDecompositions + 1)) {
				event_msg(p_manager, EVT_ERROR, "While reading QCC marker, "
					"number of step sizes (%d) is less than 3* (max decompositions) + 1, where max decompositions = %d \n", l_tccp->numStepSizes, maxDecompositions);
				return false;
			}
		}

        if(l_tccp->numStepSizes > OPJ_J2K_MAXBANDS ) {
            event_msg(p_manager, EVT_WARNING, "While reading QCD or QCC marker segment, "
                          "number of step sizes (%d) is greater than OPJ_J2K_MAXBANDS (%d). So, we limit the number of elements stored to "
                          "OPJ_J2K_MAXBANDS (%d) and skip the rest.\n", l_tccp->numStepSizes, OPJ_J2K_MAXBANDS, OPJ_J2K_MAXBANDS);
        }
    }

	if (isQCD)
		l_tcp->numStepSizes = l_tccp->numStepSizes;

    if (l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) {
        for     (l_band_no = 0; l_band_no < l_tccp->numStepSizes; l_band_no++) {
			/* SPqcx_i */
            grok_read_bytes(l_current_ptr, &l_tmp ,1);                       
            ++l_current_ptr;
            if (l_band_no < OPJ_J2K_MAXBANDS) {
                l_tccp->stepsizes[l_band_no].expn = l_tmp >> 3;
                l_tccp->stepsizes[l_band_no].mant = 0;
            }
        }
        *p_header_size = *p_header_size - l_tccp->numStepSizes;
    } else {
        for     (l_band_no = 0; l_band_no < l_tccp->numStepSizes; l_band_no++) {
			/* SPqcx_i */
            grok_read_bytes(l_current_ptr, &l_tmp ,2);                       
            l_current_ptr+=2;
            if (l_band_no < OPJ_J2K_MAXBANDS) {
                l_tccp->stepsizes[l_band_no].expn = l_tmp >> 11;
                l_tccp->stepsizes[l_band_no].mant = l_tmp & 0x7ff;
            }
        }
        *p_header_size = *p_header_size - 2* l_tccp->numStepSizes;
    }

    /* if scalar derived, then compute other stepsizes */
    if (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) {
        for (l_band_no = 1; l_band_no < OPJ_J2K_MAXBANDS; l_band_no++) {
			uint32_t bandDividedBy3 = (l_band_no - 1) / 3;
			l_tccp->stepsizes[l_band_no].expn = 0;
			if (l_tccp->stepsizes[0].expn > bandDividedBy3)
				l_tccp->stepsizes[l_band_no].expn = l_tccp->stepsizes[0].expn - bandDividedBy3;
            l_tccp->stepsizes[l_band_no].mant = l_tccp->stepsizes[0].mant;
        }
    }

    return true;
}

static void j2k_copy_tile_quantization_parameters( j2k_t *p_j2k )
{
    uint32_t i;
    tcp_t *l_tcp = nullptr;
    tccp_t *l_ref_tccp = nullptr;
    tccp_t *l_copied_tccp = nullptr;
  
    assert(p_j2k != nullptr);
	l_tcp = j2k_get_tcp(p_j2k);

    l_ref_tccp = &l_tcp->tccps[0];
    l_copied_tccp = l_ref_tccp + 1;
    auto l_size = OPJ_J2K_MAXBANDS * sizeof(stepsize_t);

    for     (i=1; i<p_j2k->m_private_image->numcomps; ++i) {
        l_copied_tccp->qntsty = l_ref_tccp->qntsty;
        l_copied_tccp->numgbits = l_ref_tccp->numgbits;
        memcpy(l_copied_tccp->stepsizes,l_ref_tccp->stepsizes,l_size);
        ++l_copied_tccp;
    }
}

static void j2k_dump_tile_info( tcp_t * l_default_tile,uint32_t numcomps,FILE* out_stream)
{
    if (l_default_tile) {
        uint32_t compno;

        fprintf(out_stream, "\t default tile {\n");
        fprintf(out_stream, "\t\t csty=%#x\n", l_default_tile->csty);
        fprintf(out_stream, "\t\t prg=%#x\n", l_default_tile->prg);
        fprintf(out_stream, "\t\t numlayers=%d\n", l_default_tile->numlayers);
        fprintf(out_stream, "\t\t mct=%x\n", l_default_tile->mct);

        for (compno = 0; compno < numcomps; compno++) {
            tccp_t *l_tccp = &(l_default_tile->tccps[compno]);
            uint32_t resno;
            uint32_t bandno, numbands;

            /* coding style*/
            fprintf(out_stream, "\t\t comp %d {\n", compno);
            fprintf(out_stream, "\t\t\t csty=%#x\n", l_tccp->csty);
            fprintf(out_stream, "\t\t\t numresolutions=%d\n", l_tccp->numresolutions);
            fprintf(out_stream, "\t\t\t cblkw=2^%d\n", l_tccp->cblkw);
            fprintf(out_stream, "\t\t\t cblkh=2^%d\n", l_tccp->cblkh);
            fprintf(out_stream, "\t\t\t cblksty=%#x\n", l_tccp->cblksty);
            fprintf(out_stream, "\t\t\t qmfbid=%d\n", l_tccp->qmfbid);

            fprintf(out_stream, "\t\t\t preccintsize (w,h)=");
            for (resno = 0; resno < l_tccp->numresolutions; resno++) {
                fprintf(out_stream, "(%d,%d) ", l_tccp->prcw[resno], l_tccp->prch[resno]);
            }
            fprintf(out_stream, "\n");

            /* quantization style*/
            fprintf(out_stream, "\t\t\t qntsty=%d\n", l_tccp->qntsty);
            fprintf(out_stream, "\t\t\t numgbits=%d\n", l_tccp->numgbits);
            fprintf(out_stream, "\t\t\t stepsizes (m,e)=");
            numbands = (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (int32_t)l_tccp->numresolutions * 3 - 2;
            for (bandno = 0; bandno < numbands; bandno++) {
                fprintf(out_stream, "(%d,%d) ", l_tccp->stepsizes[bandno].mant,
                        l_tccp->stepsizes[bandno].expn);
            }
            fprintf(out_stream, "\n");

            /* RGN value*/
            fprintf(out_stream, "\t\t\t roishift=%d\n", l_tccp->roishift);

            fprintf(out_stream, "\t\t }\n");
        } /*end of component of default tile*/
        fprintf(out_stream, "\t }\n"); /*end of default tile*/
    }
}

void j2k_dump (j2k_t* p_j2k, int32_t flag, FILE* out_stream)
{
    /* Check if the flag is compatible with j2k file*/
    if ( (flag & OPJ_JP2_INFO) || (flag & OPJ_JP2_IND)) {
        fprintf(out_stream, "Wrong flag\n");
        return;
    }

    /* Dump the image_header */
    if (flag & OPJ_IMG_INFO) {
        if (p_j2k->m_private_image)
            j2k_dump_image_header(p_j2k->m_private_image, 0, out_stream);
    }

    /* Dump the codestream info from main header */
    if (flag & OPJ_J2K_MH_INFO) {
		if (p_j2k->m_private_image)
			j2k_dump_MH_info(p_j2k, out_stream);
    }
    /* Dump all tile/codestream info */
    if (flag & OPJ_J2K_TCH_INFO) {
        uint32_t l_nb_tiles = p_j2k->m_cp.th * p_j2k->m_cp.tw;
        uint32_t i;
        tcp_t * l_tcp = p_j2k->m_cp.tcps;
		if (p_j2k->m_private_image) {
			for (i = 0; i < l_nb_tiles; ++i) {
				j2k_dump_tile_info(l_tcp, p_j2k->m_private_image->numcomps, out_stream);
				++l_tcp;
			}
		}
    }

    /* Dump the codestream info of the current tile */
    if (flag & OPJ_J2K_TH_INFO) {

    }

    /* Dump the codestream index from main header */
    if (flag & OPJ_J2K_MH_IND) {
        j2k_dump_MH_index(p_j2k, out_stream);
    }

    /* Dump the codestream index of the current tile */
    if (flag & OPJ_J2K_TH_IND) {

    }

}

static void j2k_dump_MH_index(j2k_t* p_j2k, FILE* out_stream)
{
    opj_codestream_index_t* cstr_index = p_j2k->cstr_index;
    uint32_t it_marker, it_tile, it_tile_part;

    fprintf(out_stream, "Codestream index from main header: {\n");

	std::stringstream ss;
	ss << "\t Main header start position="
		<<	cstr_index->main_head_start
		<<  std::endl 
		<<  "\t Main header end position="
		<< cstr_index->main_head_end
		<< 	std::endl;

    fprintf(out_stream,"%s",ss.str().c_str());
    fprintf(out_stream, "\t Marker list: {\n");

    if (cstr_index->marker) {
        for (it_marker=0; it_marker < cstr_index->marknum ; it_marker++) {

#if (defined(__GNUC__) &&  (__GNUC__ < 5 ))
			fprintf(out_stream, "\t\t type=%#x, pos=%lld, len=%d\n",
				cstr_index->marker[it_marker].type,
				(int64_t)cstr_index->marker[it_marker].pos,
				cstr_index->marker[it_marker].len);

#else
			fprintf(out_stream, "\t\t type=%#x, pos=%" PRIi64", len=%d\n",
				cstr_index->marker[it_marker].type,
				cstr_index->marker[it_marker].pos,
				cstr_index->marker[it_marker].len);

#endif
        }
    }

    fprintf(out_stream, "\t }\n");

    if (cstr_index->tile_index) {

        /* Simple test to avoid to write empty information*/
        uint32_t l_acc_nb_of_tile_part = 0;
        for (it_tile=0; it_tile < cstr_index->nb_of_tiles ; it_tile++) {
            l_acc_nb_of_tile_part += cstr_index->tile_index[it_tile].nb_tps;
        }

        if (l_acc_nb_of_tile_part) {
            fprintf(out_stream, "\t Tile index: {\n");

            for (it_tile=0; it_tile < cstr_index->nb_of_tiles ; it_tile++) {
                uint32_t nb_of_tile_part = cstr_index->tile_index[it_tile].nb_tps;

                fprintf(out_stream, "\t\t nb of tile-part in tile [%d]=%d\n", it_tile, nb_of_tile_part);

                if (cstr_index->tile_index[it_tile].tp_index) {
                    for (it_tile_part =0; it_tile_part < nb_of_tile_part; it_tile_part++) {
						ss.clear();
						ss << "\t\t\t tile-part[" << it_tile_part << "]:"
							<< " star_pos=" << cstr_index->tile_index[it_tile].tp_index[it_tile_part].start_pos << ","
							<< " end_header=" << cstr_index->tile_index[it_tile].tp_index[it_tile_part].end_header << ","
							<< " end_pos=" << cstr_index->tile_index[it_tile].tp_index[it_tile_part].end_pos << std::endl;
                        fprintf(out_stream, "%s",ss.str().c_str());
                    }
                }

                if (cstr_index->tile_index[it_tile].marker) {
                    for (it_marker=0; it_marker < cstr_index->tile_index[it_tile].marknum ; it_marker++) {
						ss.clear();
						ss << "\t\t type=" << cstr_index->tile_index[it_tile].marker[it_marker].type << ","
							<< " pos=" << cstr_index->tile_index[it_tile].marker[it_marker].pos << ","
							<< " len=" << cstr_index->tile_index[it_tile].marker[it_marker].len << std::endl;
                        fprintf(out_stream, "%s",ss.str().c_str());
                    }
                }
            }
            fprintf(out_stream,"\t }\n");
        }
    }
    fprintf(out_stream,"}\n");
}


static void j2k_dump_MH_info(j2k_t* p_j2k, FILE* out_stream)
{

    fprintf(out_stream, "Codestream info from main header: {\n");

    fprintf(out_stream, "\t tx0=%d, ty0=%d\n", p_j2k->m_cp.tx0, p_j2k->m_cp.ty0);
    fprintf(out_stream, "\t tdx=%d, tdy=%d\n", p_j2k->m_cp.tdx, p_j2k->m_cp.tdy);
    fprintf(out_stream, "\t tw=%d, th=%d\n", p_j2k->m_cp.tw, p_j2k->m_cp.th);
    j2k_dump_tile_info(p_j2k->m_specific_param.m_decoder.m_default_tcp,p_j2k->m_private_image->numcomps, out_stream);
    fprintf(out_stream, "}\n");
}

void j2k_dump_image_header(opj_image_t* img_header, bool dev_dump_flag, FILE* out_stream)
{
    char tab[2];

    if (dev_dump_flag) {
        fprintf(stdout, "[DEV] Dump an image_header struct {\n");
        tab[0] = '\0';
    } else {
        fprintf(out_stream, "Image info {\n");
        tab[0] = '\t';
        tab[1] = '\0';
    }

    fprintf(out_stream, "%s x0=%d, y0=%d\n", tab, img_header->x0, img_header->y0);
    fprintf(out_stream,     "%s x1=%d, y1=%d\n", tab, img_header->x1, img_header->y1);
    fprintf(out_stream, "%s numcomps=%d\n", tab, img_header->numcomps);

    if (img_header->comps) {
        uint32_t compno;
        for (compno = 0; compno < img_header->numcomps; compno++) {
            fprintf(out_stream, "%s\t component %d {\n", tab, compno);
            j2k_dump_image_comp_header(&(img_header->comps[compno]), dev_dump_flag, out_stream);
            fprintf(out_stream,"%s}\n",tab);
        }
    }

    fprintf(out_stream, "}\n");
}

void j2k_dump_image_comp_header(opj_image_comp_t* comp_header, bool dev_dump_flag, FILE* out_stream)
{
    char tab[3];

    if (dev_dump_flag) {
        fprintf(stdout, "[DEV] Dump an image_comp_header struct {\n");
        tab[0] = '\0';
    }       else {
        tab[0] = '\t';
        tab[1] = '\t';
        tab[2] = '\0';
    }

    fprintf(out_stream, "%s dx=%d, dy=%d\n", tab, comp_header->dx, comp_header->dy);
    fprintf(out_stream, "%s prec=%d\n", tab, comp_header->prec);
    fprintf(out_stream, "%s sgnd=%d\n", tab, comp_header->sgnd);

    if (dev_dump_flag)
        fprintf(out_stream, "}\n");
}

opj_codestream_info_v2_t* j2k_get_cstr_info(j2k_t* p_j2k)
{
    uint32_t compno;
    uint32_t numcomps = p_j2k->m_private_image->numcomps;
    tcp_t *l_default_tile;
    opj_codestream_info_v2_t* cstr_info = (opj_codestream_info_v2_t*) grok_calloc(1,sizeof(opj_codestream_info_v2_t));
    if (!cstr_info)
        return nullptr;

    cstr_info->nbcomps = p_j2k->m_private_image->numcomps;

    cstr_info->tx0 = p_j2k->m_cp.tx0;
    cstr_info->ty0 = p_j2k->m_cp.ty0;
    cstr_info->tdx = p_j2k->m_cp.tdx;
    cstr_info->tdy = p_j2k->m_cp.tdy;
    cstr_info->tw = p_j2k->m_cp.tw;
    cstr_info->th = p_j2k->m_cp.th;

    cstr_info->tile_info = nullptr; /* Not fill from the main header*/

    l_default_tile = p_j2k->m_specific_param.m_decoder.m_default_tcp;

    cstr_info->m_default_tile_info.csty = l_default_tile->csty;
    cstr_info->m_default_tile_info.prg = l_default_tile->prg;
    cstr_info->m_default_tile_info.numlayers = l_default_tile->numlayers;
    cstr_info->m_default_tile_info.mct = l_default_tile->mct;

    cstr_info->m_default_tile_info.tccp_info = (opj_tccp_info_t*) grok_calloc(cstr_info->nbcomps, sizeof(opj_tccp_info_t));
    if (!cstr_info->m_default_tile_info.tccp_info) {
        opj_destroy_cstr_info(&cstr_info);
        return nullptr;
    }

    for (compno = 0; compno < numcomps; compno++) {
        tccp_t *l_tccp = &(l_default_tile->tccps[compno]);
        opj_tccp_info_t *l_tccp_info = &(cstr_info->m_default_tile_info.tccp_info[compno]);
        uint32_t bandno, numbands;

        /* coding style*/
        l_tccp_info->csty = l_tccp->csty;
        l_tccp_info->numresolutions = l_tccp->numresolutions;
        l_tccp_info->cblkw = l_tccp->cblkw;
        l_tccp_info->cblkh = l_tccp->cblkh;
        l_tccp_info->cblksty = l_tccp->cblksty;
        l_tccp_info->qmfbid = l_tccp->qmfbid;
        if (l_tccp->numresolutions < OPJ_J2K_MAXRLVLS) {
            memcpy(l_tccp_info->prch, l_tccp->prch, l_tccp->numresolutions);
            memcpy(l_tccp_info->prcw, l_tccp->prcw, l_tccp->numresolutions);
        }

        /* quantization style*/
        l_tccp_info->qntsty = l_tccp->qntsty;
        l_tccp_info->numgbits = l_tccp->numgbits;

        numbands = (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (l_tccp->numresolutions * 3 - 2);
        if (numbands < OPJ_J2K_MAXBANDS) {
            for (bandno = 0; bandno < numbands; bandno++) {
                l_tccp_info->stepsizes_mant[bandno] = l_tccp->stepsizes[bandno].mant;
                l_tccp_info->stepsizes_expn[bandno] = l_tccp->stepsizes[bandno].expn;
            }
        }

        /* RGN value*/
        l_tccp_info->roishift = l_tccp->roishift;
    }

    return cstr_info;
}

opj_codestream_index_t* j2k_get_cstr_index(j2k_t* p_j2k)
{
    opj_codestream_index_t* l_cstr_index = (opj_codestream_index_t*)
                                           grok_calloc(1,sizeof(opj_codestream_index_t));
    if (!l_cstr_index)
        return nullptr;

    l_cstr_index->main_head_start = p_j2k->cstr_index->main_head_start;
    l_cstr_index->main_head_end = p_j2k->cstr_index->main_head_end;
    l_cstr_index->codestream_size = p_j2k->cstr_index->codestream_size;

    l_cstr_index->marknum = p_j2k->cstr_index->marknum;
    l_cstr_index->marker = (opj_marker_info_t*)grok_malloc(l_cstr_index->marknum*sizeof(opj_marker_info_t));
    if (!l_cstr_index->marker) {
        grok_free( l_cstr_index);
        return nullptr;
    }

    if (p_j2k->cstr_index->marker)
        memcpy(l_cstr_index->marker, p_j2k->cstr_index->marker, l_cstr_index->marknum * sizeof(opj_marker_info_t) );
    else {
        grok_free(l_cstr_index->marker);
        l_cstr_index->marker = nullptr;
    }

    l_cstr_index->nb_of_tiles = p_j2k->cstr_index->nb_of_tiles;
    l_cstr_index->tile_index = (opj_tile_index_t*)grok_calloc(l_cstr_index->nb_of_tiles, sizeof(opj_tile_index_t) );
    if (!l_cstr_index->tile_index) {
        grok_free( l_cstr_index->marker);
        grok_free( l_cstr_index);
        return nullptr;
    }

    if (!p_j2k->cstr_index->tile_index) {
        grok_free(l_cstr_index->tile_index);
        l_cstr_index->tile_index = nullptr;
    } else {
        uint32_t it_tile = 0;
        for (it_tile = 0; it_tile < l_cstr_index->nb_of_tiles; it_tile++ ) {

            /* Tile Marker*/
            l_cstr_index->tile_index[it_tile].marknum = p_j2k->cstr_index->tile_index[it_tile].marknum;

            l_cstr_index->tile_index[it_tile].marker =
                (opj_marker_info_t*)grok_malloc(l_cstr_index->tile_index[it_tile].marknum*sizeof(opj_marker_info_t));

            if (!l_cstr_index->tile_index[it_tile].marker) {
                uint32_t it_tile_free;

                for (it_tile_free=0; it_tile_free < it_tile; it_tile_free++) {
                    grok_free(l_cstr_index->tile_index[it_tile_free].marker);
                }

                grok_free( l_cstr_index->tile_index);
                grok_free( l_cstr_index->marker);
                grok_free( l_cstr_index);
                return nullptr;
            }

            if (p_j2k->cstr_index->tile_index[it_tile].marker)
                memcpy( l_cstr_index->tile_index[it_tile].marker,
                        p_j2k->cstr_index->tile_index[it_tile].marker,
                        l_cstr_index->tile_index[it_tile].marknum * sizeof(opj_marker_info_t) );
            else {
                grok_free(l_cstr_index->tile_index[it_tile].marker);
                l_cstr_index->tile_index[it_tile].marker = nullptr;
            }

            /* Tile part index*/
            l_cstr_index->tile_index[it_tile].nb_tps = p_j2k->cstr_index->tile_index[it_tile].nb_tps;

            l_cstr_index->tile_index[it_tile].tp_index =
                (opj_tp_index_t*)grok_malloc(l_cstr_index->tile_index[it_tile].nb_tps*sizeof(opj_tp_index_t));

            if(!l_cstr_index->tile_index[it_tile].tp_index) {
                uint32_t it_tile_free;

                for (it_tile_free=0; it_tile_free < it_tile; it_tile_free++) {
                    grok_free(l_cstr_index->tile_index[it_tile_free].marker);
                    grok_free(l_cstr_index->tile_index[it_tile_free].tp_index);
                }

                grok_free( l_cstr_index->tile_index);
                grok_free( l_cstr_index->marker);
                grok_free( l_cstr_index);
                return nullptr;
            }

            if (p_j2k->cstr_index->tile_index[it_tile].tp_index) {
                memcpy( l_cstr_index->tile_index[it_tile].tp_index,
                        p_j2k->cstr_index->tile_index[it_tile].tp_index,
                        l_cstr_index->tile_index[it_tile].nb_tps * sizeof(opj_tp_index_t) );
            } else {
                grok_free(l_cstr_index->tile_index[it_tile].tp_index);
                l_cstr_index->tile_index[it_tile].tp_index = nullptr;
            }

            /* Packet index (NOT USED)*/
            l_cstr_index->tile_index[it_tile].nb_packet = 0;
            l_cstr_index->tile_index[it_tile].packet_index = nullptr;

        }
    }
    return l_cstr_index;
}

static bool j2k_allocate_tile_element_cstr_index(j2k_t *p_j2k)
{
    uint32_t it_tile=0;

    p_j2k->cstr_index->nb_of_tiles = p_j2k->m_cp.tw * p_j2k->m_cp.th;
    p_j2k->cstr_index->tile_index = (opj_tile_index_t*)grok_calloc(p_j2k->cstr_index->nb_of_tiles, sizeof(opj_tile_index_t));
    if (!p_j2k->cstr_index->tile_index)
        return false;

    for (it_tile=0; it_tile < p_j2k->cstr_index->nb_of_tiles; it_tile++) {
        p_j2k->cstr_index->tile_index[it_tile].maxmarknum = 100;
        p_j2k->cstr_index->tile_index[it_tile].marknum = 0;
        p_j2k->cstr_index->tile_index[it_tile].marker = (opj_marker_info_t*)
                grok_calloc(p_j2k->cstr_index->tile_index[it_tile].maxmarknum, sizeof(opj_marker_info_t));
        if (!p_j2k->cstr_index->tile_index[it_tile].marker)
            return false;
    }
    return true;
}

static bool j2k_needs_copy_tile_data(j2k_t *p_j2k, uint32_t num_tiles)
{

	/* If we only have one tile, check the following:

	1) Check if each output image component's dimensions match
	destination image component dimensions. It they don't, then set copy_tile_data to true
	and break.

	2) Check if we are not decoding all resolutions. If we are not, set copy_tile_data to true
	and break;

	*/

	if (p_j2k->m_cp.m_specific_param.m_dec.m_reduce != 0)
		return true;
    /* single tile, RGB images only*/
    bool copy_tile_data = (num_tiles> 1) ;
    uint32_t i = 0;

    if (!copy_tile_data) {

        for (i = 0; i < p_j2k->m_output_image->numcomps; i++) {
            opj_image_comp_t* dest_comp = p_j2k->m_output_image->comps + i;
            uint32_t l_x0_dest = uint_ceildivpow2(dest_comp->x0, dest_comp->decodeScaleFactor);
            uint32_t l_y0_dest = uint_ceildivpow2(dest_comp->y0, dest_comp->decodeScaleFactor);
            uint32_t l_x1_dest = l_x0_dest + dest_comp->w; /* can't overflow given that image->x1 is uint32 */
            uint32_t l_y1_dest = l_y0_dest + dest_comp->h;

            opj_image_comp_t* src_comp = p_j2k->m_tcd->image->comps + i;


            if (src_comp->x0 != l_x0_dest ||
                    src_comp->y0 != l_y0_dest ||
                    src_comp->w != (l_x1_dest - l_x0_dest) ||
                    src_comp->h != (l_y1_dest - l_y0_dest)) {
                copy_tile_data = true;
                break;
            }
        }
    }
    return copy_tile_data;
}

static bool j2k_decode_tiles ( j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager)
{
    bool l_go_on = true;
    uint32_t l_current_tile_no=0;
    uint64_t l_data_size=0,l_max_data_size=0;
    uint32_t l_nb_comps=0;
    uint8_t * l_current_data=nullptr;
    uint32_t nr_tiles = 0;
	uint32_t num_tiles_to_decode = p_j2k->m_cp.th * p_j2k->m_cp.tw;
	bool clearOutputOnInit = false;
    if (j2k_needs_copy_tile_data(p_j2k, num_tiles_to_decode)) {
        l_current_data = (uint8_t*)grok_malloc(1);
        if (!l_current_data) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to decode tiles\n");
            return false;
        }
        l_max_data_size = 1;
		clearOutputOnInit = num_tiles_to_decode > 1;
    }
	uint32_t num_tiles_decoded = 0;

    for (nr_tiles=0; nr_tiles < num_tiles_to_decode; nr_tiles++) {
		uint32_t l_tile_x0, l_tile_y0, l_tile_x1, l_tile_y1;
		l_tile_x0 = l_tile_y0 = l_tile_x1 = l_tile_y1 = 0;
        if (! j2k_read_tile_header( p_j2k,
                                        &l_current_tile_no,
                                        &l_data_size,
                                        &l_tile_x0,
										&l_tile_y0,
                                        &l_tile_x1,
										&l_tile_y1,
                                        &l_nb_comps,
                                        &l_go_on,
                                        p_stream,
                                        p_manager)) {
            if (l_current_data)
                grok_free(l_current_data);
            return false;
        }

        if (! l_go_on) {
            break;
        }

        if (l_current_data && (l_data_size > l_max_data_size)) {
            uint8_t *l_new_current_data = (uint8_t *) grok_realloc(l_current_data, l_data_size);
            if (! l_new_current_data) {
                grok_free(l_current_data);
                event_msg(p_manager, EVT_ERROR, "Not enough memory to decode tile %d/%d\n", l_current_tile_no +1, num_tiles_to_decode);
                return false;
            }
            l_current_data = l_new_current_data;
            l_max_data_size = l_data_size;
        }

		try {
			if (!j2k_decode_tile(p_j2k, l_current_tile_no, l_current_data, l_data_size, p_stream, p_manager)) {
				if (l_current_data)
					grok_free(l_current_data);
				event_msg(p_manager, EVT_ERROR, "Failed to decode tile %d/%d\n", l_current_tile_no + 1, num_tiles_to_decode);
				return false;
			}
		}
		catch (DecodeUnknownMarkerAtEndOfTileException e) {
			// only worry about exception if we have more tiles to decode
			if (nr_tiles < num_tiles_to_decode - 1) {
				event_msg(p_manager, EVT_ERROR, "Stream too short, expected SOT\n");
				if (l_current_data)
					grok_free(l_current_data);
				event_msg(p_manager, EVT_ERROR, "Failed to decode tile %d/%d\n", l_current_tile_no + 1, num_tiles_to_decode);
				return false;
			}
		}
        //event_msg(p_manager, EVT_INFO, "Tile %d/%d has been decoded.\n", l_current_tile_no +1, num_tiles_to_decode);

        /* copy from current data to output image, if necessary */
        if (l_current_data) {
            if (!j2k_copy_decoded_tile_to_output_image(p_j2k->m_tcd, 
															l_current_data,
															p_j2k->m_output_image,
															clearOutputOnInit,
															p_manager)) {
                grok_free(l_current_data);
                return false;
            }
           // event_msg(p_manager, EVT_INFO, "Image data has been updated with tile %d.\n\n", l_current_tile_no + 1);
        }

		num_tiles_decoded++;

        if(p_stream->get_number_byte_left() == 0
                && p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_NEOC)
            break;
    }

    if (l_current_data) {
        grok_free(l_current_data);
    }

	if (num_tiles_decoded == 0) {
		event_msg(p_manager, EVT_ERROR, "No tiles were decoded. Exiting\n");
		return false;
	}
	else if (num_tiles_decoded < num_tiles_to_decode) {
		event_msg(p_manager, EVT_WARNING, "Only %d out of %d tiles were decoded\n", num_tiles_decoded, num_tiles_to_decode);
		return true;
	}
	return true;
}

/**
 * Sets up the procedures to do on decoding data. Developers wanting to extend the library can add their own reading procedures.
 */
static bool j2k_setup_decoding (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_decode_tiles, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom procedures */
    return true;
}

/*
 * Read and decode one tile.
 */
static bool j2k_decode_one_tile ( j2k_t *p_j2k,
                                      GrokStream *p_stream,
                                      event_mgr_t * p_manager)
{
    bool l_go_on = true;
    uint32_t l_current_tile_no;
    uint32_t l_tile_no_to_dec;
    uint64_t l_data_size=0,l_max_data_size=0;
    uint32_t l_tile_x0,l_tile_y0,l_tile_x1,l_tile_y1;
    uint32_t l_nb_comps;
    uint8_t * l_current_data=nullptr;

    if (j2k_needs_copy_tile_data(p_j2k,1)) {
        l_current_data = (uint8_t*)grok_malloc(1);
        if (!l_current_data) {
            event_msg(p_manager, EVT_ERROR, "Not enough memory to decode tiles\n");
            return false;
        }
        l_max_data_size = 1;
    }

    /*Allocate and initialize some elements of codestream index if not already done*/
    if( !p_j2k->cstr_index->tile_index) {
        if (!j2k_allocate_tile_element_cstr_index(p_j2k)) {
            if (l_current_data)
                grok_free(l_current_data);
            return false;
        }
    }
    /* Move into the codestream to the first SOT used to decode the desired tile */
    l_tile_no_to_dec = p_j2k->m_specific_param.m_decoder.m_tile_ind_to_dec;
    if (p_j2k->cstr_index->tile_index)
        if(p_j2k->cstr_index->tile_index->tp_index) {
            if ( ! p_j2k->cstr_index->tile_index[l_tile_no_to_dec].nb_tps) {
                /* the index for this tile has not been built,
                 *  so move to the last SOT read */
                if ( !(p_stream->seek( p_j2k->m_specific_param.m_decoder.m_last_sot_read_pos+2, p_manager)) ) {
                    event_msg(p_manager, EVT_ERROR, "Problem with seek function\n");
                    if (l_current_data)
                        grok_free(l_current_data);
                    return false;
                }
            } else {
                if ( !(p_stream->seek( p_j2k->cstr_index->tile_index[l_tile_no_to_dec].tp_index[0].start_pos+2, p_manager)) ) {
                    event_msg(p_manager, EVT_ERROR, "Problem with seek function\n");
                    if (l_current_data)
                        grok_free(l_current_data);
                    return false;
                }
            }
            /* Special case if we have previously read the EOC marker (if the previous tile decoded is the last ) */
            if(p_j2k->m_specific_param.m_decoder.m_state == J2K_DEC_STATE_EOC)
                p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_TPHSOT;
        }

    for (;;) {
        if (! j2k_read_tile_header( p_j2k,
                                        &l_current_tile_no,
                                        &l_data_size,
                                        &l_tile_x0,
										&l_tile_y0,
                                        &l_tile_x1,
										&l_tile_y1,
                                        &l_nb_comps,
                                        &l_go_on,
                                        p_stream,
                                        p_manager)) {
            if (l_current_data)
                grok_free(l_current_data);
            return false;
        }

        if (! l_go_on) {
            break;
        }

        if (l_current_data && l_data_size > l_max_data_size) {
            uint8_t *l_new_current_data = (uint8_t *) grok_realloc(l_current_data, l_data_size);
            if (! l_new_current_data) {
                grok_free(l_current_data);
                l_current_data = nullptr;
                event_msg(p_manager, EVT_ERROR, "Not enough memory to decode tile %d/%d\n", l_current_tile_no+1, p_j2k->m_cp.th * p_j2k->m_cp.tw);
                return false;
            }
            l_current_data = l_new_current_data;
            l_max_data_size = l_data_size;
        }

		try {
			if (!j2k_decode_tile(p_j2k, l_current_tile_no, l_current_data, l_data_size, p_stream, p_manager)) {
				if (l_current_data)
					grok_free(l_current_data);
				return false;
			}
		}
		catch (DecodeUnknownMarkerAtEndOfTileException e) {
			// suppress exception
		}
        //event_msg(p_manager, EVT_INFO, "Tile %d/%d has been decoded.\n", l_current_tile_no+1, p_j2k->m_cp.th * p_j2k->m_cp.tw);

        if (l_current_data) {
            if (!j2k_copy_decoded_tile_to_output_image(p_j2k->m_tcd, 
															l_current_data,
															p_j2k->m_output_image,
															false,
															p_manager)) {
                grok_free(l_current_data);
                return false;
            }
        }
        //event_msg(p_manager, EVT_INFO, "Image data has been updated with tile %d.\n\n", l_current_tile_no+1);
        if(l_current_tile_no == l_tile_no_to_dec) {
            /* move into the codestream to the first SOT (FIXME or not move?)*/
            if (!(p_stream->seek( p_j2k->cstr_index->main_head_end + 2, p_manager) ) ) {
                event_msg(p_manager, EVT_ERROR, "Problem with seek function\n");
                if (l_current_data)
                    grok_free(l_current_data);
                return false;
            }
            break;
        } else {
            event_msg(p_manager, EVT_WARNING, "Tile read, decoded and updated is not the desired one (%d vs %d).\n", l_current_tile_no+1, l_tile_no_to_dec+1);
        }

    }
    if (l_current_data)
        grok_free(l_current_data);
    return true;
}

/**
 * Sets up the procedures to do on decoding one tile. Developers wanting to extend the library can add their own reading procedures.
 */
static bool j2k_setup_decoding_tile (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    /* preconditions*/
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_decode_one_tile, p_manager)) {
        return false;
    }
    /* DEVELOPER CORNER, add your custom procedures */
    return true;
}

bool j2k_decode(j2k_t * p_j2k,
					grok_plugin_tile_t* tile,
                    GrokStream * p_stream,
                    opj_image_t * p_image,
                    event_mgr_t * p_manager)
{
    if (!p_image)
        return false;

    p_j2k->m_output_image = opj_image_create0();
    if (! (p_j2k->m_output_image)) {
        return false;
    }
    opj_copy_image_header(p_image, p_j2k->m_output_image);

    /* customization of the decoding */
	if (!j2k_setup_decoding(p_j2k, p_manager))
		return false;
	p_j2k->m_tcd->current_plugin_tile = tile;

    /* Decode the codestream */
    if (! j2k_exec (p_j2k,p_j2k->m_procedure_list,p_stream,p_manager)) {
        opj_image_destroy(p_j2k->m_private_image);
        p_j2k->m_private_image = nullptr;
        return false;
    }

    /* Move data and information from codec output image to user output image*/
    j2k_transfer_image_data(p_j2k->m_output_image, p_image);
    return true;
}

bool j2k_get_tile(  j2k_t *p_j2k,
                        GrokStream *p_stream,
                        opj_image_t* p_image,
                        event_mgr_t * p_manager,
                        uint32_t tile_index )
{
    uint32_t compno;
    uint32_t l_tile_x, l_tile_y;
    opj_image_comp_t* l_img_comp;
    rect_t original_image_rect, tile_rect, overlap_rect;

    if (!p_image) {
        event_msg(p_manager, EVT_ERROR, "We need an image previously created.\n");
        return false;
    }

    if ( /*(tile_index < 0) &&*/ (tile_index >= p_j2k->m_cp.tw * p_j2k->m_cp.th) ) {
        event_msg(p_manager, EVT_ERROR, "Tile index provided by the user is incorrect %d (max = %d) \n", tile_index, (p_j2k->m_cp.tw * p_j2k->m_cp.th) - 1);
        return false;
    }

    /* Compute the dimension of the desired tile*/
    l_tile_x = tile_index % p_j2k->m_cp.tw;
    l_tile_y = tile_index / p_j2k->m_cp.tw;

	original_image_rect= rect_t(p_image->x0,
								  p_image->y0,
								  p_image->x1,
								  p_image->y1);

    p_image->x0 = l_tile_x * p_j2k->m_cp.tdx + p_j2k->m_cp.tx0;
    if (p_image->x0 < p_j2k->m_private_image->x0)
        p_image->x0 = p_j2k->m_private_image->x0;
    p_image->x1 = (l_tile_x + 1) * p_j2k->m_cp.tdx + p_j2k->m_cp.tx0;
    if (p_image->x1 > p_j2k->m_private_image->x1)
        p_image->x1 = p_j2k->m_private_image->x1;

    p_image->y0 = l_tile_y * p_j2k->m_cp.tdy + p_j2k->m_cp.ty0;
    if (p_image->y0 < p_j2k->m_private_image->y0)
        p_image->y0 = p_j2k->m_private_image->y0;
    p_image->y1 = (l_tile_y + 1) * p_j2k->m_cp.tdy + p_j2k->m_cp.ty0;
    if (p_image->y1 > p_j2k->m_private_image->y1)
        p_image->y1 = p_j2k->m_private_image->y1;

    tile_rect.x0 = p_image->x0;
    tile_rect.y0 = p_image->y0;
    tile_rect.x1 = p_image->x1;
    tile_rect.y1 = p_image->y1;

    if (original_image_rect.is_non_degenerate() &&
		tile_rect.is_non_degenerate() &&
		original_image_rect.clip(&tile_rect, &overlap_rect) &&
		overlap_rect.is_non_degenerate()) {
        p_image->x0 = (uint32_t)overlap_rect.x0;
        p_image->y0 = (uint32_t)overlap_rect.y0;
        p_image->x1 = (uint32_t)overlap_rect.x1;
        p_image->y1 = (uint32_t)overlap_rect.y1;
    } else {
        event_msg(p_manager, EVT_WARNING, "Decode region <%d,%d,%d,%d> does not overlap requested tile %d. Ignoring.\n",
                      original_image_rect.x0,
                      original_image_rect.y0,
                      original_image_rect.x1,
                      original_image_rect.y1,
                      tile_index);
    }

    l_img_comp = p_image->comps;
    for (compno=0; compno < p_image->numcomps; ++compno) {
        uint32_t l_comp_x1, l_comp_y1;

        l_img_comp->decodeScaleFactor = p_j2k->m_private_image->comps[compno].decodeScaleFactor;

        l_img_comp->x0 = ceildiv<uint32_t>(p_image->x0, l_img_comp->dx);
        l_img_comp->y0 = ceildiv<uint32_t>(p_image->y0, l_img_comp->dy);
        l_comp_x1 = ceildiv<uint32_t>(p_image->x1, l_img_comp->dx);
        l_comp_y1 = ceildiv<uint32_t>(p_image->y1, l_img_comp->dy);

        l_img_comp->w = (uint_ceildivpow2(l_comp_x1, l_img_comp->decodeScaleFactor) - uint_ceildivpow2(l_img_comp->x0, l_img_comp->decodeScaleFactor));
        l_img_comp->h = (uint_ceildivpow2(l_comp_y1, l_img_comp->decodeScaleFactor) - uint_ceildivpow2(l_img_comp->y0, l_img_comp->decodeScaleFactor));

        l_img_comp++;
    }

    /* Destroy the previous output image*/
    if (p_j2k->m_output_image)
        opj_image_destroy(p_j2k->m_output_image);

    /* Create the ouput image from the information previously computed*/
    p_j2k->m_output_image = opj_image_create0();
    if (! (p_j2k->m_output_image)) {
        return false;
    }
    opj_copy_image_header(p_image, p_j2k->m_output_image);

    p_j2k->m_specific_param.m_decoder.m_tile_ind_to_dec = (int32_t)tile_index;

	// reset tile part numbers, in case we are re-using the same codec object from previous decode
	uint32_t l_nb_tiles = p_j2k->m_cp.tw * p_j2k->m_cp.th;
	for (uint32_t i = 0; i < l_nb_tiles; ++i) {
		p_j2k->m_cp.tcps[i].m_current_tile_part_number = -1;
	}

    /* customization of the decoding */
	if (!j2k_setup_decoding_tile(p_j2k, p_manager))
		return false;

    /* Decode the codestream */
    if (! j2k_exec (p_j2k,p_j2k->m_procedure_list,p_stream,p_manager)) {
        opj_image_destroy(p_j2k->m_private_image);
        p_j2k->m_private_image = nullptr;
        return false;
    }

    /* Move data information from codec output image to user output image*/
    j2k_transfer_image_data(p_j2k->m_output_image,	p_image);

    return true;
}

bool j2k_set_decoded_resolution_factor(j2k_t *p_j2k,
        uint32_t res_factor,
        event_mgr_t * p_manager)
{
    uint32_t it_comp;

    p_j2k->m_cp.m_specific_param.m_dec.m_reduce = res_factor;

    if (p_j2k->m_private_image) {
        if (p_j2k->m_private_image->comps) {
            if (p_j2k->m_specific_param.m_decoder.m_default_tcp) {
                if (p_j2k->m_specific_param.m_decoder.m_default_tcp->tccps) {
                    for (it_comp = 0 ; it_comp < p_j2k->m_private_image->numcomps; it_comp++) {
                        uint32_t max_res = p_j2k->m_specific_param.m_decoder.m_default_tcp->tccps[it_comp].numresolutions;
                        if ( res_factor >= max_res) {
                            event_msg(p_manager, EVT_ERROR, "Resolution factor is greater than the maximum resolution in the component.\n");
                            return false;
                        }
                        p_j2k->m_private_image->comps[it_comp].decodeScaleFactor = res_factor;
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

bool j2k_encode(j2k_t * p_j2k,
					grok_plugin_tile_t* tile,
                    GrokStream *p_stream,
                    event_mgr_t * p_manager )
{
    uint32_t i, j;
    uint32_t l_nb_tiles;
	uint64_t l_max_tile_size = 0;
	uint64_t l_current_tile_size;
    uint8_t * l_current_data = nullptr;
    bool l_reuse_data = false;
    tcd_t* p_tcd = nullptr;

    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    p_tcd = p_j2k->m_tcd;
	p_tcd->current_plugin_tile = tile;

    l_nb_tiles = p_j2k->m_cp.th * p_j2k->m_cp.tw;
    if (l_nb_tiles == 1) {
        l_reuse_data = true;
#ifdef __SSE__
        for (j=0; j<p_j2k->m_tcd->image->numcomps; ++j) {
            opj_image_comp_t * l_img_comp = p_tcd->image->comps + j;
            if (((size_t)l_img_comp->data & 0xFU) != 0U) { /* tile data shall be aligned on 16 bytes */
                l_reuse_data = false;
            }
        }
#endif
    }
    for (i=0; i<l_nb_tiles; ++i) {
        if (! j2k_pre_write_tile(p_j2k,i,p_manager)) {
            if (l_current_data)
                grok_free(l_current_data);
            return false;
        }

        /* if we only have one tile, then simply set tile component data equal to image component data */
        /* otherwise, allocate the data */
        for (j=0; j<p_j2k->m_tcd->image->numcomps; ++j) {
            tcd_tilecomp_t* l_tilec = p_tcd->tile->comps + j;
            if (l_reuse_data) {
				l_tilec->buf->data = (p_tcd->image->comps + j)->data;
				l_tilec->buf->owns_data = false;
            } else {
                if(! tile_buf_alloc_component_data_encode(l_tilec->buf)) {
                    event_msg(p_manager, EVT_ERROR, "Error allocating tile component data." );
                    if (l_current_data) {
                        grok_free(l_current_data);
                    }
                    return false;
                }
            }
        }
        l_current_tile_size = tcd_get_encoded_tile_size(p_j2k->m_tcd);
        if (!l_reuse_data) {
            if (l_current_tile_size > l_max_tile_size) {
                uint8_t *l_new_current_data = (uint8_t *) grok_realloc(l_current_data, l_current_tile_size);
                if (! l_new_current_data) {
                    if (l_current_data) {
                        grok_free(l_current_data);
                    }
                    event_msg(p_manager, EVT_ERROR, "Not enough memory to encode all tiles\n");
                    return false;
                }
                l_current_data = l_new_current_data;
                l_max_tile_size = l_current_tile_size;
            }

            /* copy image data (32 bit) to l_current_data as contiguous, all-component, zero offset buffer */
            /* 32 bit components @ 8 bit precision get converted to 8 bit */
            /* 32 bit components @ 16 bit precision get converted to 16 bit */
            j2k_get_tile_data(p_j2k->m_tcd,l_current_data);

            /* now copy this data into the tile component */
            if (! tcd_copy_tile_data(p_j2k->m_tcd,l_current_data,l_current_tile_size)) {
                event_msg(p_manager, EVT_ERROR, "Size mismatch between tile data and sent data." );
                grok_free(l_current_data);
                return false;
            }
        }


		if (!j2k_post_write_tile(p_j2k, p_stream, p_manager)) {
			if (l_current_data) {
				grok_free(l_current_data);
			}
			return false;
		}
    }
    if (l_current_data) {
        grok_free(l_current_data);
    }
    return true;
}

bool j2k_end_compress(  j2k_t *p_j2k,
                            GrokStream *p_stream,
                            event_mgr_t * p_manager)
{
    /* customization of the encoding */
    if (! j2k_setup_end_compress(p_j2k, p_manager)) {
        return false;
    }

    if (! j2k_exec (p_j2k, p_j2k->m_procedure_list, p_stream, p_manager)) {
        return false;
    }
    return true;
}

bool j2k_start_compress(j2k_t *p_j2k,
                            GrokStream *p_stream,
                            opj_image_t * p_image,
                            event_mgr_t * p_manager)
{
    
    assert(p_j2k != nullptr);
    assert(p_stream != nullptr);
    assert(p_manager != nullptr);

    p_j2k->m_private_image = opj_image_create0();
    if (! p_j2k->m_private_image) {
        event_msg(p_manager, EVT_ERROR, "Failed to allocate image header." );
        return false;
    }
    opj_copy_image_header(p_image, p_j2k->m_private_image);

    /* TODO_MSD: Find a better way */
    if (p_image->comps) {
        uint32_t it_comp;
        for (it_comp = 0 ; it_comp < p_image->numcomps; it_comp++) {
            if (p_image->comps[it_comp].data) {
                p_j2k->m_private_image->comps[it_comp].data =p_image->comps[it_comp].data;
                p_image->comps[it_comp].data = nullptr;

            }
        }
    }

    /* customization of the validation */
    if (! j2k_setup_encoding_validation (p_j2k, p_manager)) {
        return false;
    }

    /* validation of the parameters codec */
    if (! j2k_exec(p_j2k,p_j2k->m_validation_list,p_stream,p_manager)) {
        return false;
    }

    /* customization of the encoding */
    if (! j2k_setup_header_writing(p_j2k, p_manager)) {
        return false;
    }

    /* write header */
    if (! j2k_exec (p_j2k,p_j2k->m_procedure_list,p_stream,p_manager)) {
        return false;
    }

    return true;
}

static bool j2k_pre_write_tile (j2k_t * p_j2k,
								uint32_t p_tile_index,
								event_mgr_t * p_manager )
{
    if (p_tile_index != p_j2k->m_current_tile_number) {
        event_msg(p_manager, EVT_ERROR, "The given tile index does not match." );
        return false;
    }
    //event_msg(p_manager, EVT_INFO, "tile number %d / %d\n", p_j2k->m_current_tile_number + 1, p_j2k->m_cp.tw * p_j2k->m_cp.th);
    p_j2k->m_specific_param.m_encoder.m_current_tile_part_number = 0;
    p_j2k->m_tcd->cur_totnum_tp = p_j2k->m_cp.tcps[p_tile_index].m_nb_tile_parts;
    p_j2k->m_specific_param.m_encoder.m_current_poc_tile_part_number = 0;

    /* initialisation before tile encoding  */
    if (! tcd_init_encode_tile(p_j2k->m_tcd, p_j2k->m_current_tile_number, p_manager)) {
        return false;
    }

    return true;
}

static void get_tile_dimensions(opj_image_t * l_image,
                                    tcd_tilecomp_t * l_tilec,
                                    opj_image_comp_t * l_img_comp,
                                    uint32_t* l_size_comp,
                                    uint32_t* l_width,
                                    uint32_t* l_height,
                                    uint32_t* l_offset_x,
                                    uint32_t* l_offset_y,
                                    uint32_t* l_image_width,
                                    uint32_t* l_stride,
                                    uint64_t* l_tile_offset)
{
    uint32_t l_remaining;
    *l_size_comp = l_img_comp->prec >> 3; /* (/8) */
    l_remaining = l_img_comp->prec & 7;  /* (%8) */
    if (l_remaining) {
        *l_size_comp += 1;
    }

    if (*l_size_comp == 3) {
        *l_size_comp = 4;
    }

    *l_width  = (l_tilec->x1 - l_tilec->x0);
    *l_height = (l_tilec->y1 - l_tilec->y0);
    *l_offset_x = ceildiv<uint32_t>(l_image->x0, l_img_comp->dx);
    *l_offset_y = ceildiv<uint32_t>(l_image->y0, l_img_comp->dy);
    *l_image_width = ceildiv<uint32_t>(l_image->x1 - l_image->x0, l_img_comp->dx);
    *l_stride = *l_image_width - *l_width;
    *l_tile_offset = (l_tilec->x0 - *l_offset_x) + (uint64_t)(l_tilec->y0 - *l_offset_y) * *l_image_width;
}

static void j2k_get_tile_data (tcd_t * p_tcd, uint8_t * p_data)
{
    uint32_t i,j,k = 0;

    for (i=0; i<p_tcd->image->numcomps; ++i) {
        opj_image_t * l_image =  p_tcd->image;
        int32_t * l_src_ptr;
        tcd_tilecomp_t * l_tilec = p_tcd->tile->comps + i;
        opj_image_comp_t * l_img_comp = l_image->comps + i;
		uint32_t l_size_comp, l_width, l_height, l_offset_x, l_offset_y, l_image_width, l_stride;
		uint64_t l_tile_offset;

        get_tile_dimensions(l_image,
                                l_tilec,
                                l_img_comp,
                                &l_size_comp,
                                &l_width,
                                &l_height,
                                &l_offset_x,
                                &l_offset_y,
                                &l_image_width,
                                &l_stride,
                                &l_tile_offset);

        l_src_ptr = l_img_comp->data + l_tile_offset;

        switch (l_size_comp) {
        case 1: {
            char * l_dest_ptr = (char*) p_data;
            if (l_img_comp->sgnd) {
                for     (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr) = (char) (*l_src_ptr);
                        ++l_dest_ptr;
                        ++l_src_ptr;
                    }
                    l_src_ptr += l_stride;
                }
            } else {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr) = (char)((*l_src_ptr)&0xff);
                        ++l_dest_ptr;
                        ++l_src_ptr;
                    }
                    l_src_ptr += l_stride;
                }
            }

            p_data = (uint8_t*) l_dest_ptr;
        }
        break;
        case 2: {
            int16_t * l_dest_ptr = (int16_t *) p_data;
            if (l_img_comp->sgnd) {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (int16_t) (*(l_src_ptr++));
                    }
                    l_src_ptr += l_stride;
                }
            } else {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (int16_t)((*(l_src_ptr++)) & 0xffff);
                    }
                    l_src_ptr += l_stride;
                }
            }

            p_data = (uint8_t*) l_dest_ptr;
        }
        break;
        case 4: {
            int32_t * l_dest_ptr = (int32_t *) p_data;
            for (j=0; j<l_height; ++j) {
                for (k=0; k<l_width; ++k) {
                    *(l_dest_ptr++) = *(l_src_ptr++);
                }
                l_src_ptr += l_stride;
            }

            p_data = (uint8_t*) l_dest_ptr;
        }
        break;
        }
    }
}

static bool j2k_post_write_tile (   j2k_t * p_j2k,
										GrokStream *p_stream,
										event_mgr_t * p_manager )
{
    uint64_t l_nb_bytes_written;
    uint64_t l_tile_size = 0;
    uint64_t l_available_data;

	auto l_cp = &(p_j2k->m_cp);
	auto l_image = p_j2k->m_private_image;
	auto l_img_comp = l_image->comps;
	l_tile_size = 0;

	for (uint32_t i = 0; i<l_image->numcomps; ++i) {
		l_tile_size += (uint64_t)ceildiv<uint32_t>(l_cp->tdx, l_img_comp->dx) *
			ceildiv<uint32_t>(l_cp->tdy, l_img_comp->dy) *
			l_img_comp->prec;
		++l_img_comp;
	}

	l_tile_size = (uint64_t)((double)(l_tile_size) * 0.1625); /* 1.3/8 = 0.1625 */
	l_tile_size += j2k_get_specific_header_sizes(p_j2k);

	// ToDo: use better estimate of signaling overhead for packets, 
	// to avoid hard-coding this lower bound on tile buffer size

	// allocate at least 256 bytes per component
	if (l_tile_size < 256 * l_image->numcomps)
		l_tile_size = 256 * l_image->numcomps;

    l_available_data = l_tile_size;
    l_nb_bytes_written = 0;
    if (! j2k_write_first_tile_part(p_j2k,&l_nb_bytes_written,l_available_data,p_stream,p_manager)) {
        return false;
    }
    l_available_data -= l_nb_bytes_written;
    l_nb_bytes_written = 0;
    if (! j2k_write_all_tile_parts(p_j2k,&l_nb_bytes_written,l_available_data,p_stream,p_manager)) {
        return false;
    }
    ++p_j2k->m_current_tile_number;
    return true;
}

static bool j2k_setup_end_compress (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    /* DEVELOPER CORNER, insert your custom procedures */
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_eoc, p_manager)) {
        return false;
    }

    if (OPJ_IS_CINEMA(p_j2k->m_cp.rsiz)) {
        if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_updated_tlm, p_manager)) {
            return false;
        }
    }

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_epc, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_end_encoding, p_manager)) {
        return false;
    }
    return true;
}

static bool j2k_setup_encoding_validation (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(p_j2k->m_validation_list, (procedure)j2k_build_encoder, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_validation_list, (procedure)j2k_encoding_validation, p_manager)) {
        return false;
    }

    /* DEVELOPER CORNER, add your custom validation procedure */
    if (! procedure_list_add_procedure(p_j2k->m_validation_list, (procedure)j2k_mct_validation, p_manager)) {
        return false;
    }

    return true;
}

static bool j2k_setup_header_writing (j2k_t *p_j2k, event_mgr_t * p_manager)
{
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_init_info, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_soc, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_siz, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_cod, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_qcd, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_all_coc, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_all_qcc, p_manager)) {
        return false;
    }

    if (OPJ_IS_CINEMA(p_j2k->m_cp.rsiz)) {
        if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_tlm, p_manager)) {
            return false;
        }

        if (p_j2k->m_cp.rsiz == OPJ_PROFILE_CINEMA_4K) {
            if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_poc, p_manager)) {
                return false;
            }
        }
    }

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_regions, p_manager)) {
        return false;
    }

    if (p_j2k->m_cp.comment != nullptr)  {
        if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_com, p_manager)) {
            return false;
        }
    }

    /* DEVELOPER CORNER, insert your custom procedures */
    if (OPJ_IS_PART2(p_j2k->m_cp.rsiz) && (p_j2k->m_cp.rsiz & OPJ_EXTENSION_MCT) ) {
        if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_write_mct_data_group, p_manager)) {
            return false;
        }
    }
    /* End of Developer Corner */

    if (p_j2k->cstr_index) {
        if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_get_end_header, p_manager)) {
            return false;
        }
    }

    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_create_tcd, p_manager)) {
        return false;
    }
    if (! procedure_list_add_procedure(p_j2k->m_procedure_list,(procedure)j2k_update_rates, p_manager)) {
        return false;
    }

    return true;
}

static bool j2k_write_first_tile_part (j2k_t *p_j2k,
        uint64_t * p_data_written,
        uint64_t p_total_data_size,
        GrokStream *p_stream,
        event_mgr_t * p_manager )
{
    uint64_t l_nb_bytes_written = 0;
    uint64_t l_current_nb_bytes_written;

    tcd_t * l_tcd = nullptr;
    cp_t * l_cp = nullptr;

    l_tcd = p_j2k->m_tcd;
    l_cp = &(p_j2k->m_cp);

    l_tcd->cur_pino = 0;

    /*Get number of tile parts*/
    p_j2k->m_specific_param.m_encoder.m_current_poc_tile_part_number = 0;

    /* INDEX >> */
    /* << INDEX */

    l_current_nb_bytes_written = 0;
	uint64_t psot_location=0;
    if (! j2k_write_sot(p_j2k,p_stream,&psot_location, &l_current_nb_bytes_written,p_manager)) {
        return false;
    }
    l_nb_bytes_written += l_current_nb_bytes_written;
    p_total_data_size -= l_current_nb_bytes_written;

    if (!OPJ_IS_CINEMA(l_cp->rsiz)) {
        if (l_cp->tcps[p_j2k->m_current_tile_number].numpocs) {
			l_current_nb_bytes_written = 0;
			if (!j2k_write_poc_in_memory(p_j2k, p_stream, &l_current_nb_bytes_written, p_manager))
				return false;
			l_nb_bytes_written += l_current_nb_bytes_written;
			p_total_data_size -= l_current_nb_bytes_written;
        }
    }

    l_current_nb_bytes_written = 0;
    if (! j2k_write_sod(p_j2k,l_tcd,&l_current_nb_bytes_written,p_total_data_size,p_stream,p_manager)) {
        return false;
    }
    l_nb_bytes_written += l_current_nb_bytes_written;
	p_total_data_size -= l_current_nb_bytes_written;
    * p_data_written = l_nb_bytes_written;

    /* Writing Psot in SOT marker */
	/* PSOT */
	auto currentLocation = p_stream->tell();
	p_stream->seek(psot_location, p_manager);
	if (!p_stream->write_int((uint32_t)l_nb_bytes_written, p_manager)) {
		return false;
	}
	p_stream->seek(currentLocation, p_manager);
    if (OPJ_IS_CINEMA(l_cp->rsiz)) {
        j2k_update_tlm(p_j2k, (uint32_t)l_nb_bytes_written);
    }
    return true;
}

static bool j2k_write_all_tile_parts(  j2k_t *p_j2k,
        uint64_t * p_data_written,
        uint64_t p_total_data_size,
        GrokStream *p_stream,
        event_mgr_t * p_manager
                                        )
{
    uint32_t tilepartno=0;
    uint64_t l_nb_bytes_written = 0;
    uint64_t l_current_nb_bytes_written;
    uint32_t l_part_tile_size;
    uint32_t tot_num_tp;
    uint32_t pino;

    tcp_t *l_tcp = nullptr;
    tcd_t * l_tcd = nullptr;
    cp_t * l_cp = nullptr;

    l_tcd = p_j2k->m_tcd;
    l_cp = &(p_j2k->m_cp);
    l_tcp = l_cp->tcps + p_j2k->m_current_tile_number;

    /*Get number of tile parts*/
    tot_num_tp = j2k_get_num_tp(l_cp,0,p_j2k->m_current_tile_number);

    /* start writing remaining tile parts */
    ++p_j2k->m_specific_param.m_encoder.m_current_tile_part_number;
    for (tilepartno = 1; tilepartno < tot_num_tp ; ++tilepartno) {
        p_j2k->m_specific_param.m_encoder.m_current_poc_tile_part_number = tilepartno;
        l_current_nb_bytes_written = 0;
        l_part_tile_size = 0;

		uint64_t psot_location = 0;
        if (! j2k_write_sot(p_j2k,p_stream,&psot_location, &l_current_nb_bytes_written,p_manager)) {
            return false;
        }
        l_nb_bytes_written += l_current_nb_bytes_written;
        p_total_data_size -= l_current_nb_bytes_written;
        l_part_tile_size += (uint32_t)l_current_nb_bytes_written;

        l_current_nb_bytes_written = 0;
        if (! j2k_write_sod(p_j2k,l_tcd,&l_current_nb_bytes_written,p_total_data_size,p_stream,p_manager)) {
            return false;
        }
        l_nb_bytes_written += l_current_nb_bytes_written;
        p_total_data_size -= l_current_nb_bytes_written;
        l_part_tile_size += (uint32_t)l_current_nb_bytes_written;

		/* Writing Psot in SOT marker */
		/* PSOT */
		auto currentLocation = p_stream->tell();
		p_stream->seek(psot_location, p_manager);
		if (!p_stream->write_int(l_part_tile_size, p_manager)) {
			return false;
		}
		p_stream->seek(currentLocation, p_manager);
        if (OPJ_IS_CINEMA(l_cp->rsiz)) {
            j2k_update_tlm(p_j2k,l_part_tile_size);
        }

        ++p_j2k->m_specific_param.m_encoder.m_current_tile_part_number;
    }

    for (pino = 1; pino <= l_tcp->numpocs; ++pino) {
        l_tcd->cur_pino = pino;

        /*Get number of tile parts*/
        tot_num_tp = j2k_get_num_tp(l_cp,pino,p_j2k->m_current_tile_number);
        for (tilepartno = 0; tilepartno < tot_num_tp ; ++tilepartno) {
            p_j2k->m_specific_param.m_encoder.m_current_poc_tile_part_number = tilepartno;
            l_current_nb_bytes_written = 0;
            l_part_tile_size = 0;
			uint64_t psot_location = 0;
            if (! j2k_write_sot(p_j2k,p_stream,&psot_location, &l_current_nb_bytes_written,p_manager)) {
                return false;
            }

            l_nb_bytes_written += l_current_nb_bytes_written;
            p_total_data_size -= l_current_nb_bytes_written;
            l_part_tile_size += (uint32_t)l_current_nb_bytes_written;

            l_current_nb_bytes_written = 0;
            if (! j2k_write_sod(p_j2k,l_tcd,&l_current_nb_bytes_written,p_total_data_size,p_stream,p_manager)) {
                return false;
            }

            l_nb_bytes_written += l_current_nb_bytes_written;
            p_total_data_size -= l_current_nb_bytes_written;
            l_part_tile_size += (uint32_t)l_current_nb_bytes_written;

            /* Writing Psot in SOT marker */
			/* PSOT */
			auto currentLocation = p_stream->tell();
			p_stream->seek(psot_location, p_manager);
			if (!p_stream->write_int(l_part_tile_size, p_manager)) {
				return false;
			}
			p_stream->seek(currentLocation, p_manager);

            if (OPJ_IS_CINEMA(l_cp->rsiz)) {
                j2k_update_tlm(p_j2k,l_part_tile_size);
            }
            ++p_j2k->m_specific_param.m_encoder.m_current_tile_part_number;
        }
    }
    *p_data_written = l_nb_bytes_written;
    return true;
}

static bool j2k_write_updated_tlm( j2k_t *p_j2k,
                                       GrokStream *p_stream,
                                       event_mgr_t * p_manager )
{
    uint32_t l_tlm_size;
    int64_t l_tlm_position, l_current_position;

    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    l_tlm_size = 5 * p_j2k->m_specific_param.m_encoder.m_total_tile_parts;
    l_tlm_position = 6 + p_j2k->m_specific_param.m_encoder.m_tlm_start;
    l_current_position = p_stream->tell();

    if (! p_stream->seek(l_tlm_position,p_manager)) {
        return false;
    }
    if (p_stream->write_bytes(p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer,l_tlm_size,p_manager) != l_tlm_size) {
        return false;
    }
    if (! p_stream->seek(l_current_position,p_manager)) {
        return false;
    }
    return true;
}

static bool j2k_end_encoding(  j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
	(void)p_stream;
	(void)p_manager;
    
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    tcd_destroy(p_j2k->m_tcd);
    p_j2k->m_tcd = nullptr;

    if (p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer) {
        grok_free(p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer);
        p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_buffer = 0;
        p_j2k->m_specific_param.m_encoder.m_tlm_sot_offsets_current = 0;
    }
    return true;
}

static bool j2k_init_info(     j2k_t *p_j2k,
                                   GrokStream *p_stream,
                                   event_mgr_t * p_manager )
{
	(void)p_stream;
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);
    return j2k_calculate_tp(&p_j2k->m_cp,&p_j2k->m_specific_param.m_encoder.m_total_tile_parts,p_j2k->m_private_image,p_manager);
}

/**
 * Creates a tile-coder decoder.
 *
 * @param       p_stream                the stream to write data to.
 * @param       p_j2k                   J2K codec.
 * @param       p_manager               the user event manager.
*/
static bool j2k_create_tcd(     j2k_t *p_j2k,
                                    GrokStream *p_stream,
                                    event_mgr_t * p_manager
                              )
{
    
	(void)p_stream;
    assert(p_j2k != nullptr);
    assert(p_manager != nullptr);
    assert(p_stream != nullptr);

    p_j2k->m_tcd = tcd_create(false);

    if (! p_j2k->m_tcd) {
        event_msg(p_manager, EVT_ERROR, "Not enough memory to create Tile Coder\n");
        return false;
    }

    if (!tcd_init(p_j2k->m_tcd,p_j2k->m_private_image,&p_j2k->m_cp, p_j2k->numThreads)) {
        tcd_destroy(p_j2k->m_tcd);
        p_j2k->m_tcd = nullptr;
        return false;
    }

    return true;
}

bool j2k_write_tile (j2k_t * p_j2k,
                         uint32_t p_tile_index,
                         uint8_t * p_data,
                         uint64_t p_data_size,
                         GrokStream *p_stream,
                         event_mgr_t * p_manager )
{
    if (! j2k_pre_write_tile(p_j2k,p_tile_index,p_manager)) {
        event_msg(p_manager, EVT_ERROR, "Error while j2k_pre_write_tile with tile index = %d\n", p_tile_index);
        return false;
    } else {
        uint32_t j;
        /* Allocate data */
        for (j=0; j<p_j2k->m_tcd->image->numcomps; ++j) {
            tcd_tilecomp_t* l_tilec = p_j2k->m_tcd->tile->comps + j;

            if(!tile_buf_alloc_component_data_encode(l_tilec->buf)) {
                event_msg(p_manager, EVT_ERROR, "Error allocating tile component data." );
                return false;
            }
        }

        /* now copy data into the tile component */
        if (! tcd_copy_tile_data(p_j2k->m_tcd,p_data,p_data_size)) {
            event_msg(p_manager, EVT_ERROR, "Size mismatch between tile data and sent data." );
            return false;
        }
        if (! j2k_post_write_tile(p_j2k,p_stream,p_manager)) {
            event_msg(p_manager, EVT_ERROR, "Error while j2k_post_write_tile with tile index = %d\n", p_tile_index);
            return false;
        }
    }

    return true;
}

}
