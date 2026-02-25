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
#include "GrkMatrix.h"
#include "grk_includes.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "StreamIO.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "Profile.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodeStream.h"
#include "PacketIter.h"
#include "PacketLengthCache.h"
#include "TLMMarker.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "CodeblockCompress.h"
#include "WindowScheduler.h"
#include "PacketManager.h"
#include "mct.h"
#include "ITileProcessor.h"
#include "TileProcessorCompress.h"
#include "SOTMarker.h"
#include "CodeStreamCompress.h"
#include "TileProcessorCompress.h"

namespace grk
{

namespace
{

  void write_float_to_int16(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<float, int16_t>(p_src_data, p_dest_data, nb_elem);
  }
  void write_float_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
  }
  void write_float_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<float, float>(p_src_data, p_dest_data, nb_elem);
  }
  void write_float_to_float64(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
  {
    write<float, double>(p_src_data, p_dest_data, nb_elem);
  }

} // namespace

static const mct_function mct_write_functions_from_float[] = {
    write_float_to_int16, write_float_to_int32, write_float_to_float, write_float_to_float64};

struct prog_order
{
  GRK_PROG_ORDER enum_prog;
  char str_prog[5];
};

static prog_order prog_order_list[] = {{GRK_CPRL, "CPRL"}, {GRK_LRCP, "LRCP"},
                                       {GRK_PCRL, "PCRL"}, {GRK_RLCP, "RLCP"},
                                       {GRK_RPCL, "RPCL"}, {(GRK_PROG_ORDER)-1, ""}};

CodeStreamCompress::CodeStreamCompress(IStream* stream) : CodeStream(stream), totalTileParts_(0) {}

char* CodeStreamCompress::convertProgressionOrder(GRK_PROG_ORDER prg_order)
{
  prog_order* po;
  for(po = prog_order_list; po->enum_prog != -1; po++)
  {
    if(po->enum_prog == prg_order)
      return (char*)po->str_prog;
  }
  return po->str_prog;
}
bool CodeStreamCompress::mct_validation(void)
{
  bool valid = true;
  if((cp_.rsiz_ & 0x8200) == 0x8200)
  {
    uint32_t numTiles = cp_.t_grid_height_ * cp_.t_grid_width_;
    for(uint16_t i = 0; i < numTiles; ++i)
    {
      auto tcp = cp_.tcps_.get(i);
      if(tcp->mct_ == 2)
      {
        valid &= (tcp->mctCodingMatrix_ != nullptr);
        for(uint16_t j = 0; j < headerImage_->numcomps; ++j)
        {
          auto tccp = tcp->tccps_ + j;
          valid &= !(tccp->qmfbid_ & 1);
        }
      }
    }
  }

  return valid;
}

