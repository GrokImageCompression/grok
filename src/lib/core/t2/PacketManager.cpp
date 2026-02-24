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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
}
#include "CodeStream.h"
#include "PacketIter.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "CodecScheduler.h"
#include "PacketManager.h"
#include "TileProcessor.h"

namespace grk
{

PacketManager::PacketManager(bool compression, GrkImage* img, CodingParams* cparams,
                             uint16_t tilenumber, T2_MODE t2_mode, TileProcessor* tileProc)
    : image_(img), cp_(cparams), tileIndex_(tilenumber), includeTracker_(new IncludeTracker()),
      pi_(nullptr), t2Mode_(t2_mode), tileProcessor_(tileProc)
{
  assert(cparams != nullptr);
  assert(img != nullptr);
  assert(tilenumber < cp_->t_grid_width_ * cp_->t_grid_height_);
  auto tcp = tileProc->getTCP();
  uint32_t numProgressions = tcp->numpocs_ + 1;
  uint32_t dataStride = 4 * GRK_MAXRLVLS;
  auto componentPrecinctInfo = std::make_unique<uint32_t*[]>(image_->numcomps);
  auto precinct = std::make_unique<uint32_t[]>(dataStride * image_->numcomps);
  auto resolutionPrecinctGrid = precinct.get();
  for(uint16_t compno = 0; compno < image_->numcomps; ++compno)
  {
    componentPrecinctInfo[compno] = resolutionPrecinctGrid;
    resolutionPrecinctGrid += dataStride;
  }
  uint8_t max_res;
  uint64_t max_precincts;
  uint32_t dx_min;
  uint32_t dy_min;
  getParams(image_, cp_, tcp, tileIndex_, &tileBounds_, &dx_min, &dy_min, includeTracker_,
            &max_precincts, &max_res, componentPrecinctInfo.get());

  pi_ = new PacketIter[numProgressions];
  for(uint32_t prog_iter_num = 0; prog_iter_num < numProgressions; ++prog_iter_num)
  {
    auto pi = pi_ + prog_iter_num;
    pi->init(this, prog_iter_num, tcp, tileBounds_, compression, max_res, max_precincts,
             componentPrecinctInfo.get());
  }
  if(compression)
  {
    bool poc = tcp->hasPoc() && (GRK_IS_CINEMA(cp_->rsiz_) || t2_mode == FINAL_PASS);
    updateCompressTcpProgressions(tcp, image_->numcomps, tileBounds_, max_precincts, max_res,
                                  dx_min, dy_min, poc);
  }
}
PacketManager::~PacketManager()
{
  if(pi_)
  {
    pi_->destroy_include();
    delete[] pi_;
  }
  delete includeTracker_;
}
GrkImage* PacketManager::getImage()
{
  return image_;
}
Rect32 PacketManager::getTileBounds(void)
{
  return tileBounds_;
}
CodingParams* PacketManager::getCodingParams(void)
{
  return cp_;
}
T2_MODE PacketManager::getT2Mode(void)
{
  return t2Mode_;
}
uint32_t PacketManager::getNumProgressions(void)
{
  return tileProcessor_->getTCP()->getNumProgressions();
}
PacketIter* PacketManager::getPacketIter(uint32_t poc) const
{
  return pi_ + poc;
}
TileProcessor* PacketManager::getTileProcessor(void)
{
  return tileProcessor_;
}
void PacketManager::enable_tile_part_generation(uint32_t prog_iter_num, bool first_poc_tile_part,
                                                uint8_t newTilePartProgressionPosition)
{
  (pi_ + prog_iter_num)
      ->enable_tile_part_generation(prog_iter_num, first_poc_tile_part,
                                    newTilePartProgressionPosition);
}
void PacketManager::getParams(const GrkImage* image, CodingParams* p_cp, TileCodingParams* tcp,
                              uint16_t tileno, Rect32* tileBounds, uint32_t* dx_min,
                              uint32_t* dy_min, IncludeTracker* includeTracker,
                              uint64_t* max_precincts, uint8_t* max_res,
                              uint32_t** precinctDimExpByComponent)
{
  assert(p_cp != nullptr);
  assert(image != nullptr);
  assert(tileno < p_cp->t_grid_width_ * p_cp->t_grid_height_);

  uint16_t tile_x = tileno % p_cp->t_grid_width_;
  uint16_t tile_y = tileno / p_cp->t_grid_width_;
  *tileBounds = p_cp->getTileBounds(image->getBounds(), tile_x, tile_y);

  *max_precincts = 0;
  *max_res = 0;
  *dx_min = UINT_MAX;
  *dy_min = UINT_MAX;

  if(includeTracker)
    includeTracker->resetNumPrecinctsPerRes();
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    uint32_t* precinctInfo = nullptr;
    if(precinctDimExpByComponent)
      precinctInfo = precinctDimExpByComponent[compno];

    auto tccp = tcp->tccps_ + compno;
    auto comp = image->comps + compno;

    auto tileCompBounds = tileBounds->scaleDownCeil(comp->dx, comp->dy);
    if(tccp->numresolutions_ > *max_res)
      *max_res = tccp->numresolutions_;

    /* use custom size for precincts*/
    for(uint8_t resno = 0; resno < tccp->numresolutions_; ++resno)
    {
      // 1. precinct dimensions
      auto precWidthExp = tccp->precWidthExp_[resno];
      auto precHeightExp = tccp->precHeightExp_[resno];
      if(precinctInfo)
      {
        *precinctInfo++ = precWidthExp;
        *precinctInfo++ = precHeightExp;
      }

      // 2. precinct grid
      auto resBounds =
          tileCompBounds.scaleDownCeilPow2((uint8_t)(tccp->numresolutions_ - 1U - resno));
      auto resBoundsAdjusted =
          Rect32(floordivpow2(resBounds.x0, precWidthExp) << precWidthExp,
                 floordivpow2(resBounds.y0, precHeightExp) << precHeightExp,
                 ceildivpow2<uint32_t>(resBounds.x1, precWidthExp) << precWidthExp,
                 ceildivpow2<uint32_t>(resBounds.y1, precHeightExp) << precHeightExp);
      uint32_t precinctGridWidth =
          resBounds.width() == 0 ? 0 : resBoundsAdjusted.width() >> precWidthExp;
      uint32_t precinctGridHeight =
          resBounds.height() == 0 ? 0 : resBoundsAdjusted.height() >> precHeightExp;
      if(precinctInfo)
      {
        *precinctInfo++ = precinctGridWidth;
        *precinctInfo++ = precinctGridHeight;
      }
      uint64_t num_precincts = (uint64_t)precinctGridWidth * precinctGridHeight;

      if(includeTracker)
        includeTracker->updateNumPrecinctsPerRes(resno, num_precincts);
      if(num_precincts > *max_precincts)
        *max_precincts = num_precincts;

      // 3. find minimal precinct subsampling factors over all components and resolutions
      uint64_t compResDx =
          comp->dx * ((uint64_t)1u << (precWidthExp + tccp->numresolutions_ - 1U - resno));
      uint64_t compResDy =
          comp->dy * ((uint64_t)1u << (precHeightExp + tccp->numresolutions_ - 1U - resno));
      if(compResDx < UINT_MAX)
        *dx_min = std::min<uint32_t>(*dx_min, (uint32_t)compResDx);
      if(compResDy < UINT_MAX)
        *dy_min = std::min<uint32_t>(*dy_min, (uint32_t)compResDy);
    }
  }
}
void PacketManager::updateCompressTcpProgressions(TileCodingParams* tcp, uint16_t num_comps,
                                                  Rect32 tileBounds, uint64_t max_precincts,
                                                  uint8_t max_res, uint32_t dx_min, uint32_t dy_min,
                                                  bool poc)
{
  for(uint32_t prog_iter_num = 0; prog_iter_num < tcp->getNumProgressions(); ++prog_iter_num)
  {
    auto prog = tcp->progressionOrderChange_ + prog_iter_num;
    prog->progression = poc ? prog->specified_compression_poc_prog : tcp->prg_;
    prog->tp_lay_e = poc ? prog->lay_e : tcp->numLayers_;
    prog->tp_res_s = poc ? prog->res_s : 0;
    prog->tp_res_e = poc ? prog->res_e : max_res;
    prog->tp_comp_s = poc ? prog->comp_s : 0;
    prog->tp_comp_e = poc ? prog->comp_e : num_comps;
    prog->tp_prec_e = max_precincts;
    prog->tp_tx_s = tileBounds.x0;
    prog->tp_ty_s = tileBounds.y0;
    prog->tp_tx_e = tileBounds.x1;
    prog->tp_ty_e = tileBounds.y1;
    prog->dx = dx_min;
    prog->dy = dy_min;
  }
}
void PacketManager::updateCompressParams(const GrkImage* image, CodingParams* p_cp,
                                         TileCodingParams* tcp, uint16_t tileno)
{
  assert(p_cp != nullptr);
  assert(image != nullptr);

  uint8_t max_res;
  uint64_t max_precincts;
  Rect32 tileBounds;
  uint32_t dx_min, dy_min;
  getParams(image, p_cp, tcp, tileno, &tileBounds, &dx_min, &dy_min, nullptr, &max_precincts,
            &max_res, nullptr);
  updateCompressTcpProgressions(tcp, image->numcomps, tileBounds, max_precincts, max_res, dx_min,
                                dy_min, tcp->hasPoc());
}
IncludeTracker* PacketManager::getIncludeTracker(void)
{
  return includeTracker_;
}

} // namespace grk
