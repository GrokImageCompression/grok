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


T2Encode::T2Encode(TileProcessor *tileProc) :
		tileProcessor(tileProc) {
}


bool T2Encode::encode_packets(uint16_t tile_no, uint16_t max_layers,
		BufferedStream *stream, uint32_t *p_data_written,
		uint32_t tp_num, uint32_t tp_pos,
		uint32_t pino) {
	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->image;
	auto p_tile = tileProcessor->tile;
	auto tcp = &cp->tcps[tile_no];
	uint32_t nb_pocs = tcp->numpocs + 1;

	auto pi = pi_initialise_encode(image, cp, tile_no, FINAL_PASS);
	if (!pi)
		return false;

	pi_init_encode(pi, cp, tile_no, pino, tp_num, tp_pos, FINAL_PASS);

	auto current_pi = &pi[pino];
	if (current_pi->poc.prg == GRK_PROG_UNKNOWN) {
		pi_destroy(pi, nb_pocs);
		GRK_ERROR("encode_packets: Unknown progression order");
		return false;
	}
	while (pi_next(current_pi)) {
		if (current_pi->layno < max_layers) {
			uint32_t nb_bytes = 0;
			if (!encode_packet(tcp, current_pi, stream, &nb_bytes)) {
				pi_destroy(pi, nb_pocs);
				return false;
			}
			*p_data_written += nb_bytes;
			/* << INDEX */
			++p_tile->packno;
		}
	}
	pi_destroy(pi, nb_pocs);

	return true;
}

bool T2Encode::encode_packets_simulate(uint16_t tile_no, uint16_t max_layers,
		uint32_t *all_packets_len, uint32_t max_len, uint32_t tp_pos,
		PacketLengthMarkers *markers) {

	assert(all_packets_len);
	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->image;
	auto tcp = cp->tcps + tile_no;
	uint32_t pocno = (cp->rsiz == GRK_PROFILE_CINEMA_4K) ? 2 : 1;
	uint32_t max_comp =
			cp->m_coding_params.m_enc.m_max_comp_size > 0 ? image->numcomps : 1;
	uint32_t nb_pocs = tcp->numpocs + 1;

	auto pi = pi_initialise_encode(image, cp, tile_no, THRESH_CALC);
	if (!pi)
		return false;

	*all_packets_len = 0;

	tileProcessor->m_packetTracker.clear();
#ifdef DEBUG_ENCODE_PACKETS
    GRK_INFO("simulate encode packets for layers below layno %u", max_layers);
#endif
	for (uint32_t compno = 0; compno < max_comp; ++compno) {
		uint64_t comp_len = 0;
		for (uint32_t poc = 0; poc < pocno; ++poc) {
			auto current_pi = pi + poc;
			uint32_t tp_num = compno;
			pi_init_encode(pi, cp, tile_no, poc, tp_num, tp_pos, THRESH_CALC);

			if (current_pi->poc.prg == GRK_PROG_UNKNOWN) {
				pi_destroy(pi, nb_pocs);
				GRK_ERROR(
						"decode_packets_simulate: Unknown progression order");
				return false;
			}
			while (pi_next(current_pi)) {
				if (current_pi->layno < max_layers) {
					uint32_t bytesInPacket = 0;

					if (!encode_packet_simulate(tcp, current_pi, &bytesInPacket,
							max_len, markers)) {
						pi_destroy(pi, nb_pocs);
						return false;
					}

					comp_len += bytesInPacket;
					max_len -= bytesInPacket;
					*all_packets_len += bytesInPacket;
				}
			}

			if (cp->m_coding_params.m_enc.m_max_comp_size) {
				if (comp_len > cp->m_coding_params.m_enc.m_max_comp_size) {
					pi_destroy(pi, nb_pocs);
					return false;
				}
			}
		}
	}
	pi_destroy(pi, nb_pocs);
	return true;
}

