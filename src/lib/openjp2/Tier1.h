/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
*/

#pragma once

#include "grok_includes.h"
#include <vector>
#include "t1_interface.h"

namespace grk {



class Tier1
{
public:
	
	bool encodeCodeblocks(tcp_t *tcp, 
						tcd_tile_t *tile,
						const double * mct_norms,
						uint32_t mct_numcomps,
						uint32_t numThreads, 
						bool doRateControl);

	bool prepareDecodeCodeblocks(tcd_tilecomp_t* tilec,
								tccp_t* tccp,
								std::vector<decodeBlockInfo*>* blocks,
								event_mgr_t * p_manager);

	bool decodeCodeblocks(tcp_t *tcp,
								uint16_t blockw,
							uint16_t blockh,
							std::vector<decodeBlockInfo*>* blocks,
							int32_t numThreads);

};


}
