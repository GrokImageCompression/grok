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

#include <memory>

#if defined(GROK_HAVE_FSEEKO) && !defined(fseek)
#define fseek fseeko
#define ftell ftello
#endif
#if defined(_WIN32)
#define GRK_FSEEK(stream, offset, whence) _fseeki64(stream, /* __int64 */ offset, whence)
#define GRK_FTELL(stream) /* __int64 */ _ftelli64(stream)
#else
#define GRK_FSEEK(stream, offset, whence) fseek(stream, offset, whence)
#define GRK_FTELL(stream) ftell(stream)
#endif
#if defined(__GNUC__)
#define GRK_RESTRICT __restrict__
#else
#define GRK_RESTRICT /* GRK_RESTRICT */
#endif

#include "grk_exceptions.h"
#include <Logger.h>
#include "MinHeap.h"
#include "SequentialCache.h"
#include "SparseCache.h"
#include "CodeStreamLimits.h"
#include "geometry.h"
#include "MemManager.h"
#include "buffer.h"
#include "ChunkBuffer.h"
#include "TileWindow.h"
#include "GrkObjectWrapper.h"
#include "ChronoTimer.h"
#include "testing.h"
#include "MappedFile.h"
#include "GrkMatrix.h"
#include "Quantizer.h"
#include "SparseBuffer.h"
#include "ResSimple.h"
#include "SparseCanvas.h"
#include "intmath.h"
#include "ImageComponentFlow.h"
#include "MarkerCache.h"
#include "SlabPool.h"
#include "StreamIO.h"
#include "IStream.h"
#include "MemAdvisor.h"

#include "FetchCommon.h"
#include "TPFetchSeq.h"

#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"

#include "MemStream.h"

#include "StreamGenerator.h"
#include "Profile.h"
#include "MarkerParser.h"
#include "Codec.h"

#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
struct TileProcessorCompress;
} // namespace grk
#include "PacketParser.h"
#include "PacketCache.h"
#include "CodingParams.h"
#include "CodeStream.h"
#include "PacketIter.h"

#include "PacketLengthCache.h"
#include "TLMMarker.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "FileFormatJP2Family.h"
#include "FileFormatJP2Compress.h"
#include "FileFormatJP2Decompress.h"
#include "FileFormatMJ2.h"
#include "FileFormatMJ2Compress.h"
#include "FileFormatMJ2Decompress.h"

#include "BitIO.h"
#include "TagTree.h"

#include "Codeblock.h"
#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"

#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "BlockExec.h"
#include "WindowScheduler.h"
#include "WholeTileScheduler.h"

#include "canvas/tile/TileComponentWindow.h"
#include "WaveletCommon.h"
#include "WaveletReverse.h"
#include "WaveletFwd.h"

#include "PacketManager.h"
#include "canvas/tile/TileComponent.h"
#include "canvas/tile/Tile.h"
#include "mct.h"

#include "TileProcessor.h"
#include "SchedulerFactory.h"
#include "TileCache.h"
#include "T2Compress.h"
#include "T2Decompress.h"
#include "plugin_bridge.h"
#include "DecompressScheduler.h"
#include "DecompressWindowScheduler.h"

