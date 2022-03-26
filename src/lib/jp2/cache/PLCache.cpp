/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
 */

#include "grk_includes.h"

namespace grk
{
PLCache::PLCache(CodingParams* cp) : pltMarkers(nullptr), cp_(cp) {}
PLCache::~PLCache()
{
	delete pltMarkers;
}

PLMarkerMgr* PLCache::createMarkers(IBufferedStream* strm)
{
	if(!pltMarkers)
		pltMarkers = strm ? new PLMarkerMgr(strm) : new PLMarkerMgr();

	return pltMarkers;
}

PLMarkerMgr* PLCache::getMarkers(void)
{
	return pltMarkers;
}

void PLCache::deleteMarkers(void)
{
	delete pltMarkers;
	pltMarkers = nullptr;
}

bool PLCache::next(PacketInfo** p)
{
	assert(p);
#ifdef ENABLE_PACKET_CACHE
	auto packetInfo = packetInfoCache.get();
#else
	auto packetInfo = *p;
#endif
	if(!packetInfo->packetLength)
	{
		// we don't currently support PLM markers,
		// so we disable packet length markers if we have both PLT and PLM
		auto packetLengths = pltMarkers;
		bool usePlt = packetLengths && (!cp_->plm_markers || (pltMarkers && pltMarkers->isEnabled()));
		if(usePlt)
		{
			packetInfo->packetLength = packetLengths->pop();
			if(packetInfo->packetLength == 0)
			{
				GRK_ERROR("PLT marker: missing packet lengths.");
				return false;
			}
		}
	}
#ifdef ENABLE_PACKET_CACHE
	*p = packetInfo;
#endif

	return true;
}

void PLCache::rewind(void)
{
	// we don't currently support PLM markers,
	// so we disable packet length markers if we have both PLT and PLM
	if(pltMarkers && !cp_->plm_markers)
		pltMarkers->rewind();
}

} // namespace grk
