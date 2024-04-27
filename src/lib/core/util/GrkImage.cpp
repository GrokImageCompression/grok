#include <grk_includes.h>

namespace grk
{
GrkImage::GrkImage()
{
   memset((grk_image*)(this), 0, sizeof(grk_image));
   obj.wrapper = new GrkObjectWrapperImpl(this);
   rowsPerTask = singleTileRowsPerStrip;
}
GrkImage::~GrkImage()
{
   if(comps)
   {
	  all_components_data_free();
	  delete[] comps;
   }
   if(meta)
	  grk_object_unref(&meta->obj);
   grk_aligned_free(interleavedData.data_);
}
uint32_t GrkImage::width(void) const
{
   return x1 - x0;
}
uint32_t GrkImage::height(void) const
{
   return y1 - y0;
}

void GrkImage::print(void) const
{
   Logger::logger_.info("bounds: [%u,%u,%u,%u]", x0, y0, x1, y1);
   for(uint16_t i = 0; i < numcomps; ++i)
   {
	  auto comp = comps + i;
	  Logger::logger_.info("component %d bounds : [%u,%u,%u,%u]", i, comp->x0, comp->y0, comp->w,
						   comp->h);
   }
}

void GrkImage::copyComponent(grk_image_comp* src, grk_image_comp* dest)
{
   dest->dx = src->dx;
   dest->dy = src->dy;
   dest->w = src->w;
   dest->h = src->h;
   dest->x0 = src->x0;
   dest->y0 = src->y0;
   dest->Xcrg = src->Xcrg;
   dest->Ycrg = src->Ycrg;
   dest->prec = src->prec;
   dest->sgnd = src->sgnd;
   dest->type = src->type;
}

bool GrkImage::componentsEqual(uint16_t firstNComponents, bool checkPrecision)
{
   if(firstNComponents <= 1)
	  return true;

   // check that all components dimensions etc. are equal
   for(uint16_t compno = 1; compno < firstNComponents; compno++)
   {
	  if(!componentsEqual(comps, comps + compno, checkPrecision))
		 return false;
   }

   return true;
}
bool GrkImage::componentsEqual(bool checkPrecision)
{
   return componentsEqual(numcomps, checkPrecision);
}
bool GrkImage::componentsEqual(grk_image_comp* src, grk_image_comp* dest, bool checkPrecision)
{
   if(checkPrecision && dest->prec != src->prec)
	  return false;

   return (dest->dx == src->dx && dest->dy == src->dy && dest->w == src->w &&
		   dest->stride == src->stride && dest->h == src->h && dest->x0 == src->x0 &&
		   dest->y0 == src->y0 && dest->Xcrg == src->Xcrg && dest->Ycrg == src->Ycrg &&
		   dest->sgnd == src->sgnd && dest->type == src->type);
}
GrkImage* GrkImage::create(grk_image* src, uint16_t numcmpts, grk_image_comp* cmptparms,
						   GRK_COLOR_SPACE clrspc, bool doAllocation)
{
   assert(numcmpts);
   assert(cmptparms);

   if(!numcmpts || !cmptparms)
	  return nullptr;

   auto image = new GrkImage();
   image->color_space = clrspc;
   image->numcomps = numcmpts;
   image->decompressNumComps = numcmpts;
   image->decompressWidth = cmptparms->w;
   image->decompressHeight = cmptparms->h;
   image->decompressPrec = cmptparms->prec;
   image->decompressColourSpace = clrspc;
   if(src)
   {
	  image->decompressFormat = src->decompressFormat;
	  image->forceRGB = src->forceRGB;
	  image->upsample = src->upsample;
	  image->precision = src->precision;
	  image->numPrecision = src->numPrecision;
	  image->rowsPerStrip = src->rowsPerStrip;
	  image->packedRowBytes = src->packedRowBytes;
   }

   /* allocate memory for the per-component information */
   image->comps = new grk_image_comp[image->numcomps];
   memset(image->comps, 0, image->numcomps * sizeof(grk_image_comp));

   /* create the individual image components */
   for(uint16_t compno = 0; compno < numcmpts; compno++)
   {
	  auto comp = &image->comps[compno];

	  assert(cmptparms[compno].dx);
	  assert(cmptparms[compno].dy);
	  comp->dx = cmptparms[compno].dx;
	  comp->dy = cmptparms[compno].dy;
	  comp->w = cmptparms[compno].w;
	  comp->h = cmptparms[compno].h;
	  comp->x0 = cmptparms[compno].x0;
	  comp->y0 = cmptparms[compno].y0;
	  comp->prec = cmptparms[compno].prec;
	  comp->sgnd = cmptparms[compno].sgnd;
	  if(doAllocation && !allocData(comp))
	  {
		 grk::Logger::logger_.error("Unable to allocate memory for image.");
		 delete image;
		 return nullptr;
	  }
	  comp->type = GRK_CHANNEL_TYPE_COLOUR;
	  switch(compno)
	  {
		 case 0:
			comp->association = GRK_CHANNEL_ASSOC_COLOUR_1;
			break;
		 case 1:
			comp->association = GRK_CHANNEL_ASSOC_COLOUR_2;
			break;
		 case 2:
			comp->association = GRK_CHANNEL_ASSOC_COLOUR_3;
			break;
		 default:
			comp->association = GRK_CHANNEL_ASSOC_UNASSOCIATED;
			// CMKY component 3 type equals GRK_CHANNEL_TYPE_COLOUR
			if(clrspc != GRK_CLRSPC_CMYK || compno != 3)
			   comp->type = GRK_CHANNEL_TYPE_UNSPECIFIED;
			break;
	  }
   }

   // use first component dimensions as image dimensions
   image->x1 = cmptparms[0].w;
   image->y1 = cmptparms[0].h;

   return image;
}

void GrkImage::all_components_data_free()
{
   uint32_t i;
   if(!comps)
	  return;
   for(i = 0; i < numcomps; ++i)
	  single_component_data_free(comps + i);
}

bool GrkImage::subsampleAndReduce(uint32_t reduce)
{
   for(uint16_t compno = 0; compno < numcomps; ++compno)
   {
	  auto comp = comps + compno;

	  if(x0 > (uint32_t)INT_MAX || y0 > (uint32_t)INT_MAX || x1 > (uint32_t)INT_MAX ||
		 y1 > (uint32_t)INT_MAX)
	  {
		 Logger::logger_.error("Image coordinates above INT_MAX are not supported.");
		 return false;
	  }

	  // sub-sample and reduce component origin
	  comp->x0 = ceildiv<uint32_t>(x0, comp->dx);
	  comp->y0 = ceildiv<uint32_t>(y0, comp->dy);

	  comp->x0 = ceildivpow2<uint32_t>(comp->x0, reduce);
	  comp->y0 = ceildivpow2<uint32_t>(comp->y0, reduce);

	  uint32_t comp_x1 = ceildiv<uint32_t>(x1, comp->dx);
	  comp_x1 = ceildivpow2<uint32_t>(comp_x1, reduce);
	  if(comp_x1 <= comp->x0)
	  {
		 Logger::logger_.error(
			 "subsampleAndReduce: component %u: x1 (%u) is <= x0 (%u). Subsampled and "
			 "reduced image is invalid",
			 compno, comp_x1, comp->x0);
		 return false;
	  }
	  comp->w = (uint32_t)(comp_x1 - comp->x0);
	  assert(comp->w);

	  uint32_t comp_y1 = ceildiv<uint32_t>(y1, comp->dy);
	  comp_y1 = ceildivpow2<uint32_t>(comp_y1, reduce);
	  if(comp_y1 <= comp->y0)
	  {
		 Logger::logger_.error(
			 "subsampleAndReduce: component %u: y1 (%u) is <= y0 (%u).  Subsampled and "
			 "reduced image is invalid",
			 compno, comp_y1, comp->y0);
		 return false;
	  }
	  comp->h = (uint32_t)(comp_y1 - comp->y0);
	  assert(comp->h);
   }

   return true;
}

/**
 * Copy only header of image and its component header (no data copied)
 * if dest image has data, it will be freed
 *
 * @param	dest	the dest image
 *
 *
 */
void GrkImage::copyHeader(GrkImage* dest)
{
   if(!dest)
	  return;

   dest->x0 = x0;
   dest->y0 = y0;
   dest->x1 = x1;
   dest->y1 = y1;

   if(dest->comps)
   {
	  all_components_data_free();
	  delete[] dest->comps;
	  dest->comps = nullptr;
   }
   dest->numcomps = numcomps;
   dest->comps = new grk_image_comp[dest->numcomps];
   for(uint16_t compno = 0; compno < dest->numcomps; compno++)
   {
	  memcpy(&(dest->comps[compno]), &(comps[compno]), sizeof(grk_image_comp));
	  dest->comps[compno].data = nullptr;
   }

   dest->color_space = color_space;
   if(has_capture_resolution)
   {
	  dest->capture_resolution[0] = capture_resolution[0];
	  dest->capture_resolution[1] = capture_resolution[1];
   }
   if(has_display_resolution)
   {
	  dest->display_resolution[0] = display_resolution[0];
	  dest->display_resolution[1] = display_resolution[1];
   }
   if(meta)
   {
	  GrkImageMeta* temp = (GrkImageMeta*)meta;
	  grk_object_ref(&temp->obj);
	  dest->meta = meta;
   }
   dest->decompressFormat = decompressFormat;
   dest->decompressNumComps = decompressNumComps;
   dest->decompressWidth = decompressWidth;
   dest->decompressHeight = decompressHeight;
   dest->decompressPrec = decompressPrec;
   dest->decompressColourSpace = decompressColourSpace;
   dest->forceRGB = forceRGB;
   dest->upsample = upsample;
   dest->precision = precision;
   dest->hasMultipleTiles = hasMultipleTiles;
   dest->numPrecision = numPrecision;
   dest->rowsPerStrip = rowsPerStrip;
   dest->packedRowBytes = packedRowBytes;
}
bool GrkImage::allocData(grk_image_comp* comp)
{
   return allocData(comp, false);
}
bool GrkImage::allocData(grk_image_comp* comp, bool clear)
{
   if(!comp || comp->w == 0 || comp->h == 0)
	  return false;
   comp->stride = grk_make_aligned_width(comp->w);
   assert(comp->stride);
   assert(!comp->data);

   size_t dataSize = (uint64_t)comp->stride * comp->h * sizeof(uint32_t);
   auto data = (int32_t*)grk_aligned_malloc(dataSize);
   if(!data)
   {
	  grk::Logger::logger_.error("Failed to allocate aligned memory buffer of dimensions %u x %u",
								 comp->stride, comp->h);
	  return false;
   }
   if(clear)
	  memset(data, 0, dataSize);
   single_component_data_free(comp);
   comp->data = data;

   return true;
}

bool GrkImage::supportsStripCache(CodingParams* cp)
{
   if(!cp->wholeTileDecompress_)
	  return false;

   if(hasMultipleTiles)
   {
	  // packed tile width bits must be divisible by 8
	  if(((cp->t_width * numcomps * comps->prec) & 7) != 0)
		 return false;
   }
   else
   {
	  // only mono supported (why is this restriction relaxed for multiple tiles ?)
	  if(numcomps > 1)
		 return false;
   }

   // difference between image origin y coordinate and tile origin y coordinate
   // must be multiple of the tile height, so that only the final strip may have
   // different height than the rest. Otherwise, TIFF will not be successfully
   // created
   if(((y0 - cp->ty0) % cp->t_height) != 0)
	  return false;

   bool supportedFileFormat =
	   decompressFormat == GRK_FMT_TIF || (decompressFormat == GRK_FMT_PXM && !splitByComponent);
   if(isSubsampled() || precision || upsample || needsConversionToRGB() || !supportedFileFormat ||
	  (meta && (meta->color.palette || meta->color.icc_profile_buf)))
   {
	  return false;
   }

   return componentsEqual(true);
}

bool GrkImage::isSubsampled()
{
   for(uint32_t i = 0; i < numcomps; ++i)
   {
	  if(comps[i].dx != 1 || comps[i].dy != 1)
		 return true;
   }
   return false;
}

void GrkImage::validateColourSpace(void)
{
   if(color_space == GRK_CLRSPC_UNKNOWN && numcomps == 3 && comps[0].dx == 1 && comps[0].dy == 1 &&
	  comps[1].dx == comps[2].dx && comps[1].dy == comps[2].dy &&
	  (comps[1].dx == 2 || comps[1].dy == 2) && (comps[2].dx == 2 || comps[2].dy == 2))
   {
	  color_space = GRK_CLRSPC_SYCC;
   }
}
bool GrkImage::isOpacity(uint16_t compno)
{
   if(compno >= numcomps)
	  return false;
   auto comp = comps + compno;

   return (comp->type == GRK_CHANNEL_TYPE_OPACITY ||
		   comp->type == GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY);
}
void GrkImage::postReadHeader(CodingParams* cp)
{
   uint8_t prec = comps[0].prec;
   if(precision)
	  prec = precision->prec;
   bool isGAorRGBA =
	   (decompressNumComps == 4 || decompressNumComps == 2) && isOpacity(decompressNumComps - 1);
   if(meta && meta->color.palette)
	  decompressNumComps = meta->color.palette->num_channels;
   else
	  decompressNumComps = (forceRGB && numcomps < 3) ? 3 : numcomps;
   if(decompressFormat == GRK_FMT_PXM && decompressNumComps == 4 && !isGAorRGBA)
	  decompressNumComps = 3;
   uint16_t ncmp = decompressNumComps;
   decompressWidth = comps->w;
   if(isSubsampled() && (upsample || forceRGB))
	  decompressWidth = x1 - x0;
   decompressHeight = comps->h;
   if(isSubsampled() && (upsample || forceRGB))
	  decompressHeight = y1 - y0;
   decompressPrec = comps->prec;
   if(precision)
	  decompressPrec = precision->prec;
   decompressColourSpace = color_space;
   if(needsConversionToRGB())
	  decompressColourSpace = GRK_CLRSPC_SRGB;
   bool tiffSubSampled = decompressFormat == GRK_FMT_TIF && isSubsampled() &&
						 (color_space == GRK_CLRSPC_EYCC || color_space == GRK_CLRSPC_SYCC);
   if(tiffSubSampled)
   {
	  uint32_t chroma_subsample_x = comps[1].dx;
	  uint32_t chroma_subsample_y = comps[1].dy;
	  uint32_t units = (decompressWidth + chroma_subsample_x - 1) / chroma_subsample_x;
	  packedRowBytes =
		  (uint64_t)((((uint64_t)decompressWidth * chroma_subsample_y + units * 2U) * prec + 7U) /
					 8U);
	  rowsPerStrip = (uint32_t)((chroma_subsample_y * 8 * 1024 * 1024) / packedRowBytes);
   }
   else
   {
	  switch(decompressFormat)
	  {
		 case GRK_FMT_BMP:
			packedRowBytes = (((uint64_t)ncmp * decompressWidth + 3) >> 2) << 2;
			break;
		 case GRK_FMT_PXM:
			packedRowBytes = grk::PlanarToInterleaved<int32_t>::getPackedBytes(
				ncmp, decompressWidth, prec > 8 ? 16 : 8);
			break;
		 default:
			packedRowBytes =
				grk::PlanarToInterleaved<int32_t>::getPackedBytes(ncmp, decompressWidth, prec);
			break;
	  }
	  rowsPerStrip = hasMultipleTiles ? ceildivpow2(cp->t_height, cp->coding_params_.dec_.reduce_)
									  : singleTileRowsPerStrip;
   }
   if(rowsPerStrip > height())
	  rowsPerStrip = height();

   if(meta && meta->color.icc_profile_buf && meta->color.icc_profile_len &&
	  decompressFormat == GRK_FMT_PNG)
   {
	  // extract the description tag from the ICC header,
	  // and use this tag as the profile name
	  auto in_prof =
		  cmsOpenProfileFromMem(meta->color.icc_profile_buf, meta->color.icc_profile_len);
	  if(in_prof)
	  {
		 cmsUInt32Number bufferSize = cmsGetProfileInfoASCII(
			 in_prof, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
		 if(bufferSize)
		 {
			std::unique_ptr<char[]> description(new char[bufferSize]);
			cmsUInt32Number result =
				cmsGetProfileInfoASCII(in_prof, cmsInfoDescription, cmsNoLanguage, cmsNoCountry,
									   description.get(), bufferSize);
			if(result)
			{
			   std::string profileName = description.get();
			   auto len = profileName.length();
			   meta->color.icc_profile_name = new char[len + 1];
			   memcpy(meta->color.icc_profile_name, profileName.c_str(), len);
			   meta->color.icc_profile_name[len] = 0;
			}
		 }
		 cmsCloseProfile(in_prof);
	  }
   }
}
bool GrkImage::validateZeroed(void)
{
   for(uint16_t compno = 0; compno < numcomps; compno++)
   {
	  auto comp = comps + compno;
	  if(comp->data)
	  {
		 for(uint32_t j = 0; j < comp->stride * comp->h; ++j)
		 {
			assert(comp->data[j] == 0);
			if(comp->data[j] != 0)
			   return false;
		 }
	  }
   }

   return true;
}
void GrkImage::allocPalette(uint8_t num_channels, uint16_t num_entries)
{
   ((GrkImageMeta*)meta)->allocPalette(num_channels, num_entries);
}
bool GrkImage::applyColour(void)
{
   if(meta->color.palette)
   {
	  /* Part 1, I.5.3.4: Either both or none : */
	  if(!meta->color.palette->component_mapping)
		 ((GrkImageMeta*)meta)->releaseColorPalatte();
	  else if(!apply_palette_clr())
		 return false;
   }
   if(meta->color.channel_definition)
	  apply_channel_definition();

   return true;
}
void GrkImage::apply_channel_definition()
{
   if(channelDefinitionApplied_)
	  return;

   auto info = meta->color.channel_definition->descriptions;
   uint16_t n = meta->color.channel_definition->num_channel_descriptions;
   for(uint16_t i = 0; i < n; ++i)
   {
	  /* WATCH: asoc_index = asoc - 1 ! */
	  uint16_t asoc = info[i].asoc;
	  uint16_t channel = info[i].channel;

	  if(channel >= numcomps)
	  {
		 Logger::logger_.warn("apply_channel_definition: channel=%u, numcomps=%u", channel,
							  numcomps);
		 continue;
	  }
	  comps[channel].type = (GRK_CHANNEL_TYPE)info[i].typ;

	  // no need to do anything further if this is not a colour channel,
	  // or if this channel is associated with the whole image
	  if(info[i].typ != GRK_CHANNEL_TYPE_COLOUR || info[i].asoc == GRK_CHANNEL_ASSOC_WHOLE_IMAGE)
		 continue;

	  if(info[i].typ == GRK_CHANNEL_TYPE_COLOUR && asoc > numcomps)
	  {
		 Logger::logger_.warn("apply_channel_definition: association=%u > numcomps=%u", asoc,
							  numcomps);
		 continue;
	  }
	  uint16_t asoc_index = (uint16_t)(asoc - 1);

	  /* Swap only if color channel */
	  if((channel != asoc_index) && (info[i].typ == GRK_CHANNEL_TYPE_COLOUR))
	  {
		 grk_image_comp saved;
		 uint16_t j;

		 memcpy(&saved, &comps[channel], sizeof(grk_image_comp));
		 memcpy(&comps[channel], &comps[asoc_index], sizeof(grk_image_comp));
		 memcpy(&comps[asoc_index], &saved, sizeof(grk_image_comp));

		 /* Swap channels in following channel definitions, don't bother with j <= i that are
		  * already processed */
		 for(j = (uint16_t)(i + 1U); j < n; ++j)
		 {
			if(info[j].channel == channel)
			   info[j].channel = asoc_index;
			else if(info[j].channel == asoc_index)
			   info[j].channel = channel;
			/* asoc is related to color index. Do not update. */
		 }
	  }
   }
   channelDefinitionApplied_ = true;
}
bool GrkImage::check_color(void)
{
   uint16_t i;
   auto clr = &meta->color;
   if(clr->channel_definition)
   {
	  auto info = clr->channel_definition->descriptions;
	  uint16_t n = clr->channel_definition->num_channel_descriptions;
	  uint32_t num_channels =
		  numcomps; /* FIXME image->numcomps == numcomps before color is applied ??? */

	  /* cdef applies to component_mapping channels if any */
	  if(clr->palette && clr->palette->component_mapping)
		 num_channels = (uint32_t)clr->palette->num_channels;
	  for(i = 0; i < n; i++)
	  {
		 if(info[i].channel >= num_channels)
		 {
			Logger::logger_.error("Invalid channel index %u (>= %u).", info[i].channel,
								  num_channels);
			return false;
		 }
		 if(info[i].asoc == GRK_CHANNEL_ASSOC_UNASSOCIATED)
			continue;
		 if(info[i].asoc > 0 && (uint32_t)(info[i].asoc - 1) >= num_channels)
		 {
			Logger::logger_.error("Invalid component association %u  (>= %u).", info[i].asoc - 1,
								  num_channels);
			return false;
		 }
	  }
	  /* issue 397 */
	  /* ISO 15444-1 states that if cdef is present, it shall contain a complete list of channel
	   * definitions. */
	  while(num_channels > 0)
	  {
		 for(i = 0; i < n; ++i)
		 {
			if((uint32_t)info[i].channel == (num_channels - 1U))
			   break;
		 }
		 if(i == n)
		 {
			Logger::logger_.error("Incomplete channel definitions.");
			return false;
		 }
		 --num_channels;
	  }
   }
   if(clr->palette && clr->palette->component_mapping)
   {
	  uint16_t num_channels = clr->palette->num_channels;
	  auto component_mapping = clr->palette->component_mapping;
	  bool* pcol_usage = nullptr;
	  bool is_sane = true;

	  /* verify that all original components match an existing one */
	  for(i = 0; i < num_channels; i++)
	  {
		 if(component_mapping[i].component_index >= numcomps)
		 {
			Logger::logger_.error("Invalid component index %u (>= %u).",
								  component_mapping[i].component_index, numcomps);
			is_sane = false;
			goto cleanup;
		 }
	  }
	  pcol_usage = (bool*)grk_calloc(num_channels, sizeof(bool));
	  if(!pcol_usage)
	  {
		 Logger::logger_.error("Unexpected OOM.");
		 return false;
	  }
	  /* verify that no component is targeted more than once */
	  for(i = 0; i < num_channels; i++)
	  {
		 uint16_t palette_column = component_mapping[i].palette_column;
		 if(component_mapping[i].mapping_type != 0 && component_mapping[i].mapping_type != 1)
		 {
			Logger::logger_.error("Unexpected MTYP value.");
			is_sane = false;
			goto cleanup;
		 }
		 if(palette_column >= num_channels)
		 {
			Logger::logger_.error("Invalid component/palette index for direct mapping %u.",
								  palette_column);
			is_sane = false;
			goto cleanup;
		 }
		 else if(pcol_usage[palette_column] && component_mapping[i].mapping_type == 1)
		 {
			Logger::logger_.error("Component %u is mapped twice.", palette_column);
			is_sane = false;
			goto cleanup;
		 }
		 else if(component_mapping[i].mapping_type == 0 && component_mapping[i].palette_column != 0)
		 {
			/* I.5.3.5 PCOL: If the value of the MTYP field for this channel is 0, then
			 * the value of this field shall be 0. */
			Logger::logger_.error("Direct use at #%u however palette_column=%u.", i,
								  palette_column);
			is_sane = false;
			goto cleanup;
		 }
		 else
			pcol_usage[palette_column] = true;
	  }
	  /* verify that all components are targeted at least once */
	  for(i = 0; i < num_channels; i++)
	  {
		 if(!pcol_usage[i] && component_mapping[i].mapping_type != 0)
		 {
			Logger::logger_.error("Component %u doesn't have a mapping.", i);
			is_sane = false;
			goto cleanup;
		 }
	  }
	  /* Issue 235/447 weird component_mapping */
	  if(is_sane && (numcomps == 1U))
	  {
		 for(i = 0; i < num_channels; i++)
		 {
			if(!pcol_usage[i])
			{
			   is_sane = false;
			   Logger::logger_.warn("Component mapping seems wrong. Trying to correct.", i);
			   break;
			}
		 }
		 if(!is_sane)
		 {
			is_sane = true;
			for(i = 0; i < num_channels; i++)
			{
			   component_mapping[i].mapping_type = 1U;
			   component_mapping[i].palette_column = (uint8_t)i;
			}
		 }
	  }
   cleanup:
	  grk_free(pcol_usage);
	  if(!is_sane)
		 return false;
   }

   return true;
}
bool GrkImage::apply_palette_clr()
{
   if(paletteApplied_)
	  return true;

   auto clr = &meta->color;
   auto pal = clr->palette;
   auto channel_prec = pal->channel_prec;
   auto channel_sign = pal->channel_sign;
   auto lut = pal->lut;
   auto component_mapping = pal->component_mapping;
   uint16_t num_channels = pal->num_channels;

   // sanity check on component mapping
   for(uint16_t channel = 0; channel < num_channels; ++channel)
   {
	  auto mapping = component_mapping + channel;
	  uint16_t compno = mapping->component_index;
	  auto comp = comps + compno;
	  if(compno >= numcomps)
	  {
		 Logger::logger_.error(
			 "apply_palette_clr: component mapping component number %u for channel %u "
			 "must be less than number of image components %u",
			 compno, channel, numcomps);
		 return false;
	  }
	  if(comp->data == nullptr)
	  {
		 Logger::logger_.error("comps[%u].data == nullptr"
							   " in apply_palette_clr().",
							   compno);
		 return false;
	  }
	  if(comp->prec > pal->num_entries)
	  {
		 Logger::logger_.error("Precision %u of component %u is greater than "
							   "number of palette entries %u",
							   compno, comps[compno].prec, pal->num_entries);
		 return false;
	  }
	  uint16_t paletteColumn = mapping->palette_column;
	  switch(mapping->mapping_type)
	  {
		 case 0:
			if(paletteColumn != 0)
			{
			   Logger::logger_.error("apply_palette_clr: channel %u with direct component mapping: "
									 "non-zero palette column %u not allowed",
									 channel, paletteColumn);
			   return false;
			}
			break;
		 case 1:
			if(comp->sgnd)
			{
			   Logger::logger_.error(
				   "apply_palette_clr: channel %u with non-direct component mapping: "
				   "cannot be signed",
				   channel);
			   return false;
			}
			break;
	  }
   }
   auto oldComps = comps;
   auto newComps = new grk_image_comp[num_channels];
   memset(newComps, 0, num_channels * sizeof(grk_image_comp));
   for(uint16_t channel = 0; channel < num_channels; ++channel)
   {
	  auto mapping = component_mapping + channel;
	  uint16_t palette_column = mapping->palette_column;
	  uint16_t compno = mapping->component_index;
	  // Direct mapping
	  uint16_t componentIndex = channel;

	  if(mapping->mapping_type != 0)
		 componentIndex = palette_column;

	  newComps[componentIndex] = oldComps[compno];
	  newComps[componentIndex].data = nullptr;

	  if(!GrkImage::allocData(newComps + channel))
	  {
		 while(channel > 0)
		 {
			--channel;
			grk_aligned_free(newComps[channel].data);
		 }
		 delete[] newComps;
		 Logger::logger_.error("Memory allocation failure in apply_palette_clr().");
		 return false;
	  }
	  newComps[channel].prec = channel_prec[channel];
	  newComps[channel].sgnd = channel_sign[channel];
   }
   int32_t top_k = pal->num_entries - 1;
   for(uint16_t channel = 0; channel < num_channels; ++channel)
   {
	  /* Palette mapping: */
	  auto mapping = component_mapping + channel;
	  uint16_t compno = mapping->component_index;
	  uint16_t palette_column = mapping->palette_column;
	  auto src = oldComps[compno].data;
	  switch(mapping->mapping_type)
	  {
		 case 0: {
			size_t num_pixels = (size_t)newComps[channel].stride * newComps[channel].h;
			memcpy(newComps[channel].data, src, num_pixels * sizeof(int32_t));
		 }
		 break;
		 case 1: {
			auto dst = newComps[palette_column].data;
			uint32_t diff =
				(uint32_t)(newComps[palette_column].stride - newComps[palette_column].w);
			size_t ind = 0;
			// note: 1 <= n <= 255
			for(uint32_t n = 0; n < newComps[palette_column].h; ++n)
			{
			   for(uint32_t m = 0; m < newComps[palette_column].w; ++m)
			   {
				  int32_t k = 0;
				  if((k = src[ind]) < 0)
					 k = 0;
				  else if(k > top_k)
					 k = top_k;
				  dst[ind++] = (int32_t)lut[k * num_channels + palette_column];
			   }
			   ind += diff;
			}
		 }
		 break;
	  }
   }
   for(uint16_t i = 0; i < numcomps; ++i)
	  single_component_data_free(oldComps + i);
   delete[] oldComps;
   comps = newComps;
   numcomps = num_channels;
   paletteApplied_ = true;

   return true;
}
bool GrkImage::allocCompositeData(void)
{
   // only allocate data if there are multiple tiles. Otherwise, the single tile data
   // will simply be transferred to the output image
   if(!hasMultipleTiles)
	  return true;

   for(uint32_t i = 0; i < numcomps; i++)
   {
	  auto destComp = comps + i;
	  if(destComp->w == 0 || destComp->h == 0)
	  {
		 Logger::logger_.error("Output component %u has invalid dimensions %u x %u", i, destComp->w,
							   destComp->h);
		 return false;
	  }
	  if(!destComp->data)
	  {
		 if(!GrkImage::allocData(destComp, true))
		 {
			Logger::logger_.error(
				"Failed to allocate pixel data for component %u, with dimensions %u x %u", i,
				destComp->w, destComp->h);
			return false;
		 }
	  }
   }

   return true;
}

/**
 Transfer data to dest for each component, and null out this data.
 Assumption:  this and dest have the same number of components
 */
void GrkImage::transferDataTo(GrkImage* dest)
{
   if(!dest || !comps || !dest->comps || numcomps != dest->numcomps)
	  return;

   for(uint16_t compno = 0; compno < numcomps; compno++)
   {
	  auto srcComp = comps + compno;
	  auto destComp = dest->comps + compno;

	  single_component_data_free(destComp);
	  destComp->data = srcComp->data;
	  if(srcComp->stride)
	  {
		 destComp->stride = srcComp->stride;
		 assert(destComp->stride >= destComp->w);
	  }
	  srcComp->data = nullptr;
   }

   dest->interleavedData.data_ = interleavedData.data_;
   interleavedData.data_ = nullptr;
}

/**
 * Create new image and transfer tile buffer data
 *
 * @param src	tile source
 *
 * @return new GrkImage if successful
 *
 */
GrkImage* GrkImage::duplicate(const Tile* src)
{
   auto destImage = new GrkImage();
   copyHeader(destImage);
   destImage->x0 = src->x0;
   destImage->y0 = src->y0;
   destImage->x1 = src->x1;
   destImage->y1 = src->y1;

   for(uint16_t compno = 0; compno < src->numcomps_; ++compno)
   {
	  auto srcComp = src->comps + compno;
	  auto src_buffer = srcComp->getWindow();
	  auto src_bounds = src_buffer->bounds();

	  auto destComp = destImage->comps + compno;
	  destComp->x0 = src_bounds.x0;
	  destComp->y0 = src_bounds.y0;
	  destComp->w = src_bounds.width();
	  destComp->h = src_bounds.height();
   }

   destImage->transferDataFrom(src);

   return destImage;
}

void GrkImage::transferDataFrom(const Tile* tile_src_data)
{
   for(uint16_t compno = 0; compno < numcomps; compno++)
   {
	  auto srcComp = tile_src_data->comps + compno;
	  auto destComp = comps + compno;

	  // transfer memory from tile component to output image
	  single_component_data_free(destComp);
	  srcComp->getWindow()->transfer(&destComp->data, &destComp->stride);
	  if(destComp->data)
		 assert(destComp->stride >= destComp->w);
   }
}

bool GrkImage::composite(const GrkImage* srcImg)
{
   return interleavedData.data_ ? compositeInterleaved(srcImg) : compositePlanar(srcImg);
}

/**
 * Interleave strip of tile data and copy to interleaved composite image
 *
 * @param srcImg 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositeInterleaved(const Tile* src, uint32_t yBegin, uint32_t yEnd)
{
   auto srcComp = src->comps;
   auto destComp = comps;
   grk_rect32 destWin;
   grk_rect32 srcWin(srcComp->x0, srcComp->y0 + yBegin, srcComp->x0 + srcComp->width(),
					 srcComp->y0 + yEnd);

   if(!generateCompositeBounds(srcWin, 0, &destWin))
   {
	  Logger::logger_.warn("GrkImage::compositeInterleaved: cannot generate composite bounds");
	  return false;
   }
   for(uint16_t i = 0; i < src->numcomps_; ++i)
   {
	  if(!(src->comps + i)->getWindow()->getResWindowBufferHighestSimple().buf_)
	  {
		 Logger::logger_.warn("GrkImage::compositeInterleaved: null data for source component %u",
							  i);
		 return false;
	  }
   }
   uint8_t prec = 0;
   switch(decompressFormat)
   {
	  case GRK_FMT_TIF:
		 prec = destComp->prec;
		 break;
	  case GRK_FMT_PXM:
		 prec = destComp->prec > 8 ? 16 : 8;
		 break;
	  default:
		 return false;
		 break;
   }
   auto destStride =
	   grk::PlanarToInterleaved<int32_t>::getPackedBytes(src->numcomps_, destComp->w, prec);
   auto destx0 =
	   grk::PlanarToInterleaved<int32_t>::getPackedBytes(src->numcomps_, destWin.x0, prec);
   auto destIndex = (uint64_t)destWin.y0 * destStride + (uint64_t)destx0;
   auto iter = InterleaverFactory<int32_t>::makeInterleaver(
	   prec == 16 && decompressFormat != GRK_FMT_TIF ? packer16BitBE : prec);
   if(!iter)
	  return false;
   int32_t const* planes[grk::maxNumPackComponents];
   for(uint16_t i = 0; i < src->numcomps_; ++i)
   {
	  auto b = (src->comps + i)->getWindow()->getResWindowBufferHighestSimple();
	  planes[i] = b.buf_ + yBegin * b.stride_;
   }
   iter->interleave(const_cast<int32_t**>(planes), src->numcomps_,
					interleavedData.data_ + destIndex, destWin.width(),
					srcComp->getWindow()->getResWindowBufferHighestStride(), destStride,
					destWin.height(), 0);
   delete iter;

   return true;
}

/**
 * Interleave image data and copy to interleaved composite image
 *
 * @param src 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositeInterleaved(const GrkImage* src)
{
   auto srcComp = src->comps;
   auto destComp = comps;
   grk_rect32 destWin;

   if(!generateCompositeBounds(srcComp, 0, &destWin))
   {
	  Logger::logger_.warn("GrkImage::compositeInterleaved: cannot generate composite bounds");
	  return false;
   }
   for(uint16_t i = 0; i < src->numcomps; ++i)
   {
	  if(!(src->comps + i)->data)
	  {
		 Logger::logger_.warn("GrkImage::compositeInterleaved: null data for source component %u",
							  i);
		 return false;
	  }
   }
   uint8_t prec = 0;
   switch(decompressFormat)
   {
	  case GRK_FMT_TIF:
		 prec = destComp->prec;
		 break;
	  case GRK_FMT_PXM:
		 prec = destComp->prec > 8 ? 16 : 8;
		 break;
	  default:
		 return false;
		 break;
   }
   auto destStride =
	   grk::PlanarToInterleaved<int32_t>::getPackedBytes(src->numcomps, destComp->w, prec);
   auto destx0 = grk::PlanarToInterleaved<int32_t>::getPackedBytes(src->numcomps, destWin.x0, prec);
   auto destIndex = (uint64_t)destWin.y0 * destStride + (uint64_t)destx0;
   auto iter = InterleaverFactory<int32_t>::makeInterleaver(prec == 16 ? packer16BitBE : prec);
   if(!iter)
	  return false;
   int32_t const* planes[grk::maxNumPackComponents];
   for(uint16_t i = 0; i < src->numcomps; ++i)
	  planes[i] = (src->comps + i)->data;
   iter->interleave(const_cast<int32_t**>(planes), src->numcomps, interleavedData.data_ + destIndex,
					destWin.width(), srcComp->stride, destStride, destWin.height(), 0);
   delete iter;

   return true;
}

/**
 * Copy planar image data to planar composite image
 *
 * @param src 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositePlanar(const GrkImage* src)
{
   for(uint16_t compno = 0; compno < src->numcomps; compno++)
   {
	  auto srcComp = src->comps + compno;
	  grk_rect32 destWin;
	  if(!generateCompositeBounds(srcComp, compno, &destWin))
	  {
		 Logger::logger_.warn(
			 "GrkImage::compositePlanar: cannot generate composite bounds for component %u",
			 compno);
		 continue;
	  }
	  auto destComp = comps + compno;
	  if(!destComp->data)
	  {
		 Logger::logger_.warn("GrkImage::compositePlanar: null data for destination component %u",
							  compno);
		 continue;
	  }

	  if(!srcComp->data)
	  {
		 Logger::logger_.warn("GrkImage::compositePlanar: null data for source component %u",
							  compno);
		 continue;
	  }
	  size_t srcIndex = 0;
	  auto destIndex = (size_t)destWin.x0 + (size_t)destWin.y0 * destComp->stride;
	  size_t destLineOffset = (size_t)destComp->stride - (size_t)destWin.width();
	  auto src_ptr = srcComp->data;
	  uint32_t srcLineOffset = srcComp->stride - srcComp->w;
	  for(uint32_t j = 0; j < destWin.height(); ++j)
	  {
		 memcpy(destComp->data + destIndex, src_ptr + srcIndex, destWin.width() * sizeof(int32_t));
		 destIndex += destLineOffset + destWin.width();
		 srcIndex += srcLineOffset + destWin.width();
	  }
   }

   return true;
}
/***
 * Generate destination window (relative to destination component bounds)
 * Assumption: source region is wholly contained inside destination component region
 */
bool GrkImage::generateCompositeBounds(grk_rect32 src, uint16_t destCompno, grk_rect32* destWin)
{
   auto destComp = comps + destCompno;
   *destWin = src.intersection(grk_rect32(destComp->x0, destComp->y0, destComp->x0 + destComp->w,
										  destComp->y0 + destComp->h))
				  .pan(-(int64_t)destComp->x0, -(int64_t)destComp->y0);
   return true;
}
/***
 * Generate destination window (relative to destination component bounds)
 * Assumption: source region is wholly contained inside destinatin component region
 */
bool GrkImage::generateCompositeBounds(const grk_image_comp* srcComp, uint16_t destCompno,
									   grk_rect32* destWin)
{
   return generateCompositeBounds(
	   grk_rect32(srcComp->x0, srcComp->y0, srcComp->x0 + srcComp->w, srcComp->y0 + srcComp->h),
	   destCompno, destWin);
}

void GrkImage::single_component_data_free(grk_image_comp* comp)
{
   if(!comp || !comp->data)
	  return;
   grk_aligned_free(comp->data);
   comp->data = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////
GrkImageMeta::GrkImageMeta()
{
   obj.wrapper = new GrkObjectWrapperImpl(this);
   iptc_buf = nullptr;
   iptc_len = 0;
   xmp_buf = nullptr;
   xmp_len = 0;
   memset(&color, 0, sizeof(color));
}

GrkImageMeta::~GrkImageMeta()
{
   releaseColor();
   delete[] iptc_buf;
   delete[] xmp_buf;
}
void GrkImageMeta::allocPalette(uint8_t num_channels, uint16_t num_entries)
{
   assert(num_channels);
   assert(num_entries);

   if(!num_channels || !num_entries)
	  return;

   releaseColorPalatte();
   auto jp2_pclr = new grk_palette_data();
   jp2_pclr->channel_sign = new bool[num_channels];
   jp2_pclr->channel_prec = new uint8_t[num_channels];
   jp2_pclr->lut = new int32_t[num_channels * num_entries];
   jp2_pclr->num_entries = num_entries;
   jp2_pclr->num_channels = num_channels;
   jp2_pclr->component_mapping = nullptr;
   color.palette = jp2_pclr;
}
void GrkImageMeta::releaseColorPalatte()
{
   if(color.palette)
   {
	  delete[] color.palette->channel_sign;
	  delete[] color.palette->channel_prec;
	  delete[] color.palette->lut;
	  delete[] color.palette->component_mapping;
	  delete color.palette;
	  color.palette = nullptr;
   }
}
void GrkImageMeta::releaseColor()
{
   releaseColorPalatte();
   delete[] color.icc_profile_buf;
   color.icc_profile_buf = nullptr;
   color.icc_profile_len = 0;
   delete[] color.icc_profile_name;
   color.icc_profile_name = nullptr;
   if(color.channel_definition)
   {
	  delete[] color.channel_definition->descriptions;
	  delete color.channel_definition;
	  color.channel_definition = nullptr;
   }
}

} // namespace grk