namespace grk
{
TileProcessor::TileProcessor(uint16_t tile_index, TileCodingParams* tcp, CodeStream* codeStream,
                             IStream* stream, bool isCompressor, uint32_t tileCacheStrategy)
    : headerImage_(codeStream->getHeaderImage()),
      current_plugin_tile_(codeStream->getCurrentPluginTile()), cp_(codeStream->getCodingParams()),
      packetLengthCache_(std::make_shared<PacketLengthCache<uint32_t>>(cp_)),
      tile_(new Tile(headerImage_->numcomps)), tileIndex_(tile_index), tcp_(tcp),
      mct_(new Mct(tile_, headerImage_, tcp_)),
      markerParser_(isCompressor ? nullptr : new MarkerParser()), truncated_(false),
      image_(nullptr), isCompressor_(isCompressor), tileCacheStrategy_(tileCacheStrategy)
{
  setStream(stream, false);
  setProcessors(markerParser_);
  if(!isCompressor_)
    tcp_->packets_ = new PacketCache();
  threadTilePart_.resize(ExecSingleton::num_threads());
}
TileProcessor::~TileProcessor()
{
  release(GRK_TILE_CACHE_NONE);
  delete scheduler_;
  delete mct_;
  delete postDecompressFlow_;
  delete rootFlow_;
  if(!isCompressor_)
    delete tcp_;
  delete markerParser_;
}

void TileProcessor::setProcessors(MarkerParser* parser)
{
  if(!parser)
    return;
  parser->clearProcessors();
  parser->add(
      {{SOT, new MarkerProcessor(SOT,
                                 [this](uint8_t* data, uint16_t len) {
                                   return readSOT(getStream(), data, len, tilePartInfo_, true);
                                 })},

       {PLT, new MarkerProcessor(
                 PLT, [this](uint8_t* data, uint16_t len) { return readPLT(data, len); })},

       {PPT, new MarkerProcessor(
                 PPT, [this](uint8_t* data, uint16_t len) { return tcp_->readPpt(data, len); })},

       {COD, new MarkerProcessor(
                 COD, [this](uint8_t* data, uint16_t len) { return tcp_->readCod(data, len); })},

       {COC, new MarkerProcessor(
                 COC, [this](uint8_t* data, uint16_t len) { return tcp_->readCoc(data, len); })},

       {RGN, new MarkerProcessor(
                 RGN, [this](uint8_t* data, uint16_t len) { return tcp_->readRgn(data, len); })},

       {QCD,
        new MarkerProcessor(
            QCD, [this](uint8_t* data, uint16_t len) { return tcp_->readQcd(true, data, len); })},

       {QCC,
        new MarkerProcessor(
            QCC, [this](uint8_t* data, uint16_t len) { return tcp_->readQcc(true, data, len); })},

       {POC, new MarkerProcessor(POC,
                                 [this](uint8_t* data, uint16_t len) {
                                   return tcp_->readPoc(data, len,
                                                        threadTilePart_[ExecSingleton::workerId()]);
                                 })},

       {COM, new MarkerProcessor(
                 COM, [this](uint8_t* data, uint16_t len) { return cp_->readCom(data, len); })},

       {MCT, new MarkerProcessor(
                 MCT, [this](uint8_t* data, uint16_t len) { return tcp_->readMct(data, len); })},

       {MCC, new MarkerProcessor(
                 MCC, [this](uint8_t* data, uint16_t len) { return tcp_->readMcc(data, len); })},

       {MCO, new MarkerProcessor(
                 MCO, [this](uint8_t* data, uint16_t len) { return tcp_->readMco(data, len); })}});
}

// Performed after T2, just before plugin decompress is triggered
// note: only support single segment at the moment
void TileProcessor::decompress_synch_plugin_with_host(void)
{
  auto plugin_tile = current_plugin_tile_;
  if(plugin_tile && plugin_tile->tile_components)
  {
    auto tile = tile_;
    for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
    {
      auto tilec = &tile->comps_[compno];
      auto plugin_tilec = plugin_tile->tile_components[compno];
      assert(tilec->num_resolutions_ == plugin_tilec->numresolutions);
      for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
      {
        auto res = &tilec->resolutions_[resno];
        auto plugin_res = plugin_tilec->resolutions[resno];
        assert(plugin_res->num_bands == res->numBands_);
        for(uint32_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
        {
          auto band = &res->band[bandIndex];
          auto plugin_band = plugin_res->band[bandIndex];
          assert(plugin_band->num_precincts == res->precinctGrid_.area());
          //!!!! plugin still uses stepsize/2
          plugin_band->stepsize = band->stepsize_ / 2;
          for(auto [precinctIndex, vectorIndex] : band->precinctMap_)
          {
            auto prc = band->precincts_[vectorIndex];
            auto plugin_prc = plugin_band->precincts[precinctIndex];
            assert(plugin_prc->num_blocks == prc->getNumCblks());
            for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
            {
              auto cblk = prc->getDecompressedBlock(cblkno);
              if(!cblk->getNumDataParsedSegments())
                continue;
              // sanity check
              if(cblk->getNumDataParsedSegments() != 1)
              {
                grklog.info("Plugin does not handle code blocks with multiple "
                            "segments. Image will be decompressed on CPU.");
                throw PluginDecodeUnsupportedException();
              }
              uint32_t maxPasses =
                  3 * (uint32_t)((headerImage_->comps[0].prec + GRK_BIBO_EXTRA_BITS) - 2);
              if(cblk->getSegment(0)->totalPasses_ > maxPasses)
              {
                grklog.info("Number of passes %u in segment exceeds BIBO maximum %u. "
                            "Image will be decompressed on CPU.",
                            cblk->getSegment(0)->totalPasses_, maxPasses);
                throw PluginDecodeUnsupportedException();
              }

              auto plugin_cblk = plugin_prc->blocks[cblkno];

              // copy segments into plugin codeblock buffer, and point host code block
              // data to plugin data buffer
              plugin_cblk->compressed_data_length = (uint32_t)cblk->getDataChunksLength();
              cblk->copyDataChunksToContiguous(plugin_cblk->compressed_data);
              auto blockStream = cblk->getCompressedStream();
              blockStream->set_buf(plugin_cblk->compressed_data,
                                   plugin_cblk->compressed_data_length);
              blockStream->set_owns_data(false);
              plugin_cblk->num_bit_planes = cblk->numbps();
              plugin_cblk->num_passes = cblk->getSegment(0)->totalPasses_;
            }
          }
        }
      }
    }
  }
}

grk_progression_state TileProcessor::getProgressionState()
{
  grk_progression_state rc = {};
  rc.tile_index = tileIndex_;
  rc.single_tile = true;
  auto& prog = tile_->comps_->currentPacketProgressionState_;
  rc.num_resolutions = (uint8_t)prog.resLayers_.size();
  for(uint8_t r = 0; r < rc.num_resolutions; ++r)
  {
    rc.layers_per_resolution[r] = prog.resLayers_[r];
  }

  return rc;
}

bool TileProcessor::isInitialized(void)
{
  return initialized_;
}

bool TileProcessor::init(void)
{
  uint32_t state = grk_plugin_get_debug_state();
  // generate tile bounds from tile grid coordinates
  uint16_t tile_x = tileIndex_ % cp_->t_grid_width_;
  uint16_t tile_y = tileIndex_ / cp_->t_grid_width_;
  *((Rect32*)tile_) = cp_->getTileBounds(headerImage_->getBounds(), tile_x, tile_y);

  if(tcp_->tccps_->numresolutions_ == 0)
  {
    grklog.error("tiles require at least one resolution");
    return false;
  }

  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto imageComp = headerImage_->comps + compno;
    /*fprintf(stderr, "compno = %u/%u\n", compno, tile->numcomps);*/
    if(imageComp->dx == 0 || imageComp->dy == 0)
      return false;
    auto tilec = tile_->comps_ + compno;
    Rect32 unreducedTileComp = Rect32(
        ceildiv<uint32_t>(tile_->x0, imageComp->dx), ceildiv<uint32_t>(tile_->y0, imageComp->dy),
        ceildiv<uint32_t>(tile_->x1, imageComp->dx), ceildiv<uint32_t>(tile_->y1, imageComp->dy));

    // 1. calculate resolution bounds, precinct bounds and precinct grid
    // all in canvas coordinates (with subsampling)
    auto tccp = tcp_->tccps_ + compno;
    auto numres = tccp->numresolutions_;
    auto resolutions = new Resolution[numres];
    for(auto resno = 0U; resno < numres; ++resno)
    {
      auto res = resolutions + resno;
      res->setRect(ResSimple::getBandWindow((uint8_t)(numres - (resno + 1)), t1::BAND_ORIENT_LL,
                                            unreducedTileComp));

      /* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
      auto precWidthExp = tccp->precWidthExp_[resno];
      auto precHeightExp = tccp->precHeightExp_[resno];
      /* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
      res->precinctPartition_ = Resolution::genPrecinctPartition(*res, precWidthExp, precHeightExp);
      res->precinctGrid_ = res->precinctPartition_.scaleDownPow2(precWidthExp, precHeightExp);
      res->numBands_ = (resno == 0) ? 1 : 3;
      if(DEBUG_TILE_COMPONENT)
      {
        std::cout << "res: " << resno << " ";
        res->print();
      }
    }

    // 2. set band bounds and band step size
    for(uint8_t resno = 0U; resno < numres; ++resno)
    {
      auto res = resolutions + resno;
      for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        t1::eBandOrientation orientation =
            (resno == 0) ? t1::BAND_ORIENT_LL : (t1::eBandOrientation)(bandIndex + 1);
        band->orientation_ = orientation;
        uint8_t numDecomps = (resno == 0) ? (uint8_t)(numres - 1U) : (uint8_t)(numres - resno);
        band->setRect(ResSimple::getBandWindow(numDecomps, band->orientation_, unreducedTileComp));

        /* Table E-1 - Sub-band gains */
        /* BUG_WEIRD_TWO_INVK (look for this identifier in dwt.c): */
        /* the test (!isCompressor_ && l_tccp->qmfbid == 0) is strongly */
        /* linked to the use of two_invK instead of invK */
        const uint32_t log2_gain = (!isCompressor() && tccp->qmfbid_ == 0) ? 0
                                   : (band->orientation_ == 0)             ? 0
                                   : (band->orientation_ == 3)             ? 2
                                                                           : 1;
        uint32_t numbps = imageComp->prec + log2_gain;
        auto offset = (resno == 0) ? 0 : 3 * resno - 2;
        auto step_size = tccp->stepsizes_ + offset + bandIndex;
        band->stepsize_ = (float)(((1.0 + step_size->mant / 2048.0) *
                                   pow(2.0, (int32_t)(numbps - step_size->expn))));
        // printf("res=%u, band=%u, mant=%u,expn=%u, numbps=%u, step size=
        // %f\n",resno,band->orientation,step_size->mant,step_size->expn,numbps,
        // band->stepsize);

        // see Taubman + Marcellin - Equation 10.22
        band->maxBitPlanes_ =
            tccp->roishift_ +
            (uint8_t)std::max<int8_t>(0, int8_t(step_size->expn + tccp->numgbits_ - 1U));
        // assert(band->numbps <= maxBitPlanesJ2K);
      }
      // initialize precincts and code blocks
      if(!res->init(current_plugin_tile_, isCompressor_, tcp_->numLayers_, this, tccp, resno))
      {
        delete[] resolutions;
        return false;
      }
    }

    tilec->init(resolutions, isCompressor(), tcp_->wholeTileDecompress_,
                cp_->codingParams_.dec_.reduce_, tcp_->tccps_ + compno);
  }
  if(state & GRK_PLUGIN_STATE_DEBUG)
  {
    if(!tile_equals(current_plugin_tile_, tile_))
      grklog.warn("plugin tile differs from grok tile");
  }