//--------------------------------------------------------------------------------------------------
bool T2Encode::encode_packet(TileCodingParams *tcp, PacketIter *pi,
		BufferedStream *stream, uint32_t *packet_bytes_written) {
	assert(stream);
	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint64_t precno = pi->precno;
	uint32_t layno = pi->layno;
	auto tile = tileProcessor->tile;
	auto tilec = &tile->comps[compno];
	auto res = &tilec->resolutions[resno];
	size_t stream_start = stream->tell();

	if (tileProcessor->m_packetTracker.is_packet_encoded(compno, resno, precno,
			layno))
		return true;
	tileProcessor->m_packetTracker.packet_encoded(compno, resno, precno, layno);

#ifdef DEBUG_ENCODE_PACKETS
    GRK_INFO("encode packet compono=%u, resno=%u, precno=%u, layno=%u",
             compno, resno, precno, layno);
#endif

	// SOP marker
	if (tcp->csty & J2K_CP_CSTY_SOP) {
		if (!stream->write_byte(255))
			return false;
		if (!stream->write_byte(145))
			return false;
		if (!stream->write_byte(0))
			return false;
		if (!stream->write_byte(4))
			return false;
		/* packno is uint32_t modulo 65536, in big endian format */
		uint16_t packno = (uint16_t) (tile->packno % 0x10000);
		if (!stream->write_byte((uint8_t) (packno >> 8)))
			return false;
		if (!stream->write_byte((uint8_t) (packno & 0xff)))
			return false;
	}

	// initialize precinct and code blocks if this is the first layer
	if (!layno) {
		for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
			auto band = res->bands + bandno;
			auto prc = band->precincts + precno;
			uint64_t nb_blocks = (uint64_t)prc->cw * prc->ch;

			if (band->isEmpty() || !nb_blocks) {
				band++;
				continue;
			}
			if (prc->incltree)
				prc->incltree->reset();
			if (prc->imsbtree)
				prc->imsbtree->reset();
			for (uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno) {
				auto cblk = prc->enc + cblkno;
				cblk->numPassesInPacket = 0;
				assert(band->numbps >= cblk->numbps);
				if (band->numbps < cblk->numbps) {
					GRK_WARN(
							"Code block %u bps greater than band bps. Skipping.",
							cblkno);
				} else {
					prc->imsbtree->setvalue(cblkno,
							(int64_t) (band->numbps - cblk->numbps));
				}
			}
		}
	}

	std::unique_ptr<BitIO> bio(new BitIO(stream, true));
	// Empty header bit. Grok always sets this to 1,
	// even though there is also an option to set it to zero.
	if (!bio->write(1, 1))
		return false;

	/* Writing Packet header */
	for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
		auto band = res->bands + bandno;
		auto prc = band->precincts + precno;
		uint64_t nb_blocks = (uint64_t)prc->cw * prc->ch;

		if (band->isEmpty() || !nb_blocks) {
			band++;
			continue;
		}

		for (uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno) {
			auto cblk = prc->enc + cblkno;
			auto layer = cblk->layers + layno;

			if (!cblk->numPassesInPacket
					&& layer->numpasses) {
				prc->incltree->setvalue(cblkno, (int32_t) layno);
			}
		}

		auto cblk = prc->enc;
		for (uint64_t cblkno = 0; cblkno < nb_blocks; cblkno++) {
			auto layer = cblk->layers + layno;
			uint32_t increment = 0;
			uint32_t nump = 0;
			uint32_t len = 0;

			/* cblk inclusion bits */
			if (!cblk->numPassesInPacket) {
				bool rc = prc->incltree->compress(bio.get(), cblkno,
						(int32_t) (layno + 1));
				assert(rc);
				if (!rc)
				   return false;
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
			if (!cblk->numPassesInPacket) {
				cblk->numlenbits = 3;
				bool rc = prc->imsbtree->compress(bio.get(), cblkno,
						tag_tree_uninitialized_node_value);
				assert(rc);
				if (!rc)
					return false;
			}
			/* number of coding passes included */
			bio->putnumpasses(layer->numpasses);
			uint32_t nb_passes = cblk->numPassesInPacket
					+ layer->numpasses;
			auto pass = cblk->passes
					+ cblk->numPassesInPacket;

			/* computation of the increase of the length indicator and insertion in the header     */
			for (uint32_t passno = cblk->numPassesInPacket;
					passno < nb_passes; ++passno) {
				++nump;
				len += pass->len;

				if (pass->term || passno == nb_passes - 1) {
					increment = (uint32_t) std::max<int32_t>(
							(int32_t) increment,
							floorlog2<int32_t>(len) + 1
									- ((int32_t) cblk->numlenbits
											+ floorlog2<int32_t>(nump)));
					len = 0;
					nump = 0;
				}
				++pass;
			}
			bio->putcommacode((int32_t) increment);

			/* computation of the new Length indicator */
			cblk->numlenbits += increment;

			pass = cblk->passes + cblk->numPassesInPacket;
			/* insertion of the codeword segment length */
			for (uint32_t passno = cblk->numPassesInPacket;
					passno < nb_passes; ++passno) {
				nump++;
				len += pass->len;

				if (pass->term || passno == nb_passes - 1) {
#ifdef DEBUG_LOSSLESS_T2
						cblk->packet_length_info.push_back(grk_packet_length_info(len, cblk->numlenbits + (uint32_t)floorlog2<int32_t>((int32_t)nump)));
#endif
					if (!bio->write(len,
							cblk->numlenbits + (uint32_t) floorlog2<int32_t>(nump)))
						return false;
					len = 0;
					nump = 0;
				}
				++pass;
			}
			++cblk;
		}
	}

	if (!bio->flush()) {
		GRK_ERROR("encode_packet: Bit IO flush failed while encoding packet");
		return false;
	}

	// EPH marker
	if (tcp->csty & J2K_CP_CSTY_EPH) {
		if (!stream->write_byte(255))
			return false;
		if (!stream->write_byte(146))
			return false;
	}
	/* Writing the packet body */
	for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
		auto band = res->bands + bandno;
		auto prc = band->precincts + precno;
		uint64_t nb_blocks = (uint64_t)prc->cw * prc->ch;

		if (band->isEmpty() || !nb_blocks) {
			band++;
			continue;
		}

		auto cblk = prc->enc;
		for (uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno) {
			auto cblk_layer = cblk->layers + layno;
			if (!cblk_layer->numpasses) {
				++cblk;
				continue;
			}

			if (cblk_layer->len) {
				if (!stream->write_bytes(cblk_layer->data, cblk_layer->len))
					return false;
			}
			cblk->numPassesInPacket += cblk_layer->numpasses;
			++cblk;
		}
	}
	*packet_bytes_written += (uint32_t)(stream->tell() - stream_start);

