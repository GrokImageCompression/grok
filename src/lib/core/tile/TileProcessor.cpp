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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include "grk_includes.h"
namespace grk
{
TileProcessor::TileProcessor(uint16_t tileIndex, CodeStream* codeStream, BufferedStream* stream,
							 bool isCompressor, StripCache* stripCache)
	: first_poc_tile_part_(true), tilePartCounter_(0), pino(0),
	  headerImage(codeStream->getHeaderImage()),
	  current_plugin_tile(codeStream->getCurrentPluginTile()), cp_(codeStream->getCodingParams()),
	  packetLengthCache(PLCache()), tile(new Tile(headerImage->numcomps)), scheduler_(nullptr),
	  numProcessedPackets(0), numDecompressedPackets(0), tilePartDataLength(0),
	  tileIndex_(tileIndex), stream_(stream), corrupt_packet_(false),
	  newTilePartProgressionPosition(cp_->coding_params_.enc_.newTilePartProgressionPosition),
	  tcp_(cp_->tcps + tileIndex_), truncated(false), image_(nullptr), isCompressor_(isCompressor),
	  preCalculatedTileLen(0), mct_(new mct(tile, headerImage, tcp_, stripCache))
{}
TileProcessor::~TileProcessor()
{
   release(GRK_TILE_CACHE_NONE);
   delete scheduler_;
   delete mct_;
}
uint64_t TileProcessor::getTilePartDataLength(void)
{
   return tilePartDataLength;
}
bool TileProcessor::subtractMarkerSegmentLength(uint16_t markerLen)
{
   if(tilePartDataLength == 0)
	  return true;

   uint32_t segmentLength = (uint32_t)(markerLen + MARKER_LENGTH_BYTES);
   if(tilePartDataLength > 0 && tilePartDataLength < segmentLength)
   {
	  Logger::logger_.error("Tile part data length %u smaller than marker segment length %u",
							tilePartDataLength, markerLen);
	  return false;
   }
   tilePartDataLength -= (uint64_t)segmentLength;

   return true;
}
bool TileProcessor::setTilePartDataLength(uint16_t tilePart, uint32_t tilePartLength,
										  bool lastTilePartInCodeStream)
{
   if(!lastTilePartInCodeStream)
   {
	  if(tilePartLength < sot_marker_segment_len_minus_tile_data_len)
	  {
		 Logger::logger_.error(
			 "Tile part data length %u is smaller than for marker segment length %u",
			 tilePartDataLength, sot_marker_segment_len_minus_tile_data_len);
		 return false;
	  }
	  tilePartDataLength = tilePartLength - sot_marker_segment_len_minus_tile_data_len;
	  // handle some edge cases
	  if(tilePartDataLength < 2)
	  {
		 if(tilePartDataLength == 1)
		 {
			Logger::logger_.warn(
				"Tile %u: tile part %u data length %u is smaller than minimum size of 2 - "
				"room for single SOD marker. Ignoring.",
				getIndex(), tilePart, tilePartDataLength);
			tilePartDataLength = 0;
		 }
		 else
		 {
			// some non-compliant images do not add 2 bytes for SOD marker
			// for an empty tile part
			tilePartDataLength = 2;
		 }
	  }
   }
   else
   {
	  tilePartDataLength = stream_->numBytesLeft();
   }

   return true;
}
uint64_t TileProcessor::getNumProcessedPackets(void)
{
   return numProcessedPackets;
}
void TileProcessor::incNumProcessedPackets(void)
{
   numProcessedPackets++;
}
void TileProcessor::incNumProcessedPackets(uint64_t numPackets)
{
   numProcessedPackets += numPackets;
}
uint64_t TileProcessor::getNumDecompressedPackets(void)
{
   return numDecompressedPackets;
}
void TileProcessor::incNumDecompressedPackets(void)
{
   numDecompressedPackets++;
}
BufferedStream* TileProcessor::getStream(void)
{
   return stream_;
}
uint32_t TileProcessor::getPreCalculatedTileLen(void)
{
   return preCalculatedTileLen;
}
bool TileProcessor::canPreCalculateTileLen(void)
{
   return !cp_->coding_params_.enc_.enableTilePartGeneration_ &&
		  (cp_->tcps + tileIndex_)->getNumProgressions() == 1;
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
   return tile;
}
Scheduler* TileProcessor::getScheduler(void)
{
   return scheduler_;
}
bool TileProcessor::isCompressor(void)
{
   return isCompressor_;
}
void TileProcessor::generateImage(GrkImage* src_image, Tile* src_tile)
{
   if(image_)
	  grk_object_unref(&image_->obj);
   image_ = src_image->duplicate(src_tile);
}
GrkImage* TileProcessor::getImage(void)
{
   return image_;
}
void TileProcessor::release(GRK_TILE_CACHE_STRATEGY strategy)
{
   // delete image in absence of tile cache strategy
   if(strategy == GRK_TILE_CACHE_NONE)
   {
	  if(image_)
		 grk_object_unref(&image_->obj);
	  image_ = nullptr;
   }

   // delete tile components
   delete tile;
   tile = nullptr;
}
PacketTracker* TileProcessor::getPacketTracker(void)
{
   return &packetTracker_;
}
TileCodingParams* TileProcessor::getTileCodingParams(void)
{
   return cp_->tcps + tileIndex_;
}
uint8_t TileProcessor::getMaxNumDecompressResolutions(void)
{
   uint8_t rc = 0;
   auto tcp = cp_->tcps + tileIndex_;
   for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
   {
	  auto tccp = tcp->tccps + compno;
	  auto numresolutions = tccp->numresolutions;
	  uint8_t resToDecomp;
	  if(numresolutions < cp_->coding_params_.dec_.reduce_)
		 resToDecomp = 1;
	  else
		 resToDecomp = (uint8_t)(numresolutions - cp_->coding_params_.dec_.reduce_);
	  rc = std::max<uint8_t>(rc, resToDecomp);
   }
   return rc;
}
bool TileProcessor::init(void)
{
   uint32_t state = grk_plugin_get_debug_state();
   auto tcp = &(cp_->tcps[tileIndex_]);

   if(tcp->compressedTileData_)
	  tcp->compressedTileData_->rewind();

   // generate tile bounds from tile grid coordinates
   uint32_t tile_x = tileIndex_ % cp_->t_grid_width;
   uint32_t tile_y = tileIndex_ / cp_->t_grid_width;
   *((grk_rect32*)tile) = cp_->getTileBounds(headerImage, tile_x, tile_y);

   if(tcp->tccps->numresolutions == 0)
   {
	  Logger::logger_.error("tiles require at least one resolution");
	  return false;
   }

   for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
   {
	  auto imageComp = headerImage->comps + compno;
	  /*fprintf(stderr, "compno = %u/%u\n", compno, tile->numcomps);*/
	  if(imageComp->dx == 0 || imageComp->dy == 0)
		 return false;
	  auto tilec = tile->comps + compno;
	  grk_rect32 unreducedTileComp = grk_rect32(
		  ceildiv<uint32_t>(tile->x0, imageComp->dx), ceildiv<uint32_t>(tile->y0, imageComp->dy),
		  ceildiv<uint32_t>(tile->x1, imageComp->dx), ceildiv<uint32_t>(tile->y1, imageComp->dy));
	  if(!tilec->init(this, unreducedTileComp, imageComp->prec, tcp->tccps + compno))
	  {
		 return false;
	  }
   }

   // decompressor plugin debug sanity check on tile struct
   if(!isCompressor_)
   {
	  if(state & GRK_PLUGIN_STATE_DEBUG)
	  {
		 if(!tile_equals(current_plugin_tile, tile))
			Logger::logger_.warn("plugin tile differs from grok tile", nullptr);
	  }
   }
   numProcessedPackets = 0;

   if(isCompressor_)
   {
	  uint64_t max_precincts = 0;
	  for(uint16_t compno = 0; compno < headerImage->numcomps; ++compno)
	  {
		 auto tilec = tile->comps + compno;
		 for(uint32_t resno = 0; resno < tilec->numresolutions; ++resno)
		 {
			auto res = tilec->resolutions_ + resno;
			max_precincts = (std::max<uint64_t>)(max_precincts, (uint64_t)res->precinctGridWidth *
																	res->precinctGridHeight);
		 }
	  }
	  packetTracker_.init(tile->numcomps_, tile->comps->numresolutions, max_precincts,
						  tcp->max_layers_);
   }

   return true;
}
bool TileProcessor::createWindowBuffers(const GrkImage* outputImage)
{
   for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
   {
	  auto imageComp = headerImage->comps + compno;
	  if(imageComp->dx == 0 || imageComp->dy == 0)
		 return false;
	  auto tileComp = tile->comps + compno;
	  grk_rect32 unreducedImageCompWindow;
	  if(isCompressor_)
	  {
		 if(!tileComp->canCreateWindow(tileComp))
			return false;
		 unreducedImageCompWindow = tileComp;
	  }
	  else
	  {
		 unreducedImageWindow =
			 grk_rect32(outputImage->x0, outputImage->y0, outputImage->x1, outputImage->y1);
		 unreducedImageCompWindow =
			 unreducedImageWindow.scaleDownCeil(imageComp->dx, imageComp->dy);
		 if(!tileComp->canCreateWindow(unreducedImageCompWindow))
			return false;
	  }
	  tileComp->createWindow(unreducedImageCompWindow);
   }

   return true;
}

grk_rect32 TileProcessor::getUnreducedTileWindow(void)
{
   return unreducedImageWindow.clip(tile);
}

void TileProcessor::deallocBuffers()
{
   for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
   {
	  auto tile_comp = tile->comps + compno;
	  tile_comp->dealloc();
   }
}
bool TileProcessor::doCompress(void)
{
   uint32_t state = grk_plugin_get_debug_state();
#ifdef PLUGIN_DEBUG_ENCODE
   if(state & GRK_PLUGIN_STATE_DEBUG)
	  set_context_stream(this);
#endif

   tcp_ = &cp_->tcps[tileIndex_];

   // When debugging the compressor, we do all of T1 up to and including DWT
   // in the plugin, and pass this in as image data.
   // This way, both Grok and plugin start with same inputs for
   // context formation and MQ coding.
   bool debugEncode = state & GRK_PLUGIN_STATE_DEBUG;
   bool debugMCT = (state & GRK_PLUGIN_STATE_MCT_ONLY) ? true : false;

   if(!current_plugin_tile || debugEncode)
   {
	  if(!debugEncode)
	  {
		 if(!dcLevelShiftCompress())
			return false;
		 if(!mct_encode())
			return false;
	  }
	  if(!debugEncode || debugMCT)
	  {
		 if(!dwt_encode())
			return false;
	  }
	  t1_encode();
   }
   // 1. create PLT marker if required
   packetLengthCache.deleteMarkers();
   if(cp_->coding_params_.enc_.writePLT)
	  packetLengthCache.createMarkers(stream_);
   // 2. rate control
   uint32_t allPacketBytes = 0;
   bool rc = rateAllocate(&allPacketBytes, false);
   if(!rc)
   {
	  Logger::logger_.warn("Unable to perform rate control on tile %d", tileIndex_);
	  Logger::logger_.warn("Rate control will be disabled for this tile");
	  allPacketBytes = 0;
	  rc = rateAllocate(&allPacketBytes, true);
	  if(!rc)
	  {
		 Logger::logger_.error("Unable to perform rate control on tile %d", tileIndex_);
		 return false;
	  }
   }
   packetTracker_.clear();

   if(canPreCalculateTileLen())
   {
	  // SOT marker
	  preCalculatedTileLen = sot_marker_segment_len_minus_tile_data_len;
	  // POC marker
	  if(canWritePocMarker())
	  {
		 uint32_t pocSize =
			 CodeStreamCompress::getPocSize(tile->numcomps_, tcp_->getNumProgressions());
		 preCalculatedTileLen += pocSize;
	  }
	  // calculate PLT marker length
	  if(packetLengthCache.getMarkers())
		 preCalculatedTileLen += packetLengthCache.getMarkers()->getTotalBytesWritten();

	  // calculate SOD marker length
	  preCalculatedTileLen += 2;
	  // calculate packets length
	  preCalculatedTileLen += allPacketBytes;
   }
   return true;
}
bool TileProcessor::canWritePocMarker(void)
{
   bool firstTilePart = (tilePartCounter_ == 0);

   // note: DCP standard does not allow POC marker
   return cp_->tcps[tileIndex_].hasPoc() && firstTilePart && !GRK_IS_CINEMA(cp_->rsiz);
}
bool TileProcessor::writeTilePartT2(uint32_t* tileBytesWritten)
{
   // write entire PLT marker in first tile part header
   if(tilePartCounter_ == 0 && packetLengthCache.getMarkers())
   {
	  if(!packetLengthCache.getMarkers()->write())
		 return false;
	  *tileBytesWritten += packetLengthCache.getMarkers()->getTotalBytesWritten();
   }

   // write SOD
   if(!stream_->writeShort(J2K_MS_SOD))
	  return false;
   *tileBytesWritten += 2;

   // write tile packets
   return encodeT2(tileBytesWritten);
}
/** Returns whether a tile component should be fully decompressed,
 * taking into account win_* members.
 *
 * @param compno Component number
 * @return true if the tile component should be fully decompressed
 */
bool TileProcessor::isWholeTileDecompress(uint16_t compno)
{
   auto tilec = tile->comps + compno;
   /* Compute the intersection of the area of interest, expressed in tile component coordinates, */
   /* with the tile bounds */
   auto dims = tilec->getWindow()->bounds().intersection(tilec);

   uint32_t shift = (uint32_t)(tilec->numresolutions - tilec->numResolutionsToDecompress);
   /* Tolerate small margin within the reduced resolution factor to consider if */
   /* the whole tile path must be taken */
   return (dims.valid() &&
		   (shift >= 32 ||
			(((dims.x0 - tilec->x0) >> shift) == 0 && ((dims.y0 - tilec->y0) >> shift) == 0 &&
			 ((tilec->x1 - dims.x1) >> shift) == 0 && ((tilec->y1 - dims.y1) >> shift) == 0)));
}

bool TileProcessor::decompressT2T1(GrkImage* outputImage)
{
   auto tcp = getTileCodingParams();
   if(!tcp->compressedTileData_)
   {
	  Logger::logger_.error("Decompress: Tile %u has no compressed data", getIndex());
	  return false;
   }
   bool doT1 = !current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_T1);
   bool doPostT1 =
	   !current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);

   // create window buffers
   // (no buffer allocation)
   if(!createWindowBuffers(outputImage))
	  return false;

   // T2
   // optimization for regions that are close to largest decompressed resolution
   for(uint16_t compno = 0; compno < headerImage->numcomps; compno++)
   {
	  if(!isWholeTileDecompress(compno))
	  {
		 cp_->wholeTileDecompress_ = false;
		 break;
	  }
   }
   bool doT2 = !current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_T2);
   if(doT2)
   {
	  auto t2 = std::make_unique<T2Decompress>(this);
	  t2->decompressPackets(tileIndex_, tcp->compressedTileData_, &truncated);
	  // synch plugin with T2 data
	  // todo re-enable decompress synch
	  // decompress_synch_plugin_with_host(this);

	  // 1. count parsers
	  auto tile = getTile();
	  uint64_t parserCount = 0;
	  for(uint16_t compno = 0; compno < headerImage->numcomps; ++compno)
	  {
		 auto tilec = tile->comps + compno;
		 for(uint8_t resno = 0; resno < tilec->numResolutionsToDecompress; ++resno)
		 {
			auto res = tilec->resolutions_ + resno;
			parserCount += res->parserMap_->precinctParsers_.size();
		 }
	  }
	  // 2.create and populate tasks, and execute
	  if(parserCount)
	  {
		 auto numThreads = std::min<size_t>(ExecSingleton::get().num_workers(), parserCount);
		 if(numThreads == 1)
		 {
			for(uint16_t compno = 0; compno < headerImage->numcomps; ++compno)
			{
			   auto tilec = tile->comps + compno;
			   for(uint8_t resno = 0; resno < tilec->numResolutionsToDecompress; ++resno)
			   {
				  auto res = tilec->resolutions_ + resno;
				  for(const auto& pp : res->parserMap_->precinctParsers_)
				  {
					 for(uint64_t j = 0; j < pp.second->numParsers_; ++j)
					 {
						try
						{
						   auto parser = pp.second->parsers_[j];
						   parser->readHeader();
						   parser->readData();
						}
						catch([[maybe_unused]] const std::exception& ex)
						{
						   break;
						}
					 }
				  }
			   }
			}
		 }
		 else
		 {
			tf::Taskflow taskflow;
			auto numTasks = parserCount;
			auto tasks = new tf::Task[numTasks];
			for(uint64_t i = 0; i < numTasks; i++)
			   tasks[i] = taskflow.placeholder();
			uint64_t i = 0;
			for(uint16_t compno = 0; compno < headerImage->numcomps; ++compno)
			{
			   auto tilec = tile->comps + compno;
			   for(uint8_t resno = 0; resno < tilec->numResolutionsToDecompress; ++resno)
			   {
				  auto res = tilec->resolutions_ + resno;
				  for(const auto& pp : res->parserMap_->precinctParsers_)
				  {
					 const auto& ppair = pp;
					 auto decompressor = [ppair]() {
						for(uint64_t j = 0; j < ppair.second->numParsers_; ++j)
						{
						   try
						   {
							  auto parser = ppair.second->parsers_[j];
							  parser->readHeader();
							  parser->readData();
						   }
						   catch([[maybe_unused]] const std::exception& ex)
						   {
							  break;
						   }
						}
					 };
					 tasks[i++].work(decompressor);
				  }
			   }
			}
			ExecSingleton::get().run(taskflow).wait();
			delete[] tasks;
		 }
	  }
   }
   // T1
   if(doT1)
   {
	  scheduler_ = new DecompressScheduler(this, tile, tcp_, headerImage->comps->prec);
	  FlowComponent* mctPostProc = nullptr;
	  // schedule MCT post processing
	  if(doPostT1 && needsMctDecompress())
		 mctPostProc = scheduler_->getPrePostProc();
	  uint16_t mctComponentCount = 0;

	  for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
	  {
		 auto tilec = tile->comps + compno;
		 if(!cp_->wholeTileDecompress_)
		 {
			try
			{
			   tilec->allocRegionWindow(tilec->highestResolutionDecompressed + 1U, truncated);
			}
			catch([[maybe_unused]] const std::runtime_error& ex)
			{
			   continue;
			}
			catch([[maybe_unused]] const std::bad_alloc& baex)
			{
			   return false;
			}
		 }
		 if(!tilec->getWindow()->alloc())
		 {
			Logger::logger_.error("Not enough memory for tile data");
			return false;
		 }
		 if(!scheduler_->schedule(compno))
			return false;

		 // post processing
		 auto compFlow = scheduler_->getImageComponentFlow(compno);
		 if(compFlow)
		 {
			if(mctPostProc && compno < 3)
			{
			   // link to MCT
			   compFlow->getFinalFlowT1()->precede(mctPostProc);
			   mctComponentCount++;
			}
			else if(doPostT1)
			{
			   // use with either custom MCT, or no MCT
			   if(!needsMctDecompress(compno) || tcp_->mct == 2)
			   {
				  auto dcPostProc = compFlow->getPrePostProc(scheduler_->getCodecFlow());
				  compFlow->getFinalFlowT1()->precede(dcPostProc);
				  if((tcp_->tccps + compno)->qmfbid == 1)
					 mct_->decompress_dc_shift_rev(dcPostProc, compno);
				  else
					 mct_->decompress_dc_shift_irrev(dcPostProc, compno);
			   }
			}
		 }
	  }
	  // sanity check on MCT scheduling
	  if(doPostT1 && mctComponentCount == 3 && mctPostProc && !mctDecompress(mctPostProc))
		 return false;
	  if(!scheduler_->run())
		 return false;
	  delete scheduler_;
	  scheduler_ = nullptr;
   }
   // 4. post T1
   bool doPost =
	   !current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);
   if(doPost)
   {
	  if(outputImage->hasMultipleTiles)
		 generateImage(outputImage, tile);
	  else
		 outputImage->transferDataFrom(tile);
	  deallocBuffers();
   }
   if(doT1 && getNumDecompressedPackets() == 0)
   {
	  Logger::logger_.warn("Tile %u was not decompressed", tileIndex_);
	  if(!outputImage->hasMultipleTiles)
		 return false;
   }
   return true;
}