  initialized_ = true;

  return true;
}

void TileProcessor::setStream(IStream* stream, bool ownsStream)
{
  if(markerParser_)
    markerParser_->setStream(stream, ownsStream);
  else
    stream_ = stream;
}

bool TileProcessor::decompressPrepareWithTLM(const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq)
{
  if(allSOTMarkersParsed())
    return true;

  for(const auto& tp : *tilePartFetchSeq)
  {
    if(tp->stream_)
    {
      setStream(tp->stream_.get(), false);
    }
    else
    {
      // seek to beginning of tile part
      if(!getStream()->seek(tp->offset_) || getStream()->numBytesLeft() == 0)
        break;
    }

    // read SOT marker id
    try
    {
      if(!markerParser_->readSOTorEOC())
        return false;
    }
    catch(const t1_t2::InvalidMarkerException& ime)
    {
      truncated_ = true;
      continue;
    }

    // process SOT marker
    auto [processed, length] = markerParser_->processMarker();
    if(!processed)
      break;

    // read next tile part header marker
    try
    {
      if(!markerParser_->readId(false))
        return false;
    }
    catch(const t1_t2::InvalidMarkerException& ime)
    {
      truncated_ = true;
      continue;
    }

    // parse tile part
    if(!parseTilePart(nullptr, nullptr, markerParser_->currId(), tilePartInfo_))
      return false;

    // sanity check
    uint64_t actualTilePartLength = getStream()->tell() - (tp->stream_ ? 0 : tp->offset_);
    if(actualTilePartLength > tp->length_)
    {
      grklog.error("Tile %u: TLM marker tile part length %u differs from actual"
                   " tile part length %u:\n"
                   "     last sot position: %u, current position : %u.",
                   tileIndex_, tp->length_, actualTilePartLength, tp->offset_, getStream()->tell());
      throw CorruptTLMException();
    }
    else if(actualTilePartLength < tp->length_)
    {
      grklog.warn("Tile %u: TLM marker tile part length %u differs from actual"
                  " tile part length %u:\n"
                  "     last sot position: %u, current position : %u",
                  tileIndex_, tp->length_, actualTilePartLength, tp->offset_, getStream()->tell());
      truncated_ = true;
    }
  }

  tilePartFetchSeq_ = tilePartFetchSeq;

  prepareForDecompression();

  return true;
}