bool CodeStreamCompress::start(void)
{
  /* customization of the validation */
  validationList_.push_back(std::bind(&CodeStreamCompress::compressValidation, this));
  // custom validation here
  validationList_.push_back(std::bind(&CodeStreamCompress::mct_validation, this));

  /* validation of the parameters codec */
  if(!exec(validationList_))
    return false;

  /* customization of the compressing */
  if(!init_header_writing())
    return false;

  /* write header */
  return exec(procedureList_);
}
bool CodeStreamCompress::init(grk_cparameters* parameters, GrkImage* image)
{
  if(!parameters || !image)
    return false;

  bool isHT = (parameters->cblk_sty & 0X7F) == GRK_CBLKSTY_HT_ONLY;

  // sanity check on image
  if(image->numcomps < 1 || image->numcomps > maxNumComponentsJ2K)
  {
    grklog.error("Invalid number of components specified while setting up JP2 compressor");
    return false;
  }
  if((image->x1 < image->x0) || (image->y1 < image->y0))
  {
    grklog.error("Invalid input image dimensions found while setting up JP2 compressor");
    return false;
  }
  for(uint16_t i = 0; i < image->numcomps; ++i)
  {
    auto comp = image->comps + i;
#ifdef GRK_FORCE_SIGNED_COMPRESS
    comp->sgnd = true;
#endif
    if(comp->w == 0 || comp->h == 0)
    {
      grklog.error(
          "Invalid input image component dimensions found while setting up JP2 compressor");
      return false;
    }
    if(comp->prec == 0)
    {
      grklog.error("Invalid component precision of 0 found while setting up JP2 compressor");
      return false;
    }
  }
  if(parameters->apply_icc)
    image->applyICC<int32_t>();

  // create private sanitized copy of image
  headerImage_ = new GrkImage();
  image->copyHeaderTo(headerImage_);
  if(image->comps)
  {
    for(uint16_t compno = 0; compno < image->numcomps; compno++)
    {
      if(image->comps[compno].data)
      {
        headerImage_->comps[compno].data = image->comps[compno].data;
        headerImage_->comps[compno].owns_data = false;
        headerImage_->comps[compno].stride = image->comps[compno].stride;
      }
    }
  }

  if(isHT)
  {
    if(parameters->numlayers > 1 || parameters->layer_rate[0] != 0)
    {
      grklog.warn("Rate control not supported for HTJ2K compression.");
      parameters->numlayers = 1;
      parameters->layer_rate[0] = 0;
    }
    parameters->allocation_by_rate_distortion = true;
  }

  if((parameters->numresolution == 0) || (parameters->numresolution > GRK_MAXRLVLS))
  {
    grklog.error("Invalid number of resolutions : %u not in range [1,%u]",
                 parameters->numresolution, GRK_MAXRLVLS);
    return false;
  }

  if(GRK_IS_IMF(parameters->rsiz) && parameters->max_cs_size > 0 && parameters->numlayers == 1 &&
     parameters->layer_rate[0] == 0)
  {
    parameters->layer_rate[0] =
        (float)(image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec) /
        (float)(((uint32_t)parameters->max_cs_size) * 8 * image->comps[0].dx * image->comps[0].dy);
  }

  /* if no rate entered, lossless by default */
  if(parameters->numlayers == 0)
  {
    parameters->layer_rate[0] = 0;
    parameters->numlayers = 1;
    parameters->allocation_by_rate_distortion = true;
  }

  /* see if max_codestream_size does limit input rate */
  double image_bytes =
      ((double)image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec) /
      (8 * image->comps[0].dx * image->comps[0].dy);
  if(parameters->max_cs_size == 0)
  {
    if(parameters->numlayers > 0 && parameters->layer_rate[parameters->numlayers - 1] > 0)
    {
      parameters->max_cs_size =
          (uint64_t)floor(image_bytes / parameters->layer_rate[parameters->numlayers - 1]);
    }
  }
  else
  {
    bool cap = false;
    auto min_rate = image_bytes / (double)parameters->max_cs_size;
    for(uint32_t i = 0; i < parameters->numlayers; i++)
    {
      if(parameters->layer_rate[i] < min_rate)
      {
        parameters->layer_rate[i] = min_rate;
        cap = true;
      }
    }
    if(cap)
    {
      grklog.warn("The desired maximum code stream size has limited");
      grklog.warn("at least one of the desired quality layers");
    }
  }

  /* Manage profiles and applications and set RSIZ */
  /* set cinema parameters if required */
  if(isHT)
    parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
  if(GRK_IS_CINEMA(parameters->rsiz))
  {
    if((parameters->rsiz == GRK_PROFILE_CINEMA_S2K) || (parameters->rsiz == GRK_PROFILE_CINEMA_S4K))
    {
      grklog.warn("JPEG 2000 Scalable Digital Cinema profiles not supported");
      parameters->rsiz = GRK_PROFILE_NONE;
    }
    else
    {
      if(Profile::isCinemaCompliant(image, parameters->rsiz))
        Profile::setCinemaParams(parameters, image);
      else
        parameters->rsiz = GRK_PROFILE_NONE;
    }
  }
  else if(GRK_IS_STORAGE(parameters->rsiz))
  {
    grklog.warn("JPEG 2000 Long Term Storage profile not supported");
    parameters->rsiz = GRK_PROFILE_NONE;
  }
  else if(GRK_IS_BROADCAST(parameters->rsiz))
  {
    Profile::setBroadcastParams(parameters);
    if(!Profile::isBroadcastCompliant(parameters, image))
      parameters->rsiz = GRK_PROFILE_NONE;
  }
  else if(GRK_IS_IMF(parameters->rsiz))
  {
    Profile::setImfParams(parameters, image);
    if(!Profile::isImfCompliant(parameters, image))
      parameters->rsiz = GRK_PROFILE_NONE;
  }
  else if(GRK_IS_PART2(parameters->rsiz))
  {
    if(parameters->rsiz == ((GRK_PROFILE_PART2) | (GRK_EXTENSION_NONE)))
    {
      grklog.warn("JPEG 2000 Part-2 profile defined\n"
                  "but no Part-2 extension enabled.\n"
                  "Profile set to NONE.");
      parameters->rsiz = GRK_PROFILE_NONE;
    }
    else if(parameters->rsiz != ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT)))
    {
      grklog.warn("Unsupported Part-2 extension enabled\n"
                  "Profile set to NONE.");
      parameters->rsiz = GRK_PROFILE_NONE;
    }
  }

  if(parameters->numpocs)
  {
    if(!validateProgressionOrders(parameters->progression, parameters->numpocs + 1,
                                  parameters->numresolution, image->numcomps,
                                  parameters->numlayers))
    {
      grklog.error("Failed to initialize POC");
      return false;
    }
  }
  /* set default values for cp_ */
  cp_.t_grid_width_ = 1;
  cp_.t_grid_height_ = 1;

  cp_.codingParams_.enc_.maxComponentRate_ = parameters->max_comp_size;
  cp_.rsiz_ = parameters->rsiz;
  cp_.codingParams_.enc_.allocationByRateDistortion_ = parameters->allocation_by_rate_distortion;
  cp_.codingParams_.enc_.allocationByFixedQuality_ = parameters->allocation_by_quality;
  cp_.codingParams_.enc_.writePlt_ = parameters->write_plt;
  cp_.codingParams_.enc_.writeTlm_ = parameters->write_tlm;
  cp_.codingParams_.enc_.rateControlAlgorithm_ = parameters->rate_control_algorithm;

  /* tiles */
  cp_.t_width_ = parameters->t_width;
  cp_.t_height_ = parameters->t_height;

  /* tile offset */
  cp_.tx0_ = parameters->tx0;
  cp_.ty0_ = parameters->ty0;

  /* comment string */
  if(parameters->num_comments)
  {
    for(size_t i = 0; i < parameters->num_comments; ++i)
    {
      cp_.commentLength_[i] = parameters->comment_len[i];
      if(!cp_.commentLength_[i])
      {
        grklog.warn("Empty comment. Ignoring");
        continue;
      }
      if(cp_.commentLength_[i] > GRK_MAX_COMMENT_LENGTH)
      {
        grklog.warn("Comment length %s is greater than maximum comment length %u. Ignoring",
                    cp_.commentLength_[i], GRK_MAX_COMMENT_LENGTH);
        continue;
      }
      cp_.isBinaryComment_[i] = parameters->is_binary_comment[i];
      cp_.comment_[i] = new char[cp_.commentLength_[i]];
      memcpy(cp_.comment_[i], parameters->comment[i], cp_.commentLength_[i]);
      cp_.numComments_++;
    }
  }
  else
  {
    /* Create default comment for code stream */
    auto comment = std::string("Created by Grok version ") + grk_version();
    auto comment_len = comment.size();
    cp_.comment_[0] = new char[comment_len];
    memcpy(cp_.comment_[0], comment.c_str(), comment_len);
    cp_.commentLength_[0] = (uint16_t)comment_len;
    cp_.numComments_ = 1;
    cp_.isBinaryComment_[0] = false;
  }
  if(parameters->tile_size_on)
  {
    // avoid divide by zero
    if(cp_.t_width_ == 0 || cp_.t_height_ == 0)
    {
      grklog.error("Invalid tile dimensions (%u,%u)", cp_.t_width_, cp_.t_height_);
      return false;
    }
    uint32_t tgw = ceildiv<uint32_t>((image->x1 - cp_.tx0_), cp_.t_width_);
    uint32_t tgh = ceildiv<uint32_t>((image->y1 - cp_.ty0_), cp_.t_height_);
    uint64_t numTiles = (uint64_t)tgw * tgh;
    if(numTiles > maxNumTilesJ2K)
    {
      grklog.error("Number of tiles %u is greater than max tiles %u"
                   "allowed by the standard.",
                   numTiles, maxNumTilesJ2K);
      return false;
    }
    cp_.t_grid_width_ = (uint16_t)tgw;
    cp_.t_grid_height_ = (uint16_t)tgh;
  }
  else
  {
    cp_.t_width_ = image->x1 - cp_.tx0_;
    cp_.t_height_ = image->y1 - cp_.ty0_;
  }
  if(parameters->enable_tile_part_generation)
  {
    cp_.codingParams_.enc_.newTilePartProgressionDivider_ =
        parameters->new_tile_part_progression_divider;
    cp_.codingParams_.enc_.enableTilePartGeneration_ = true;
  }
  uint8_t numgbits = parameters->numgbits;
  if(parameters->numgbits > 7)
  {
    grklog.error("Number of guard bits %u is greater than 7", numgbits);
    return false;
  }
  for(uint16_t tileno = 0; tileno < (uint16_t)(cp_.t_grid_width_ * cp_.t_grid_height_); tileno++)
  {
    auto tcp = cp_.tcps_.get(tileno);
    tcp->tccps_ = new TileComponentCodingParams[image->numcomps];

    tcp->setIsHT(isHT, !parameters->irreversible, numgbits);
    tcp->qcd_->generate((uint8_t)(parameters->numresolution - 1), image->comps[0].prec,
                        parameters->mct > 0, image->comps[0].sgnd);
    for(uint16_t i = 0; i < image->numcomps; i++)
      tcp->qcd_->pull((tcp->tccps_ + i)->stepsizes_);

    tcp->numLayers_ = parameters->numlayers;
    for(uint16_t j = 0; j < tcp->numLayers_; j++)
    {
      if(cp_.codingParams_.enc_.allocationByFixedQuality_)
        tcp->distortion_[j] = parameters->layer_distortion[j];
      else
        tcp->rates_[j] = parameters->layer_rate[j];
    }
    tcp->csty_ = parameters->csty;
    tcp->prg_ = parameters->prog_order;
    tcp->mct_ = parameters->mct;
    if(parameters->numpocs)
    {
      uint32_t numTileProgressions = 0;
      for(uint32_t i = 0; i < parameters->numpocs + 1; i++)
      {
        if(tileno == parameters->progression[i].tileno)
        {
          auto tcp_poc = tcp->progressionOrderChange_ + numTileProgressions;

          tcp_poc->res_s = parameters->progression[numTileProgressions].res_s;
          tcp_poc->comp_s = parameters->progression[numTileProgressions].comp_s;
          tcp_poc->lay_e = parameters->progression[numTileProgressions].lay_e;
          tcp_poc->res_e = parameters->progression[numTileProgressions].res_e;
          tcp_poc->comp_e = parameters->progression[numTileProgressions].comp_e;
          tcp_poc->specified_compression_poc_prog =
              parameters->progression[numTileProgressions].specified_compression_poc_prog;
          tcp_poc->tileno = parameters->progression[numTileProgressions].tileno;
          numTileProgressions++;
        }
      }
      if(numTileProgressions == 0)
      {
        grklog.error("Problem with specified progression order changes");
        return false;
      }
      tcp->numpocs_ = numTileProgressions - 1;
    }
    else
    {
      tcp->numpocs_ = 0;
    }
    if(parameters->mct_data)
    {
      uint64_t lMctSize = (uint64_t)image->numcomps * image->numcomps * sizeof(float);
      auto lTmpBuf = (float*)grk_malloc(lMctSize);
      auto dc_shift = (int32_t*)((uint8_t*)parameters->mct_data + lMctSize);
      if(!lTmpBuf)
      {
        grklog.error("Not enough memory to allocate temp buffer");
        return false;
      }
      tcp->mct_ = 2;
      tcp->mctCodingMatrix_ = (float*)grk_malloc(lMctSize);
      if(!tcp->mctCodingMatrix_)
      {
        grk_free(lTmpBuf);
        lTmpBuf = nullptr;
        grklog.error("Not enough memory to allocate compressor MCT coding matrix ");
        return false;
      }
      memcpy(tcp->mctCodingMatrix_, parameters->mct_data, lMctSize);
      memcpy(lTmpBuf, parameters->mct_data, lMctSize);

      tcp->mctDecodingMatrix_ = (float*)grk_malloc(lMctSize);
      if(!tcp->mctDecodingMatrix_)
      {
        grk_free(lTmpBuf);
        lTmpBuf = nullptr;
        grklog.error("Not enough memory to allocate compressor MCT decoding matrix ");
        return false;
      }
      if(GrkMatrix().matrix_inversion_f(lTmpBuf, (tcp->mctDecodingMatrix_), image->numcomps) ==
         false)
      {
        grk_free(lTmpBuf);
        lTmpBuf = nullptr;
        grklog.error("Failed to inverse compressor MCT decoding matrix ");
        return false;
      }

      tcp->mct_norms_ = (double*)grk_malloc(image->numcomps * sizeof(double));
      if(!tcp->mct_norms_)
      {
        grk_free(lTmpBuf);
        lTmpBuf = nullptr;
        grklog.error("Not enough memory to allocate compressor MCT norms ");
        return false;
      }
      Mct::calculate_norms(tcp->mct_norms_, image->numcomps, tcp->mctDecodingMatrix_);
      grk_free(lTmpBuf);

      for(uint16_t i = 0; i < image->numcomps; i++)
      {
        auto tccp = &tcp->tccps_[i];
        tccp->dcLevelShift_ = dc_shift[i];
      }

      if(init_mct_encoding(tcp, image) == false)
      {
        grklog.error("Failed to set up j2k mct compressing");
        return false;
      }
    }
    else
    {
      if(tcp->mct_ == 1)
      {
        if(image->color_space == GRK_CLRSPC_EYCC || image->color_space == GRK_CLRSPC_SYCC)
        {
          grklog.warn("Disabling MCT for sYCC/eYCC colour space");
          tcp->mct_ = 0;
        }
        else if(image->numcomps >= 3)
        {
          if((image->comps[0].dx != image->comps[1].dx) ||
             (image->comps[0].dx != image->comps[2].dx) ||
             (image->comps[0].dy != image->comps[1].dy) ||
             (image->comps[0].dy != image->comps[2].dy))
          {
            grklog.warn("Cannot perform MCT on components with different dimensions. "
                        "Disabling MCT.");
            tcp->mct_ = 0;
          }
        }
      }
      for(uint16_t i = 0; i < image->numcomps; i++)
      {
        auto tccp = tcp->tccps_ + i;
        auto comp = image->comps + i;
        if(!comp->sgnd)
          tccp->dcLevelShift_ = 1 << (comp->prec - 1);
      }
    }

    for(uint16_t i = 0; i < image->numcomps; i++)
    {
      auto tccp = &tcp->tccps_[i];

      /* 0 => one precinct || 1 => custom precinct  */
      tccp->csty_ = parameters->csty & CP_CSTY_PRT;
      tccp->numresolutions_ = parameters->numresolution;
      tccp->cblkw_expn_ = floorlog2(parameters->cblockw_init);
      tccp->cblkh_expn_ = floorlog2(parameters->cblockh_init);
      tccp->cblkStyle_ = parameters->cblk_sty;
      tccp->qmfbid_ = parameters->irreversible ? 0 : 1;
      tccp->qntsty_ = parameters->irreversible ? CCP_QNTSTY_SEQNT : CCP_QNTSTY_NOQNT;
      tccp->numgbits_ = numgbits;
      if((int32_t)i == parameters->roi_compno)
        tccp->roishift_ = (uint8_t)parameters->roi_shift;
      else
        tccp->roishift_ = 0;
      if((parameters->csty & CCP_CSTY_PRECINCT) && parameters->res_spec)
      {
        uint32_t p = 0;
        int32_t it_res;
        assert(tccp->numresolutions_ > 0);
        for(it_res = (int32_t)tccp->numresolutions_ - 1; it_res >= 0; it_res--)
        {
          if(p < parameters->res_spec)
          {
            if(parameters->prcw_init[p] < 1)
              tccp->precWidthExp_[it_res] = 1;
            else
              tccp->precWidthExp_[it_res] = floorlog2(parameters->prcw_init[p]);
            if(parameters->prch_init[p] < 1)
              tccp->precHeightExp_[it_res] = 1;
            else
              tccp->precHeightExp_[it_res] = floorlog2(parameters->prch_init[p]);
          }
          else
          {
            uint32_t res_spec = parameters->res_spec;
            uint32_t size_prcw = 0;
            uint32_t size_prch = 0;
            size_prcw = parameters->prcw_init[res_spec - 1] >> (p - (res_spec - 1));
            size_prch = parameters->prch_init[res_spec - 1] >> (p - (res_spec - 1));
            if(size_prcw < 1)
              tccp->precWidthExp_[it_res] = 1;
            else
              tccp->precWidthExp_[it_res] = floorlog2(size_prcw);
            if(size_prch < 1)
              tccp->precHeightExp_[it_res] = 1;
            else
              tccp->precHeightExp_[it_res] = floorlog2(size_prch);
          }
          p++;
          /*printf("\nsize precinct for level %u : %u,%u\n",
           * it_res,tccp->precWidthExp[it_res], tccp->precHeightExp[it_res]); */
        } /*end for*/
      }
      else
      {
        for(uint32_t j = 0; j < tccp->numresolutions_; j++)
        {
          tccp->precWidthExp_[j] = 15;
          tccp->precHeightExp_[j] = 15;
        }
      }
    }
  }
  grk_free(parameters->mct_data);
  parameters->mct_data = nullptr;

  return true;
}