void TileProcessor::ingestImage()
{
   for(uint16_t i = 0; i < headerImage->numcomps; ++i)
   {
	  auto tilec = tile->comps + i;
	  auto img_comp = headerImage->comps + i;

	  uint32_t offset_x = ceildiv<uint32_t>(headerImage->x0, img_comp->dx);
	  uint32_t offset_y = ceildiv<uint32_t>(headerImage->y0, img_comp->dy);
	  uint64_t image_offset =
		  (tilec->x0 - offset_x) + (uint64_t)(tilec->y0 - offset_y) * img_comp->stride;
	  auto src = img_comp->data + image_offset;
	  auto dest = tilec->getWindow()->getResWindowBufferHighestSimple();

	  for(uint32_t j = 0; j < tilec->height(); ++j)
	  {
		 memcpy(dest.buf_, src, tilec->width() * sizeof(int32_t));
		 src += img_comp->stride;
		 dest.buf_ += dest.stride_;
	  }
   }
}
bool TileProcessor::needsMctDecompress(void)
{
   if(!tcp_->mct)
	  return false;
   if(tile->numcomps_ < 3)
   {
	  Logger::logger_.warn("Number of components (%u) is less than 3 - skipping MCT.",
						   tile->numcomps_);
	  return false;
   }
   if(!headerImage->componentsEqual(3, false))
   {
	  Logger::logger_.warn("Not all tiles components have the same dimensions - skipping MCT.");
	  return false;
   }
   if(tcp_->mct == 2 && !tcp_->mct_decoding_matrix_)
	  return false;

   return true;
}
bool TileProcessor::needsMctDecompress(uint16_t compno)
{
   if(!needsMctDecompress())
	  return false;

   return (compno <= 2);
}
bool TileProcessor::mctDecompress(FlowComponent* flow)
{
   // custom MCT
   if(tcp_->mct == 2)
   {
	  auto data = new uint8_t*[tile->numcomps_];
	  for(uint16_t i = 0; i < tile->numcomps_; ++i)
	  {
		 auto tile_comp = tile->comps + i;
		 data[i] = (uint8_t*)tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
	  }
	  uint64_t samples = tile->comps->getWindow()->stridedArea();
	  bool rc = mct::decompress_custom((uint8_t*)tcp_->mct_decoding_matrix_, samples, data,
									   tile->numcomps_, headerImage->comps->sgnd);
	  return rc;
   }
   else
   {
	  if(tcp_->tccps->qmfbid == 1)
		 mct_->decompress_rev(flow);
	  else
		 mct_->decompress_irrev(flow);
   }

   return true;
}
bool TileProcessor::dcLevelShiftCompress()
{
   for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
   {
	  auto tile_comp = tile->comps + compno;
	  auto tccp = tcp_->tccps + compno;
	  auto current_ptr = tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
	  uint64_t samples = tile_comp->getWindow()->stridedArea();
#ifndef GRK_FORCE_SIGNED_COMPRESS
	  if(needsMctDecompress(compno))
		 continue;
#else
	  tccp->dc_level_shift_ = 1 << ((this->headerImage->comps + compno)->prec - 1);
#endif

	  if(tccp->qmfbid == 1)
	  {
		 if(tccp->dc_level_shift_ == 0)
			continue;
		 for(uint64_t i = 0; i < samples; ++i)
		 {
			*current_ptr -= tccp->dc_level_shift_;
			++current_ptr;
		 }
	  }
	  else
	  {
		 // output float

		 // Note: we need to convert to FP even if level shift is zero
		 // todo: skip this inefficiency for zero level shift

		 float* floatPtr = (float*)current_ptr;
		 for(uint64_t i = 0; i < samples; ++i)
			*floatPtr++ = (float)(*current_ptr++ - tccp->dc_level_shift_);
	  }
#ifdef GRK_FORCE_SIGNED_COMPRESS
	  tccp->dc_level_shift_ = 0;
#endif
   }

   return true;
}
bool TileProcessor::mct_encode()
{
   if(!tcp_->mct)
	  return true;
   if(tcp_->mct == 2)
   {
	  if(!tcp_->mct_coding_matrix_)
		 return true;
	  auto data = new uint8_t*[tile->numcomps_];
	  for(uint32_t i = 0; i < tile->numcomps_; ++i)
	  {
		 auto tile_comp = tile->comps + i;
		 data[i] = (uint8_t*)tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
	  }
	  uint64_t samples = tile->comps->getWindow()->stridedArea();
	  bool rc = mct::compress_custom((uint8_t*)tcp_->mct_coding_matrix_, samples, data,
									 tile->numcomps_, headerImage->comps->sgnd);
	  delete[] data;
	  return rc;
   }
   else if(tcp_->tccps->qmfbid == 0)
	  mct_->compress_irrev(nullptr);
   else
	  mct_->compress_rev(nullptr);

   return true;
}
bool TileProcessor::dwt_encode()
{
   bool rc = true;
   for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
   {
	  auto tile_comp = tile->comps + compno;
	  auto tccp = tcp_->tccps + compno;
	  WaveletFwdImpl w;
	  if(!w.compress(tile_comp, tccp->qmfbid))
	  {
		 rc = false;
		 continue;
	  }
   }
   return rc;
}
void TileProcessor::t1_encode()
{
   const double* mct_norms;
   uint16_t mct_numcomps = 0U;
   auto tcp = tcp_;

   if(tcp->mct == 1)
   {
	  mct_numcomps = 3U;
	  /* irreversible compressing */
	  if(tcp->tccps->qmfbid == 0)
		 mct_norms = mct::get_norms_irrev();
	  else
		 mct_norms = mct::get_norms_rev();
   }
   else
   {
	  mct_numcomps = headerImage->numcomps;
	  mct_norms = (const double*)(tcp->mct_norms);
   }

   scheduler_ = new CompressScheduler(tile, needsRateControl(), tcp, mct_norms, mct_numcomps);
   scheduler_->schedule(0);
}
bool TileProcessor::encodeT2(uint32_t* tileBytesWritten)
{
   auto l_t2 = new T2Compress(this);
#ifdef DEBUG_LOSSLESS_T2
   for(uint16_t compno = 0; compno < p_image->numcomps_; ++compno)
   {
	  TileComponent* tilec = &tilePtr->comps[compno];
	  tilec->round_trip_resolutions = new Resolution[tilec->numresolutions];
	  for(uint32_t resno = 0; resno < tilec->numresolutions; ++resno)
	  {
		 auto res = tilec->resolutions_ + resno;
		 auto roundRes = tilec->round_trip_resolutions + resno;
		 roundRes->x0 = res->x0;
		 roundRes->y0 = res->y0;
		 roundRes->x1 = res->x1;
		 roundRes->y1 = res->y1;
		 roundRes->numTileBandWindows = res->numTileBandWindows;
		 for(uint32_t bandIndex = 0; bandIndex < roundRes->numTileBandWindows; ++bandIndex)
		 {
			roundRes->tileBand[bandIndex] = res->tileBand[bandIndex];
			roundRes->tileBand[bandIndex].x0 = res->tileBand[bandIndex].x0;
			roundRes->tileBand[bandIndex].y0 = res->tileBand[bandIndex].y0;
			roundRes->tileBand[bandIndex].x1 = res->tileBand[bandIndex].x1;
			roundRes->tileBand[bandIndex].y1 = res->tileBand[bandIndex].y1;
		 }

		 // allocate
		 for(uint32_t bandIndex = 0; bandIndex < roundRes->numTileBandWindows; ++bandIndex)
		 {
			auto tileBand = res->tileBand + bandIndex;
			auto decodeBand = roundRes->tileBand + bandIndex;
			if(!tileBand->numPrecincts)
			   continue;
			decodeBand->precincts = new Precinct[tileBand->numPrecincts];
			decodeBand->precincts_data_size = (uint32_t)(tileBand->numPrecincts * sizeof(Precinct));
			for(uint64_t precinctIndex = 0; precinctIndex < tileBand->numPrecincts; ++precinctIndex)
			{
			   auto prec = tileBand->precincts + precinctIndex;
			   auto decodePrec = decodeBand->precincts + precinctIndex;
			   decodePrec->cblk_grid_width = prec->cblk_grid_width;
			   decodePrec->cblk_grid_height = prec->cblk_grid_height;
			   if(prec->getCompressedBlockPtr() && prec->cblk_grid_width && prec->cblk_grid_height)
			   {
				  decodePrec->initTagTrees();
				  decodePrec->getDecompressedBlockPtr() =
					  new DecompressCodeblock[(uint64_t)decodePrec->cblk_grid_width *
											  decodePrec->cblk_grid_height];
				  for(uint64_t cblkno = 0;
					  cblkno < decodePrec->cblk_grid_width * decodePrec->cblk_grid_height; ++cblkno)
				  {
					 auto cblk = prec->getCompressedBlockPtr() + cblkno;
					 auto decodeCblk = decodePrec->getDecompressedBlockPtr() + cblkno;
					 decodeCblk->x0 = cblk->x0;
					 decodeCblk->y0 = cblk->y0;
					 decodeCblk->x1 = cblk->x1;
					 decodeCblk->y1 = cblk->y1;
					 decodeCblk->alloc();
				  }
			   }
			}
		 }
	  }
   }
#endif
   if(!l_t2->compressPackets(tileIndex_, tcp_->max_layers_, stream_, tileBytesWritten,
							 first_poc_tile_part_, newTilePartProgressionPosition, pino))
   {
	  delete l_t2;
	  return false;
   }
   delete l_t2;
#ifdef DEBUG_LOSSLESS_T2
   for(uint16_t compno = 0; compno < p_image->numcomps_; ++compno)
   {
	  TileComponent* tilec = &tilePtr->comps[compno];
	  for(uint32_t resno = 0; resno < tilec->numresolutions; ++resno)
	  {
		 auto roundRes = tilec->round_trip_resolutions + resno;
		 for(uint32_t bandIndex = 0; bandIndex < roundRes->numTileBandWindows; ++bandIndex)
		 {
			auto decodeBand = roundRes->tileBand + bandIndex;
			if(decodeBand->precincts)
			{
			   for(uint64_t precinctIndex = 0; precinctIndex < decodeBand->numPrecincts;
				   ++precinctIndex)
			   {
				  auto decodePrec = decodeBand->precincts + precinctIndex;
				  decodePrec->cleanupDecodeBlocks();
			   }
			   delete[] decodeBand->precincts;
			   decodeBand->precincts = nullptr;
			}
		 }
	  }
	  delete[] tilec->round_trip_resolutions;
   }
#endif

   return true;
}
bool TileProcessor::preCompressTile()
{
   tilePartCounter_ = 0;
   first_poc_tile_part_ = true;

   /* initialization before tile compressing  */
   bool rc = init();
   if(!rc)
	  return false;
   // don't need to allocate any buffers if this is from the plugin.
   if(current_plugin_tile)
	  return true;
   rc = createWindowBuffers(nullptr);
   if(!rc)
	  return false;
   uint32_t numTiles = (uint32_t)cp_->t_grid_height * cp_->t_grid_width;
   bool transfer_image_to_tile = (numTiles == 1);
   /* if we only have one tile, then simply set tile component data equal to
	* image component data. Otherwise, allocate tile data and copy */
   for(uint32_t j = 0; j < headerImage->numcomps; ++j)
   {
	  auto tilec = tile->comps + j;
	  auto imagec = headerImage->comps + j;
	  if(transfer_image_to_tile && imagec->data)
		 tilec->getWindow()->attach(imagec->data, imagec->stride);
	  else if(!tilec->getWindow()->alloc())
	  {
		 Logger::logger_.error("Error allocating tile component data.");
		 return false;
	  }
   }
   if(!transfer_image_to_tile)
	  ingestImage();

   return true;
}
/**
 * Assume that source stride  == source width == destination width
 */
