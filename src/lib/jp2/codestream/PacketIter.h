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
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once
namespace grk {

/**
 @file PacketIter.h
 @brief Implementation of a packet iterator (PI)

 A packet iterator gets the next packet following the progression order
*/

enum J2K_T2_MODE {
	THRESH_CALC = 0, /** Function called in rate allocation process*/
	FINAL_PASS = 1 	/** Function called in Tier 2 process*/
};


/***
 * Packet iterator resolution
 */
struct grk_pi_resolution {
	uint32_t pdx, pdy;
	uint32_t pw, ph;
};

/**
 * Packet iterator component
 */
struct grk_pi_comp {
	uint32_t dx, dy;
	/** number of resolution levels */
	uint32_t numresolutions;
	grk_pi_resolution *resolutions;
};

struct ResBuf;
struct IncludeTracker;
struct PacketIter;

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */
/**
 * Creates a packet iterator for compression/decompression.
 *
 * @param   compression true if for compression, otherwise false
 * @param	image		the image being encoded.
 * @param	cp		the coding parameters.
 * @param	tileno	index of the tile being encoded.
 * @param	t2_mode	the type of pass for generating the packet iterator
 * @param 	include	vector of include buffers, one per layer
 *
 * @return	a list of packet iterator that points to the first packet of the tile (not true).
 */
PacketIter* pi_create_compress_decompress(bool compression,
								const GrkImage *image,
								CodingParams *cp,
								uint16_t tileno,
								J2K_T2_MODE t2_mode,
								IncludeTracker *include);

/**
 * Updates the compressing parameters of the codec.
 *
 * @param	p_image		the image being encoded.
 * @param	p_cp		the coding parameters.
 * @param	tile_no	index of the tile being encoded.
 */
void pi_update_params_compress(const GrkImage *p_image,
									CodingParams *p_cp,
									uint16_t tile_no);

/**
 Modify the packet iterator for enabling tile part generation
 @param pi 		Handle to the packet iterator generated in pi_create_compress
 @param cp 		Coding parameters
 @param tileno 	Number that identifies the tile for which to list the packets
 @param pino   	packet iterator number
 @param first_poc_tile_part true for first POC tile part
 @param tppos 	The position of the tile part flag in the progression order
 @param t2_mode T2 mode
 */
void pi_enable_tile_part_generation(PacketIter *pi,
									CodingParams *cp,
									uint16_t tileno,
									uint32_t pino,
									bool first_poc_tile_part,
									uint32_t tppos,
									J2K_T2_MODE t2_mode);

/**
 * Destroys a packet iterator array.
 *
 * @param	p_pi			the packet iterator array to destroy.
 */
void pi_destroy(PacketIter *p_pi);


struct ResBuf{
	ResBuf(){
		for (uint8_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
			buffers[i]=nullptr;
	}
	~ResBuf(){
		for (uint8_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
			delete[] buffers[i];
	}


	uint8_t* buffers[GRK_J2K_MAXRLVLS];
};
struct IncludeTracker {

	IncludeTracker(uint16_t numcomponents) : numcomps(numcomponents),
											currentLayer(0),
											currentResBuf(nullptr),
											include(new std::map<uint16_t, ResBuf*>())
	{}

	~IncludeTracker() {
		clear();
		delete include;
	}

	uint8_t* get_include(uint16_t layerno,  uint8_t resno){
		ResBuf* resBuf = nullptr;
		if (layerno == currentLayer && currentResBuf) {
			resBuf =  currentResBuf;
		} else {
			if (include->find(layerno) == include->end()){
				resBuf = new ResBuf;
				include->operator[](layerno) = resBuf;
			} else {
				resBuf = include->operator[](layerno);
			}
			currentResBuf = resBuf;
			currentLayer = layerno;
		}
		auto buf = resBuf->buffers[resno];
		if (!buf){
			auto numprecs = precincts[resno];
			auto len = (numprecs * numcomps + 7)/8;
			buf = new uint8_t[len];
			memset(buf, 0, len);
			resBuf->buffers[resno] = buf;
		}
		return buf;
	}


	bool update(uint16_t layno, uint8_t resno, uint16_t compno, uint64_t precno) {
		auto include = get_include(layno, resno);
		auto numprecs = precincts[resno];
		uint64_t index = compno * numprecs + precno;
		uint64_t include_index 	= (index >> 3);
		uint32_t shift 	= (index & 7);
		bool rc = false;
		uint8_t val = include[include_index];
		if ( ((val >> shift)& 1) == 0 ) {
			include[include_index] = (uint8_t)(val | (1 << shift));
			rc = true;
		}

		return rc;
	}

	void clear() {
		for (auto it = include->begin(); it != include->end(); ++it){
			delete it->second;
		}
		include->clear();
	}

	uint16_t numcomps;
	uint16_t currentLayer;
	ResBuf* currentResBuf;
	uint64_t precincts[GRK_J2K_MAXRLVLS];
	std::map<uint16_t, ResBuf*> *include;
};



/**
 Packet iterator
 */
struct PacketIter {
	PacketIter();
	~PacketIter();

	uint8_t* get_include(uint16_t layerIndex);
	bool update_include(void);
	void destroy_include(void);

	/**
	 Get next packet in component-precinct-resolution-layer order.
	 @return returns false if pi pointed to the last packet or else returns true
	 */
	bool next_cprl(void);

	bool generate_precinct_index(void);

	/**
	 Get next packet in precinct-component-resolution-layer order.
	 @return returns false if pi pointed to the last packet or else returns true
	 */
	bool next_pcrl(void);

	/**
	 Get next packet in layer-resolution-component-precinct order.
	 @return returns false if pi pointed to the last packet or else returns true
	 */
	bool next_lrcp(void);
	/**
	 Get next packet in resolution-layer-component-precinct order.
	 @return returns false if pi pointed to the last packet or else returns true
	 */
	bool next_rlcp(void);
	/**
	 Get next packet in resolution-precinct-component-layer order.
	 @return returns false if pi pointed to the last packet or else returns true
	 */
	bool next_rpcl(void);

	/**
	 Modify the packet iterator to point to the next packet
	 @return false if pi pointed to the last packet or else returns true
	 */

	bool next(void);

	void update_dxy(void);
	void update_dxy_for_comp(grk_pi_comp *comp);


	/** Enabling Tile part generation*/
	bool  tp_on;

	IncludeTracker *includeTracker;

	/** layer step used to localize the packet in the include vector */
	uint64_t step_l;
	/** resolution step used to localize the packet in the include vector */
	uint64_t step_r;
	/** component step used to localize the packet in the include vector */
	uint64_t step_c;
	/** precinct step used to localize the packet in the include vector */
	uint32_t step_p;
	/** component that identify the packet */
	uint16_t compno;
	/** resolution that identify the packet */
	uint8_t resno;
	/** precinct that identify the packet */
	uint64_t precinctIndex;
	/** layer that identify the packet */
	uint16_t layno;
	/** progression order change information */
	 grk_progression  prog;
	 uint32_t numpocs;
	/** number of components in the image */
	uint16_t numcomps;
	/** Components*/
	grk_pi_comp *comps;
	/** tile coordinates*/
	uint32_t tx0, ty0, tx1, ty1;
	/** packet coordinates */
	uint32_t x, y;
	/** packet sub-sampling factors */
	uint32_t dx, dy;
};


/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

}
