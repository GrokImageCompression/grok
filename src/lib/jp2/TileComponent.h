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
struct TileComponent {
	TileComponent();
	~TileComponent();
	uint32_t width();
	uint32_t height();
	uint64_t area();
	uint64_t size();

	bool create_buffer(	grk_image *output_image,uint32_t dx,uint32_t dy);

	void get_dimensions(grk_image *l_image, grk_image_comp  *l_img_comp,
			uint32_t *l_size_comp, uint32_t *l_width,
			uint32_t *l_height, uint32_t *l_offset_x, uint32_t *l_offset_y,
			uint32_t *l_image_width, uint32_t *l_stride, uint64_t *l_tile_offset);

	bool init(bool isEncoder,
			bool whole_tile,
			grk_image *output_image,
			grk_coding_parameters *cp,
			grk_tcp *tcp,
			grk_tcd_tile *tile,
			grk_image_comp* image_comp,
			grk_tccp* tccp,
			grk_plugin_tile *current_plugin_tile);

	 void alloc_sparse_array(uint32_t numres);
	 void release_mem();


	uint32_t numresolutions; /* number of resolutions level */
	uint32_t numAllocatedResolutions;
	uint32_t minimum_num_resolutions; /* number of resolutions level to decompress (at max)*/
	grk_tcd_resolution *resolutions; /* resolutions information */
#ifdef DEBUG_LOSSLESS_T2
	grk_tcd_resolution* round_trip_resolutions;  /* round trip resolution information */
#endif
	uint64_t numpix;
	TileBuffer *buf;
    bool   whole_tile_decoding;

    /* reduced tile component coordinates */
	uint32_t x0, y0, x1, y1;
	grk_rect unreduced_tile_dim;
	bool m_is_encoder;
	sparse_array *m_sa;

private:
	void finalizeCoordinates();
	/**
	 * Deallocates the decoding data of the given precinct.
	 */
	 void code_block_dec_deallocate(grk_tcd_precinct *p_precinct);

	/**
	 * Deallocates the encoding data of the given precinct.
	 */
	 void code_block_enc_deallocate(grk_tcd_precinct *p_precinct);

};

}