void CodeStreamCompress::handleTileProcessor(
    TileProcessorCompress* proc, MinHeapPtr<TileProcessorCompress, uint16_t, MinHeapLocker>& heap,
    std::atomic<bool>& success)
{
  std::unique_lock<std::mutex> lk(heapMutex_);
  auto seq = heap.pop(proc);
  for(const auto& s : seq)
  {
    // printf("Write tile %d\n", s->getIndex());
    if(success)
    {
      if(!writeTileParts(s))
        success = false;
    }
    delete s;
  }
}

uint64_t CodeStreamCompress::compress(grk_plugin_tile* tile)
{
  MinHeapPtr<TileProcessorCompress, uint16_t, MinHeapLocker> heap;
  uint32_t numTiles = (uint32_t)cp_.t_grid_height_ * cp_.t_grid_width_;
  if(numTiles > maxNumTilesJ2K)
  {
    grklog.error("Number of tiles %u is greater than max tiles %u"
                 "allowed by the standard.",
                 numTiles, maxNumTilesJ2K);
    return 0;
  }
  auto numRequiredThreads = std::min<uint32_t>((uint32_t)ExecSingleton::num_threads(), numTiles);
  std::atomic<bool> success(true);
  if(numRequiredThreads > 1)
  {
    tf::Executor exec(numRequiredThreads);
    tf::Taskflow taskflow;
    auto node = new tf::Task[numTiles];
    for(uint64_t i = 0; i < numTiles; i++)
      node[i] = taskflow.placeholder();
    for(uint16_t j = 0; j < numTiles; ++j)
    {
      uint16_t tile_index = j;
      node[j].work([this, &exec, tile_index, &heap, &success] {
        if(success)
        {
          auto tileProcessor =
              new TileProcessorCompress(tile_index, this->cp_.tcps_.get(tile_index), this, stream_);
          auto workerId = exec.this_worker_id();
          if(workerId < 0)
            grklog.error("Invalid worker id %d", workerId);
          if(!tileProcessor->preCompressTile((size_t)exec.this_worker_id()) ||
             !tileProcessor->doCompress())
            success = false;
          handleTileProcessor(tileProcessor, heap, success);
        }
      });
    }
    exec.run(taskflow).wait();
    delete[] node;
  }
  else
  {
    for(uint16_t i = 0; i < numTiles; ++i)
    {
      auto tileProcessor = new TileProcessorCompress(i, cp_.tcps_.get(i), this, stream_);
      tileProcessor->setCurrentPluginTile(tile);
      if(!tileProcessor->preCompressTile(0) || !tileProcessor->doCompress())
      {
        delete tileProcessor;
        success = false;
        goto cleanup;
      }
      bool write_success = writeTileParts(tileProcessor);
      delete tileProcessor;
      if(!write_success)
      {
        success = false;
        goto cleanup;
      }
    }
  }
cleanup:
  handleTileProcessor(nullptr, heap, success);
  if(success)
    success = end();

  return success ? stream_->tell() : 0;
}
bool CodeStreamCompress::end(void)
{
  /* customization of the compressing */
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_eoc, this));
  if(cp_.codingParams_.enc_.writeTlm_)
    procedureList_.push_back(std::bind(&CodeStreamCompress::write_tlm_end, this));

  return exec(procedureList_);
}
///////////////////////////////////////////////////////////////////////////////////////////
bool CodeStreamCompress::write_rgn(uint16_t tile_no, uint16_t comp_no, uint16_t nb_comps)
{
  uint32_t rgn_size;
  auto cp = &cp_;
  auto tcp = cp->tcps_.get(tile_no);
  auto tccp = &tcp->tccps_[comp_no];
  uint32_t comp_room = (nb_comps <= 256) ? 1 : 2;
  rgn_size = 6 + comp_room;

  /* RGN  */
  if(!stream_->write(RGN))
    return false;
  /* Lrgn */
  if(!stream_->write((uint16_t)(rgn_size - 2)))
    return false;
  /* Crgn */
  if(comp_room == 2)
  {
    if(!stream_->write((uint16_t)comp_no))
      return false;
  }
  else
  {
    if(!stream_->write8u((uint8_t)comp_no))
      return false;
  }
  /* Srgn */
  if(!stream_->write8u(0))
    return false;

  /* SPrgn */
  return stream_->write8u((uint8_t)tccp->roishift_);
}

