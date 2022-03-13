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
 *
 */
#pragma once

#include "grk_includes.h"
#include <map>

namespace grk
{
enum eBandOrientation
{
	BAND_ORIENT_LL,
	BAND_ORIENT_HL,
	BAND_ORIENT_LH,
	BAND_ORIENT_HH,
	BAND_NUM_ORIENTATIONS
};
// LL band index when resolution == 0
const uint32_t BAND_RES_ZERO_INDEX_LL = 0;

// band indices when resolution > 0
enum eBandIndex
{
	BAND_INDEX_HL,
	BAND_INDEX_LH,
	BAND_INDEX_HH,
	BAND_NUM_INDICES
};

struct Subband : public grkRectU32
{
	Subband() : orientation(BAND_ORIENT_LL), numPrecincts(0), numbps(0), stepsize(0) {}
	// note: don't copy precinct array
	Subband(const Subband& rhs)
		: grkRectU32(rhs), orientation(rhs.orientation), numPrecincts(0), numbps(rhs.numbps),
		  stepsize(rhs.stepsize)
	{}
	Subband& operator=(const Subband& rhs)
	{
		if(this != &rhs)
		{ // self-assignment check expected
			*this = Subband(rhs);
		}
		return *this;
	}
	void print()
	{
		grkRectU32::print();
	}
	bool isEmpty()
	{
		return ((x1 - x0 == 0) || (y1 - y0 == 0));
	}
	Precinct* getPrecinct(uint64_t precinctIndex)
	{
		if(precinctMap.find(precinctIndex) == precinctMap.end())
			return nullptr;
		uint64_t index = precinctMap[precinctIndex];

		return precincts[index];
	}
	grkRectU32 generatePrecinctBounds(uint64_t precinctIndex, grkpt precinctPartitionTopLeft,
									  grkpt precinctExpn, uint32_t precinctGridWidth)
	{
		auto precinctTopLeft =
			grkpt(precinctPartitionTopLeft.x +
					  (uint32_t)((precinctIndex % precinctGridWidth) << precinctExpn.x),
				  precinctPartitionTopLeft.y +
					  (uint32_t)((precinctIndex / precinctGridWidth) << precinctExpn.y));
		return grkRectU32(precinctTopLeft.x, precinctTopLeft.y,
						  precinctTopLeft.x + (1U << precinctExpn.x),
						  precinctTopLeft.y + (1U << precinctExpn.y))
			.intersection(this);
	}
	Precinct* createPrecinct(bool isCompressor, uint64_t precinctIndex,
							 grkpt precinctPartitionTopLeft, grkpt precinctExpn,
							 uint32_t precinctGridWidth, grkpt cblk_expn)
	{
		auto temp = precinctMap.find(precinctIndex);
		if(temp != precinctMap.end())
			return precincts[temp->second];

		auto bounds = generatePrecinctBounds(precinctIndex, precinctPartitionTopLeft, precinctExpn,
											 precinctGridWidth);
		if (!bounds.is_valid()){
			GRK_ERROR("createPrecinct: invalid precinct bounds.");
			return nullptr;
		}
		auto currPrec = new Precinct(bounds, isCompressor, cblk_expn);
		currPrec->precinctIndex = precinctIndex;
		precincts.push_back(currPrec);
		precinctMap[precinctIndex] = precincts.size() - 1;

		return currPrec;
	}
	eBandOrientation orientation;
	std::vector<Precinct*> precincts;
	// maps global precinct index to precincts vector index
	std::map<uint64_t, uint64_t> precinctMap;
	uint64_t numPrecincts;
	uint8_t numbps;
	float stepsize;
};

} // namespace grk
