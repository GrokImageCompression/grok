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
#include <grk_includes.h>
#include "SparseCanvas.h"
#include "ImageComponentFlow.h"
#include "IStream.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"

#include "CoderPool.h"
#include "BitIO.h"

#include "TagTree.h"

#include "CodeblockCompress.h"

#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"

#include "TileComponentWindow.h"
#include "canvas/tile/Tile.h"

namespace grk
{
GrkImage::GrkImage()
{
  *(grk_image*)(this) = {};
  obj.wrapper = new GrkObjectWrapperImpl(this);
  rows_per_task = singleTileRowsPerStrip;
}
GrkImage::~GrkImage()
{
  if(comps)
  {
    all_components_data_free();
    delete[] comps;
  }
  grk_unref(meta);
  grk_aligned_free(interleaved_data.data);
}
uint32_t GrkImage::width(void) const
{
  return x1 - x0;
}
uint32_t GrkImage::height(void) const
{
  return y1 - y0;
}

Rect32 GrkImage::getBounds(void) const
{
  return Rect32(x0, y0, x1, y1);
}

void GrkImage::print(void) const
{
  grklog.info("bounds: [%u,%u,%u,%u]", x0, y0, x1, y1);
  for(uint16_t i = 0; i < numcomps; ++i)
  {
    auto comp = comps + i;
    grklog.info("component %d bounds : [%u,%u,%u,%u]", i, comp->x0, comp->y0, comp->w, comp->h);
  }
}

size_t GrkImage::sizeOfDataType(grk_data_type type)
{
  switch(type)
  {
    case GRK_INT_32:
      return 4;
    case GRK_INT_16:
      return 2;
    case GRK_INT_8:
      return 1;
    case GRK_FLOAT:
      return 4;
    case GRK_DOUBLE:
      return 8;
    default:
      return 0;
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
  dest->crg_x = src->crg_x;
  dest->crg_y = src->crg_y;
  dest->prec = src->prec;
  dest->sgnd = src->sgnd;
  dest->type = src->type;
  dest->data_type = src->data_type;
}

bool GrkImage::componentsEqual(uint16_t firstNComponents, bool checkPrecision) const
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
bool GrkImage::componentsEqual(bool checkPrecision) const
{
  return componentsEqual(numcomps, checkPrecision);
}
bool GrkImage::componentsEqual(grk_image_comp* src, grk_image_comp* dest, bool checkPrecision) const
{
  if(checkPrecision && dest->prec != src->prec)
    return false;

  return (dest->dx == src->dx && dest->dy == src->dy && dest->w == src->w &&
          dest->stride == src->stride && dest->h == src->h && dest->x0 == src->x0 &&
          dest->y0 == src->y0 && dest->crg_x == src->crg_x && dest->crg_y == src->crg_y &&
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
  image->decompress_num_comps = numcmpts;
  image->decompress_width = cmptparms->w;
  image->decompress_height = cmptparms->h;
  image->decompress_prec = cmptparms->prec;
  image->decompress_colour_space = clrspc;
  if(src)
  {
    image->decompress_fmt = src->decompress_fmt;
    image->force_rgb = src->force_rgb;
    image->upsample = src->upsample;
    image->precision = src->precision;
    image->num_precision = src->num_precision;
    image->rows_per_strip = src->rows_per_strip;
    image->packed_row_bytes = src->packed_row_bytes;
  }

  /* allocate memory for the per-component information */
  image->comps = new grk_image_comp[image->numcomps];
  memset(image->comps, 0, image->numcomps * sizeof(grk_image_comp));

  /* create the individual image components */
  for(uint16_t compno = 0; compno < numcmpts; compno++)
  {
    auto comp = &image->comps[compno];
    auto params = cmptparms + compno;

    comp->dx = params->dx == 0 ? 1 : params->dx;
    comp->dy = params->dy == 0 ? 1 : params->dy;
    comp->w = params->w;
    comp->h = params->h;
    comp->x0 = params->x0;
    comp->y0 = params->y0;
    comp->prec = params->prec;
    comp->sgnd = params->sgnd;
    if(doAllocation && !allocData(comp))
    {
      grk::grklog.error("Unable to allocate memory for image.");
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
/***
 * Check if decompress format requires conversion
 */
bool GrkImage::needsConversionToRGB(void) const
{
  return ((color_space == GRK_CLRSPC_SYCC || color_space == GRK_CLRSPC_EYCC ||
           color_space == GRK_CLRSPC_CMYK) &&
          (decompress_fmt != GRK_FMT_UNK && decompress_fmt != GRK_FMT_TIF)) ||
         force_rgb;
}

bool GrkImage::subsampleAndReduce(uint8_t reduce)
{
  for(uint16_t compno = 0; compno < numcomps; ++compno)
  {
    auto comp = comps + compno;
    if(!comp)
      continue;
    assert((comp->stride && comp->data) || (!comp->stride && !comp->data));
    Rect32 c;

    // sub-sample and reduce component origin
    c.x0 = ceildiv<uint32_t>(x0, comp->dx);
    c.y0 = ceildiv<uint32_t>(y0, comp->dy);

    c.x0 = ceildivpow2<uint32_t>(c.x0, reduce);
    c.y0 = ceildivpow2<uint32_t>(c.y0, reduce);

    uint32_t comp_x1 = ceildiv<uint32_t>(x1, comp->dx);
    comp_x1 = ceildivpow2<uint32_t>(comp_x1, reduce);
    if(comp_x1 <= c.x0)
    {
      grklog.error("subsampleAndReduce: component %u: x1 (%u) is <= x0 (%u). Subsampled and "
                   "reduced image is invalid",
                   compno, comp_x1, c.x0);
      return false;
    }
    uint32_t w = (uint32_t)(comp_x1 - c.x0);
    assert(w);
    uint32_t comp_y1 = ceildiv<uint32_t>(y1, comp->dy);
    comp_y1 = ceildivpow2<uint32_t>(comp_y1, reduce);
    if(comp_y1 <= comp->y0)
    {
      grklog.error("subsampleAndReduce: component %u: y1 (%u) is <= y0 (%u).  Subsampled and "
                   "reduced image is invalid",
                   compno, comp_y1, comp->y0);
      return false;
    }
    uint32_t h = (uint32_t)(comp_y1 - c.y0);
    assert(h);
    bool needsAlloc = (comp->w != w || comp->h != h);
    comp->x0 = c.x0;
    comp->y0 = c.y0;
    comp->w = w;
    comp->h = h;
    if(comp->data)
    {
      if(needsAlloc)
        allocData(comp);
      else
        memset(comp->data, 0, comp->stride * comp->h * sizeOfDataType(comp->data_type));
    }
  }

  return true;
}

void GrkImage::setDataToNull(grk_image_comp* comp)
{
  comp->data = nullptr;
  comp->owns_data = false;
  comp->stride = 0;
}

/**
 * Copy only header of image and its component header (no data copied)
 * if dest image has data, it will be freed
 *
 * @param	dest	the dest image
 *
 *
 */
void GrkImage::copyHeaderTo(GrkImage* dest) const
{
  if(!dest)
    return;

  dest->x0 = x0;
  dest->y0 = y0;
  dest->x1 = x1;
  dest->y1 = y1;

  if(dest->comps)
  {
    dest->all_components_data_free();
    delete[] dest->comps;
    dest->comps = nullptr;
  }
  dest->numcomps = numcomps;
  dest->comps = new grk_image_comp[dest->numcomps];
  // copy components, but set data ownership to false
  for(uint16_t compno = 0; compno < dest->numcomps; compno++)
  {
    *(dest->comps + compno) = *(comps + compno);
    dest->comps[compno].owns_data = false;
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
  // dest has a reference to source meta
  if(meta)
  {
    if(dest->meta)
    {
      auto temp = (GrkImageMeta*)meta;
      grk_unref(temp);
    }
    auto temp = (GrkImageMeta*)meta;
    grk_ref(temp);
    dest->meta = meta;
  }
  dest->decompress_fmt = decompress_fmt;
  dest->decompress_num_comps = decompress_num_comps;
  dest->decompress_width = decompress_width;
  dest->decompress_height = decompress_height;
  dest->decompress_prec = decompress_prec;
  dest->decompress_colour_space = decompress_colour_space;
  dest->force_rgb = force_rgb;
  dest->upsample = upsample;
  dest->precision = precision;
  dest->has_multiple_tiles = has_multiple_tiles;
  dest->num_precision = num_precision;
  dest->rows_per_strip = rows_per_strip;
  dest->packed_row_bytes = packed_row_bytes;
}
bool GrkImage::allocData(grk_image_comp* comp)
{
  return allocData(comp, false);
}
bool GrkImage::allocData(grk_image_comp* comp, bool clear)
{
  if(!comp || comp->w == 0 || comp->h == 0)
    return false;
  single_component_data_free(comp);
  uint32_t stride = grk_make_aligned_width<int32_t>(comp->w);
  size_t dataSize = (uint64_t)stride * comp->h * sizeOfDataType(comp->data_type);
  auto data = (int32_t*)grk_aligned_malloc(dataSize);
  if(!data)
  {
    grk::grklog.error("Failed to allocate aligned memory buffer of dimensions %u x %u",
                      comp->stride, comp->h);
    return false;
  }
  if(clear)
    memset(data, 0, dataSize);
  comp->data = data;
  comp->owns_data = true;
  comp->stride = stride;

  return true;
}

bool GrkImage::isSubsampled() const
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
bool GrkImage::isOpacity(uint16_t compno) const
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
  bool isGAorRGBA = (decompress_num_comps == 4 || decompress_num_comps == 2) &&
                    isOpacity(decompress_num_comps - 1);
  if(meta && meta->color.palette)
    decompress_num_comps = meta->color.palette->num_channels;
  else
    decompress_num_comps = (force_rgb && numcomps < 3) ? 3 : numcomps;
  if(decompress_fmt == GRK_FMT_PXM && decompress_num_comps == 4 && !isGAorRGBA)
    decompress_num_comps = 3;
  uint16_t ncmp = decompress_num_comps;
  decompress_width = comps->w;
  if(isSubsampled() && (upsample || force_rgb))
    decompress_width = x1 - x0;
  decompress_height = comps->h;
  if(isSubsampled() && (upsample || force_rgb))
    decompress_height = y1 - y0;
  decompress_prec = comps->prec;
  if(precision)
    decompress_prec = precision->prec;
  decompress_colour_space = color_space;
  if(needsConversionToRGB())
    decompress_colour_space = GRK_CLRSPC_SRGB;
  bool tiffSubSampled = decompress_fmt == GRK_FMT_TIF && isSubsampled() &&
                        (color_space == GRK_CLRSPC_EYCC || color_space == GRK_CLRSPC_SYCC);
  if(tiffSubSampled)
  {
    uint32_t chroma_subsample_x = comps[1].dx;
    uint32_t chroma_subsample_y = comps[1].dy;
    uint32_t units = (decompress_width + chroma_subsample_x - 1) / chroma_subsample_x;
    packed_row_bytes =
        (uint64_t)((((uint64_t)decompress_width * chroma_subsample_y + units * 2U) * prec + 7U) /
                   8U);
    rows_per_strip = (uint32_t)((chroma_subsample_y * 8 * 1024 * 1024) / packed_row_bytes);
  }
  else
  {
    switch(decompress_fmt)
    {
      case GRK_FMT_BMP:
        packed_row_bytes = (((uint64_t)ncmp * decompress_width + 3) >> 2) << 2;
        break;
      case GRK_FMT_PXM:
        packed_row_bytes = grk::PlanarToInterleaved<int32_t>::getPackedBytes(ncmp, decompress_width,
                                                                             prec > 8 ? 16 : 8);
        break;
      default:
        packed_row_bytes =
            grk::PlanarToInterleaved<int32_t>::getPackedBytes(ncmp, decompress_width, prec);
        break;
    }
    rows_per_strip = has_multiple_tiles ? ceildivpow2(cp->t_height_, cp->codingParams_.dec_.reduce_)
                                        : singleTileRowsPerStrip;
  }
  if(rows_per_strip > height())
    rows_per_strip = height();

  if(meta && meta->color.icc_profile_buf && meta->color.icc_profile_len &&
     decompress_fmt == GRK_FMT_PNG)
  {
    // extract the description tag from the ICC header,
    // and use this tag as the profile name
    auto in_prof = cmsOpenProfileFromMem(meta->color.icc_profile_buf, meta->color.icc_profile_len);
    if(in_prof)
    {
      cmsUInt32Number bufferSize = cmsGetProfileInfoASCII(in_prof, cmsInfoDescription,
                                                          cmsNoLanguage, cmsNoCountry, nullptr, 0);
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
void GrkImage::allocPalette(uint8_t num_channels, uint16_t num_entries)
{
  ((GrkImageMeta*)meta)->allocPalette(num_channels, num_entries);
}

void GrkImage::apply_channel_definition()
{
  if(channel_definition_applied)
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
      grklog.warn("apply_channel_definition: channel=%u, numcomps=%u", channel, numcomps);
      continue;
    }
    comps[channel].type = (GRK_CHANNEL_TYPE)info[i].typ;

    // no need to do anything further if this is not a colour channel,
    // or if this channel is associated with the whole image
    if(info[i].typ != GRK_CHANNEL_TYPE_COLOUR || info[i].asoc == GRK_CHANNEL_ASSOC_WHOLE_IMAGE)
      continue;

    if(info[i].typ == GRK_CHANNEL_TYPE_COLOUR && asoc > numcomps)
    {
      grklog.warn("apply_channel_definition: association=%u > numcomps=%u", asoc, numcomps);
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
  channel_definition_applied = true;
}
bool GrkImage::check_color(uint16_t signalledNumComps)
{
  auto clr = &meta->color;
  if(clr->channel_definition)
  {
    auto info = clr->channel_definition->descriptions;
    uint16_t n = clr->channel_definition->num_channel_descriptions;
    auto cdef_info = clr->channel_definition->descriptions;
    std::set<uint16_t> channels;
    for(uint16_t j = 0; j < clr->channel_definition->num_channel_descriptions; ++j)
      channels.insert(cdef_info[j].channel);
    uint16_t num_channels = (uint16_t)channels.size();
    bool hasPalette = clr->palette && clr->palette->component_mapping;
    /* cdef applies to component_mapping channels if any */
    if(hasPalette)
      num_channels = clr->palette->num_channels;
    for(uint16_t i = 0; i < n; i++)
    {
      if(info[i].channel >= num_channels)
      {
        grklog.error("Invalid channel index %u (>= %u).", info[i].channel, num_channels);
        return false;
      }
      if(info[i].asoc == 0 || info[i].asoc == GRK_CHANNEL_ASSOC_UNASSOCIATED)
        continue;
      uint16_t ascMinusOne = (uint16_t)(info[i].asoc - 1);
      if(ascMinusOne > 2)
      {
        grklog.error("Illegal channel association %u ", info[i].asoc);
        return false;
      }
      if(hasPalette && ascMinusOne >= num_channels)
      {
        grklog.error("Invalid channel association %u for number of palette channels %u.",
                     info[i].asoc, num_channels);
        return false;
      }
      if(!hasPalette && ascMinusOne >= signalledNumComps)
      {
        grklog.error("Invalid channel association %u for number of components %u.", info[i].asoc,
                     signalledNumComps);
        return false;
      }
    }
    /* issue 397 */
    /* ISO 15444-1 states that if cdef is present, it shall contain a complete list of channel
     * definitions. */
    while(num_channels > 0)
    {
      uint16_t i = 0;
      for(i = 0; i < n; ++i)
      {
        if((uint32_t)info[i].channel == (num_channels - 1U))
          break;
      }
      if(i == n)
      {
        grklog.error("Incomplete channel definitions.");
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
    for(uint16_t i = 0; i < num_channels; i++)
    {
      if(component_mapping[i].component >= signalledNumComps)
      {
        grklog.error("Invalid component index %u (>= %u).", component_mapping[i].component,
                     numcomps);
        is_sane = false;
        goto cleanup;
      }
    }
    pcol_usage = (bool*)grk_calloc(num_channels, sizeof(bool));
    if(!pcol_usage)
    {
      grklog.error("Unexpected OOM.");
      return false;
    }
    /* verify that no component is targeted more than once */
    for(uint16_t i = 0; i < num_channels; i++)
    {
      uint16_t palette_column = component_mapping[i].palette_column;
      if(component_mapping[i].mapping_type != 0 && component_mapping[i].mapping_type != 1)
      {
        grklog.error("Unexpected MTYP value.");
        is_sane = false;
        goto cleanup;
      }
      if(palette_column >= num_channels)
      {
        grklog.error("Invalid component/palette index for direct mapping %u.", palette_column);
        is_sane = false;
        goto cleanup;
      }
      else if(pcol_usage[palette_column] && component_mapping[i].mapping_type == 1)
      {
        grklog.error("Component %u is mapped twice.", palette_column);
        is_sane = false;
        goto cleanup;
      }
      else if(component_mapping[i].mapping_type == 0 && component_mapping[i].palette_column != 0)
      {
        /* I.5.3.5 PCOL: If the value of the MTYP field for this channel is 0, then
         * the value of this field shall be 0. */
        grklog.error("Direct use at #%u however palette_column=%u.", i, palette_column);
        is_sane = false;
        goto cleanup;
      }
      else
        pcol_usage[palette_column] = true;
    }
    /* verify that all components are targeted at least once */
    for(uint16_t i = 0; i < num_channels; i++)
    {
      if(!pcol_usage[i] && component_mapping[i].mapping_type != 0)
      {
        grklog.error("Component %u doesn't have a mapping.", i);
        is_sane = false;
        goto cleanup;
      }
    }
    /* Issue 235/447 weird component_mapping */
    if(is_sane && (num_channels == 1U))
    {
      for(uint16_t i = 0; i < num_channels; i++)
      {
        if(!pcol_usage[i])
        {
          is_sane = false;
          grklog.warn("Component mapping seems wrong. Trying to correct.", i);
          break;
        }
      }
      if(!is_sane)
      {
        is_sane = true;
        for(uint16_t i = 0; i < num_channels; i++)
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

bool GrkImage::allocCompositeData(void)
{
  // only allocate data if there are multiple tiles. Otherwise, the single tile data
  // will simply be transferred to the composite image
  if(!has_multiple_tiles)
    return true;

  for(uint16_t i = 0; i < numcomps; i++)
  {
    auto destComp = comps + i;
    if(destComp->w == 0 || destComp->h == 0)
    {
      grklog.error("Output component %u has invalid dimensions %u x %u", i, destComp->w,
                   destComp->h);
      return false;
    }
    if(!destComp->data)
    {
      if(!GrkImage::allocData(destComp, true))
      {
        grklog.error("Failed to allocate pixel data for component %u, with dimensions %u x %u", i,
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
    destComp->owns_data = srcComp->owns_data;
    if(srcComp->stride)
    {
      assert(srcComp->data);
      destComp->stride = srcComp->stride;
      assert(destComp->stride >= destComp->w);
    }
    setDataToNull(srcComp);
  }

  dest->interleaved_data.data = interleaved_data.data;
  interleaved_data.data = nullptr;
}

GrkImage* GrkImage::duplicate(void) const
{
  auto destImage = new GrkImage();
  copyHeaderTo(destImage);
  for(uint16_t compno = 0; compno < numcomps; ++compno)
  {
    auto compDest = destImage->comps + compno;
    auto compSrc = comps + compno;
    GrkImage::allocData(compDest);
    assert(compSrc->stride <= compDest->stride);
    compDest->stride = compSrc->stride;
    assert(compSrc->w == compDest->w);
    std::memcpy(compDest->data, compSrc->data,
                (size_t)compSrc->stride * compSrc->h * sizeOfDataType(compSrc->data_type));
    assert(componentsEqual(compSrc, compDest, true));
  }

  return destImage;
}

/**
 * Create new image and transfer tile buffer data
 *
 * @param src	tile source
 *
 * @return new GrkImage if successful
 *
 */
GrkImage* GrkImage::extractFrom(const Tile* src) const
{
  auto destImage = new GrkImage();
  copyHeaderTo(destImage);
  destImage->x0 = src->x0;
  destImage->y0 = src->y0;
  destImage->x1 = src->x1;
  destImage->y1 = src->y1;

  for(uint16_t compno = 0; compno < src->numcomps_; ++compno)
  {
    auto srcComp = src->comps_ + compno;
    auto src_buffer = srcComp->getWindow();
    auto src_bounds = src_buffer->bounds();

    auto destComp = destImage->comps + compno;
    destComp->x0 = src_bounds.x0;
    destComp->y0 = src_bounds.y0;
    destComp->w = src_bounds.width();
    destComp->h = src_bounds.height();
  }

  // stride is set here
  destImage->transferDataFrom(src);

  return destImage;
}

bool GrkImage::composite(const GrkImage* srcImg)
{
  return interleaved_data.data ? compositeInterleaved<int32_t>(srcImg->numcomps, srcImg->comps)
                               : compositePlanar<int32_t>(srcImg->numcomps, srcImg->comps);
}

/***
 * Generate destination window (relative to destination component bounds)
 * Assumption: source region is wholly contained inside destination component region
 */
bool GrkImage::generateCompositeBounds(Rect32 src, uint16_t destCompno, Rect32* destWin)
{
  auto destComp = comps + destCompno;
  *destWin = src.intersection(Rect32(destComp->x0, destComp->y0, destComp->x0 + destComp->w,
                                     destComp->y0 + destComp->h))
                 .pan(-(int64_t)destComp->x0, -(int64_t)destComp->y0);
  return true;
}
/***
 * Generate destination window (relative to destination component bounds)
 * Assumption: source region is wholly contained inside destination component region
 */
bool GrkImage::generateCompositeBounds(const grk_image_comp* srcComp, uint16_t destCompno,
                                       Rect32* destWin)
{
  return generateCompositeBounds(
      Rect32(srcComp->x0, srcComp->y0, srcComp->x0 + srcComp->w, srcComp->y0 + srcComp->h),
      destCompno, destWin);
}

void GrkImage::single_component_data_free(grk_image_comp* comp)
{
  if(!comp || !comp->data || !comp->owns_data)
  {
    // assert(!comp || !comp->stride);
    return;
  }
  grk_aligned_free(comp->data);
  setDataToNull(comp);
}

/**
 * return false if :
 * 1. any component's data buffer is NULL
 * 2. any component's precision is either 0 or greater than GRK_MAX_SUPPORTED_IMAGE_PRECISION
 * 3. any component's signedness does not match another component's signedness
 * 4. any component's precision does not match another component's precision  (if equalPrecision is
 * true)
 * 5. any component's width,stride or height does not match another component's respective
 * width,stride or height
 *
 */
bool GrkImage::allComponentsSanityCheck(bool equalPrecision) const
{
  if(numcomps == 0)
    return false;
  auto comp0 = comps;

  if(!comp0->data)
  {
    grklog.error("component 0 : data is null.");
    return false;
  }
  if(comp0->prec == 0 || comp0->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
  {
    grklog.warn("component 0 precision %d is not supported.", 0, comp0->prec);
    return false;
  }

  for(uint16_t i = 1U; i < numcomps; ++i)
  {
    auto compi = comps + i;

    if(!comp0->data)
    {
      grklog.warn("component %d : data is null.", i);
      return false;
    }
    if(equalPrecision && comp0->prec != compi->prec)
    {
      grklog.warn("precision %d of component %d"
                  " differs from precision %d of component 0.",
                  compi->prec, i, comp0->prec);
      return false;
    }
    if(comp0->sgnd != compi->sgnd)
    {
      grklog.warn("signedness %d of component %d"
                  " differs from signedness %d of component 0.",
                  compi->sgnd, i, comp0->sgnd);
      return false;
    }
    if(comp0->w != compi->w)
    {
      grklog.warn("width %d of component %d"
                  " differs from width %d of component 0.",
                  compi->sgnd, i, comp0->sgnd);
      return false;
    }
    if(comp0->stride != compi->stride)
    {
      grklog.warn("stride %d of component %d"
                  " differs from stride %d of component 0.",
                  compi->sgnd, i, comp0->sgnd);
      return false;
    }
    if(comp0->h != compi->h)
    {
      grklog.warn("height %d of component %d"
                  " differs from height %d of component 0.",
                  compi->sgnd, i, comp0->sgnd);
      return false;
    }
  }
  return true;
}

grk_image* GrkImage::createRGB(uint16_t numcmpts, uint32_t w, uint32_t h, uint8_t prec)
{
  if(!numcmpts)
  {
    grklog.warn("createRGB: number of components cannot be zero.");
    return nullptr;
  }

  auto cmptparms = new grk_image_comp[numcmpts];
  for(uint16_t compno = 0U; compno < numcmpts; ++compno)
  {
    *(cmptparms + compno) = {};
    cmptparms[compno].w = w;
    cmptparms[compno].h = h;
    cmptparms[compno].prec = prec;
  }
  auto img = GrkImage::create(this, numcmpts, (grk_image_comp*)cmptparms, GRK_CLRSPC_SRGB, true);
  delete[] cmptparms;

  return img;
}

std::string GrkImage::getColourSpaceString(void) const
{
  std::string rc = "";
  switch(color_space)
  {
    case GRK_CLRSPC_UNKNOWN:
      rc = "unknown";
      break;
    case GRK_CLRSPC_SRGB:
      rc = "sRGB";
      break;
    case GRK_CLRSPC_GRAY:
      rc = "grayscale";
      break;
    case GRK_CLRSPC_SYCC:
      rc = "SYCC";
      break;
    case GRK_CLRSPC_EYCC:
      rc = "EYCC";
      break;
    case GRK_CLRSPC_CMYK:
      rc = "CMYK";
      break;
    case GRK_CLRSPC_DEFAULT_CIE:
      rc = "CIE";
      break;
    case GRK_CLRSPC_CUSTOM_CIE:
      rc = "custom CIE";
      break;
    case GRK_CLRSPC_ICC:
      rc = "ICC";
      break;
  }

  return rc;
}
std::string GrkImage::getICCColourSpaceString(cmsColorSpaceSignature color_space) const
{
  std::string rc = "";
  switch(color_space)
  {
    case cmsSigLabData:
      rc = "LAB";
      break;
    case cmsSigYCbCrData:
      rc = "YCbCr";
      break;
    case cmsSigRgbData:
      rc = "sRGB";
      break;
    case cmsSigGrayData:
      rc = "grayscale";
      break;
    case cmsSigCmykData:
      rc = "CMYK";
      break;
    default:
      rc = "Unsupported";
      break;
  }

  return rc;
}
bool GrkImage::isValidICCColourSpace(uint32_t signature) const
{
  return (signature == cmsSigXYZData) || (signature == cmsSigLabData) ||
         (signature == cmsSigLuvData) || (signature == cmsSigYCbCrData) ||
         (signature == cmsSigYxyData) || (signature == cmsSigRgbData) ||
         (signature == cmsSigGrayData) || (signature == cmsSigHsvData) ||
         (signature == cmsSigHlsData) || (signature == cmsSigCmykData) ||
         (signature == cmsSigCmyData) || (signature == cmsSigMCH1Data) ||
         (signature == cmsSigMCH2Data) || (signature == cmsSigMCH3Data) ||
         (signature == cmsSigMCH4Data) || (signature == cmsSigMCH5Data) ||
         (signature == cmsSigMCH6Data) || (signature == cmsSigMCH7Data) ||
         (signature == cmsSigMCH8Data) || (signature == cmsSigMCH9Data) ||
         (signature == cmsSigMCHAData) || (signature == cmsSigMCHBData) ||
         (signature == cmsSigMCHCData) || (signature == cmsSigMCHDData) ||
         (signature == cmsSigMCHEData) || (signature == cmsSigMCHFData) ||
         (signature == cmsSigNamedData) || (signature == cmsSig1colorData) ||
         (signature == cmsSig2colorData) || (signature == cmsSig3colorData) ||
         (signature == cmsSig4colorData) || (signature == cmsSig5colorData) ||
         (signature == cmsSig6colorData) || (signature == cmsSig7colorData) ||
         (signature == cmsSig8colorData) || (signature == cmsSig9colorData) ||
         (signature == cmsSig10colorData) || (signature == cmsSig11colorData) ||
         (signature == cmsSig12colorData) || (signature == cmsSig13colorData) ||
         (signature == cmsSig14colorData) || (signature == cmsSig15colorData) ||
         (signature == cmsSigLuvKData);
}
bool GrkImage::validateICC(void)
{
  if(!meta || !meta->color.icc_profile_buf)
    return false;

  // check if already validated
  if(color_space == GRK_CLRSPC_ICC)
    return true;

  // image colour space matches ICC colour space
  bool imageColourSpaceMatchesICCColourSpace = false;

  // image properties such as subsampling and number of components
  // are consistent with ICC colour space
  bool imagePropertiesMatchICCColourSpace = false;

  // colour space conversion is supported by the library
  // for this ICC colour space
  bool supportedICCColourSpace = false;

  uint32_t iccColourSpace = 0;
  auto in_prof = cmsOpenProfileFromMem(meta->color.icc_profile_buf, meta->color.icc_profile_len);
  if(in_prof)
  {
    iccColourSpace = cmsGetColorSpace(in_prof);
    if(!isValidICCColourSpace(iccColourSpace))
    {
      grklog.warn("Invalid ICC colour space 0x%x. Ignoring", iccColourSpace);
      cmsCloseProfile(in_prof);

      return false;
    }
    switch(iccColourSpace)
    {
      case cmsSigLabData:
        imageColourSpaceMatchesICCColourSpace =
            (color_space == GRK_CLRSPC_DEFAULT_CIE || color_space == GRK_CLRSPC_CUSTOM_CIE);
        imagePropertiesMatchICCColourSpace = numcomps >= 3;
        break;
      case cmsSigYCbCrData:
        imageColourSpaceMatchesICCColourSpace =
            (color_space == GRK_CLRSPC_SYCC || color_space == GRK_CLRSPC_EYCC);
        if(numcomps < 3)
          imagePropertiesMatchICCColourSpace = false;
        else
        {
          auto compLuma = comps;
          imagePropertiesMatchICCColourSpace =
              compLuma->dx == 1 && compLuma->dy == 1 && isSubsampled();
        }
        break;
      case cmsSigRgbData:
        imageColourSpaceMatchesICCColourSpace = color_space == GRK_CLRSPC_SRGB;
        imagePropertiesMatchICCColourSpace = numcomps >= 3 && !isSubsampled();
        supportedICCColourSpace = true;
        break;
      case cmsSigGrayData:
        imageColourSpaceMatchesICCColourSpace = color_space == GRK_CLRSPC_GRAY;
        imagePropertiesMatchICCColourSpace = numcomps <= 2;
        supportedICCColourSpace = true;
        break;
      case cmsSigCmykData:
        imageColourSpaceMatchesICCColourSpace = color_space == GRK_CLRSPC_CMYK;
        imagePropertiesMatchICCColourSpace = numcomps == 4 && !isSubsampled();
        break;
      default:
        break;
    }
    cmsCloseProfile(in_prof);
  }
  else
  {
    grklog.warn("Unable to parse ICC profile. Ignoring");
    return false;
  }
  if(!supportedICCColourSpace)
  {
    grklog.warn("Unsupported ICC colour space %s. Ignoring",
                getICCColourSpaceString((cmsColorSpaceSignature)iccColourSpace).c_str());
    return false;
  }
  if(color_space != GRK_CLRSPC_UNKNOWN && !imageColourSpaceMatchesICCColourSpace)
  {
    grklog.warn("Signalled colour space %s doesn't match ICC colour space %s. Ignoring",
                getColourSpaceString().c_str(),
                getICCColourSpaceString((cmsColorSpaceSignature)iccColourSpace).c_str());
    return false;
  }
  if(!imagePropertiesMatchICCColourSpace)
    grklog.warn(
        "Image subsampling / number of components do not match ICC colour space %s. Ignoring",
        getICCColourSpaceString((cmsColorSpaceSignature)iccColourSpace).c_str());

  if(imagePropertiesMatchICCColourSpace)
    color_space = GRK_CLRSPC_ICC;

  return imagePropertiesMatchICCColourSpace;
}

/**
 * Convert to sRGB
 */
bool GrkImage::applyColourManagement(void)
{
  if(!meta || !meta->color.icc_profile_buf)
    return true;

  bool isTiff = decompress_fmt == GRK_FMT_TIF;
  bool canStoreCIE = isTiff && color_space == GRK_CLRSPC_DEFAULT_CIE;
  bool isCIE = color_space == GRK_CLRSPC_DEFAULT_CIE || color_space == GRK_CLRSPC_CUSTOM_CIE;
  // A TIFF,PNG, BMP or JPEG image can store the ICC profile,
  // so no need to apply it in this case,
  // (unless we are forcing to RGB).
  // Otherwise, we apply the profile
  bool canStoreICC = (decompress_fmt == GRK_FMT_TIF || decompress_fmt == GRK_FMT_PNG ||
                      decompress_fmt == GRK_FMT_JPG || decompress_fmt == GRK_FMT_BMP);

  bool shouldApplyColourManagement =
      force_rgb || (decompress_fmt != GRK_FMT_UNK && meta->color.icc_profile_buf &&
                    ((isCIE && !canStoreCIE) || !canStoreICC));
  if(!shouldApplyColourManagement)
    return true;

  if(isCIE)
  {
    if(!force_rgb)
      grklog.warn(" Input image is in CIE colour space,\n"
                  "but the codec is unable to store this information in the "
                  "output file .\n"
                  "The output image will therefore be converted to sRGB before saving.");
    if(!cieLabToRGB<int32_t>())
    {
      grklog.error("Unable to convert L*a*b image to sRGB");
      return false;
    }
  }
  else
  {
    if(validateICC())
    {
      if(!force_rgb)
      {
        grklog.warn("");
        grklog.warn("The input image contains an ICC profile");
        grklog.warn("but the codec is unable to store this profile"
                    " in the output file.");
        grklog.warn("The profile will therefore be applied to the output"
                    " image before saving.");
        grklog.warn("");
      }
      if(!applyICC<int32_t>())
      {
        grklog.warn("Unable to apply ICC profile");
        return false;
      }
    }
  }

  return true;
}

bool GrkImage::greyToRGB(void)
{
  if(numcomps != 1)
    return true;

  if(!force_rgb || color_space != GRK_CLRSPC_GRAY)
    return true;

  auto new_components = new grk_image_comp[3];
  memset(new_components, 0, 3 * sizeof(grk_image_comp));
  for(uint16_t i = 0; i < 3; ++i)
  {
    auto dest = new_components + i;
    copyComponent(comps, dest);
    // alloc data for new components
    if(i > 0)
    {
      if(!allocData(dest))
      {
        delete[] new_components;
        return false;
      }
      size_t dataSize = (uint64_t)comps->stride * comps->h * sizeOfDataType(dest->data_type);
      memcpy(dest->data, comps->data, dataSize);
    }
  }

  // attach first new component to old component
  new_components->data = comps->data;
  new_components->owns_data = comps->owns_data;
  new_components->stride = comps->stride;
  comps->data = nullptr;
  comps->owns_data = false;
  all_components_data_free();
  delete[] comps;
  comps = new_components;
  numcomps = 3;
  color_space = GRK_CLRSPC_SRGB;

  return true;
}

template<typename T>
void GrkImage::transferDataFrom_T(const Tile* tile_src_data)
{
  for(uint16_t compno = 0; compno < numcomps; compno++)
  {
    auto srcComp = tile_src_data->comps_ + compno;
    auto destComp = comps + compno;

    // transfer memory from tile component to output image
    single_component_data_free(destComp);
    srcComp->getWindow()->transfer((T**)&destComp->data, &destComp->stride);
    destComp->owns_data = true;
  }
}
void GrkImage::transferDataFrom(const Tile* tile_src_data)
{
  switch(comps->data_type)
  {
    case GRK_INT_32:
      transferDataFrom_T<int32_t>(tile_src_data);
      break;
    // case GRK_INT_16:
    //   transferDataFrom_T<int16_t>(tile_src_data);
    // break;
    // case GRK_INT_8:
    //   transferDataFrom_T<int8_t>(tile_src_data);
    // break;
    // case GRK_FLOAT:
    //   transferDataFrom_T<float>(tile_src_data);
    // break;
    // case GRK_DOUBLE:
    //   transferDataFrom_T<double>(tile_src_data);
    // break;
    default:
      break;
  }
}

} // namespace grk