bool CodeStreamCompress::write_eoc()
{
  if(!stream_->write(EOC))
    return false;

  return stream_->flush();
}
bool CodeStreamCompress::write_mct_record(grk_mct_data* p_mct_record, IStream* stream)
{
  uint32_t mct_size;
  uint32_t tmp;

  mct_size = 10 + p_mct_record->data_size_;

  /* MCT */
  if(!stream->write(MCT))
    return false;
  /* Lmct */
  if(!stream->write((uint16_t)(mct_size - 2)))
    return false;
  /* Zmct */
  if(!stream->write((uint16_t)0))
    return false;
  /* only one marker atm */
  tmp = (p_mct_record->index_ & 0xff) | (uint32_t)(p_mct_record->array_type_ << 8) |
        (uint32_t)(p_mct_record->element_type_ << 10);

  if(!stream->write((uint16_t)tmp))
    return false;

  /* Ymct */
  if(!stream->write((uint16_t)0))
    return false;

  return stream->writeBytes(p_mct_record->data_, p_mct_record->data_size_);
}
bool CodeStreamCompress::init_header_writing(void)
{
  procedureList_.push_back([this] { return getNumTileParts(&totalTileParts_, getHeaderImage()); });

  procedureList_.push_back(std::bind(&CodeStreamCompress::write_soc, this));
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_siz, this));
  if(cp_.tcps_.get(0)->isHT())
    procedureList_.push_back(std::bind(&CodeStreamCompress::write_cap, this));
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_cod, this));
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_qcd, this));
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_all_coc, this));
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_all_qcc, this));

  if(cp_.codingParams_.enc_.writeTlm_)
    procedureList_.push_back(std::bind(&CodeStreamCompress::write_tlm_begin, this));
  if(cp_.tcps_.get(0)->hasPoc())
    procedureList_.push_back(std::bind(&CodeStreamCompress::writePoc, this));

  procedureList_.push_back(std::bind(&CodeStreamCompress::write_regions, this));
  procedureList_.push_back(std::bind(&CodeStreamCompress::write_com, this));
  // begin custom procedures
  if((cp_.rsiz_ & (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT)) ==
     (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT))
    procedureList_.push_back(std::bind(&CodeStreamCompress::write_mct_data_group, this));
  // end custom procedures

  procedureList_.push_back(std::bind(&CodeStreamCompress::updateRates, this));

  return true;
}
bool CodeStreamCompress::writeTilePart(TileProcessorCompress* tileProcessor)
{
  uint64_t currentPos = 0;
  if(tileProcessor->canPreCalculateTileLen())
    currentPos = stream_->tell();
  auto calculatedBytesWritten = tileProcessor->getPreCalculatedTileLen();
  // 1. write SOT
  SOTMarker sot;
  if(!sot.write(tileProcessor, calculatedBytesWritten))
    return false;
  uint32_t tilePartBytesWritten = sotMarkerSegmentLen;
  // 2. write POC marker to first tile part
  if(tileProcessor->canWritePocMarker())
  {
    if(!writePoc())
      return false;
    auto tcp = cp_.tcps_.get(tileProcessor->getIndex());
    tilePartBytesWritten += getPocSize(headerImage_->numcomps, tcp->getNumProgressions());
  }
  // 3. compress tile part and write to stream
  if(!tileProcessor->writeTilePartT2(&tilePartBytesWritten))
  {
    grklog.error("Cannot compress tile");
    return false;
  }
  // 4. now that we know the tile part length, we can
  // write the Psot in the SOT marker
  if(!sot.write_psot(stream_, tilePartBytesWritten))
    return false;
  // 5. update TLM
  if(tileProcessor->canPreCalculateTileLen())
  {
    auto actualBytes = stream_->tell() - currentPos;
    // grklog.info("Tile %u: precalculated / actual : %u / %u",
    //		tileProcessor->getIndex(), calculatedBytesWritten, actualBytes);
    if(actualBytes != calculatedBytesWritten)
    {
      grklog.error("Error in tile length calculation. Please share uncompressed image\n"
                   "and compression parameters on Github issue tracker");
      return false;
    }
    tilePartBytesWritten = calculatedBytesWritten;
  }
  if(cp_.tlmMarkers_)
    cp_.tlmMarkers_->add(tileProcessor->getIndex(), tilePartBytesWritten);
  tileProcessor->incTilePartCounter();

  return true;
}
bool CodeStreamCompress::writeTileParts(TileProcessorCompress* tileProcessor)
{
  if(tileProcessor->getTilePartCounter() != 0)
    return false;
  // 1. write first tile part
  tileProcessor->setProgIterNum(0);
  tileProcessor->setFirstPocTilePart(true);
  if(!writeTilePart(tileProcessor))
    return false;
  // 2. write the other tile parts
  uint32_t prog_iter_num;
  auto tcp = cp_.tcps_.get(tileProcessor->getIndex());
  // write tile parts for first progression order
  uint64_t numTileParts = getNumTilePartsForProgression(0, tileProcessor->getIndex());
  if(numTileParts > maxTilePartsPerTileJ2K)
  {
    grklog.error("Number of tile parts %u for first POC exceeds maximum number of tile parts %u",
                 numTileParts, maxTilePartsPerTileJ2K);
    return false;
  }
  tileProcessor->setFirstPocTilePart(false);
  for(uint8_t tilepartno = 1; tilepartno < (uint8_t)numTileParts; ++tilepartno)
  {
    if(!writeTilePart(tileProcessor))
      return false;
  }
  // write tile parts for remaining progression orders
  for(prog_iter_num = 1; prog_iter_num < tcp->getNumProgressions(); ++prog_iter_num)
  {
    tileProcessor->setProgIterNum(prog_iter_num);
    numTileParts = getNumTilePartsForProgression(prog_iter_num, tileProcessor->getIndex());
    if(numTileParts > maxTilePartsPerTileJ2K)
    {
      grklog.error("Number of tile parts %u exceeds maximum number of "
                   "tile parts %u",
                   numTileParts, maxTilePartsPerTileJ2K);
      return false;
    }
    for(uint8_t tilepartno = 0; tilepartno < numTileParts; ++tilepartno)
    {
      tileProcessor->setFirstPocTilePart(tilepartno == 0);
      if(!writeTilePart(tileProcessor))
        return false;
    }
  }
  tileProcessor->incrementIndex();

  return true;
}
bool CodeStreamCompress::updateRates(void)
{
  auto cp = &cp_;
  auto image = headerImage_;
  auto width = image->x1 - image->x0;
  auto height = image->y1 - image->y0;
  if(width <= 0 || height <= 0)
    return false;

  uint32_t bits_empty = 8 * (uint32_t)image->comps->dx * image->comps->dy;
  uint32_t size_pixel = (uint32_t)image->numcomps * image->comps->prec;
  auto headerSize = (double)stream_->tell();

  for(uint16_t tile_y = 0; tile_y < cp->t_grid_height_; ++tile_y)
  {
    for(uint16_t tile_x = 0; tile_x < cp->t_grid_width_; ++tile_x)
    {
      uint16_t tileId = (uint16_t)(tile_y * cp->t_grid_width_ + tile_x);
      auto tcp = cp->tcps_.get(tileId);
      double stride = 0;
      if(cp->codingParams_.enc_.enableTilePartGeneration_)
        stride = (tcp->signalledNumTileParts_ - 1) * 14;
      double offset = stride / tcp->numLayers_;
      auto tileBounds = cp->getTileBounds(image->getBounds(), tile_x, tile_y);
      uint64_t numTilePixels = tileBounds.area();
      for(uint16_t k = 0; k < tcp->numLayers_; ++k)
      {
        double* rates = tcp->rates_ + k;
        // convert to target bytes for layer
        if(*rates > 0.0f)
          *rates = ((((double)size_pixel * (double)numTilePixels)) /
                    ((double)*rates * (double)bits_empty)) -
                   offset;
      }
    }
  }
  for(uint16_t tile_y = 0; tile_y < cp->t_grid_height_; ++tile_y)
  {
    for(uint16_t tile_x = 0; tile_x < cp->t_grid_width_; ++tile_x)
    {
      uint16_t tileId = (uint16_t)(tile_y * cp->t_grid_width_ + tile_x);
      auto tcp = cp->tcps_.get(tileId);
      double* rates = tcp->rates_;
      auto tileBounds = cp->getTileBounds(image->getBounds(), tile_x, tile_y);
      uint64_t numTilePixels = tileBounds.area();
      // correction for header size is distributed amongst all tiles
      double sot_adjust = ((double)numTilePixels * (double)headerSize) / ((double)width * height);
      for(uint16_t k = 0; k < (uint16_t)(tcp->numLayers_ - 1); ++k)
      {
        if(*rates > 0.0)
          *rates -= sot_adjust;
        ++rates;
      }
      if(*rates > 0.0)
        *rates -= (sot_adjust + 2.0);
    }
  }

  return true;
}
bool CodeStreamCompress::compressValidation()
{
  bool valid = true;

  /* ISO 15444-1:2004 states between 1 & 33
   * ergo (number of decomposition levels between 0 -> 32) */
  if((cp_.tcps_.get(0)->tccps_->numresolutions_ == 0) ||
     (cp_.tcps_.get(0)->tccps_->numresolutions_ > GRK_MAXRLVLS))
  {
    grklog.error("Invalid number of resolutions : %u not in range [1,%u]",
                 cp_.tcps_.get(0)->tccps_->numresolutions_, GRK_MAXRLVLS);
    return false;
  }
  if(cp_.t_width_ == 0)
  {
    grklog.error("Tile x dimension must be greater than zero ");
    return false;
  }
  if(cp_.t_height_ == 0)
  {
    grklog.error("Tile y dimension must be greater than zero ");
    return false;
  }

  return valid;
}
bool CodeStreamCompress::write_soc()
{
  return stream_->write(SOC);
}
bool CodeStreamCompress::write_siz()
{
  SIZMarker siz;

  return siz.write(this, stream_);
}
bool CodeStreamCompress::write_cap()
{
  return cp_.tcps_.get(0)->qcd_->write(stream_);
}

