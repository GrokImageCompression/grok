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

//#define DEBUG_ENCODE_PACKETS

namespace grk {

T2Decode::T2Decode(TileProcessor *tileProc) :
		tileProcessor(tileProc) {
}

bool T2Decode::decode_packets(uint16_t tile_no, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {

	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->image;
	auto tcp = cp->tcps + tile_no;
	auto p_tile = tileProcessor->tile;
	uint32_t nb_pocs = tcp->numpocs + 1;
	auto pi = pi_create_decode(image, cp, tile_no);
	if (!pi)
		return false;

	auto packetLengths = tileProcessor->plt_markers;
	// we don't currently support PLM markers,
	// so we disable packet length markers if we have both PLT and PLM
	bool usePlt = packetLengths && !cp->plm_markers;
	if (usePlt)
		packetLengths->getInit();
	for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
		/* if the resolution needed is too low, one dim of the tilec
		 * could be equal to zero
		 * and no packets are used to decode this resolution and
		 * current_pi->resno is always >=
		 * tile->comps[current_pi->compno].minimum_num_resolutions
		 * and no l_img_comp->resno_decoded are computed
		 */
		bool *first_pass_failed = new bool[image->numcomps];
		for (size_t k = 0; k < image->numcomps; ++k)
			first_pass_failed[k] = true;

		auto current_pi = pi + pino;
		if (current_pi->poc.prg == GRK_PROG_UNKNOWN) {
			pi_destroy(pi, nb_pocs);
			delete[] first_pass_failed;
			GROK_ERROR("decode_packets: Unknown progression order");
			return false;
		}
		while (pi_next(current_pi)) {
			auto tilec = p_tile->comps + current_pi->compno;
			auto skip_the_packet = current_pi->layno
					>= tcp->num_layers_to_decode
					|| current_pi->resno >= tilec->minimum_num_resolutions;

			auto img_comp = image->comps + current_pi->compno;
			uint32_t pltMarkerLen = 0;
			if (usePlt)
				pltMarkerLen = packetLengths->getNext();

			/*
			 GROK_INFO(
			 "packet prg=%d cmptno=%02d rlvlno=%02d prcno=%03d layrno=%02d\n",
			 current_pi->poc.prg1, current_pi->compno,
			 current_pi->resno, current_pi->precno,
			 current_pi->layno);
			 */
			if (!skip_the_packet && !tilec->whole_tile_decoding) {
				skip_the_packet = true;
				auto res = tilec->resolutions + current_pi->resno;
				for (uint32_t bandno = 0;
						bandno < res->numbands && skip_the_packet; ++bandno) {
					auto band = res->bands + bandno;
					auto prec = band->precincts + current_pi->precno;
					if (tilec->is_subband_area_of_interest(current_pi->resno,
							band->bandno, prec->x0, prec->y0, prec->x1,
							prec->y1)) {
						skip_the_packet = false;
						break;
					}
				}
			}

			uint64_t nb_bytes_read = 0;
			if (!skip_the_packet) {
				/*
				 printf("packet cmptno=%02d rlvlno=%02d prcno=%03d layrno=%02d -> %s\n",
				 current_pi->compno, current_pi->resno,
				 current_pi->precno, current_pi->layno, skip_the_packet ? "skipped" : "kept");
				 */
				first_pass_failed[current_pi->compno] = false;

				if (!decode_packet(tcp, current_pi, src_buf, &nb_bytes_read)) {
					pi_destroy(pi, nb_pocs);
					delete[] first_pass_failed;
					return false;
				}

				img_comp->resno_decoded = std::max<uint32_t>(current_pi->resno,
						img_comp->resno_decoded);

			} else {
				if (pltMarkerLen) {
					nb_bytes_read = pltMarkerLen;
					src_buf->incr_cur_chunk_offset(nb_bytes_read);
				} else if (!skip_packet(tcp, current_pi, src_buf,
						&nb_bytes_read)) {
					pi_destroy(pi, nb_pocs);
					delete[] first_pass_failed;
					return false;
				}
			}

			if (first_pass_failed[current_pi->compno]) {
				img_comp = image->comps + current_pi->compno;
				if (img_comp->resno_decoded == 0) {
					img_comp->resno_decoded =
							p_tile->comps[current_pi->compno].minimum_num_resolutions
									- 1;
				}
			}
			//GROK_INFO("T2Decode Packet length: %d", nb_bytes_read);
			*p_data_read += nb_bytes_read;
		}
		delete[] first_pass_failed;
	}
	pi_destroy(pi, nb_pocs);
	return true;
}


bool T2Decode::decode_packet(TileCodingParams *p_tcp, PacketIter *p_pi, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	uint64_t max_length = src_buf->data_len - src_buf->get_global_offset();
	if (max_length == 0) {
		GROK_WARN("decode_packet: No data for either packet header\n"
				"or packet body for packet prg=%d "
				"cmptno=%02d reslvlno=%02d prcno=%03d layrno=%02d",
		 p_pi->poc.prg1, p_pi->compno,
		 p_pi->resno, p_pi->precno,
		 p_pi->layno);
		return true;
	}
	auto p_tile = tileProcessor->tile;
	auto res = &p_tile->comps[p_pi->compno].resolutions[p_pi->resno];
	bool read_data;
	uint64_t nb_bytes_read = 0;
	uint64_t nb_total_bytes_read = 0;
	*p_data_read = 0;
	if (!read_packet_header(p_tcp, p_pi, &read_data, src_buf, &nb_bytes_read)) {
		return false;
	}
	nb_total_bytes_read += nb_bytes_read;

	/* we should read data for the packet */
	if (read_data) {
		nb_bytes_read = 0;
		if (!read_packet_data(res, p_pi, src_buf, &nb_bytes_read)) {
			return false;
		}
		nb_total_bytes_read += nb_bytes_read;
	}
	*p_data_read = nb_total_bytes_read;
	return true;
}

bool T2Decode::read_packet_header(TileCodingParams *p_tcp, PacketIter *p_pi,
		bool *p_is_data_present, ChunkBuffer *src_buf, uint64_t *p_data_read) {
	auto p_tile = tileProcessor->tile;
	auto res = &p_tile->comps[p_pi->compno].resolutions[p_pi->resno];
	auto p_src_data = src_buf->get_global_ptr();
	uint64_t max_length = src_buf->data_len - src_buf->get_global_offset();
	uint64_t nb_code_blocks = 0;
	auto active_src = p_src_data;

	if (p_pi->layno == 0) {
		/* reset tagtrees */
		for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
			auto band = res->bands + bandno;
			if (band->isEmpty())
				continue;
			auto prc = &band->precincts[p_pi->precno];
			if (!(p_pi->precno < (band->numPrecincts))) {
				GROK_ERROR("Invalid precinct");
				return false;
			}
			if (prc->incltree)
				prc->incltree->reset();
			if (prc->imsbtree)
				prc->imsbtree->reset();
			nb_code_blocks = (uint64_t) prc->cw * prc->ch;
			for (uint64_t cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
				auto cblk = prc->cblks.dec + cblkno;
				cblk->numSegments = 0;
			}
		}
	}

