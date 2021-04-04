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
 */

#include "grk_includes.h"

namespace grk
{
#include "grk_includes.h"

PacketLengthCache::PacketLengthCache(CodingParams* cp) : pltMarkers(nullptr), m_cp(cp) {}

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
		bool usePlt = packetLengths && !m_cp->plm_markers;
		if(usePlt)
			packetInfo->packetLength = packetLengths->getNext();
	}

	return packetInfo;
}

void PacketLengthCache::rewind(void)
{
	auto packetLengths = pltMarkers;
	// we don't currently support PLM markers,
	// so we disable packet length markers if we have both PLT and PLM
	bool usePlt = packetLengths && !m_cp->plm_markers;
	if(usePlt)
		packetLengths->rewind();
}

} // namespace grk
