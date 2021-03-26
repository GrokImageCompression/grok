/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

//#define DEBUG_DECOMPRESS_PACKETS

namespace grk {

T2Decompress::T2Decompress(TileProcessor *tileProc) :
		tileProcessor(tileProc) {
}
void T2Decompress::initSegment(DecompressCodeblock *cblk, uint32_t index, uint8_t cblk_sty,
		bool first) {
	auto seg = cblk->getSegment(index);
	seg->clear();

	if (cblk_sty & GRK_CBLKSTY_TERMALL) {
		seg->maxpasses = 1;
	} else if (cblk_sty & GRK_CBLKSTY_LAZY) {
		if (first) {
			seg->maxpasses = 10;
		} else {
			auto last_seg = seg - 1;
			seg->maxpasses =
					((last_seg->maxpasses == 1) || (last_seg->maxpasses == 10)) ?	2 : 1;
		}
	} else {
		seg->maxpasses = max_passes_per_segment;
	}
}
bool T2Decompress::processPacket(TileCodingParams *tcp,
								PacketIter *currPi,
								SparseBuffer *srcBuf){
	auto tilec = tileProcessor->tile->comps + currPi->compno;
	auto tilecBuffer = tilec->getBuffer();
	auto packetInfo = tileProcessor->packetLengthCache.next();
	auto res = tilec->tileCompResolution + currPi->resno;
	auto skipPacket = currPi->layno >= tcp->num_layers_to_decompress
								|| currPi->resno >= tilec->resolutions_to_decompress;
	if (!skipPacket) {
		if (!tilec->isWholeTileDecoding()) {
			skipPacket = true;
			for (uint8_t bandIndex = 0;	bandIndex < res->numTileBandWindows; ++bandIndex) {
				auto band = res->tileBand + bandIndex;
				if (band->isEmpty())
					continue;
				auto paddedBandWindow = tilecBuffer->getPaddedBandWindow(currPi->resno, band->orientation);
				auto prec = band->generatePrecinctBounds(currPi->precinctIndex,
														res->precinctStart,
														res->precinctExpn,
														res->precinctGridWidth);
				if (paddedBandWindow->non_empty_intersection(&prec)) {
					skipPacket = false;
					break;
				}
			}
		}
	}
	if (!skipPacket || !packetInfo->packetLength) {
		for (uint32_t bandIndex = 0;	bandIndex < res->numTileBandWindows; ++bandIndex) {
			auto band = res->tileBand + bandIndex;
			if (band->isEmpty())
				continue;
			if (!band->createPrecinct(false,
										currPi->precinctIndex,
										res->precinctStart,
										res->precinctExpn,
										res->precinctGridWidth,
										res->cblkExpn))
				return false;
		}
	}
	if (!skipPacket) {
		if (!decompressPacket(tcp, currPi, srcBuf,packetInfo,false))
			return false;
		tilec->resolutions_decompressed = std::max<uint8_t>(currPi->resno,tilec->resolutions_decompressed);
		tileProcessor->tile->numDecompressedPackets++;
	} else {
		if (packetInfo->packetLength)
			srcBuf->incrementCurrentChunkOffset(packetInfo->packetLength);
		else
			if (!decompressPacket(tcp, currPi, srcBuf,packetInfo,true))
				return false;
	}
	tileProcessor->tile->numProcessedPackets++;
	return true;
}
bool T2Decompress::decompressPackets(uint16_t tile_no,
										SparseBuffer *srcBuf,
										bool *truncated) {
	auto cp = tileProcessor->m_cp;
	auto tcp = cp->tcps + tile_no;
	*truncated = false;
	PacketManager packetManager(false,tileProcessor->headerImage, cp, tile_no,FINAL_PASS);
	tileProcessor->packetLengthCache.rewind();
	for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
		auto currPi = packetManager.getPacketIter(pino);
		if (currPi->prog.progression == GRK_PROG_UNKNOWN) {
			GRK_ERROR("decompressPackets: Unknown progression order");
			return false;
		}
		while (currPi->next()) {
			if (srcBuf->getCurrentChunkLength() == 0){
				GRK_WARN("Tile %d is truncated.", tile_no);
				*truncated = true;
				break;
			}
			try {
				if (!processPacket(tcp, currPi, srcBuf)) {
					return false;
				}
			} 	catch (TruncatedPacketHeaderException &tex){
				GRK_UNUSED(tex);
				GRK_WARN("Truncated packet: tile=%d component=%02d resolution=%02d precinct=%03d layer=%02d",
				 tile_no, currPi->compno, currPi->resno,
				 currPi->precinctIndex, currPi->layno);
				break;
			}
#ifdef DEBUG_DECOMPRESS_PACKETS
			GRK_INFO("packet cmptno=%02d rlvlno=%02d prcno=%03d layrno=%02d -> %s",
			currPi->compno, currPi->resno,
			currPi->precinctIndex, currPi->layno, skip ? "skipped" : "decompressed");
			GRK_INFO("T2Decompress Packet length: %u", bytesRead);
			if (pltMarkerLen) {
				if (bytesRead != pltMarkerLen)
					GRK_WARN("T2Decompress: bytes read %d != PLT Packet length: %u", pltMarkerLen);
			}
#endif
		}
		if (*truncated)
			break;
	}
	if (tileProcessor->tile->numDecompressedPackets == 0)
		GRK_WARN("T2Decompress: no packets for tile %d were successfully read",tile_no+1);
	return tileProcessor->tile->numDecompressedPackets > 0;
}
bool T2Decompress::decompressPacket(TileCodingParams *tcp,
									const PacketIter *pi,
									SparseBuffer *srcBuf,
									PacketInfo *packetInfo,
									bool skipData) {
	auto tile = tileProcessor->tile;
	auto res = tile->comps[pi->compno].tileCompResolution + pi->resno;
	bool dataPresent;
	uint32_t packetDataBytes = 0;
	uint32_t packetBytes = 0;
	if (packetInfo->headerLength){
		srcBuf->incrementCurrentChunkOffset(packetInfo->headerLength);
		packetDataBytes = packetInfo->getPacketDataLength();
		dataPresent = packetDataBytes > 0;
		packetBytes = packetInfo->headerLength + packetDataBytes;
	} else {
		if (!readPacketHeader(tcp, pi, &dataPresent, srcBuf, &packetBytes,&packetDataBytes))
			return false;
		packetInfo->headerLength = packetBytes;
		packetInfo->packetLength = packetBytes + packetDataBytes;
		packetBytes = packetInfo->packetLength;
	}
	if (dataPresent) {
		if (skipData || packetInfo->parsedData){
			srcBuf->incrementCurrentChunkOffset(packetDataBytes);
		} else {
			if (!readPacketData(res, pi, srcBuf))
				return false;
			packetInfo->parsedData = true;
		}
	}

	return true;
}
bool T2Decompress::readPacketHeader(TileCodingParams *p_tcp,
									const PacketIter *p_pi,
									bool *p_is_data_present,
									SparseBuffer *srcBuf,
									uint32_t *dataRead,
									uint32_t *packetDataBytes) {
	auto tilePtr = tileProcessor->tile;
	auto res = tilePtr->comps[p_pi->compno].tileCompResolution + p_pi->resno;
	auto p_src_data = srcBuf->getCurrentChunkPtr();
	size_t available_bytes = srcBuf->getCurrentChunkLength();
	auto active_src = p_src_data;

	if (p_tcp->csty & J2K_CP_CSTY_SOP) {
		if (available_bytes < 6)
			throw TruncatedPacketHeaderException();
		uint16_t marker = (uint16_t)(((uint16_t)(*active_src) << 8) | (uint16_t)(*(active_src + 1)));
		if (marker != J2K_MS_SOP) {
			GRK_WARN("Expected SOP marker, but found 0x%x", marker);
		} else {
			uint16_t numIteratedPackets = (uint16_t) (((uint16_t) active_src[4] << 8) | active_src[5]);
			if (numIteratedPackets != (tilePtr->numProcessedPackets % 0x10000)) {
				GRK_ERROR("SOP marker packet counter %u does not match expected counter %u",
						numIteratedPackets, tilePtr->numProcessedPackets);
				return false;
			}
			active_src += 6;
			available_bytes -= 6;
		}
	}
	auto header_data_start = &active_src;
	auto remaining_length 	= &available_bytes;
	auto cp = tileProcessor->m_cp;
	if (cp->ppm_marker) {
		if (tileProcessor->m_tileIndex >= cp->ppm_marker->m_tile_packet_headers.size()){
			GRK_ERROR("PPM marker has no packed packet header data for tile %d",
					tileProcessor->m_tileIndex+1);
			return false;
		}
		auto tile_packet_header = &cp->ppm_marker->m_tile_packet_headers[tileProcessor->m_tileIndex];
		header_data_start 	= &tile_packet_header->buf;
		remaining_length 	= &tile_packet_header->len;
	} else if (p_tcp->ppt) {
		header_data_start 	= &p_tcp->ppt_data;
		remaining_length 	= &p_tcp->ppt_len;
	}
	if (*remaining_length==0)
		throw TruncatedPacketHeaderException();
	auto header_data 	= *header_data_start;
	uint32_t present = 0;
	std::unique_ptr<BitIO> bio(new BitIO(header_data, *remaining_length, false));
	auto tccp = p_tcp->tccps + p_pi->compno;
	try {
		bio->read(&present, 1);
		//GRK_INFO("present=%u ", present);
		if (present) {
			for (uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex) {
				auto band = res->tileBand + bandIndex;
				if (band->isEmpty())
					continue;
				auto prc = band->getPrecinct(p_pi->precinctIndex);
				if (!prc)
					continue;
				for (uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++) {
					uint32_t included = 0, increment = 0;
					auto cblk = prc->getDecompressedBlockPtr(cblkno);

					/* if cblk not yet included before --> inclusion tagtree */
					if (!cblk->numlenbits) {
						uint64_t value;
						prc->getInclTree()->decodeValue(bio.get(), cblkno, p_pi->layno + 1, &value);
						if (value != tag_tree_uninitialized_node_value	&& value != p_pi->layno) {
							GRK_WARN("Tile number: %u",tileProcessor->m_tileIndex+1);
							std::string msg =
								  "Illegal inclusion tag tree found when decoding packet header.\n";
							msg +="This problem can occur if empty packets are used (i.e., packets whose first header\n";
							msg +="bit is 0) and the value coded by the inclusion tag tree in a subsequent packet\n";
							msg +="is not exactly equal to the index of the quality layer in which each code-block\n";
							msg +="makes its first contribution.  Such an error may occur from a\n";
							msg +="mis-interpretation of the standard.  The problem may also occur as a result of\n";
							msg +="a corrupted code-stream";
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
					if (!included) {
						cblk->numPassesInPacket = 0;
						//GRK_INFO("included=%u ", included);
						continue;
					}
					/* if cblk not yet included --> zero-bitplane tagtree */
					if (!cblk->numlenbits) {
						uint32_t K_msbs = 0;
						uint8_t value;

						// see Taubman + Marcellin page 388
						// loop below stops at (# of missing bit planes  + 1)
						prc->getImsbTree()->decompress(bio.get(), cblkno,K_msbs, &value);
						while (!value) {
							++K_msbs;
							prc->getImsbTree()->decompress(bio.get(), cblkno,K_msbs, &value);
						}
						assert(K_msbs >= 1);
						K_msbs--;

						if (K_msbs > band->numbps) {
							GRK_WARN("More missing code block bit planes (%u) than band bit planes (%u).",
									K_msbs, band->numbps);
							// since we don't know how many bit planes are in this block, we
							// set numbps to max - the t1 decoder will sort it out
							cblk->numbps = max_precision_jpeg_2000 * 3 - 2;
						} else {
							cblk->numbps = band->numbps - K_msbs;
						}
						// BIBO analysis gives sanity check on number of bit planes
						if (cblk->numbps > max_precision_jpeg_2000 + GRK_J2K_MAXRLVLS * 5) {
							GRK_WARN("Number of bit planes %u is impossibly large.",
									cblk->numbps);
							return false;
						}
						cblk->numlenbits = 3;
					}
					bio->getnumpasses(&cblk->numPassesInPacket);
					bio->getcommacode(&increment);
					cblk->numlenbits += increment;
					uint32_t segno = 0;
					if (!cblk->getNumSegments()) {
						initSegment(cblk, 0,tccp->cblk_sty, true);
					} else {
						segno = cblk->getNumSegments() - 1;
						if (cblk->getSegment(segno)->numpasses	== cblk->getSegment(segno)->maxpasses) {
							++segno;
							initSegment(cblk, segno,tccp->cblk_sty, false);
						}
					}
					auto blockPassesInPacket = (int32_t) cblk->numPassesInPacket;
					do {
						auto seg = cblk->getSegment(segno);
						/* sanity check when there is no mode switch */
						if (seg->maxpasses == max_passes_per_segment) {
							if (blockPassesInPacket	> (int32_t) max_passes_per_segment) {
								GRK_WARN("Number of code block passes (%u) in packet is suspiciously large.",
										blockPassesInPacket);
								// TODO - we are truncating the number of passes at an arbitrary value of
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
						uint32_t bits_to_read = cblk->numlenbits+ floorlog2<uint32_t>(seg->numPassesInPacket);
						if (bits_to_read > 32) {
							GRK_ERROR("readPacketHeader: too many bits in segment length ");
							return false;
						}
						bio->read(&seg->numBytesInPacket, bits_to_read);
						if (packetDataBytes)
							*packetDataBytes += seg->numBytesInPacket;
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
							initSegment(cblk, segno,tccp->cblk_sty, false);
						}
					} while (blockPassesInPacket > 0);
				}
			}
		}
		bio->inalign();
		header_data += bio->numbytes();
	} catch (InvalidMarkerException &ex){
		GRK_UNUSED(ex);
		return false;
	}
	/* EPH markers */
	if (p_tcp->csty & J2K_CP_CSTY_EPH) {
		if ((*remaining_length	- (uint32_t) (header_data - *header_data_start)) < 2U)
			//GRK_WARN("Not enough space for expected EPH marker");
			throw TruncatedPacketHeaderException();
		uint16_t marker = (uint16_t)(((uint16_t)(*header_data) << 8) | (uint16_t)(*(header_data + 1)));
		if (marker != J2K_MS_EPH) {
			GRK_ERROR("Expected EPH marker, but found 0x%x",marker);
			return false;
		} else {
			header_data += 2;
		}
	}

	auto header_length = (size_t) (header_data - *header_data_start);
	//GRK_INFO("hdrlen=%u ", header_length);
	//GRK_INFO("packet body\n");
	*remaining_length  	-= header_length;
	*header_data_start  += header_length;
	*p_is_data_present  = present;
	*dataRead 		    = (uint32_t) (active_src - p_src_data);
	srcBuf->incrementCurrentChunkOffset(*dataRead);

	if (!present && !*dataRead)
		throw TruncatedPacketHeaderException();

	return true;
}
bool T2Decompress::readPacketData(Resolution *res,
									const PacketIter *p_pi,
									SparseBuffer *srcBuf) {
	for (uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex) {
		auto band = res->tileBand + bandIndex;
		if (band->isEmpty())
			continue;
		auto prc = band->getPrecinct(p_pi->precinctIndex);
		if (!prc)
			continue;
		for (uint64_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno) {
			auto cblk = prc->getDecompressedBlockPtr(cblkno);
			if (!cblk->numPassesInPacket)
				continue;

			auto seg = cblk->getCurrentSegment();
			if (!seg || (seg->numpasses == seg->maxpasses))
				seg = cblk->nextSegment();
			uint32_t numPassesInPacket = cblk->numPassesInPacket;
			do {
				size_t maxLen = srcBuf->getCurrentChunkLength();
				if (maxLen == 0)
					return true;
				if (((seg->numBytesInPacket) > maxLen)) {
					// HT doesn't tolerate truncated code blocks since decoding runs both forward and reverse.
					// So, in this case, we ignore the entire code block
					if (tileProcessor->m_cp->tcps[0].getIsHT())
						cblk->cleanUpSegBuffers();
					seg->numBytesInPacket = 0;
					seg->numpasses = 0;
					break;
				}
				if (seg->numBytesInPacket) {
					// sanity check on seg->numBytesInPacket
					if (UINT_MAX - seg->numBytesInPacket <  seg->len){
						GRK_ERROR("Segment packet length %d plus total segment length %d must be less than 2^32",
								seg->numBytesInPacket, seg->len);
						return false;
					}
					size_t maxSegmentLength = srcBuf->getCurrentChunkLength();
					// correct for truncated packet
					if (seg->numBytesInPacket > maxSegmentLength)
						seg->numBytesInPacket = (uint32_t)maxSegmentLength;
					cblk->seg_buffers.push_back(new grkBufferU8(srcBuf->getCurrentChunkPtr(),seg->numBytesInPacket, false));
					srcBuf->incrementCurrentChunkOffset(seg->numBytesInPacket);
					cblk->compressedStream.len 	+= seg->numBytesInPacket;
					seg->len 					+= seg->numBytesInPacket;
				}
				seg->numpasses 		+= seg->numPassesInPacket;
				numPassesInPacket 	-= seg->numPassesInPacket;
				if (numPassesInPacket > 0)
					seg = cblk->nextSegment();
			} while (numPassesInPacket > 0);
		} /* next code_block */
	}

	return true;
}


}