	/* SOP markers */
	if (p_tcp->csty & J2K_CP_CSTY_SOP) {
		if (max_length < 6) {
			GROK_WARN("Not enough space for expected SOP marker");
		} else if ((*active_src) != 0xff || (*(active_src + 1) != 0x91)) {
			GROK_WARN("Expected SOP marker");
		} else {
			uint16_t packno = (uint16_t) (((uint16_t) active_src[4] << 8)
					| active_src[5]);
			if (packno != (p_tile->packno % 0x10000)) {
				GROK_ERROR(
						"SOP marker packet counter %d does not match expected counter %d",
						packno, p_tile->packno);
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
	 step 2: Return to code stream for decoding
	 */
	uint8_t *header_data = nullptr;
	uint8_t **header_data_start = nullptr;
	size_t *modified_length_ptr = nullptr;
	size_t remaining_length = 0;
	auto cp = tileProcessor->m_cp;
	if (cp->ppm) { /* PPM */
		header_data_start = &cp->ppm_data;
		header_data = *header_data_start;
		modified_length_ptr = &(cp->ppm_len);

	} else if (p_tcp->ppt) { /* PPT */
		header_data_start = &(p_tcp->ppt_data);
		header_data = *header_data_start;
		modified_length_ptr = &(p_tcp->ppt_len);
	} else { /* Normal Case */
		header_data_start = &(active_src);
		header_data = *header_data_start;
		remaining_length = (size_t) (p_src_data + max_length - header_data);
		modified_length_ptr = &(remaining_length);
	}

	uint32_t present = 0;
	std::unique_ptr<BitIO> bio(
			new BitIO(header_data, *modified_length_ptr, false));
	if (*modified_length_ptr) {
		if (!bio->read(&present, 1)) {
			GROK_ERROR("read_packet_header: failed to read `present` bit ");
			return false;
		}
	}
	//GROK_INFO("present=%d ", present);
	if (!present) {
		if (!bio->inalign())
			return false;
		header_data += bio->numbytes();

		/* EPH markers */
		if (p_tcp->csty & J2K_CP_CSTY_EPH) {
			if ((*modified_length_ptr
					- (size_t) (header_data - *header_data_start)) < 2U) {
				GROK_WARN("Not enough space for expected EPH marker");
			} else if ((*header_data) != 0xff || (*(header_data + 1) != 0x92)) {
				GROK_WARN("Expected EPH marker");
			} else {
				header_data += 2;
			}
		}

		auto header_length = (size_t) (header_data - *header_data_start);
		*modified_length_ptr -= header_length;
		*header_data_start += header_length;

		*p_is_data_present = false;
		*p_data_read = (size_t) (active_src - p_src_data);
		src_buf->incr_cur_chunk_offset(*p_data_read);
		return true;
	}
	for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
		auto band = res->bands + bandno;
		if (band->isEmpty())
			continue;
		auto prc = band->precincts + p_pi->precno;
		nb_code_blocks = (uint64_t) prc->cw * prc->ch;
		for (uint64_t cblkno = 0; cblkno < nb_code_blocks; cblkno++) {
			uint32_t included = 0, increment = 0;
			auto cblk = prc->cblks.dec + cblkno;

			/* if cblk not yet included before --> inclusion tagtree */
			if (!cblk->numSegments) {
				uint64_t value;
				if (!prc->incltree->decodeValue(bio.get(), cblkno,
						p_pi->layno + 1, &value)) {
					GROK_ERROR(
							"read_packet_header: failed to read `inclusion` bit ");
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
					GROK_WARN("%s", msg.c_str());

				}
#ifdef DEBUG_LOSSLESS_T2
				 cblk->included = value;
#endif
				included = (value <= p_pi->layno) ? 1 : 0;
			}
			/* else one bit */
			else {
				if (!bio->read(&included, 1)) {
					GROK_ERROR(
							"read_packet_header: failed to read `inclusion` bit ");
					return false;
				}

#ifdef DEBUG_LOSSLESS_T2
				 cblk->included = included;
#endif
			}

			/* if cblk not included */
			if (!included) {
				cblk->numPassesInPacket = 0;
				//GROK_INFO("included=%d ", included);
				continue;
			}

			/* if cblk not yet included --> zero-bitplane tagtree */
			if (!cblk->numSegments) {
				uint32_t K_msbs = 0;
				uint8_t value;
				bool rc = true;

				// see Taubman + Marcellin page 388
				// loop below stops at (# of missing bit planes  + 1)
				while ((rc = prc->imsbtree->decompress(bio.get(), cblkno,
						K_msbs, &value)) && !value) {
					++K_msbs;
				}
				assert(K_msbs >= 1);
				K_msbs--;

				if (!rc) {
					GROK_ERROR("Failed to decompress zero-bitplane tag tree ");
					return false;
				}

				if (K_msbs > band->numbps) {
					GROK_WARN(
							"More missing bit planes (%d) than band bit planes (%d).",
							K_msbs, band->numbps);
					cblk->numbps = band->numbps;
				} else {
					cblk->numbps = band->numbps - K_msbs;
				}
				// BIBO analysis gives sanity check on number of bit planes
				if (cblk->numbps
						> max_precision_jpeg_2000 + GRK_J2K_MAXRLVLS * 5) {
					GROK_WARN("Number of bit planes %u is impossibly large.",
							cblk->numbps);
					return false;
				}
				cblk->numlenbits = 3;
			}

			/* number of coding passes */
			if (!bio->getnumpasses(&cblk->numPassesInPacket)) {
				GROK_ERROR("read_packet_header: failed to read numpasses.");
				return false;
			}
			if (!bio->getcommacode(&increment)) {
				GROK_ERROR(
						"read_packet_header: failed to read length indicator increment.");
				return false;
			}

			/* length indicator increment */
			cblk->numlenbits += increment;
			uint32_t segno = 0;

			if (!cblk->numSegments) {
				if (!T2Decode::init_seg(cblk, segno,
						p_tcp->tccps[p_pi->compno].cblk_sty, true)) {
					return false;
				}
			} else {
				segno = cblk->numSegments - 1;
				if (cblk->segs[segno].numpasses
						== cblk->segs[segno].maxpasses) {
					++segno;
					if (!T2Decode::init_seg(cblk, segno,
							p_tcp->tccps[p_pi->compno].cblk_sty, false)) {
						return false;
					}
				}
			}
			auto blockPassesInPacket = (int32_t) cblk->numPassesInPacket;
			do {
				auto seg = cblk->segs + segno;
				/* sanity check when there is no mode switch */
				if (seg->maxpasses == max_passes_per_segment) {
					if (blockPassesInPacket
							> (int32_t) max_passes_per_segment) {
						GROK_WARN(
								"Number of code block passes (%d) in packet is suspiciously large.",
								blockPassesInPacket);
						// ToDO - we are truncating the number of passes at an arbitrary value of
						// max_passes_per_segment. We should probably either skip the rest of this
						// block, if possible, or do further sanity check on packet
						seg->numPassesInPacket = max_passes_per_segment;
					} else {
						seg->numPassesInPacket = (uint32_t) blockPassesInPacket;
					}

				} else {
					assert(seg->maxpasses >= seg->numpasses);
					seg->numPassesInPacket = (uint32_t) std::min<int32_t>(
							(int32_t) (seg->maxpasses - seg->numpasses),
							blockPassesInPacket);
				}
				uint32_t bits_to_read = cblk->numlenbits
						+ uint_floorlog2(seg->numPassesInPacket);
				if (bits_to_read > 32) {
					GROK_ERROR(
							"read_packet_header: too many bits in segment length ");
					return false;
				}
				if (!bio->read(&seg->numBytesInPacket, bits_to_read)) {
					GROK_WARN(
							"read_packet_header: failed to read segment length ");
				}
#ifdef DEBUG_LOSSLESS_T2
			 cblk->packet_length_info->push_back(grk_packet_length_info(seg->numBytesInPacket,
							 cblk->numlenbits + uint_floorlog2(seg->numPassesInPacket)));
#endif
				/*
				 GROK_INFO(
				 "included=%d numPassesInPacket=%d increment=%d len=%d ",
				 included, seg->numPassesInPacket, increment,
				 seg->newlen);
				 */
				blockPassesInPacket -= (int32_t) seg->numPassesInPacket;
				if (blockPassesInPacket > 0) {
					++segno;
					if (!T2Decode::init_seg(cblk, segno,
							p_tcp->tccps[p_pi->compno].cblk_sty, false)) {
						return false;
					}
				}
			} while (blockPassesInPacket > 0);
		}
	}

	if (!bio->inalign()) {
		GROK_ERROR("Unable to read packet header");
		return false;
	}

	header_data += bio->numbytes();

	/* EPH markers */
	if (p_tcp->csty & J2K_CP_CSTY_EPH) {
		if ((*modified_length_ptr
				- (uint32_t) (header_data - *header_data_start)) < 2U) {
			GROK_WARN("Not enough space for expected EPH marker");
		} else if ((*header_data) != 0xff || (*(header_data + 1) != 0x92)) {
			GROK_WARN("Expected EPH marker");
		} else {
			header_data += 2;
		}
	}

	auto header_length = (size_t) (header_data - *header_data_start);
	//GROK_INFO("hdrlen=%d ", header_length);
	//GROK_INFO("packet body\n");
	*modified_length_ptr -= header_length;
	*header_data_start += header_length;
	*p_is_data_present = true;
	*p_data_read = (uint32_t) (active_src - p_src_data);
	src_buf->incr_cur_chunk_offset(*p_data_read);

	return true;
}

bool T2Decode::read_packet_data(grk_resolution *res, PacketIter *p_pi,
		ChunkBuffer *src_buf, uint64_t *p_data_read) {
	for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
		auto band = res->bands + bandno;
		auto prc = &band->precincts[p_pi->precno];
		uint64_t nb_code_blocks = (uint64_t) prc->cw * prc->ch;
		for (uint64_t cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
			auto cblk = prc->cblks.dec + cblkno;
			if (!cblk->numPassesInPacket) {
				++cblk;
				continue;
			}
			grk_seg *seg = nullptr;
			if (!cblk->numSegments) {
				seg = cblk->segs;
				++cblk->numSegments;
				cblk->compressedDataSize = 0;
			} else {
				seg = &cblk->segs[cblk->numSegments - 1];
				if (seg->numpasses == seg->maxpasses) {
					++seg;
					++cblk->numSegments;
				}
			}

			uint32_t numPassesInPacket = cblk->numPassesInPacket;
			do {
				size_t offset = (size_t) src_buf->get_global_offset();
				size_t len = src_buf->data_len;
				// Check possible overflow on segment length
				if (((offset + seg->numBytesInPacket) > len)) {
					GROK_WARN(
							"read packet data: segment offset (%u) plus segment length %u\n"
							"is greater than total length \n"
							"of all segments (%u) for codeblock "
							"%d (layer=%d, prec=%d, band=%d, res=%d, comp=%d)."
							" Truncating packet data.", offset,
							seg->numBytesInPacket, len, cblkno, p_pi->layno,
							p_pi->precno, bandno, p_pi->resno, p_pi->compno);
					seg->numBytesInPacket = (uint32_t) (len - offset);
				}
				//initialize dataindex to current contiguous size of code block
				if (seg->numpasses == 0)
					seg->dataindex = (uint32_t) cblk->compressedDataSize;

				// only add segment to seg_buffers if length is greater than zero
				if (seg->numBytesInPacket) {
					cblk->seg_buffers.push_back(src_buf->get_global_ptr(),
							seg->numBytesInPacket);
					*(p_data_read) += seg->numBytesInPacket;
					src_buf->incr_cur_chunk_offset(seg->numBytesInPacket);
					cblk->compressedDataSize += seg->numBytesInPacket;
					seg->len += seg->numBytesInPacket;
				}
				seg->numpasses += seg->numPassesInPacket;
				numPassesInPacket -= seg->numPassesInPacket;
				if (numPassesInPacket > 0) {
					++seg;
					++cblk->numSegments;
				}
			} while (numPassesInPacket > 0);
		} /* next code_block */
	}

	return true;
}
bool T2Decode::skip_packet(TileCodingParams *p_tcp, PacketIter *p_pi, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	bool read_data;
	uint64_t nb_bytes_read = 0;
	uint64_t nb_totabytes_read = 0;
	uint64_t max_length = (uint64_t) src_buf->get_cur_chunk_len();
	auto p_tile = tileProcessor->tile;

	*p_data_read = 0;
	if (!read_packet_header(p_tcp, p_pi, &read_data, src_buf, &nb_bytes_read))
		return false;
	nb_totabytes_read += nb_bytes_read;
	max_length -= nb_bytes_read;

	/* we should read data for the packet */
	if (read_data) {
		nb_bytes_read = 0;
		if (!skip_packet_data(
				&p_tile->comps[p_pi->compno].resolutions[p_pi->resno], p_pi,
				&nb_bytes_read, max_length)) {
			return false;
		}
		src_buf->incr_cur_chunk_offset(nb_bytes_read);
		nb_totabytes_read += nb_bytes_read;
	}
	*p_data_read = nb_totabytes_read;

	return true;
}

bool T2Decode::skip_packet_data(grk_resolution *res, PacketIter *p_pi,
		uint64_t *p_data_read, uint64_t max_length) {
	*p_data_read = 0;
	for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
		auto band = res->bands + bandno;
		if (band->isEmpty())
			continue;

		auto prc = &band->precincts[p_pi->precno];
		uint64_t nb_code_blocks = (uint64_t) prc->cw * prc->ch;
		for (uint64_t cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
			auto cblk = prc->cblks.dec + cblkno;
			if (!cblk->numPassesInPacket) {
				/* nothing to do */
				++cblk;
				continue;
			}
			grk_seg *seg = nullptr;
			if (!cblk->numSegments) {
				seg = cblk->segs;
				++cblk->numSegments;
				cblk->compressedDataSize = 0;
			} else {
				seg = &cblk->segs[cblk->numSegments - 1];
				if (seg->numpasses == seg->maxpasses) {
					++seg;
					++cblk->numSegments;
				}
			}
			uint32_t numPassesInPacket = cblk->numPassesInPacket;
			do {
				/* Check possible overflow then size */
				if (((*p_data_read + seg->numBytesInPacket) < (*p_data_read))
						|| ((*p_data_read + seg->numBytesInPacket) > max_length)) {
					GROK_ERROR(
							"skip: segment too long (%d) with max (%d) for codeblock %d (p=%d, b=%d, r=%d, c=%d)",
							seg->numBytesInPacket, max_length, cblkno,
							p_pi->precno, bandno, p_pi->resno, p_pi->compno);
					return false;
				}

				//GROK_INFO( "skip packet: p_data_read = %d, bytes in packet =  %d ",
				//		*p_data_read, seg->numBytesInPacket);
				*(p_data_read) += seg->numBytesInPacket;
				seg->numpasses += seg->numPassesInPacket;
				numPassesInPacket -= seg->numPassesInPacket;
				if (numPassesInPacket > 0) {
					++seg;
					++cblk->numSegments;
				}
			} while (numPassesInPacket > 0);
		}
	}
	return true;
}

}
