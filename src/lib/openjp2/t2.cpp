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
#include "testing.h"
#include <memory>


namespace grk {


	/** @defgroup T2 T2 - Implementation of a tier-2 coding */
	/*@{*/

	/** @name Local static functions */
	/*@{*/

	static void t2_putcommacode(BitIO *bio, int32_t n);

	static  bool t2_getcommacode(BitIO *bio, uint32_t* n);
	/**
	Variable length code for signaling delta Zil (truncation point)
	@param bio  Bit Input/Output component
	@param n    delta Zil
	*/
	static void t2_putnumpasses(BitIO *bio, uint32_t n);
	static bool t2_getnumpasses(BitIO *bio, uint32_t* numpasses);

	/**
	Encode a packet of a tile to a destination buffer
	@param tileno Number of the tile encoded
	@param tile Tile for which to write the packets
	@param tcp Tile coding parameters
	@param pi Packet identity
	@param dest Destination buffer
	@param p_data_written   FIXME DOC
	@param len Length of the destination buffer
	@param cstr_info Codestream information structure
	@return
	*/
	static bool t2_encode_packet(uint32_t tileno,
								tcd_tile_t *tile,
								tcp_t *tcp,
								pi_iterator_t *pi,
								GrokStream* p_stream,
								uint64_t * p_data_written,
								uint64_t len,
								opj_codestream_info_t *cstr_info,
								event_mgr_t * p_manager);

	/**
	Encode a packet of a tile to a destination buffer
	@param tileno Number of the tile encoded
	@param tile Tile for which to write the packets
	@param tcp Tile coding parameters
	@param pi Packet identity
	@param dest Destination buffer
	@param p_data_written   FIXME DOC
	@param len Length of the destination buffer
	@param cstr_info Codestream information structure
	@return
	*/
	static bool t2_encode_packet_simulate(tcd_tile_t *tile,
		tcp_t *tcp,
		pi_iterator_t *pi,
		uint64_t * p_data_written,
		uint64_t len);



	/**
	Decode a packet of a tile from a source buffer
	@param t2 T2 handle
	@param tile Tile for which to write the packets
	@param tcp Tile coding parameters
	@param pi Packet identity
	@param src Source buffer
	@param data_read   FIXME DOC
	@param max_length  FIXME DOC
	@param pack_info Packet information

	@return  FIXME DOC
	*/
	static bool t2_decode_packet(t2_t* t2,
		tcd_resolution_t* l_res,
		tcp_t *tcp,
		pi_iterator_t *pi,
		seg_buf_t* src_buf,
		uint64_t * data_read,
		event_mgr_t *p_manager);

	static bool t2_skip_packet(t2_t* p_t2,
		tcd_tile_t *p_tile,
		tcp_t *p_tcp,
		pi_iterator_t *p_pi,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager);

	static bool t2_read_packet_header(t2_t* p_t2,
		tcd_resolution_t* l_res,
		tcp_t *p_tcp,
		pi_iterator_t *p_pi,
		bool * p_is_data_present,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager);

	static bool t2_read_packet_data(tcd_resolution_t* l_res,
		pi_iterator_t *p_pi,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager);

	static bool t2_skip_packet_data(tcd_resolution_t* l_res,
		pi_iterator_t *p_pi,
		uint64_t * p_data_read,
		uint64_t p_max_length,
		event_mgr_t *p_manager);

	/**
	@param cblk
	@param index
	@param cblksty
	@param first
	*/
	static bool t2_init_seg(tcd_cblk_dec_t* cblk,
		uint32_t index,
		uint32_t cblksty,
		bool first);

	/*@}*/

	/*@}*/

	/* ----------------------------------------------------------------------- */

	/* #define RESTART 0x04 */
	static void t2_putcommacode(BitIO *bio, int32_t n)
	{
		while (--n >= 0) {
			bio->write(1, 1);
		}
		bio->write(0, 1);
	}

	static  bool t2_getcommacode(BitIO *bio, uint32_t* n)
	{
		*n = 0;
		uint32_t temp;
		bool rc = true;
		while ( (rc = bio->read(&temp, 1)) && temp) {
			++*n;
		}
		return rc;
	}

	static void t2_putnumpasses(BitIO *bio, uint32_t n)
	{
		if (n == 1) {
			bio->write(0, 1);
		}
		else if (n == 2) {
			bio->write(2, 2);
		}
		else if (n <= 5) {
			bio->write(0xc | (n - 3), 4);
		}
		else if (n <= 36) {
			bio->write(0x1e0 | (n - 6), 9);
		}
		else if (n <= 164) {
			bio->write(0xff80 | (n - 37), 16);
		}
	}

	static bool t2_getnumpasses(BitIO *bio, uint32_t* numpasses)
	{
		uint32_t n=0;
		if (!bio->read(&n, 1))
			return false;
		if (!n) {
			*numpasses = 1;
			return true;
		}
		if (!bio->read(&n, 1))
			return false;
		if (!n) {
			*numpasses =  2;
			return true;
		}
		if (!bio->read(&n, 2))
			return false;
		if (n != 3) {
			*numpasses =  n + 3;
			return true;
		}
		if (!bio->read(&n, 5))
			return false;
		if (n != 31) {
			*numpasses =  n + 6;
			return true;
		}
		if (!bio->read(&n, 7))
			return false;
		*numpasses = n + 37;
		return true;
	}

	/* ----------------------------------------------------------------------- */

	bool t2_encode_packets(t2_t* p_t2,
		uint32_t p_tile_no,
		tcd_tile_t *p_tile,
		uint32_t p_maxlayers,
		GrokStream* p_stream,
		uint64_t * p_data_written,
		uint64_t p_max_len,
		opj_codestream_info_t *cstr_info,
		uint32_t p_tp_num,
		uint32_t p_tp_pos,
		uint32_t p_pino,
		event_mgr_t * p_manager)
	{
		uint64_t l_nb_bytes = 0;
		pi_iterator_t *l_pi = nullptr;
		pi_iterator_t *l_current_pi = nullptr;
		opj_image_t *l_image = p_t2->image;
		cp_t *l_cp = p_t2->cp;
		tcp_t *l_tcp = &l_cp->tcps[p_tile_no];
		uint32_t l_nb_pocs = l_tcp->numpocs + 1;

		l_pi = pi_initialise_encode(l_image, l_cp, p_tile_no, FINAL_PASS);
		if (!l_pi) {
			return false;
		}
		pi_init_encode(l_pi, l_cp, p_tile_no, p_pino, p_tp_num, p_tp_pos, FINAL_PASS);

		l_current_pi = &l_pi[p_pino];
		if (l_current_pi->poc.prg == OPJ_PROG_UNKNOWN) {
			pi_destroy(l_pi, l_nb_pocs);
			event_msg(p_manager, EVT_ERROR, "t2_encode_packets: Unknown progression order\n");
			return false;
		}
		while (pi_next(l_current_pi)) {
			if (l_current_pi->layno < p_maxlayers) {
				l_nb_bytes = 0;

				if (!t2_encode_packet(p_tile_no,
									p_tile,
									l_tcp,
									l_current_pi,
									p_stream,
									&l_nb_bytes,
									p_max_len,
									cstr_info,
									p_manager)) {
					pi_destroy(l_pi, l_nb_pocs);
					return false;
				}

				p_max_len -= l_nb_bytes;
				*p_data_written += l_nb_bytes;

				/* INDEX >> */
				if (cstr_info) {
					if (cstr_info->index_write) {
						opj_tile_info_t *info_TL = &cstr_info->tile[p_tile_no];
						opj_packet_info_t *info_PK = &info_TL->packet[cstr_info->packno];
						if (!cstr_info->packno) {
							info_PK->start_pos = info_TL->end_header + 1;
						}
						else {
							info_PK->start_pos = ((l_cp->m_specific_param.m_enc.m_tp_on | l_tcp->POC) && info_PK->start_pos) ? info_PK->start_pos : info_TL->packet[cstr_info->packno - 1].end_pos + 1;
						}
						info_PK->end_pos = info_PK->start_pos + l_nb_bytes - 1;
						info_PK->end_ph_pos += info_PK->start_pos - 1;  /* End of packet header which now only represents the distance
																																																													to start of packet is incremented by value of start of packet*/
					}

					cstr_info->packno++;
				}
				/* << INDEX */
				++p_tile->packno;
			}
		}

		pi_destroy(l_pi, l_nb_pocs);

		return true;
	}