template<typename T>
void grk_copy_strided(uint32_t w, uint32_t stride, uint32_t h, T* src, int32_t* dest)
{
   assert(stride >= w);
   uint32_t stride_diff = stride - w;
   size_t src_ind = 0, dest_ind = 0;
   for(uint32_t j = 0; j < h; ++j)
   {
	  for(uint32_t i = 0; i < w; ++i)
		 dest[dest_ind++] = src[src_ind++];
	  dest_ind += stride_diff;
   }
}
bool TileProcessor::ingestUncompressedData(uint8_t* p_src, uint64_t src_length)
{
   uint64_t tile_size = 0;
   for(uint32_t i = 0; i < headerImage->numcomps; ++i)
   {
	  auto tilec = tile->comps + i;
	  auto img_comp = headerImage->comps + i;
	  uint32_t size_comp = (uint32_t)((img_comp->prec + 7) >> 3);
	  tile_size += size_comp * tilec->area();
   }
   if(!p_src || (tile_size != src_length))
	  return false;
   size_t length_per_component = src_length / headerImage->numcomps;
   for(uint32_t i = 0; i < headerImage->numcomps; ++i)
   {
	  auto tilec = tile->comps + i;
	  auto img_comp = headerImage->comps + i;
	  uint32_t size_comp = (uint32_t)((img_comp->prec + 7) >> 3);
	  auto b = tilec->getWindow()->getResWindowBufferHighestSimple();
	  auto dest_ptr = b.buf_;
	  uint32_t w = (uint32_t)tilec->getWindow()->bounds().width();
	  uint32_t h = (uint32_t)tilec->getWindow()->bounds().height();
	  uint32_t stride = b.stride_;
	  switch(size_comp)
	  {
		 case 1:
			if(img_comp->sgnd)
			{
			   auto src = (int8_t*)p_src;
			   grk_copy_strided<int8_t>(w, stride, h, src, dest_ptr);
			   p_src = (uint8_t*)(src + length_per_component);
			}
			else
			{
			   auto src = (uint8_t*)p_src;
			   grk_copy_strided<uint8_t>(w, stride, h, src, dest_ptr);
			   p_src = (uint8_t*)(src + length_per_component);
			}
			break;
		 case 2:
			if(img_comp->sgnd)
			{
			   auto src = (int16_t*)p_src;
			   grk_copy_strided<int16_t>(w, stride, h, (int16_t*)p_src, dest_ptr);
			   p_src = (uint8_t*)(src + length_per_component);
			}
			else
			{
			   auto src = (uint16_t*)p_src;
			   grk_copy_strided<uint16_t>(w, stride, h, (uint16_t*)p_src, dest_ptr);
			   p_src = (uint8_t*)(src + length_per_component);
			}
			break;
	  }
   }

   return true;
}
bool TileProcessor::cacheTilePartPackets(CodeStreamDecompress* codeStream)
{
   assert(codeStream);

   // note: we subtract MARKER_BYTES to account for SOD marker
   auto tcp = codeStream->get_current_decode_tcp();
   if(tilePartDataLength >= MARKER_BYTES)
	  tilePartDataLength -= MARKER_BYTES;
   else
	  // illegal tile part data length, but we will allow it
	  tilePartDataLength = 0;

   if(tilePartDataLength)
   {
	  auto bytesLeftInStream = stream_->numBytesLeft();
	  if(bytesLeftInStream == 0)
	  {
		 Logger::logger_.error("Tile %u, tile part %u: stream has been truncated and "
							   "there is no tile data available",
							   tileIndex_, tcp->tilePartCounter_ - 1);
		 return false;
	  }
	  // check that there are enough bytes in stream to fill tile data
	  if(tilePartDataLength > bytesLeftInStream)
	  {
		 Logger::logger_.warn("Tile part length %lld greater than "
							  "stream length %lld\n"
							  "(tile: %u, tile part: %u). Tile has been truncated.",
							  tilePartDataLength, stream_->numBytesLeft(), tileIndex_,
							  tcp->tilePartCounter_ - 1);

		 // sanitize tilePartDataLength
		 tilePartDataLength = (uint64_t)bytesLeftInStream;
		 truncated = true;
	  }
   }
   /* Index */
   auto codeStreamInfo = codeStream->getCodeStreamInfo();
   if(codeStreamInfo)
   {
	  uint64_t current_pos = stream_->tell();
	  if(current_pos < MARKER_BYTES)
	  {
		 Logger::logger_.error("Stream too short");

		 return false;
	  }
	  current_pos = (uint64_t)(current_pos - MARKER_BYTES);
	  auto tileInfo = codeStreamInfo->getTileInfo(tileIndex_);
	  uint8_t current_tile_part = tileInfo->currentTilePart;
	  auto tilePartInfo = tileInfo->getTilePartInfo(current_tile_part);
	  tilePartInfo->endHeaderPosition = current_pos;
	  tilePartInfo->endPosition = current_pos + tilePartDataLength + MARKER_BYTES;
	  if(!TileLengthMarkers::addTileMarkerInfo(tileIndex_, codeStreamInfo, J2K_MS_SOD, current_pos,
											   0))
	  {
		 Logger::logger_.error("Not enough memory to add tl marker");

		 return false;
	  }
   }
   size_t current_read_size = 0;
   if(tilePartDataLength)
   {
	  if(!tcp->compressedTileData_)
		 tcp->compressedTileData_ = new SparseBuffer();
	  auto len = tilePartDataLength;
	  uint8_t* buff = nullptr;
	  auto zeroCopy = stream_->supportsZeroCopy();
	  if(zeroCopy)
	  {
		 buff = stream_->getZeroCopyPtr();
	  }
	  else
	  {
		 try
		 {
			buff = new uint8_t[len];
		 }
		 catch([[maybe_unused]] const std::bad_alloc& ex)
		 {
			Logger::logger_.error("Not enough memory to allocate segment");

			return false;
		 }
	  }
	  current_read_size = stream_->read(zeroCopy ? nullptr : buff, len);
	  tcp->compressedTileData_->pushBack(buff, len, !zeroCopy);
   }
   if(current_read_size != tilePartDataLength)
	  codeStream->getDecompressorState()->setState(DECOMPRESS_STATE_NO_EOC);
   else
	  codeStream->getDecompressorState()->setState(DECOMPRESS_STATE_TPH_SOT);

   return true;
}
// RATE CONTROL ////////////////////////////////////////////
bool TileProcessor::rateAllocate(uint32_t* allPacketBytes, bool disableRateControl)
{
   return pcrdBisectSimple(allPacketBytes, disableRateControl);
}
bool TileProcessor::layerNeedsRateControl(uint32_t layno)
{
   auto enc_params = &cp_->coding_params_.enc_;
   return (enc_params->allocationByRateDistortion_ && (tcp_->rates[layno] > 0.0)) ||
		  (enc_params->allocationByFixedQuality_ && (tcp_->distortion[layno] > 0.0f));
}
bool TileProcessor::needsRateControl()
{
   for(uint16_t i = 0; i < tcp_->max_layers_; ++i)
   {
	  if(layerNeedsRateControl(i))
		 return true;
   }
   return false;
}
// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool TileProcessor::makeSingleLosslessLayer()
{
   if(tcp_->max_layers_ == 1 && !layerNeedsRateControl(0))
   {
	  makeLayerFinal(0);

	  return true;
   }

   return false;
}
/*
 Simple bisect algorithm to calculate optimal layer truncation points
 */
