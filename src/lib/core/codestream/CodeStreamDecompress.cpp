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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
CodeStreamDecompress::CodeStreamDecompress(BufferedStream* stream)
	: CodeStream(stream), expectSOD_(false), curr_marker_(0), headerError_(false),
	  headerRead_(false), marker_scratch_(nullptr), marker_scratch_size_(0), outputImage_(nullptr),
	  tileCache_(new TileCache()), ioBufferCallback(nullptr), ioUserData(nullptr),
	  grkRegisterReclaimCallback_(nullptr)
{
   decompressorState_.default_tcp_ = new TileCodingParams();
   decompressorState_.lastSotReadPosition = 0;

   /* code stream index creation */
   codeStreamInfo = new CodeStreamInfo(stream);
   headerImage_ = new GrkImage();
   headerImage_->meta = grk_image_meta_new();
   marker_map = {
	   {J2K_MS_SOT,
		new marker_handler(J2K_MS_SOT, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH_SOT,
						   [this](uint8_t* data, uint16_t len) { return read_sot(data, len); })},
	   {J2K_MS_COD,
		new marker_handler(J2K_MS_COD, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_cod(data, len); })},
	   {J2K_MS_COC,
		new marker_handler(J2K_MS_COC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_coc(data, len); })},
	   {J2K_MS_RGN,
		new marker_handler(J2K_MS_RGN, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_rgn(data, len); })},
	   {J2K_MS_QCD,
		new marker_handler(J2K_MS_QCD, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_qcd(data, len); })},
	   {J2K_MS_QCC,
		new marker_handler(J2K_MS_QCC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_qcc(data, len); })},
	   {J2K_MS_POC,
		new marker_handler(J2K_MS_POC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_poc(data, len); })},
	   {J2K_MS_SIZ,
		new marker_handler(J2K_MS_SIZ, DECOMPRESS_STATE_MH_SIZ,
						   [this](uint8_t* data, uint16_t len) { return read_siz(data, len); })},
	   {J2K_MS_CAP,
		new marker_handler(J2K_MS_CAP, DECOMPRESS_STATE_MH,
						   [this](uint8_t* data, uint16_t len) { return read_cap(data, len); })},
	   {J2K_MS_TLM,
		new marker_handler(J2K_MS_TLM, DECOMPRESS_STATE_MH,
						   [this](uint8_t* data, uint16_t len) { return read_tlm(data, len); })},
	   {J2K_MS_PLM,
		new marker_handler(J2K_MS_PLM, DECOMPRESS_STATE_MH,
						   [this](uint8_t* data, uint16_t len) { return read_plm(data, len); })},
	   {J2K_MS_PLT,
		new marker_handler(J2K_MS_PLT, DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_plt(data, len); })},
	   {J2K_MS_PPM,
		new marker_handler(J2K_MS_PPM, DECOMPRESS_STATE_MH,
						   [this](uint8_t* data, uint16_t len) { return read_ppm(data, len); })},
	   {J2K_MS_PPT,
		new marker_handler(J2K_MS_PPT, DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_ppt(data, len); })},
	   {J2K_MS_CRG,
		new marker_handler(J2K_MS_CRG, DECOMPRESS_STATE_MH,
						   [this](uint8_t* data, uint16_t len) { return read_crg(data, len); })},
	   {J2K_MS_COM,
		new marker_handler(J2K_MS_COM, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_com(data, len); })},
	   {J2K_MS_MCT,
		new marker_handler(J2K_MS_MCT, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_mct(data, len); })},
	   {J2K_MS_CBD,
		new marker_handler(J2K_MS_CBD, DECOMPRESS_STATE_MH,
						   [this](uint8_t* data, uint16_t len) { return read_cbd(data, len); })},
	   {J2K_MS_MCC,
		new marker_handler(J2K_MS_MCC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_mcc(data, len); })},
	   {J2K_MS_MCO,
		new marker_handler(J2K_MS_MCO, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
						   [this](uint8_t* data, uint16_t len) { return read_mco(data, len); })}};
}
CodeStreamDecompress::~CodeStreamDecompress()
{
   for(const auto& val : marker_map)
	  delete val.second;
   delete decompressorState_.default_tcp_;
   delete[] marker_scratch_;
   if(outputImage_)
	  grk_object_unref(&outputImage_->obj);
   delete tileCache_;
}
bool CodeStreamDecompress::needsHeaderRead(void)
{
   return !headerError_ && !headerRead_;
}
void CodeStreamDecompress::setExpectSOD()
{
   expectSOD_ = true;
}
GrkImage* CodeStreamDecompress::getCompositeImage()
{
   return tileCache_->getComposite();
}
TileProcessor* CodeStreamDecompress::allocateProcessor(uint16_t tileIndex)
{
   auto tileCache = tileCache_->get(tileIndex);
   auto tileProcessor = tileCache ? tileCache->processor : nullptr;
   if(!tileProcessor)
   {
	  tileProcessor = new TileProcessor(tileIndex, this, stream_, false, &stripCache_);
	  tileCache_->put(tileIndex, tileProcessor);
   }
   currentTileProcessor_ = tileProcessor;

   return currentTileProcessor_;
}
TileCodingParams* CodeStreamDecompress::get_current_decode_tcp()
{
   return (isDecodingTilePartHeader()) ? cp_.tcps + currentTileProcessor_->getIndex()
									   : decompressorState_.default_tcp_;
}
CodeStreamInfo* CodeStreamDecompress::getCodeStreamInfo(void)
{
   return codeStreamInfo;
}
bool CodeStreamDecompress::isDecodingTilePartHeader()
{
   return (decompressorState_.getState() & DECOMPRESS_STATE_TPH);
}
DecompressorState* CodeStreamDecompress::getDecompressorState(void)
{
   return &decompressorState_;
}
GrkImage* CodeStreamDecompress::getImage(uint16_t tileIndex)
{
   auto entry = tileCache_->get(tileIndex);
   return entry ? entry->processor->getImage() : nullptr;
}
std::vector<GrkImage*> CodeStreamDecompress::getAllImages(void)
{
   return tileCache_->getAllImages();
}
GrkImage* CodeStreamDecompress::getImage()
{
   return getCompositeImage();
}
bool CodeStreamDecompress::readHeader(grk_header_info* header_info)
{
   if(headerError_)
	  return false;

   if(!headerRead_)
   {
	  validation_list_.push_back(std::bind(&CodeStreamDecompress::decompressValidation, this));
	  if(!exec(validation_list_))
	  {
		 headerError_ = true;
		 return false;
	  }
	  headerRead_ = true;
	  procedure_list_.push_back(std::bind(&CodeStreamDecompress::readHeaderProcedure, this));
	  procedure_list_.push_back(std::bind(&CodeStreamDecompress::copy_default_tcp, this));
	  if(!exec(procedure_list_))
	  {
		 headerError_ = true;
		 return false;
	  }
	  if(header_info)
		 headerImage_->hasMultipleTiles =
			 headerImage_->hasMultipleTiles && !header_info->singleTileDecompress;
	  auto composite = getCompositeImage();
	  headerImage_->copyHeader(composite);
	  if(header_info)
	  {
		 composite->decompressFormat = header_info->decompressFormat;
		 composite->forceRGB = header_info->forceRGB;
		 composite->upsample = header_info->upsample;
		 composite->precision = header_info->precision;
		 composite->numPrecision = header_info->numPrecision;
		 composite->splitByComponent = header_info->splitByComponent;
	  }
   }
   if(header_info)
   {
	  auto cp = &(cp_);
	  auto tcp = decompressorState_.default_tcp_;
	  auto tccp = tcp->tccps;

	  header_info->cblockw_init = 1U << tccp->cblkw;
	  header_info->cblockh_init = 1U << tccp->cblkh;
	  header_info->irreversible = tccp->qmfbid == 0;
	  header_info->mct = tcp->mct;
	  header_info->rsiz = cp->rsiz;
	  header_info->numresolutions = tccp->numresolutions;
	  // !!! assume that coding style is constant across all tile components
	  header_info->csty = tccp->csty;
	  // !!! assume that mode switch is constant across all tiles
	  header_info->cblk_sty = tccp->cblk_sty;
	  for(uint32_t i = 0; i < header_info->numresolutions; ++i)
	  {
		 header_info->prcw_init[i] = 1U << tccp->precWidthExp[i];
		 header_info->prch_init[i] = 1U << tccp->precHeightExp[i];
	  }
	  header_info->tx0 = cp_.tx0;
	  header_info->ty0 = cp_.ty0;

	  header_info->t_width = cp_.t_width;
	  header_info->t_height = cp_.t_height;

	  header_info->t_grid_width = cp_.t_grid_width;
	  header_info->t_grid_height = cp_.t_grid_height;

	  header_info->max_layers_ = tcp->max_layers_;

	  header_info->num_comments = cp_.num_comments;
	  for(size_t i = 0; i < header_info->num_comments; ++i)
	  {
		 header_info->comment[i] = cp_.comment[i];
		 header_info->comment_len[i] = cp_.comment_len[i];
		 header_info->isBinaryComment[i] = cp_.isBinaryComment[i];
	  }
   }
   return true;
}
bool CodeStreamDecompress::setDecompressRegion(grk_rect_single region)
{
   auto image = headerImage_;
   auto compositeImage = getCompositeImage();
   auto decompressor = &decompressorState_;

   /* Check if we have read the main header */
   if(decompressor->getState() != DECOMPRESS_STATE_TPH_SOT)
   {
	  Logger::logger_.error("Need to read the main header before setting decompress region");
	  return false;
   }

   if(region != grk_rect_single(0, 0, 0, 0))
   {
	  grk_rect16 tilesToDecompress;
	  /* Check if the region provided by the user is correct */
	  uint32_t start_x = (uint32_t)region.x0 + image->x0;
	  uint32_t start_y = (uint32_t)region.y0 + image->y0;
	  uint32_t end_x = (uint32_t)region.x1 + image->x0;
	  uint32_t end_y = (uint32_t)region.y1 + image->y0;
	  /* Left */
	  if(start_x > image->x1)
	  {
		 Logger::logger_.error("Left position of the decompress region (%u)"
							   " is outside of the image area (Xsiz=%u).",
							   start_x, image->x1);
		 return false;
	  }
	  else
	  {
		 tilesToDecompress.x0 = uint16_t((start_x - cp_.tx0) / cp_.t_width);
		 compositeImage->x0 = start_x;
	  }

	  /* Up */
	  if(start_y > image->y1)
	  {
		 Logger::logger_.error("Top position of the decompress region (%u)"
							   " is outside of the image area (Ysiz=%u).",
							   start_y, image->y1);
		 return false;
	  }
	  else
	  {
		 tilesToDecompress.y0 = uint16_t((start_y - cp_.ty0) / cp_.t_height);
		 compositeImage->y0 = start_y;
	  }

	  /* Right */
	  assert(end_x > 0);
	  assert(end_y > 0);
	  if(end_x > image->x1)
	  {
		 Logger::logger_.warn("Right position of the decompress region (%u)"
							  " is outside the image area (Xsiz=%u).",
							  end_x, image->x1);
		 tilesToDecompress.x1 = cp_.t_grid_width;
		 compositeImage->x1 = image->x1;
	  }
	  else
	  {
		 // avoid divide by zero
		 if(cp_.t_width == 0)
			return false;
		 tilesToDecompress.x1 = uint16_t(ceildiv<uint32_t>(end_x - cp_.tx0, cp_.t_width));
		 compositeImage->x1 = end_x;
	  }

	  /* Bottom */
	  if(end_y > image->y1)
	  {
		 Logger::logger_.warn("Bottom position of the decompress region (%u)"
							  " is outside of the image area (Ysiz=%u).",
							  end_y, image->y1);
		 tilesToDecompress.y1 = cp_.t_grid_height;
		 compositeImage->y1 = image->y1;
	  }
	  else
	  {
		 // avoid divide by zero
		 if(cp_.t_height == 0)
			return false;
		 tilesToDecompress.y1 = (uint16_t)(ceildiv<uint32_t>(end_y - cp_.ty0, cp_.t_height));
		 compositeImage->y1 = end_y;
	  }
	  decompressor->tilesToDecompress_.schedule(tilesToDecompress);
	  cp_.wholeTileDecompress_ = false;
	  if(!compositeImage->subsampleAndReduce(cp_.coding_params_.dec_.reduce_))
		 return false;

	  Logger::logger_.info("decompress region canvas coordinates set to (%u,%u,%u,%u)",
						   compositeImage->x0, compositeImage->y0, compositeImage->x1,
						   compositeImage->y1);
	  auto scaledX0 = float(compositeImage->x0 - image->x0) / float(image->width());
	  auto scaledY0 = float(compositeImage->y0 - image->y0) / float(image->height());
	  auto scaledX1 = float(compositeImage->x1 - image->x0) / float(image->width());
	  auto scaledY1 = float(compositeImage->y1 - image->y0) / float(image->height());
	  Logger::logger_.info("Region scaled coordinates : (%f,%f,%f,%f)", scaledX0, scaledY0,
						   scaledX1, scaledY1);
	  Logger::logger_.info("Region scaled coordinates in ROW-COLUMN format : \"{%f,%f},{%f,%f}\"",
						   scaledY0, scaledX0, scaledY1, scaledX1);
	  Logger::logger_.info("image canvas coordinates :  (%u,%u,%u,%u)", image->x0, image->y0,
						   image->x1, image->y1);
   }
   compositeImage->validateColourSpace();
   compositeImage->postReadHeader(&cp_);

   return true;
}
void CodeStreamDecompress::init(grk_decompress_core_params* parameters)
{
   assert(parameters);

   cp_.coding_params_.dec_.layers_to_decompress_ = parameters->layers_to_decompress_;
   cp_.coding_params_.dec_.reduce_ = parameters->reduce;
   cp_.coding_params_.dec_.randomAccessFlags_ = parameters->randomAccessFlags_;
   tileCache_->setStrategy(parameters->tileCacheStrategy);

   ioBufferCallback = parameters->io_buffer_callback;
   ioUserData = parameters->io_user_data;
   grkRegisterReclaimCallback_ = parameters->io_register_client_callback;
}
bool CodeStreamDecompress::decompress(grk_plugin_tile* tile)
{
   procedure_list_.push_back(std::bind(&CodeStreamDecompress::decompressTiles, this));
   current_plugin_tile = tile;

   return decompressExec();
}
bool CodeStreamDecompress::decompressTile(uint16_t tileIndex)
{
   // 1. check if tile has already been decompressed
   auto entry = tileCache_->get(tileIndex);
   if(entry && entry->processor && entry->processor->getImage())
	  return true;

   // 2. otherwise, decompress tile
   if(outputImage_)
   {
	  /* Copy code stream image information to composite image */
	  headerImage_->copyHeader(getCompositeImage());
   }
   uint16_t numTilesToDecompress = (uint16_t)(cp_.t_grid_width * cp_.t_grid_height);
   if(codeStreamInfo && !codeStreamInfo->allocTileInfo(numTilesToDecompress))
   {
	  headerError_ = true;
	  return false;
   }

   auto compositeImage = getCompositeImage();
   if(tileIndex >= numTilesToDecompress)
   {
	  Logger::logger_.error("Tile index %u is greater than maximum tile index %u", tileIndex,
							numTilesToDecompress - 1);
	  return false;
   }

   /* Compute the dimension of the desired tile*/
   uint32_t tile_x = tileIndex % cp_.t_grid_width;
   uint32_t tile_y = tileIndex / cp_.t_grid_width;

   auto imageBounds =
	   grk_rect32(compositeImage->x0, compositeImage->y0, compositeImage->x1, compositeImage->y1);
   auto tileBounds = cp_.getTileBounds(compositeImage, tile_x, tile_y);
   // crop tile bounds with image bounds
   auto croppedImageBounds = imageBounds.intersection(tileBounds);
   if(!imageBounds.empty() && !tileBounds.empty() && !croppedImageBounds.empty())
   {
	  compositeImage->x0 = (uint32_t)croppedImageBounds.x0;
	  compositeImage->y0 = (uint32_t)croppedImageBounds.y0;
	  compositeImage->x1 = (uint32_t)croppedImageBounds.x1;
	  compositeImage->y1 = (uint32_t)croppedImageBounds.y1;
   }
   else
   {
	  Logger::logger_.warn("Decompress bounds <%u,%u,%u,%u> do not overlap with requested tile %u. "
						   "Decompressing full image",
						   imageBounds.x0, imageBounds.y0, imageBounds.x1, imageBounds.y1,
						   tileIndex);
	  croppedImageBounds = imageBounds;
   }

   auto reduce = cp_.coding_params_.dec_.reduce_;
   for(uint16_t compno = 0; compno < compositeImage->numcomps; ++compno)
   {
	  auto comp = compositeImage->comps + compno;
	  auto compBounds = croppedImageBounds.scaleDownCeil(comp->dx, comp->dy);
	  auto reducedCompBounds = compBounds.scaleDownCeilPow2(reduce);
	  comp->x0 = reducedCompBounds.x0;
	  comp->y0 = reducedCompBounds.y0;
	  comp->w = reducedCompBounds.width();
	  comp->h = reducedCompBounds.height();
   }
   compositeImage->postReadHeader(&cp_);
   decompressorState_.tilesToDecompress_.schedule(tileIndex);

   // reset tile part numbers, in case we are re-using the same codec object
   // from previous decompress
   for(uint32_t i = 0; i < numTilesToDecompress; ++i)
	  cp_.tcps[i].tilePartCounter_ = 0;

   /* customization of the decoding */
   procedure_list_.push_back([this] { return decompressTile(); });

   return decompressExec();
}
bool CodeStreamDecompress::endOfCodeStream(void)
{
   return decompressorState_.getState() == DECOMPRESS_STATE_EOC ||
		  decompressorState_.getState() == DECOMPRESS_STATE_NO_EOC || stream_->numBytesLeft() == 0;
}
bool CodeStreamDecompress::decompressTiles(void)
{
   uint16_t numTilesToDecompress = (uint16_t)(cp_.t_grid_height * cp_.t_grid_width);
   if(codeStreamInfo && !codeStreamInfo->allocTileInfo(numTilesToDecompress))
   {
	  headerError_ = true;
	  return false;
   }
   if(!createOutputImage())
	  return false;

   auto numRequiredThreads =
	   std::min<uint32_t>((uint32_t)ExecSingleton::get().num_workers(), numTilesToDecompress);
   if(outputImage_->supportsStripCache(&cp_))
   {
	  uint32_t numStrips = cp_.t_grid_height;
	  if(numTilesToDecompress == 1)
	  {
		 numStrips =
			 (outputImage_->height() + outputImage_->rowsPerStrip - 1) / outputImage_->rowsPerStrip;
	  }
	  stripCache_.init((uint32_t)ExecSingleton::get().num_workers(), cp_.t_grid_width, numStrips,
					   numTilesToDecompress > 1 ? cp_.t_height : outputImage_->rowsPerStrip,
					   cp_.coding_params_.dec_.reduce_, outputImage_, ioBufferCallback, ioUserData,
					   grkRegisterReclaimCallback_);
   }

   std::atomic<bool> success(true);
   std::atomic<uint32_t> numTilesDecompressed(0);

   tf::Executor* executor = nullptr;
   tf::Task* node = nullptr;
   tf::Taskflow taskflow;
   if(numRequiredThreads > 1)
   {
	  executor = new tf::Executor(numRequiredThreads);
	  node = new tf::Task[numTilesToDecompress];
	  for(uint64_t i = 0; i < numTilesToDecompress; i++)
		 node[i] = taskflow.placeholder();
   }
   bool breakAfterT1 = false;
   bool canDecompress = true;
   uint16_t tileCount = 0;
   while(!endOfCodeStream() && !breakAfterT1)
   {
	  // 1. parse tile
	  try
	  {
		 if(!parseTileParts(&canDecompress))
		 {
			success = false;
			goto cleanup;
		 }
	  }
	  catch(const InvalidMarkerException& ime)
	  {
		 Logger::logger_.error("Found invalid marker : 0x%x", ime.marker_);
		 success = false;
		 goto cleanup;
	  }
	  if(!canDecompress)
		 continue;
	  if(!currentTileProcessor_)
	  {
		 Logger::logger_.error("Missing SOT marker");
		 success = false;
		 goto cleanup;
	  }
	  // 2. find next tile (or EOC)
	  auto processor = currentTileProcessor_;
	  currentTileProcessor_ = nullptr;
	  try
	  {
		 if(!endOfCodeStream() && !findNextSOT(processor))
		 {
			Logger::logger_.error("Failed to find next SOT marker or EOC after tile %u/%u",
								  processor->getIndex(), numTilesToDecompress);
			success = false;
			goto cleanup;
		 }
	  }
	  catch([[maybe_unused]] const DecodeUnknownMarkerAtEndOfTileException& e)
	  {
		 breakAfterT1 = true;
	  }
	  // 3. T2 + T1 decompress
	  // once we schedule a processor for T1 compression, we will destroy it
	  // regardless of success or not
	  auto exec = [this, executor, processor, numTilesToDecompress, &numTilesDecompressed,
				   &success] {
		 if(success)
		 {
			if(!processor->decompressT2T1(outputImage_))
			{
			   Logger::logger_.error("Failed to decompress tile %u/%u", processor->getIndex(),
									 numTilesToDecompress);
			   success = false;
			}
			else
			{
			   numTilesDecompressed++;
			   auto img = processor->getImage();
			   if(outputImage_->hasMultipleTiles && img)
			   {
				  if(outputImage_->supportsStripCache(&cp_))
				  {
					 if(executor)
					 {
						uint32_t threadId =
							executor->num_workers() > 1 ? (uint32_t)executor->this_worker_id() : 0;
						if(!stripCache_.ingestTile(threadId, img))
						   success = false;
					 }
					 else
					 {
						if(!stripCache_.ingestTile(img))
						   success = false;
					 }
				  }
				  else
				  {
					 if(!outputImage_->composite(img))
						success = false;
				  }
			   }
			   processor->release(success ? tileCache_->getStrategy() : GRK_TILE_CACHE_NONE);
			}
		 }
		 return 0;
	  };
	  if(node)
		 node[tileCount++].work(exec);
	  else
	  {
		 exec();
		 if(!success)
			goto cleanup;
	  }
	  if(decompressorState_.tilesToDecompress_.allComplete())
	  {
		 // check for corrupt Adobe files where 5 tile parts per tile are signaled
		 // but there are actually 6
		 if(curr_marker_ == J2K_MS_SOT)
		 {
			if(checkForIllegalTilePart())
			{
			   success = false;
			   goto cleanup;
			}
		 }
		 break;
	  }
   }
   if(executor)
   {
	  executor->run(taskflow).wait();
	  delete executor;
	  executor = nullptr;
	  delete[] node;
	  node = nullptr;
   }

   if(!success)
	  goto cleanup;

   if(numTilesDecompressed == 0)
   {
	  Logger::logger_.error("No tiles were decompressed.");
	  success = false;
	  goto cleanup;
   }
   else if(numTilesDecompressed < numTilesToDecompress && cp_.wholeTileDecompress_)
   {
	  uint32_t decompressed = numTilesDecompressed;
	  Logger::logger_.warn("Only %u out of %u tiles were decompressed", decompressed,
						   numTilesToDecompress);
   }
cleanup:
   if(executor)
   {
	  executor->run(taskflow).wait();
	  delete executor;
	  delete[] node;
   }
   return success;
}
bool CodeStreamDecompress::copy_default_tcp(void)
{
   for(uint16_t i = 0; i < cp_.t_grid_height * cp_.t_grid_width; ++i)
   {
	  auto tcp = cp_.tcps + i;
	  if(!tcp->copy(decompressorState_.default_tcp_, headerImage_))
		 return false;
   }

   return true;
}
void CodeStreamDecompress::addMarker(uint16_t id, uint64_t pos, uint32_t len)
{
   if(codeStreamInfo)
	  codeStreamInfo->pushMarker(id, pos, len);
}
uint16_t CodeStreamDecompress::getCurrentMarker()
{
   return curr_marker_;
}
GrkImage* CodeStreamDecompress::getHeaderImage(void)
{
   return headerImage_;
}
bool CodeStreamDecompress::readHeaderProcedure(void)
{
   bool rc = false;
   try
   {
	  rc = readHeaderProcedureImpl();
   }
   catch(const InvalidMarkerException& ime)
   {
	  Logger::logger_.error("Found invalid marker : 0x%x", ime.marker_);
	  rc = false;
   }
   return rc;
}
bool CodeStreamDecompress::readHeaderProcedureImpl(void)
{
   bool has_siz = false;
   bool has_cod = false;
   bool has_qcd = false;

   decompressorState_.setState(DECOMPRESS_STATE_MH_SOC);

   /* Try to read the SOC marker, the code stream must begin with SOC marker */
   if(!read_soc())
   {
	  Logger::logger_.error("Code stream must begin with SOC marker ");
	  return false;
   }
   if(!readMarker())
	  return false;

   if(curr_marker_ != J2K_MS_SIZ)
   {
	  Logger::logger_.error(
		  "Code-stream must contain a valid SIZ marker segment, immediately after the SOC "
		  "marker ");
	  return false;
   }

   /* read until first SOT is detected */
   while(curr_marker_ != J2K_MS_SOT)
   {
	  // 1. get handler handler
	  auto marker_handler = get_marker_handler(curr_marker_);
	  if(!marker_handler)
	  {
		 if(!read_unk())
			return false;
		 if(curr_marker_ == J2K_MS_SOT)
			break;
		 marker_handler = get_marker_handler(curr_marker_);
	  }
	  if(marker_handler->id == J2K_MS_SIZ)
		 has_siz = true;
	  else if(marker_handler->id == J2K_MS_COD)
		 has_cod = true;
	  else if(marker_handler->id == J2K_MS_QCD)
		 has_qcd = true;

	  /* Check if the marker is known and if it is in the correct location
	   * (main, tile, end of code stream)*/
	  if(!(decompressorState_.getState() & marker_handler->states))
	  {
		 Logger::logger_.error("Marker %u is not compliant with its position", curr_marker_);
		 return false;
	  }

	  // 2. read rest of marker segment
	  uint16_t markerParametersLength;
	  if(!read_short(&markerParametersLength))
		 return false;
	  else if(markerParametersLength == MARKER_LENGTH_BYTES)
	  {
		 Logger::logger_.error("Zero-size marker in header.");
		 return false;
	  }
	  markerParametersLength = (uint16_t)(markerParametersLength - MARKER_LENGTH_BYTES);

	  // 3. handle marker
	  if(!process_marker(marker_handler, markerParametersLength))
		 return false;

	  // 4. add the marker to code stream index
	  uint16_t markerSegmentLength = MARKER_PLUS_MARKER_LENGTH_BYTES + markerParametersLength;
	  addMarker(marker_handler->id, stream_->tell() - markerSegmentLength, markerSegmentLength);

	  // 5. read next marker
	  if(!readMarker())
		 return false;
   }
   if(!has_siz)
   {
	  Logger::logger_.error("required SIZ marker not found in main header");
	  return false;
   }
   else if(!has_cod)
   {
	  Logger::logger_.error("required COD marker not found in main header");
	  return false;
   }
   else if(!has_qcd)
   {
	  Logger::logger_.error("required QCD marker not found in main header");
	  return false;
   }
   if(!merge_ppm(&(cp_)))
   {
	  Logger::logger_.error("Failed to merge PPM data");
	  return false;
   }
   // subtract bytes for already-read SOT marker
   if(codeStreamInfo)
	  codeStreamInfo->setMainHeaderEnd(stream_->tell() - MARKER_BYTES);

   // rewind TLM marker if present
   if(cp_.tlm_markers)
	  cp_.tlm_markers->rewind();

   /* Next step: read a tile-part header */
   decompressorState_.setState(DECOMPRESS_STATE_TPH_SOT);

   return true;
}
bool CodeStreamDecompress::decompressExec(void)
{
   if(!exec(procedure_list_))
	  return false;

   // transfer output image to composite image
   outputImage_->transferDataTo(getCompositeImage());

   return true;
}