	bool t2_encode_packets_simulate(t2_t* p_t2,
		uint32_t p_tile_no,
		tcd_tile_t *p_tile,
		uint32_t p_maxlayers,
		uint64_t * p_data_written,
		uint64_t p_max_len,
		uint32_t p_tp_pos,
		event_mgr_t * p_manager)
	{
		opj_image_t *l_image = p_t2->image;
		cp_t *l_cp = p_t2->cp;
		tcp_t *l_tcp = l_cp->tcps + p_tile_no;
		uint32_t pocno = (l_cp->rsiz == OPJ_PROFILE_CINEMA_4K) ? 2 : 1;
		uint32_t l_max_comp = l_cp->m_specific_param.m_enc.m_max_comp_size > 0 ? l_image->numcomps : 1;
		uint32_t l_nb_pocs = l_tcp->numpocs + 1;

		if (!p_data_written)
			return false;

		pi_iterator_t* l_pi = pi_initialise_encode(l_image, l_cp, p_tile_no, THRESH_CALC);
		if (!l_pi) {
			return false;
		}
		*p_data_written = 0;
		pi_iterator_t * l_current_pi = l_pi;

		for (uint32_t compno = 0; compno < l_max_comp; ++compno) {
			uint64_t l_comp_len = 0;
			l_current_pi = l_pi;

			for (uint32_t poc = 0; poc < pocno; ++poc) {
				uint32_t l_tp_num = compno;
				pi_init_encode(l_pi, l_cp, p_tile_no, poc, l_tp_num, p_tp_pos, THRESH_CALC);

				if (l_current_pi->poc.prg == OPJ_PROG_UNKNOWN) {
					pi_destroy(l_pi, l_nb_pocs);
					event_msg(p_manager, EVT_ERROR, "t2_decode_packets_simulate: Unknown progression order\n");
					return false;
				}
				while (pi_next(l_current_pi)) {
					if (l_current_pi->layno < p_maxlayers) {
						uint64_t bytesInPacket = 0;
						if (!t2_encode_packet_simulate(p_tile, l_tcp, l_current_pi, &bytesInPacket, p_max_len)) {
							pi_destroy(l_pi, l_nb_pocs);
							return false;
						}

						l_comp_len += bytesInPacket;
						p_max_len -= bytesInPacket;
						*p_data_written += bytesInPacket;
					}
				}

				if (l_cp->m_specific_param.m_enc.m_max_comp_size) {
					if (l_comp_len > l_cp->m_specific_param.m_enc.m_max_comp_size) {
						pi_destroy(l_pi, l_nb_pocs);
						return false;
					}
				}

				++l_current_pi;
			}
		}
		pi_destroy(l_pi, l_nb_pocs);
		return true;
	}

	/* see issue 80 */
#if 0
#define JAS_FPRINTF fprintf
#else
/* issue 290 */
	static void opj_null_jas_fprintf(FILE* file, const char * format, ...)
	{
		(void)file;
		(void)format;
	}
#define JAS_FPRINTF opj_null_jas_fprintf
#endif

	bool t2_decode_packets(t2_t *p_t2,
		uint32_t p_tile_no,
		tcd_tile_t *p_tile,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager)
	{
		pi_iterator_t *l_pi = nullptr;
		uint32_t pino;
		opj_image_t *l_image = p_t2->image;
		cp_t *l_cp = p_t2->cp;
		tcp_t *l_tcp = p_t2->cp->tcps + p_tile_no;
		uint64_t l_nb_bytes_read;
		uint32_t l_nb_pocs = l_tcp->numpocs + 1;
		pi_iterator_t *l_current_pi = nullptr;
		opj_image_comp_t* l_img_comp = nullptr;

		/* create a packet iterator */
		l_pi = pi_create_decode(l_image, l_cp, p_tile_no);
		if (!l_pi) {
			return false;
		}


		l_current_pi = l_pi;

		for (pino = 0; pino <= l_tcp->numpocs; ++pino) {

			/* if the resolution needed is too low, one dim of the tilec could be equal to zero
			 * and no packets are used to decode this resolution and
			 * l_current_pi->resno is always >= p_tile->comps[l_current_pi->compno].minimum_num_resolutions
			 * and no l_img_comp->resno_decoded are computed
			 */

			if (l_current_pi->poc.prg == OPJ_PROG_UNKNOWN) {
				pi_destroy(l_pi, l_nb_pocs);
				event_msg(p_manager, EVT_ERROR, "t2_decode_packets: Unknown progression order\n");
				return false;
			}
			while (pi_next(l_current_pi)) {
				bool skip_precinct = false;
				tcd_tilecomp_t* tilec = p_tile->comps + l_current_pi->compno;
				bool skip_layer_or_res = l_current_pi->layno >= l_tcp->num_layers_to_decode ||
					l_current_pi->resno >= tilec->minimum_num_resolutions;

				l_img_comp = l_image->comps + l_current_pi->compno;

				JAS_FPRINTF(stderr,
					"packet offset=00000166 prg=%d cmptno=%02d rlvlno=%02d prcno=%03d lyrno=%02d\n\n",
					l_current_pi->poc.prg1,
					l_current_pi->compno,
					l_current_pi->resno,
					l_current_pi->precno,
					l_current_pi->layno);



				if (!skip_layer_or_res) {
					tcd_resolution_t* res = tilec->resolutions + l_current_pi->resno;
					uint32_t bandno;
					skip_precinct = true;
					for (bandno = 0; bandno < res->numbands && skip_precinct; ++bandno) {
						tcd_band_t* band = res->bands + bandno;
						auto num_precincts = band->numPrecincts;
						uint32_t precno;
						for (precno = 0; precno < num_precincts && skip_precinct; ++precno) {
							rect_t prec_rect;
							tcd_precinct_t* prec = band->precincts + precno;
							prec_rect = rect_t(prec->x0, prec->y0, prec->x1, prec->y1);
							if (tile_buf_hit_test(tilec->buf, &prec_rect)) {
								skip_precinct = false;
								break;
							}
						}
					}
				}

				if (!skip_layer_or_res && !skip_precinct) {
					l_nb_bytes_read = 0;
					if (!t2_decode_packet(p_t2,
						&p_tile->comps[l_current_pi->compno].resolutions[l_current_pi->resno],
						l_tcp,
						l_current_pi,
						src_buf,
						&l_nb_bytes_read,
						p_manager)) {
						pi_destroy(l_pi, l_nb_pocs);
						return false;
					}
				}
				else {
					l_nb_bytes_read = 0;
					if (!t2_skip_packet(p_t2,
						p_tile,
						l_tcp, l_current_pi,
						src_buf,
						&l_nb_bytes_read,
						p_manager)) {
						pi_destroy(l_pi, l_nb_pocs);
						return false;
					}
				}

				if (!skip_layer_or_res)
					l_img_comp->resno_decoded = std::max<uint32_t>(l_current_pi->resno, l_img_comp->resno_decoded);
				*p_data_read += l_nb_bytes_read;
			}
			++l_current_pi;
		}
		pi_destroy(l_pi, l_nb_pocs);
		return true;
	}

