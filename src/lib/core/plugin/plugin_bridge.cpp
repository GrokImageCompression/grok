/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
#include "TileProcessorCompress.h"

namespace grk
{

bool tile_equals(grk_plugin_tile* plugin_tile, const Tile* tilePtr)
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
    auto tilecomp = tilePtr->comps_ + compno;
    auto plugin_tilecomp = plugin_tile->tile_components[compno];
    if(tilecomp->num_resolutions_ != plugin_tilecomp->numresolutions)
      return false;
    for(uint8_t resno = 0; resno < tilecomp->num_resolutions_; ++resno)
    {
      auto resolution = tilecomp->resolutions_ + resno;
      auto plugin_resolution = plugin_tilecomp->resolutions[resno];
      if(resolution->numBands_ != plugin_resolution->num_bands)
        return false;
      for(uint32_t bandIndex = 0; bandIndex < resolution->numBands_; ++bandIndex)
      {
        auto band = resolution->band + bandIndex;
        auto plugin_band = plugin_resolution->band[bandIndex];
        for(auto [precinctIndex, vectorIndex] : band->precinctMap_)
        {
          auto precinct = band->precincts_[vectorIndex];
          auto plugin_precinct = plugin_band->precincts[precinctIndex];
          uint64_t num_blocks = precinct->getNumCblks();
          if(num_blocks != plugin_precinct->num_blocks)
          {
            return false;
          }
          for(uint32_t cblkno = 0; cblkno < num_blocks; ++cblkno)
          {
            auto cblk = precinct->getDecompressedBlock(cblkno);
            auto plugin_cblk = plugin_precinct->blocks[cblkno];
            if(cblk->x0() != plugin_cblk->x0 || cblk->x1() != plugin_cblk->x1 ||
               cblk->y0() != plugin_cblk->y0 || cblk->y1() != plugin_cblk->y1)
              return false;
          }
        }
      }
    }
  }
  return true;
}