bool CodeStreamDecompress::createOutputImage(void)
{
   if(!headerImage_->hasMultipleTiles)
   {
	  if(outputImage_)
		 grk_object_unref(&outputImage_->obj);
	  outputImage_ = nullptr;
   }
   if(!outputImage_)
   {
	  outputImage_ = new GrkImage();
	  getCompositeImage()->copyHeader(outputImage_);
   }

   return outputImage_->supportsStripCache(&cp_) || outputImage_->allocCompositeData();
}
bool CodeStreamDecompress::hasTLM(void)
{
   return cp_.tlm_markers && cp_.tlm_markers->valid();
}
/***
 * Skip past non-scheduled tiles
 */
bool CodeStreamDecompress::skipNonScheduledTLM(CodingParams* cp)
{
   if(!hasTLM())
	  return false;

   cp_.tlm_markers->seek(&decompressorState_.tilesToDecompress_, cp, stream_);

   return true;
}

/*
 * Read and decompress one tile.
 */
bool CodeStreamDecompress::decompressTile(void)
{
   if(!createOutputImage())
	  return false;
   if(decompressorState_.tilesToDecompress_.numScheduled() != 1)
   {
	  Logger::logger_.error("decompressTile: Unable to decompress tile "
							"since first tile SOT has not been detected");
	  return false;
   }
   outputImage_->hasMultipleTiles = false;
   uint16_t tileIndex = decompressorState_.tilesToDecompress_.getSingle();
   auto tileCache = tileCache_->get(tileIndex);
   auto tileProcessor = tileCache ? tileCache->processor : nullptr;
   if(!tileCache || !tileCache->processor->getImage())
   {
	  // find first tile part
	  try
	  {
		 // try to skip non-scheduled tile parts using TLM marker if available
		 if(!skipNonScheduledTLM(&cp_))
		 {
			// otherwise skip non-scheduled by reading tile headers
			if(!codeStreamInfo->allocTileInfo((uint16_t)(cp_.t_grid_width * cp_.t_grid_height)))
			   return false;
			if(!codeStreamInfo->seekFirstTilePart(tileIndex))
			   return false;
		 }
	  }
	  catch(const CorruptTLMException& cte)
	  {
		 return false;
	  }
	  /* Special case if we have previously read the EOC marker
	   * (if the previous tile decompressed is the last ) */
	  if(decompressorState_.getState() == DECOMPRESS_STATE_EOC)
		 decompressorState_.setState(DECOMPRESS_STATE_TPH_SOT);

	  bool canDecompress = true;
	  try
	  {
		 if(!parseTileParts(&canDecompress))
			return false;
	  }
	  catch(const InvalidMarkerException& ime)
	  {
		 Logger::logger_.error("Found invalid marker : 0x%x", ime.marker_);
		 return false;
	  }
	  tileProcessor = currentTileProcessor_;
	  if(outputImage_->supportsStripCache(&cp_))
	  {
		 uint32_t numStrips =
			 (outputImage_->height() + outputImage_->rowsPerStrip - 1) / outputImage_->rowsPerStrip;
		 stripCache_.init((uint32_t)ExecSingleton::get().num_workers(), 1, numStrips,
						  outputImage_->rowsPerStrip, cp_.coding_params_.dec_.reduce_, outputImage_,
						  ioBufferCallback, ioUserData, grkRegisterReclaimCallback_);
	  }

	  if(!tileProcessor->decompressT2T1(outputImage_))
		 return false;

	  // check for corrupt Adobe images where a final tile part is not parsed
	  // due to incorrectly-signalled number of tile parts
	  try
	  {
		 if(readSOTorEOC() && curr_marker_ == J2K_MS_SOT)
		 {
			if(checkForIllegalTilePart())
			   return false;
		 }
	  }
	  catch(const InvalidMarkerException& ime)
	  {
		 Logger::logger_.error("Found invalid marker : 0x%x", ime.marker_);
		 return false;
	  }
   }

   return true;
}
bool CodeStreamDecompress::checkForIllegalTilePart(void)
{
   try
   {
	  uint16_t markerSize;
	  readCurrentMarkerBody(&markerSize);
   }
   catch(const CorruptSOTMarkerException& csme)
   {
	  return true;
   }

   return false;
}
bool CodeStreamDecompress::findNextSOT(TileProcessor* tileProcessor)
{
   if(!(decompressorState_.getState() & DECOMPRESS_STATE_DATA))
   {
	  Logger::logger_.error("no tile data.");
	  return false;
   }
   auto tcp = cp_.tcps + tileProcessor->getIndex();
   if(!tcp->compressedTileData_)
   {
	  Logger::logger_.error("Missing SOD marker");
	  return false;
   }
   // find next tile
   bool rc = true;
   bool doPost = !tileProcessor->current_plugin_tile ||
				 (tileProcessor->current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);
   if(doPost)
	  rc = decompressorState_.findNextSOT(this);

   return rc;
}
bool CodeStreamDecompress::decompressValidation(void)
{
   bool valid = true;
   valid &= (decompressorState_.getState() == DECOMPRESS_STATE_NONE);

   return valid;
}
bool CodeStreamDecompress::process_marker(const marker_handler* marker_handler,
										  uint16_t marker_size)
{
   if(!marker_scratch_)
   {
	  marker_scratch_ = new uint8_t[default_header_size];
	  marker_scratch_size_ = default_header_size;
   }
   if(marker_size > marker_scratch_size_)
   {
	  if(marker_size > stream_->numBytesLeft())
	  {
		 Logger::logger_.error("Marker size inconsistent with stream length");
		 return false;
	  }
	  delete[] marker_scratch_;
	  marker_scratch_ = new uint8_t[2 * marker_size];
	  marker_scratch_size_ = (uint16_t)(2U * marker_size);
   }
   if(stream_->read(marker_scratch_, marker_size) != marker_size)
   {
	  Logger::logger_.error("Stream too short");
	  return false;
   }

   return marker_handler->func(marker_scratch_, marker_size);
}
bool CodeStreamDecompress::read_short(uint16_t* val)
{
   uint8_t temp[sizeof(uint16_t)];
   if(stream_->read(temp, sizeof(uint16_t)) != sizeof(uint16_t))
	  return false;

   grk_read<uint16_t>(temp, val);

   return true;
}
const marker_handler* CodeStreamDecompress::get_marker_handler(uint16_t id)
{
   auto iter = marker_map.find(id);
   if(iter != marker_map.end())
	  return iter->second;
   else
   {
	  Logger::logger_.warn("Unknown marker 0x%02x detected.", id);
	  return nullptr;
   }
}
bool CodeStreamDecompress::readMarker(void)
{
   return readMarker(false);
}
bool CodeStreamDecompress::readMarker(bool suppressWarning)
{
   if(!read_short(&curr_marker_))
	  return false;

   if(expectSOD_ && curr_marker_ != J2K_MS_SOD)
	  throw InvalidMarkerException(curr_marker_);
   expectSOD_ = false;

   /* Check if the current marker ID is valid */
   if(curr_marker_ < 0xff00)
   {
	  if(!suppressWarning)
		 Logger::logger_.warn("marker ID 0x%.4x does not match JPEG 2000 marker format 0xffxx",
							  curr_marker_);
	  throw InvalidMarkerException(curr_marker_);
   }

   return true;
}
bool CodeStreamDecompress::preProcess(void)
{
   return true;
}
bool CodeStreamDecompress::postProcess(void)
{
   for(auto& img : getAllImages())
   {
	  if(!img->applyColour())
		 return false;
   }

   auto img = getCompositeImage();
   img->applyColourManagement();
   if(!img->convertToRGB(cp_.wholeTileDecompress_))
	  return false;
   if(!img->greyToRGB())
	  return false;
   img->convertPrecision();

   return img->execUpsample();
}