	/* ----------------------------------------------------------------------- */

	/**
	 * Creates a Tier 2 handle
	 *
	 * @param       p_image         Source or destination image
	 * @param       p_cp            Image coding parameters.
	 * @return              a new T2 handle if successful, nullptr otherwise.
	*/
	t2_t* t2_create(opj_image_t *p_image, cp_t *p_cp)
	{
		/* create the t2 structure */
		t2_t *l_t2 = (t2_t*)grok_calloc(1, sizeof(t2_t));
		if (!l_t2) {
			return nullptr;
		}
		l_t2->image = p_image;
		l_t2->cp = p_cp;
		return l_t2;
	}

	void t2_destroy(t2_t *t2)
	{
		if (t2) {
			grok_free(t2);
		}
	}

	static bool t2_decode_packet(t2_t* p_t2,
		tcd_resolution_t* l_res,
		tcp_t *p_tcp,
		pi_iterator_t *p_pi,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager)
	{
		bool l_read_data;
		uint64_t l_nb_bytes_read = 0;
		uint64_t l_nb_total_bytes_read = 0;
		*p_data_read = 0;

		if (!t2_read_packet_header(p_t2,
			l_res,
			p_tcp,
			p_pi,
			&l_read_data,
			src_buf,
			&l_nb_bytes_read,
			p_manager)) {
			return false;
		}
		l_nb_total_bytes_read += l_nb_bytes_read;

		/* we should read data for the packet */
		if (l_read_data) {
			l_nb_bytes_read = 0;
			if (!t2_read_packet_data(l_res,
				p_pi,
				src_buf,
				&l_nb_bytes_read,
				p_manager)) {
				return false;
			}
			l_nb_total_bytes_read += l_nb_bytes_read;
		}
		*p_data_read = l_nb_total_bytes_read;
		return true;
	}

	static bool t2_read_packet_header(t2_t* p_t2,
		tcd_resolution_t* l_res,
		tcp_t *p_tcp,
		pi_iterator_t *p_pi,
		bool * p_is_data_present,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager)