bool TileProcessor::decompressWithTLM(const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq,
                                      CoderPool* coderPool, Rect32 unreducedImageBounds,
                                      std::function<void()> post, TileFutureManager& futures)
{
  if(!decompressPrepareWithTLM(tilePartFetchSeq))
    return false;

  return scheduleT2T1(coderPool, unreducedImageBounds, post, futures);
}

bool TileProcessor::readSOT(IStream* stream, uint8_t* headerData, uint16_t headerSize,
                            TilePartInfo& tilePartInfo, bool needToReadIndexAndLength)
{
  if(headerSize != sotMarkerSegmentLen - MARKER_BYTES_PLUS_MARKER_LENGTH_BYTES)
  {
    grklog.error("Error reading SOT marker");
    return false;
  }
  // we consider it parsed even if there are errors below
  numSOTsParsed_++;
  if(needToReadIndexAndLength)
  {
    uint16_t tileIndex;
    grk_read(&headerData, &tileIndex);
    if(tileIndex != tileIndex_)
    {
      grklog.warn("TLM: marker tile index %u differs from SOT"
                  " tile index %u",
                  tileIndex_, tileIndex);
      return false;
    }
    grk_read(&headerData, &tilePartInfo.tilePartLength_);
  }
  grk_read(&headerData, &tilePartInfo.tilePart_);

  uint8_t numTileParts;
  grk_read(&headerData, &numTileParts);

  if(numTileParts && (tilePartInfo.tilePart_ >= numTileParts))
  {
    grklog.error("Tile %u: Tile part index (%u) must be less than number of tile parts (%u)",
                 tileIndex_, tilePartInfo.tilePart_, numTileParts);
    throw CorruptSOTMarkerException();
  }

  startPos_ = stream->tell() - sotMarkerSegmentLen;
  auto tcp = getTCP();
  Point16 currTile(tileIndex_ % cp_->t_grid_width_, tileIndex_ / cp_->t_grid_width_);

  if(tileIndex_ >= cp_->t_grid_width_ * cp_->t_grid_height_)
  {
    grklog.error("Invalid tile number %u", tileIndex_);
    return false;
  }
  if(!tcp->advanceTilePartCounter(tileIndex_, tilePartInfo.tilePart_))
    return false;

  if(tilePartInfo.tilePartLength_ != sotMarkerSegmentLen)
  {
    /* PSot should be equal to zero, or greater than or equal to sot_marker_segment_len.
     */
    if(tilePartInfo.tilePartLength_ && (tilePartInfo.tilePartLength_ < sotMarkerSegmentLen))
    {
      grklog.error("Illegal Psot value %u", tilePartInfo.tilePartLength_);
      return false;
    }
  }
  // ensure that current tile part number read from SOT marker
  // is not larger than total number of tile parts
  if(tcp->signalledNumTileParts_ && tilePartInfo.tilePart_ >= tcp->signalledNumTileParts_)
  {
    /* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
    grklog.error("Current tile part number (%u) read from SOT marker is greater\n than total "
                 "number of tile-parts (%u).",
                 tilePartInfo.tilePart_, tcp->signalledNumTileParts_);
    return false;
  }

  if(numTileParts)
  { /* Number of tile-part header is provided by this tile-part header */
    /* A.4.2 of 15444-1 : 2002 */

    if(tcp->signalledNumTileParts_)
    {
      if(tilePartInfo.tilePart_ >= tcp->signalledNumTileParts_)
      {
        grklog.error("In SOT marker, TPSot (%u) is not valid with regards to the current "
                     "number of tile-part (%u)",
                     tilePartInfo.tilePart_, tcp->signalledNumTileParts_);
        return false;
      }
      if(numTileParts != tcp->signalledNumTileParts_)
      {
        grklog.warn("Invalid number of tile parts for tile number %u. "
                    "Got %u, expected %u as signalled in previous tile part(s).",
                    tileIndex_, numTileParts, tcp->signalledNumTileParts_);
      }
    }
    if(tilePartInfo.tilePart_ >= numTileParts)
    {
      grklog.error("In SOT marker, TPSot (%u) must be less than number of tile-parts (%u)",
                   tilePartInfo.tilePart_, numTileParts);
      return false;
    }
    tcp->signalledNumTileParts_ = numTileParts;
  }

  /* Ref A.4.2: Psot may equal zero if it is the last tile-part of the code stream.*/
  if(tilePartInfo.tilePartLength_)
  {
    if(tilePartInfo.tilePartLength_ < sotMarkerSegmentLen)
    {
      grklog.error("Tile part data length %u is smaller than marker segment length %u",
                   tilePartInfo.remainingTilePartBytes_, sotMarkerSegmentLen);
      return false;
    }
    tilePartInfo.remainingTilePartBytes_ = tilePartInfo.tilePartLength_ - sotMarkerSegmentLen;
  }
  else
  {
    tilePartInfo.remainingTilePartBytes_ = getStream()->numBytesLeft();
  }
  grklog.debug("Added tile part %d to tile %d", tilePartInfo.tilePart_, tileIndex_);
  if(!tilePartSeq_.push_back(tilePartInfo.tilePart_, numTileParts, startPos_,
                             tilePartInfo.tilePartLength_))
    return false;

  assert(!tcp_->signalledNumTileParts_ || numSOTsParsed_ <= tcp_->signalledNumTileParts_);
  tilePartInfo_ = tilePartInfo;

  return true;
}

