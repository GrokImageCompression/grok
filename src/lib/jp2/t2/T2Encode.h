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
#pragma once

#include <T2.h>

namespace grk {

struct TileProcessor;

/**
 @file T2.h
 @brief Implementation of a tier-2 coding (packetization of code-block data) (T2)

 */

/** @defgroup T2 T2 - Implementation of a tier-2 coding */
/*@{*/

/**
 Tier-2 coding
 */
struct T2Encode : public T2 {
	T2Encode(TileProcessor *tileProc);

	/*
	 Encode the packets of a tile to a destination buffer
	 @param tileno           number of the tile encoded
	 @param maxlayers        maximum number of layers
	 @param dest             the destination buffer
	 @param p_data_written   FIXME DOC
	 @param cstr_info        Codestream information structure
	 @param tpnum            Tile part number of the current tile
	 @param tppos            The position of the tile part flag in the progression order
	 @param pino             FIXME DOC
	 */
	bool encode_packets(uint16_t tileno, uint32_t maxlayers,
			BufferedStream *stream, uint32_t *p_data_written,
			grk_codestream_info *cstr_info, uint32_t tpnum, uint32_t tppos,
			uint32_t pino);

	/**
	 Simulate encoding packets of a tile to a destination buffer
	 @param tileno           number of the tile encoded
	 @param maxlayers        maximum number of layers
	 @param p_data_written   FIXME DOC
	 @param max_             the max length of the destination buffer
	 @param tppos            The position of the tile part flag in the progression order
	 */
	bool encode_packets_simulate(uint16_t tileno, uint32_t maxlayers,
			uint32_t *p_data_written, uint32_t max_len, uint32_t tppos,
			PacketLengthMarkers *markers);

private:
	TileProcessor *tileProcessor;

	/**
	 Encode a packet of a tile to a destination buffer
	 @param tileno Number of the tile encoded
	 @param tcp Tile coding parameters
	 @param pi Packet identity
	 @param stream stream
	 @param p_data_written   FIXME DOC
	 @param cstr_info Codestream information structure
	 @return
	 */
	bool encode_packet(uint16_t tileno, TileCodingParams *tcp, PacketIter *pi,
			BufferedStream *stream, uint32_t *p_data_written,
			grk_codestream_info *cstr_info);

	/**
	 Encode a packet of a tile to a destination buffer
	 @param tcp Tile coding parameters
	 @param pi Packet identity
	 @param p_data_written   FIXME DOC
	 @param len Length of the destination buffer
	 @return
	 */
	bool encode_packet_simulate(TileCodingParams *tcp, PacketIter *pi,
			uint32_t *p_data_written, uint32_t len,
			PacketLengthMarkers *markers);

};

}
