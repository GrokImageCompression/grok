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

#include <cinttypes>

#include "grk_taskflow.h"

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"

#include "CodeStream.h"
#include "PacketIter.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "CodeblockCompress.h"

#include "PacketManager.h"
#include "ITileProcessor.h"
#include "ITileProcessorCompress.h"
#include "CodeStreamCompress.h"

namespace grk
{

ResPrecinctInfo::ResPrecinctInfo()
    : precWidthExp(0), precHeightExp(0), precWidthExpPRJ(0), precHeightExpPRJ(0), resOffsetX0PRJ(0),
      resOffsetY0PRJ(0), precWidthPRJ(0), precWidthPRJMinusOne(0), precHeightPRJ(0),
      precHeightPRJMinusOne(0), numPrecincts_(0), dxPRJ(0), dyPRJ(0), resInPrecGridX0(0),
      resInPrecGridY0(0), resno_(0), decompLevel_(0), innerPrecincts_(0), winPrecinctsLeft_(0),
      winPrecinctsRight_(0), winPrecinctsTop_(0), winPrecinctsBottom_(0), valid(false)
{}
bool ResPrecinctInfo::init(uint8_t resno, uint8_t decompLevelX, uint8_t decompLevelY,
                           Rect32 tileBounds, uint32_t compDx, uint32_t compDy, bool windowed,
                           Rect32 tileWindow)
{
  valid = false;
  resno_ = resno;
  // the OPT paths only run when both axes share one level count
  decompLevel_ = decompLevelX;

  uint64_t resDivisorX = (uint64_t)compDx << decompLevelX;
  uint64_t resDivisorY = (uint64_t)compDy << decompLevelY;
  auto res = tileBounds.scaleDownCeil(resDivisorX, resDivisorY);
  if(res.x0 == res.x1 || res.y0 == res.y1)
    return false;

  precWidthExpPRJ = precWidthExp + decompLevelX;
  precHeightExpPRJ = precHeightExp + decompLevelY;

  // offset of projected resolution relative to projected precinct grid
  // (these are both zero when tile origin equals (0,0))
  resOffsetX0PRJ =
      (uint32_t)(((uint64_t)res.x0 << decompLevelX) % ((uint64_t)1 << precWidthExpPRJ));
  resOffsetY0PRJ =
      (uint32_t)(((uint64_t)res.y0 << decompLevelY) % ((uint64_t)1 << precHeightExpPRJ));

  precWidthPRJ = (uint64_t)compDx << precWidthExpPRJ;
  precWidthPRJMinusOne = precWidthPRJ - 1;
  precHeightPRJ = (uint64_t)compDy << precHeightExpPRJ;
  precHeightPRJMinusOne = precHeightPRJ - 1;

  dxPRJ = (uint64_t)compDx << decompLevelX;
  dyPRJ = (uint64_t)compDy << decompLevelY;
  resInPrecGridX0 = floordivpow2(res.x0, precWidthExp);
  resInPrecGridY0 = floordivpow2(res.y0, precHeightExp);
  if(windowed)
  {
    auto window = tileWindow;
    auto resWindow = window.scaleDownCeil(resDivisorX, resDivisorY);
    // pad resolution window to next precinct
    resWindow.grow_IN_PLACE(1U << precWidthExp, 1U << precHeightExp).clip_IN_PLACE(res);
    winPrecGrid = resWindow.scaleDown(1U << precWidthExp, 1U << precHeightExp);
    winPrecPRJ = Rect<uint64_t>(
        (uint64_t)winPrecGrid.origin_x0 * precWidthPRJ,
        (uint64_t)winPrecGrid.origin_y0 * precHeightPRJ, (uint64_t)winPrecGrid.x0 * precWidthPRJ,
        (uint64_t)winPrecGrid.y0 * precHeightPRJ, (uint64_t)winPrecGrid.x1 * precWidthPRJ,
        (uint64_t)winPrecGrid.y1 * precHeightPRJ);
  }

  tileBoundsPrecGrid = res.scaleDown(1U << precWidthExp, 1U << precHeightExp);
  numPrecincts_ = tileBoundsPrecGrid.area();
  tileBoundsPrecPRJ = Rect<uint64_t>((uint64_t)tileBoundsPrecGrid.origin_x0 * precWidthPRJ,
                                     (uint64_t)tileBoundsPrecGrid.origin_y0 * precHeightPRJ,
                                     (uint64_t)tileBoundsPrecGrid.x0 * precWidthPRJ,
                                     (uint64_t)tileBoundsPrecGrid.y0 * precHeightPRJ,
                                     (uint64_t)tileBoundsPrecGrid.x1 * precWidthPRJ,
                                     (uint64_t)tileBoundsPrecGrid.y1 * precHeightPRJ);
  valid = true;

  return true;
}
uint64_t ResPrecinctInfo::precinctsToWindow(uint64_t precinctIndex) const
{
  if(precinctIndex >= numPrecincts_)
    return 0;
  uint64_t width = tileBoundsPrecGrid.width();
  uint64_t row = precinctIndex / width;
  uint64_t column = precinctIndex % width;
  uint64_t windowX0 = winPrecGrid.x0 - tileBoundsPrecGrid.x0;
  uint64_t windowX1 = winPrecGrid.x1 - tileBoundsPrecGrid.x0;
  uint64_t windowY0 = winPrecGrid.y0 - tileBoundsPrecGrid.y0;
  uint64_t windowY1 = winPrecGrid.y1 - tileBoundsPrecGrid.y0;
  if(row >= windowY1 || windowX0 >= windowX1)
    return numPrecincts_ - precinctIndex;
  if(row < windowY0)
    return (windowY0 - row) * width - column + windowX0;
  if(column < windowX0)
    return windowX0 - column;
  if(column >= windowX1)
  {
    if(row + 1 >= windowY1)
      return numPrecincts_ - precinctIndex;
    return width - column + windowX0;
  }
  return 0;
}
void ResPrecinctInfo::print(void)
{
  grklog.info("\n");
  grklog.info("RESOLUTION PRECINCT INFO for resolution level %u", resno_);
  grklog.info("precinct exponents: (%u,%u)", precWidthExp, precHeightExp);
  grklog.info("precinct dimensions (projected): (%" PRIu64 ",%" PRIu64 ")", precWidthPRJ,
              precHeightPRJ);
  grklog.info("number of precincts: %" PRIu64, numPrecincts_);
  grklog.info("subsampling (projected): (%" PRIu64 ",%" PRIu64 ")", dxPRJ, dyPRJ);
  grklog.info("tile bounds aligned to precincts (projected) =>");
  tileBoundsPrecPRJ.print();
  grklog.info("tile bounds mapped to precinct grid (resolution) =>");
  tileBoundsPrecGrid.print();
  grklog.info("window bounds aligned to precincts (projected) =>");
  winPrecPRJ.print();
  grklog.info("window bounds mapped to precinct grid (resolution) =>");
  winPrecGrid.print();
}

PacketIter::PacketIter()
{
  prog = {};
}

PacketIterInfoResolution::PacketIterInfoResolution()
    : precWidthExp(0), precHeightExp(0), precinctGridWidth(0), precinctGridHeight(0),
      precinctInfo(nullptr)
{}
PacketIterInfoResolution::~PacketIterInfoResolution()
{
  delete precinctInfo;
}
PacketIterInfoComponent::PacketIterInfoComponent()
    : dx(0), dy(0), numresolutions(0), resolutions(nullptr)
{}
PacketIterInfoComponent::~PacketIterInfoComponent()
{
  delete[] resolutions;
}

PacketIter::~PacketIter()
{
  delete[] comps;
  delete[] precinctInfoOPT_;
}
void PacketIter::printStaticState(void)
{
  if(precinctInfoOPT_)
  {
    grklog.info("Packet Iterator Static State");
    grklog.info("progression bounds [C-R-P-L] : [%u %u %u %u] ", prog.comp_e, prog.res_e,
                prog.prec_e, prog.lay_e);
    for(uint8_t resno = 0; resno < comps->numresolutions; resno++)
    {
      auto inf = precinctInfoOPT_ + resno;
      inf->print();
    }
  }
}
void PacketIter::printDynamicState(void)
{
  if(precinctInfoOPT_)
  {
    grklog.info("Packet Iterator Dynamic State");
    grklog.info("progression state [C-R-P-L] : [%u %u (%u,%u) %u] ", compno, resno, x, y, layno);
    grklog.info("precinct index: %" PRIu64 ".", precinctIndex);
  }
}

/**
 * Try the OPT path first; if preconditions aren't met, fall back to
 * per-component, per-resolution init.
 */
void PacketIter::genPrecinctInfo()
{
  if(!genPrecinctInfoOPT())
  {
    for(uint16_t c = 0; c < numcomps; ++c)
    {
      auto comp = comps + c;
      for(uint8_t r = 0; r < comp->numresolutions; ++r)
      {
        auto res = comp->resolutions + r;
        genPrecinctInfo(comp, res, r);
      }
    }
  }
}
/**
 * Non-OPT per-resolution precinct info generation.
 * Allocates and initializes a ResPrecinctInfo for each non-degenerate resolution.
 * Skipped during compression (precinct info is computed on-the-fly via validatePrecinct).
 */
void PacketIter::genPrecinctInfo(PacketIterInfoComponent* comp, PacketIterInfoResolution* res,
                                 uint8_t resNumber)
{
  if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
    return;

  if(compression_)
    return;

  ResPrecinctInfo* rpInfo = new ResPrecinctInfo();
  rpInfo->precWidthExp = res->precWidthExp;
  rpInfo->precHeightExp = res->precHeightExp;
  uint8_t levelsDone = (uint8_t)(comp->numresolutions - 1U - resNumber);
  if(rpInfo->init(resNumber, comp->horizontalDepth[levelsDone], comp->verticalDepth[levelsDone],
                  packetManager->getTileBounds(), comp->dx, comp->dy, !isWholeTile(),
                  packetManager->getTileProcessor()->getUnreducedTileWindow()))
  {
    res->precinctInfo = rpInfo;
  }
  else
  {
    delete rpInfo;
  }
}

/**
 * Attempt to use the optimized precinct info path.
 *
 * Preconditions for OPT:
 * 1. Decompression (not compression)
 * 2. Single progression (no POC)
 * 3. No subsampling (all dx=dy=1)
 * 4. All components have the same number of resolutions
 * 5. For PCRL/CPRL: projected precinct sizes are non-decreasing as resolution decreases
 *    (ensures the highest-resolution precinct grid covers all lower-resolution precincts)
 *    The tile origin may sit inside a precinct: the spatial loops start on the highest
 *    resolution's precinct grid and emit such a precinct at their first position.
 *
 * The OPT path uses per-resolution precinct step sizes for spatial
 * progressions (RPCL), avoiding degenerate iteration when tiles have
 * large dimensions or many resolution levels combined with small
 * global dx/dy.
 *
 * @return true if OPT path was used, false if caller should use the non-OPT path
 */
bool PacketIter::genPrecinctInfoOPT(void)
{
  if(compression_ || !singleProgression_)
    return false;

  auto tb = packetManager->getTileBounds();
  // no subsampling, every level splits both axes
  for(uint16_t compno = 0; compno < numcomps; ++compno)
  {
    auto comp = comps + compno;
    if(comp->dx != 1 || comp->dy != 1 || comp->usesPart2Transform)
      return false;
    if(compno > 0 && comp->numresolutions != comps->numresolutions)
      return false;
  }
  switch(prog.progression)
  {
    case GRK_LRCP:
    case GRK_RLCP:
    case GRK_RPCL:
      break;
    case GRK_PCRL:
    case GRK_CPRL:
      // if P occurs before R, then we must ensure that for all resolutions, the precinct
      // projected onto canvas is a "multiple" of the highest resolution precinct,
      // so that the P loops covers all precincts from all resolutions
      {
        auto highestRes = comps->resolutions + comps->numresolutions - 1;
        for(uint8_t resno = 0; resno < comps->numresolutions - 1; ++resno)
        {
          auto res = comps->resolutions + resno;
          auto decompLevel = (uint8_t)(comps->numresolutions - 1U - resno);
          if(res->precWidthExp + decompLevel < highestRes->precWidthExp ||
             res->precHeightExp + decompLevel < highestRes->precHeightExp)
            return false;
        }
      }
      break;
    default:
      break;
  }

  precinctInfoOPT_ = new ResPrecinctInfo[comps->numresolutions];
  for(uint8_t resno = 0; resno < comps->numresolutions; ++resno)
  {
    auto inf = precinctInfoOPT_ + resno;
    auto res = comps->resolutions + resno;
    inf->precWidthExp = res->precWidthExp;
    inf->precHeightExp = res->precHeightExp;
    uint8_t levelsDone = (uint8_t)(comps->numresolutions - 1U - resno);
    inf->init(resno, levelsDone, levelsDone, tb, comps->dx, comps->dy, !isWholeTile(),
              packetManager->getTileProcessor()->getUnreducedTileWindow());
  }

  return true;
}
/**
 * Select the precinct info covering one component and resolution.
 */
ResPrecinctInfo* PacketIter::precinctInfoFor(uint16_t componentIndex, uint8_t resolutionIndex,
                                             ResPrecinctInfo* compressionScratch)
{
  auto comp = comps + componentIndex;
  if(resolutionIndex >= comp->numresolutions)
    return nullptr;
  auto res = comp->resolutions + resolutionIndex;
  if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
    return nullptr;

  if(precinctInfoOPT_)
  {
    auto rpInfo = precinctInfoOPT_ + resolutionIndex;
    return rpInfo->valid ? rpInfo : nullptr;
  }
  if(compression_)
  {
    compressionScratch->precWidthExp = res->precWidthExp;
    compressionScratch->precHeightExp = res->precHeightExp;
    uint8_t levelsDone = (uint8_t)(comp->numresolutions - 1U - resolutionIndex);
    if(!compressionScratch->init(resolutionIndex, comp->horizontalDepth[levelsDone],
                                 comp->verticalDepth[levelsDone], packetManager->getTileBounds(),
                                 comp->dx, comp->dy, !isWholeTile(),
                                 packetManager->getTileProcessor()->getUnreducedTileWindow()))
    {
      return nullptr;
    }
    return compressionScratch->valid ? compressionScratch : nullptr;
  }
  auto rpInfo = res->precinctInfo;
  if(!rpInfo || !rpInfo->valid)
    return nullptr;

  return rpInfo;
}
/**
 * Validate that the current (compno, resno, x, y) maps to a real precinct.
 * On success, sets px0grid_ and py0grid_ for use by generatePrecinctIndex().
 */
bool PacketIter::validatePrecinct(void)
{
  ResPrecinctInfo compressionScratch;
  auto rpInfo = precinctInfoFor(compno, resno, &compressionScratch);
  if(!rpInfo)
    return false;

  return genPrecinctY0Grid(rpInfo) && genPrecinctX0Grid(rpInfo);
}
bool PacketIter::anyPrecinctStartsAtY(uint16_t componentStart, uint16_t componentEnd,
                                      uint8_t resolutionStart, uint8_t resolutionEnd)
{
  ResPrecinctInfo compressionScratch;
  for(uint16_t componentIndex = componentStart; componentIndex < componentEnd; ++componentIndex)
  {
    for(uint8_t resolutionIndex = resolutionStart; resolutionIndex < resolutionEnd;
        ++resolutionIndex)
    {
      auto rpInfo = precinctInfoFor(componentIndex, resolutionIndex, &compressionScratch);
      if(rpInfo && precinctStartsAtY(rpInfo))
        return true;
    }
  }

  return false;
}
void PacketIter::generatePrecinctIndex(void)
{
  auto comp = comps + compno;
  auto res = comp->resolutions + resno;
  precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;
}

/**
 * Check if there is a remaining valid progression order
 */
bool PacketIter::checkForRemainingValidProgression(int32_t prog, uint32_t prog_iter_num,
                                                   const char* progString)
{
  if(prog < 0)
    return false;
  auto poc = packetManager->getTileProcessor()->getTCP()->progressionOrderChange_ + prog_iter_num;

  switch(progString[prog])
  {
    case 'R':
      if(poc->res_temp == poc->tp_res_e)
        return checkForRemainingValidProgression(prog - 1, prog_iter_num, progString);
      else
        return true;
      break;
    case 'C':
      if(poc->comp_temp == poc->tp_comp_e)
        return checkForRemainingValidProgression(prog - 1, prog_iter_num, progString);
      else
        return true;
      break;
    case 'L':
      if(poc->lay_temp == poc->tp_lay_e)
        return checkForRemainingValidProgression(prog - 1, prog_iter_num, progString);
      else
        return true;
      break;
    case 'P':
      switch(poc->progression)
      {
        case GRK_LRCP:
          [[fallthrough]];
        case GRK_RLCP:
          if(poc->prec_temp == poc->tp_prec_e)
            return checkForRemainingValidProgression(prog - 1, prog_iter_num, progString);
          else
            return true;
          break;
        default:
          if(poc->tx0_temp == poc->tp_tx_e)
          {
            /*TY*/
            if(poc->ty0_temp == poc->tp_ty_e)
              return checkForRemainingValidProgression(prog - 1, prog_iter_num, progString);
            else
              return true;
            /*TY*/
          }
          else
          {
            return true;
          }
          break;
      }
  }

  return false;
}
void PacketIter::enable_tile_part_generation(uint32_t prog_iter_num, bool first_poc_tile_part,
                                             uint8_t newTilePartProgressionPosition)
{
  auto cp = packetManager->getCodingParams();
  auto tcp = packetManager->getTileProcessor()->getTCP();
  auto poc = tcp->progressionOrderChange_ + prog_iter_num;
  auto pocProg = CodeStreamCompress::convertProgressionOrder(poc->progression);
  prog.progression = poc->progression;

  if(cp->codingParams_.enc_.enableTilePartGeneration_ &&
     (GRK_IS_CINEMA(cp->rsiz_) || GRK_IS_IMF(cp->rsiz_) ||
      packetManager->getT2Mode() == FINAL_PASS))
  {
    for(uint8_t i = newTilePartProgressionPosition + 1; i < 4; i++)
    {
      switch(pocProg[i])
      {
        case 'R':
          prog.res_s = poc->tp_res_s;
          prog.res_e = poc->tp_res_e;
          break;
        case 'C':
          prog.comp_s = poc->tp_comp_s;
          prog.comp_e = poc->tp_comp_e;
          break;
        case 'L':
          prog.lay_s = 0;
          prog.lay_e = poc->tp_lay_e;
          break;
        case 'P':
          switch(poc->progression)
          {
            case GRK_LRCP:
            case GRK_RLCP:
              prog.prec_s = 0;
              prog.prec_e = poc->tp_prec_e;
              break;
            default:
              prog.tx0 = poc->tp_tx_s;
              prog.ty0 = poc->tp_ty_s;
              prog.tx1 = poc->tp_tx_e;
              prog.ty1 = poc->tp_ty_e;
              break;
          }
          break;
      }
    }
    if(first_poc_tile_part)
    {
      for(int8_t i = (int8_t)newTilePartProgressionPosition; i >= 0; i--)
      {
        switch(pocProg[i])
        {
          case 'C':
            poc->comp_temp = poc->tp_comp_s;
            prog.comp_s = poc->comp_temp;
            prog.comp_e = poc->comp_temp + 1U;
            poc->comp_temp = poc->comp_temp + 1U;
            break;
          case 'R':
            poc->res_temp = poc->tp_res_s;
            prog.res_s = poc->res_temp;
            prog.res_e = poc->res_temp + 1U;
            poc->res_temp = poc->res_temp + 1U;
            break;
          case 'L':
            poc->lay_temp = 0;
            prog.lay_s = poc->lay_temp;
            prog.lay_e = poc->lay_temp + 1U;
            poc->lay_temp = poc->lay_temp + 1U;
            break;
          case 'P':
            switch(poc->progression)
            {
              case GRK_LRCP:
              case GRK_RLCP:
                poc->prec_temp = 0;
                prog.prec_s = poc->prec_temp;
                prog.prec_e = poc->prec_temp + 1U;
                poc->prec_temp += 1;
                break;
              default:
                poc->tx0_temp = poc->tp_tx_s;
                poc->ty0_temp = poc->tp_ty_s;
                prog.tx0 = poc->tx0_temp;
                prog.tx1 = (poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
                prog.ty0 = poc->ty0_temp;
                prog.ty1 = (poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
                poc->tx0_temp = prog.tx1;
                poc->ty0_temp = prog.ty1;
                break;
            }
            break;
        }
      }
    }
    else
    {
      uint32_t incr_top = 1;
      uint32_t resetX = 0;
      for(int8_t i = (int8_t)newTilePartProgressionPosition; i >= 0; i--)
      {
        switch(pocProg[i])
        {
          case 'C':
            prog.comp_s = uint16_t(poc->comp_temp - 1);
            prog.comp_e = poc->comp_temp;
            break;
          case 'R':
            prog.res_s = uint8_t(poc->res_temp - 1);
            prog.res_e = poc->res_temp;
            break;
          case 'L':
            prog.lay_s = uint16_t(poc->lay_temp - 1);
            prog.lay_e = poc->lay_temp;
            break;
          case 'P':
            switch(poc->progression)
            {
              case GRK_LRCP:
              case GRK_RLCP:
                prog.prec_s = poc->prec_temp - 1;
                prog.prec_e = poc->prec_temp;
                break;
              default:
                prog.tx0 = (poc->tx0_temp - poc->dx - (poc->tx0_temp % poc->dx));
                prog.tx1 = poc->tx0_temp;
                prog.ty0 = (poc->ty0_temp - poc->dy - (poc->ty0_temp % poc->dy));
                prog.ty1 = poc->ty0_temp;
                break;
            }
            break;
        }
        if(incr_top == 1)
        {
          switch(pocProg[i])
          {
            case 'R':
              if(poc->res_temp == poc->tp_res_e)
              {
                if(checkForRemainingValidProgression(i - 1, prog_iter_num, pocProg))
                {
                  poc->res_temp = poc->tp_res_s;
                  prog.res_s = poc->res_temp;
                  prog.res_e = poc->res_temp + 1U;
                  poc->res_temp = poc->res_temp + 1U;
                  incr_top = 1;
                }
                else
                {
                  incr_top = 0;
                }
              }
              else
              {
                prog.res_s = poc->res_temp;
                prog.res_e = poc->res_temp + 1U;
                poc->res_temp = poc->res_temp + 1U;
                incr_top = 0;
              }
              break;
            case 'C':
              if(poc->comp_temp == poc->tp_comp_e)
              {
                if(checkForRemainingValidProgression(i - 1, prog_iter_num, pocProg))
                {
                  poc->comp_temp = poc->tp_comp_s;
                  prog.comp_s = poc->comp_temp;
                  prog.comp_e = poc->comp_temp + 1U;
                  poc->comp_temp = poc->comp_temp + 1U;
                  incr_top = 1;
                }
                else
                {
                  incr_top = 0;
                }
              }
              else
              {
                prog.comp_s = poc->comp_temp;
                prog.comp_e = poc->comp_temp + 1U;
                poc->comp_temp = poc->comp_temp + 1U;
                incr_top = 0;
              }
              break;
            case 'L':
              if(poc->lay_temp == poc->tp_lay_e)
              {
                if(checkForRemainingValidProgression(i - 1, prog_iter_num, pocProg))
                {
                  poc->lay_temp = 0;
                  prog.lay_s = poc->lay_temp;
                  prog.lay_e = poc->lay_temp + 1U;
                  poc->lay_temp = poc->lay_temp + 1U;
                  incr_top = 1;
                }
                else
                {
                  incr_top = 0;
                }
              }
              else
              {
                prog.lay_s = poc->lay_temp;
                prog.lay_e = poc->lay_temp + 1U;
                poc->lay_temp = poc->lay_temp + 1U;
                incr_top = 0;
              }
              break;
            case 'P':
              switch(poc->progression)
              {
                case GRK_LRCP:
                case GRK_RLCP:
                  if(poc->prec_temp == poc->tp_prec_e)
                  {
                    if(checkForRemainingValidProgression(i - 1, prog_iter_num, pocProg))
                    {
                      poc->prec_temp = 0;
                      prog.prec_s = poc->prec_temp;
                      prog.prec_e = poc->prec_temp + 1;
                      poc->prec_temp += 1;
                      incr_top = 1;
                    }
                    else
                    {
                      incr_top = 0;
                    }
                  }
                  else
                  {
                    prog.prec_s = poc->prec_temp;
                    prog.prec_e = poc->prec_temp + 1;
                    poc->prec_temp += 1;
                    incr_top = 0;
                  }
                  break;
                default:
                  if(poc->tx0_temp >= poc->tp_tx_e)
                  {
                    if(poc->ty0_temp >= poc->tp_ty_e)
                    {
                      if(checkForRemainingValidProgression(i - 1, prog_iter_num, pocProg))
                      {
                        poc->ty0_temp = poc->tp_ty_s;
                        prog.ty0 = poc->ty0_temp;
                        prog.ty1 = (uint32_t)(poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
                        poc->ty0_temp = prog.ty1;
                        incr_top = 1;
                        resetX = 1;
                      }
                      else
                      {
                        incr_top = 0;
                        resetX = 0;
                      }
                    }
                    else
                    {
                      prog.ty0 = poc->ty0_temp;
                      prog.ty1 = (poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
                      poc->ty0_temp = prog.ty1;
                      incr_top = 0;
                      resetX = 1;
                    }
                    if(resetX == 1)
                    {
                      poc->tx0_temp = poc->tp_tx_s;
                      prog.tx0 = poc->tx0_temp;
                      prog.tx1 = (uint32_t)(poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
                      poc->tx0_temp = prog.tx1;
                    }
                  }
                  else
                  {
                    prog.tx0 = poc->tx0_temp;
                    prog.tx1 = (uint32_t)(poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
                    poc->tx0_temp = prog.tx1;
                    incr_top = 0;
                  }
                  break;
              }
              break;
          }
        }
      }
    }
  }
  else
  {
    prog.lay_s = 0;
    prog.lay_e = poc->tp_lay_e;
    prog.res_s = poc->tp_res_s;
    prog.res_e = poc->tp_res_e;
    prog.comp_s = poc->tp_comp_s;
    prog.comp_e = poc->tp_comp_e;
    prog.prec_s = 0;
    prog.prec_e = poc->tp_prec_e;
    prog.tx0 = poc->tp_tx_s;
    prog.ty0 = poc->tp_ty_s;
    prog.tx1 = poc->tp_tx_e;
    prog.ty1 = poc->tp_ty_e;
  }
}
GRK_PROG_ORDER PacketIter::getProgression(void) const
{
  return prog.progression;
}
uint16_t PacketIter::getCompno(void) const
{
  return compno;
}
uint8_t PacketIter::getResno(void) const
{
  return resno;
}
uint64_t PacketIter::getPrecinctIndex(void) const
{
  return precinctIndex;
}
uint16_t PacketIter::getLayno(void) const
{
  return layno;
}
bool PacketIter::update_include(void)
{
  if(singleProgression_)
    return true;
  return packetManager->getIncludeTracker()->update(layno, resno, compno, precinctIndex);
}
void PacketIter::destroy_include(void)
{
  packetManager->getIncludeTracker()->clear();
}
bool PacketIter::precInfoCheck(ResPrecinctInfo* rpInfo)
{
  if(!rpInfo->valid)
    return false;
  if(resno >= comps->numresolutions)
    return false;
  auto res = comps->resolutions + resno;

  return (res->precinctGridWidth > 0 && res->precinctGridHeight > 0);
}

bool PacketIter::precinctStartsAtY(const ResPrecinctInfo* rpInfo) const
{
  return (y % rpInfo->precHeightPRJ == 0) ||
         ((y == packetManager->getTileBounds().y0) && rpInfo->resOffsetY0PRJ);
}
bool PacketIter::genPrecinctY0Grid(ResPrecinctInfo* rpInfo)
{
  if(!precinctStartsAtY(rpInfo))
    return false;

  py0grid_ =
      floordivpow2(ceildiv(y, rpInfo->dyPRJ), rpInfo->precHeightExp) - rpInfo->resInPrecGridY0;

  return true;
}
bool PacketIter::genPrecinctX0Grid(ResPrecinctInfo* rpInfo)
{
  if(!((x % rpInfo->precWidthPRJ == 0) ||
       ((x == packetManager->getTileBounds().x0) && rpInfo->resOffsetX0PRJ)))
    return false;

  px0grid_ =
      floordivpow2(ceildiv(x, rpInfo->dxPRJ), rpInfo->precWidthExp) - rpInfo->resInPrecGridX0;

  return true;
}

bool PacketIter::precinctRowStartsOPT(const ResPrecinctInfo* rpInfo, uint64_t yPos) const
{
  return (yPos & rpInfo->precHeightPRJMinusOne) == 0 ||
         (yPos == spatialStartY_ && rpInfo->resOffsetY0PRJ);
}
bool PacketIter::precinctColumnStartsOPT(const ResPrecinctInfo* rpInfo, uint64_t xPos) const
{
  return (xPos & rpInfo->precWidthPRJMinusOne) == 0 ||
         (xPos == spatialStartX_ && rpInfo->resOffsetX0PRJ);
}
bool PacketIter::genPrecinctY0GridPCRL_OPT(ResPrecinctInfo* rpInfo)
{
  if(!precinctRowStartsOPT(rpInfo, y))
    return false;

  py0grid_ = ((uint32_t)ceildivpow2(y, rpInfo->decompLevel_) >> rpInfo->precHeightExp) -
             rpInfo->resInPrecGridY0;
  return true;
}
bool PacketIter::genPrecinctX0GridPCRL_OPT(ResPrecinctInfo* rpInfo)
{
  if(!precinctColumnStartsOPT(rpInfo, x))
    return false;

  px0grid_ = ((uint32_t)ceildivpow2(x, rpInfo->decompLevel_) >> rpInfo->precWidthExp) -
             rpInfo->resInPrecGridX0;
  return true;
}
void PacketIter::genPrecinctY0GridRPCL_OPT(ResPrecinctInfo* rpInfo)
{
  py0grid_ = (uint32_t)(ceildivpow2(y, rpInfo->decompLevel_) >> rpInfo->precHeightExp) -
             rpInfo->resInPrecGridY0;
}
void PacketIter::genPrecinctX0GridRPCL_OPT(ResPrecinctInfo* rpInfo)
{
  px0grid_ = (uint32_t)(ceildivpow2(x, rpInfo->decompLevel_) >> rpInfo->precWidthExp) -
             rpInfo->resInPrecGridX0;
}
/**
 * Compute the minimum spatial step sizes (dx, dy) across all components and
 * resolutions. These are used as the x/y loop increments in spatial progression
 * orders (PCRL, RPCL, CPRL).
 *
 * For each resolution, the projected precinct width is:
 *   comp->dx * 2^(precWidthExp + numresolutions - 1 - resno)
 *
 * The global dx is the minimum of all such values that fit in uint32_t.
 * If ALL values exceed UINT_MAX, dx remains 0, which is detected by next()
 * to prevent infinite loops.
 */
void PacketIter::update_dxy(void)
{
  dx = 0;
  dy = 0;
  for(uint16_t compno = 0; compno < numcomps; compno++)
    update_dxy_for_comp(comps + compno, false);
  dxActive = dx > 0 ? (uint32_t)(dx - (x % dx)) : 0;
  dyActive = dy > 0 ? (uint32_t)(dy - (y % dy)) : 0;
}
void PacketIter::update_dxy_for_comp(PacketIterInfoComponent* comp, bool updateActive)
{
  for(uint8_t resno = 0; resno < comp->numresolutions; resno++)
  {
    auto res = comp->resolutions + resno;
    uint8_t levelsDone = (uint8_t)(comp->numresolutions - 1 - resno);
    uint64_t dx_temp =
        comp->dx *
        ((uint64_t)1u << (uint8_t)(res->precWidthExp + comp->horizontalDepth[levelsDone]));
    uint64_t dy_temp =
        comp->dy *
        ((uint64_t)1u << (uint8_t)(res->precHeightExp + comp->verticalDepth[levelsDone]));
    if(dx_temp < UINT_MAX)
      dx = !dx ? (uint32_t)dx_temp : std::min<uint32_t>(dx, (uint32_t)dx_temp);
    if(dy_temp < UINT_MAX)
      dy = !dy ? (uint32_t)dy_temp : std::min<uint32_t>(dy, (uint32_t)dy_temp);
  }
  if(updateActive)
  {
    dxActive = (uint32_t)(dx - (x % dx));
    dyActive = (uint32_t)(dy - (y % dy));
  }
}
void PacketIter::init(PacketManager* packetMan, uint32_t pocIndex, TileCodingParams* tcp,
                      Rect32 tileBounds, bool compression, uint8_t max_res, uint64_t max_precincts,
                      uint32_t** componentPrecinctInfo)
{
  packetManager = packetMan;
  maxNumDecompositionResolutions =
      packetManager->getTileProcessor()->getMaxNumDecompressResolutions();
  singleProgression_ = packetManager->getNumProgressions() == 1;
  compression_ = compression;
  auto image = packetMan->getImage();
  comps = new PacketIterInfoComponent[image->numcomps];
  numcomps = image->numcomps;
  for(uint16_t compno = 0; compno < numcomps; ++compno)
  {
    auto img_comp = image->comps + compno;
    auto comp = comps + compno;
    auto tccp = tcp->tccps_ + compno;

    comp->resolutions = new PacketIterInfoResolution[tccp->numresolutions_];
    comp->numresolutions = tccp->numresolutions_;
    comp->dx = img_comp->dx;
    comp->dy = img_comp->dy;
    memcpy(comp->horizontalDepth, tccp->horizontalDepth_, sizeof(comp->horizontalDepth));
    memcpy(comp->verticalDepth, tccp->verticalDepth_, sizeof(comp->verticalDepth));
    comp->usesPart2Transform = tccp->usesPart2Transform();
  }
  bool hasPoc = tcp->hasPoc();
  if(!compression)
  {
    auto poc = tcp->progressionOrderChange_ + pocIndex;

    prog.progression = hasPoc ? poc->progression : tcp->prg_;
    prog.lay_s = 0;
    prog.lay_e = std::min<uint16_t>(hasPoc ? poc->lay_e : tcp->numLayers_, tcp->numLayers_);
    prog.res_s = hasPoc ? poc->res_s : 0;
    prog.res_e = std::min<uint8_t>(hasPoc ? poc->res_e : max_res, max_res);
    prog.comp_s = hasPoc ? poc->comp_s : 0;
    prog.comp_e = std::min<uint16_t>(hasPoc ? poc->comp_e : numcomps, numcomps);
    prog.prec_s = 0;
    prog.prec_e = max_precincts;
  }
  prog.tx0 = tileBounds.x0;
  prog.ty0 = tileBounds.y0;
  prog.tx1 = tileBounds.x1;
  prog.ty1 = tileBounds.y1;
  x = prog.tx0;
  y = prog.ty0;
  spatialStartX_ = x;
  spatialStartY_ = y;

  // generate precinct grids
  for(uint16_t compno = 0; compno < numcomps; ++compno)
  {
    auto current_comp = comps + compno;
    auto precinctExp = componentPrecinctInfo[compno];
    /* resolutions have already been initialized */
    for(uint8_t resno = 0; resno < current_comp->numresolutions; resno++)
    {
      auto res = current_comp->resolutions + resno;

      res->precWidthExp = (uint8_t)(*(precinctExp++));
      res->precHeightExp = (uint8_t)(*(precinctExp++));
      res->precinctGridWidth = *(precinctExp++);
      res->precinctGridHeight = *(precinctExp++);
    }
  }
  genPrecinctInfo();
  update_dxy();
  // the highest resolution's precinct holding the tile origin can start before it
  if(precinctInfoOPT_ && (prog.progression == GRK_PCRL || prog.progression == GRK_CPRL))
  {
    auto highest = precinctInfoOPT_ + prog.res_e - 1;
    if(highest->valid)
    {
      spatialStartX_ = highest->tileBoundsPrecPRJ.x0;
      spatialStartY_ = highest->tileBoundsPrecPRJ.y0;
      x = (uint32_t)spatialStartX_;
      y = (uint32_t)spatialStartY_;
    }
  }

  // single progression optimizations
  if(singleProgression_)
  {
    switch(prog.progression)
    {
      case GRK_LRCP:
        prog.lay_e = (std::min)(prog.lay_e,
                                packetManager->getTileProcessor()->getTCP()->layersToDecompress_);
        break;
      case GRK_RLCP:
        prog.res_e = (std::min)(prog.res_e, maxNumDecompositionResolutions);
        break;
      case GRK_RPCL:
        prog.res_e = (std::min)(prog.res_e, maxNumDecompositionResolutions);
        if(precinctInfoOPT_)
        {
          for(uint8_t resno = 0; resno < comps->numresolutions; ++resno)
          {
            auto inf = precinctInfoOPT_ + resno;
            inf->innerPrecincts_ = (uint64_t)prog.comp_e * prog.lay_e;
            auto compLayer = inf->innerPrecincts_;
            inf->winPrecinctsLeft_ =
                (uint64_t)(inf->winPrecGrid.x0 - inf->tileBoundsPrecGrid.x0) * compLayer;
            inf->winPrecinctsRight_ =
                (uint64_t)(inf->tileBoundsPrecGrid.x1 - inf->winPrecGrid.x1) * compLayer;
            inf->winPrecinctsTop_ = (uint64_t)(inf->winPrecGrid.y0 - inf->tileBoundsPrecGrid.y0) *
                                    inf->tileBoundsPrecGrid.width() * compLayer;
            inf->winPrecinctsBottom_ =
                (uint64_t)(inf->tileBoundsPrecGrid.y1 - inf->winPrecGrid.y1) *
                inf->tileBoundsPrecGrid.width() * compLayer;
          }
        }
        break;
      case GRK_PCRL:
        break;
      case GRK_CPRL:
        break;
      default:
        break;
    }
  }
}
bool PacketIter::isWholeTile(void)
{
  return compression_ || packetManager->getTileProcessor()->getTCP()->wholeTileDecompress_;
}

bool PacketIter::next(SparseBuffer* compressedPackets)
{
  // spatial next_* still walks the tile when lay_e is 0
  if(prog.lay_s >= prog.lay_e || prog.res_s >= prog.res_e || prog.comp_s >= prog.comp_e)
    return false;

  // OPT path: per-resolution precinct info avoids degenerate spatial
  // iteration for tiles with large dimensions or many resolution levels.
  if(precinctInfoOPT_)
  {
    switch(prog.progression)
    {
      case GRK_LRCP:
        return next_lrcpOPT(compressedPackets);
      case GRK_RLCP:
        return next_rlcpOPT(compressedPackets);
      case GRK_PCRL:
        return next_pcrlOPT(compressedPackets);
      case GRK_RPCL:
        return next_rpclOPT(compressedPackets);
      case GRK_CPRL:
        return next_cprlOPT(compressedPackets);
      default:
        return false;
    }
  }

  // Non-OPT fallback: compression, multiple progressions, subsampling, etc.
  switch(prog.progression)
  {
    case GRK_LRCP:
      return next_lrcp();
    case GRK_RLCP:
      return next_rlcp();
    case GRK_PCRL:
    case GRK_RPCL:
    case GRK_CPRL:
    case GRK_PRCL:
      // spatial progressions require non-zero step sizes to avoid infinite loops
      if(dx == 0 || dy == 0)
        return false;
      switch(prog.progression)
      {
        case GRK_PCRL:
          return next_pcrl();
        case GRK_RPCL:
          return next_rpcl(compressedPackets);
        case GRK_PRCL:
          return next_prcl();
        default: // GRK_CPRL
          return next_cprl(compressedPackets);
      }
    default:
      return false;
  }
}

bool PacketIter::next_cprl(SparseBuffer*)
{
  for(; compno < prog.comp_e; compno++)
  {
    auto comp = comps + compno;
    for(; y < prog.ty1; y += dyActive, dyActive = dy)
    {
      // no precinct starts on this row
      if(!anyPrecinctStartsAtY(compno, (uint16_t)(compno + 1), prog.res_s, prog.res_e))
        continue;
      for(; x < prog.tx1; x += dxActive, dxActive = dx)
      {
        for(; resno < prog.res_e; resno++)
        {
          if(!validatePrecinct())
            continue;
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            generatePrecinctIndex();
            if(update_include())
            {
              return true;
            }
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        resno = prog.res_s;
      }
      x = prog.tx0;
      dxActive = (uint32_t)(dx - (x % dx));
    }
    y = prog.ty0;
    dx = 0;
    dy = 0;
    update_dxy_for_comp(comp, true);
  }

  return false;
}
bool PacketIter::next_pcrl()
{
  for(; y < prog.ty1; y += dyActive, dyActive = dy)
  {
    // no precinct starts on this row
    if(!anyPrecinctStartsAtY(prog.comp_s, prog.comp_e, prog.res_s, prog.res_e))
      continue;
    for(; x < prog.tx1; x += dxActive, dxActive = dx)
    {
      // windowed decode:
      // bail out if we reach a precinct which is past the
      // bottom, right hand corner of the tile window
      if(singleProgression_)
      {
        auto win = packetManager->getTileProcessor()->getUnreducedTileWindow();
        if(!win.empty() && (y >= win.y1 || (win.y1 > 0 && y == win.y1 - 1 && x >= win.x1)))
          return false;
      }
      for(; compno < prog.comp_e; compno++)
      {
        for(; resno < prog.res_e; resno++)
        {
          if(!validatePrecinct())
            continue;
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            generatePrecinctIndex();
            if(update_include())
              return true;
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        resno = prog.res_s;
      }
      compno = prog.comp_s;
    }
    x = prog.tx0;
    dxActive = (uint32_t)(dx - (x % dx));
  }

  return false;
}
bool PacketIter::next_prcl()
{
  for(; y < prog.ty1; y += dyActive, dyActive = dy)
  {
    // no precinct starts on this row
    if(!anyPrecinctStartsAtY(prog.comp_s, prog.comp_e, prog.res_s, prog.res_e))
      continue;
    for(; x < prog.tx1; x += dxActive, dxActive = dx)
    {
      for(; resno < prog.res_e; resno++)
      {
        for(; compno < prog.comp_e; compno++)
        {
          if(!validatePrecinct())
            continue;
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            generatePrecinctIndex();
            if(update_include())
              return true;
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        compno = prog.comp_s;
      }
      resno = prog.res_s;
    }
    x = prog.tx0;
    dxActive = (uint32_t)(dx - (x % dx));
  }

  return false;
}
bool PacketIter::next_lrcp()
{
  for(; layno < prog.lay_e; layno++)
  {
    for(; resno < prog.res_e; resno++)
    {
      for(; compno < prog.comp_e; compno++)
      {
        auto comp = comps + compno;
        if(resno >= comp->numresolutions)
          continue;
        auto res = comp->resolutions + resno;
        uint64_t prec_e = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
        if(incrementInner)
          precinctIndex++;
        if(precinctIndex < prec_e)
        {
          incrementInner = true;
          if(update_include())
            return true;
        }
        precinctIndex = prog.prec_s;
        incrementInner = false;
      }
      compno = prog.comp_s;
    }
    resno = prog.res_s;
  }

  return false;
}
bool PacketIter::next_rlcp()
{
  for(; resno < prog.res_e; resno++)
  {
    for(; layno < prog.lay_e; layno++)
    {
      for(; compno < prog.comp_e; compno++)
      {
        auto comp = comps + compno;
        if(resno >= comp->numresolutions)
          continue;
        auto res = comp->resolutions + resno;
        uint64_t prec_e = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
        if(incrementInner)
          precinctIndex++;
        if(precinctIndex < prec_e)
        {
          incrementInner = true;
          if(update_include())
            return true;
        }
        precinctIndex = prog.prec_s;
        incrementInner = false;
      }
      compno = prog.comp_s;
    }
    layno = prog.lay_s;
  }

  return false;
}
bool PacketIter::next_rpcl(SparseBuffer*)
{
  for(; resno < prog.res_e; resno++)
  {
    // if all remaining components have degenerate precinct grid, then
    // skip this resolution
    bool sane = false;
    for(uint16_t compnoTmp = compno; compnoTmp < prog.comp_e; compnoTmp++)
    {
      auto comp = comps + compnoTmp;
      if(resno >= comp->numresolutions)
        continue;
      auto res = comp->resolutions + resno;
      if(res->precinctGridWidth > 0 && res->precinctGridHeight > 0)
      {
        sane = true;
        break;
      }
    }
    if(!sane)
      continue;

    for(; y < prog.ty1; y += dyActive, dyActive = dy)
    {
      // no precinct starts on this row
      if(!anyPrecinctStartsAtY(prog.comp_s, prog.comp_e, resno, (uint8_t)(resno + 1)))
        continue;
      for(; x < prog.tx1; x += dxActive, dxActive = dx)
      {
        // calculate x
        for(; compno < prog.comp_e; compno++)
        {
          if(!validatePrecinct())
            continue;
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            generatePrecinctIndex();
            if(update_include())
              return true;
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        compno = prog.comp_s;
      }
      x = prog.tx0;
      dxActive = (uint32_t)(dx - (x % dx));
    }
    y = prog.ty0;
    dyActive = (uint32_t)(dy - (y % dy));
  }

  return false;
}

bool PacketIter::skipPackets(SparseBuffer* compressedPackets, uint64_t numPackets)
{
  auto tp = packetManager->getTileProcessor();
  auto plMarkers = tp->getPacketLengthCache()->getMarkers();
  auto skippedBytes = plMarkers->pop(numPackets);
  if(compressedPackets->skip(skippedBytes) != skippedBytes)
  {
    grklog.error("Packet iterator: unable to skip precincts.");
    return false;
  }
  tp->incNumProcessedPackets(numPackets);

  return true;
}
bool PacketIter::next_lrcpOPT(SparseBuffer* compressedPackets)
{
  bool skipOutsideWindow = compressedPackets && !isWholeTile();
  for(; layno < prog.lay_e; layno++)
  {
    for(; resno < prog.res_e; resno++)
    {
      auto precInfo = precinctInfoOPT_ + resno;
      if(!precInfoCheck(precInfo))
        continue;

      auto prec_e = precInfo->numPrecincts_;
      // resolutions dropped by reduce are interleaved with the kept ones
      if(compressedPackets && resno >= maxNumDecompositionResolutions)
      {
        if(!skipPackets(compressedPackets, prec_e * (uint64_t)(prog.comp_e - prog.comp_s)))
          return false;
        continue;
      }
      for(; compno < prog.comp_e; compno++)
      {
        if(incrementInner)
          precinctIndex++;
        if(skipOutsideWindow)
        {
          auto skipped = precInfo->precinctsToWindow(precinctIndex);
          if(skipped && !skipPackets(compressedPackets, skipped))
            return false;
          precinctIndex += skipped;
        }
        if(precinctIndex < prec_e)
        {
          incrementInner = true;
          return true;
        }
        precinctIndex = prog.prec_s;
        incrementInner = false;
      }
      compno = prog.comp_s;
    }
    resno = prog.res_s;
  }

  return false;
}
bool PacketIter::next_rlcpOPT(SparseBuffer* compressedPackets)
{
  bool skipOutsideWindow = compressedPackets && !isWholeTile();
  for(; resno < prog.res_e; resno++)
  {
    auto precInfo = precinctInfoOPT_ + resno;
    if(!precInfoCheck(precInfo))
      continue;

    auto prec_e = precInfo->numPrecincts_;
    for(; layno < prog.lay_e; layno++)
    {
      for(; compno < prog.comp_e; compno++)
      {
        if(incrementInner)
          precinctIndex++;
        if(skipOutsideWindow)
        {
          auto skipped = precInfo->precinctsToWindow(precinctIndex);
          if(skipped && !skipPackets(compressedPackets, skipped))
            return false;
          precinctIndex += skipped;
        }
        if(precinctIndex < prec_e)
        {
          incrementInner = true;
          return true;
        }
        precinctIndex = prog.prec_s;
        incrementInner = false;
      }
      compno = prog.comp_s;
    }
    layno = prog.lay_s;
  }

  return false;
}

uint64_t PacketIter::packetsInRowsOPT(uint64_t yBegin, uint64_t yEnd) const
{
  uint64_t precincts = 0;
  for(uint8_t res = prog.res_s; res < prog.res_e; ++res)
  {
    auto info = precinctInfoOPT_ + res;
    if(!info->valid)
      continue;
    uint64_t rows = info->tileBoundsPrecGrid.height();
    uint64_t firstStart = info->resOffsetY0PRJ ? spatialStartY_ : info->tileBoundsPrecPRJ.y0;
    auto rowsBefore = [&](uint64_t yLimit) -> uint64_t {
      if(yLimit <= firstStart)
        return 0;
      uint64_t cells = ceildiv<uint64_t>(yLimit, info->precHeightPRJ) - info->tileBoundsPrecGrid.y0;
      return std::min<uint64_t>(std::max<uint64_t>(cells, 1), rows);
    };
    precincts += (rowsBefore(yEnd) - rowsBefore(yBegin)) * info->tileBoundsPrecGrid.width();
  }
  return precincts * (uint64_t)(prog.lay_e - prog.lay_s);
}
uint64_t PacketIter::packetsInColumnsOPT(uint64_t y, uint64_t xBegin, uint64_t xEnd) const
{
  uint64_t precincts = 0;
  for(uint8_t res = prog.res_s; res < prog.res_e; ++res)
  {
    auto info = precinctInfoOPT_ + res;
    if(!info->valid || !precinctRowStartsOPT(info, y))
      continue;
    uint64_t columns = info->tileBoundsPrecGrid.width();
    uint64_t firstStart = info->resOffsetX0PRJ ? spatialStartX_ : info->tileBoundsPrecPRJ.x0;
    auto columnsBefore = [&](uint64_t xLimit) -> uint64_t {
      if(xLimit <= firstStart)
        return 0;
      uint64_t cells = ceildiv<uint64_t>(xLimit, info->precWidthPRJ) - info->tileBoundsPrecGrid.x0;
      return std::min<uint64_t>(std::max<uint64_t>(cells, 1), columns);
    };
    precincts += columnsBefore(xEnd) - columnsBefore(xBegin);
  }
  return precincts * (uint64_t)(prog.lay_e - prog.lay_s);
}
std::pair<uint64_t, uint64_t> PacketIter::windowRowsOPT(void) const
{
  uint64_t y0 = std::numeric_limits<uint64_t>::max();
  uint64_t y1 = 0;
  for(uint8_t res = prog.res_s; res < prog.res_e; ++res)
  {
    auto info = precinctInfoOPT_ + res;
    if(!info->valid)
      continue;
    y0 = std::min(y0, info->winPrecPRJ.y0);
    y1 = std::max(y1, info->winPrecPRJ.y1);
  }
  return {y0, y1};
}
std::pair<uint64_t, uint64_t> PacketIter::windowColumnsOPT(uint64_t y) const
{
  uint64_t x0 = std::numeric_limits<uint64_t>::max();
  uint64_t x1 = 0;
  for(uint8_t res = prog.res_s; res < prog.res_e; ++res)
  {
    auto info = precinctInfoOPT_ + res;
    if(!info->valid || !precinctRowStartsOPT(info, y))
      continue;
    x0 = std::min(x0, info->winPrecPRJ.x0);
    x1 = std::max(x1, info->winPrecPRJ.x1);
  }
  return {x0, x1};
}
bool PacketIter::next_cprlOPT(SparseBuffer* compressedPackets)
{
  auto wholeTile = isWholeTile();
  auto precInfo = precinctInfoOPT_ + prog.res_e - 1;
  if(!precInfo->valid)
    return false;
  bool skipOutsideWindow = !wholeTile && compressedPackets;
  auto [windowY0, windowY1] = wholeTile ? std::pair<uint64_t, uint64_t>{0, 0} : windowRowsOPT();
  for(; compno < prog.comp_e; compno++)
  {
    for(; y < precInfo->tileBoundsPrecPRJ.y1; y += dy)
    {
      if(!wholeTile && y >= windowY1)
      {
        if(compno == prog.comp_e - 1)
          return false;
        // without plt the rows below the window still have to be parsed
        if(compressedPackets)
        {
          if(!skipPackets(compressedPackets, packetsInRowsOPT(y, precInfo->tileBoundsPrecPRJ.y1)))
            return false;
          break;
        }
      }
      if(skipOutsideWindow && y < windowY0)
      {
        if(!skipPackets(compressedPackets, packetsInRowsOPT(y, windowY0)))
          return false;
        y = windowY0;
      }
      for(; x < precInfo->tileBoundsPrecPRJ.x1; x += dx)
      {
        if(skipOutsideWindow)
        {
          auto [windowX0, windowX1] = windowColumnsOPT(y);
          if(x < windowX0)
          {
            if(!skipPackets(compressedPackets, packetsInColumnsOPT(y, x, windowX0)))
              return false;
            x = windowX0;
          }
          if(x >= windowX1)
          {
            if(!skipPackets(compressedPackets,
                            packetsInColumnsOPT(y, x, precInfo->tileBoundsPrecPRJ.x1)))
              return false;
            break;
          }
        }
        for(; resno < prog.res_e; resno++)
        {
          auto comp = comps + compno;
          auto res = comp->resolutions + resno;
          auto rpInfo = precinctInfoOPT_ + resno;
          if(!rpInfo->valid)
            continue;
          if(!genPrecinctY0GridPCRL_OPT(rpInfo))
            continue;
          if(!genPrecinctX0GridPCRL_OPT(rpInfo))
            continue;
          if(compressedPackets && resno >= maxNumDecompositionResolutions)
          {
            if(!skipPackets(compressedPackets, (uint64_t)(prog.lay_e - prog.lay_s)))
              return false;
            continue;
          }
          precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            return true;
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        resno = prog.res_s;
      }
      x = (uint32_t)spatialStartX_;
    }
    y = (uint32_t)spatialStartY_;
  }

  return false;
}
bool PacketIter::next_pcrlOPT(SparseBuffer* compressedPackets)
{
  auto wholeTile = isWholeTile();
  auto precInfo = precinctInfoOPT_ + prog.res_e - 1;
  if(!precInfo->valid)
    return false;
  bool skipOutsideWindow = !wholeTile && compressedPackets;
  uint64_t components = prog.comp_e - prog.comp_s;
  auto [windowY0, windowY1] = wholeTile ? std::pair<uint64_t, uint64_t>{0, 0} : windowRowsOPT();
  for(; y < precInfo->tileBoundsPrecPRJ.y1; y += dy)
  {
    // position is the outer loop, so nothing after the window's last row is needed
    if(!wholeTile && y >= windowY1)
      return false;
    if(skipOutsideWindow && y < windowY0)
    {
      if(!skipPackets(compressedPackets, packetsInRowsOPT(y, windowY0) * components))
        return false;
      y = windowY0;
    }
    for(; x < precInfo->tileBoundsPrecPRJ.x1; x += dx)
    {
      if(skipOutsideWindow)
      {
        auto [windowX0, windowX1] = windowColumnsOPT(y);
        if(x < windowX0)
        {
          if(!skipPackets(compressedPackets, packetsInColumnsOPT(y, x, windowX0) * components))
            return false;
          x = windowX0;
        }
        if(x >= windowX1)
        {
          if(!skipPackets(compressedPackets,
                          packetsInColumnsOPT(y, x, precInfo->tileBoundsPrecPRJ.x1) * components))
            return false;
          break;
        }
      }
      for(; compno < prog.comp_e; compno++)
      {
        for(; resno < prog.res_e; resno++)
        {
          auto comp = comps + compno;
          auto res = comp->resolutions + resno;
          auto rpInfo = precinctInfoOPT_ + resno;
          if(!rpInfo->valid)
            continue;
          if(!genPrecinctY0GridPCRL_OPT(rpInfo))
            continue;
          if(!genPrecinctX0GridPCRL_OPT(rpInfo))
            continue;
          if(compressedPackets && resno >= maxNumDecompositionResolutions)
          {
            if(!skipPackets(compressedPackets, (uint64_t)(prog.lay_e - prog.lay_s)))
              return false;
            continue;
          }
          precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            return true;
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        resno = prog.res_s;
      }
      compno = prog.comp_s;
    }
    x = (uint32_t)spatialStartX_;
  }

  return false;
}
bool PacketIter::next_rpclOPT(SparseBuffer* compressedPackets)
{
  auto wholeTile = isWholeTile();
  for(; resno < prog.res_e; resno++)
  {
    auto precInfo = precinctInfoOPT_ + resno;
    if(!precInfoCheck(precInfo))
      continue;
    auto win = &precInfo->winPrecPRJ;
    for(; y < precInfo->tileBoundsPrecPRJ.y1; y += precInfo->precHeightPRJ)
    {
      // skip over packets outside of window
      if(!wholeTile)
      {
        // windowed decode:
        // bail out if we reach row of precincts that are out of bound of the window
        if(resno == maxNumDecompositionResolutions - 1 && y == win->y1)
          return false;

        if(compressedPackets)
        {
          // skip all precincts above window
          if(y < win->y0)
          {
            if(!skipPackets(compressedPackets, precInfo->winPrecinctsTop_))
              return false;
            y = win->y0;
          }
          // skip all precincts below window
          else if(y == win->y1 && precInfo->winPrecinctsBottom_)
          {
            if(!skipPackets(compressedPackets, precInfo->winPrecinctsBottom_))
              return false;
            break;
          }
          // skip precincts to the left of window
          if(!skippedLeft_ && precInfo->winPrecinctsLeft_)
          {
            if(x < win->x0)
            {
              if(!skipPackets(compressedPackets, precInfo->winPrecinctsLeft_))
                return false;
              x = win->x0;
            }
            skippedLeft_ = true;
          }
        }
      }
      genPrecinctY0GridRPCL_OPT(precInfo);
      auto precIndexY = (uint64_t)py0grid_ * precInfo->tileBoundsPrecGrid.width();
      auto xMax = wholeTile || !compressedPackets ? precInfo->tileBoundsPrecPRJ.x1 : win->x1;
      for(; x < xMax; x += precInfo->precWidthPRJ)
      {
        // windowed decode:
        // bail out if we reach a precinct which is past the
        // bottom, right hand corner of the tile window
        if(!wholeTile && resno == maxNumDecompositionResolutions - 1)
        {
          if((win->y1 == 0 || (win->y1 > 0 && y == win->y1 - 1)) && x >= win->x1)
            return false;
        }
        genPrecinctX0GridRPCL_OPT(precInfo);
        for(; compno < prog.comp_e; compno++)
        {
          if(incrementInner)
            layno++;
          if(layno < prog.lay_e)
          {
            incrementInner = true;
            precinctIndex = px0grid_ + precIndexY;
            return true;
          }
          layno = prog.lay_s;
          incrementInner = false;
        }
        compno = prog.comp_s;
      }
      x = precInfo->tileBoundsPrecPRJ.x0;
      // skip precincts to the right of window
      if(!wholeTile && compressedPackets && precInfo->winPrecinctsRight_)
      {
        if(!skipPackets(compressedPackets, precInfo->winPrecinctsRight_))
          return false;
      }
      skippedLeft_ = false;
    }
    // Reset spatial position to tile origin for the next resolution.
    // Each resolution's tileBoundsPrecPRJ can differ, so using the current
    // resolution's x0/y0 may place x/y outside the next resolution's range,
    // causing precinct-grid underflow.  The tile origin is guaranteed to fall
    // within every resolution's spatial range.
    y = prog.ty0;
    x = prog.tx0;
  }

  return false;
}

} // namespace grk