void CodeStreamDecompress::dump_tile_info(TileCodingParams* default_tile, uint32_t numcomps,
										  FILE* outputFileStream)
{
   if(default_tile)
   {
	  fprintf(outputFileStream, "\t default tile {\n");
	  fprintf(outputFileStream, "\t\t csty=%#x\n", default_tile->csty);
	  fprintf(outputFileStream, "\t\t prg=%#x\n", default_tile->prg);
	  fprintf(outputFileStream, "\t\t numlayers=%u\n", default_tile->max_layers_);
	  fprintf(outputFileStream, "\t\t mct=%x\n", default_tile->mct);

	  for(uint16_t compno = 0; compno < numcomps; compno++)
	  {
		 auto tccp = &(default_tile->tccps[compno]);
		 uint32_t resno;
		 uint32_t bandIndex, numBandWindows;

		 assert(tccp->numresolutions > 0);

		 /* coding style*/
		 fprintf(outputFileStream, "\t\t comp %u {\n", compno);
		 fprintf(outputFileStream, "\t\t\t csty=%#x\n", tccp->csty);
		 fprintf(outputFileStream, "\t\t\t numresolutions=%u\n", tccp->numresolutions);
		 fprintf(outputFileStream, "\t\t\t cblkw=2^%u\n", tccp->cblkw);
		 fprintf(outputFileStream, "\t\t\t cblkh=2^%u\n", tccp->cblkh);
		 fprintf(outputFileStream, "\t\t\t cblksty=%#x\n", tccp->cblk_sty);
		 fprintf(outputFileStream, "\t\t\t qmfbid=%u\n", tccp->qmfbid);

		 fprintf(outputFileStream, "\t\t\t preccintsize (w,h)=");
		 for(resno = 0; resno < tccp->numresolutions; resno++)
		 {
			fprintf(outputFileStream, "(%u,%u) ", tccp->precWidthExp[resno],
					tccp->precHeightExp[resno]);
		 }
		 fprintf(outputFileStream, "\n");

		 /* quantization style*/
		 fprintf(outputFileStream, "\t\t\t qntsty=%u\n", tccp->qntsty);
		 fprintf(outputFileStream, "\t\t\t numgbits=%u\n", tccp->numgbits);
		 fprintf(outputFileStream, "\t\t\t stepsizes (m,e)=");
		 numBandWindows =
			 (tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (uint32_t)(tccp->numresolutions * 3 - 2);
		 for(bandIndex = 0; bandIndex < numBandWindows; bandIndex++)
		 {
			fprintf(outputFileStream, "(%u,%u) ", tccp->stepsizes[bandIndex].mant,
					tccp->stepsizes[bandIndex].expn);
		 }
		 fprintf(outputFileStream, "\n");

		 /* RGN value*/
		 fprintf(outputFileStream, "\t\t\t roishift=%u\n", tccp->roishift);

		 fprintf(outputFileStream, "\t\t }\n");
	  } /*end of component of default tile*/
	  fprintf(outputFileStream, "\t }\n"); /*end of default tile*/
   }
}

void CodeStreamDecompress::dump(uint32_t flag, FILE* outputFileStream)
{
   /* Check if the flag is compatible with j2k file*/
   if((flag & GRK_JP2_INFO) || (flag & GRK_JP2_IND))
   {
	  fprintf(outputFileStream, "Wrong flag\n");
	  return;
   }

   /* Dump the image_header */
   if(flag & GRK_IMG_INFO)
   {
	  if(getHeaderImage())
		 dump_image_header(getHeaderImage(), 0, outputFileStream);
   }

   /* Dump the code stream info from main header */
   if(flag & GRK_J2K_MH_INFO)
   {
	  if(getHeaderImage())
		 dump_MH_info(outputFileStream);
   }
   /* Dump all tile/code stream info */
   auto cp = getCodingParams();
   if(flag & GRK_J2K_TCH_INFO)
   {
	  if(getHeaderImage())
	  {
		 uint32_t numTiles = cp->t_grid_height * cp->t_grid_width;
		 for(uint16_t i = 0; i < numTiles; ++i)
		 {
			auto tcp = cp->tcps + i;
			dump_tile_info(tcp, getHeaderImage()->numcomps, outputFileStream);
		 }
	  }
   }

   /* Dump the code stream index from main header */
   if((flag & GRK_J2K_MH_IND) && codeStreamInfo)
	  codeStreamInfo->dump(outputFileStream);
}
void CodeStreamDecompress::dump_MH_info(FILE* outputFileStream)
{
   fprintf(outputFileStream, "Codestream info from main header: {\n");
   fprintf(outputFileStream, "\t tx0=%u, ty0=%u\n", cp_.tx0, cp_.ty0);
   fprintf(outputFileStream, "\t tdx=%u, tdy=%u\n", cp_.t_width, cp_.t_height);
   fprintf(outputFileStream, "\t tw=%u, th=%u\n", cp_.t_grid_width, cp_.t_grid_height);
   CodeStreamDecompress::dump_tile_info(getDecompressorState()->default_tcp_,
										getHeaderImage()->numcomps, outputFileStream);
   fprintf(outputFileStream, "}\n");
}
void CodeStreamDecompress::dump_image_header(GrkImage* img_header, bool dev_dump_flag,
											 FILE* outputFileStream)
{
   char tab[2];
   if(dev_dump_flag)
   {
	  fprintf(stdout, "[DEV] Dump an image_header struct {\n");
	  tab[0] = '\0';
   }
   else
   {
	  fprintf(outputFileStream, "Image info {\n");
	  tab[0] = '\t';
	  tab[1] = '\0';
   }
   fprintf(outputFileStream, "%s x0=%u, y0=%u\n", tab, img_header->x0, img_header->y0);
   fprintf(outputFileStream, "%s x1=%u, y1=%u\n", tab, img_header->x1, img_header->y1);
   fprintf(outputFileStream, "%s numcomps=%u\n", tab, img_header->numcomps);
   if(img_header->comps)
   {
	  uint16_t compno;
	  for(compno = 0; compno < img_header->numcomps; compno++)
	  {
		 fprintf(outputFileStream, "%s\t component %u {\n", tab, compno);
		 CodeStreamDecompress::dump_image_comp_header(&(img_header->comps[compno]), dev_dump_flag,
													  outputFileStream);
		 fprintf(outputFileStream, "%s}\n", tab);
	  }
   }
   fprintf(outputFileStream, "}\n");
}
void CodeStreamDecompress::dump_image_comp_header(grk_image_comp* comp_header, bool dev_dump_flag,
												  FILE* outputFileStream)
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

   fprintf(outputFileStream, "%s dx=%u, dy=%u\n", tab, comp_header->dx, comp_header->dy);
   fprintf(outputFileStream, "%s prec=%u\n", tab, comp_header->prec);
   fprintf(outputFileStream, "%s sgnd=%u\n", tab, comp_header->sgnd ? 1 : 0);

   if(dev_dump_flag)
	  fprintf(outputFileStream, "}\n");
}

} // namespace grk