	{
		uint8_t *p_src_data = src_buf->get_global_ptr();
		uint64_t p_max_length = src_buf->data_len - src_buf->get_global_offset();
		uint32_t l_nb_code_blocks = 0;
		uint8_t *active_src = p_src_data;
		cp_t *l_cp = p_t2->cp;

		if (p_pi->layno == 0) {
			/* reset tagtrees */
			for (uint32_t bandno = 0; bandno < l_res->numbands; ++bandno) {
				auto l_band = l_res->bands + bandno;
				if (l_band->isEmpty())
					continue;
				tcd_precinct_t *l_prc = &l_band->precincts[p_pi->precno];
				if (!(p_pi->precno < (l_band->numPrecincts))) {
					event_msg(p_manager, EVT_ERROR, "Invalid precinct\n");
					return false;
				}
				if (l_prc->incltree)
					l_prc->incltree->reset();
				if (l_prc->imsbtree)
					l_prc->imsbtree->reset();
				l_nb_code_blocks = l_prc->cw * l_prc->ch;
				for (uint32_t cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
					auto l_cblk = l_prc->cblks.dec + cblkno;
					l_cblk->numSegments = 0;
				}
			}
		}

		/* SOP markers */
		if (p_tcp->csty & J2K_CP_CSTY_SOP) {
			if (p_max_length < 6) {
				event_msg(p_manager, EVT_WARNING, "Not enough space for expected SOP marker\n");
			}
			else if ((*active_src) != 0xff || (*(active_src + 1) != 0x91)) {
				event_msg(p_manager, EVT_WARNING, "Expected SOP marker\n");
			}
			else {
				active_src += 6;
			}
			/** TODO : check the Nsop value */
		}

		/*
		When the marker PPT/PPM is used the packet header are store in PPT/PPM marker
		This part deal with this characteristic
		step 1: Read packet header in the saved structure
		step 2: Return to codestream for decoding
		*/
		uint8_t *l_header_data = nullptr;
		uint8_t **l_header_data_start = nullptr;
		size_t * l_modified_length_ptr = nullptr;
		size_t l_remaining_length = 0;
		if (l_cp->ppm == 1) { /* PPM */
			l_header_data_start = &l_cp->ppm_data;
			l_header_data = *l_header_data_start;
			l_modified_length_ptr = &(l_cp->ppm_len);

		}
		else if (p_tcp->ppt == 1) { /* PPT */
			l_header_data_start = &(p_tcp->ppt_data);
			l_header_data = *l_header_data_start;
			l_modified_length_ptr = &(p_tcp->ppt_len);
		}
		else { /* Normal Case */
			l_header_data_start = &(active_src);
			l_header_data = *l_header_data_start;
			l_remaining_length = (size_t)(p_src_data + p_max_length - l_header_data);
			l_modified_length_ptr = &(l_remaining_length);
		}

		uint32_t l_present = 0;
		std::unique_ptr<BitIO> l_bio(new BitIO(l_header_data, *l_modified_length_ptr,false));
		if (*l_modified_length_ptr) {
			if (!l_bio->read(&l_present, 1)) {
				event_msg(p_manager, EVT_ERROR, "t2_read_packet_header: failed to read `present` bit \n");
				return false;
			}
		}
		JAS_FPRINTF(stderr, "present=%d \n", l_present);
		if (!l_present) {
			if (!l_bio->inalign())
				return false;
			l_header_data += l_bio->numbytes();

			/* EPH markers */
			if (p_tcp->csty & J2K_CP_CSTY_EPH) {
				if ((*l_modified_length_ptr - (size_t)(l_header_data - *l_header_data_start)) < 2U) {
					event_msg(p_manager, EVT_WARNING, "Not enough space for expected EPH marker\n");
				}
				else if ((*l_header_data) != 0xff || (*(l_header_data + 1) != 0x92)) {
					event_msg(p_manager, EVT_WARNING, "Expected EPH marker\n");
				}
				else {
					l_header_data += 2;
				}
			}

			auto l_header_length = (size_t)(l_header_data - *l_header_data_start);
			*l_modified_length_ptr -= l_header_length;
			*l_header_data_start += l_header_length;

			*p_is_data_present = false;
			*p_data_read = (size_t)(active_src - p_src_data);
			src_buf->incr_cur_seg_offset(*p_data_read);
			return true;
		}
		for (uint32_t bandno = 0; bandno < l_res->numbands; ++bandno) {
			auto l_band = l_res->bands + bandno;
			if (l_band->isEmpty()) {
				continue;
			}

			tcd_precinct_t *l_prc = l_band->precincts + p_pi->precno;
			l_nb_code_blocks = l_prc->cw * l_prc->ch;
			for (uint32_t cblkno = 0; cblkno < l_nb_code_blocks; cblkno++) {
				uint32_t l_included = 0, l_increment = 0;
				auto l_cblk = l_prc->cblks.dec + cblkno;

				/* if cblk not yet included before --> inclusion tagtree */
				if (!l_cblk->numSegments) {
					uint32_t value;
					if (!l_prc->incltree->decodeValue(l_bio.get(), cblkno, (int32_t)(p_pi->layno + 1), &value)) {
						event_msg(p_manager, EVT_ERROR, "t2_read_packet_header: failed to read `inclusion` bit \n");
						return false;
					}
					if (value != tag_tree_uninitialized_node_value && value != p_pi->layno) {
						event_msg(p_manager, EVT_WARNING, "Illegal inclusion tag tree found when decoding packet header\n");
					}
#ifdef DEBUG_LOSSLESS_T2
					l_cblk->included = value;
#endif
					l_included = (value <= p_pi->layno) ? 1 : 0;
					}
				/* else one bit */
				else {
					if (!l_bio->read(&l_included, 1)) {
						event_msg(p_manager, EVT_ERROR, "t2_read_packet_header: failed to read `inclusion` bit \n");
						return false;
					}
				
#ifdef DEBUG_LOSSLESS_T2
					l_cblk->included = l_included;
#endif
				}


				/* if cblk not included */
				if (!l_included) {
					l_cblk->numPassesInPacket = 0;
					JAS_FPRINTF(stderr, "included=%d \n", l_included);
					continue;
				}

				/* if cblk not yet included --> zero-bitplane tagtree */
				if (!l_cblk->numSegments) {
					uint32_t i = 0;
					uint8_t value;
					bool rc = true;
					while ( (rc = l_prc->imsbtree->decode(l_bio.get(), cblkno, (int32_t)i, &value)) && !value) {
						++i;
					}
					if (!rc) {
						event_msg(p_manager, EVT_ERROR, "Failed to decode zero-bitplane tag tree \n");
						return false;
					}

					l_cblk->numbps = l_band->numbps + 1 - i;
					// BIBO analysis gives upper limit on number of bit planes
					if (l_cblk->numbps > max_precision_jpeg_2000 + OPJ_J2K_MAXRLVLS * 5) {
						event_msg(p_manager, EVT_WARNING, "Number of bit planes %u is impossibly large.\n", l_cblk->numbps);
					}
					l_cblk->numlenbits = 3;
				}

				/* number of coding passes */
				if (!t2_getnumpasses(l_bio.get(), &l_cblk->numPassesInPacket)) {
					event_msg(p_manager, EVT_ERROR, "t2_read_packet_header: failed to read numpasses.\n");
					return false;
				}
				if (!t2_getcommacode(l_bio.get(), &l_increment)) {
					event_msg(p_manager, EVT_ERROR, "t2_read_packet_header: failed to read length indicator increment.\n");
					return false;
				 }

				/* length indicator increment */
				l_cblk->numlenbits += l_increment;
				uint32_t l_segno = 0;

				if (!l_cblk->numSegments) {
					if (!t2_init_seg(l_cblk, l_segno, p_tcp->tccps[p_pi->compno].cblksty, true)) {
						return false;
					}
				}
				else {
					l_segno = l_cblk->numSegments - 1;
					if (l_cblk->segs[l_segno].numpasses == l_cblk->segs[l_segno].maxpasses) {
						++l_segno;
						if (!t2_init_seg(l_cblk, l_segno, p_tcp->tccps[p_pi->compno].cblksty, false)) {
							return false;
						}
					}
				}
				auto numPassesInPacket = (int32_t)l_cblk->numPassesInPacket;
				do {
					auto l_seg = l_cblk->segs + l_segno;
					l_seg->numPassesInPacket = (uint32_t)std::min<int32_t>((int32_t)(l_seg->maxpasses - l_seg->numpasses), numPassesInPacket);
					uint32_t bits_to_read = l_cblk->numlenbits + uint_floorlog2(l_seg->numPassesInPacket);
					if (bits_to_read > 32) {
						event_msg(p_manager, EVT_ERROR, "t2_read_packet_header: too many bits in segment length \n");
						return false;
					}
					if (!l_bio->read(&l_seg->newlen, l_cblk->numlenbits + uint_floorlog2(l_seg->numPassesInPacket))) {
						event_msg(p_manager, EVT_WARNING, "t2_read_packet_header: failed to read segment length \n");
					}
#ifdef DEBUG_LOSSLESS_T2
					l_cblk->packet_length_info->push_back(packet_length_info_t(l_seg->newlen, 
																			l_cblk->numlenbits + uint_floorlog2(l_seg->numPassesInPacket)));
#endif
					JAS_FPRINTF(stderr, "included=%d numPassesInPacket=%d increment=%d len=%d \n", l_included, l_seg->numPassesInPacket, l_increment, l_seg->newlen);
					numPassesInPacket -= (int32_t)l_cblk->segs[l_segno].numPassesInPacket;
					if (numPassesInPacket > 0) {
						++l_segno;
						if (!t2_init_seg(l_cblk, l_segno, p_tcp->tccps[p_pi->compno].cblksty, false)) {
							return false;
						}
					}
				} while (numPassesInPacket > 0);
			}
		}

		if (!l_bio->inalign()) {
			event_msg(p_manager, EVT_ERROR, "Unable to read packet header\n");
			return false;
		}

		l_header_data += l_bio->numbytes();

		/* EPH markers */
		if (p_tcp->csty & J2K_CP_CSTY_EPH) {
			if ((*l_modified_length_ptr - (uint32_t)(l_header_data - *l_header_data_start)) < 2U) {
				event_msg(p_manager, EVT_WARNING, "Not enough space for expected EPH marker\n");
			}
			else if ((*l_header_data) != 0xff || (*(l_header_data + 1) != 0x92)) {
				event_msg(p_manager, EVT_WARNING, "Expected EPH marker\n");
			}
			else {
				l_header_data += 2;
			}
		}

		auto l_header_length = (size_t)(l_header_data - *l_header_data_start);
		JAS_FPRINTF(stderr, "hdrlen=%d \n", l_header_length);
		JAS_FPRINTF(stderr, "packet body\n");
		*l_modified_length_ptr -= l_header_length;
		*l_header_data_start += l_header_length;

		*p_is_data_present = true;
		*p_data_read = (uint32_t)(active_src - p_src_data);
		src_buf->incr_cur_seg_offset(*p_data_read);

		return true;
	}