bool TileProcessor::parseTilePart(std::vector<std::unique_ptr<MarkerParser>>* parsers,
                                  IStream* bifurcatedStream, uint16_t mainMarkerId,
                                  TilePartInfo tilePartInfo)
{
  bool concurrent = parsers && bifurcatedStream;
  std::shared_ptr<IStream> streamGuard;
  std::shared_ptr<bool> owned_by_parser;
  if(concurrent)
  {
    owned_by_parser = std::make_shared<bool>(false);
    streamGuard = std::shared_ptr<IStream>(bifurcatedStream, [owned_by_parser](IStream* p) {
      if(!*owned_by_parser)
        delete p;
    });
  }
  auto parseHeader = [this, parsers, bifurcatedStream, concurrent, mainMarkerId, tilePartInfo,
                      streamGuard, owned_by_parser]() {
    auto tpi = tilePartInfo;
    auto parser = markerParser_;
    auto id = ExecSingleton::workerId();
    threadTilePart_[id] = tpi.tilePart_;
    if(concurrent)
    {
      parser = (*parsers)[id].get();
      setProcessors(parser);
      parser->setStream(bifurcatedStream, true);
      *owned_by_parser = true;
    }
    parser->synch(mainMarkerId);

    // 1. read tile markers from stream until SOD or EOC
    auto stream = parser->getStream();
    while(parser->currId() != SOD)
    {
      assert(parser->currId() != SOT);
      if(stream->numBytesLeft() == 0)
      {
        success_ = false;
        return;
      }
      try
      {
        auto [processed, markerBodyLength] = parser->processMarker();
        if(!processed)
        {
          success_ = false;
          return;
        }
        if(tpi.remainingTilePartBytes_)
        {
          auto segmentLength = (uint32_t)(markerBodyLength + MARKER_BYTES);
          if(tpi.remainingTilePartBytes_ > 0 && tpi.remainingTilePartBytes_ < segmentLength)
          {
            grklog.error("Tile part data length %u smaller than marker segment length %u",
                         tpi.remainingTilePartBytes_, segmentLength);
            success_ = false;
            return;
          }
          tpi.remainingTilePartBytes_ -= segmentLength;
        }
        if(!parser->readId(false))
        {
          success_ = false;
          return;
        }
      }
      catch(const CorruptSOTMarkerException& csme)
      {
        success_ = false;
        return;
      }
      catch(const t1_t2::InvalidMarkerException& ime)
      {
        success_ = false;
        return;
      }
    }
    assert(parser->currId() == SOD);

    // 2. cache tile parts
    // note: we subtract MARKER_BYTES to account for SOD marker
    if(tpi.remainingTilePartBytes_ >= MARKER_BYTES)
      tpi.remainingTilePartBytes_ -= MARKER_BYTES;
    else
      // illegal tile part data length of 1, but we will allow it
      tpi.remainingTilePartBytes_ = 0;

    if(!tpi.remainingTilePartBytes_)
    {
      return;
    }

    auto bytesLeftInStream = stream->numBytesLeft();
    if(bytesLeftInStream == 0)
    {
      grklog.error("Tile %u, tile part %u: stream has been truncated and "
                   "there is no tile data available",
                   tileIndex_, tcp_->tilePartCounter_ + 1);
      success_ = false;
      return;
    }
    // check that there are enough bytes in stream to fill tile data
    if(tpi.remainingTilePartBytes_ > bytesLeftInStream)
    {
      grklog.warn("Tile part length %lld greater than "
                  "stream length %lld\n"
                  "(tile: %u, tile part: %u). Tile has been truncated.",
                  tpi.remainingTilePartBytes_, stream->numBytesLeft(), tileIndex_,
                  tcp_->tilePartCounter_ + 1);

      // sanitize tilePartInfo.remainingTilePartBytes_
      tpi.remainingTilePartBytes_ = bytesLeftInStream <= UINT_MAX ? (uint32_t)bytesLeftInStream : 0;
      std::atomic_ref<bool> ref(truncated_);
      ref = true;
    }
    // now cache the packets
    uint8_t* buff = nullptr;
    auto zeroCopy = stream->supportsZeroCopy();
    if(!zeroCopy)
    {
      try
      {
        buff = new uint8_t[tpi.remainingTilePartBytes_];
      }
      catch([[maybe_unused]] const std::bad_alloc& ex)
      {
        grklog.error("Not enough memory to allocate segment");
        success_ = false;
        return;
      }
    }
    stream->read(buff, &buff, tpi.remainingTilePartBytes_);
    tcp_->packets_->push(tpi.tilePart_, buff, tpi.remainingTilePartBytes_, !zeroCopy);
  };
  if(concurrent)
  {
    prepareConcurrentParsing();
    tileHeaderParseFlow_->nextTask().work(parseHeader);
  }
  else
  {
    parseHeader();
  }

  return success_;
}