void compress_synch_with_plugin(TileProcessorCompress* tileProcessor, uint16_t compno,
                                uint8_t resno, uint8_t bandIndex, uint64_t precinctIndex,
                                uint32_t cblkno, Subband* band, t1::CodeblockCompress* cblk,
                                uint32_t* num_pix)
{
  auto pluginTile = tileProcessor->getCurrentPluginTile();
  if(pluginTile && pluginTile->tile_components)
  {
    auto plugin_band = pluginTile->tile_components[compno]->resolutions[resno]->band[bandIndex];
    auto precinct = plugin_band->precincts[precinctIndex];
    auto plugin_cblk = precinct->blocks[cblkno];
    uint32_t state = grk_plugin_get_debug_state();
    bool debugPlugin = state & GRK_PLUGIN_STATE_DEBUG;

    if(debugPlugin)
    {
      if(band->stepsize_ != plugin_band->stepsize)
      {
        grklog.warn("grok band step size %u differs from plugin step size %u", band->stepsize_,
                    plugin_band->stepsize);
      }
      if(cblk->getNumPasses() != plugin_cblk->num_passes)
        grklog.warn("CPU total number of passes (%u) differs from "
                    "plugin total number of passes (%u) : component=%u, res=%u, band=%u, block=%u",
                    cblk->getNumPasses(), (uint32_t)plugin_cblk->num_passes, compno, resno,
                    bandIndex, cblkno);
    }

    cblk->setNumPasses(plugin_cblk->num_passes);
    *num_pix = (uint32_t)plugin_cblk->num_pix;

    if(debugPlugin)
    {
      uint32_t grkNumPix = (uint32_t)cblk->area();
      if(plugin_cblk->num_pix != grkNumPix)
        grklog.warn("grok num_pix %u differs from plugin num_pix %u", grkNumPix,
                    plugin_cblk->num_pix);
    }

    bool goodData = true;
    uint16_t totalRatePlugin = (uint16_t)plugin_cblk->compressed_data_length;

    // check data
    if(debugPlugin)
    {
      uint32_t totalRate = 0;
      if(cblk->getNumPasses() > 0)
      {
        totalRate = cblk->getLastPass()->rate_;
        if(totalRatePlugin != totalRate)
        {
          grklog.warn("Total CPU rate %u differs from total plugin rate %u, "
                      "component=%u,res=%u,band=%u, "
                      "block=%u",
                      totalRate, totalRatePlugin, compno, resno, bandIndex, cblkno);
        }
      }

      for(uint32_t p = 0; p < totalRate; ++p)
      {
        auto stream = cblk->getPaddedCompressedStream();
        if(stream[p] != plugin_cblk->compressed_data[p])
        {
          grklog.warn("data differs at position=%u, component=%u, res=%u, band=%u, "
                      "block=%u, CPU rate =%u, plugin rate=%u",
                      p, compno, resno, bandIndex, cblkno, totalRate, totalRatePlugin);
          goodData = false;
          break;
        }
      }
    }

    if(goodData)
      cblk->setPaddedCompressedStream(plugin_cblk->compressed_data);
    auto blockStream = cblk->getCompressedStream();
    blockStream->set_num_elts((uint32_t)plugin_cblk->compressed_data_length);
    blockStream->set_owns_data(false);
    cblk->setNumBps(plugin_cblk->num_bit_planes);
    if(debugPlugin)
    {
      if(cblk->x0() != plugin_cblk->x0 || cblk->y0() != plugin_cblk->y0 ||
         cblk->x1() != plugin_cblk->x1 || cblk->y1() != plugin_cblk->y1)
      {
        grklog.error("CPU code block bounding box differs from plugin code block");
      }
    }
    uint32_t lastRate = 0;
    for(uint8_t passno = 0; passno < cblk->getNumPasses(); passno++)
    {
      auto pass = cblk->getPass(passno);
      auto pluginPass = plugin_cblk->passes + passno;

      // synch distortion, if applicable
      if(tileProcessor->needsRateControl())
      {
        if(debugPlugin)
        {
          if(fabs(pass->distortiondec_ - pluginPass->distortion_decrease) /
                 fabs(pass->distortiondec_) >
             0.01)
          {
            grklog.warn("distortion decrease for pass %u differs between plugin and CPU:  "
                        "plugin: %u, CPU : %u",
                        passno, pluginPass->distortion_decrease, pass->distortiondec_);
          }
        }
        pass->distortiondec_ = pluginPass->distortion_decrease;
      }
      uint16_t pluginRate = (uint16_t)(pluginPass->rate + 1);
      if(pluginRate > totalRatePlugin)
        pluginRate = totalRatePlugin;

      // Preventing generation of FF as last data byte of a pass
      if((pluginRate > 1) && (plugin_cblk->compressed_data[pluginRate - 1] == 0xFF))
        pluginRate--;
      if(debugPlugin)
      {
        if(pluginRate != pass->rate_)
        {
          grklog.warn("CPU rate %u differs from plugin rate %u,pass=%u, "
                      "component=%u,res=%u,band=%u, "
                      "block=%u",
                      pass->rate_, pluginRate, passno, compno, resno, bandIndex, cblkno);
        }
      }
      pass->rate_ = pluginRate;
      pass->len_ = (uint16_t)(pass->rate_ - lastRate);
      lastRate = pass->rate_;
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
    for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
    {
      auto res = &tilec->resolutions_[resno];
      for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
      {
        auto band = &res->tileBand[bandIndex];
        for(auto prc : band->precincts)
        {
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
          {
            auto cblk = prc->getCompressedBlock(cblkno);
            if(p_tileProcessor->current_plugin_tile &&
               p_tileProcessor->current_plugin_tile->tile_components)
            {
              auto comp = p_tileProcessor->current_plugin_tile->tile_components[compno];
              if(resno < comp->numresolutions)
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
    auto func = (PLUGIN_DEBUG_MQC_NEXT_CXD)minpf_get_symbol(mgr->dynamic_libraries[0],
                                                            plugin_debug_mqc_next_cxd_method_name);
    if(func)
      func(mqc, d);
  }
}
#endif

} // namespace grk
