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
static void j2k_write_float_to_int16(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
   j2k_write<float, int16_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
   j2k_write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
   j2k_write<float, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_float64(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
   j2k_write<float, double>(p_src_data, p_dest_data, nb_elem);
}

static const j2k_mct_function j2k_mct_write_functions_from_float[] = {
	j2k_write_float_to_int16, j2k_write_float_to_int32, j2k_write_float_to_float,
	j2k_write_float_to_float64};

struct j2k_prog_order
{
   GRK_PROG_ORDER enum_prog;
   char str_prog[5];
};

static j2k_prog_order j2k_prog_order_list[] = {{GRK_CPRL, "CPRL"}, {GRK_LRCP, "LRCP"},
											   {GRK_PCRL, "PCRL"}, {GRK_RLCP, "RLCP"},
											   {GRK_RPCL, "RPCL"}, {(GRK_PROG_ORDER)-1, ""}};

CodeStreamCompress::CodeStreamCompress(BufferedStream* stream) : CodeStream(stream)
{
   cp_.wholeTileDecompress_ = false;
}

CodeStreamCompress::~CodeStreamCompress() {}
char* CodeStreamCompress::convertProgressionOrder(GRK_PROG_ORDER prg_order)
{
   j2k_prog_order* po;
   for(po = j2k_prog_order_list; po->enum_prog != -1; po++)
   {
	  if(po->enum_prog == prg_order)
		 return (char*)po->str_prog;
   }
   return po->str_prog;
}
bool CodeStreamCompress::mct_validation(void)
{
   bool valid = true;
   if((cp_.rsiz & 0x8200) == 0x8200)
   {
	  uint32_t numTiles = cp_.t_grid_height * cp_.t_grid_width;
	  for(uint32_t i = 0; i < numTiles; ++i)
	  {
		 auto tcp = cp_.tcps + i;
		 if(tcp->mct == 2)
		 {
			valid &= (tcp->mct_coding_matrix_ != nullptr);
			for(uint32_t j = 0; j < headerImage_->numcomps; ++j)
			{
			   auto tccp = tcp->tccps + j;
			   valid &= !(tccp->qmfbid & 1);
			}
		 }
	  }
   }

   return valid;
}

bool CodeStreamCompress::start(void)
{
   /* customization of the validation */
   validation_list_.push_back(std::bind(&CodeStreamCompress::compressValidation, this));
   // custom validation here
   validation_list_.push_back(std::bind(&CodeStreamCompress::mct_validation, this));

   /* validation of the parameters codec */
   if(!exec(validation_list_))
	  return false;

   /* customization of the compressing */
   if(!init_header_writing())
	  return false;

   /* write header */
   return exec(procedure_list_);
}
bool CodeStreamCompress::init(grk_cparameters* parameters, GrkImage* image)
{
   if(!parameters || !image)
	  return false;

   bool isHT = (parameters->cblk_sty & 0X7F) == GRK_CBLKSTY_HT;

   // sanity check on image
   if(image->numcomps < 1 || image->numcomps > maxNumComponentsJ2K)
   {
	  Logger::logger_.error(
		  "Invalid number of components specified while setting up JP2 compressor");
	  return false;
   }
   if((image->x1 < image->x0) || (image->y1 < image->y0))
   {
	  Logger::logger_.error("Invalid input image dimensions found while setting up JP2 compressor");
	  return false;
   }
   for(uint32_t i = 0; i < image->numcomps; ++i)
   {
	  auto comp = image->comps + i;
#ifdef GRK_FORCE_SIGNED_COMPRESS
	  comp->sgnd = true;
#endif
	  if(comp->w == 0 || comp->h == 0)
	  {
		 Logger::logger_.error(
			 "Invalid input image component dimensions found while setting up JP2 compressor");
		 return false;
	  }
	  if(comp->prec == 0)
	  {
		 Logger::logger_.error(
			 "Invalid component precision of 0 found while setting up JP2 compressor");
		 return false;
	  }
   }
   if(parameters->apply_icc_)
	  image->applyICC();

   // create private sanitized copy of image
   headerImage_ = new GrkImage();
   image->copyHeader(headerImage_);
   if(image->comps)
   {
	  for(uint16_t compno = 0; compno < image->numcomps; compno++)
	  {
		 if(image->comps[compno].data)
		 {
			headerImage_->comps[compno].data = image->comps[compno].data;
			image->comps[compno].data = nullptr;
		 }
	  }
   }

   if(isHT)
   {
	  if(parameters->numlayers > 1 || parameters->layer_rate[0] != 0)
	  {
		 Logger::logger_.warn("Rate control not supported for HTJ2K compression.");
		 parameters->numlayers = 1;
		 parameters->layer_rate[0] = 0;
	  }
	  parameters->allocation_by_rate_distortion = true;
   }

   if((parameters->numresolution == 0) || (parameters->numresolution > GRK_MAXRLVLS))
   {
	  Logger::logger_.error("Invalid number of resolutions : %u not in range [1,%u]",
							parameters->numresolution, GRK_MAXRLVLS);
	  return false;
   }

   if(GRK_IS_IMF(parameters->rsiz) && parameters->max_cs_size > 0 && parameters->numlayers == 1 &&
	  parameters->layer_rate[0] == 0)
   {
	  parameters->layer_rate[0] =
		  (float)(image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec) /
		  (float)(((uint32_t)parameters->max_cs_size) * 8 * image->comps[0].dx *
				  image->comps[0].dy);
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
		 Logger::logger_.warn("The desired maximum code stream size has limited");
		 Logger::logger_.warn("at least one of the desired quality layers");
	  }
   }

   /* Manage profiles and applications and set RSIZ */
   /* set cinema parameters if required */
   if(isHT)
	  parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
   if(GRK_IS_CINEMA(parameters->rsiz))
   {
	  if((parameters->rsiz == GRK_PROFILE_CINEMA_S2K) ||
		 (parameters->rsiz == GRK_PROFILE_CINEMA_S4K))
	  {
		 Logger::logger_.warn("JPEG 2000 Scalable Digital Cinema profiles not supported");
		 parameters->rsiz = GRK_PROFILE_NONE;
	  }
	  else
	  {
		 if(Profile::is_cinema_compliant(image, parameters->rsiz))
			Profile::set_cinema_parameters(parameters, image);
		 else
			parameters->rsiz = GRK_PROFILE_NONE;
	  }
   }
   else if(GRK_IS_STORAGE(parameters->rsiz))
   {
	  Logger::logger_.warn("JPEG 2000 Long Term Storage profile not supported");
	  parameters->rsiz = GRK_PROFILE_NONE;
   }
   else if(GRK_IS_BROADCAST(parameters->rsiz))
   {
	  Profile::set_broadcast_parameters(parameters);
	  if(!Profile::is_broadcast_compliant(parameters, image))
		 parameters->rsiz = GRK_PROFILE_NONE;
   }
   else if(GRK_IS_IMF(parameters->rsiz))
   {
	  Profile::set_imf_parameters(parameters, image);
	  if(!Profile::is_imf_compliant(parameters, image))
		 parameters->rsiz = GRK_PROFILE_NONE;
   }
   else if(GRK_IS_PART2(parameters->rsiz))
   {
	  if(parameters->rsiz == ((GRK_PROFILE_PART2) | (GRK_EXTENSION_NONE)))
	  {
		 Logger::logger_.warn("JPEG 2000 Part-2 profile defined\n"
							  "but no Part-2 extension enabled.\n"
							  "Profile set to NONE.");
		 parameters->rsiz = GRK_PROFILE_NONE;
	  }
	  else if(parameters->rsiz != ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT)))
	  {
		 Logger::logger_.warn("Unsupported Part-2 extension enabled\n"
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
		 Logger::logger_.error("Failed to initialize POC");
		 return false;
	  }
   }
   /* set default values for cp_ */
   cp_.t_grid_width = 1;
   cp_.t_grid_height = 1;

   cp_.coding_params_.enc_.max_comp_size_ = parameters->max_comp_size;
   cp_.rsiz = parameters->rsiz;
   cp_.coding_params_.enc_.allocationByRateDistortion_ = parameters->allocation_by_rate_distortion;
   cp_.coding_params_.enc_.allocationByFixedQuality_ = parameters->allocation_by_quality;
   cp_.coding_params_.enc_.write_plt = parameters->write_plt;
   cp_.coding_params_.enc_.write_tlm = parameters->write_tlm;
   cp_.coding_params_.enc_.rate_control_algorithm = parameters->rate_control_algorithm;

   /* tiles */
   cp_.t_width = parameters->t_width;
   cp_.t_height = parameters->t_height;

   /* tile offset */
   cp_.tx0 = parameters->tx0;
   cp_.ty0 = parameters->ty0;

   /* comment string */
   if(parameters->num_comments)
   {
	  for(size_t i = 0; i < parameters->num_comments; ++i)
	  {
		 cp_.comment_len[i] = parameters->comment_len[i];
		 if(!cp_.comment_len[i])
		 {
			Logger::logger_.warn("Empty comment. Ignoring");
			continue;
		 }
		 if(cp_.comment_len[i] > GRK_MAX_COMMENT_LENGTH)
		 {
			Logger::logger_.warn(
				"Comment length %s is greater than maximum comment length %u. Ignoring",
				cp_.comment_len[i], GRK_MAX_COMMENT_LENGTH);
			continue;
		 }
		 cp_.is_binary_comment[i] = parameters->is_binary_comment[i];
		 cp_.comment[i] = new char[cp_.comment_len[i]];
		 memcpy(cp_.comment[i], parameters->comment[i], cp_.comment_len[i]);
		 cp_.num_comments++;
	  }
   }
   else
   {
	  /* Create default comment for code stream */
	  auto comment = std::string("Created by Grok version ") + grk_version();
	  auto comment_len = comment.size();
	  cp_.comment[0] = new char[comment_len];
	  memcpy(cp_.comment[0], comment.c_str(), comment_len);
	  cp_.comment_len[0] = (uint16_t)comment_len;
	  cp_.num_comments = 1;
	  cp_.is_binary_comment[0] = false;
   }
   if(parameters->tile_size_on)
   {
	  // avoid divide by zero
	  if(cp_.t_width == 0 || cp_.t_height == 0)
	  {
		 Logger::logger_.error("Invalid tile dimensions (%u,%u)", cp_.t_width, cp_.t_height);
		 return false;
	  }
	  uint32_t tgw = ceildiv<uint32_t>((image->x1 - cp_.tx0), cp_.t_width);
	  uint32_t tgh = ceildiv<uint32_t>((image->y1 - cp_.ty0), cp_.t_height);
	  uint64_t numTiles = (uint64_t)tgw * tgh;
	  if(numTiles > maxNumTilesJ2K)
	  {
		 Logger::logger_.error("Number of tiles %u is greater than max tiles %u"
							   "allowed by the standard.",
							   numTiles, maxNumTilesJ2K);
		 return false;
	  }
	  cp_.t_grid_width = (uint16_t)tgw;
	  cp_.t_grid_height = (uint16_t)tgh;
   }
   else
   {
	  cp_.t_width = image->x1 - cp_.tx0;
	  cp_.t_height = image->y1 - cp_.ty0;
   }
   if(parameters->enable_tile_part_generation)
   {
	  cp_.coding_params_.enc_.newTilePartProgressionDivider_ =
		  parameters->new_tile_part_progression_divider;
	  cp_.coding_params_.enc_.enableTilePartGeneration_ = true;
   }
   uint8_t numgbits = parameters->numgbits;
   if(parameters->numgbits > 7)
   {
	  Logger::logger_.error("Number of guard bits %u is greater than 7", numgbits);
	  return false;
   }
   cp_.tcps = new TileCodingParams[cp_.t_grid_width * cp_.t_grid_height];
   for(uint16_t tileno = 0; tileno < (uint16_t)(cp_.t_grid_width * cp_.t_grid_height); tileno++)
   {
	  auto tcp = cp_.tcps + tileno;
	  tcp->tccps = new TileComponentCodingParams[image->numcomps];

	  tcp->setIsHT(isHT, !parameters->irreversible, numgbits);
	  tcp->qcd_->generate((uint32_t)(parameters->numresolution - 1), image->comps[0].prec,
						  parameters->mct > 0, image->comps[0].sgnd);
	  for(uint32_t i = 0; i < image->numcomps; i++)
		 tcp->qcd_->pull((tcp->tccps + i)->stepsizes);

	  tcp->max_layers_ = parameters->numlayers;
	  for(uint16_t j = 0; j < tcp->max_layers_; j++)
	  {
		 if(cp_.coding_params_.enc_.allocationByFixedQuality_)
			tcp->distortion[j] = parameters->layer_distortion[j];
		 else
			tcp->rates[j] = parameters->layer_rate[j];
	  }
	  tcp->csty = parameters->csty;
	  tcp->prg = parameters->prog_order;
	  tcp->mct = parameters->mct;
	  if(parameters->numpocs)
	  {
		 uint32_t numTileProgressions = 0;
		 for(uint32_t i = 0; i < parameters->numpocs + 1; i++)
		 {
			if(tileno == parameters->progression[i].tileno)
			{
			   auto tcp_poc = tcp->progressionOrderChange + numTileProgressions;

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
			Logger::logger_.error("Problem with specified progression order changes");
			return false;
		 }
		 tcp->numpocs = numTileProgressions - 1;
	  }
	  else
	  {
		 tcp->numpocs = 0;
	  }
	  if(parameters->mct_data)
	  {
		 uint64_t lMctSize = (uint64_t)image->numcomps * image->numcomps * sizeof(float);
		 auto lTmpBuf = (float*)grk_malloc(lMctSize);
		 auto dc_shift = (int32_t*)((uint8_t*)parameters->mct_data + lMctSize);
		 if(!lTmpBuf)
		 {
			Logger::logger_.error("Not enough memory to allocate temp buffer");
			return false;
		 }
		 tcp->mct = 2;
		 tcp->mct_coding_matrix_ = (float*)grk_malloc(lMctSize);
		 if(!tcp->mct_coding_matrix_)
		 {
			grk_free(lTmpBuf);
			lTmpBuf = nullptr;
			Logger::logger_.error("Not enough memory to allocate compressor MCT coding matrix ");
			return false;
		 }
		 memcpy(tcp->mct_coding_matrix_, parameters->mct_data, lMctSize);
		 memcpy(lTmpBuf, parameters->mct_data, lMctSize);

		 tcp->mct_decoding_matrix_ = (float*)grk_malloc(lMctSize);
		 if(!tcp->mct_decoding_matrix_)
		 {
			grk_free(lTmpBuf);
			lTmpBuf = nullptr;
			Logger::logger_.error("Not enough memory to allocate compressor MCT decoding matrix ");
			return false;
		 }
		 if(GrkMatrix().matrix_inversion_f(lTmpBuf, (tcp->mct_decoding_matrix_), image->numcomps) ==
			false)
		 {
			grk_free(lTmpBuf);
			lTmpBuf = nullptr;
			Logger::logger_.error("Failed to inverse compressor MCT decoding matrix ");
			return false;
		 }

		 tcp->mct_norms = (double*)grk_malloc(image->numcomps * sizeof(double));
		 if(!tcp->mct_norms)
		 {
			grk_free(lTmpBuf);
			lTmpBuf = nullptr;
			Logger::logger_.error("Not enough memory to allocate compressor MCT norms ");
			return false;
		 }
		 mct::calculate_norms(tcp->mct_norms, image->numcomps, tcp->mct_decoding_matrix_);
		 grk_free(lTmpBuf);

		 for(uint32_t i = 0; i < image->numcomps; i++)
		 {
			auto tccp = &tcp->tccps[i];
			tccp->dc_level_shift_ = dc_shift[i];
		 }

		 if(init_mct_encoding(tcp, image) == false)
		 {
			Logger::logger_.error("Failed to set up j2k mct compressing");
			return false;
		 }
	  }
	  else
	  {
		 if(tcp->mct == 1)
		 {
			if(image->color_space == GRK_CLRSPC_EYCC || image->color_space == GRK_CLRSPC_SYCC)
			{
			   Logger::logger_.warn("Disabling MCT for sYCC/eYCC colour space");
			   tcp->mct = 0;
			}
			else if(image->numcomps >= 3)
			{
			   if((image->comps[0].dx != image->comps[1].dx) ||
				  (image->comps[0].dx != image->comps[2].dx) ||
				  (image->comps[0].dy != image->comps[1].dy) ||
				  (image->comps[0].dy != image->comps[2].dy))
			   {
				  Logger::logger_.warn(
					  "Cannot perform MCT on components with different dimensions. "
					  "Disabling MCT.");
				  tcp->mct = 0;
			   }
			}
		 }
		 for(uint32_t i = 0; i < image->numcomps; i++)
		 {
			auto tccp = tcp->tccps + i;
			auto comp = image->comps + i;
			if(!comp->sgnd)
			   tccp->dc_level_shift_ = 1 << (comp->prec - 1);
		 }
	  }

	  for(uint32_t i = 0; i < image->numcomps; i++)
	  {
		 auto tccp = &tcp->tccps[i];

		 /* 0 => one precinct || 1 => custom precinct  */
		 tccp->csty = parameters->csty & J2K_CP_CSTY_PRT;
		 tccp->numresolutions = parameters->numresolution;
		 tccp->cblkw = floorlog2(parameters->cblockw_init);
		 tccp->cblkh = floorlog2(parameters->cblockh_init);
		 tccp->cblk_sty = parameters->cblk_sty;
		 tccp->qmfbid = parameters->irreversible ? 0 : 1;
		 tccp->qntsty = parameters->irreversible ? J2K_CCP_QNTSTY_SEQNT : J2K_CCP_QNTSTY_NOQNT;
		 tccp->numgbits = numgbits;
		 if((int32_t)i == parameters->roi_compno)
			tccp->roishift = (uint8_t)parameters->roi_shift;
		 else
			tccp->roishift = 0;
		 if((parameters->csty & J2K_CCP_CSTY_PRT) && parameters->res_spec)
		 {
			uint32_t p = 0;
			int32_t it_res;
			assert(tccp->numresolutions > 0);
			for(it_res = (int32_t)tccp->numresolutions - 1; it_res >= 0; it_res--)
			{
			   if(p < parameters->res_spec)
			   {
				  if(parameters->prcw_init[p] < 1)
					 tccp->precWidthExp[it_res] = 1;
				  else
					 tccp->precWidthExp[it_res] = floorlog2(parameters->prcw_init[p]);
				  if(parameters->prch_init[p] < 1)
					 tccp->precHeightExp[it_res] = 1;
				  else
					 tccp->precHeightExp[it_res] = floorlog2(parameters->prch_init[p]);
			   }
			   else
			   {
				  uint32_t res_spec = parameters->res_spec;
				  uint32_t size_prcw = 0;
				  uint32_t size_prch = 0;
				  size_prcw = parameters->prcw_init[res_spec - 1] >> (p - (res_spec - 1));
				  size_prch = parameters->prch_init[res_spec - 1] >> (p - (res_spec - 1));
				  if(size_prcw < 1)
					 tccp->precWidthExp[it_res] = 1;
				  else
					 tccp->precWidthExp[it_res] = floorlog2(size_prcw);
				  if(size_prch < 1)
					 tccp->precHeightExp[it_res] = 1;
				  else
					 tccp->precHeightExp[it_res] = floorlog2(size_prch);
			   }
			   p++;
			   /*printf("\nsize precinct for level %u : %u,%u\n",
				* it_res,tccp->precWidthExp[it_res], tccp->precHeightExp[it_res]); */
			} /*end for*/
		 }
		 else
		 {
			for(uint32_t j = 0; j < tccp->numresolutions; j++)
			{
			   tccp->precWidthExp[j] = 15;
			   tccp->precHeightExp[j] = 15;
			}
		 }
	  }
   }
   grk_free(parameters->mct_data);
   parameters->mct_data = nullptr;

   return true;
}
uint64_t CodeStreamCompress::compress(grk_plugin_tile* tile)
{
   MinHeapPtr<TileProcessor, uint16_t, MinHeapLocker> heap;
   uint32_t numTiles = (uint32_t)cp_.t_grid_height * cp_.t_grid_width;
   if(numTiles > maxNumTilesJ2K)
   {
	  Logger::logger_.error("Number of tiles %u is greater than max tiles %u"
							"allowed by the standard.",
							numTiles, maxNumTilesJ2K);
	  return 0;
   }
   auto numRequiredThreads =
	   std::min<uint32_t>((uint32_t)ExecSingleton::get().num_workers(), numTiles);
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
		 node[j].work([this, tile, tile_index, &heap, &success] {
			if(success)
			{
			   auto tileProcessor = new TileProcessor(tile_index, this, stream_, true, nullptr);
			   tileProcessor->current_plugin_tile = tile;
			   if(!tileProcessor->preCompressTile() || !tileProcessor->doCompress())
				  success = false;
			   heap.push(tileProcessor);
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
		 auto tileProcessor = new TileProcessor(i, this, stream_, true, nullptr);
		 tileProcessor->current_plugin_tile = tile;
		 if(!tileProcessor->preCompressTile() || !tileProcessor->doCompress())
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
   auto completeTileProcessor = heap.pop();
   while(completeTileProcessor)
   {
	  if(success)
	  {
		 if(!writeTileParts(completeTileProcessor))
			success = false;
	  }
	  delete completeTileProcessor;
	  completeTileProcessor = heap.pop();
   }
   if(success)
	  success = end();

   return success ? stream_->tell() : 0;
}
bool CodeStreamCompress::end(void)
{
   /* customization of the compressing */
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_eoc, this));
   if(cp_.coding_params_.enc_.write_tlm)
	  procedure_list_.push_back(std::bind(&CodeStreamCompress::write_tlm_end, this));

   return exec(procedure_list_);
}
///////////////////////////////////////////////////////////////////////////////////////////
bool CodeStreamCompress::write_rgn(uint16_t tile_no, uint32_t comp_no, uint32_t nb_comps)
{
   uint32_t rgn_size;
   auto cp = &(cp_);
   auto tcp = &cp->tcps[tile_no];
   auto tccp = &tcp->tccps[comp_no];
   uint32_t comp_room = (nb_comps <= 256) ? 1 : 2;
   rgn_size = 6 + comp_room;

   /* RGN  */
   if(!stream_->writeShort(J2K_RGN))
	  return false;
   /* Lrgn */
   if(!stream_->writeShort((uint16_t)(rgn_size - 2)))
	  return false;
   /* Crgn */
   if(comp_room == 2)
   {
	  if(!stream_->writeShort((uint16_t)comp_no))
		 return false;
   }
   else
   {
	  if(!stream_->writeByte((uint8_t)comp_no))
		 return false;
   }
   /* Srgn */
   if(!stream_->writeByte(0))
	  return false;

   /* SPrgn */
   return stream_->writeByte((uint8_t)tccp->roishift);
}

bool CodeStreamCompress::write_eoc()
{
   if(!stream_->writeShort(J2K_EOC))
	  return false;

   return stream_->flush();
}
bool CodeStreamCompress::write_mct_record(grk_mct_data* p_mct_record, BufferedStream* stream)
{
   uint32_t mct_size;
   uint32_t tmp;

   mct_size = 10 + p_mct_record->data_size_;

   /* MCT */
   if(!stream->writeShort(J2K_MCT))
	  return false;
   /* Lmct */
   if(!stream->writeShort((uint16_t)(mct_size - 2)))
	  return false;
   /* Zmct */
   if(!stream->writeShort(0))
	  return false;
   /* only one marker atm */
   tmp = (p_mct_record->index_ & 0xff) | (uint32_t)(p_mct_record->array_type_ << 8) |
		 (uint32_t)(p_mct_record->element_type_ << 10);

   if(!stream->writeShort((uint16_t)tmp))
	  return false;

   /* Ymct */
   if(!stream->writeShort(0))
	  return false;

   return stream->writeBytes(p_mct_record->data_, p_mct_record->data_size_);
}
bool CodeStreamCompress::cacheEndOfHeader(void)
{
   codeStreamInfo->setMainHeaderEnd(stream_->tell());

   return true;
}
bool CodeStreamCompress::init_header_writing(void)
{
   procedure_list_.push_back(
	   [this] { return getNumTileParts(&compressorState_.total_tile_parts_, getHeaderImage()); });

   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_soc, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_siz, this));
   if(cp_.tcps[0].isHT())
	  procedure_list_.push_back(std::bind(&CodeStreamCompress::write_cap, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_cod, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_qcd, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_all_coc, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_all_qcc, this));

   if(cp_.coding_params_.enc_.write_tlm)
	  procedure_list_.push_back(std::bind(&CodeStreamCompress::write_tlm_begin, this));
   if(cp_.tcps->hasPoc())
	  procedure_list_.push_back(std::bind(&CodeStreamCompress::writePoc, this));

   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_regions, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::write_com, this));
   // begin custom procedures
   if((cp_.rsiz & (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT)) ==
	  (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT))
	  procedure_list_.push_back(std::bind(&CodeStreamCompress::write_mct_data_group, this));
   // end custom procedures

   if(codeStreamInfo)
	  procedure_list_.push_back(std::bind(&CodeStreamCompress::cacheEndOfHeader, this));
   procedure_list_.push_back(std::bind(&CodeStreamCompress::updateRates, this));

   return true;
}
bool CodeStreamCompress::writeTilePart(TileProcessor* tileProcessor)
{
   uint64_t currentPos = 0;
   if(tileProcessor->canPreCalculateTileLen())
	  currentPos = stream_->tell();
   uint16_t currentTileIndex = tileProcessor->getIndex();
   auto calculatedBytesWritten = tileProcessor->getPreCalculatedTileLen();
   // 1. write SOT
   SOTMarker sot;
   if(!sot.write(tileProcessor, calculatedBytesWritten))
	  return false;
   uint32_t tilePartBytesWritten = sot_marker_segment_len_minus_tile_data_len;
   // 2. write POC marker to first tile part
   if(tileProcessor->canWritePocMarker())
   {
	  if(!writePoc())
		 return false;
	  auto tcp = cp_.tcps + currentTileIndex;
	  tilePartBytesWritten += getPocSize(headerImage_->numcomps, tcp->getNumProgressions());
   }
   // 3. compress tile part and write to stream
   if(!tileProcessor->writeTilePartT2(&tilePartBytesWritten))
   {
	  Logger::logger_.error("Cannot compress tile");
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
	  // Logger::logger_.info("Tile %u: precalculated / actual : %u / %u",
	  //		tileProcessor->getIndex(), calculatedBytesWritten, actualBytes);
	  if(actualBytes != calculatedBytesWritten)
	  {
		 Logger::logger_.error("Error in tile length calculation. Please share uncompressed image\n"
							   "and compression parameters on Github issue tracker");
		 return false;
	  }
	  tilePartBytesWritten = calculatedBytesWritten;
   }
   if(cp_.tlm_markers)
	  cp_.tlm_markers->push(currentTileIndex, tilePartBytesWritten);
   ++tileProcessor->tilePartCounter_;

   return true;
}
bool CodeStreamCompress::writeTileParts(TileProcessor* tileProcessor)
{
   currentTileProcessor_ = tileProcessor;
   assert(tileProcessor->tilePartCounter_ == 0);
   // 1. write first tile part
   tileProcessor->pino = 0;
   tileProcessor->first_poc_tile_part_ = true;
   if(!writeTilePart(tileProcessor))
	  return false;
   // 2. write the other tile parts
   uint32_t pino;
   auto tcp = cp_.tcps + tileProcessor->getIndex();
   // write tile parts for first progression order
   uint64_t numTileParts = getNumTilePartsForProgression(0, tileProcessor->getIndex());
   if(numTileParts > maxTilePartsPerTileJ2K)
   {
	  Logger::logger_.error(
		  "Number of tile parts %u for first POC exceeds maximum number of tile parts %u",
		  numTileParts, maxTilePartsPerTileJ2K);
	  return false;
   }
   tileProcessor->first_poc_tile_part_ = false;
   for(uint8_t tilepartno = 1; tilepartno < (uint8_t)numTileParts; ++tilepartno)
   {
	  if(!writeTilePart(tileProcessor))
		 return false;
   }
   // write tile parts for remaining progression orders
   for(pino = 1; pino < tcp->getNumProgressions(); ++pino)
   {
	  tileProcessor->pino = pino;
	  numTileParts = getNumTilePartsForProgression(pino, tileProcessor->getIndex());
	  if(numTileParts > maxTilePartsPerTileJ2K)
	  {
		 Logger::logger_.error("Number of tile parts %u exceeds maximum number of "
							   "tile parts %u",
							   numTileParts, maxTilePartsPerTileJ2K);
		 return false;
	  }
	  for(uint8_t tilepartno = 0; tilepartno < numTileParts; ++tilepartno)
	  {
		 tileProcessor->first_poc_tile_part_ = (tilepartno == 0);
		 if(!writeTilePart(tileProcessor))
			return false;
	  }
   }
   tileProcessor->incrementIndex();

   return true;
}
bool CodeStreamCompress::updateRates(void)
{
   auto cp = &(cp_);
   auto image = headerImage_;
   auto width = image->x1 - image->x0;
   auto height = image->y1 - image->y0;
   if(width <= 0 || height <= 0)
	  return false;

   uint32_t bits_empty = 8 * (uint32_t)image->comps->dx * image->comps->dy;
   uint32_t size_pixel = (uint32_t)image->numcomps * image->comps->prec;
   auto header_size = (double)stream_->tell();

   for(uint32_t tile_y = 0; tile_y < cp->t_grid_height; ++tile_y)
   {
	  for(uint32_t tile_x = 0; tile_x < cp->t_grid_width; ++tile_x)
	  {
		 uint32_t tileId = tile_y * cp->t_grid_width + tile_x;
		 auto tcp = cp->tcps + tileId;
		 double stride = 0;
		 if(cp->coding_params_.enc_.enableTilePartGeneration_)
			stride = (tcp->numTileParts_ - 1) * 14;
		 double offset = stride / tcp->max_layers_;
		 auto tileBounds = cp->getTileBounds(image, tile_x, tile_y);
		 uint64_t numTilePixels = tileBounds.area();
		 for(uint16_t k = 0; k < tcp->max_layers_; ++k)
		 {
			double* rates = tcp->rates + k;
			// convert to target bytes for layer
			if(*rates > 0.0f)
			   *rates = ((((double)size_pixel * (double)numTilePixels)) /
						 ((double)*rates * (double)bits_empty)) -
						offset;
		 }
	  }
   }
   for(uint32_t tile_y = 0; tile_y < cp->t_grid_height; ++tile_y)
   {
	  for(uint32_t tile_x = 0; tile_x < cp->t_grid_width; ++tile_x)
	  {
		 uint32_t tileId = tile_y * cp->t_grid_width + tile_x;
		 auto tcp = cp->tcps + tileId;
		 double* rates = tcp->rates;
		 auto tileBounds = cp->getTileBounds(image, tile_x, tile_y);
		 uint64_t numTilePixels = tileBounds.area();
		 // correction for header size is distributed amongst all tiles
		 double sot_adjust =
			 ((double)numTilePixels * (double)header_size) / ((double)width * height);
		 if(*rates > 0.0)
		 {
			*rates -= sot_adjust;
			if(*rates < 30.0f)
			   *rates = 30.0f;
		 }
		 ++rates;
		 for(uint16_t k = 1; k < (uint16_t)(tcp->max_layers_ - 1); ++k)
		 {
			if(*rates > 0.0)
			{
			   *rates -= sot_adjust;
			   if(*rates < *(rates - 1) + 10.0)
				  *rates = (*(rates - 1)) + 20.0;
			}
			++rates;
		 }
		 if(*rates > 0.0)
		 {
			*rates -= (sot_adjust + 2.0);
			if(*rates < *(rates - 1) + 10.0)
			   *rates = (*(rates - 1)) + 20.0;
		 }
	  }
   }

   return true;
}
bool CodeStreamCompress::compressValidation()
{
   bool valid = true;

   /* ISO 15444-1:2004 states between 1 & 33
	* ergo (number of decomposition levels between 0 -> 32) */
   if((cp_.tcps->tccps->numresolutions == 0) ||
	  (cp_.tcps->tccps->numresolutions > GRK_MAXRLVLS))
   {
	  Logger::logger_.error("Invalid number of resolutions : %u not in range [1,%u]",
							cp_.tcps->tccps->numresolutions, GRK_MAXRLVLS);
	  return false;
   }
   if(cp_.t_width == 0)
   {
	  Logger::logger_.error("Tile x dimension must be greater than zero ");
	  return false;
   }
   if(cp_.t_height == 0)
   {
	  Logger::logger_.error("Tile y dimension must be greater than zero ");
	  return false;
   }

   return valid;
}
bool CodeStreamCompress::write_soc()
{
   return stream_->writeShort(J2K_SOC);
}
bool CodeStreamCompress::write_siz()
{
   SIZMarker siz;

   return siz.write(this, stream_);
}
bool CodeStreamCompress::write_cap()
{
   return cp_.tcps->qcd_->write(stream_);
}