bool CodeStreamCompress::write_com()
{
  for(uint32_t i = 0; i < cp_.numComments_; ++i)
  {
    const char* comment = cp_.comment_[i];
    uint16_t comment_size = cp_.commentLength_[i];
    if(!comment_size)
    {
      grklog.warn("Empty comment. Ignoring");
      continue;
    }
    if(comment_size > GRK_MAX_COMMENT_LENGTH)
    {
      grklog.warn("Comment length %s is greater than maximum comment length %u. Ignoring",
                  comment_size, GRK_MAX_COMMENT_LENGTH);
      continue;
    }
    uint32_t totacom_size = (uint32_t)comment_size + 6;

    /* COM */
    if(!stream_->write(COM))
      return false;
    /* L_COM */
    if(!stream_->write((uint16_t)(totacom_size - 2)))
      return false;
    if(!stream_->write((uint16_t)(cp_.isBinaryComment_[i] ? 0 : 1)))
      return false;
    if(!stream_->writeBytes((uint8_t*)comment, comment_size))
      return false;
  }

  return true;
}
bool CodeStreamCompress::write_cod()
{
  uint32_t code_size;
  auto tcp = cp_.tcps_.get(0);
  code_size = 9 + get_SPCod_SPCoc_size(0);

  /* COD */
  if(!stream_->write(COD))
    return false;
  /* L_COD */
  if(!stream_->write((uint16_t)(code_size - 2)))
    return false;
  /* Scod */
  if(!stream_->write8u(tcp->csty_))
    return false;
  /* SGcod (A) */
  if(!stream_->write8u((uint8_t)tcp->prg_))
    return false;
  /* SGcod (B) */
  if(!stream_->write(tcp->numLayers_))
    return false;
  /* SGcod (C) */
  if(!stream_->write8u(tcp->mct_))
    return false;
  if(!write_SPCod_SPCoc(0))
  {
    grklog.error("Error writing COD marker");
    return false;
  }

  return true;
}
bool CodeStreamCompress::write_coc(uint16_t comp_no)
{
  auto tcp = cp_.tcps_.get(0);
  auto image = getHeaderImage();
  uint32_t comp_room = (image->numcomps <= 256) ? 1 : 2;
  uint32_t coc_size = codSocLen + comp_room + get_SPCod_SPCoc_size(comp_no);

  /* COC */
  if(!stream_->write(COC))
    return false;
  /* L_COC */
  if(!stream_->write((uint16_t)(coc_size - 2)))
    return false;
  /* Ccoc */
  if(comp_room == 2)
  {
    if(!stream_->write(comp_no))
      return false;
  }
  else
  {
    if(!stream_->write8u((uint8_t)comp_no))
      return false;
  }

  /* Scoc */
  if(!stream_->write8u((uint8_t)tcp->tccps_[comp_no].csty_))
    return false;

  return write_SPCod_SPCoc(0);
}
bool CodeStreamCompress::compare_coc(uint16_t first_comp_no, uint16_t second_comp_no)
{
  auto tcp = cp_.tcps_.get(0);

  if(tcp->tccps_[first_comp_no].csty_ != tcp->tccps_[second_comp_no].csty_)
    return false;

  return compare_SPCod_SPCoc(first_comp_no, second_comp_no);
}

bool CodeStreamCompress::write_qcd()
{
  uint32_t qcd_size;
  qcd_size = 4 + get_SQcd_SQcc_size(0);

  /* QCD */
  if(!stream_->write(QCD))
    return false;
  /* L_QCD */
  if(!stream_->write((uint16_t)(qcd_size - 2)))
    return false;
  if(!write_SQcd_SQcc(0))
  {
    grklog.error("Error writing QCD marker");
    return false;
  }

  return true;
}
bool CodeStreamCompress::write_qcc(uint16_t comp_no)
{
  uint32_t qcc_size = 6 + get_SQcd_SQcc_size(comp_no);

  /* QCC */
  if(!stream_->write(QCC))
  {
    return false;
  }

  if(getHeaderImage()->numcomps <= 256)
  {
    --qcc_size;

    /* L_QCC */
    if(!stream_->write((uint16_t)(qcc_size - 2)))
      return false;
    /* Cqcc */
    if(!stream_->write8u((uint8_t)comp_no))
      return false;
  }
  else
  {
    /* L_QCC */
    if(!stream_->write((uint16_t)(qcc_size - 2)))
      return false;
    /* Cqcc */
    if(!stream_->write(comp_no))
      return false;
  }

  return write_SQcd_SQcc(comp_no);
}
bool CodeStreamCompress::compare_qcc(uint16_t first_comp_no, uint16_t second_comp_no)
{
  return compare_SQcd_SQcc(first_comp_no, second_comp_no);
}
bool CodeStreamCompress::writePoc()
{
  auto tcp = cp_.tcps_.get(0);
  auto tccp = tcp->tccps_;
  auto image = getHeaderImage();
  uint16_t numComps = image->numcomps;
  uint32_t numPocs = tcp->getNumProgressions();
  uint32_t pocRoom = (numComps <= 256) ? 1 : 2;

  auto poc_size = getPocSize(numComps, numPocs);

  /* POC  */
  if(!stream_->write(POC))
    return false;

  /* Lpoc */
  if(!stream_->write((uint16_t)(poc_size - 2)))
    return false;

  for(uint32_t i = 0; i < numPocs; ++i)
  {
    auto current_prog = tcp->progressionOrderChange_ + i;
    /* RSpoc_i */
    if(!stream_->write8u(current_prog->res_s))
      return false;
    /* CSpoc_i */
    if(pocRoom == 2)
    {
      if(!stream_->write(current_prog->comp_s))
        return false;
    }
    else
    {
      if(!stream_->write8u((uint8_t)current_prog->comp_s))
        return false;
    }
    /* LYEpoc_i */
    if(!stream_->write(current_prog->lay_e))
      return false;
    /* REpoc_i */
    if(!stream_->write8u(current_prog->res_e))
      return false;
    /* CEpoc_i */
    if(pocRoom == 2)
    {
      if(!stream_->write(current_prog->comp_e))
        return false;
    }
    else
    {
      if(!stream_->write8u((uint8_t)current_prog->comp_e))
        return false;
    }
    /* Ppoc_i */
    if(!stream_->write8u((uint8_t)current_prog->progression))
      return false;

    /* change the value of the max layer according to the actual number of layers in the file,
     * components and resolutions*/
    current_prog->lay_e = std::min<uint16_t>(current_prog->lay_e, tcp->numLayers_);
    current_prog->res_e = std::min<uint8_t>(current_prog->res_e, tccp->numresolutions_);
    current_prog->comp_e = std::min<uint16_t>(current_prog->comp_e, numComps);
  }

  return true;
}

