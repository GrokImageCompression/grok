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
PacketLengthCache::PacketLengthCache(CodingParams* cp) : pltMarkers(nullptr), cp_(cp) {}
PacketLengthCache::~PacketLengthCache()
{
	delete pltMarkers;
}

PacketLengthMarkers* PacketLengthCache::createMarkers(IBufferedStream* strm)
{
	if(!pltMarkers)
		pltMarkers = strm ? new PacketLengthMarkers(strm) : new PacketLengthMarkers();

	return pltMarkers;
}

PacketLengthMarkers* PacketLengthCache::getMarkers(void)
{
	return pltMarkers;
}

void PacketLengthCache::deleteMarkers(void)
{
	delete pltMarkers;
	pltMarkers = nullptr;
}

PacketInfo* PacketLengthCache::next(void)
{
	auto packetInfo = packetInfoCache.get();
	if(!packetInfo->packetLength)
	{
		// we don't currently support PLM markers,
		// so we disable packet length markers if we have both PLT and PLM
		auto packetLengths = pltMarkers;
		bool usePlt = packetLengths && !cp_->plm_markers;
		if(usePlt)
		{
			packetInfo->packetLength = packetLengths->popNextPacketLength();
			if(packetInfo->packetLength == 0)
			{
				GRK_ERROR("PLT marker: missing packet lengths.");
				return nullptr;
			}
		}
	}

	return packetInfo;
}

void PacketLengthCache::rewind(void)
{
	// we don't currently support PLM markers,
	// so we disable packet length markers if we have both PLT and PLM
	if(pltMarkers && !cp_->plm_markers)
		pltMarkers->rewind();
}

} // namespace grk
