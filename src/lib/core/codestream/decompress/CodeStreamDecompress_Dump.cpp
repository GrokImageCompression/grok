/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

void CodeStreamDecompress::dumpTileHeader(TileCodingParams* defaultTile, uint32_t numcomps,
                                          FILE* out)
{
  if(!defaultTile)
    return;

  fprintf(out, "\t default tile {\n");
  fprintf(out, "\t\t csty=%#x\n", defaultTile->csty_);
  fprintf(out, "\t\t prg=%#x\n", defaultTile->prg_);
  fprintf(out, "\t\t numlayers=%u\n", defaultTile->numLayers_);
  fprintf(out, "\t\t mct=%x\n", defaultTile->mct_);

  for(uint16_t compno = 0; compno < numcomps; compno++)
  {
    auto tccp = &(defaultTile->tccps_[compno]);
    uint8_t resno;
    uint8_t bandIndex, numBandWindows;

    assert(tccp->numresolutions_ > 0);

    /* coding style*/
    fprintf(out, "\t\t comp %u {\n", compno);
    fprintf(out, "\t\t\t csty=%#x\n", tccp->csty_);
    fprintf(out, "\t\t\t numresolutions=%u\n", tccp->numresolutions_);
    fprintf(out, "\t\t\t cblkw=2^%u\n", tccp->cblkw_expn_);
    fprintf(out, "\t\t\t cblkh=2^%u\n", tccp->cblkh_expn_);
    fprintf(out, "\t\t\t cblksty=%#x\n", tccp->cblkStyle_);
    fprintf(out, "\t\t\t qmfbid=%u\n", tccp->qmfbid_);

    fprintf(out, "\t\t\t preccintsize (w,h)=");
    for(resno = 0; resno < tccp->numresolutions_; resno++)
    {
      fprintf(out, "(%u,%u) ", tccp->precWidthExp_[resno], tccp->precHeightExp_[resno]);
    }
    fprintf(out, "\n");

    /* quantization style*/
    fprintf(out, "\t\t\t qntsty=%u\n", tccp->qntsty_);
    fprintf(out, "\t\t\t numgbits=%u\n", tccp->numgbits_);
    fprintf(out, "\t\t\t stepsizes (m,e)=");
    numBandWindows =
        (tccp->qntsty_ == CCP_QNTSTY_SIQNT) ? 1 : (uint8_t)(tccp->numresolutions_ * 3 - 2);
    for(bandIndex = 0; bandIndex < numBandWindows; bandIndex++)
    {
      fprintf(out, "(%u,%u) ", tccp->stepsizes_[bandIndex].mant, tccp->stepsizes_[bandIndex].expn);
    }
    fprintf(out, "\n");

    /* RGN value*/
    fprintf(out, "\t\t\t roishift=%u\n", tccp->roishift_);

    fprintf(out, "\t\t }\n");
  } /*end of component of default tile*/
  fprintf(out, "\t }\n"); /*end of default tile*/
}

void CodeStreamDecompress::dump(uint32_t flag, FILE* out)
{
  /* Dump the image_header */
  if(flag & GRK_IMG_INFO)
  {
    if(headerImage_)
      dumpImageHeader(getHeaderImage(), 0, out);
  }

  /* Dump the code stream info from main header */
  if(flag & GRK_MH_INFO)
  {
    if(headerImage_)
      dumpMainHeader(out);
  }
  /* Dump all tile/code stream info */
  if(flag & GRK_TCH_INFO)
  {
    if(headerImage_)
    {
      for(uint16_t i = 0; i < cp_.t_grid_height_ * cp_.t_grid_width_; ++i)
      {
        auto tp = this->tileCache_->get(i);
        if(tp && tp->processor)
          dumpTileHeader(tp->processor->getTCP(), getHeaderImage()->numcomps, out);
      }
    }
  }

  /* Dump the code stream index from main header */
  if((flag & GRK_MH_IND) && markerCache_)
    markerCache_->dump(out);
}
void CodeStreamDecompress::dumpMainHeader(FILE* out)
{
  fprintf(out, "Codestream info from main header: {\n");
  fprintf(out, "\t tx0=%u, ty0=%u\n", cp_.tx0_, cp_.ty0_);
  fprintf(out, "\t tdx=%u, tdy=%u\n", cp_.t_width_, cp_.t_height_);
  fprintf(out, "\t tw=%u, th=%u\n", cp_.t_grid_width_, cp_.t_grid_height_);
  CodeStreamDecompress::dumpTileHeader(defaultTcp_.get(), getHeaderImage()->numcomps, out);
  fprintf(out, "}\n");
}
void CodeStreamDecompress::dumpImageHeader(GrkImage* imgHeader, bool dev_dump_flag, FILE* out)
{
  char tab[2];
  if(dev_dump_flag)
  {
    fprintf(stdout, "[DEV] Dump an image_header struct {\n");
    tab[0] = '\0';
  }
  else
  {
    fprintf(out, "Image info {\n");
    tab[0] = '\t';
    tab[1] = '\0';
  }
  fprintf(out, "%s x0=%u, y0=%u\n", tab, imgHeader->x0, imgHeader->y0);
  fprintf(out, "%s x1=%u, y1=%u\n", tab, imgHeader->x1, imgHeader->y1);
  fprintf(out, "%s numcomps=%u\n", tab, imgHeader->numcomps);
  if(imgHeader->comps)
  {
    for(uint16_t compno = 0; compno < imgHeader->numcomps; compno++)
    {
      fprintf(out, "%s\t component %u {\n", tab, compno);
      CodeStreamDecompress::dumpImageComponentHeader(&(imgHeader->comps[compno]), dev_dump_flag,
                                                     out);
      fprintf(out, "%s}\n", tab);
    }
  }
  fprintf(out, "}\n");
}
void CodeStreamDecompress::dumpImageComponentHeader(grk_image_comp* compHeader, bool dev_dump_flag,
                                                    FILE* out)
{
  char tab[3];
  if(dev_dump_flag)
  {
    fprintf(stdout, "[DEV] Dump an image_comp_header struct {\n");
    tab[0] = '\0';
  }
  else
  {
    tab[0] = '\t';
    tab[1] = '\t';
    tab[2] = '\0';
  }

  fprintf(out, "%s dx=%u, dy=%u\n", tab, compHeader->dx, compHeader->dy);
  fprintf(out, "%s prec=%u\n", tab, compHeader->prec);
  fprintf(out, "%s sgnd=%u\n", tab, compHeader->sgnd ? 1U : 0U);

  if(dev_dump_flag)
    fprintf(out, "}\n");
}

} // namespace grk