bool CodeStreamCompress::write_com()
{
   for(uint32_t i = 0; i < cp_.num_comments; ++i)
   {
	  const char* comment = cp_.comment[i];
	  uint16_t comment_size = cp_.comment_len[i];
	  if(!comment_size)
	  {
		 Logger::logger_.warn("Empty comment. Ignoring");
		 continue;
	  }
	  if(comment_size > GRK_MAX_COMMENT_LENGTH)
	  {
		 Logger::logger_.warn(
			 "Comment length %s is greater than maximum comment length %u. Ignoring", comment_size,
			 GRK_MAX_COMMENT_LENGTH);
		 continue;
	  }
	  uint32_t totacom_size = (uint32_t)comment_size + 6;

	  /* COM */
	  if(!stream_->writeShort(J2K_COM))
		 return false;
	  /* L_COM */
	  if(!stream_->writeShort((uint16_t)(totacom_size - 2)))
		 return false;
	  if(!stream_->writeShort(cp_.is_binary_comment[i] ? 0 : 1))
		 return false;
	  if(!stream_->writeBytes((uint8_t*)comment, comment_size))
		 return false;
   }

   return true;
}
bool CodeStreamCompress::write_cod()
{
   uint32_t code_size;
   auto tcp = cp_.tcps;
   code_size = 9 + get_SPCod_SPCoc_size(0);

   /* COD */
   if(!stream_->writeShort(J2K_COD))
	  return false;
   /* L_COD */
   if(!stream_->writeShort((uint16_t)(code_size - 2)))
	  return false;
   /* Scod */
   if(!stream_->writeByte((uint8_t)tcp->csty))
	  return false;
   /* SGcod (A) */
   if(!stream_->writeByte((uint8_t)tcp->prg))
	  return false;
   /* SGcod (B) */
   if(!stream_->writeShort((uint16_t)tcp->max_layers_))
	  return false;
   /* SGcod (C) */
   if(!stream_->writeByte((uint8_t)tcp->mct))
	  return false;
   if(!write_SPCod_SPCoc(0))
   {
	  Logger::logger_.error("Error writing COD marker");
	  return false;
   }

   return true;
}
bool CodeStreamCompress::write_coc(uint32_t comp_no)
{
   uint32_t coc_size;
   uint32_t comp_room;
   auto tcp = cp_.tcps;
   auto image = getHeaderImage();
   comp_room = (image->numcomps <= 256) ? 1 : 2;
   coc_size = cod_coc_len + comp_room + get_SPCod_SPCoc_size(comp_no);

   /* COC */
   if(!stream_->writeShort(J2K_COC))
	  return false;
   /* L_COC */
   if(!stream_->writeShort((uint16_t)(coc_size - 2)))
	  return false;
   /* Ccoc */
   if(comp_room == 2)
   {
	  if(!stream_->writeShort((uint16_t)comp_no))
		 return false;
   }
   else
   {
	  if(!stream_->writeByte((uint8_t)comp_no))
		 return false;
   }

   /* Scoc */
   if(!stream_->writeByte((uint8_t)tcp->tccps[comp_no].csty))
	  return false;

   return write_SPCod_SPCoc(0);
}
bool CodeStreamCompress::compare_coc(uint32_t first_comp_no, uint32_t second_comp_no)
{
   auto tcp = cp_.tcps;

   if(tcp->tccps[first_comp_no].csty != tcp->tccps[second_comp_no].csty)
	  return false;

   return compare_SPCod_SPCoc(first_comp_no, second_comp_no);
}