bool TileProcessor::pcrdBisectSimple(uint32_t* allPacketBytes, bool disableRateControl)
{
   uint32_t passno;
   const double K = 1;
   double maxSE = 0;
   double min_slope = DBL_MAX;
   double max_slope = -1;
   uint32_t state = grk_plugin_get_debug_state();
   bool single_lossless = makeSingleLosslessLayer();
   uint64_t numPacketsPerLayer = 0;
   uint64_t numCodeBlocks = 0;
   auto tcp = tcp_;
   for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
   {
	  auto tilec = &tile->comps[compno];
	  uint64_t numpix = 0;
	  for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
	  {
		 auto res = &tilec->resolutions_[resno];
		 for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
		 {
			auto band = &res->tileBand[bandIndex];
			for(auto prc : band->precincts)
			{
			   numPacketsPerLayer++;
			   for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
			   {
				  auto cblk = prc->getCompressedBlockPtr(cblkno);
				  uint32_t numPix = (uint32_t)cblk->area();
				  numCodeBlocks++;
				  if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
				  {
					 compress_synch_with_plugin(this, compno, resno, bandIndex, prc->precinctIndex,
												cblkno, band, cblk, &numPix);
				  }
				  if(!single_lossless)
				  {
					 for(passno = 0; passno < cblk->numPassesTotal; passno++)
					 {
						auto pass = cblk->passes + passno;
						int32_t dr;
						double dd, rdslope;
						if(passno == 0)
						{
						   dr = (int32_t)pass->rate;
						   dd = pass->distortiondec;
						}
						else
						{
						   dr = (int32_t)(pass->rate - cblk->passes[passno - 1].rate);
						   dd = pass->distortiondec - cblk->passes[passno - 1].distortiondec;
						}
						if(dr == 0)
						   continue;
						rdslope = dd / dr;
						if(rdslope < min_slope)
						   min_slope = rdslope;
						if(rdslope > max_slope)
						   max_slope = rdslope;
					 } /* passno */
					 numpix += numPix;
				  }
			   } /* cbklno */
			} /* precinctIndex */
		 } /* bandIndex */
	  } /* resno */
	  if(!single_lossless)
		 maxSE += (double)(((uint64_t)1 << headerImage->comps[compno].prec) - 1) *
				  (double)(((uint64_t)1 << headerImage->comps[compno].prec) - 1) * (double)numpix;

   } /* compno */

   auto t2 = T2Compress(this);
   if(single_lossless)
   {
	  // simulation will generate correct PLT lengths
	  // and correct tile length
	  return t2.compressPacketsSimulate(tileIndex_, 0 + 1U, allPacketBytes, UINT_MAX,
										newTilePartProgressionPosition,
										packetLengthCache.getMarkers(), true, false);
   }
   double cumulativeDistortion[maxCompressLayersGRK];
   double upperBound = max_slope;
   uint32_t maxLayerLength = UINT_MAX;
   for(uint16_t layno = 0; layno < tcp_->max_layers_; layno++)
   {
	  maxLayerLength = (!disableRateControl && tcp->rates[layno] > 0.0f)
						   ? ((uint32_t)ceil(tcp->rates[layno]))
						   : UINT_MAX;
	  if(layerNeedsRateControl(layno))
	  {
		 double lowerBound = min_slope;
		 /* Threshold for Marcela Index */
		 // start by including everything in this layer
		 double goodthresh = 0;
		 // thresh from previous iteration - starts off uninitialized
		 // used to bail out if difference with current thresh is small enough
		 double prevthresh = -1;
		 double distortionTarget =
			 tile->distortion - ((K * maxSE) / pow(10.0, tcp_->distortion[layno] / 10.0));
		 double thresh;
		 for(uint32_t i = 0; i < 128; ++i)
		 {
			// thresh is half-way between lower and upper bound
			thresh = (upperBound == -1) ? lowerBound : (lowerBound + upperBound) / 2;
			makeLayerSimple(layno, thresh, false);
			if(prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
			   break;
			prevthresh = thresh;
			if(cp_->coding_params_.enc_.allocationByFixedQuality_)
			{
			   double distoachieved =
				   layno == 0 ? tile->layerDistoration[0]
							  : cumulativeDistortion[layno - 1] + tile->layerDistoration[layno];
			   if(distoachieved < distortionTarget)
			   {
				  upperBound = thresh;
				  continue;
			   }
			   lowerBound = thresh;
			}
			else
			{
			   if(!t2.compressPacketsSimulate(tileIndex_, layno + 1U, allPacketBytes,
											  maxLayerLength, newTilePartProgressionPosition,
											  packetLengthCache.getMarkers(), false, false))
			   {
				  lowerBound = thresh;
				  continue;
			   }
			   upperBound = thresh;
			}
		 }
		 // choose conservative value for goodthresh
		 goodthresh = (upperBound == -1) ? thresh : upperBound;
		 makeLayerSimple(layno, goodthresh, true);
		 cumulativeDistortion[layno] =
			 (layno == 0) ? tile->layerDistoration[0]
						  : (cumulativeDistortion[layno - 1] + tile->layerDistoration[layno]);

		 // upper bound for next layer will equal lowerBound for previous layer, minus one
		 upperBound = lowerBound - 1;
	  }
	  else
	  {
		 makeLayerFinal(layno);
		 assert(layno == tcp_->max_layers_ - 1);
	  }
   }

   // final simulation will generate correct PLT lengths
   // and correct tile length
   // Logger::logger_.info("Rate control final simulation");
   return t2.compressPacketsSimulate(tileIndex_, tcp_->max_layers_, allPacketBytes, maxLayerLength,
									 newTilePartProgressionPosition, packetLengthCache.getMarkers(),
									 true, false);
}
static void prepareBlockForFirstLayer(CompressCodeblock* cblk)
{
   cblk->numPassesInPreviousPackets = 0;
   cblk->setNumPassesInPacket(0, 0);
   cblk->numlenbits = 0;
}
/*
 Form layer for bisect rate control algorithm
 */
void TileProcessor::makeLayerSimple(uint32_t layno, double thresh, bool finalAttempt)
{
   tile->layerDistoration[layno] = 0;
   for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
   {
	  auto tilec = tile->comps + compno;
	  for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
	  {
		 auto res = tilec->resolutions_ + resno;
		 for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
		 {
			auto band = res->tileBand + bandIndex;
			for(auto prc : band->precincts)
			{
			   for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
			   {
				  auto cblk = prc->getCompressedBlockPtr(cblkno);
				  auto layer = cblk->layers + layno;
				  uint32_t included_blk_passes;

				  if(layno == 0)
					 prepareBlockForFirstLayer(cblk);
				  if(thresh == 0)
				  {
					 included_blk_passes = cblk->numPassesTotal;
				  }
				  else
				  {
					 included_blk_passes = cblk->numPassesInPreviousPackets;
					 for(uint32_t passno = cblk->numPassesInPreviousPackets;
						 passno < cblk->numPassesTotal; passno++)
					 {
						uint32_t dr;
						double dd;
						CodePass* pass = &cblk->passes[passno];
						if(included_blk_passes == 0)
						{
						   dr = pass->rate;
						   dd = pass->distortiondec;
						}
						else
						{
						   dr = pass->rate - cblk->passes[included_blk_passes - 1].rate;
						   dd = pass->distortiondec -
								cblk->passes[included_blk_passes - 1].distortiondec;
						}

						if(!dr)
						{
						   if(dd != 0)
							  included_blk_passes = passno + 1;
						   continue;
						}
						auto slope = dd / dr;
						/* do not rely on float equality, check with DBL_EPSILON margin */
						if(thresh - slope < DBL_EPSILON)
						   included_blk_passes = passno + 1;
					 }
				  }
				  layer->numpasses = included_blk_passes - cblk->numPassesInPreviousPackets;
				  if(!layer->numpasses)
				  {
					 layer->distortion = 0;
					 continue;
				  }

				  // update layer
				  if(cblk->numPassesInPreviousPackets == 0)
				  {
					 layer->len = cblk->passes[included_blk_passes - 1].rate;
					 layer->data = cblk->paddedCompressedStream;
					 layer->distortion = cblk->passes[included_blk_passes - 1].distortiondec;
				  }
				  else
				  {
					 layer->len = cblk->passes[included_blk_passes - 1].rate -
								  cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
					 layer->data = cblk->paddedCompressedStream +
								   cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
					 layer->distortion =
						 cblk->passes[included_blk_passes - 1].distortiondec -
						 cblk->passes[cblk->numPassesInPreviousPackets - 1].distortiondec;
				  }
				  tile->layerDistoration[layno] += layer->distortion;
				  if(finalAttempt)
					 cblk->numPassesInPreviousPackets = included_blk_passes;
			   }
			}
		 }
	  }
   }
}
// Add all remaining passes to this layer
void TileProcessor::makeLayerFinal(uint32_t layno)
{
   tile->layerDistoration[layno] = 0;
   for(uint16_t compno = 0; compno < tile->numcomps_; compno++)
   {
	  auto tilec = tile->comps + compno;
	  for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
	  {
		 auto res = tilec->resolutions_ + resno;
		 for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
		 {
			auto band = res->tileBand + bandIndex;
			for(auto prc : band->precincts)
			{
			   for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
			   {
				  auto cblk = prc->getCompressedBlockPtr(cblkno);
				  auto layer = cblk->layers + layno;
				  if(layno == 0)
					 prepareBlockForFirstLayer(cblk);
				  uint32_t included_blk_passes = cblk->numPassesInPreviousPackets;
				  if(cblk->numPassesTotal > cblk->numPassesInPreviousPackets)
					 included_blk_passes = cblk->numPassesTotal;

				  layer->numpasses = included_blk_passes - cblk->numPassesInPreviousPackets;
				  if(!layer->numpasses)
				  {
					 layer->distortion = 0;
					 continue;
				  }
				  // update layer
				  if(cblk->numPassesInPreviousPackets == 0)
				  {
					 layer->len = cblk->passes[included_blk_passes - 1].rate;
					 layer->data = cblk->paddedCompressedStream;
					 layer->distortion = cblk->passes[included_blk_passes - 1].distortiondec;
				  }
				  else
				  {
					 layer->len = cblk->passes[included_blk_passes - 1].rate -
								  cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
					 layer->data = cblk->paddedCompressedStream +
								   cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
					 layer->distortion =
						 cblk->passes[included_blk_passes - 1].distortiondec -
						 cblk->passes[cblk->numPassesInPreviousPackets - 1].distortiondec;
				  }
				  tile->layerDistoration[layno] += layer->distortion;
				  cblk->numPassesInPreviousPackets = included_blk_passes;
				  assert(cblk->numPassesInPreviousPackets == cblk->numPassesTotal);
			   }
			}
		 }
	  }
   }
}
Tile::Tile() : numcomps_(0), comps(nullptr), distortion(0)
{
   for(uint32_t i = 0; i < maxCompressLayersGRK; ++i)
	  layerDistoration[i] = 0;
}
Tile::Tile(uint16_t numcomps) : Tile()
{
   assert(numcomps);
   numcomps_ = numcomps;
   if(numcomps)
	  comps = new TileComponent[numcomps];
}
Tile::~Tile()
{
   delete[] comps;
}
PacketTracker::PacketTracker() : bits(nullptr), numcomps_(0), numres_(0), numprec_(0), numlayers_(0)
{}
PacketTracker::~PacketTracker()
{
   delete[] bits;
}
void PacketTracker::init(uint32_t numcomps, uint32_t numres, uint64_t numprec, uint32_t numlayers)
{
   uint64_t len = get_buffer_len(numcomps, numres, numprec, numlayers);
   if(!bits)
   {
	  bits = new uint8_t[len];
   }
   else
   {
	  uint64_t currentLen = get_buffer_len(numcomps_, numres_, numprec_, numlayers_);
	  if(len > currentLen)
	  {
		 delete[] bits;
		 bits = new uint8_t[len];
	  }
   }
   clear();
   numcomps_ = numcomps;
   numres_ = numres;
   numprec_ = numprec;
   numlayers_ = numlayers;
}
void PacketTracker::clear(void)
{
   uint64_t currentLen = get_buffer_len(numcomps_, numres_, numprec_, numlayers_);
   memset(bits, 0, currentLen);
}
uint64_t PacketTracker::get_buffer_len(uint32_t numcomps, uint32_t numres, uint64_t numprec,
									   uint32_t numlayers)
{
   uint64_t len = (uint64_t)numcomps * numres * numprec * numlayers;

   return ((len + 7) >> 3) << 3;
}
void PacketTracker::packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer)
{
   if(comps >= numcomps_ || prec >= numprec_ || res >= numres_ || layer >= numlayers_)
	  return;

   uint64_t ind = index(comps, res, prec, layer);
   uint64_t ind_maj = ind >> 3;
   uint64_t ind_min = ind & 7;

   bits[ind_maj] = (uint8_t)(bits[ind_maj] | (1 << ind_min));
}
bool PacketTracker::is_packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer)
{
   if(comps >= numcomps_ || prec >= numprec_ || res >= numres_ || layer >= numlayers_)
	  return true;

   uint64_t ind = index(comps, res, prec, layer);
   uint64_t ind_maj = ind >> 3;
   uint64_t ind_min = ind & 7;

   return ((bits[ind_maj] >> ind_min) & 1);
}
uint64_t PacketTracker::index(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer)
{
   return prec + (uint64_t)res * numprec_ + (uint64_t)comps * numres_ * numprec_ +
		  (uint64_t)layer * numcomps_ * numres_ * numprec_;
}

} // namespace grk