	static bool t2_read_packet_data(tcd_resolution_t* l_res,
		pi_iterator_t *p_pi,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t* p_manager)
	{
		uint32_t bandno, cblkno;
		uint32_t l_nb_code_blocks;
		tcd_band_t *l_band = nullptr;
		tcd_cblk_dec_t* l_cblk = nullptr;
		l_band = l_res->bands;
		for (bandno = 0; bandno < l_res->numbands; ++bandno) {
			tcd_precinct_t *l_prc = &l_band->precincts[p_pi->precno];
			l_nb_code_blocks = l_prc->cw * l_prc->ch;
			l_cblk = l_prc->cblks.dec;

			for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
				tcd_seg_t *l_seg = nullptr;

				if (!l_cblk->numPassesInPacket) {
					++l_cblk;
					continue;
				}

				if (!l_cblk->numSegments) {
					l_seg = l_cblk->segs;
					++l_cblk->numSegments;
					l_cblk->dataSize = 0;
				}
				else {
					l_seg = &l_cblk->segs[l_cblk->numSegments - 1];
					if (l_seg->numpasses == l_seg->maxpasses) {
						++l_seg;
						++l_cblk->numSegments;
					}
				}

				uint32_t numPassesInPacket = l_cblk->numPassesInPacket;
				do {
					size_t offset = (size_t)src_buf->get_global_offset();
					size_t len = src_buf->data_len;
					// Check possible overflow on segment length
					if (((offset + l_seg->newlen) > len)) {
						event_msg(p_manager, EVT_ERROR, "read packet data: segment offset (%u) plus segment length %u is greater than total length \nof all segments (%u) for codeblock %d (layer=%d, prec=%d, band=%d, res=%d, comp=%d)\n",
							offset,
							l_seg->newlen,
							len,
							cblkno,
							p_pi->layno,
							p_pi->precno,
							bandno,
							p_pi->resno,
							p_pi->compno);
						return false;
					}
					//initialize dataindex to current contiguous size of code block
					if (l_seg->numpasses == 0) {
						l_seg->dataindex = l_cblk->dataSize;
					}
					// only add segment to seg_buffers if length is greater than zero
					if (l_seg->newlen) {
						l_cblk->seg_buffers.push_back(src_buf->get_global_ptr(), (uint16_t)l_seg->newlen);
						*(p_data_read) += l_seg->newlen;
						src_buf->incr_cur_seg_offset(l_seg->newlen);
						l_cblk->dataSize += l_seg->newlen;
						l_seg->len += l_seg->newlen;
					}
					l_seg->numpasses += l_seg->numPassesInPacket;
					numPassesInPacket -= l_seg->numPassesInPacket;
					if (numPassesInPacket > 0) {
						++l_seg;
						++l_cblk->numSegments;
					}
				} while (numPassesInPacket > 0);
				++l_cblk;
			} /* next code_block */

			++l_band;
		}
		return true;
	}
	//--------------------------------------------------------------------------------------------------

	static bool t2_encode_packet(uint32_t tileno,
								tcd_tile_t * tile,
								tcp_t * tcp,
								pi_iterator_t *pi,
								GrokStream* p_stream,
								uint64_t * p_data_written,
								uint64_t num_bytes_available,
								opj_codestream_info_t *cstr_info,
								event_mgr_t * p_manager)
	{
		uint32_t compno = pi->compno;
		uint32_t resno = pi->resno;
		uint32_t precno = pi->precno;
		uint32_t layno = pi->layno;
		tcd_tilecomp_t *tilec = &tile->comps[compno];
		tcd_resolution_t *res = &tilec->resolutions[resno];
		uint64_t numHeaderBytes = 0;
		size_t streamBytes = 0;
		if (p_stream)
			streamBytes = p_stream->tell();
		// SOP marker
		if (tcp->csty & J2K_CP_CSTY_SOP) {
			if (!p_stream->write_byte(255, p_manager)) {
				return false;
			}
			if (!p_stream->write_byte(145, p_manager)) {
				return false;
			}
			if (!p_stream->write_byte(0, p_manager)) {
				return false;
			}
			if (!p_stream->write_byte(4, p_manager)) {
				return false;
			}
			/* packno is uint32_t, in big endian format */
			if (!p_stream->write_byte((tile->packno >> 8) & 0xff, p_manager)) {
				return false;
			}
			if (!p_stream->write_byte(tile->packno & 0xff, p_manager)) {
				return false;
			}
			num_bytes_available -= 6;
			numHeaderBytes += 6;
		}

		// initialize precinct and code blocks if this is the first layer
		if (!layno) {
			auto band = res->bands;
			for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
				auto prc = band->precincts + precno;
				uint32_t l_nb_blocks = prc->cw * prc->ch;

				if (band->isEmpty() || !l_nb_blocks) {
					band++;
					continue;
				}
				if (prc->incltree)
					prc->incltree->reset();
				if (prc->imsbtree)
					prc->imsbtree->reset();
				for (uint32_t cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
					auto cblk = prc->cblks.enc + cblkno;
					cblk->num_passes_included_in_current_layer = 0;
					prc->imsbtree->setvalue(cblkno, band->numbps - (int32_t)cblk->numbps);
				}
				++band;
			}
		}

		std::unique_ptr<BitIO> bio(new BitIO(p_stream, true));
		// Empty header bit. Grok always sets this to 1,
		// even though there is also an option to set it to zero.
		if (!bio->write(1, 1))
			return false;

		/* Writing Packet header */
		auto band = res->bands;
		for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
			auto prc = band->precincts + precno;
			uint64_t l_nb_blocks = prc->cw * prc->ch;

			if (band->isEmpty() || !l_nb_blocks) {
				band++;
				continue;
			}

			auto cblk = prc->cblks.enc;
			for (uint32_t cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
				auto layer = cblk->layers + layno;
				if (!cblk->num_passes_included_in_current_layer && layer->numpasses) {
					prc->incltree->setvalue(cblkno, (int32_t)layno);
				}
				++cblk;
			}

			cblk = prc->cblks.enc;
			for (uint32_t cblkno = 0; cblkno < l_nb_blocks; cblkno++) {
				auto layer = cblk->layers + layno;
				uint32_t increment = 0;
				uint32_t nump = 0;
				uint32_t len = 0;

				/* cblk inclusion bits */
				if (!cblk->num_passes_included_in_current_layer) {
					prc->incltree->encode(bio.get(), cblkno, (int32_t)(layno + 1));
#ifdef DEBUG_LOSSLESS_T2
					cblk->included = layno;
#endif
				}
				else {
#ifdef DEBUG_LOSSLESS_T2
					cblk->included = layer->numpasses != 0 ? 1 : 0;
#endif
					if (!bio->write(layer->numpasses != 0, 1))
						return false;
				}

				/* if cblk not included, go to next cblk  */
				if (!layer->numpasses) {
					++cblk;
					continue;
				}

				/* if first instance of cblk --> zero bit-planes information */
				if (!cblk->num_passes_included_in_current_layer) {
					cblk->numlenbits = 3;
					prc->imsbtree->encode(bio.get(), cblkno, tag_tree_uninitialized_node_value);
				}
				/* number of coding passes included */
				t2_putnumpasses(bio.get(), layer->numpasses);
				uint32_t l_nb_passes = cblk->num_passes_included_in_current_layer + layer->numpasses;
				auto pass = cblk->passes + cblk->num_passes_included_in_current_layer;

				/* computation of the increase of the length indicator and insertion in the header     */
				for (uint32_t passno = cblk->num_passes_included_in_current_layer; passno < l_nb_passes; ++passno) {
					++nump;
					len += pass->len;

					if (pass->term || passno == l_nb_passes - 1) {
						increment = (uint32_t)std::max<int32_t>((int32_t)increment, int_floorlog2((int32_t)len) + 1
							- ((int32_t)cblk->numlenbits + int_floorlog2((int32_t)nump)));
						len = 0;
						nump = 0;
					}
					++pass;
				}
				t2_putcommacode(bio.get(), (int32_t)increment);

				/* computation of the new Length indicator */
				cblk->numlenbits += increment;

				pass = cblk->passes + cblk->num_passes_included_in_current_layer;
				/* insertion of the codeword segment length */
				for (uint32_t passno = cblk->num_passes_included_in_current_layer; passno < l_nb_passes; ++passno) {
					nump++;
					len += pass->len;

					if (pass->term || passno == l_nb_passes - 1) {
#ifdef DEBUG_LOSSLESS_T2
						cblk->packet_length_info->push_back(packet_length_info_t(len, cblk->numlenbits + (uint32_t)int_floorlog2((int32_t)nump)));
#endif
						if (!bio->write(len, cblk->numlenbits + (uint32_t)int_floorlog2((int32_t)nump)))
							return false;
						len = 0;
						nump = 0;
					}
					++pass;
				}
				++cblk;
			}
			++band;
		}

		if (!bio->flush()) {
			event_msg(p_manager, EVT_ERROR, "t2_encode_packet: Bit IO flush failed while encoding packet\n");
			return false;
		}

		auto temp = bio->numbytes();
		num_bytes_available -= (uint64_t)temp;
		numHeaderBytes += (uint64_t)temp;

		// EPH marker
		if (tcp->csty & J2K_CP_CSTY_EPH) {
			if (!p_stream->write_byte(255, p_manager)) {
				return false;
			}
			if (!p_stream->write_byte(146, p_manager)) {
				return false;
			}
			num_bytes_available -= 2;
			numHeaderBytes += 2;
		}


		/* << INDEX */
		/* End of packet header position. Currently only represents the distance to start of packet
		   Will be updated later by incrementing with packet start value*/
		//if (cstr_info && cstr_info->index_write) {
		//	opj_packet_info_t *info_PK = &cstr_info->tile[tileno].packet[cstr_info->packno];
		//	info_PK->end_ph_pos = (int64_t)(active_dest - dest);
		//}
		/* INDEX >> */

		/* Writing the packet body */
		band = res->bands;
		for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
			auto prc = band->precincts + precno;
			uint64_t l_nb_blocks = prc->cw * prc->ch;

			if (band->isEmpty() || !l_nb_blocks) {
				band++;
				continue;
			}

			auto cblk = prc->cblks.enc;

			for (uint32_t cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
				auto cblk_layer = cblk->layers + layno;
				if (!cblk_layer->numpasses) {
					++cblk;
					continue;
				}

				if (cblk_layer->len > num_bytes_available) {
					event_msg(p_manager, EVT_ERROR, 
	"Code block layer size %d exceeds number of available bytes %d in tile buffer\n", 
								cblk_layer->len,num_bytes_available);
					return false;
				}

				if (cblk_layer->len) {
					if (!p_stream->write_bytes(cblk_layer->data, cblk_layer->len, p_manager)) {
						return false;
					}
					num_bytes_available -= cblk_layer->len;
				}
				cblk->num_passes_included_in_current_layer += cblk_layer->numpasses;
				if (cstr_info && cstr_info->index_write) {
					opj_packet_info_t *info_PK = &cstr_info->tile[tileno].packet[cstr_info->packno];
					info_PK->disto += cblk_layer->disto;
					if (cstr_info->D_max < info_PK->disto) {
						cstr_info->D_max = info_PK->disto;
					}
				}
				++cblk;
			}
			++band;
		}
		*p_data_written += p_stream->tell() - streamBytes;