void TileProcessor::setTruncated(void)
{
  if(numSOTsParsed_ != tcp_->signalledNumTileParts_)
    truncated_ = true;
}

bool TileProcessor::allSOTMarkersParsed(void)
{
  return truncated_ || (numSOTsParsed_ == tcp_->signalledNumTileParts_);
}

void TileProcessor::prepareConcurrentParsing(void)
{
  if(!tileHeaderParseFlow_)
    tileHeaderParseFlow_ = std::make_unique<FlowComponent>();
  if(!prepareFlow_)
    prepareFlow_ = std::make_unique<FlowComponent>();
}

void TileProcessor::prepareForDecompression(void)
{
  auto prep = [this]() {
    // now we can get ready to decompress this tile
    if(!tcp_->validateQuantization())
      return;

    if(!tcp_->mergePpt())
    {
      grklog.error("Failed to merge PPT data");
      return;
    }
    if(!init())
    {
      grklog.error("Cannot decompress tile %u", tileIndex_);
      return;
    }

    tcp_->finalizePocs();
  };

  if(prepareFlow_)
    prepareFlow_->nextTask().work(prep);
  else
    prep();
}

Mct* TileProcessor::getMCT(void)
{
  return mct_;
}

void TileProcessor::release(uint32_t strategy)
{
  if((strategy & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL)
    return;

  // delete image in absence of tile cache strategy
  if(strategy == GRK_TILE_CACHE_NONE)
  {
    grk_unref(image_);
    image_ = nullptr;
  }

  // delete tile components
  delete tile_;
  tile_ = nullptr;

  if(tilePartFetchSeq_ && strategy != GRK_TILE_CACHE_ALL)
  {
    for(const auto& tpfs : *tilePartFetchSeq_)
    {
      tpfs->data_.release();
      tpfs->stream_.release();
    }
  }
}
void TileProcessor::release(void)
{
  release(tileCacheStrategy_);
}
void TileProcessor::deallocBuffers()
{
  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto tile_comp = tile_->comps_ + compno;
    tile_comp->dealloc();
  }
}

bool TileProcessor::differentialUpdate(Rect32 unreducedImageBounds)
{
  tcp_->updateLayersToDecompress();
  for(uint16_t i = 0; i < tile_->numcomps_; ++i)
  {
    auto tilec = tile_->comps_ + i;
    tilec->update(cp_->codingParams_.dec_.reduce_);
  }

  unreducedImageWindow_ = unreducedImageBounds;
  return createDecompressTileComponentWindows();
}

bool TileProcessor::readPLT(uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  auto cp = getCodingParams();
  auto tilePart = threadTilePart_[ExecSingleton::workerId()];
  bool rc;
  {
    std::lock_guard<std::mutex> lock(pltMutex_);
    rc = getPacketLengthCache()->createMarkers(nullptr)->readPLT(headerData, headerSize, tilePart);
    if(rc && (cp->codingParams_.dec_.disableRandomAccessFlags_ & GRK_RANDOM_ACCESS_PLT) != 0)
      getPacketLengthCache()->getMarkers()->disable();
  }
  return rc;
}

bool TileProcessor::createDecompressTileComponentWindows(void)
{
  if(!initialized_)
    return false;
  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto imageComp = headerImage_->comps + compno;
    if(imageComp->dx == 0 || imageComp->dy == 0)
      return false;
    auto tileComp = tile_->comps_ + compno;
    auto unreducedImageCompWindow =
        unreducedImageWindow_.scaleDownCeil(imageComp->dx, imageComp->dy);
    if(!tileComp->canCreateWindow(unreducedImageCompWindow))
      return false;
    tileComp->createWindow(unreducedImageCompWindow);
  }

  return true;
}