#ifdef DEBUG_LOSSLESS_T2
		auto originalDataBytes = *packet_bytes_written - numHeaderBytes;
		auto roundRes = &tilec->round_trip_resolutions[resno];
		size_t nb_bytes_read = 0;
		auto src_buf = std::unique_ptr<ChunkBuffer>(new ChunkBuffer());
		seg_buf_push_back(src_buf.get(), dest, *packet_bytes_written);

		bool ret = true;
		bool read_data;
		if (!T2Encode::read_packet_header(p_t2,
			roundRes,
			tcp,
			pi,
			&read_data,
			src_buf.get(),
			&nb_bytes_read)) {
			ret = false;
		}
		if (rc) {

			// compare size of header
			if (numHeaderBytes != nb_bytes_read) {
				printf("encode_packet: round trip header bytes %u differs from original %u\n", (uint32_t)l_nb_bytes_read, (uint32_t)numHeaderBytes);
			}
			for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
				auto band = res->bands + bandno;
				auto roundTripBand = roundRes->bands + bandno;
				if (!band->precincts)
					continue;
				for (uint64_t pno = 0; pno < band->numPrecincts; ++pno) {
					auto prec = band->precincts + pno;
					auto roundTripPrec = roundTripBand->precincts + pno;
					for (uint64_t cblkno = 0; cblkno < (uint64_t)prec->cw * prec->ch; ++cblkno) {
						auto originalCblk = prec->enc + cblkno;
						grk_layer *layer = originalCblk->layers + layno;
						if (!layer->numpasses)
							continue;

						// compare number of passes
						auto roundTripCblk = roundTripPrec->dec + cblkno;
						if (roundTripCblk->numPassesInPacket != layer->numpasses) {
							printf("encode_packet: round trip layer numpasses %u differs from original num passes %u at layer %u, component %u, band %u, precinct %u, resolution %u\n",
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
							printf("encode_packet: round trip numbps %u differs from original %u\n", roundTripCblk->numbps, originalCblk->numbps);
						}

						// compare number of length bits
						if (roundTripCblk->numlenbits != originalCblk->numlenbits) {
							printf("encode_packet: round trip numlenbits %u differs from original %u\n", roundTripCblk->numlenbits, originalCblk->numlenbits);
						}

						// compare inclusion
						if (roundTripCblk->included != originalCblk->included) {
							printf("encode_packet: round trip inclusion %u differs from original inclusion %u at layer %u, component %u, band %u, precinct %u, resolution %u\n",
								roundTripCblk->included,
								originalCblk->included,
								layno,
								compno,
								bandno,
								(uint32_t)precno,
								pi->resno);
						}

						// compare lengths
						if (roundTripCblk->packet_length_info.size() != originalCblk->packet_length_info.size()) {
							printf("encode_packet: round trip length size %u differs from original %u at layer %u, component %u, band %u, precinct %u, resolution %u\n",
								(uint32_t)roundTripCblk->packet_length_info.size(),
								(uint32_t)originalCblk->packet_length_info.size(),
								layno,
								compno,
								bandno,
								(uint32_t)precno,
								pi->resno);
						} else {
							for (uint32_t i = 0; i < roundTripCblk->packet_length_info.size(); ++i) {
								auto roundTrip = roundTripCblk->packet_length_info.operator[](i);
								auto original = originalCblk->packet_length_info.operator[](i);
								if (!(roundTrip ==original)) {
									printf("encode_packet: round trip length size %u differs from original %u at layer %u, component %u, band %u, precinct %u, resolution %u\n",
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
			if (read_data) {
			 nb_bytes_read = 0;
				if (!T2Encode::read_packet_data(roundRes,
					pi,
					src_buf.get(),
					&nb_bytes_read)) {
					rc = false;
				}
				else {
					if (originalDataBytes != nb_bytes_read) {
						printf("encode_packet: round trip data bytes %u differs from original %u\n", (uint32_t)l_nb_bytes_read, (uint32_t)originalDataBytes);
					}

					for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
						auto band = res->bands + bandno;
						auto roundTripBand = roundRes->bands + bandno;
						if (!band->precincts)
							continue;
						for (uint64_t pno = 0; pno < band->numPrecincts; ++pno) {
							auto prec = band->precincts + pno;
							auto roundTripPrec = roundTripBand->precincts + pno;
							for (uint32_t cblkno = 0; cblkno < (uint64_t)prec->cw * prec->ch; ++cblkno) {
								auto originalCblk = prec->enc + cblkno;
								grk_layer *layer = originalCblk->layers + layno;
								if (!layer->numpasses)
									continue;

								// compare cumulative length
								uint32_t originalCumulativeLayerLength = 0;
								for (uint32_t i = 0; i <= layno; ++i) {
									auto lay = originalCblk->layers + i;
									if (lay->numpasses)
										originalCumulativeLayerLength += lay->len;
								}
								auto roundTripCblk = roundTripPrec->dec + cblkno;
								uint16_t roundTripTotalSegLen = min_buf_vec_get_len(&roundTripCblk->seg_buffers);
								if (roundTripTotalSegLen != originalCumulativeLayerLength) {
									printf("encode_packet: layer %u: round trip segment length %u differs from original %u\n", layno, roundTripTotalSegLen, originalCumulativeLayerLength);
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
											printf("encode_packet: layer %u: round trip data %x differs from original %x\n", layno, roundTripData[i], originalCblk->data[i]);
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
			GRK_ERROR("encode_packet: decompress packet failed");
		}
		return ret;
#endif
	return true;
}

bool T2Encode::encode_packet_simulate(TileCodingParams *tcp, PacketIter *pi,
		uint32_t *packet_bytes_written, uint32_t max_bytes_available,
		PacketLengthMarkers *markers) {
	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint64_t precno = pi->precno;
	uint32_t layno = pi->layno;
	uint64_t nb_blocks;

	auto tile = tileProcessor->tile;
	auto tilec = tile->comps + compno;
	auto res = tilec->resolutions + resno;
	*packet_bytes_written = 0;

	if (tileProcessor->m_packetTracker.is_packet_encoded(compno, resno, precno,
			layno))
		return true;
	tileProcessor->m_packetTracker.packet_encoded(compno, resno, precno, layno);

#ifdef DEBUG_ENCODE_PACKETS
    GRK_INFO("simulate encode packet compono=%u, resno=%u, precno=%u, layno=%u",
             compno, resno, precno, layno);
#endif

	/* <SOP 0xff91> */
	if (tcp->csty & J2K_CP_CSTY_SOP) {
		max_bytes_available -= 6;
		*packet_bytes_written += 6;
	}
	/* </SOP> */

	if (!layno) {
		for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
			auto band = res->bands + bandno;
			auto prc = band->precincts + precno;

			if (prc->incltree)
				prc->incltree->reset();
			if (prc->imsbtree)
				prc->imsbtree->reset();

			nb_blocks = (uint64_t)prc->cw * prc->ch;
			for (uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno) {
				auto cblk = prc->enc + cblkno;
				cblk->numPassesInPacket = 0;
				if (band->numbps < cblk->numbps) {
					GRK_WARN(
							"Code block %u bps greater than band bps. Skipping.",
							cblkno);
				} else {
					prc->imsbtree->setvalue(cblkno,
							(int64_t) (band->numbps - cblk->numbps));
				}
			}
		}
	}

	std::unique_ptr<BitIO> bio(new BitIO(0, max_bytes_available, true));
	bio->simulateOutput(true);
	/* Empty header bit */
	if (!bio->write(1, 1))
		return false;

	/* Writing Packet header */
	for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
		auto band = res->bands + bandno;
		auto prc = band->precincts + precno;

		nb_blocks = (uint64_t)prc->cw * prc->ch;
		for (uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno) {
			auto cblk = prc->enc + cblkno;
			auto layer = cblk->layers + layno;
			if (!cblk->numPassesInPacket
					&& layer->numpasses) {
				prc->incltree->setvalue(cblkno, (int32_t) layno);
			}
		}
		for (uint64_t cblkno = 0; cblkno < nb_blocks; cblkno++) {
			auto cblk = prc->enc + cblkno;
			auto layer = cblk->layers + layno;
			uint32_t increment = 0;
			uint32_t nump = 0;
			uint32_t len = 0, passno;
			uint32_t nb_passes;

			/* cblk inclusion bits */
			if (!cblk->numPassesInPacket) {
				if (!prc->incltree->compress(bio.get(), cblkno,
						(int32_t) (layno + 1)))
					return false;
			} else {
				if (!bio->write(layer->numpasses != 0, 1))
					return false;
			}

			/* if cblk not included, go to the next cblk  */
			if (!layer->numpasses)
				continue;

			/* if first instance of cblk --> zero bit-planes information */
			if (!cblk->numPassesInPacket) {
				cblk->numlenbits = 3;
				if (!prc->imsbtree->compress(bio.get(), cblkno,
						tag_tree_uninitialized_node_value))
					return false;
			}

			/* number of coding passes included */
			bio->putnumpasses(layer->numpasses);
			nb_passes = cblk->numPassesInPacket
					+ layer->numpasses;
			/* computation of the increase of the length indicator and insertion in the header     */
			for (passno = cblk->numPassesInPacket;
					passno < nb_passes; ++passno) {
				auto pass =
						cblk->passes + cblk->numPassesInPacket + passno;
				++nump;
				len += pass->len;

				if (pass->term
						|| passno
								== (cblk->numPassesInPacket
										+ layer->numpasses) - 1) {
					increment = (uint32_t) std::max<int32_t>(
							(int32_t) increment,
							floorlog2<int32_t>(len) + 1
									- ((int32_t) cblk->numlenbits
											+ floorlog2<int32_t>(nump)));
					len = 0;
					nump = 0;
				}
			}
			bio->putcommacode((int32_t) increment);

			/* computation of the new Length indicator */
			cblk->numlenbits += increment;
			/* insertion of the codeword segment length */
			for (passno = cblk->numPassesInPacket;
					passno < nb_passes; ++passno) {
				auto pass =
						cblk->passes + cblk->numPassesInPacket + passno;
				nump++;
				len += pass->len;
				if (pass->term
						|| passno
								== (cblk->numPassesInPacket
										+ layer->numpasses) - 1) {
					if (!bio->write(len,
							cblk->numlenbits + (uint32_t) floorlog2<int32_t>(nump)))
						return false;
					len = 0;
					nump = 0;
				}
			}
		}
	}

	if (!bio->flush())
		return false;

	*packet_bytes_written += (uint32_t) bio->numbytes();
	max_bytes_available -= (uint32_t) bio->numbytes();

	/* <EPH 0xff92> */
	if (tcp->csty & J2K_CP_CSTY_EPH) {
		max_bytes_available -= 2;
		*packet_bytes_written += 2;
	}
	/* </EPH> */

	/* Writing the packet body */
	for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
		auto band = res->bands + bandno;
		auto prc = band->precincts + precno;

		nb_blocks = (uint64_t)prc->cw * prc->ch;
		for (uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno) {
			auto cblk = prc->enc + cblkno;
			auto layer = cblk->layers + layno;

			if (!layer->numpasses)
				continue;

			if (layer->len > max_bytes_available)
				return false;

			cblk->numPassesInPacket += layer->numpasses;
			*packet_bytes_written += layer->len;
			max_bytes_available -= layer->len;
		}
	}
	if (markers)
		markers->writeNext(*packet_bytes_written);

	return true;
}

}