#ifdef DEBUG_LOSSLESS_T2
		auto originalDataBytes = *p_data_written - numHeaderBytes;
		auto roundRes = &tilec->round_trip_resolutions[resno];
		size_t l_nb_bytes_read = 0;
		auto src_buf = std::unique_ptr<seg_buf_t>(new seg_buf_t());
		seg_buf_push_back(src_buf.get(), dest, *p_data_written);

		bool rc = true;
		bool l_read_data;
		if (!t2_read_packet_header(p_t2,
			roundRes,
			tcp,
			pi,
			&l_read_data,
			src_buf.get(),
			&l_nb_bytes_read,
			p_manager)) {
			rc = false;
		}
		if (rc) {

			// compare size of header
			if (numHeaderBytes != l_nb_bytes_read) {
				printf("t2_encode_packet: round trip header bytes %u differs from original %u\n", (uint32_t)l_nb_bytes_read, (uint32_t)numHeaderBytes);
			}
			for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
				auto band = res->bands + bandno;
				auto roundTripBand = roundRes->bands + bandno;
				if (!band->precincts)
					continue;
				for (size_t precno = 0; precno < band->numPrecincts; ++precno) {
					auto prec = band->precincts + precno;
					auto roundTripPrec = roundTripBand->precincts + precno;
					for (uint32_t cblkno = 0; cblkno < prec->cw * prec->ch; ++cblkno) {
						auto originalCblk = prec->cblks.enc + cblkno;
						tcd_layer_t *layer = originalCblk->layers + layno;
						if (!layer->numpasses)
							continue;

						// compare number of passes
						auto roundTripCblk = roundTripPrec->cblks.dec + cblkno;
						if (roundTripCblk->numPassesInPacket != layer->numpasses) {
							printf("t2_encode_packet: round trip layer numpasses %d differs from original num passes %d at layer %d, component %d, band %d, precinct %d, resolution %d\n",
								roundTripCblk->numPassesInPacket,
								layer->numpasses,
								layno,
								compno,
								bandno,
								(uint32_t)precno,
								pi->resno);

						}
						// compare number of bit planes
						if (roundTripCblk->numbps != originalCblk->numbps) {
							printf("t2_encode_packet: round trip numbps %d differs from original %d\n", roundTripCblk->numbps, originalCblk->numbps);
						}

						// compare number of length bits
						if (roundTripCblk->numlenbits != originalCblk->numlenbits) {
							printf("t2_encode_packet: round trip numlenbits %u differs from original %u\n", roundTripCblk->numlenbits, originalCblk->numlenbits);
						}

						// compare inclusion
						if (roundTripCblk->included != originalCblk->included) {
							printf("t2_encode_packet: round trip inclusion %d differs from original inclusion %d at layer %d, component %d, band %d, precinct %d, resolution %d\n",
								roundTripCblk->included,
								originalCblk->included,
								layno,
								compno,
								bandno,
								(uint32_t)precno,
								pi->resno);
						}

						// compare lengths
						if (roundTripCblk->packet_length_info->size() != originalCblk->packet_length_info->size()) {
							printf("t2_encode_packet: round trip length size %d differs from original %d at layer %d, component %d, band %d, precinct %d, resolution %d\n",
								(uint32_t)roundTripCblk->packet_length_info->size(),
								(uint32_t)originalCblk->packet_length_info->size(),
								layno,
								compno,
								bandno,
								(uint32_t)precno,
								pi->resno);
						} else {
							for (uint32_t i = 0; i < roundTripCblk->packet_length_info->size(); ++i) {
								auto roundTrip = roundTripCblk->packet_length_info->operator[](i);
								auto original = originalCblk->packet_length_info->operator[](i);
								if (!(roundTrip ==original)) {
									printf("t2_encode_packet: round trip length size %d differs from original %d at layer %d, component %d, band %d, precinct %d, resolution %d\n",
										roundTrip.len,
										original.len,
										layno,
										compno,
										bandno,
										(uint32_t)precno,
										pi->resno);
								}
							}

						
						}
					}
				}
			}
			/* we should read data for the packet */
			if (l_read_data) {
				l_nb_bytes_read = 0;
				if (!t2_read_packet_data(roundRes,
					pi,
					src_buf.get(),
					&l_nb_bytes_read,
					p_manager)) {
					rc = false;
				}
				else {
					if (originalDataBytes != l_nb_bytes_read) {
						printf("t2_encode_packet: round trip data bytes %u differs from original %u\n", (uint32_t)l_nb_bytes_read, (uint32_t)originalDataBytes);
					}

					for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
						auto band = res->bands + bandno;
						auto roundTripBand = roundRes->bands + bandno;
						if (!band->precincts)
							continue;
						for (size_t precno = 0; precno < band->numPrecincts; ++precno) {
							auto prec = band->precincts + precno;
							auto roundTripPrec = roundTripBand->precincts + precno;
							for (uint32_t cblkno = 0; cblkno < prec->cw * prec->ch; ++cblkno) {
								auto originalCblk = prec->cblks.enc + cblkno;
								tcd_layer_t *layer = originalCblk->layers + layno;
								if (!layer->numpasses)
									continue;

								// compare cumulative length
								uint32_t originalCumulativeLayerLength = 0;
								for (uint32_t i = 0; i <= layno; ++i) {
									auto lay = originalCblk->layers + i;
									if (lay->numpasses)
										originalCumulativeLayerLength += lay->len;
								}
								auto roundTripCblk = roundTripPrec->cblks.dec + cblkno;
								uint16_t roundTripTotalSegLen = min_buf_vec_get_len(&roundTripCblk->seg_buffers);
								if (roundTripTotalSegLen != originalCumulativeLayerLength) {
									printf("t2_encode_packet: layer %d: round trip segment length %d differs from original %d\n", layno, roundTripTotalSegLen, originalCumulativeLayerLength);
								}

								// compare individual data points
								if (roundTripCblk->numSegments && roundTripTotalSegLen) {
									uint8_t* roundTripData = nullptr;
									bool needs_delete = false;
									/* if there is only one segment, then it is already contiguous, so no need to make a copy*/
									if (roundTripTotalSegLen == 1 && roundTripCblk->seg_buffers.get(0)) {
										roundTripData = ((buf_t*)(roundTripCblk->seg_buffers.get(0)))->buf;
									}
									else {
										needs_delete = true;
										roundTripData = new uint8_t[roundTripTotalSegLen];
										min_buf_vec_copy_to_contiguous_buffer(&roundTripCblk->seg_buffers, roundTripData);
									}
									for (uint32_t i = 0; i < originalCumulativeLayerLength; ++i) {
										if (roundTripData[i] != originalCblk->data[i]) {
											printf("t2_encode_packet: layer %d: round trip data %x differs from original %x\n", layno, roundTripData[i], originalCblk->data[i]);
										}
									}
									if (needs_delete)
										delete[] roundTripData;
								}

							}
						}
					}
				}
			}
		}
		else {
			printf("t2_encode_packet: decode packet failed\n");
		}