bool TileProcessor::hasError(void)
{
  return !success_;
}

grk_plugin_tile* TileProcessor::getCurrentPluginTile(void) const
{
  return current_plugin_tile_;
}
void TileProcessor::setCurrentPluginTile(grk_plugin_tile* tile)
{
  current_plugin_tile_ = tile;
}
uint64_t TileProcessor::getNumProcessedPackets(void)
{
  return numProcessedPackets_;
}
void TileProcessor::incNumProcessedPackets(void)
{
  numProcessedPackets_++;
}
void TileProcessor::incNumProcessedPackets(uint64_t numPackets)
{
  numProcessedPackets_ += numPackets;
}

CodingParams* TileProcessor::getCodingParams(void)
{
  return cp_;
}

GrkImage* TileProcessor::getHeaderImage(void)
{
  return headerImage_;
}

TileCodingParams* TileProcessor::getTCP(void)
{
  return tcp_;
}

std::shared_ptr<PacketLengthCache<uint32_t>> TileProcessor::getPacketLengthCache()
{
  return packetLengthCache_;
}

uint32_t TileProcessor::getTileCacheStrategy(void)
{
  return tileCacheStrategy_;
}

IStream* TileProcessor::getStream(void)
{
  return markerParser_ ? markerParser_->getStream() : stream_;
}
uint16_t TileProcessor::getIndex(void) const
{
  return tileIndex_;
}
void TileProcessor::incrementIndex(void)
{
  tileIndex_++;
}
Tile* TileProcessor::getTile(void)
{
  return tile_;
}
CodecScheduler* TileProcessor::getScheduler(void)
{
  return scheduler_;
}
bool TileProcessor::isCompressor(void)
{
  return isCompressor_;
}
GrkImage* TileProcessor::getImage(void)
{
  return image_;
}
void TileProcessor::setImage(GrkImage* img)
{
  if(img != image_)
  {
    grk_unref(image_);
    image_ = img;
  }
}

bool TileProcessor::doPostT1(void)
{
  return !current_plugin_tile_ || (current_plugin_tile_->decompress_flags & GRK_DECODE_POST_T1);
}

void TileProcessor::post_decompressT2T1(GrkImage* scratch)
{
  if(this->doPostT1())
  {
    if(scratch->has_multiple_tiles)
    {
      grk_unref(image_);
      image_ = scratch->extractFrom(tile_);
    }
    else
    {
      // dispense with image_ when there is only one tile
      scratch->transferDataFrom(tile_);
    }
    deallocBuffers();
  }
}

