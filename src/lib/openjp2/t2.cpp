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
static bool t2_encode_packet(uint16_t tileno, grk_tcd_tile *tile, grk_tcp *tcp,
		grk_pi_iterator *pi, GrokStream *p_stream, uint64_t *p_data_written,
		uint64_t len,  grk_codestream_info  *cstr_info);

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
static bool t2_encode_packet_simulate(grk_tcd_tile *tile, grk_tcp *tcp,
		grk_pi_iterator *pi, uint64_t *p_data_written, uint64_t len);

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
static bool t2_decode_packet(t2_t *t2, grk_tcd_tile *p_tile, grk_tcp *tcp,
		grk_pi_iterator *pi, ChunkBuffer *src_buf, uint64_t *data_read);

static bool t2_skip_packet(t2_t *p_t2, grk_tcd_tile *p_tile, grk_tcp *p_tcp,
		grk_pi_iterator *p_pi, ChunkBuffer *src_buf, uint64_t *p_data_read);

static bool t2_read_packet_header(t2_t *p_t2, grk_tcd_tile *p_tile,
		grk_tcp *p_tcp, grk_pi_iterator *p_pi, bool *p_is_data_present,
		ChunkBuffer *src_buf, uint64_t *p_data_read);

static bool t2_read_packet_data(grk_tcd_resolution *l_res, grk_pi_iterator *p_pi,
		ChunkBuffer *src_buf, uint64_t *p_data_read);

static bool t2_skip_packet_data(grk_tcd_resolution *l_res, grk_pi_iterator *p_pi,
		uint64_t *p_data_read, uint64_t max_length);

/**
 @param cblk
 @param index
 @param cblk_sty
 @param first
 */
static bool t2_init_seg(grk_tcd_cblk_dec *cblk, uint32_t index,
		uint8_t cblk_sty, bool first);


