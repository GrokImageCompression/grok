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
#include "testing.h"
#include <vector>

#include "TileProcessor.h"

namespace grk {

// tile component
struct TileComponent : public grk_rect_u32 {
	TileComponent();
	~TileComponent();

	void create_buffer(	grk_image *output_image,uint32_t dx,uint32_t dy);

	bool init(bool isEncoder,
			bool whole_tile,
			grk_image *output_image,
			CodingParams *cp,
			TileCodingParams *tcp,
			grk_tile *tile,
			grk_image_comp* image_comp,
			TileComponentCodingParams* tccp,
			grk_plugin_tile *current_plugin_tile);

	 void alloc_sparse_array(uint32_t numres);
	 void release_mem();

	 bool is_subband_area_of_interest(uint32_t resno,
	 								uint32_t bandno,
	 								uint32_t aoi_x0,
	 								uint32_t aoi_y0,
	 								uint32_t aoi_x1,
	 								uint32_t aoi_y1) const;

	uint32_t numresolutions; /* number of resolutions level */
	uint32_t numAllocatedResolutions;
	uint32_t resolutions_to_decompress; /* number of resolutions level to decompress (at max)*/
	grk_resolution *resolutions; /* resolutions information */
#ifdef DEBUG_LOSSLESS_T2
	grk_resolution* round_trip_resolutions;  /* round trip resolution information */
#endif
	uint64_t numpix;
	TileComponentBuffer<int32_t> *buf;
    bool   whole_tile_decoding;
	bool m_is_encoder;
	sparse_array *m_sa;

private:
	TileComponentCodingParams *m_tccp;

};

}




