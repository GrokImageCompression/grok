/**
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

#include "grk_includes.h"

namespace grk
{
// Performed after T2, just before plugin decompress is triggered
// note: only support single segment at the moment
void decompress_synch_plugin_with_host(TileProcessor* tcd)
{
   if(tcd->current_plugin_tile && tcd->current_plugin_tile->tile_components)
   {
	  auto tile = tcd->getTile();
	  for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
	  {
		 auto tilec = &tile->comps[compno];
		 auto plugin_tilec = tcd->current_plugin_tile->tile_components[compno];
		 assert(tilec->numresolutions == plugin_tilec->num_resolutions);
		 for(uint32_t resno = 0; resno < tilec->numresolutions; resno++)
		 {
			auto res = &tilec->resolutions_[resno];
			auto plugin_res = plugin_tilec->resolutions[resno];
			assert(plugin_res->num_bands == res->numTileBandWindows);
			for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
			{
			   auto band = &res->tileBand[bandIndex];
			   auto plugin_band = plugin_res->band[bandIndex];
			   assert(plugin_band->num_precincts ==
					  (uint64_t)res->precinctGridWidth * res->precinctGridHeight);
			   //!!!! plugin still uses stepsize/2
			   plugin_band->stepsize = band->stepsize / 2;
			   for(auto prc : band->precincts)
			   {
				  auto plugin_prc = plugin_band->precincts[prc->precinctIndex];
				  assert(plugin_prc->num_blocks == prc->getNumCblks());
				  for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
				  {
					 auto cblk = prc->getDecompressedBlockPtr(cblkno);
					 if(!cblk->getNumSegments())
						continue;
					 // sanity check
					 if(cblk->getNumSegments() != 1)
					 {
						Logger::logger_.info("Plugin does not handle code blocks with multiple "
											 "segments. Image will be decompressed on CPU.");
						throw PluginDecodeUnsupportedException();
					 }
					 uint32_t maxPasses =
						 3 * (uint32_t)((tcd->headerImage->comps[0].prec + GRK_BIBO_EXTRA_BITS) - 2);
					 if(cblk->getSegment(0)->numpasses > maxPasses)
					 {
						Logger::logger_.info(
							"Number of passes %u in segment exceeds BIBO maximum %u. "
							"Image will be decompressed on CPU.",
							cblk->getSegment(0)->numpasses, maxPasses);
						throw PluginDecodeUnsupportedException();
					 }

					 auto plugin_cblk = plugin_prc->blocks[cblkno];

					 // copy segments into plugin codeblock buffer, and point host code block
					 // data to plugin data buffer
					 plugin_cblk->compressed_data_length = (uint32_t)cblk->getSegBuffersLen();
					 cblk->copyToContiguousBuffer(plugin_cblk->compressed_data);
					 cblk->compressedStream.buf = plugin_cblk->compressed_data;
					 cblk->compressedStream.len = plugin_cblk->compressed_data_length;
					 cblk->compressedStream.owns_data = false;
					 plugin_cblk->num_bit_planes = cblk->numbps;
					 plugin_cblk->num_passes = cblk->getSegment(0)->numpasses;
				  }
			   }
			}
		 }
	  }
   }
}

bool tile_equals(grk_plugin_tile* plugin_tile, Tile* tilePtr)
{
   uint32_t state = grk_plugin_get_debug_state();
   if(!(state & GRK_PLUGIN_STATE_DEBUG))
	  return true;
   if((!plugin_tile && tilePtr) || (plugin_tile && !tilePtr))
	  return false;
   if(!plugin_tile && !tilePtr)
	  return true;
   if(plugin_tile->num_components != tilePtr->numcomps_)
	  return false;
   for(uint16_t compno = 0; compno < tilePtr->numcomps_; ++compno)
   {
	  auto tilecomp = tilePtr->comps + compno;
	  auto plugin_tilecomp = plugin_tile->tile_components[compno];
	  if(tilecomp->numresolutions != plugin_tilecomp->num_resolutions)
		 return false;
	  for(uint32_t resno = 0; resno < tilecomp->numresolutions; ++resno)
	  {
		 auto resolution = tilecomp->resolutions_ + resno;
		 auto plugin_resolution = plugin_tilecomp->resolutions[resno];
		 if(resolution->numTileBandWindows != plugin_resolution->num_bands)
			return false;
		 for(uint32_t bandIndex = 0; bandIndex < resolution->numTileBandWindows; ++bandIndex)
		 {
			auto band = resolution->tileBand + bandIndex;
			auto plugin_band = plugin_resolution->band[bandIndex];
			size_t num_precincts = band->num_precincts;
			if(num_precincts != plugin_band->num_precincts)
			   return false;
			for(auto precinct : band->precincts)
			{
			   auto plugin_precinct = plugin_band->precincts[precinct->precinctIndex];
			   uint64_t num_blocks = precinct->getNumCblks();
			   if(num_blocks != plugin_precinct->num_blocks)
			   {
				  return false;
			   }
			   for(uint64_t cblkno = 0; cblkno < num_blocks; ++cblkno)
			   {
				  auto cblk = precinct->getDecompressedBlockPtr(cblkno);
				  auto plugin_cblk = plugin_precinct->blocks[cblkno];
				  if(cblk->x0 != plugin_cblk->x0 || cblk->x1 != plugin_cblk->x1 ||
					 cblk->y0 != plugin_cblk->y0 || cblk->y1 != plugin_cblk->y1)
					 return false;
			   }
			}
		 }
	  }
   }
   return true;
}

void compress_synch_with_plugin(TileProcessor* tcd, uint16_t compno, uint32_t resno,
								uint32_t bandIndex, uint64_t precinctIndex, uint64_t cblkno,
								Subband* band, CompressCodeblock* cblk, uint32_t* num_pix)
{
   if(tcd->current_plugin_tile && tcd->current_plugin_tile->tile_components)
   {
	  auto plugin_band =
		  tcd->current_plugin_tile->tile_components[compno]->resolutions[resno]->band[bandIndex];
	  auto precinct = plugin_band->precincts[precinctIndex];
	  auto plugin_cblk = precinct->blocks[cblkno];
	  uint32_t state = grk_plugin_get_debug_state();
	  bool debugPlugin = state & GRK_PLUGIN_STATE_DEBUG;

	  if(debugPlugin)
	  {
		 if(band->stepsize != plugin_band->stepsize)
		 {
			Logger::logger_.warn("grok band step size %u differs from plugin step size %u",
								 band->stepsize, plugin_band->stepsize);
		 }
		 if(cblk->numPassesTotal != plugin_cblk->num_passes)
			Logger::logger_.warn(
				"CPU total number of passes (%u) differs from "
				"plugin total number of passes (%u) : component=%u, res=%u, band=%u, block=%u",
				cblk->numPassesTotal, (uint32_t)plugin_cblk->num_passes, compno, resno, bandIndex,
				cblkno);
	  }

	  cblk->numPassesTotal = (uint32_t)plugin_cblk->num_passes;
	  *num_pix = (uint32_t)plugin_cblk->num_pix;

	  if(debugPlugin)
	  {
		 uint32_t grkNumPix = (uint32_t)cblk->area();
		 if(plugin_cblk->num_pix != grkNumPix)
			Logger::logger_.warn("grok num_pix %u differs from plugin num_pix %u", grkNumPix,
								 plugin_cblk->num_pix);
	  }

	  bool goodData = true;
	  uint16_t totalRatePlugin = (uint16_t)plugin_cblk->compressed_data_length;

	  // check data
	  if(debugPlugin)
	  {
		 uint32_t totalRate = 0;
		 if(cblk->numPassesTotal > 0)
		 {
			totalRate = (cblk->passes + cblk->numPassesTotal - 1)->rate;
			if(totalRatePlugin != totalRate)
			{
			   Logger::logger_.warn("Total CPU rate %u differs from total plugin rate %u, "
									"component=%u,res=%u,band=%u, "
									"block=%u",
									totalRate, totalRatePlugin, compno, resno, bandIndex, cblkno);
			}
		 }

		 for(uint32_t p = 0; p < totalRate; ++p)
		 {
			if(cblk->paddedCompressedStream[p] != plugin_cblk->compressed_data[p])
			{
			   Logger::logger_.warn("data differs at position=%u, component=%u, res=%u, band=%u, "
									"block=%u, CPU rate =%u, plugin rate=%u",
									p, compno, resno, bandIndex, cblkno, totalRate,
									totalRatePlugin);
			   goodData = false;
			   break;
			}
		 }
	  }

	  if(goodData)
		 cblk->paddedCompressedStream = plugin_cblk->compressed_data;
	  cblk->compressedStream.len = (uint32_t)(plugin_cblk->compressed_data_length);
	  cblk->compressedStream.owns_data = false;
	  cblk->numbps = plugin_cblk->num_bit_planes;
	  if(debugPlugin)
	  {
		 if(cblk->x0 != plugin_cblk->x0 || cblk->y0 != plugin_cblk->y0 ||
			cblk->x1 != plugin_cblk->x1 || cblk->y1 != plugin_cblk->y1)
		 {
			Logger::logger_.error("CPU code block bounding box differs from plugin code block");
		 }
	  }
	  uint32_t lastRate = 0;
	  for(uint32_t passno = 0; passno < cblk->numPassesTotal; passno++)
	  {
		 auto pass = cblk->passes + passno;
		 auto pluginPass = plugin_cblk->passes + passno;

		 // synch distortion, if applicable
		 if(tcd->needsRateControl())
		 {
			if(debugPlugin)
			{
			   if(fabs(pass->distortiondec - pluginPass->distortion_decrease) /
					  fabs(pass->distortiondec) >
				  0.01)
			   {
				  Logger::logger_.warn(
					  "distortion decrease for pass %u differs between plugin and CPU:  "
					  "plugin: %u, CPU : %u",
					  passno, pluginPass->distortion_decrease, pass->distortiondec);
			   }
			}
			pass->distortiondec = pluginPass->distortion_decrease;
		 }
		 uint16_t pluginRate = (uint16_t)(pluginPass->rate + 1);
		 if(pluginRate > totalRatePlugin)
			pluginRate = totalRatePlugin;

		 // Preventing generation of FF as last data byte of a pass
		 if((pluginRate > 1) && (plugin_cblk->compressed_data[pluginRate - 1] == 0xFF))
			pluginRate--;
		 if(debugPlugin)
		 {
			if(pluginRate != pass->rate)
			{
			   Logger::logger_.warn("CPU rate %u differs from plugin rate %u,pass=%u, "
									"component=%u,res=%u,band=%u, "
									"block=%u",
									pass->rate, pluginRate, passno, compno, resno, bandIndex,
									cblkno);
			}
		 }
		 pass->rate = pluginRate;
		 pass->len = (uint16_t)(pass->rate - lastRate);
		 lastRate = pass->rate;
	  }
   }
}

// set context stream for debugging purposes
#ifdef PLUGIN_DEBUG_ENCODE
void set_context_stream(TileProcessor* p_tileProcessor)
{
   auto tile = p_tileProcessor->getTile();
   for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
   {
	  auto tilec = tile->comps + compno;
	  for(uint32_t resno = 0; resno < tilec->numresolutions; resno++)
	  {
		 auto res = &tilec->resolutions_[resno];
		 for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
		 {
			auto band = &res->tileBand[bandIndex];
			for(auto prc : band->precincts)
			{
			   for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
			   {
				  auto cblk = prc->getCompressedBlockPtr(cblkno);
				  if(p_tileProcessor->current_plugin_tile &&
					 p_tileProcessor->current_plugin_tile->tile_components)
				  {
					 auto comp = p_tileProcessor->current_plugin_tile->tile_components[compno];
					 if(resno < comp->num_resolutions)
					 {
						auto plugin_band = comp->resolutions[resno]->band[bandIndex];
						auto precinct = plugin_band->precincts[prc->precinctIndex];
						auto plugin_cblk = precinct->blocks[cblkno];
						cblk->context_stream = plugin_cblk->context_stream;
					 }
				  }
			   }
			}
		 }
	  }
   }
}

static const char* plugin_debug_mqc_next_cxd_method_name = "plugin_debug_mqc_next_cxd";
static const char* plugin_debug_mqc_next_plane_method_name = "plugin_debug_mqc_next_plane";

// Debug: these methods wrap plugin methods for parsing a context stream
void mqc_next_plane(grk_plugin_debug_mqc* mqc)
{
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_DEBUG_MQC_NEXT_PLANE)minpf_get_symbol(
		  mgr->dynamic_libraries[0], plugin_debug_mqc_next_plane_method_name);
	  if(func)
		 func(mqc);
   }
}
void nextCXD(grk_plugin_debug_mqc* mqc, uint32_t d)
{
   auto mgr = minpf_get_plugin_manager();
   if(mgr && mgr->num_libraries > 0)
   {
	  auto func = (PLUGIN_DEBUG_MQC_NEXT_CXD)minpf_get_symbol(
		  mgr->dynamic_libraries[0], plugin_debug_mqc_next_cxd_method_name);
	  if(func)
		 func(mqc, d);
   }
}
#endif

} // namespace grk
