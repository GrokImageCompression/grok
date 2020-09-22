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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
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
		 * tile->comps[current_pi->compno].resolutions_to_decompress
		 * and no l_img_comp->resno_decoded are computed
		 */
		bool *first_pass_failed = new bool[image->numcomps];
		for (size_t k = 0; k < image->numcomps; ++k)
			first_pass_failed[k] = true;

		auto current_pi = pi + pino;
		if (current_pi->poc.prg == GRK_PROG_UNKNOWN) {
			pi_destroy(pi, nb_pocs);
			delete[] first_pass_failed;
			GRK_ERROR("decode_packets: Unknown progression order");
			return false;
		}
		while (pi_next(current_pi)) {
			auto tilec = p_tile->comps + current_pi->compno;
			auto skip_the_packet = current_pi->layno
					>= tcp->num_layers_to_decode
					|| current_pi->resno >= tilec->resolutions_to_decompress;

			uint32_t pltMarkerLen = 0;
			if (usePlt)
				pltMarkerLen = packetLengths->getNext();

			/*
			 GRK_INFO(
			 "packet prg=%u cmptno=%02d rlvlno=%02d prcno=%03d layrno=%02d\n",
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
			try {
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
					tileProcessor->m_resno_decoded_per_component[current_pi->compno] = std::max<uint32_t>(current_pi->resno,
							tileProcessor->m_resno_decoded_per_component[current_pi->compno]);

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
			} 	catch (TruncatedStreamException &tex){
				GRK_WARN("Truncated packet: tile=%d component=%02d resolution=%02d precinct=%03d layer=%02d",
				 tile_no, current_pi->compno, current_pi->resno,
				 current_pi->precno, current_pi->layno);
				break;
			}
			if (first_pass_failed[current_pi->compno]) {
				if (tileProcessor->m_resno_decoded_per_component[current_pi->compno]  == 0) {
					tileProcessor->m_resno_decoded_per_component[current_pi->compno] =
							p_tile->comps[current_pi->compno].resolutions_to_decompress
									- 1;
				}
			}
			//GRK_INFO("T2Decode Packet length: %u", nb_bytes_read);
			*p_data_read += nb_bytes_read;
		}
		delete[] first_pass_failed;
	}
	pi_destroy(pi, nb_pocs);
	return true;
}


bool T2Decode::decode_packet(TileCodingParams *p_tcp, PacketIter *p_pi, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	uint64_t max_length = src_buf->getRemainingLength();
	if (max_length == 0) {
		GRK_WARN("Tile %d decode_packet: No data for either packet header\n"
				"or packet body for packet prg=%u "
				"cmptno=%02d reslvlno=%02d prcno=%03d layrno=%02d",
		tileProcessor->m_tile_index,
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
	uint64_t max_length = src_buf->getRemainingLength();
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
				GRK_ERROR("Invalid precinct");
				return false;
			}
			if (prc->incltree)
				prc->incltree->reset();
			if (prc->imsbtree)
				prc->imsbtree->reset();
			nb_code_blocks = (uint64_t) prc->cw * prc->ch;
			for (uint64_t cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
				auto cblk = prc->dec + cblkno;
				cblk->numSegments = 0;
			}
		}
	}

	/* SOP markers */
	if (p_tcp->csty & J2K_CP_CSTY_SOP) {
		if (max_length < 6) {
			GRK_WARN("Not enough space for expected SOP marker");
		} else if ((*active_src) != 0xff || (*(active_src + 1) != 0x91)) {
			GRK_WARN("Expected SOP marker");
		} else {
			uint16_t packno = (uint16_t) (((uint16_t) active_src[4] << 8)
					| active_src[5]);
			if (packno != (p_tile->packno % 0x10000)) {
				GRK_ERROR(
						"SOP marker packet counter %u does not match expected counter %u",
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
	if (cp->ppm_marker) { /* PPM */
		if (tileProcessor->m_tile_index >= cp->ppm_marker->m_tile_packet_headers.size()){
			GRK_ERROR("PPM marker has no packed packet header data for tile %d",
					tileProcessor->m_tile_index+1);
			return false;
		}
		auto tile_packet_header =
				&cp->ppm_marker->m_tile_packet_headers[tileProcessor->m_tile_index];
		header_data_start = &tile_packet_header->buf;
		header_data = *header_data_start;
		remaining_length = tile_packet_header->len;
		modified_length_ptr = &(remaining_length);

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
		bio->read(&present, 1);
	}
	//GRK_INFO("present=%u ", present);
	if (!present) {
		bio->inalign();
		header_data += bio->numbytes();

		/* EPH markers */
		if (p_tcp->csty & J2K_CP_CSTY_EPH) {
			if ((*modified_length_ptr
					- (size_t) (header_data - *header_data_start)) < 2U) {
				GRK_WARN("Not enough space for expected EPH marker");
			} else if ((*header_data) != 0xff || (*(header_data + 1) != 0x92)) {
				GRK_WARN("Expected EPH marker");
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
			auto cblk = prc->dec + cblkno;

			/* if cblk not yet included before --> inclusion tagtree */
			if (!cblk->numSegments) {
				uint64_t value;
				prc->incltree->decodeValue(bio.get(), cblkno,
						p_pi->layno + 1, &value);

				if (value != tag_tree_uninitialized_node_value
						&& value != p_pi->layno) {
					GRK_WARN("Tile number: %u",tileProcessor->m_tile_index+1);
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
					msg += "a corrupted code-stream";
					GRK_WARN("%s", msg.c_str());
					tileProcessor->m_corrupt_packet = true;

				}
#ifdef DEBUG_LOSSLESS_T2
				 cblk->included = value;
#endif
				included = (value <= p_pi->layno) ? 1 : 0;
			}
			/* else one bit */
			else {
				bio->read(&included, 1);
#ifdef DEBUG_LOSSLESS_T2
				 cblk->included = included;
#endif
			}

			/* if cblk not included */
			if (!included) {
				cblk->numPassesInPacket = 0;
				//GRK_INFO("included=%u ", included);
				continue;
			}

			/* if cblk not yet included --> zero-bitplane tagtree */
			if (!cblk->numSegments) {
				uint32_t K_msbs = 0;
				uint8_t value;

				// see Taubman + Marcellin page 388
				// loop below stops at (# of missing bit planes  + 1)
				prc->imsbtree->decompress(bio.get(), cblkno,
										K_msbs, &value);
				while (!value) {
					++K_msbs;
					prc->imsbtree->decompress(bio.get(), cblkno,
											K_msbs, &value);
				}
				assert(K_msbs >= 1);
				K_msbs--;

				if (K_msbs > band->numbps) {
					GRK_WARN(
							"More missing bit planes (%u) than band bit planes (%u).",
							K_msbs, band->numbps);
					cblk->numbps = band->numbps;
				} else {
					cblk->numbps = band->numbps - K_msbs;
				}
				// BIBO analysis gives sanity check on number of bit planes
				if (cblk->numbps
						> max_precision_jpeg_2000 + GRK_J2K_MAXRLVLS * 5) {
					GRK_WARN("Number of bit planes %u is impossibly large.",
							cblk->numbps);
					return false;
				}
				cblk->numlenbits = 3;
			}

			/* number of coding passes */
			bio->getnumpasses(&cblk->numPassesInPacket);
			bio->getcommacode(&increment);

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
						GRK_WARN(
								"Number of code block passes (%u) in packet is suspiciously large.",
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
						+ floorlog2<uint32_t>(seg->numPassesInPacket);
				if (bits_to_read > 32) {
					GRK_ERROR(
							"read_packet_header: too many bits in segment length ");
					return false;
				}
				bio->read(&seg->numBytesInPacket, bits_to_read);
#ifdef DEBUG_LOSSLESS_T2
			 cblk->packet_length_info.push_back(grk_packet_length_info(seg->numBytesInPacket,
							 cblk->numlenbits + floorlog2<uint32_t>(seg->numPassesInPacket)));
#endif
				/*
				 GRK_INFO(
				 "included=%u numPassesInPacket=%u increment=%u len=%u ",
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

	bio->inalign();

	header_data += bio->numbytes();

	/* EPH markers */
	if (p_tcp->csty & J2K_CP_CSTY_EPH) {
		if ((*modified_length_ptr
				- (uint32_t) (header_data - *header_data_start)) < 2U) {
			GRK_WARN("Not enough space for expected EPH marker");
		} else if ((*header_data) != 0xff || (*(header_data + 1) != 0x92)) {
			GRK_WARN("Expected EPH marker");
		} else {
			header_data += 2;
		}
	}

	auto header_length = (size_t) (header_data - *header_data_start);
	//GRK_INFO("hdrlen=%u ", header_length);
	//GRK_INFO("packet body\n");
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
			auto cblk = prc->dec + cblkno;
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
				size_t maxLen = src_buf->getRemainingLength();
				// Check possible overflow on segment length
				if (((seg->numBytesInPacket) > maxLen)) {
					GRK_WARN("read packet data:\nSegment segment length %u\n"
							"is greater than remaining total length of all segments (%u)\n"
							"for codeblock %u (layer=%u, prec=%u, band=%u, res=%u, comp=%u).\n"
							"Truncating packet data.", seg->numBytesInPacket,
							maxLen, cblkno, p_pi->layno, p_pi->precno, bandno, p_pi->resno, p_pi->compno);
					seg->numBytesInPacket = (uint32_t) maxLen;
				}
				//initialize dataindex to current contiguous size of code block
				if (seg->numpasses == 0)
					seg->dataindex = (uint32_t) cblk->compressedDataSize;

				// only add segment to seg_buffers if length is greater than zero
				if (seg->numBytesInPacket) {
					cblk->seg_buffers.push_back(new grk_buf(src_buf->get_global_ptr(),
							seg->numBytesInPacket, false));
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
			auto cblk = prc->dec + cblkno;
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
					GRK_ERROR(
							"skip: segment too long (%u) with max (%u) for codeblock %u (p=%u, b=%u, r=%u, c=%u)",
							seg->numBytesInPacket, max_length, cblkno,
							p_pi->precno, bandno, p_pi->resno, p_pi->compno);
					return false;
				}

				//GRK_INFO( "skip packet: p_data_read = %u, bytes in packet =  %u ",
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