bool CodeStreamCompress::write_qcd()
{
   uint32_t qcd_size;
   qcd_size = 4 + get_SQcd_SQcc_size(0);

   /* QCD */
   if(!stream_->writeShort(J2K_QCD))
	  return false;
   /* L_QCD */
   if(!stream_->writeShort((uint16_t)(qcd_size - 2)))
	  return false;
   if(!write_SQcd_SQcc(0))
   {
	  Logger::logger_.error("Error writing QCD marker");
	  return false;
   }

   return true;
}
bool CodeStreamCompress::write_qcc(uint32_t comp_no)
{
   uint32_t qcc_size = 6 + get_SQcd_SQcc_size(comp_no);

   /* QCC */
   if(!stream_->writeShort(J2K_QCC))
   {
	  return false;
   }

   if(getHeaderImage()->numcomps <= 256)
   {
	  --qcc_size;

	  /* L_QCC */
	  if(!stream_->writeShort((uint16_t)(qcc_size - 2)))
		 return false;
	  /* Cqcc */
	  if(!stream_->writeByte((uint8_t)comp_no))
		 return false;
   }
   else
   {
	  /* L_QCC */
	  if(!stream_->writeShort((uint16_t)(qcc_size - 2)))
		 return false;
	  /* Cqcc */
	  if(!stream_->writeShort((uint16_t)comp_no))
		 return false;
   }

   return write_SQcd_SQcc(comp_no);
}
bool CodeStreamCompress::compare_qcc(uint32_t first_comp_no, uint32_t second_comp_no)
{
   return compare_SQcd_SQcc(first_comp_no, second_comp_no);
}
bool CodeStreamCompress::writePoc()
{
   auto tcp = cp_.tcps;
   auto tccp = tcp->tccps;
   auto image = getHeaderImage();
   uint16_t numComps = image->numcomps;
   uint32_t numPocs = tcp->getNumProgressions();
   uint32_t pocRoom = (numComps <= 256) ? 1 : 2;

   auto poc_size = getPocSize(numComps, numPocs);

   /* POC  */
   if(!stream_->writeShort(J2K_POC))
	  return false;

   /* Lpoc */
   if(!stream_->writeShort((uint16_t)(poc_size - 2)))
	  return false;

   for(uint32_t i = 0; i < numPocs; ++i)
   {
	  auto current_prog = tcp->progressionOrderChange + i;
	  /* RSpoc_i */
	  if(!stream_->writeByte(current_prog->res_s))
		 return false;
	  /* CSpoc_i */
	  if(pocRoom == 2)
	  {
		 if(!stream_->writeShort(current_prog->comp_s))
			return false;
	  }
	  else
	  {
		 if(!stream_->writeByte((uint8_t)current_prog->comp_s))
			return false;
	  }
	  /* LYEpoc_i */
	  if(!stream_->writeShort(current_prog->lay_e))
		 return false;
	  /* REpoc_i */
	  if(!stream_->writeByte(current_prog->res_e))
		 return false;
	  /* CEpoc_i */
	  if(pocRoom == 2)
	  {
		 if(!stream_->writeShort(current_prog->comp_e))
			return false;
	  }
	  else
	  {
		 if(!stream_->writeByte((uint8_t)current_prog->comp_e))
			return false;
	  }
	  /* Ppoc_i */
	  if(!stream_->writeByte((uint8_t)current_prog->progression))
		 return false;

	  /* change the value of the max layer according to the actual number of layers in the file,
	   * components and resolutions*/
	  current_prog->lay_e = std::min<uint16_t>(current_prog->lay_e, tcp->max_layers_);
	  current_prog->res_e = std::min<uint8_t>(current_prog->res_e, tccp->numresolutions);
	  current_prog->comp_e = std::min<uint16_t>(current_prog->comp_e, numComps);
   }

   return true;
}