bool CodeStreamCompress::write_mct_data_group()
{
  if(!write_cbd())
    return false;

  auto tcp = cp_.tcps_.get(0);
  auto mct_record = tcp->mctRecords_;

  for(uint32_t i = 0; i < tcp->numMctRecords_; ++i)
  {
    if(!write_mct_record(mct_record, stream_))
      return false;
    ++mct_record;
  }

  auto mcc_record = tcp->mccRecords_;
  for(uint32_t i = 0; i < tcp->numMccRecords_; ++i)
  {
    if(!write_mcc_record(mcc_record, stream_))
      return false;
    ++mcc_record;
  }

  return write_mco();
}
bool CodeStreamCompress::write_all_coc()
{
  for(uint16_t compno = 1; compno < getHeaderImage()->numcomps; ++compno)
  {
    /* cod is first component of first tile */
    if(!compare_coc(0, compno))
    {
      if(!write_coc(compno))
        return false;
    }
  }

  return true;
}
bool CodeStreamCompress::write_all_qcc()
{
  for(uint16_t compno = 1; compno < getHeaderImage()->numcomps; ++compno)
  {
    /* qcd is first component of first tile */
    if(!compare_qcc(0, compno))
    {
      if(!write_qcc(compno))
        return false;
    }
  }
  return true;
}
bool CodeStreamCompress::write_regions()
{
  for(uint16_t compno = 0; compno < getHeaderImage()->numcomps; ++compno)
  {
    auto tccp = cp_.tcps_.get(0)->tccps_ + compno;
    if(tccp->roishift_)
    {
      if(!write_rgn(0, compno, getHeaderImage()->numcomps))
        return false;
    }
  }

  return true;
}
bool CodeStreamCompress::write_mcc_record(grk_simple_mcc_decorrelation_data* p_mcc_record,
                                          IStream* stream)
{
  uint32_t mcc_size;
  uint32_t nb_bytes_for_comp;
  uint32_t mask;
  uint32_t tmcc;

  assert(stream != nullptr);

  if(p_mcc_record->nb_comps_ > 255)
  {
    nb_bytes_for_comp = 2;
    mask = 0x8000;
  }
  else
  {
    nb_bytes_for_comp = 1;
    mask = 0;
  }

  mcc_size = p_mcc_record->nb_comps_ * 2 * nb_bytes_for_comp + 19;

  /* MCC */
  if(!stream->write(MCC))
    return false;
  /* Lmcc */
  if(!stream->write((uint16_t)(mcc_size - 2)))
    return false;
  /* first marker */
  /* Zmcc */
  if(!stream->write((uint16_t)0))
    return false;
  /* Imcc -> no need for other values, take the first */
  if(!stream->write8u((uint8_t)p_mcc_record->index_))
    return false;
  /* only one marker atm */
  /* Ymcc */
  if(!stream->write((uint16_t)0))
    return false;
  /* Qmcc -> number of collections -> 1 */
  if(!stream->write((uint16_t)1))
    return false;
  /* Xmcci type of component transformation -> array based decorrelation */
  if(!stream->write8u((uint8_t)0x1))
    return false;
  /* Nmcci number of input components involved and size for each component offset = 8 bits */
  if(!stream->write((uint16_t)(p_mcc_record->nb_comps_ | mask)))
    return false;

  for(uint16_t i = 0; i < p_mcc_record->nb_comps_; ++i)
  {
    /* Cmccij Component offset*/
    if(nb_bytes_for_comp == 2)
    {
      if(!stream->write(i))
        return false;
    }
    else
    {
      if(!stream->write8u((uint8_t)i))
        return false;
    }
  }

  /* Mmcci number of output components involved and size for each component offset = 8 bits */
  if(!stream->write((uint16_t)(p_mcc_record->nb_comps_ | mask)))
    return false;

  for(uint16_t i = 0; i < p_mcc_record->nb_comps_; ++i)
  {
    /* Wmccij Component offset*/
    if(nb_bytes_for_comp == 2)
    {
      if(!stream->write(i))
        return false;
    }
    else
    {
      if(!stream->write8u((uint8_t)i))
        return false;
    }
  }

  tmcc = ((uint32_t)((!p_mcc_record->is_irreversible_) & 1U)) << 16;

  if(p_mcc_record->decorrelation_array_)
    tmcc |= p_mcc_record->decorrelation_array_->index_;

  if(p_mcc_record->offset_array_)
    tmcc |= ((p_mcc_record->offset_array_->index_) << 8);

  /* Tmcci : use MCT defined as number 1 and irreversible array based. */
  return stream->write24u(tmcc);
}
bool CodeStreamCompress::write_mco()
{
  uint32_t mco_size;
  uint32_t i;
  auto tcp = cp_.tcps_.get(0);
  mco_size = 5 + tcp->numMccRecords_;

  /* MCO */
  if(!stream_->write(MCO))
    return false;

  /* Lmco */
  if(!stream_->write((uint16_t)(mco_size - 2)))
    return false;

  /* Nmco : only one transform stage*/
  if(!stream_->write8u((uint8_t)tcp->numMccRecords_))
    return false;

  auto mcc_record = tcp->mccRecords_;
  for(i = 0; i < tcp->numMccRecords_; ++i)
  {
    /* Imco -> use the mcc indicated by 1*/
    if(!stream_->write8u((uint8_t)mcc_record->index_))
      return false;
    ++mcc_record;
  }
  return true;
}

bool CodeStreamCompress::write_cbd()
{
  uint32_t i;
  auto image = getHeaderImage();
  uint16_t cbd_size = (uint16_t)(6U + getHeaderImage()->numcomps);

  /* CBD */
  if(!stream_->write(CBD))
    return false;

  /* L_CBD */
  if(!stream_->write((uint16_t)(cbd_size - 2U)))
    return false;

  /* Ncbd */
  if(!stream_->write(image->numcomps))
    return false;

  for(i = 0; i < image->numcomps; ++i)
  {
    auto comp = image->comps + i;
    /* Component bit depth */
    uint8_t bpc = (uint8_t)(comp->prec - 1);
    if(comp->sgnd)
      bpc = (uint8_t)(bpc + (1 << 7));
    if(!stream_->write8u(bpc))
      return false;
  }
  return true;
}

bool CodeStreamCompress::write_tlm_begin()
{
  if(!cp_.tlmMarkers_)
    cp_.tlmMarkers_ = std::make_unique<TLMMarker>(stream_);

  return cp_.tlmMarkers_->writeBegin(totalTileParts_);
}
bool CodeStreamCompress::write_tlm_end()
{
  return cp_.tlmMarkers_->writeEnd();
}
uint32_t CodeStreamCompress::get_SPCod_SPCoc_size(uint16_t comp_no)
{
  auto tcp = cp_.tcps_.get(0);
  auto tccp = tcp->tccps_ + comp_no;
  assert(comp_no < getHeaderImage()->numcomps);

  uint32_t rc = SPCodSPCocLen;
  if(tccp->csty_ & CCP_CSTY_PRECINCT)
    rc += tccp->numresolutions_;

  return rc;
}
bool CodeStreamCompress::compare_SPCod_SPCoc(uint16_t first_comp_no, uint16_t second_comp_no)
{
  auto tcp = cp_.tcps_.get(0);
  auto tccp0 = tcp->tccps_ + first_comp_no;
  auto tccp1 = tcp->tccps_ + second_comp_no;

  if(tccp0->numresolutions_ != tccp1->numresolutions_)
    return false;
  if(tccp0->cblkw_expn_ != tccp1->cblkw_expn_)
    return false;
  if(tccp0->cblkh_expn_ != tccp1->cblkh_expn_)
    return false;
  if(tccp0->cblkStyle_ != tccp1->cblkStyle_)
    return false;
  if(tccp0->qmfbid_ != tccp1->qmfbid_)
    return false;
  if((tccp0->csty_ & CCP_CSTY_PRECINCT) != (tccp1->csty_ & CCP_CSTY_PRECINCT))
    return false;
  for(uint8_t i = 0U; i < tccp0->numresolutions_; ++i)
  {
    if(tccp0->precWidthExp_[i] != tccp1->precWidthExp_[i])
      return false;
    if(tccp0->precHeightExp_[i] != tccp1->precHeightExp_[i])
      return false;
  }

  return true;
}