bool t2_encode_packets(t2_t *p_t2, uint16_t tile_no, grk_tcd_tile *p_tile,
		uint32_t max_layers, GrokStream *p_stream, uint64_t *p_data_written,
		uint64_t max_len,  grk_codestream_info  *cstr_info, uint32_t tp_num,
		uint32_t tp_pos, uint32_t pino) {

	uint64_t l_nb_bytes = 0;
	auto l_image = p_t2->image;
	auto l_cp = p_t2->cp;
	auto l_tcp = &l_cp->tcps[tile_no];
	uint32_t l_nb_pocs = l_tcp->numpocs + 1;

	auto l_pi = pi_initialise_encode(l_image, l_cp, tile_no, FINAL_PASS);
	if (!l_pi) {
		return false;
	}
	pi_init_encode(l_pi, l_cp, tile_no, pino, tp_num, tp_pos,
			FINAL_PASS);

	auto l_current_pi = &l_pi[pino];
	if (l_current_pi->poc.prg == GRK_PROG_UNKNOWN) {
		pi_destroy(l_pi, l_nb_pocs);
		GROK_ERROR(
				"t2_encode_packets: Unknown progression order");
		return false;
	}
	while (pi_next(l_current_pi)) {
		if (l_current_pi->layno < max_layers) {
			l_nb_bytes = 0;

			if (!t2_encode_packet(tile_no, p_tile, l_tcp, l_current_pi,
					p_stream, &l_nb_bytes, max_len, cstr_info)) {
				pi_destroy(l_pi, l_nb_pocs);
				return false;
			}

			max_len -= l_nb_bytes;
			*p_data_written += l_nb_bytes;

			/* INDEX >> */
			if (cstr_info) {
				if (cstr_info->index_write) {
					 auto info_TL = &cstr_info->tile[tile_no];
					 auto info_PK =	&info_TL->packet[cstr_info->packno];
					if (!cstr_info->packno) {
						info_PK->start_pos = info_TL->end_header + 1;
					} else {
						info_PK->start_pos =
								((l_cp->m_specific_param.m_enc.m_tp_on
										| l_tcp->POC) && info_PK->start_pos) ?
										info_PK->start_pos :
										info_TL->packet[cstr_info->packno - 1].end_pos
												+ 1;
					}
					info_PK->end_pos = info_PK->start_pos + l_nb_bytes - 1;
					info_PK->end_ph_pos += info_PK->start_pos - 1; /* End of packet header which now only represents the distance
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

bool t2_encode_packets_simulate(t2_t *p_t2, uint16_t tile_no,
		grk_tcd_tile *p_tile, uint32_t max_layers, uint64_t *p_data_written,
		uint64_t max_len, uint32_t tp_pos) {

	auto l_image = p_t2->image;
	auto l_cp = p_t2->cp;
	auto l_tcp = l_cp->tcps + tile_no;
	uint32_t pocno = (l_cp->rsiz == GRK_PROFILE_CINEMA_4K) ? 2 : 1;
	uint32_t l_max_comp =
			l_cp->m_specific_param.m_enc.m_max_comp_size > 0 ?
					l_image->numcomps : 1;
	uint32_t l_nb_pocs = l_tcp->numpocs + 1;

	if (!p_data_written)
		return false;

	auto l_pi = pi_initialise_encode(l_image, l_cp, tile_no,THRESH_CALC);
	if (!l_pi) {
		return false;
	}
	*p_data_written = 0;
	auto l_current_pi = l_pi;

	for (uint32_t compno = 0; compno < l_max_comp; ++compno) {
		uint64_t l_comp_len = 0;
		l_current_pi = l_pi;

		for (uint32_t poc = 0; poc < pocno; ++poc) {
			uint32_t l_tp_num = compno;
			pi_init_encode(l_pi, l_cp, tile_no, poc, l_tp_num, tp_pos,
					THRESH_CALC);

			if (l_current_pi->poc.prg == GRK_PROG_UNKNOWN) {
				pi_destroy(l_pi, l_nb_pocs);
				GROK_ERROR(
						"t2_decode_packets_simulate: Unknown progression order");
				return false;
			}
			while (pi_next(l_current_pi)) {
				if (l_current_pi->layno < max_layers) {
					uint64_t bytesInPacket = 0;
					if (!t2_encode_packet_simulate(p_tile, l_tcp, l_current_pi,
							&bytesInPacket, max_len)) {
						pi_destroy(l_pi, l_nb_pocs);
						return false;
					}

					l_comp_len += bytesInPacket;
					max_len -= bytesInPacket;
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
bool t2_decode_packets(t2_t *p_t2, uint16_t tile_no, grk_tcd_tile *p_tile,
		ChunkBuffer *src_buf, uint64_t *p_data_read) {

	auto l_image = p_t2->image;
	auto l_cp = p_t2->cp;
	auto l_tcp = p_t2->cp->tcps + tile_no;
	uint32_t l_nb_pocs = l_tcp->numpocs + 1;
	auto l_pi = pi_create_decode(l_image, l_cp, tile_no);
	if (!l_pi)
		return false;

	auto l_current_pi = l_pi;
	for (uint32_t pino = 0; pino <= l_tcp->numpocs; ++pino) {

		/* if the resolution needed is too low, one dim of the tilec could be equal to zero
		 * and no packets are used to decode this resolution and
		 * l_current_pi->resno is always >= p_tile->comps[l_current_pi->compno].minimum_num_resolutions
		 * and no l_img_comp->resno_decoded are computed
		 */

		if (l_current_pi->poc.prg == GRK_PROG_UNKNOWN) {
			pi_destroy(l_pi, l_nb_pocs);
			GROK_ERROR(
					"t2_decode_packets: Unknown progression order");
			return false;
		}
		while (pi_next(l_current_pi)) {
			auto skip_precinct = false;
			auto tilec = p_tile->comps + l_current_pi->compno;
			auto skip_layer_or_res = l_current_pi->layno
					>= l_tcp->num_layers_to_decode
					|| l_current_pi->resno >= tilec->minimum_num_resolutions;

			auto l_img_comp = l_image->comps + l_current_pi->compno;
/*
			GROK_INFO(
					"packet offset=00000166 prg=%d cmptno=%02d rlvlno=%02d prcno=%03d lyrno=%02d\n\n",
					l_current_pi->poc.prg1, l_current_pi->compno,
					l_current_pi->resno, l_current_pi->precno,
					l_current_pi->layno);
*/
			if (!skip_layer_or_res) {
				auto res = tilec->resolutions + l_current_pi->resno;
				skip_precinct = true;
				for (uint32_t bandno = 0; bandno < res->numbands && skip_precinct;
						++bandno) {
					auto band = res->bands + bandno;
					auto num_precincts = band->numPrecincts;
					for (uint32_t precno = 0; precno < num_precincts && skip_precinct;
							++precno) {
						auto prec = band->precincts + precno;
						auto prec_rect = grk_rect(prec->x0, prec->y0, prec->x1,
								prec->y1);
						if (tilec->buf->hit_test( &prec_rect)) {
							skip_precinct = false;
							break;
						}
					}
				}
			}
			uint64_t l_nb_bytes_read = 0;
			if (!skip_layer_or_res && !skip_precinct) {
				if (!t2_decode_packet(p_t2,
						p_tile, l_tcp, l_current_pi, src_buf, &l_nb_bytes_read)) {
					pi_destroy(l_pi, l_nb_pocs);
					return false;
				}
			} else {
				if (!t2_skip_packet(p_t2, p_tile, l_tcp, l_current_pi, src_buf,
						&l_nb_bytes_read)) {
					pi_destroy(l_pi, l_nb_pocs);
					return false;
				}
			}

			if (!skip_layer_or_res)
				l_img_comp->resno_decoded = std::max<uint32_t>(
						l_current_pi->resno, l_img_comp->resno_decoded);
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
t2_t* t2_create(grk_image *p_image, grk_coding_parameters *p_cp) {
	/* create the t2 structure */
	auto l_t2 = (t2_t*) grok_calloc(1, sizeof(t2_t));
	if (!l_t2) {
		return nullptr;
	}
	l_t2->image = p_image;
	l_t2->cp = p_cp;
	return l_t2;
}

void t2_destroy(t2_t *t2) {
	if (t2) {
		grok_free(t2);
	}
}

static bool t2_decode_packet(t2_t *p_t2, grk_tcd_tile *p_tile, grk_tcp *p_tcp,
		grk_pi_iterator *p_pi, ChunkBuffer *src_buf, uint64_t *p_data_read) {
    auto l_res = &p_tile->comps[p_pi->compno].resolutions[p_pi->resno];
	bool l_read_data;
	uint64_t l_nb_bytes_read = 0;
	uint64_t l_nb_total_bytes_read = 0;
	*p_data_read = 0;

	if (!t2_read_packet_header(p_t2, p_tile, p_tcp, p_pi, &l_read_data, src_buf,
			&l_nb_bytes_read)) {
		return false;
	}
	l_nb_total_bytes_read += l_nb_bytes_read;

	/* we should read data for the packet */
	if (l_read_data) {
		l_nb_bytes_read = 0;
		if (!t2_read_packet_data(l_res, p_pi, src_buf, &l_nb_bytes_read)) {
			return false;
		}
		l_nb_total_bytes_read += l_nb_bytes_read;
	}
	*p_data_read = l_nb_total_bytes_read;
	return true;
}

static bool t2_read_packet_header(t2_t *p_t2,  grk_tcd_tile *p_tile,
		grk_tcp *p_tcp, grk_pi_iterator *p_pi, bool *p_is_data_present,
		ChunkBuffer *src_buf, uint64_t *p_data_read)		{

	auto l_res = &p_tile->comps[p_pi->compno].resolutions[p_pi->resno];
	auto p_src_data = src_buf->get_global_ptr();
	uint64_t max_length = src_buf->data_len - src_buf->get_global_offset();
	uint64_t l_nb_code_blocks = 0;
	auto active_src = p_src_data;
	auto l_cp = p_t2->cp;

	if (p_pi->layno == 0) {
		/* reset tagtrees */
		for (uint32_t bandno = 0; bandno < l_res->numbands; ++bandno) {
			auto l_band = l_res->bands + bandno;
			if (l_band->isEmpty())
				continue;
			auto l_prc = &l_band->precincts[p_pi->precno];
			if (!(p_pi->precno < (l_band->numPrecincts))) {
				GROK_ERROR( "Invalid precinct");
				return false;
			}
			if (l_prc->incltree)
				l_prc->incltree->reset();
			if (l_prc->imsbtree)
				l_prc->imsbtree->reset();
			l_nb_code_blocks = (uint64_t) l_prc->cw * l_prc->ch;
			for (uint64_t cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
				auto l_cblk = l_prc->cblks.dec + cblkno;
				l_cblk->numSegments = 0;
			}
		}
	}

	/* SOP markers */
	if (p_tcp->csty & J2K_CP_CSTY_SOP) {
		if (max_length < 6) {
			GROK_WARN(
					"Not enough space for expected SOP marker");
		} else if ((*active_src) != 0xff || (*(active_src + 1) != 0x91)) {
			GROK_WARN( "Expected SOP marker");
		} else {
		  uint16_t packno = (uint16_t)(((uint16_t)active_src[4] << 8) | active_src[5]);
		  if (packno != (p_tile->packno % 0x10000)){
		      GROK_ERROR("SOP marker packet counter %d does not match expected counter %d",packno, p_tile->packno );
	        return false;
		  }
		  p_tile->packno++;
			active_src += 6;
		}
	}

	/*
	 When the marker PPT/PPM is used the packet header are store in PPT/PPM marker
	 This part deal with this characteristic
	 step 1: Read packet header in the saved structure
	 step 2: Return to codestream for decoding
	 */
	uint8_t *l_header_data = nullptr;
	uint8_t **l_header_data_start = nullptr;
	size_t *l_modified_length_ptr = nullptr;
	size_t l_remaining_length = 0;
	if (l_cp->ppm == 1) { /* PPM */
		l_header_data_start = &l_cp->ppm_data;
		l_header_data = *l_header_data_start;
		l_modified_length_ptr = &(l_cp->ppm_len);

	} else if (p_tcp->ppt == 1) { /* PPT */
		l_header_data_start = &(p_tcp->ppt_data);
		l_header_data = *l_header_data_start;
		l_modified_length_ptr = &(p_tcp->ppt_len);
	} else { /* Normal Case */
		l_header_data_start = &(active_src);
		l_header_data = *l_header_data_start;
		l_remaining_length =
				(size_t) (p_src_data + max_length - l_header_data);
		l_modified_length_ptr = &(l_remaining_length);
	}

	uint32_t l_present = 0;
	std::unique_ptr<BitIO> l_bio(
			new BitIO(l_header_data, *l_modified_length_ptr, false));
	if (*l_modified_length_ptr) {
		if (!l_bio->read(&l_present, 1)) {
			GROK_ERROR(
					"t2_read_packet_header: failed to read `present` bit ");
			return false;
		}
	}
	//GROK_INFO("present=%d \n", l_present);
	if (!l_present) {
		if (!l_bio->inalign())
			return false;
		l_header_data += l_bio->numbytes();

		/* EPH markers */
		if (p_tcp->csty & J2K_CP_CSTY_EPH) {
			if ((*l_modified_length_ptr
					- (size_t) (l_header_data - *l_header_data_start)) < 2U) {
				GROK_WARN(
						"Not enough space for expected EPH marker");
			} else if ((*l_header_data) != 0xff
					|| (*(l_header_data + 1) != 0x92)) {
				GROK_WARN( "Expected EPH marker");
			} else {
				l_header_data += 2;
			}
		}

		auto l_header_length = (size_t) (l_header_data - *l_header_data_start);
		*l_modified_length_ptr -= l_header_length;
		*l_header_data_start += l_header_length;

		*p_is_data_present = false;
		*p_data_read = (size_t) (active_src - p_src_data);
		src_buf->incr_cur_chunk_offset(*p_data_read);
		return true;
	}
	for (uint32_t bandno = 0; bandno < l_res->numbands; ++bandno) {
		auto l_band = l_res->bands + bandno;
		if (l_band->isEmpty()) {
			continue;
		}

		grk_tcd_precinct *l_prc = l_band->precincts + p_pi->precno;
		l_nb_code_blocks = (uint64_t) l_prc->cw * l_prc->ch;
		for (uint64_t cblkno = 0; cblkno < l_nb_code_blocks; cblkno++) {
			uint32_t l_included = 0, l_increment = 0;
			auto l_cblk = l_prc->cblks.dec + cblkno;

			/* if cblk not yet included before --> inclusion tagtree */
			if (!l_cblk->numSegments) {
				uint64_t value;
				if (!l_prc->incltree->decodeValue(l_bio.get(), cblkno,
						p_pi->layno + 1, &value)) {
					GROK_ERROR(
							"t2_read_packet_header: failed to read `inclusion` bit ");
					return false;
				}
				if (value != tag_tree_uninitialized_node_value
						&& value != p_pi->layno) {
					std::string msg =
							"Illegal inclusion tag tree found when decoding packet header.\n";
					msg +=
							"This problem can occur if empty packets are used (i.e., packets whose first header\n";
					msg +=
							"bit is 0) and the value coded by the inclusion tag tree in a subsequent packet\n";
					msg +=
							"is not exactly equal to the index of the quality layer in which each code-block\n";
					msg +=
							"makes its first contribution.  Such an error may occur from a\n";
					msg +=
							"mis-interpretation of the standard.  The problem may also occur as a result of\n";
					msg += "a corrupted code-stream\n";
					GROK_WARN( "%s", msg.c_str());

				}
#ifdef DEBUG_LOSSLESS_T2
					l_cblk->included = value;
#endif
				l_included = (value <= p_pi->layno) ? 1 : 0;
			}
			/* else one bit */
			else {
				if (!l_bio->read(&l_included, 1)) {
					GROK_ERROR(
							"t2_read_packet_header: failed to read `inclusion` bit ");
					return false;
				}

#ifdef DEBUG_LOSSLESS_T2
					l_cblk->included = l_included;
#endif
			}

			/* if cblk not included */
			if (!l_included) {
				l_cblk->numPassesInPacket = 0;
				//GROK_INFO("included=%d \n", l_included);
				continue;
			}

			/* if cblk not yet included --> zero-bitplane tagtree */
			if (!l_cblk->numSegments) {
				uint32_t K_msbs = 0;
				uint8_t value;
				bool rc = true;

				// see Taubman + Marcellin page 388
				// loop below stops at (# of missing bit planes  + 1)
				while ((rc = l_prc->imsbtree->decode(l_bio.get(), cblkno,
						K_msbs, &value)) && !value) {
					++K_msbs;
				}
				assert(K_msbs >= 1);
				K_msbs--;

				if (!rc) {
					GROK_ERROR(
							"Failed to decode zero-bitplane tag tree ");
					return false;
				}

				if (K_msbs > l_band->numbps) {
					GROK_WARN(
							"More missing bit planes (%d) than band bit planes (%d).\n",
							K_msbs, l_band->numbps);
					l_cblk->numbps = l_band->numbps;
				} else {
					l_cblk->numbps = l_band->numbps - K_msbs;
				}
				// BIBO analysis gives sanity check on number of bit planes
				if (l_cblk->numbps
						> max_precision_jpeg_2000 + GRK_J2K_MAXRLVLS * 5) {
					GROK_WARN(
							"Number of bit planes %u is impossibly large.\n",
							l_cblk->numbps);
					return false;
				}
				l_cblk->numlenbits = 3;
			}

			/* number of coding passes */
			if (!l_bio->getnumpasses(&l_cblk->numPassesInPacket)) {
				GROK_ERROR(
						"t2_read_packet_header: failed to read numpasses.");
				return false;
			}
			if (!l_bio->getcommacode(&l_increment)) {
				GROK_ERROR(
						"t2_read_packet_header: failed to read length indicator increment.");
				return false;
			}

			/* length indicator increment */
			l_cblk->numlenbits += l_increment;
			uint32_t l_segno = 0;

			if (!l_cblk->numSegments) {
				if (!t2_init_seg(l_cblk, l_segno,
						p_tcp->tccps[p_pi->compno].cblk_sty, true)) {
					return false;
				}
			} else {
				l_segno = l_cblk->numSegments - 1;
				if (l_cblk->segs[l_segno].numpasses
						== l_cblk->segs[l_segno].maxpasses) {
					++l_segno;
					if (!t2_init_seg(l_cblk, l_segno,
							p_tcp->tccps[p_pi->compno].cblk_sty, false)) {
						return false;
					}
				}
			}
			auto blockPassesInPacket = (int32_t) l_cblk->numPassesInPacket;
			do {
				auto l_seg = l_cblk->segs + l_segno;
				/* sanity check when there is no mode switch */
				if (l_seg->maxpasses == max_passes_per_segment){
				    if (blockPassesInPacket > (int32_t)max_passes_per_segment) {
				        GROK_WARN("Number of code block passes (%d) in packet is suspiciously large.", blockPassesInPacket);
				        // ToDO - we are truncating the number of passes at an arbitrary value of
				        // max_passes_per_segment. We should probably either skip the rest of this
				        // block, if possible, or do further sanity check on packet
		            l_seg->numPassesInPacket = max_passes_per_segment;
				    } else {
				        l_seg->numPassesInPacket = blockPassesInPacket;
				    }

				} else {
				    assert(l_seg->maxpasses >= l_seg->numpasses);
            l_seg->numPassesInPacket = (uint32_t) std::min<int32_t>(
                (int32_t) (l_seg->maxpasses - l_seg->numpasses),
                blockPassesInPacket);
				}
				uint32_t bits_to_read = l_cblk->numlenbits
						+ uint_floorlog2(l_seg->numPassesInPacket);
				if (bits_to_read > 32) {
					GROK_ERROR(
							"t2_read_packet_header: too many bits in segment length ");
					return false;
				}
				if (!l_bio->read(&l_seg->numBytesInPacket,bits_to_read)) {
					GROK_WARN(
							"t2_read_packet_header: failed to read segment length ");
				}
#ifdef DEBUG_LOSSLESS_T2
				l_cblk->packet_length_info->push_back(grk_packet_length_info(l_seg->numBytesInPacket,
								l_cblk->numlenbits + uint_floorlog2(l_seg->numPassesInPacket)));
#endif
				/*
				GROK_INFO(
						"included=%d numPassesInPacket=%d increment=%d len=%d \n",
						l_included, l_seg->numPassesInPacket, l_increment,
						l_seg->newlen);
						*/
				blockPassesInPacket -=	(int32_t) l_seg->numPassesInPacket;
				if (blockPassesInPacket > 0) {
					++l_segno;
					if (!t2_init_seg(l_cblk, l_segno,
							p_tcp->tccps[p_pi->compno].cblk_sty, false)) {
						return false;
					}
				}
			} while (blockPassesInPacket > 0);
		}
	}

	if (!l_bio->inalign()) {
		GROK_ERROR( "Unable to read packet header");
		return false;
	}

	l_header_data += l_bio->numbytes();

	/* EPH markers */
	if (p_tcp->csty & J2K_CP_CSTY_EPH) {
		if ((*l_modified_length_ptr
				- (uint32_t) (l_header_data - *l_header_data_start)) < 2U) {
			GROK_WARN(
					"Not enough space for expected EPH marker");
		} else if ((*l_header_data) != 0xff || (*(l_header_data + 1) != 0x92)) {
			GROK_WARN( "Expected EPH marker");
		} else {
			l_header_data += 2;
		}
	}

	auto l_header_length = (size_t) (l_header_data - *l_header_data_start);
	//GROK_INFO("hdrlen=%d \n", l_header_length);
	//GROK_INFO("packet body\n");
	*l_modified_length_ptr -= l_header_length;
	*l_header_data_start += l_header_length;

	*p_is_data_present = true;
	*p_data_read = (uint32_t) (active_src - p_src_data);
	src_buf->incr_cur_chunk_offset(*p_data_read);

	return true;
}

static bool t2_read_packet_data(grk_tcd_resolution *l_res, grk_pi_iterator *p_pi,
		ChunkBuffer *src_buf, uint64_t *p_data_read) {
	uint32_t bandno;
	uint64_t cblkno;
	auto l_band = l_res->bands;
	for (bandno = 0; bandno < l_res->numbands; ++bandno) {
		auto l_prc = &l_band->precincts[p_pi->precno];
		uint64_t l_nb_code_blocks = (uint64_t) l_prc->cw * l_prc->ch;
		auto l_cblk = l_prc->cblks.dec;

		for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
			grk_tcd_seg *l_seg = nullptr;

			if (!l_cblk->numPassesInPacket) {
				++l_cblk;
				continue;
			}

			if (!l_cblk->numSegments) {
				l_seg = l_cblk->segs;
				++l_cblk->numSegments;
				l_cblk->dataSize = 0;
			} else {
				l_seg = &l_cblk->segs[l_cblk->numSegments - 1];
				if (l_seg->numpasses == l_seg->maxpasses) {
					++l_seg;
					++l_cblk->numSegments;
				}
			}

			uint32_t numPassesInPacket = l_cblk->numPassesInPacket;
			do {
				size_t offset = (size_t) src_buf->get_global_offset();
				size_t len = src_buf->data_len;
				// Check possible overflow on segment length
				if (((offset + l_seg->numBytesInPacket) > len)) {
					GROK_WARN(
							"read packet data: segment offset (%u) plus segment length %u is greater than "
							"total length \nof all segments (%u) for codeblock "
							"%d (layer=%d, prec=%d, band=%d, res=%d, comp=%d)."
							"Truncating packet data.\n",
							offset, l_seg->numBytesInPacket, len, cblkno, p_pi->layno,
							p_pi->precno, bandno, p_pi->resno, p_pi->compno);
					l_seg->numBytesInPacket = (uint32_t)(len - offset);
				}
				//initialize dataindex to current contiguous size of code block
				if (l_seg->numpasses == 0) {
					l_seg->dataindex = l_cblk->dataSize;
				}
				// only add segment to seg_buffers if length is greater than zero
				if (l_seg->numBytesInPacket) {
					l_cblk->seg_buffers.push_back(src_buf->get_global_ptr(),
							(uint16_t) l_seg->numBytesInPacket);
					*(p_data_read) += l_seg->numBytesInPacket;
					src_buf->incr_cur_chunk_offset(l_seg->numBytesInPacket);
					l_cblk->dataSize += l_seg->numBytesInPacket;
					l_seg->len += l_seg->numBytesInPacket;
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

static bool t2_encode_packet(uint16_t tileno, grk_tcd_tile *tile, grk_tcp *tcp,
		grk_pi_iterator *pi, GrokStream *p_stream, uint64_t *p_data_written,
		uint64_t num_bytes_available,  grk_codestream_info  *cstr_info) {
	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint32_t precno = pi->precno;
	uint32_t layno = pi->layno;
	grk_tcd_tilecomp *tilec = &tile->comps[compno];
	grk_tcd_resolution *res = &tilec->resolutions[resno];
	uint64_t numHeaderBytes = 0;
	size_t streamBytes = 0;
	if (p_stream)
		streamBytes = p_stream->tell();
	// SOP marker
	if (tcp->csty & J2K_CP_CSTY_SOP) {
		if (!p_stream->write_byte(255)) {
			return false;
		}
		if (!p_stream->write_byte(145)) {
			return false;
		}
		if (!p_stream->write_byte(0)) {
			return false;
		}
		if (!p_stream->write_byte(4)) {
			return false;
		}
		/* packno is uint32_t modulo 65536, in big endian format */
		uint16_t packno = tile->packno % 0x10000;
		if (!p_stream->write_byte((uint8_t)(packno >> 8))) {
			return false;
		}
		if (!p_stream->write_byte((uint8_t)(packno & 0xff))) {
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
				prc->imsbtree->setvalue(cblkno,
						band->numbps - (int32_t) cblk->numbps);
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
			if (!cblk->num_passes_included_in_current_layer
					&& layer->numpasses) {
				prc->incltree->setvalue(cblkno, (int32_t) layno);
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
				prc->incltree->encode(bio.get(), cblkno, (int32_t) (layno + 1));
#ifdef DEBUG_LOSSLESS_T2
					cblk->included = layno;
#endif
			} else {
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
				prc->imsbtree->encode(bio.get(), cblkno,
						tag_tree_uninitialized_node_value);
			}
			/* number of coding passes included */
			bio->putnumpasses(layer->numpasses);
			uint32_t l_nb_passes = cblk->num_passes_included_in_current_layer
					+ layer->numpasses;
			auto pass = cblk->passes
					+ cblk->num_passes_included_in_current_layer;

			/* computation of the increase of the length indicator and insertion in the header     */
			for (uint32_t passno = cblk->num_passes_included_in_current_layer;
					passno < l_nb_passes; ++passno) {
				++nump;
				len += pass->len;

				if (pass->term || passno == l_nb_passes - 1) {
					increment = (uint32_t) std::max<int32_t>(
							(int32_t) increment,
							int_floorlog2((int32_t) len) + 1
									- ((int32_t) cblk->numlenbits
											+ int_floorlog2((int32_t) nump)));
					len = 0;
					nump = 0;
				}
				++pass;
			}
			bio->putcommacode((int32_t) increment);

			/* computation of the new Length indicator */
			cblk->numlenbits += increment;

			pass = cblk->passes + cblk->num_passes_included_in_current_layer;
			/* insertion of the codeword segment length */
			for (uint32_t passno = cblk->num_passes_included_in_current_layer;
					passno < l_nb_passes; ++passno) {
				nump++;
				len += pass->len;

				if (pass->term || passno == l_nb_passes - 1) {
#ifdef DEBUG_LOSSLESS_T2
						cblk->packet_length_info->push_back(grk_packet_length_info(len, cblk->numlenbits + (uint32_t)int_floorlog2((int32_t)nump)));
#endif
					if (!bio->write(len,
							cblk->numlenbits
									+ (uint32_t) int_floorlog2((int32_t) nump)))
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
		GROK_ERROR(
				"t2_encode_packet: Bit IO flush failed while encoding packet");
		return false;
	}

	auto temp = bio->numbytes();
	num_bytes_available -= (uint64_t) temp;
	numHeaderBytes += (uint64_t) temp;

	// EPH marker
	if (tcp->csty & J2K_CP_CSTY_EPH) {
		if (!p_stream->write_byte(255)) {
			return false;
		}
		if (!p_stream->write_byte(146)) {
			return false;
		}
		num_bytes_available -= 2;
		numHeaderBytes += 2;
	}

	/* << INDEX */
	/* End of packet header position. Currently only represents the distance to start of packet
	 Will be updated later by incrementing with packet start value*/
	//if (cstr_info && cstr_info->index_write) {
	//	 grk_packet_info  *info_PK = &cstr_info->tile[tileno].packet[cstr_info->packno];
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
				GROK_ERROR(
						"Code block layer size %d exceeds number of available bytes %d in tile buffer\n",
						cblk_layer->len, num_bytes_available);
				return false;
			}

			if (cblk_layer->len) {
				if (!p_stream->write_bytes(cblk_layer->data, cblk_layer->len)) {
					return false;
				}
				num_bytes_available -= cblk_layer->len;
			}
			cblk->num_passes_included_in_current_layer += cblk_layer->numpasses;
			if (cstr_info && cstr_info->index_write) {
				 grk_packet_info  *info_PK =
						&cstr_info->tile[tileno].packet[cstr_info->packno];
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
		auto src_buf = std::unique_ptr<ChunkBuffer>(new ChunkBuffer());
		seg_buf_push_back(src_buf.get(), dest, *p_data_written);

		bool rc = true;
		bool l_read_data;
		if (!t2_read_packet_header(p_t2,
			roundRes,
			tcp,
			pi,
			&l_read_data,
			src_buf.get(),
			&l_nb_bytes_read)) {
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
						grk_tcd_layer *layer = originalCblk->layers + layno;
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
					&l_nb_bytes_read)) {
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
								grk_tcd_layer *layer = originalCblk->layers + layno;
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
										roundTripData = ((grk_buf*)(roundTripCblk->seg_buffers.get(0)))->buf;
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
			GROK_ERROR("t2_encode_packet: decode packet failed");
		}
#endif
	return true;
}

static bool t2_encode_packet_simulate(grk_tcd_tile *tile, grk_tcp *tcp,
		grk_pi_iterator *pi, uint64_t *p_data_written, uint64_t length) {
	uint32_t bandno, cblkno;
	uint64_t l_nb_bytes;
	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint32_t precno = pi->precno;
	uint32_t layno = pi->layno;
	uint32_t l_nb_blocks;
	grk_tcd_band *band = nullptr;
	grk_tcd_cblk_enc *cblk = nullptr;
	grk_tcd_pass *pass = nullptr;

	grk_tcd_tilecomp *tilec = tile->comps + compno;
	grk_tcd_resolution *res = tilec->resolutions + resno;
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
			grk_tcd_precinct *prc = band->precincts + precno;

			if (prc->incltree)
				prc->incltree->reset();
			if (prc->imsbtree)
				prc->imsbtree->reset();

			l_nb_blocks = prc->cw * prc->ch;
			for (cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
				cblk = prc->cblks.enc + cblkno;

				cblk->num_passes_included_in_current_layer = 0;
				prc->imsbtree->setvalue(cblkno,
						band->numbps - (int32_t) cblk->numbps);
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
		grk_tcd_precinct *prc = band->precincts + precno;

		l_nb_blocks = prc->cw * prc->ch;
		cblk = prc->cblks.enc;

		for (cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
			grk_tcd_layer *layer = cblk->layers + layno;

			if (!cblk->num_passes_included_in_current_layer
					&& layer->numpasses) {
				prc->incltree->setvalue(cblkno, (int32_t) layno);
			}

			++cblk;
		}

		cblk = prc->cblks.enc;
		for (cblkno = 0; cblkno < l_nb_blocks; cblkno++) {
			grk_tcd_layer *layer = cblk->layers + layno;
			uint32_t increment = 0;
			uint32_t nump = 0;
			uint32_t len = 0, passno;
			uint32_t l_nb_passes;

			/* cblk inclusion bits */
			if (!cblk->num_passes_included_in_current_layer) {
				prc->incltree->encode(bio.get(), cblkno, (int32_t) (layno + 1));
			} else {
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
				prc->imsbtree->encode(bio.get(), cblkno,
						tag_tree_uninitialized_node_value);
			}

			/* number of coding passes included */
			bio->putnumpasses(layer->numpasses);
			l_nb_passes = cblk->num_passes_included_in_current_layer
					+ layer->numpasses;
			pass = cblk->passes + cblk->num_passes_included_in_current_layer;

			/* computation of the increase of the length indicator and insertion in the header     */
			for (passno = cblk->num_passes_included_in_current_layer;
					passno < l_nb_passes; ++passno) {
				++nump;
				len += pass->len;

				if (pass->term
						|| passno
								== (cblk->num_passes_included_in_current_layer
										+ layer->numpasses) - 1) {
					increment = (uint32_t) std::max<int32_t>(
							(int32_t) increment,
							int_floorlog2((int32_t) len) + 1
									- ((int32_t) cblk->numlenbits
											+ int_floorlog2((int32_t) nump)));
					len = 0;
					nump = 0;
				}

				++pass;
			}
			bio->putcommacode((int32_t) increment);

			/* computation of the new Length indicator */
			cblk->numlenbits += increment;

			pass = cblk->passes + cblk->num_passes_included_in_current_layer;
			/* insertion of the codeword segment length */
			for (passno = cblk->num_passes_included_in_current_layer;
					passno < l_nb_passes; ++passno) {
				nump++;
				len += pass->len;

				if (pass->term
						|| passno
								== (cblk->num_passes_included_in_current_layer
										+ layer->numpasses) - 1) {
					if (!bio->write(len,
							cblk->numlenbits
									+ (uint32_t) int_floorlog2((int32_t) nump)))
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

	l_nb_bytes = (uint64_t) bio->numbytes();
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
		grk_tcd_precinct *prc = band->precincts + precno;

		l_nb_blocks = prc->cw * prc->ch;
		cblk = prc->cblks.enc;

		for (cblkno = 0; cblkno < l_nb_blocks; ++cblkno) {
			grk_tcd_layer *layer = cblk->layers + layno;

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
static bool t2_skip_packet(t2_t *p_t2, grk_tcd_tile *p_tile, grk_tcp *p_tcp,
		grk_pi_iterator *p_pi, ChunkBuffer *src_buf, uint64_t *p_data_read) {
	bool l_read_data;
	uint64_t l_nb_bytes_read = 0;
	uint64_t l_nb_total_bytes_read = 0;
	uint64_t max_length = (uint64_t) src_buf->get_cur_chunk_len();

	*p_data_read = 0;

	if (!t2_read_packet_header(p_t2,
			p_tile, p_tcp, p_pi,
			&l_read_data, src_buf, &l_nb_bytes_read)) {
		return false;
	}

	l_nb_total_bytes_read += l_nb_bytes_read;
	max_length -= l_nb_bytes_read;

	/* we should read data for the packet */
	if (l_read_data) {
		l_nb_bytes_read = 0;

		if (!t2_skip_packet_data(
				&p_tile->comps[p_pi->compno].resolutions[p_pi->resno], p_pi,
				&l_nb_bytes_read, max_length)) {
			return false;
		}
		src_buf->incr_cur_chunk_offset(l_nb_bytes_read);
		l_nb_total_bytes_read += l_nb_bytes_read;
	}
	*p_data_read = l_nb_total_bytes_read;

	return true;
}

static bool t2_skip_packet_data(grk_tcd_resolution *l_res, grk_pi_iterator *p_pi,
		uint64_t *p_data_read, uint64_t max_length) {
	uint32_t bandno;
	uint64_t l_nb_code_blocks, cblkno;
	*p_data_read = 0;
	for (bandno = 0; bandno < l_res->numbands; ++bandno) {
		auto l_band = l_res->bands + bandno;
		if (l_band->isEmpty())
			continue;

		auto l_prc = &l_band->precincts[p_pi->precno];
		l_nb_code_blocks = (uint64_t) l_prc->cw * l_prc->ch;
		auto l_cblk = l_prc->cblks.dec;
		for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
			grk_tcd_seg *l_seg = nullptr;

			if (!l_cblk->numPassesInPacket) {
				/* nothing to do */
				++l_cblk;
				continue;
			}

			if (!l_cblk->numSegments) {
				l_seg = l_cblk->segs;
				++l_cblk->numSegments;
				l_cblk->dataSize = 0;
			} else {
				l_seg = &l_cblk->segs[l_cblk->numSegments - 1];
				if (l_seg->numpasses == l_seg->maxpasses) {
					++l_seg;
					++l_cblk->numSegments;
				}
			}
			uint32_t numPassesInPacket = l_cblk->numPassesInPacket;
			do {
				/* Check possible overflow then size */
				if (((*p_data_read + l_seg->numBytesInPacket) < (*p_data_read))
						|| ((*p_data_read + l_seg->numBytesInPacket) > max_length)) {
					GROK_ERROR(
							"skip: segment too long (%d) with max (%d) for codeblock %d (p=%d, b=%d, r=%d, c=%d)\n",
							l_seg->numBytesInPacket, max_length, cblkno, p_pi->precno,
							bandno, p_pi->resno, p_pi->compno);
					return false;
				}

				GROK_ERROR( "p_data_read (%d) newlen (%d) \n",
						*p_data_read, l_seg->numBytesInPacket);
				*(p_data_read) += l_seg->numBytesInPacket;

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

static bool t2_init_seg(grk_tcd_cblk_dec *cblk, uint32_t index,
		uint8_t cblk_sty, bool first) {
	uint32_t l_nb_segs = index + 1;

	if (l_nb_segs > cblk->numSegmentsAllocated) {
		auto new_segs = new grk_tcd_seg[cblk->numSegmentsAllocated
				+ cblk->numSegmentsAllocated];
		for (uint32_t i = 0; i < cblk->numSegmentsAllocated; ++i)
			new_segs[i] = cblk->segs[i];
		cblk->numSegmentsAllocated += default_numbers_segments;
		if (cblk->segs)
			delete[] cblk->segs;
		cblk->segs = new_segs;
	}

	auto seg = &cblk->segs[index];
	seg->clear();

	if (cblk_sty & J2K_CCP_CBLKSTY_TERMALL) {
		seg->maxpasses = 1;
	} else if (cblk_sty & J2K_CCP_CBLKSTY_LAZY) {
		if (first) {
			seg->maxpasses = 10;
		} else {
			auto last_seg = seg - 1;
			seg->maxpasses =
					((last_seg->maxpasses == 1) || (last_seg->maxpasses == 10)) ?
							2 : 1;
		}
	} else {
		seg->maxpasses = max_passes_per_segment;
	}

	return true;
}

}
