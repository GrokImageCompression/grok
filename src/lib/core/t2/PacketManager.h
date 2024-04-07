/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

namespace grk
{
class PacketManager
{
 public:
   PacketManager(bool compression, GrkImage* img, CodingParams* cparams, uint16_t tilenumber,
				 J2K_T2_MODE t2_mode, TileProcessor* tileProc);
   virtual ~PacketManager();
   PacketIter* getPacketIter(uint32_t poc) const;
   /**
	Modify the packet iterator for enabling tile part generation
	@param pino   	packet iterator number
	@param first_poc_tile_part true for first POC tile part
	@param tppos 	The position of the tile part flag in the progression order
	*/
   void enableTilePartGeneration(uint32_t pino, bool first_poc_tile_part, uint32_t tppos);
   /**
	* Updates the compressing parameters of the codec.
	*
	* @param	p_image		the image being encoded.
	* @param	p_cp		the coding parameters.
	* @param	tile_no	index of the tile being encoded.
	*/
   static void updateCompressParams(const GrkImage* p_image, CodingParams* p_cp, uint16_t tile_no);

   IncludeTracker* getIncludeTracker(void);
   uint32_t getNumProgressions(void);
   TileProcessor* getTileProcessor(void);
   GrkImage* getImage();
   grk_rect32 getTileBounds(void);
   CodingParams* getCodingParams(void);
   J2K_T2_MODE getT2Mode(void);

 private:
   /**
	* Updates the coding parameters
	*
	* @param	p_cp		the coding parameters to modify
	* @param	num_comps		the number of components
	* @param	tileno	the tile index being concerned.
	* @param	tileBounds tile bounds
	* @param	max_precincts	the maximum number of precincts for all the bands of the tile
	* @param	max_res	the maximum number of resolutions for all the poc inside the tile.
	* @param	dx_min		the minimum dx of all the components of all the resolutions for the
	* tile.
	* @param	dy_min		the minimum dy of all the components of all the resolutions for the
	* tile.
	*/
   static void updateCompressTcpProgressions(CodingParams* p_cp, uint16_t num_comps,
											 uint16_t tileno, grk_rect32 tileBounds,
											 uint64_t max_precincts, uint8_t max_res,
											 uint32_t dx_min, uint32_t dy_min, bool poc);
   /**
	* Get the compression parameters needed to update the coding parameters and all the pocs.
	* The precinct widths, heights, dx and dy for each component at each resolution will be stored
	* as well. the last parameter of the function should be an array of pointers of size nb
	* components, each pointer leading to an area of size 4 * max_res. The data is stored inside
	* this area with the following pattern : dx_compi_res0 , dy_compi_res0 , w_compi_res0,
	* h_compi_res0 , dx_compi_res1 , dy_compi_res1 , w_compi_res1, h_compi_res1 , ...
	*
	* @param	p_image		the image being encoded.
	* @param	p_cp		the coding parameters.
	* @param	tileno		the tile index of the tile being encoded.
	* @param	tileBounds	tile bounds
	* @param	max_precincts	maximum number of precincts for all the bands of the tile
	* @param	max_res		maximum number of resolutions for all the poc inside the tile.
	* @param	dx_min		minimum dx of all the components of all the resolutions for the tile.
	* @param	dy_min		minimum dy of all the components of all the resolutions for the tile.
	* @param	precinctByComponent	stores log2 precinct grid dimensions by component
	*/
   static void getParams(const GrkImage* image, const CodingParams* p_cp, uint16_t tileno,
						 grk_rect32* tileBounds, uint32_t* dx_min, uint32_t* dy_min,
						 uint64_t* precincts, uint64_t* max_precincts, uint8_t* max_res,
						 uint32_t** precinctByComponent);
   GrkImage* image;
   CodingParams* cp;
   uint16_t tileno;
   IncludeTracker* includeTracker;
   PacketIter* pi_;
   J2K_T2_MODE t2Mode;
   TileProcessor* tileProcessor;
   grk_rect32 tileBounds_;
};

} // namespace grk
