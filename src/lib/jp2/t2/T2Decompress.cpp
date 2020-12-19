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

const bool debugDecompressPackets = false;

namespace grk {

T2Decompress::T2Decompress(TileProcessor *tileProc) :
		tileProcessor(tileProc) {
}

bool T2Decompress::decompress_packets(uint16_t tile_no, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {

	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->image;
	auto tcp = cp->tcps + tile_no;
	auto p_tile = tileProcessor->tile;
	std::vector<uint8_t*> include;
	auto pi = pi_create_decompress(image, cp, tile_no, &include);
	if (!pi)
		return false;

	auto packetLengths = tileProcessor->plt_markers;
	// we don't currently support PLM markers,
	// so we disable packet length markers if we have both PLT and PLM
	bool usePlt = packetLengths && !cp->plm_markers;
	if (usePlt)
		packetLengths->getInit();
	bool truncatedTile = false;
	for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
		/* if the resolution needed is too low, one dim of the tilec
		 * could be equal to zero
		 * and no packets are used to decompress this resolution and
		 * current_pi->resno is always >=
		 * tile->comps[current_pi->compno].resolutions_to_decompress
		 * and no l_img_comp->resno_decoded are computed
		 */
		bool *first_pass_failed = new bool[image->numcomps];
		for (size_t k = 0; k < image->numcomps; ++k)
			first_pass_failed[k] = true;

		auto current_pi = pi + pino;
		current_pi->include = pi->include;
		if (current_pi->poc.prg == GRK_PROG_UNKNOWN) {
			pi_destroy(pi);
			delete[] first_pass_failed;
			GRK_ERROR("decompress_packets: Unknown progression order");
			return false;
		}
		while (pi_next(current_pi)) {
			if (src_buf->get_cur_chunk_len() == 0){
				GRK_WARN("Tile %d is truncated.", tile_no);
				truncatedTile = true;
				break;
			}
			auto tilec = p_tile->comps + current_pi->compno;
			auto skip_the_packet = current_pi->layno
					>= tcp->num_layers_to_decompress
					|| current_pi->resno >= tilec->resolutions_to_decompress;

			uint32_t pltMarkerLen = 0;
			if (usePlt)
				pltMarkerLen = packetLengths->getNext();
			auto res = tilec->resolutions + current_pi->resno;
			// create precincts
			for (uint32_t bandIndex = 0;	bandIndex < res->numBandWindows; ++bandIndex) {
				auto band = res->band + bandIndex;
				if (!band->createPrecinct(false,
									current_pi->precinctIndex,
									res->precinct_start,
									res->precinct_expn,
									res->pw,
									res->cblk_expn,
									res->current_plugin_tile))
					return false;

			}
			if (!skip_the_packet) {
				if (!tilec->isWholeTileDecoding()) {
					skip_the_packet = true;
					for (uint8_t bandIndex = 0;	bandIndex < res->numBandWindows; ++bandIndex) {
						auto band = res->band + bandIndex;
						auto prec = band->getPrecinct(current_pi->precinctIndex);
						if (prec && tilec->subbandIntersectsAOI(current_pi->resno,band->orientation, prec)) {
							skip_the_packet = false;
							break;
						}
					}
				}
			}

			uint64_t nb_bytes_read = 0;
			try {
				if (!skip_the_packet) {
					first_pass_failed[current_pi->compno] = false;

					if (!decompress_packet(tcp, current_pi, src_buf, &nb_bytes_read)) {
						pi_destroy(pi);
						delete[] first_pass_failed;
						return false;
					}
					tilec->resolutions_decompressed = std::max<uint8_t>(current_pi->resno,
							tilec->resolutions_decompressed);
				} else {
					if (pltMarkerLen) {
						nb_bytes_read = pltMarkerLen;
						src_buf->incr_cur_chunk_offset(nb_bytes_read);
					} else if (!skip_packet(tcp, current_pi, src_buf,
							&nb_bytes_read)) {
						pi_destroy(pi);
						delete[] first_pass_failed;
						return false;
					}
				}
				p_tile->packno++;
			} 	catch (TruncatedPacketHeaderException &tex){
				GRK_WARN("Truncated packet: tile=%d component=%02d resolution=%02d precinct=%03d layer=%02d",
				 tile_no, current_pi->compno, current_pi->resno,
				 current_pi->precinctIndex, current_pi->layno);
				break;
			}
			if (debugDecompressPackets) {
				 GRK_INFO("packet cmptno=%02d rlvlno=%02d prcno=%03d layrno=%02d -> %s",
				 current_pi->compno, current_pi->resno,
				 current_pi->precinctIndex, current_pi->layno, skip_the_packet ? "skipped" : "decompressed");
			}
			if (first_pass_failed[current_pi->compno]) {
				if (tilec->resolutions_decompressed == 0) {
					tilec->resolutions_decompressed =
							(uint8_t)(p_tile->comps[current_pi->compno].resolutions_to_decompress- 1);
				}
			}
			if (debugDecompressPackets) {
				GRK_INFO("T2Decompress Packet length: %u", nb_bytes_read);
				if (pltMarkerLen) {
					if (nb_bytes_read != pltMarkerLen)
						GRK_WARN("T2Decompress PLT Packet length: %u", pltMarkerLen);
				}
			}
			*p_data_read += nb_bytes_read;
		}
		delete[] first_pass_failed;
		if (truncatedTile)
			break;
	}
	pi_destroy(pi);
	if (p_tile->packno == 0)
		GRK_ERROR("T2Decompress: no packets in tile were successfully decompressed");
	return p_tile->packno > 0;
}


bool T2Decompress::decompress_packet(TileCodingParams *p_tcp, PacketIter *p_pi, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	auto p_tile = tileProcessor->tile;
	auto res = &p_tile->comps[p_pi->compno].resolutions[p_pi->resno];
	bool read_data;
	uint64_t nb_bytes_read = 0;
	uint64_t nb_total_bytes_read = 0;
	*p_data_read = 0;
	if (!read_packet_header(p_tcp, p_pi, &read_data, src_buf, &nb_bytes_read))
		return false;
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

void T2Decompress::init_seg(DecompressCodeblock *cblk, uint32_t index, uint8_t cblk_sty,
		bool first) {
	uint32_t nb_segs = index + 1;

	if (nb_segs > cblk->numSegmentsAllocated) {
		auto new_segs = new Segment[cblk->numSegmentsAllocated
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

	if (cblk_sty & GRK_CBLKSTY_TERMALL) {
		seg->maxpasses = 1;
	} else if (cblk_sty & GRK_CBLKSTY_LAZY) {
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
}


bool T2Decompress::read_packet_header(TileCodingParams *p_tcp, PacketIter *p_pi,
		bool *p_is_data_present, ChunkBuffer *src_buf, uint64_t *p_data_read) {
	auto p_tile = tileProcessor->tile;
	auto res = &p_tile->comps[p_pi->compno].resolutions[p_pi->resno];
	auto p_src_data = src_buf->get_cur_chunk_ptr();
	uint64_t available_bytes = src_buf->get_cur_chunk_len();
	auto active_src = p_src_data;

	if (p_pi->layno == 0) {
		/* reset tagtrees */
		for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			auto band = res->band + bandIndex;
			if (band->isEmpty())
				continue;
			auto prc = band->getPrecinct(p_pi->precinctIndex);
			if (!prc)
				continue;
			if (p_pi->precinctIndex >= (band->numPrecincts)) {
				GRK_ERROR("Invalid precinct");
				return false;
			}
			prc->init(false, res->cblk_expn, tileProcessor->current_plugin_tile);
			if (prc->getInclTree())
				prc->getInclTree()->reset();
			if (prc->getImsbTree())
				prc->getImsbTree()->reset();
			for (uint64_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno) {
				auto cblk = prc->getDecompressedBlockPtr() + cblkno;
				cblk->numSegments = 0;
			}
		}
	}

	/* SOP markers */
	if (p_tcp->csty & J2K_CP_CSTY_SOP) {
		if (available_bytes < 6) {
			//GRK_WARN("Not enough space for expected SOP marker");
			throw TruncatedPacketHeaderException();
		} else if ((*active_src) != 0xff || (*(active_src + 1) != 0x91)) {
			GRK_WARN("Expected SOP marker");
		} else {
			uint16_t packno = (uint16_t) (((uint16_t) active_src[4] << 8)
					| active_src[5]);
			if (packno != (p_tile->packno % 0x10000)) {
				GRK_ERROR("SOP marker packet counter %u does not match expected counter %u",
						packno, p_tile->packno);
				return false;
			}
			active_src += 6;
			available_bytes -= 6;
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
		modified_length_ptr = &remaining_length;
	} else if (p_tcp->ppt) { /* PPT */
		header_data_start = &(p_tcp->ppt_data);
		header_data = *header_data_start;
		modified_length_ptr = &(p_tcp->ppt_len);
	} else { /* Normal Case */
		header_data_start = &active_src;
		header_data = *header_data_start;
		remaining_length = available_bytes;
		modified_length_ptr = &remaining_length;
	}

	if (*modified_length_ptr==0)
		throw TruncatedPacketHeaderException();

	uint32_t present = 0;
	std::unique_ptr<BitIO> bio(new BitIO(header_data, *modified_length_ptr, false));
	try {
		bio->read(&present, 1);

		//GRK_INFO("present=%u ", present);
		if (present) {
			for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
				auto band = res->band + bandIndex;
				if (band->isEmpty())
					continue;
				auto prc = band->getPrecinct(p_pi->precinctIndex);
				if (!prc)
					continue;
				for (uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++) {
					uint32_t included = 0, increment = 0;
					auto cblk = prc->getDecompressedBlockPtr() + cblkno;

					/* if cblk not yet included before --> inclusion tagtree */
					if (!cblk->numSegments) {
						uint64_t value;
						prc->getInclTree()->decodeValue(bio.get(), cblkno,
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
						prc->getImsbTree()->decompress(bio.get(), cblkno,
												K_msbs, &value);
						while (!value) {
							++K_msbs;
							prc->getImsbTree()->decompress(bio.get(), cblkno,
													K_msbs, &value);
						}
						assert(K_msbs >= 1);
						K_msbs--;

						if (K_msbs > band->numbps) {
							GRK_WARN("More missing bit planes (%u) than band bit planes (%u). Ignoring code block",
									K_msbs, band->numbps);
							//suppress this code block
							cblk->numbps = 0;
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
						init_seg(cblk, segno,p_tcp->tccps[p_pi->compno].cblk_sty, true);
					} else {
						segno = cblk->numSegments - 1;
						if (cblk->segs[segno].numpasses
								== cblk->segs[segno].maxpasses) {
							++segno;
							init_seg(cblk, segno,p_tcp->tccps[p_pi->compno].cblk_sty, false);
						}
					}
					auto blockPassesInPacket = (int32_t) cblk->numPassesInPacket;
					do {
						auto seg = cblk->segs + segno;
						/* sanity check when there is no mode switch */
						if (seg->maxpasses == max_passes_per_segment) {
							if (blockPassesInPacket
									> (int32_t) max_passes_per_segment) {
								GRK_WARN("Number of code block passes (%u) in packet is suspiciously large.",
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
							GRK_ERROR("read_packet_header: too many bits in segment length ");
							return false;
						}
						bio->read(&seg->numBytesInPacket, bits_to_read);
		#ifdef DEBUG_LOSSLESS_T2
					 cblk->packet_length_info.push_back(PacketLengthInfo(seg->numBytesInPacket,
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
							init_seg(cblk, segno,p_tcp->tccps[p_pi->compno].cblk_sty, false);
						}
					} while (blockPassesInPacket > 0);
				}
			}
		}

		bio->inalign();
		header_data += bio->numbytes();

	} catch (InvalidMarkerException &ex){
		return false;
	}

	/* EPH markers */
	if (p_tcp->csty & J2K_CP_CSTY_EPH) {
		if ((*modified_length_ptr
				- (uint32_t) (header_data - *header_data_start)) < 2U) {
			//GRK_WARN("Not enough space for expected EPH marker");
			throw TruncatedPacketHeaderException();
		} else if ((*header_data) != 0xff || (*(header_data + 1) != 0x92)) {
			GRK_ERROR("Expected EPH marker");
			return false;
		} else {
			header_data += 2;
		}
	}

	auto header_length = (size_t) (header_data - *header_data_start);
	//GRK_INFO("hdrlen=%u ", header_length);
	//GRK_INFO("packet body\n");
	*modified_length_ptr -= header_length;
	*header_data_start += header_length;
	*p_is_data_present = present;
	*p_data_read = (uint32_t) (active_src - p_src_data);
	src_buf->incr_cur_chunk_offset(*p_data_read);

	if (!present && !*p_data_read)
		throw TruncatedPacketHeaderException();

	return true;
}

bool T2Decompress::read_packet_data(Resolution *res, PacketIter *p_pi,
		ChunkBuffer *src_buf, uint64_t *p_data_read) {
	for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
		auto band = res->band + bandIndex;
		auto prc = band->getPrecinct(p_pi->precinctIndex);
		if (!prc)
			continue;
		for (uint64_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno) {
			auto cblk = prc->getDecompressedBlockPtr() + cblkno;
			if (!cblk->numPassesInPacket) {
				++cblk;
				continue;
			}
			Segment *seg = nullptr;
			if (!cblk->numSegments) {
				seg = cblk->segs;
				++cblk->numSegments;
				cblk->compressedStream.len = 0;
			} else {
				seg = &cblk->segs[cblk->numSegments - 1];
				if (seg->numpasses == seg->maxpasses) {
					++seg;
					++cblk->numSegments;
				}
			}

			uint32_t numPassesInPacket = cblk->numPassesInPacket;
			do {
				size_t maxLen = src_buf->get_cur_chunk_len();
				if (maxLen == 0)
					return true;
				// reject truncated packet
				if (((seg->numBytesInPacket) > maxLen)) {
					// HT doesn't tolerate truncated code blocks since decoding runs both forward and reverse.
					// So, in this case, we ignore the entire code block
					if (tileProcessor->m_cp->tcps[0].isHT){
						cblk->numSegments = 0;
						cblk->cleanup_seg_buffers();
					}
					seg->numBytesInPacket = 0;
					seg->numpasses = 0;
					break;
				}
				//initialize dataindex to current contiguous size of code block
				if (seg->numpasses == 0)
					seg->dataindex = (uint32_t) cblk->compressedStream.len;

				// only add segment to seg_buffers if length is greater than zero
				if (seg->numBytesInPacket) {
					// sanity check on seg->numBytesInPacket
					if (UINT_MAX - seg->numBytesInPacket <  seg->len){
						GRK_ERROR("Segment packet length %d plus total segment length %d must be less than 2^32",
								seg->numBytesInPacket, seg->len);
						return false;
					}
					size_t max_seg_len = src_buf->get_cur_chunk_len();
					// correct for truncated packet
					if (seg->numBytesInPacket > max_seg_len)
						seg->numBytesInPacket = (uint32_t)max_seg_len;
					cblk->seg_buffers.push_back(new grk_buf(src_buf->get_cur_chunk_ptr(),
							seg->numBytesInPacket, false));
					*(p_data_read) += seg->numBytesInPacket;
					src_buf->incr_cur_chunk_offset(seg->numBytesInPacket);
					cblk->compressedStream.len += seg->numBytesInPacket;
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
bool T2Decompress::skip_packet(TileCodingParams *p_tcp, PacketIter *p_pi, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	uint64_t max_length = (uint64_t) src_buf->get_cur_chunk_len();
	auto p_tile = tileProcessor->tile;

	*p_data_read = 0;
	uint64_t nb_bytes_read = 0;
	bool read_data;
	if (!read_packet_header(p_tcp, p_pi, &read_data, src_buf, &nb_bytes_read))
		return false;
	uint64_t nb_totabytes_read = nb_bytes_read;
	max_length -= nb_bytes_read;

	/* we should read data for the packet */
	if (read_data) {
		nb_bytes_read = 0;
		if (!skip_packet_data(&p_tile->comps[p_pi->compno].resolutions[p_pi->resno], p_pi,
				&nb_bytes_read, max_length)) {
			return false;
		}
		src_buf->incr_cur_chunk_offset(nb_bytes_read);
		nb_totabytes_read += nb_bytes_read;
	}
	*p_data_read = nb_totabytes_read;

	return true;
}

bool T2Decompress::skip_packet_data(Resolution *res, PacketIter *p_pi,
		uint64_t *p_data_read, uint64_t max_length) {
	*p_data_read = 0;
	for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
		if (max_length - *p_data_read == 0)
			return true;
		auto band = res->band + bandIndex;
		if (band->isEmpty())
			continue;

		auto prc = band->getPrecinct(p_pi->precinctIndex);
		if (!prc)
			continue;
		for (uint64_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno) {
			if (max_length - *p_data_read == 0)
				return true;
			auto cblk = prc->getDecompressedBlockPtr() + cblkno;
			if (!cblk->numPassesInPacket) {
				/* nothing to do */
				++cblk;
				continue;
			}
			Segment *seg = nullptr;
			if (!cblk->numSegments) {
				seg = cblk->segs;
				++cblk->numSegments;
				cblk->compressedStream.len = 0;
			} else {
				seg = &cblk->segs[cblk->numSegments - 1];
				if (seg->numpasses == seg->maxpasses) {
					++seg;
					++cblk->numSegments;
				}
			}
			uint32_t numPassesInPacket = cblk->numPassesInPacket;
			do {
				if (max_length - *p_data_read == 0)
					return true;
				/* Check possible overflow  */
				if (((*p_data_read + seg->numBytesInPacket) > max_length)) {
					GRK_WARN("skip: segment bytes (%u) too large for remaining stream bytes (%u) in codeblock %u (p=%u, b=%u, r=%u, c=%u). Truncating segment",
							seg->numBytesInPacket, max_length - *p_data_read, cblkno,
							p_pi->precinctIndex, bandIndex, p_pi->resno, p_pi->compno);
					seg->numBytesInPacket = (uint32_t)(max_length - *p_data_read);
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
