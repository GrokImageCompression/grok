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

#pragma once


#include "TileProcessor.h"

namespace grk
{

class TileComponentRegion  : public grk_rect_u32 {
public:
	TileComponentRegion(Resolution *res,uint32_t numres,TileComponentCodingParams *tccp);
	virtual ~TileComponentRegion();

	void create_buffer(grk_image *output_image,
										uint32_t dx,
										uint32_t dy);

	void allocSparseBuffer(Resolution *resolutions, uint32_t numres);

	 void release_mem();
	 TileComponentBuffer<int32_t>* getBuffer();
	 ISparseBuffer* getSparseBuffer();
private:
	 bool subbandIntersectsAOI(uint32_t resno,
	 								uint32_t bandno,
	 								const grk_rect_u32 *aoi) const;


	TileComponentBuffer<int32_t> *buf;
	ISparseBuffer *m_sa;
	Resolution *resolutions; /* resolutions information */
	uint32_t numresolutions; /* number of resolution levels */
	uint32_t resolutions_to_decompress; /* number of resolutions level to decompress (at max)*/
	TileComponentCodingParams *m_tccp;

};

}