bool TileProcessor::scheduleT2T1(CoderPool* coderPool, Rect32 unreducedImageBounds,
                                 std::function<void()> post, TileFutureManager& futures)
{
  unreducedImageWindow_ = unreducedImageBounds;

  if(!scheduler_)
  {
    if(Scheduling::isWindowedScheduling())
      scheduler_ = new DecompressWindowScheduler(headerImage_->numcomps, headerImage_->comps->prec,
                                                 coderPool);
    else
      scheduler_ =
          new DecompressScheduler(headerImage_->numcomps, headerImage_->comps->prec, coderPool);
  }
  else
  {
    scheduler_->release();
  }

  bool doT2 = !current_plugin_tile_ || (current_plugin_tile_->decompress_flags & GRK_DECODE_T2);

  auto allocAndSchedule = [this]() {
    if(!Scheduling::isWindowedScheduling())
    {
      for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
      {
        auto tilec = tile_->comps_ + compno;
        if(!tcp_->wholeTileDecompress_)
        {
          try
          {
            tilec->allocRegionWindow(tilec->nextPacketProgressionState_.numResolutionsRead(),
                                     truncated_);
          }
          catch([[maybe_unused]] const std::runtime_error& ex)
          {
            continue;
          }
          catch([[maybe_unused]] const std::bad_alloc& baex)
          {
            success_ = false;
            return;
          }
        }
        if(!tilec->getWindow()->alloc())
        {
          grklog.error("Not enough memory for tile data");
          success_ = false;
          return;
        }
      }
    }
    if(!scheduler_->schedule(this))
    {
      success_ = false;
      return;
    }
  };

  auto t2Parse = [this]() {
    // synch plugin with T2 data
    // todo re-enable decompress synch
    // decompress_synch_plugin_with_host(this);

    if(tcp_->packets_->empty())
    {
      success_ = false;
      return;
    }

    tcp_->packets_->rewind();
    packetLengthCache_->rewind();
    numProcessedPackets_ = 0;
    numReadDataPackets_ = 0;
    getStream()->memAdvise(startPos_, tilePartInfo_.tilePartLength_,
                           GrkAccessPattern::ACCESS_SEQUENTIAL);
    for(uint16_t compno = 0; compno < headerImage_->numcomps; ++compno)
    {
      auto tilec = tile_->comps_ + compno;
      for(uint8_t resno = 0; resno < tilec->resolutions_to_decompress_; ++resno)
      {
        auto res = tilec->resolutions_ + resno;
        res->packetParser_->clearPrecinctParsers();
      }
    }

    if(!createDecompressTileComponentWindows())
    {
      success_ = false;
      return;
    }

    auto t2 = std::make_unique<T2Decompress>(this);
    truncated_ = t2->parsePackets(tileIndex_, tcp_->packets_);

    // 1. count parsers
    auto tile = getTile();
    uint64_t parserCount = 0;
    for(uint16_t compno = 0; compno < headerImage_->numcomps; ++compno)
    {
      auto tilec = tile->comps_ + compno;
      for(uint8_t resno = 0; resno < tilec->resolutions_to_decompress_; ++resno)
      {
        auto res = tilec->resolutions_ + resno;
        parserCount += res->packetParser_->allLayerPrecinctParsers_.size();
      }
    }
    // 2.create and populate tasks, and execute
    if(parserCount)
    {
      for(uint16_t compno = 0; compno < headerImage_->numcomps; ++compno)
      {
        auto tilec = tile->comps_ + compno;
        for(uint8_t resno = 0; resno < tilec->resolutions_to_decompress_; ++resno)
        {
          auto res = tilec->resolutions_ + resno;
          for(const auto& pp : res->packetParser_->allLayerPrecinctParsers_)
          {
            const auto& ppair = pp;
            auto t2Decompressor = [&ppair]() {
              auto parser = ppair.second->parserQueue_.pop();
              while(parser != std::nullopt)
              {
                T2Decompress::parsePacketData(parser.value());
                parser = ppair.second->parserQueue_.pop();
              }
            };
            t2Decompressor();
          }
        }
      }
    }
  };

  if(doT2)
  {
    if(ExecSingleton::num_threads() > 1)
    {
      if(!t2ParseFlow_)
        t2ParseFlow_ = std::make_unique<FlowComponent>();
      else
        t2ParseFlow_->clear();
      t2ParseFlow_->nextTask().work(t2Parse);

      if(!allocAndScheduleFlow_)
        allocAndScheduleFlow_ = std::make_unique<FlowComponent>();
      else
        allocAndScheduleFlow_->clear();
      allocAndScheduleFlow_->nextTask().work(allocAndSchedule);
    }
    else
    {
      t2Parse();
      allocAndSchedule();
    }
  }

  if(ExecSingleton::num_threads() > 1)
  {
    if(!rootFlow_)
      rootFlow_ = new FlowComponent();
    else
      rootFlow_->clear();

    std::function<int()> condition_lambda = [this]() -> int { return hasError() ? 1 : 0; };

    scheduler_->addTo(*rootFlow_);

    allocAndScheduleFlow_->addTo(*rootFlow_);
    allocAndScheduleFlow_->conditional_precede(rootFlow_, scheduler_, condition_lambda);

    t2ParseFlow_->addTo(*rootFlow_);
    t2ParseFlow_->conditional_precede(rootFlow_, allocAndScheduleFlow_.get(), condition_lambda);

    if(tileHeaderParseFlow_ && prepareFlow_)
    {
      prepareFlow_->addTo(*rootFlow_);
      prepareFlow_->conditional_precede(rootFlow_, t2ParseFlow_.get(), condition_lambda);

      tileHeaderParseFlow_->addTo(*rootFlow_);
      tileHeaderParseFlow_->conditional_precede(rootFlow_, prepareFlow_.get(), condition_lambda);
    }

    if(!postDecompressFlow_)
      postDecompressFlow_ = new FlowComponent();
    else
      postDecompressFlow_->clear();
    postDecompressFlow_->nextTask().work(post);
    postDecompressFlow_->addTo(*rootFlow_);

    scheduler_->precede(*postDecompressFlow_);
    futures.add(tileIndex_, ExecSingleton::get().run(*rootFlow_));
  }
  else
  {
    post();
  }
  return true;
}

uint8_t TileProcessor::getMaxNumDecompressResolutions(void)
{
  uint8_t rc = 0;
  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto tccp = tcp_->tccps_ + compno;
    auto numresolutions = tccp->numresolutions_;
    uint8_t resToDecomp =
        (numresolutions < cp_->codingParams_.dec_.reduce_)
            ? 1
            : static_cast<uint8_t>(numresolutions - cp_->codingParams_.dec_.reduce_);

    rc = std::max<uint8_t>(rc, resToDecomp);
  }
  return rc;
}

Rect32 TileProcessor::getUnreducedTileWindow(void)
{
  return unreducedImageWindow_.clip(tile_);
}

uint64_t TileProcessor::getNumReadDataPackets(void)
{
  return numReadDataPackets_;
}
void TileProcessor::incNumReadDataPackets(void)
{
  numReadDataPackets_++;
}

bool TileProcessor::needsMctDecompress(void)
{
  if(!tcp_->mct_)
    return false;
  if(tile_->numcomps_ < 3)
  {
    grklog.warn("Number of components (%u) is less than 3 - skipping MCT.", tile_->numcomps_);
    return false;
  }
  if(!headerImage_->componentsEqual(3, false))
  {
    grklog.warn("Not all tiles components have the same dimensions - skipping MCT.");
    return false;
  }
  if(tcp_->mct_ == 2 && !tcp_->mctDecodingMatrix_)
    return false;

  return true;
}
bool TileProcessor::needsMctDecompress(uint16_t compno)
{
  return needsMctDecompress() && (compno <= 2);
}

} // namespace grk