bool CodeStreamCompress::write_mct_data_group()
{
   if(!write_cbd())
	  return false;

   auto tcp = cp_.tcps;
   auto mct_record = tcp->mct_records_;

   for(uint32_t i = 0; i < tcp->nb_mct_records_; ++i)
   {
	  if(!write_mct_record(mct_record, stream_))
		 return false;
	  ++mct_record;
   }

   auto mcc_record = tcp->mcc_records_;
   for(uint32_t i = 0; i < tcp->nb_mcc_records_; ++i)
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
	  auto tccp = cp_.tcps->tccps + compno;
	  if(tccp->roishift)
	  {
		 if(!write_rgn(0, compno, getHeaderImage()->numcomps))
			return false;
	  }
   }

   return true;
}
bool CodeStreamCompress::write_mcc_record(grk_simple_mcc_decorrelation_data* p_mcc_record,
										  BufferedStream* stream)
{
   uint32_t i;
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
   if(!stream->writeShort(J2K_MCC))
	  return false;
   /* Lmcc */
   if(!stream->writeShort((uint16_t)(mcc_size - 2)))
	  return false;
   /* first marker */
   /* Zmcc */
   if(!stream->writeShort(0))
	  return false;
   /* Imcc -> no need for other values, take the first */
   if(!stream->writeByte((uint8_t)p_mcc_record->index_))
	  return false;
   /* only one marker atm */
   /* Ymcc */
   if(!stream->writeShort(0))
	  return false;
   /* Qmcc -> number of collections -> 1 */
   if(!stream->writeShort(1))
	  return false;
   /* Xmcci type of component transformation -> array based decorrelation */
   if(!stream->writeByte(0x1))
	  return false;
   /* Nmcci number of input components involved and size for each component offset = 8 bits */
   if(!stream->writeShort((uint16_t)(p_mcc_record->nb_comps_ | mask)))
	  return false;

   for(i = 0; i < p_mcc_record->nb_comps_; ++i)
   {
	  /* Cmccij Component offset*/
	  if(nb_bytes_for_comp == 2)
	  {
		 if(!stream->writeShort((uint16_t)i))
			return false;
	  }
	  else
	  {
		 if(!stream->writeByte((uint8_t)i))
			return false;
	  }
   }

   /* Mmcci number of output components involved and size for each component offset = 8 bits */
   if(!stream->writeShort((uint16_t)(p_mcc_record->nb_comps_ | mask)))
	  return false;

   for(i = 0; i < p_mcc_record->nb_comps_; ++i)
   {
	  /* Wmccij Component offset*/
	  if(nb_bytes_for_comp == 2)
	  {
		 if(!stream->writeShort((uint16_t)i))
			return false;
	  }
	  else
	  {
		 if(!stream->writeByte((uint8_t)i))
			return false;
	  }
   }

   tmcc = ((uint32_t)((!p_mcc_record->is_irreversible_) & 1U)) << 16;

   if(p_mcc_record->decorrelation_array_)
	  tmcc |= p_mcc_record->decorrelation_array_->index_;

   if(p_mcc_record->offset_array_)
	  tmcc |= ((p_mcc_record->offset_array_->index_) << 8);

   /* Tmcci : use MCT defined as number 1 and irreversible array based. */
   return stream->write24(tmcc);
}
bool CodeStreamCompress::write_mco()
{
   uint32_t mco_size;
   uint32_t i;
   auto tcp = &(cp_.tcps[0]);
   mco_size = 5 + tcp->nb_mcc_records_;

   /* MCO */
   if(!stream_->writeShort(J2K_MCO))
	  return false;

   /* Lmco */
   if(!stream_->writeShort((uint16_t)(mco_size - 2)))
	  return false;

   /* Nmco : only one transform stage*/
   if(!stream_->writeByte((uint8_t)tcp->nb_mcc_records_))
	  return false;

   auto mcc_record = tcp->mcc_records_;
   for(i = 0; i < tcp->nb_mcc_records_; ++i)
   {
	  /* Imco -> use the mcc indicated by 1*/
	  if(!stream_->writeByte((uint8_t)mcc_record->index_))
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
   if(!stream_->writeShort(J2K_CBD))
	  return false;

   /* L_CBD */
   if(!stream_->writeShort((uint16_t)(cbd_size - 2U)))
	  return false;

   /* Ncbd */
   if(!stream_->writeShort(image->numcomps))
	  return false;

   for(i = 0; i < image->numcomps; ++i)
   {
	  auto comp = image->comps + i;
	  /* Component bit depth */
	  uint8_t bpc = (uint8_t)(comp->prec - 1);
	  if(comp->sgnd)
		 bpc = (uint8_t)(bpc + (1 << 7));
	  if(!stream_->writeByte(bpc))
		 return false;
   }
   return true;
}

bool CodeStreamCompress::write_tlm_begin()
{
   if(!cp_.tlm_markers)
	  cp_.tlm_markers = new TileLengthMarkers(stream_);

   return cp_.tlm_markers->writeBegin(compressorState_.total_tile_parts_);
}
bool CodeStreamCompress::write_tlm_end()
{
   return cp_.tlm_markers->writeEnd();
}
uint32_t CodeStreamCompress::get_SPCod_SPCoc_size(uint32_t comp_no)
{
   auto tcp = cp_.tcps;
   auto tccp = tcp->tccps + comp_no;
   assert(comp_no < getHeaderImage()->numcomps);

   uint32_t rc = SPCod_SPCoc_len;
   if(tccp->csty & J2K_CCP_CSTY_PRT)
	  rc += tccp->numresolutions;

   return rc;
}
bool CodeStreamCompress::compare_SPCod_SPCoc(uint32_t first_comp_no, uint32_t second_comp_no)
{
   auto tcp = cp_.tcps;
   auto tccp0 = tcp->tccps + first_comp_no;
   auto tccp1 = tcp->tccps + second_comp_no;

   if(tccp0->numresolutions != tccp1->numresolutions)
	  return false;
   if(tccp0->cblkw != tccp1->cblkw)
	  return false;
   if(tccp0->cblkh != tccp1->cblkh)
	  return false;
   if(tccp0->cblk_sty != tccp1->cblk_sty)
	  return false;
   if(tccp0->qmfbid != tccp1->qmfbid)
	  return false;
   if((tccp0->csty & J2K_CCP_CSTY_PRT) != (tccp1->csty & J2K_CCP_CSTY_PRT))
	  return false;
   for(uint32_t i = 0U; i < tccp0->numresolutions; ++i)
   {
	  if(tccp0->precWidthExp[i] != tccp1->precWidthExp[i])
		 return false;
	  if(tccp0->precHeightExp[i] != tccp1->precHeightExp[i])
		 return false;
   }

   return true;
}

bool CodeStreamCompress::write_SPCod_SPCoc(uint32_t comp_no)
{
   auto tcp = cp_.tcps;
   auto tccp = tcp->tccps + comp_no;

   assert(comp_no < (getHeaderImage()->numcomps));

   /* SPcoc (D) */
   if(!stream_->writeByte((uint8_t)(tccp->numresolutions - 1)))
	  return false;
   /* SPcoc (E) */
   if(!stream_->writeByte((uint8_t)(tccp->cblkw - 2)))
	  return false;
   /* SPcoc (F) */
   if(!stream_->writeByte((uint8_t)(tccp->cblkh - 2)))
	  return false;
   /* SPcoc (G) */
   if(!stream_->writeByte(tccp->cblk_sty))
	  return false;
   /* SPcoc (H) */
   if(!stream_->writeByte((uint8_t)tccp->qmfbid))
	  return false;

   if(tccp->csty & J2K_CCP_CSTY_PRT)
   {
	  for(uint32_t i = 0; i < tccp->numresolutions; ++i)
	  {
		 /* SPcoc (I_i) */
		 if(!stream_->writeByte((uint8_t)(tccp->precWidthExp[i] + (tccp->precHeightExp[i] << 4))))
		 {
			return false;
		 }
	  }
   }

   return true;
}
uint32_t CodeStreamCompress::get_SQcd_SQcc_size(uint32_t comp_no)
{
   auto tcp = cp_.tcps;
   auto tccp = tcp->tccps + comp_no;
   assert(comp_no < getHeaderImage()->numcomps);

   uint32_t num_bands =
	   (tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions * 3U - 2);

   return (tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) ? 1 + num_bands : 1 + 2 * num_bands;
}
bool CodeStreamCompress::compare_SQcd_SQcc(uint32_t first_comp_no, uint32_t second_comp_no)
{
   auto tcp = cp_.tcps;
   auto tccp0 = tcp->tccps + first_comp_no;
   auto tccp1 = tcp->tccps + second_comp_no;

   if(tccp0->qntsty != tccp1->qntsty)
	  return false;
   if(tccp0->numgbits != tccp1->numgbits)
	  return false;
   uint32_t band_no, num_bands;
   if(tccp0->qntsty == J2K_CCP_QNTSTY_SIQNT)
   {
	  num_bands = 1U;
   }
   else
   {
	  num_bands = tccp0->numresolutions * 3U - 2U;
	  if(num_bands != (tccp1->numresolutions * 3U - 2U))
		 return false;
   }
   for(band_no = 0; band_no < num_bands; ++band_no)
   {
	  if(tccp0->stepsizes[band_no].expn != tccp1->stepsizes[band_no].expn)
		 return false;
   }
   if(tccp0->qntsty != J2K_CCP_QNTSTY_NOQNT)
   {
	  for(band_no = 0; band_no < num_bands; ++band_no)
	  {
		 if(tccp0->stepsizes[band_no].mant != tccp1->stepsizes[band_no].mant)
			return false;
	  }
   }

   return true;
}
bool CodeStreamCompress::write_SQcd_SQcc(uint32_t comp_no)
{
   assert(comp_no < getHeaderImage()->numcomps);
   auto tcp = cp_.tcps;
   auto tccp = tcp->tccps + comp_no;
   uint32_t num_bands =
	   (tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions * 3U - 2);

   /* Sqcx */
   if(!stream_->writeByte((uint8_t)(tccp->qntsty + (tccp->numgbits << 5))))
	  return false;

   /* SPqcx_i */
   for(uint32_t band_no = 0; band_no < num_bands; ++band_no)
   {
	  uint32_t expn = tccp->stepsizes[band_no].expn;
	  uint32_t mant = tccp->stepsizes[band_no].mant;
	  if(tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
	  {
		 if(!stream_->writeByte((uint8_t)(expn << 3)))
			return false;
	  }
	  else
	  {
		 if(!stream_->writeShort((uint16_t)((expn << 11) + mant)))
			return false;
	  }
   }
   return true;
}
uint16_t CodeStreamCompress::getPocSize(uint32_t numComps, uint32_t numPocs)
{
   uint32_t pocRoom = (numComps <= 256) ? 1 : 2;

   return (uint16_t)(4 + (5 + 2 * pocRoom) * numPocs);
}
bool CodeStreamCompress::validateProgressionOrders(const grk_progression* progressions,
												   uint32_t numProgressions, uint8_t num_resolutions,
												   uint16_t numComps, uint16_t numLayers)
{
   uint32_t resno, compno, layno;
   uint32_t i;
   uint32_t step_c = 1;
   uint32_t step_r = numComps * step_c;
   uint32_t step_l = num_resolutions * step_r;

   auto packet_array = new uint8_t[(size_t)step_l * numLayers];
   memset(packet_array, 0, (size_t)step_l * numLayers * sizeof(uint8_t));

   /* iterate through all the pocs */
   for(i = 0; i < numProgressions; ++i)
   {
	  auto currentPoc = progressions + i;
	  size_t index = step_r * currentPoc->res_s;
	  /* take each resolution for each poc */
	  for(resno = currentPoc->res_s; resno < std::min<uint32_t>(currentPoc->res_e, num_resolutions);
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
	  for(resno = 0; resno < num_resolutions; ++resno)
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
	  Logger::logger_.error("POC: missing packets");
   delete[] packet_array;

   return !loss;
}
bool CodeStreamCompress::init_mct_encoding(TileCodingParams* p_tcp, GrkImage* p_image)
{
   uint32_t i;
   uint32_t indix = 1;
   grk_mct_data *mct_deco_data = nullptr, *mct_offset_data = nullptr;
   grk_simple_mcc_decorrelation_data* mcc_data;
   uint32_t mct_size, nb_elem;
   float *data, *current_data;
   TileComponentCodingParams* tccp;

   assert(p_tcp != nullptr);

   if(p_tcp->mct != 2)
	  return true;

   if(p_tcp->mct_decoding_matrix_)
   {
	  if(p_tcp->nb_mct_records_ == p_tcp->nb_max_mct_records_)
	  {
		 p_tcp->nb_max_mct_records_ += default_number_mct_records;

		 auto new_mct_records = (grk_mct_data*)grk_realloc(
			 p_tcp->mct_records_, p_tcp->nb_max_mct_records_ * sizeof(grk_mct_data));
		 if(!new_mct_records)
		 {
			grk_free(p_tcp->mct_records_);
			p_tcp->mct_records_ = nullptr;
			p_tcp->nb_max_mct_records_ = 0;
			p_tcp->nb_mct_records_ = 0;
			/* Logger::logger_.error( "Not enough memory to set up mct compressing"); */
			return false;
		 }
		 p_tcp->mct_records_ = new_mct_records;
		 mct_deco_data = p_tcp->mct_records_ + p_tcp->nb_mct_records_;

		 memset(mct_deco_data, 0,
				(p_tcp->nb_max_mct_records_ - p_tcp->nb_mct_records_) * sizeof(grk_mct_data));
	  }
	  mct_deco_data = p_tcp->mct_records_ + p_tcp->nb_mct_records_;
	  grk_free(mct_deco_data->data_);
	  mct_deco_data->data_ = nullptr;

	  mct_deco_data->index_ = indix++;
	  mct_deco_data->array_type_ = MCT_TYPE_DECORRELATION;
	  mct_deco_data->element_type_ = MCT_TYPE_FLOAT;
	  nb_elem = (uint32_t)p_image->numcomps * p_image->numcomps;
	  mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_deco_data->element_type_];
	  mct_deco_data->data_ = (uint8_t*)grk_malloc(mct_size);

	  if(!mct_deco_data->data_)
		 return false;

	  j2k_mct_write_functions_from_float[mct_deco_data->element_type_](
		  p_tcp->mct_decoding_matrix_, mct_deco_data->data_, nb_elem);

	  mct_deco_data->data_size_ = mct_size;
	  ++p_tcp->nb_mct_records_;
   }

   if(p_tcp->nb_mct_records_ == p_tcp->nb_max_mct_records_)
   {
	  grk_mct_data* new_mct_records;
	  p_tcp->nb_max_mct_records_ += default_number_mct_records;
	  new_mct_records = (grk_mct_data*)grk_realloc(p_tcp->mct_records_, p_tcp->nb_max_mct_records_ *
																			sizeof(grk_mct_data));
	  if(!new_mct_records)
	  {
		 grk_free(p_tcp->mct_records_);
		 p_tcp->mct_records_ = nullptr;
		 p_tcp->nb_max_mct_records_ = 0;
		 p_tcp->nb_mct_records_ = 0;
		 /* Logger::logger_.error( "Not enough memory to set up mct compressing"); */
		 return false;
	  }
	  p_tcp->mct_records_ = new_mct_records;
	  mct_offset_data = p_tcp->mct_records_ + p_tcp->nb_mct_records_;

	  memset(mct_offset_data, 0,
			 (p_tcp->nb_max_mct_records_ - p_tcp->nb_mct_records_) * sizeof(grk_mct_data));
	  if(mct_deco_data)
		 mct_deco_data = mct_offset_data - 1;
   }
   mct_offset_data = p_tcp->mct_records_ + p_tcp->nb_mct_records_;
   if(mct_offset_data->data_)
   {
	  grk_free(mct_offset_data->data_);
	  mct_offset_data->data_ = nullptr;
   }
   mct_offset_data->index_ = indix++;
   mct_offset_data->array_type_ = MCT_TYPE_OFFSET;
   mct_offset_data->element_type_ = MCT_TYPE_FLOAT;
   nb_elem = p_image->numcomps;
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
   tccp = p_tcp->tccps;
   current_data = data;

   for(i = 0; i < nb_elem; ++i)
   {
	  *(current_data++) = (float)(tccp->dc_level_shift_);
	  ++tccp;
   }
   j2k_mct_write_functions_from_float[mct_offset_data->element_type_](data, mct_offset_data->data_,
																	  nb_elem);
   grk_free(data);
   mct_offset_data->data_size_ = mct_size;
   ++p_tcp->nb_mct_records_;

   if(p_tcp->nb_mcc_records_ == p_tcp->nb_max_mcc_records_)
   {
	  grk_simple_mcc_decorrelation_data* new_mcc_records;
	  p_tcp->nb_max_mcc_records_ += default_number_mct_records;
	  new_mcc_records = (grk_simple_mcc_decorrelation_data*)grk_realloc(
		  p_tcp->mcc_records_,
		  p_tcp->nb_max_mcc_records_ * sizeof(grk_simple_mcc_decorrelation_data));
	  if(!new_mcc_records)
	  {
		 grk_free(p_tcp->mcc_records_);
		 p_tcp->mcc_records_ = nullptr;
		 p_tcp->nb_max_mcc_records_ = 0;
		 p_tcp->nb_mcc_records_ = 0;
		 /* Logger::logger_.error( "Not enough memory to set up mct compressing"); */
		 return false;
	  }
	  p_tcp->mcc_records_ = new_mcc_records;
	  mcc_data = p_tcp->mcc_records_ + p_tcp->nb_mcc_records_;
	  memset(mcc_data, 0,
			 (p_tcp->nb_max_mcc_records_ - p_tcp->nb_mcc_records_) *
				 sizeof(grk_simple_mcc_decorrelation_data));
   }
   mcc_data = p_tcp->mcc_records_ + p_tcp->nb_mcc_records_;
   mcc_data->decorrelation_array_ = mct_deco_data;
   mcc_data->is_irreversible_ = 1;
   mcc_data->nb_comps_ = p_image->numcomps;
   mcc_data->index_ = indix++;
   mcc_data->offset_array_ = mct_offset_data;
   ++p_tcp->nb_mcc_records_;

   return true;
}
uint64_t CodeStreamCompress::getNumTilePartsForProgression(uint32_t pino, uint16_t tileno)
{
   uint64_t numTileParts = 1;
   auto cp = &cp_;

   /*  preconditions */
   assert(tileno < (cp->t_grid_width * cp->t_grid_height));
   assert(pino < (cp->tcps[tileno].getNumProgressions()));

   /* get the given tile coding parameter */
   auto tcp = cp->tcps + tileno;
   assert(tcp != nullptr);

   auto current_poc = &(tcp->progressionOrderChange[pino]);
   assert(current_poc != 0);

   /* get the progression order as a character string */
   auto prog = convertProgressionOrder(tcp->prg);
   assert(strlen(prog) > 0);

   if(cp->coding_params_.enc_.enableTilePartGeneration_)
   {
	  for(uint32_t i = 0; i < 4; ++i)
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
		 if(cp->coding_params_.enc_.newTilePartProgressionDivider_ == prog[i])
		 {
			assert(prog[i] != 'P');
			cp->coding_params_.enc_.newTilePartProgressionPosition = i;
			break;
		 }
	  }
   }
   else
   {
	  numTileParts = 1;
   }
   assert(numTileParts < maxTilePartsPerTileJ2K);

   return numTileParts;
}
bool CodeStreamCompress::getNumTileParts(uint16_t* numTilePartsForAllTiles, GrkImage* image)
{
   assert(numTilePartsForAllTiles != nullptr);
   assert(image != nullptr);

   uint32_t numTiles = (uint16_t)(cp_.t_grid_width * cp_.t_grid_height);
   *numTilePartsForAllTiles = 0;
   for(uint16_t tileno = 0; tileno < numTiles; ++tileno)
   {
	  auto tcp = cp_.tcps + tileno;
	  uint8_t totalTilePartsForTile = 0;
	  PacketManager::updateCompressParams(image, &cp_, tileno);
	  for(uint32_t pino = 0; pino < tcp->getNumProgressions(); ++pino)
	  {
		 uint64_t numTilePartsForProgression = getNumTilePartsForProgression(pino, tileno);
		 uint16_t newTotalTilePartsForTile =
			 uint16_t(numTilePartsForProgression + totalTilePartsForTile);
		 if(newTotalTilePartsForTile > maxTilePartsPerTileJ2K)
		 {
			Logger::logger_.error("Number of tile parts %u exceeds maximum number of "
								  "tile parts %u",
								  (uint16_t)newTotalTilePartsForTile, maxTilePartsPerTileJ2K);
			return false;
		 }
		 totalTilePartsForTile = (uint8_t)newTotalTilePartsForTile;

		 uint32_t newTotalTilePartsForAllTiles =
			 (uint32_t)(*numTilePartsForAllTiles + numTilePartsForProgression);
		 if(newTotalTilePartsForAllTiles > maxTotalTilePartsJ2K)
		 {
			Logger::logger_.error(
				"Total number of tile parts %u for image exceeds JPEG 2000 maximum total "
				"number of "
				"tile parts %u",
				newTotalTilePartsForAllTiles, maxTotalTilePartsJ2K);
			return false;
		 }
		 *numTilePartsForAllTiles = (uint16_t)newTotalTilePartsForAllTiles;
	  }
	  tcp->numTileParts_ = totalTilePartsForTile;
   }

   return true;
}

} // namespace grk
