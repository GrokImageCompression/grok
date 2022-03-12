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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once
namespace grk
{
/**
 @file PacketIter.h
 @brief Implementation of a packet iterator (PI)

 A packet iterator gets the next packet following the progression order
*/

enum J2K_T2_MODE
{
	THRESH_CALC = 0, /** Function called in rate allocation process*/
	FINAL_PASS = 1 /** Function called in Tier 2 process*/
};

/***
 * Packet iterator resolution
 */
struct PiResolution
{
	PiResolution()
		: precinctWidthExp(0), precinctHeightExp(0), precinctGridWidth(0), precinctGridHeight(0)
	{}
	uint32_t precinctWidthExp;
	uint32_t precinctHeightExp;
	uint32_t precinctGridWidth;
	uint32_t precinctGridHeight;
};

/**
 * Packet iterator component
 */
struct PiComp
{
	PiComp() : dx(0), dy(0), numresolutions(0), resolutions(nullptr) {}
	// component sub-sampling factors
	uint32_t dx;
	uint32_t dy;
	uint8_t numresolutions;
	PiResolution* resolutions;
};

struct ResIncludeBuffers
{
	ResIncludeBuffers()
	{
		for(uint8_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
			buffers[i] = nullptr;
	}
	~ResIncludeBuffers()
	{
		for(uint8_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
			delete[] buffers[i];
	}
	uint8_t* buffers[GRK_J2K_MAXRLVLS];
};
struct IncludeTracker
{
	IncludeTracker(uint16_t numcomponents)
		: numcomps(numcomponents), currentLayer(0), currentResBuf(nullptr),
		  include(new std::map<uint16_t, ResIncludeBuffers*>())
	{
		for (uint8_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
			numPrecinctsPerRes[i] = 0;
	}
	~IncludeTracker()
	{
		clear();
		delete include;
	}
	uint8_t* get_include(uint16_t layerno, uint8_t resno)
	{
		ResIncludeBuffers* resBuf = nullptr;
		if(layerno == currentLayer && currentResBuf)
		{
			resBuf = currentResBuf;
		}
		else
		{
			if(include->find(layerno) == include->end())
			{
				resBuf = new ResIncludeBuffers;
				include->operator[](layerno) = resBuf;
			}
			else
			{
				resBuf = include->operator[](layerno);
			}
			currentResBuf = resBuf;
			currentLayer = layerno;
		}
		auto buf = resBuf->buffers[resno];
		if(!buf)
		{
			auto numprecs = numPrecinctsPerRes[resno];
			auto len = (numprecs * numcomps + 7) / 8;
			buf = new uint8_t[len];
			memset(buf, 0, len);
			resBuf->buffers[resno] = buf;
		}
		return buf;
	}
	bool update(uint16_t layno, uint8_t resno, uint16_t compno, uint64_t precno)
	{
		auto include = get_include(layno, resno);
		auto numprecs = numPrecinctsPerRes[resno];
		uint64_t index = compno * numprecs + precno;
		uint64_t include_index = (index >> 3);
		uint32_t shift = (index & 7);
		uint8_t val = include[include_index];
		if(((val >> shift) & 1) == 0)
		{
			include[include_index] = (uint8_t)(val | (1 << shift));
			return true;
		}

		return false;
	}
	void clear()
	{
		for(auto it = include->begin(); it != include->end(); ++it)
			delete it->second;
		include->clear();
	}
	uint64_t numPrecinctsPerRes[GRK_J2K_MAXRLVLS];
private:
	uint16_t numcomps;
	uint16_t currentLayer;
	ResIncludeBuffers* currentResBuf;
	std::map<uint16_t, ResIncludeBuffers*>* include;
};

class PacketManager;

struct ResPrecinctInfo {
	ResPrecinctInfo();
	void init(uint8_t levelno,
			grkRectU32 tileBounds,
			uint32_t dx,
			uint32_t dy,
			bool windowed,
			grkRectU32  tileWindow);
	bool operator==(ResPrecinctInfo &rhs){
		return operator==(&rhs);
	}
	bool operator==(ResPrecinctInfo *rhs){
		return rhs &&
				precinctWidthExp == rhs->precinctWidthExp &&
				precinctHeightExp == rhs->precinctHeightExp &&
				rpx0 == rhs->rpx0 &&
				rpy0 == rhs->rpy0 &&
				rpdx == rhs->rpdx &&
				rpdy == rhs->rpdy &&
				rdx == rhs->rdx &&
				rdy == rhs->rdy &&
				px0 == rhs->px0 &&
				py0 == rhs->py0 &&
				valid == rhs->valid;
	}
	uint32_t precinctWidthExp;
	uint32_t precinctHeightExp;
	uint32_t rpx0;
	uint32_t rpy0;
	uint64_t rpdx;
	uint64_t rpdy;
	uint64_t rdx;
	uint64_t rdy;
	uint32_t px0;
	uint32_t py0;
	grkRectU32 window;
	bool valid;
};

/**
 Packet iterator
 */
struct PacketIter
{
	PacketIter();
	~PacketIter();

	void init(	PacketManager* packetMan,
				TileCodingParams* tcp);

	void genPrecinctInfo();

	uint8_t* get_include(uint16_t layerIndex);
	bool update_include(void);
	void destroy_include(void);

	/**
	 Modify the packet iterator to point to the next packet
	 @return false if pi pointed to the last packet, otherwise true
	 */
	bool next(void);

	void update_dxy(void);

	/** layer step used to localize the packet in the include vector */
	uint64_t step_l;
	/** resolution step used to localize the packet in the include vector */
	uint64_t step_r;
	/** component step used to localize the packet in the include vector */
	uint64_t step_c;
	/** precinct step used to localize the packet in the include vector */
	uint32_t step_p;
	uint16_t compno;
	uint8_t resno;
	uint64_t precinctIndex;
	uint16_t layno;
	grk_progression prog;
	uint16_t numcomps;
	PiComp* comps;
	/** packet coordinates */
	uint64_t x, y;
	/** component sub-sampling */
	uint32_t dx, dy;
  private:
	// This packet iterator is designed so that the innermost progression
	// is only incremented before the **next** packet is processed.
	// i.e. it is not incremented before the very first packet is processed,
	// but rather before all subsequent packets are processed.
	// This flag keeps track of this state.
	bool incrementInner;

	PacketManager* packetManager;
	uint8_t maxNumDecompositionResolutions;
	bool singleProgression_;
	ResPrecinctInfo * precinctInfo_;
	// precinct top,left grid coordinates
	uint32_t px0grid_;
	uint32_t py0grid_;
	bool genPrecinctY0Grid(ResPrecinctInfo *rpInfo);
	bool genPrecinctX0Grid(ResPrecinctInfo *rpInfo);
	void genPrecinctY0GridOPT(ResPrecinctInfo *rpInfo);
	void genPrecinctX0GridOPT(ResPrecinctInfo *rpInfo);
	bool genPrecinctResCheck(ResPrecinctInfo *rpInfo);
	bool generatePrecinctIndex(void);
	void update_dxy_for_comp(PiComp* comp);

	/**
	 Get next packet in component-precinct-resolution-layer order.
	 @return returns false if pi pointed to the last packet, otherwise true
	 */
	bool next_cprl(void);

	/**
	 Get next packet in precinct-component-resolution-layer order.
	 @return returns false if pi pointed to the last packet, otherwise true
	 */
	bool next_pcrl(void);

	/**
	 Get next packet in layer-resolution-component-precinct order.
	 @return returns false if pi pointed to the last packet, otherwise true
	 */
	bool next_lrcp(void);
	/**
	 Get next packet in resolution-layer-component-precinct order.
	 @return returns false if pi pointed to the last packet, otherwise true
	 */
	bool next_rlcp(void);
	/**
	 Get next packet in resolution-precinct-component-layer order.
	 @return returns false if pi pointed to the last packet, otherwise true
	 */
	bool next_rpcl(void);
	bool next_rpclOPT(void);
};

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

} // namespace grk
