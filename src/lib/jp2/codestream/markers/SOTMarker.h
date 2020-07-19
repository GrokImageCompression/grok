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
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 *
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

namespace grk {

struct TileProcessor;

class SOTMarker {
public:

	SOTMarker(BufferedStream *stream);
	SOTMarker(void);

	/**
	 * Writes the SOT marker (Start of tile-part)
	 *
	 * @param       codeStream      JPEG 2000 code stream
	 * @param		tileProcess		tile processor
	 */
	bool write(CodeStream *codeStream, TileProcessor *tileProcessor);

	bool write_psot(uint32_t tile_part_bytes_written);

	/**
	 * Decode a SOT marker (Start of tile-part)
	 *
	 * @param       codeStream      JPEG 2000 code stream
	 * @param		tileProcess		tile processor
	 * @param       p_header_data   the data contained in the SOT marker.
	 * @param       header_size     the size of the data contained in the PPT marker.

	 */
	 bool read(CodeStream *codeStream, TileProcessor *tileProcessor, uint8_t *p_header_data,
			uint16_t header_size);

	 /**
	  * Reads values from a SOT marker (Start of tile-part)
	  *
	  * the j2k decoder state is not affected. No side effects,
	  *  no checks except for header_size.
	  *
	  * @param       p_header_data   the data contained in the SOT marker.
	  * @param       header_size   the size of the data contained in the SOT marker.
	  * @param       tile_no       Isot.
	  * @param       p_tot_len       Psot.
	  * @param       p_current_part  TPsot.
	  * @param       p_num_parts     TNsot.

	  */
	 bool get_sot_values(uint8_t *p_header_data, uint32_t header_size,
	 		uint16_t *tile_no, uint32_t *p_tot_len, uint8_t *p_current_part,
	 		uint8_t *p_num_parts);
private:
	 BufferedStream *m_stream;
	 uint64_t m_psot_location;

};

} /* namespace grk */