#endif
		return true;
	}


	static bool t2_encode_packet_simulate(tcd_tile_t * tile,
		tcp_t * tcp,
		pi_iterator_t *pi,
		uint64_t * p_data_written,
		uint64_t length)
	{
		uint32_t bandno, cblkno;
		uint64_t l_nb_bytes;
		uint32_t compno = pi->compno;
		uint32_t resno = pi->resno;
		uint32_t precno = pi->precno;
		uint32_t layno = pi->layno;
		uint32_t l_nb_blocks;
		tcd_band_t *band = nullptr;
		tcd_cblk_enc_t* cblk = nullptr;
		tcd_pass_t *pass = nullptr;

		tcd_tilecomp_t *tilec = tile->comps + compno;
		tcd_resolution_t *res = tilec->resolutions + resno;
		uint64_t packet_bytes_written = 0;

		/* <SOP 0xff91> */
		if (tcp->csty & J2K_CP_CSTY_SOP) {
			length -= 6;
			packet_bytes_written += 6;
		}
		/* </SOP> */

		if (!layno) {
			band = res->bands;

			for (bandno = 0; bandno < res->numbands; ++bandno) {
				tcd_precinct_t *prc = band->precincts + precno;

				if (prc->incltree)
					prc->incltree->reset();
				if (prc->imsbtree)
					prc->imsbtree->reset();

				l_nb_blocks = prc->cw * prc->ch;
				for (cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
					cblk = prc->cblks.enc + cblkno;

					cblk->num_passes_included_in_current_layer = 0;
					prc->imsbtree->setvalue(cblkno, band->numbps - (int32_t)cblk->numbps);
				}
				++band;
			}
		}

		std::unique_ptr<BitIO> bio(new BitIO(0, length, true));
		bio->simulateOutput(true);
		/* Empty header bit */
		if (!bio->write(1, 1))
			return false;

		/* Writing Packet header */
		band = res->bands;
		for (bandno = 0; bandno < res->numbands; ++bandno) {
			tcd_precinct_t *prc = band->precincts + precno;

			l_nb_blocks = prc->cw * prc->ch;
			cblk = prc->cblks.enc;

			for (cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
				tcd_layer_t *layer = cblk->layers + layno;

				if (!cblk->num_passes_included_in_current_layer && layer->numpasses) {
					prc->incltree->setvalue(cblkno, (int32_t)layno);
				}

				++cblk;
			}

			cblk = prc->cblks.enc;
			for (cblkno = 0; cblkno < l_nb_blocks; cblkno++) {
				tcd_layer_t *layer = cblk->layers + layno;
				uint32_t increment = 0;
				uint32_t nump = 0;
				uint32_t len = 0, passno;
				uint32_t l_nb_passes;

				/* cblk inclusion bits */
				if (!cblk->num_passes_included_in_current_layer) {
					prc->incltree->encode(bio.get(), cblkno, (int32_t)(layno + 1));
				}
				else {
					if (!bio->write(layer->numpasses != 0, 1))
						return false;
				}

				/* if cblk not included, go to the next cblk  */
				if (!layer->numpasses) {
					++cblk;
					continue;
				}

				/* if first instance of cblk --> zero bit-planes information */
				if (!cblk->num_passes_included_in_current_layer) {
					cblk->numlenbits = 3;
					prc->imsbtree->encode(bio.get(), cblkno, tag_tree_uninitialized_node_value);
				}

				/* number of coding passes included */
				t2_putnumpasses(bio.get(), layer->numpasses);
				l_nb_passes = cblk->num_passes_included_in_current_layer + layer->numpasses;
				pass = cblk->passes + cblk->num_passes_included_in_current_layer;

				/* computation of the increase of the length indicator and insertion in the header     */
				for (passno = cblk->num_passes_included_in_current_layer; passno < l_nb_passes; ++passno) {
					++nump;
					len += pass->len;

					if (pass->term || passno == (cblk->num_passes_included_in_current_layer + layer->numpasses) - 1) {
						increment = (uint32_t)std::max<int32_t>((int32_t)increment, int_floorlog2((int32_t)len) + 1
							- ((int32_t)cblk->numlenbits + int_floorlog2((int32_t)nump)));
						len = 0;
						nump = 0;
					}

					++pass;
				}
				t2_putcommacode(bio.get(), (int32_t)increment);

				/* computation of the new Length indicator */
				cblk->numlenbits += increment;

				pass = cblk->passes + cblk->num_passes_included_in_current_layer;
				/* insertion of the codeword segment length */
				for (passno = cblk->num_passes_included_in_current_layer; passno < l_nb_passes; ++passno) {
					nump++;
					len += pass->len;

					if (pass->term || passno == (cblk->num_passes_included_in_current_layer + layer->numpasses) - 1) {
						if (!bio->write(len, cblk->numlenbits + (uint32_t)int_floorlog2((int32_t)nump)))
							return false;
						len = 0;
						nump = 0;
					}
					++pass;
				}

				++cblk;
			}

			++band;
		}

		if (!bio->flush()) {
			return false;
		}

		l_nb_bytes = (uint64_t)bio->numbytes();
		packet_bytes_written += l_nb_bytes;
		length -= l_nb_bytes;

		/* <EPH 0xff92> */
		if (tcp->csty & J2K_CP_CSTY_EPH) {
			length -= 2;
			packet_bytes_written += 2;
		}
		/* </EPH> */


		/* Writing the packet body */
		band = res->bands;
		for (bandno = 0; bandno < res->numbands; bandno++) {
			tcd_precinct_t *prc = band->precincts + precno;

			l_nb_blocks = prc->cw * prc->ch;
			cblk = prc->cblks.enc;

			for (cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
				tcd_layer_t *layer = cblk->layers + layno;

				if (!layer->numpasses) {
					++cblk;
					continue;
				}

				if (layer->len > length) {
					return false;
				}

				cblk->num_passes_included_in_current_layer += layer->numpasses;
				packet_bytes_written += layer->len;
				length -= layer->len;
				++cblk;
			}
			++band;
		}
		*p_data_written += packet_bytes_written;

		return true;
	}
	static bool t2_skip_packet(t2_t* p_t2,
		tcd_tile_t *p_tile,
		tcp_t *p_tcp,
		pi_iterator_t *p_pi,
		seg_buf_t* src_buf,
		uint64_t * p_data_read,
		event_mgr_t *p_manager)
	{
		bool l_read_data;
		uint64_t l_nb_bytes_read = 0;
		uint64_t l_nb_total_bytes_read = 0;
		uint64_t p_max_length = (uint64_t)src_buf->get_cur_seg_len();

		*p_data_read = 0;

		if (!t2_read_packet_header(p_t2,
			&p_tile->comps[p_pi->compno].resolutions[p_pi->resno],
			p_tcp,
			p_pi,
			&l_read_data,
			src_buf,
			&l_nb_bytes_read,
			p_manager)) {
			return false;
		}

		l_nb_total_bytes_read += l_nb_bytes_read;
		p_max_length -= l_nb_bytes_read;

		/* we should read data for the packet */
		if (l_read_data) {
			l_nb_bytes_read = 0;

			if (!t2_skip_packet_data(&p_tile->comps[p_pi->compno].resolutions[p_pi->resno],
				p_pi,
				&l_nb_bytes_read,
				p_max_length,
				p_manager)) {
				return false;
			}
			src_buf->incr_cur_seg_offset(l_nb_bytes_read);
			l_nb_total_bytes_read += l_nb_bytes_read;
		}
		*p_data_read = l_nb_total_bytes_read;


		return true;
	}


	static bool t2_skip_packet_data(tcd_resolution_t* l_res,
		pi_iterator_t *p_pi,
		uint64_t * p_data_read,
		uint64_t p_max_length,
		event_mgr_t *p_manager)
	{
		uint32_t bandno, cblkno;
		uint32_t l_nb_code_blocks;
		tcd_cblk_dec_t* l_cblk = nullptr;

		*p_data_read = 0;
		for (bandno = 0; bandno < l_res->numbands; ++bandno) {
			auto l_band = l_res->bands + bandno;
			tcd_precinct_t *l_prc = &l_band->precincts[p_pi->precno];

			if (l_band->isEmpty()) {
				continue;
			}

			l_nb_code_blocks = l_prc->cw * l_prc->ch;
			l_cblk = l_prc->cblks.dec;

			for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
				tcd_seg_t *l_seg = nullptr;

				if (!l_cblk->numPassesInPacket) {
					/* nothing to do */
					++l_cblk;
					continue;
				}

				if (!l_cblk->numSegments) {
					l_seg = l_cblk->segs;
					++l_cblk->numSegments;
					l_cblk->dataSize = 0;
				}
				else {
					l_seg = &l_cblk->segs[l_cblk->numSegments - 1];

					if (l_seg->numpasses == l_seg->maxpasses) {
						++l_seg;
						++l_cblk->numSegments;
					}
				}
				uint32_t numPassesInPacket = l_cblk->numPassesInPacket;
				do {
					/* Check possible overflow then size */
					if (((*p_data_read + l_seg->newlen) < (*p_data_read)) || ((*p_data_read + l_seg->newlen) > p_max_length)) {
						event_msg(p_manager, EVT_ERROR, "skip: segment too long (%d) with max (%d) for codeblock %d (p=%d, b=%d, r=%d, c=%d)\n",
							l_seg->newlen, p_max_length, cblkno, p_pi->precno, bandno, p_pi->resno, p_pi->compno);
						return false;
					}

					JAS_FPRINTF(stderr, "p_data_read (%d) newlen (%d) \n", *p_data_read, l_seg->newlen);
					*(p_data_read) += l_seg->newlen;

					l_seg->numpasses += l_seg->numPassesInPacket;
					numPassesInPacket -= l_seg->numPassesInPacket;
					if (numPassesInPacket > 0) {
						++l_seg;
						++l_cblk->numSegments;
					}
				} while (numPassesInPacket > 0);

				++l_cblk;
			}
		}
		return true;
	}


	static bool t2_init_seg(tcd_cblk_dec_t* cblk,
		uint32_t index,
		uint32_t cblksty,
		bool first)
	{
		tcd_seg_t* seg = nullptr;
		uint32_t l_nb_segs = index + 1;

		if (l_nb_segs > cblk->numSegmentsAllocated) {
			tcd_seg_t* new_segs;
			cblk->numSegmentsAllocated += default_numbers_segments;

			new_segs = (tcd_seg_t*)grok_realloc(cblk->segs, cblk->numSegmentsAllocated * sizeof(tcd_seg_t));
			if (!new_segs) {
				grok_free(cblk->segs);
				cblk->segs = nullptr;
				cblk->numSegmentsAllocated = 0;
				/* event_msg(p_manager, EVT_ERROR, "Not enough memory to initialize segment %d\n", l_nb_segs); */
				return false;
			}
			cblk->segs = new_segs;
		}

		seg = &cblk->segs[index];
		memset(seg, 0, sizeof(tcd_seg_t));

		if (cblksty & J2K_CCP_CBLKSTY_TERMALL) {
			seg->maxpasses = 1;
		}
		else if (cblksty & J2K_CCP_CBLKSTY_LAZY) {
			if (first) {
				seg->maxpasses = 10;
			}
			else {
				auto last_seg = seg - 1;
				seg->maxpasses = ((last_seg->maxpasses == 1) || (last_seg->maxpasses == 10)) ? 2 : 1;
			}
		}
		else {
			seg->maxpasses = 109;
		}

		return true;
	}

}