bool CodeStreamCompress::write_SPCod_SPCoc(uint16_t comp_no)
{
  auto tcp = cp_.tcps_.get(0);
  auto tccp = tcp->tccps_ + comp_no;

  assert(comp_no < (getHeaderImage()->numcomps));

  /* SPcoc (D) */
  if(!stream_->write8u((uint8_t)(tccp->numresolutions_ - 1)))
    return false;
  /* SPcoc (E) */
  if(!stream_->write8u((uint8_t)(tccp->cblkw_expn_ - 2)))
    return false;
  /* SPcoc (F) */
  if(!stream_->write8u((uint8_t)(tccp->cblkh_expn_ - 2)))
    return false;
  /* SPcoc (G) */
  if(!stream_->write8u(tccp->cblkStyle_))
    return false;
  /* SPcoc (H) */
  if(!stream_->write8u(tccp->qmfbid_))
    return false;

  if(tccp->csty_ & CCP_CSTY_PRECINCT)
  {
    for(uint8_t i = 0; i < tccp->numresolutions_; ++i)
    {
      /* SPcoc (I_i) */
      if(!stream_->write8u((uint8_t)(tccp->precWidthExp_[i] + (tccp->precHeightExp_[i] << 4))))
        return false;
    }
  }

  return true;
}
uint32_t CodeStreamCompress::get_SQcd_SQcc_size(uint16_t comp_no)
{
  auto tcp = cp_.tcps_.get(0);
  auto tccp = tcp->tccps_ + comp_no;
  assert(comp_no < getHeaderImage()->numcomps);

  uint32_t num_bands = (tccp->qntsty_ == CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions_ * 3U - 2);

  return (tccp->qntsty_ == CCP_QNTSTY_NOQNT) ? 1 + num_bands : 1 + 2 * num_bands;
}
bool CodeStreamCompress::compare_SQcd_SQcc(uint16_t first_comp_no, uint16_t second_comp_no)
{
  auto tcp = cp_.tcps_.get(0);
  auto tccp0 = tcp->tccps_ + first_comp_no;
  auto tccp1 = tcp->tccps_ + second_comp_no;

  if(tccp0->qntsty_ != tccp1->qntsty_)
    return false;
  if(tccp0->numgbits_ != tccp1->numgbits_)
    return false;
  uint32_t band_no, num_bands;
  if(tccp0->qntsty_ == CCP_QNTSTY_SIQNT)
  {
    num_bands = 1U;
  }
  else
  {
    num_bands = tccp0->numresolutions_ * 3U - 2U;
    if(num_bands != (tccp1->numresolutions_ * 3U - 2U))
      return false;
  }
  for(band_no = 0; band_no < num_bands; ++band_no)
  {
    if(tccp0->stepsizes_[band_no].expn != tccp1->stepsizes_[band_no].expn)
      return false;
  }
  if(tccp0->qntsty_ != CCP_QNTSTY_NOQNT)
  {
    for(band_no = 0; band_no < num_bands; ++band_no)
    {
      if(tccp0->stepsizes_[band_no].mant != tccp1->stepsizes_[band_no].mant)
        return false;
    }
  }

  return true;
}
bool CodeStreamCompress::write_SQcd_SQcc(uint16_t comp_no)
{
  assert(comp_no < getHeaderImage()->numcomps);
  auto tcp = cp_.tcps_.get(0);
  auto tccp = tcp->tccps_ + comp_no;
  uint8_t num_bands =
      (tccp->qntsty_ == CCP_QNTSTY_SIQNT) ? 1 : (uint8_t)(tccp->numresolutions_ * 3U - 2);

  /* Sqcx */
  if(!stream_->write8u((uint8_t)(tccp->qntsty_ + (tccp->numgbits_ << 5))))
    return false;

  /* SPqcx_i */
  for(uint8_t band_no = 0; band_no < num_bands; ++band_no)
  {
    uint32_t expn = tccp->stepsizes_[band_no].expn;
    uint32_t mant = tccp->stepsizes_[band_no].mant;
    if(tccp->qntsty_ == CCP_QNTSTY_NOQNT)
    {
      if(!stream_->write8u((uint8_t)(expn << 3)))
        return false;
    }
    else
    {
      if(!stream_->write((uint16_t)((expn << 11) + mant)))
        return false;
    }
  }
  return true;
}
uint16_t CodeStreamCompress::getPocSize(uint16_t numComps, uint32_t numPocs)
{
  uint32_t pocRoom = (numComps <= 256) ? 1 : 2;

  return (uint16_t)(4 + (5 + 2 * pocRoom) * numPocs);
}
bool CodeStreamCompress::validateProgressionOrders(const grk_progression* progressions,
                                                   uint32_t numProgressions, uint8_t numresolutions,
                                                   uint16_t numComps, uint16_t numLayers)
{
  uint8_t resno;
  uint16_t compno, layno;
  uint32_t i;
  uint32_t step_c = 1;
  uint32_t step_r = numComps * step_c;
  uint32_t step_l = numresolutions * step_r;

  auto packet_array = new uint8_t[(size_t)step_l * numLayers];
  memset(packet_array, 0, (size_t)step_l * numLayers * sizeof(uint8_t));

  /* iterate through all the pocs */
  for(i = 0; i < numProgressions; ++i)
  {
    auto currentPoc = progressions + i;
    size_t index = step_r * currentPoc->res_s;
    /* take each resolution for each poc */
    for(resno = currentPoc->res_s; resno < std::min<uint32_t>(currentPoc->res_e, numresolutions);
        ++resno)
    {
      size_t res_index = index + currentPoc->comp_s * step_c;

      /* take each comp of each resolution for each poc */
      for(compno = currentPoc->comp_s; compno < std::min<uint32_t>(currentPoc->comp_e, numComps);
          ++compno)
      {
        size_t comp_index = res_index + 0 * step_l;

        /* and finally take each layer of each res of ... */
        for(layno = 0; layno < std::min<uint32_t>(currentPoc->lay_e, numLayers); ++layno)
        {
          /*index = step_r * resno + step_c * compno + step_l * layno;*/
          packet_array[comp_index] = 1;
          // printf("%u %u\n",i,comp_index);
          comp_index += step_l;
        }
        res_index += step_c;
      }
      index += step_r;
    }
  }
  bool loss = false;
  size_t index = 0;
  for(layno = 0; layno < numLayers; ++layno)
  {
    for(resno = 0; resno < numresolutions; ++resno)
    {
      for(compno = 0; compno < numComps; ++compno)
      {
        if(!packet_array[index])
        {
          loss = true;
          break;
        }
        index += step_c;
      }
    }
  }
  if(loss)
    grklog.error("POC: missing packets");
  delete[] packet_array;

  return !loss;
}
bool CodeStreamCompress::init_mct_encoding(TileCodingParams* tcp, GrkImage* image)
{
  uint32_t i;
  uint32_t indix = 1;
  grk_mct_data *mct_deco_data = nullptr, *mct_offset_data = nullptr;
  grk_simple_mcc_decorrelation_data* mcc_data;
  uint32_t mct_size, nb_elem;
  float *data, *current_data;
  TileComponentCodingParams* tccp;

  assert(tcp != nullptr);

  if(tcp->mct_ != 2)
    return true;

  if(tcp->mctDecodingMatrix_)
  {
    if(tcp->numMctRecords_ == tcp->numMaxMctRecords_)
    {
      tcp->numMaxMctRecords_ += default_number_mct_records;

      auto new_mct_records = (grk_mct_data*)grk_realloc(tcp->mctRecords_, tcp->numMaxMctRecords_ *
                                                                              sizeof(grk_mct_data));
      if(!new_mct_records)
      {
        grk_free(tcp->mctRecords_);
        tcp->mctRecords_ = nullptr;
        tcp->numMaxMctRecords_ = 0;
        tcp->numMctRecords_ = 0;
        /* grklog.error( "Not enough memory to set up mct compressing"); */
        return false;
      }
      tcp->mctRecords_ = new_mct_records;
      mct_deco_data = tcp->mctRecords_ + tcp->numMctRecords_;

      memset(mct_deco_data, 0,
             (tcp->numMaxMctRecords_ - tcp->numMctRecords_) * sizeof(grk_mct_data));
    }
    mct_deco_data = tcp->mctRecords_ + tcp->numMctRecords_;
    grk_free(mct_deco_data->data_);
    mct_deco_data->data_ = nullptr;

    mct_deco_data->index_ = indix++;
    mct_deco_data->array_type_ = MCT_TYPE_DECORRELATION;
    mct_deco_data->element_type_ = MCT_TYPE_FLOAT;
    nb_elem = (uint32_t)image->numcomps * image->numcomps;
    mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_deco_data->element_type_];
    mct_deco_data->data_ = (uint8_t*)grk_malloc(mct_size);

    if(!mct_deco_data->data_)
      return false;

    mct_write_functions_from_float[mct_deco_data->element_type_](tcp->mctDecodingMatrix_,
                                                                 mct_deco_data->data_, nb_elem);

    mct_deco_data->data_size_ = mct_size;
    ++tcp->numMctRecords_;
  }

  if(tcp->numMctRecords_ == tcp->numMaxMctRecords_)
  {
    grk_mct_data* new_mct_records;
    tcp->numMaxMctRecords_ += default_number_mct_records;
    new_mct_records =
        (grk_mct_data*)grk_realloc(tcp->mctRecords_, tcp->numMaxMctRecords_ * sizeof(grk_mct_data));
    if(!new_mct_records)
    {
      grk_free(tcp->mctRecords_);
      tcp->mctRecords_ = nullptr;
      tcp->numMaxMctRecords_ = 0;
      tcp->numMctRecords_ = 0;
      /* grklog.error( "Not enough memory to set up mct compressing"); */
      return false;
    }
    tcp->mctRecords_ = new_mct_records;
    mct_offset_data = tcp->mctRecords_ + tcp->numMctRecords_;

    memset(mct_offset_data, 0,
           (tcp->numMaxMctRecords_ - tcp->numMctRecords_) * sizeof(grk_mct_data));
    if(mct_deco_data)
      mct_deco_data = mct_offset_data - 1;
  }
  mct_offset_data = tcp->mctRecords_ + tcp->numMctRecords_;
  if(mct_offset_data->data_)
  {
    grk_free(mct_offset_data->data_);
    mct_offset_data->data_ = nullptr;
  }
  mct_offset_data->index_ = indix++;
  mct_offset_data->array_type_ = MCT_TYPE_OFFSET;
  mct_offset_data->element_type_ = MCT_TYPE_FLOAT;
  nb_elem = image->numcomps;
  mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_offset_data->element_type_];
  mct_offset_data->data_ = (uint8_t*)grk_malloc(mct_size);
  if(!mct_offset_data->data_)
    return false;

  data = (float*)grk_malloc(nb_elem * sizeof(float));
  if(!data)
  {
    grk_free(mct_offset_data->data_);
    mct_offset_data->data_ = nullptr;
    return false;
  }
  tccp = tcp->tccps_;
  current_data = data;

  for(i = 0; i < nb_elem; ++i)
  {
    *(current_data++) = (float)(tccp->dcLevelShift_);
    ++tccp;
  }
  mct_write_functions_from_float[mct_offset_data->element_type_](data, mct_offset_data->data_,
                                                                 nb_elem);
  grk_free(data);
  mct_offset_data->data_size_ = mct_size;
  ++tcp->numMctRecords_;

  if(tcp->numMccRecords_ == tcp->numMaxMccRecords_)
  {
    grk_simple_mcc_decorrelation_data* new_mcc_records;
    tcp->numMaxMccRecords_ += default_number_mct_records;
    new_mcc_records = (grk_simple_mcc_decorrelation_data*)grk_realloc(
        tcp->mccRecords_, tcp->numMaxMccRecords_ * sizeof(grk_simple_mcc_decorrelation_data));
    if(!new_mcc_records)
    {
      grk_free(tcp->mccRecords_);
      tcp->mccRecords_ = nullptr;
      tcp->numMaxMccRecords_ = 0;
      tcp->numMccRecords_ = 0;
      /* grklog.error( "Not enough memory to set up mct compressing"); */
      return false;
    }
    tcp->mccRecords_ = new_mcc_records;
    mcc_data = tcp->mccRecords_ + tcp->numMccRecords_;
    memset(mcc_data, 0,
           (tcp->numMaxMccRecords_ - tcp->numMccRecords_) *
               sizeof(grk_simple_mcc_decorrelation_data));
  }
  mcc_data = tcp->mccRecords_ + tcp->numMccRecords_;
  mcc_data->decorrelation_array_ = mct_deco_data;
  mcc_data->is_irreversible_ = 1;
  mcc_data->nb_comps_ = image->numcomps;
  mcc_data->index_ = indix++;
  mcc_data->offset_array_ = mct_offset_data;
  ++tcp->numMccRecords_;

  return true;
}
uint8_t CodeStreamCompress::getNumTilePartsForProgression(uint32_t prog_iter_num, uint16_t tileno)
{
  uint64_t numTileParts = 1;
  auto cp = &cp_;
  auto tcp = cp->tcps_.get(tileno);
  assert(tcp != nullptr);

  /*  preconditions */
  assert(tileno < (cp->t_grid_width_ * cp->t_grid_height_));
  assert(prog_iter_num < tcp->getNumProgressions());

  auto current_poc = &(tcp->progressionOrderChange_[prog_iter_num]);
  assert(current_poc != 0);

  /* get the progression order as a character string */
  auto prog = convertProgressionOrder(tcp->prg_);
  assert(strlen(prog) > 0);

  if(cp->codingParams_.enc_.enableTilePartGeneration_)
  {
    for(uint8_t i = 0; i < 4; ++i)
    {
      switch(prog[i])
      {
        /* component wise */
        case 'C':
          numTileParts *= current_poc->tp_comp_e;
          break;
          /* resolution wise */
        case 'R':
          numTileParts *= current_poc->tp_res_e;
          break;
          /* precinct wise */
        case 'P':
          numTileParts *= current_poc->tp_prec_e;
          break;
          /* layer wise */
        case 'L':
          numTileParts *= current_poc->tp_lay_e;
          break;
      }
      // we start a new tile part when progression matches specified tile part
      // divider
      if(cp->codingParams_.enc_.newTilePartProgressionDivider_ == prog[i])
      {
        assert(prog[i] != 'P');
        cp->codingParams_.enc_.newTilePartProgressionPosition_ = i;
        break;
      }
    }
  }
  else
  {
    numTileParts = 1;
  }
  assert(numTileParts < maxTilePartsPerTileJ2K);

  return (uint8_t)numTileParts;
}
bool CodeStreamCompress::getNumTileParts(uint32_t* numTilePartsForAllTiles, GrkImage* image)
{
  assert(numTilePartsForAllTiles != nullptr);
  assert(image != nullptr);

  uint16_t numTiles = (uint16_t)(cp_.t_grid_width_ * cp_.t_grid_height_);
  *numTilePartsForAllTiles = 0;
  for(uint16_t tileno = 0; tileno < numTiles; ++tileno)
  {
    auto tcp = cp_.tcps_.get(tileno);
    uint8_t totalTilePartsForTile = 0;
    PacketManager::updateCompressParams(image, &cp_, tcp, tileno);
    for(uint32_t prog_iter_num = 0; prog_iter_num < tcp->getNumProgressions(); ++prog_iter_num)
    {
      auto numTilePartsForProgression = getNumTilePartsForProgression(prog_iter_num, tileno);
      uint16_t newTotalTilePartsForTile =
          uint16_t(numTilePartsForProgression + totalTilePartsForTile);
      if(newTotalTilePartsForTile > maxTilePartsPerTileJ2K)
      {
        grklog.error("Number of tile parts %u exceeds maximum number of "
                     "tile parts %u",
                     (uint16_t)newTotalTilePartsForTile, maxTilePartsPerTileJ2K);
        return false;
      }
      totalTilePartsForTile = (uint8_t)newTotalTilePartsForTile;

      auto newTotalTilePartsForAllTiles = *numTilePartsForAllTiles + numTilePartsForProgression;
      if(newTotalTilePartsForAllTiles > maxTotalTilePartsJ2K)
      {
        grklog.error("Total number of tile parts %u for image exceeds JPEG 2000 maximum total "
                     "number of "
                     "tile parts %u",
                     newTotalTilePartsForAllTiles, maxTotalTilePartsJ2K);
        return false;
      }
      *numTilePartsForAllTiles = newTotalTilePartsForAllTiles;
    }
    tcp->signalledNumTileParts_ = totalTilePartsForTile;
  }

  return true;
}

} // namespace grk
