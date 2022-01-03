#include <grk_includes.h>

namespace grk
{
GrkImage::GrkImage()
{
	memset((grk_image*)(this), 0, sizeof(grk_image));
	obj.wrapper = new GrkObjectWrapperImpl(this);
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
	delete[] interleavedData;
}

void GrkImage::copyComponent(grk_image_comp* src, grk_image_comp* dest){
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

bool GrkImage::componentsEqual(grk_image_comp* src, grk_image_comp* dest){
	return (dest->dx == src->dx &&
			dest->dy == src->dy &&
			dest->w == src->w &&
			dest->stride == src->stride &&
			dest->h == src->h &&
			dest->x0 == src->x0 &&
			dest->y0 == src->y0 &&
			dest->Xcrg == src->Xcrg &&
			dest->Ycrg == src->Ycrg &&
			dest->prec == src->prec &&
			dest->sgnd == src->sgnd &&
			dest->type == src->type);
}

GrkImage* GrkImage::create(grk_image *src,
							uint16_t numcmpts,
							grk_image_comp* cmptparms,
							GRK_COLOR_SPACE clrspc,
						    bool doAllocation)
{
	auto image = new GrkImage();
	image->color_space = clrspc;
	image->targetColourSpace = clrspc;
	image->numcomps = numcmpts;
	if (src) {
		image->decompressFormat = src->decompressFormat;
		image->forceRGB = src->forceRGB;
		image->upsample = src->upsample;
		image->targetColourSpace = src->targetColourSpace;
		image->precision = src->precision;
		image->numPrecision = src->numPrecision;
	}

	/* allocate memory for the per-component information */
	image->comps = new grk_image_comp[image->numcomps];
	memset(image->comps, 0, image->numcomps * sizeof(grk_image_comp));

	/* create the individual image components */
	for(uint32_t compno = 0; compno < numcmpts; compno++)
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
			grk::GRK_ERROR("Unable to allocate memory for image.");
			delete image;
			return nullptr;
		}
		comp->type = GRK_COMPONENT_TYPE_COLOUR;
		switch(compno)
		{
			case 0:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_1;
				break;
			case 1:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_2;
				break;
			case 2:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_3;
				break;
			default:
				comp->association = GRK_COMPONENT_ASSOC_UNASSOCIATED;
				comp->type = GRK_COMPONENT_TYPE_UNSPECIFIED;
				break;
		}
	}

	return image;
}

void GrkImage::all_components_data_free()
{
	uint32_t i;
	if(!comps)
		return;
	for(i = 0; i < numcomps; ++i)
		grk_image_single_component_data_free(comps + i);
}

bool GrkImage::subsampleAndReduce(uint32_t reduce)
{
	for(uint32_t compno = 0; compno < numcomps; ++compno)
	{
		auto comp = comps + compno;

		if(x0 > (uint32_t)INT_MAX || y0 > (uint32_t)INT_MAX || x1 > (uint32_t)INT_MAX ||
		   y1 > (uint32_t)INT_MAX)
		{
			GRK_ERROR("Image coordinates above INT_MAX are not supported.");
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
			GRK_ERROR("component %d: x1 (%d) is <= x0 (%d).", compno, comp_x1, comp->x0);
			return false;
		}
		comp->w = (uint32_t)(comp_x1 - comp->x0);
		assert(comp->w);

		uint32_t comp_y1 = ceildiv<uint32_t>(y1, comp->dy);
		comp_y1 = ceildivpow2<uint32_t>(comp_y1, reduce);
		if(comp_y1 <= comp->y0)
		{
			GRK_ERROR("component %d: y1 (%d) is <= y0 (%d).", compno, comp_y1, comp->y0);
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
	for(uint32_t compno = 0; compno < dest->numcomps; compno++)
	{
		memcpy(&(dest->comps[compno]), &(comps[compno]), sizeof(grk_image_comp));
		dest->comps[compno].data = nullptr;
	}

	dest->color_space = color_space;
	dest->targetColourSpace = targetColourSpace;
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
	dest->forceRGB = forceRGB;
	dest->upsample = upsample;
	dest->precision = precision;
	dest->numPrecision = numPrecision;
}

void GrkImage::createMeta()
{
	if(!meta)
		meta = new GrkImageMeta();
}

bool GrkImage::allocData(grk_image_comp* comp)
{
	if(!comp || comp->w == 0 || comp->h == 0)
		return false;
	comp->stride = grkMakeAlignedWidth(comp->w);
	assert(comp->stride);
	assert(!comp->data);

	size_t dataSize = (uint64_t)comp->stride * comp->h * sizeof(uint32_t);
	auto data = (int32_t*)grkAlignedMalloc(dataSize);
	if(!data)
	{
		grk::GRK_ERROR("Failed to allocate aligned memory buffer of dimensions %u x %u",
					   comp->stride, comp->h);
		return false;
	}
	grk_image_single_component_data_free(comp);
	comp->data = data;
	return true;
}

bool GrkImage::canAllocInterleaved(uint32_t tileWidth){
	if ( ((tileWidth * numcomps * comps->prec) & 7) != 0)
		return false;
	if (precision ||
		(decompressFormat != GRK_TIF_FMT) ||
			(color_space != GRK_CLRSPC_SRGB && color_space != GRK_CLRSPC_GRAY))
		return false;

	// check the all components are equal
	for(uint16_t compno = 1; compno < numcomps; compno++){
		if (!componentsEqual(comps, comps+compno))
			return false;
	}

	return true;
}

bool GrkImage::allocCompositeData(uint32_t tileWidth){
	if (canAllocInterleaved(tileWidth)){
		uint64_t stride =  grk::PtoI<int32_t>::getPackedBytes(numcomps, comps->w, comps->prec);
		uint64_t dataSize = (uint64_t)stride * comps->h;

		interleavedData = (uint8_t*)grkAlignedMalloc(dataSize);
		if(!interleavedData)
		{
			GRK_ERROR("Failed to allocate aligned memory buffer of dimensions %u x %u",
						   stride, comps->h);
			return false;
		}
	} else {
		for(uint32_t i = 0; i < numcomps; i++)
		{
			auto destComp = comps + i;
			if(destComp->w == 0 || destComp->h == 0)
			{
				GRK_ERROR("Output component %d has invalid dimensions %u x %u", i, destComp->w,
						  destComp->h);
				return false;
			}
			if(!destComp->data)
			{
				if(!GrkImage::allocData(destComp))
				{
					GRK_ERROR("Failed to allocate pixel data for component %d, with dimensions %u x %u",
							  i, destComp->w, destComp->h);
					return false;
				}
				memset(destComp->data, 0,
					   (uint64_t)destComp->stride * destComp->h * sizeof(int32_t));
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

	for(uint32_t compno = 0; compno < numcomps; compno++)
	{
		auto srcComp = comps + compno;
		auto destComp = dest->comps + compno;

		grk_image_single_component_data_free(destComp);
		destComp->data = srcComp->data;
		if(srcComp->stride)
		{
			destComp->stride = srcComp->stride;
			assert(destComp->stride >= destComp->w);
		}
		srcComp->data = nullptr;
	}

	dest->interleavedData = interleavedData;
	interleavedData = nullptr;
}

/**
 * Create new image and transfer tile buffer data
 *
 * @param tile_src_data	tile source data
 *
 * @return new GrkImage if successful
 *
 */
GrkImage* GrkImage::duplicate(const Tile* src_tile)
{
	auto destImage = new GrkImage();
	copyHeader(destImage);
	destImage->x0 = src_tile->x0;
	destImage->y0 = src_tile->y0;
	destImage->x1 = src_tile->x1;
	destImage->y1 = src_tile->y1;

	for(uint16_t compno = 0; compno < src_tile->numcomps; ++compno)
	{
		auto srcComp = src_tile->comps + compno;
		auto src_buffer = srcComp->getBuffer();
		auto src_bounds = src_buffer->bounds();

		auto destComp = destImage->comps + compno;
		destComp->x0 = src_bounds.x0;
		destComp->y0 = src_bounds.y0;
		destComp->w = src_bounds.width();
		destComp->h = src_bounds.height();
	}

	destImage->transferDataFrom(src_tile);

	return destImage;
}

void GrkImage::transferDataFrom(const Tile* tile_src_data)
{
	for(uint16_t compno = 0; compno < numcomps; compno++)
	{
		auto srcComp = tile_src_data->comps + compno;
		auto destComp = comps + compno;

		// transfer memory from tile component to output image
		srcComp->getBuffer()->transfer(&destComp->data, &destComp->stride);
		if(destComp->data)
			assert(destComp->stride >= destComp->w);
	}
}
bool GrkImage::generateCompositeBounds(const grk_image_comp* srcComp,
										uint16_t compno,
										grkRectU32* destWin,
										uint32_t* srcLineOffset)
{
	auto src = grkRectU32(srcComp->x0,
						  srcComp->y0,
						  srcComp->x0 + srcComp->w,
					      srcComp->y0 + srcComp->h);

	return generateCompositeBounds(compno, src, srcComp->stride,destWin, srcLineOffset);
}

bool GrkImage::composite(const GrkImage* srcImg){
	return interleavedData ? compositeInterleaved(srcImg) : compositePlanar(srcImg);
}

/**
 * Copy image data to composite image
 *
 * @param srcImg 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositeInterleaved(const GrkImage* srcImg)
{
	auto srcComp = srcImg->comps;
	auto destComp = comps;
	grkRectU32 destWin;
	uint32_t srcLineOffset;

	if(!generateCompositeBounds(srcComp, 0, &destWin, &srcLineOffset))
	{
		GRK_WARN("GrkImage::compositeInterleaved: cannot generate composite bounds");
		return false;
	}
	for (uint16_t i = 0; i < srcImg->numcomps; ++i){
		if(!(srcImg->comps + i)->data)
		{
			GRK_WARN("GrkImage::compositeInterleaved: null data for source component %d",i);
			return false;
		}
	}
	auto destStride = grk::PtoI<int32_t>::getPackedBytes(numcomps, destComp->w, destComp->prec);
	auto destx0 = grk::PtoI<int32_t>::getPackedBytes(numcomps, destWin.x0, destComp->prec);
	auto destIndex = (uint64_t)destWin.y0 * destStride + (uint64_t)destx0;
	auto iter = InterleaverFactory<int32_t>::makeInterleaver(destComp->prec);
	if (!iter)
		return false;
	int32_t const* planes[grk::maxNumPackComponents];
	for (uint16_t i = 0; i < srcImg->numcomps; ++i)
		planes[i] = (srcImg->comps + i)->data;
	iter->interleave((int32_t**)planes,
						srcImg->numcomps,
						interleavedData + destIndex,
						destWin.width(),
						srcComp->stride,
						destStride,
						destWin.height(), 0);
	delete iter;

	return true;
}

/**
 * Copy image data to composite image
 *
 * @param srcImg 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositePlanar(const GrkImage* srcImg)
{
	for(uint16_t compno = 0; compno < srcImg->numcomps; compno++)
	{
		auto srcComp = srcImg->comps + compno;
		auto destComp = comps + compno;

		grkRectU32 destWin;
		uint32_t srcLineOffset;

		if(!generateCompositeBounds(srcComp, compno, &destWin, &srcLineOffset))
		{
			GRK_WARN("GrkImage::compositePlanar: cannot generate composite bounds for component %d",
					 compno);
			continue;
		}
		if(!destComp->data)
		{
			GRK_WARN("GrkImage::compositePlanar: null data for destination component %d", compno);
			continue;
		}

		if(!srcComp->data)
		{
			GRK_WARN("GrkImage::compositePlanar: null data for source component %d", compno);
			continue;
		}

		size_t srcIndex = 0;
		auto destIndex = (size_t)destWin.x0 + (size_t)destWin.y0 * destComp->stride;
		size_t destLineOffset = (size_t)destComp->stride - (size_t)destWin.width();
		auto src_ptr = srcComp->data;
		for(uint32_t j = 0; j < destWin.height(); ++j)
		{
			memcpy(destComp->data + destIndex, src_ptr + srcIndex,destWin.width() * sizeof(int32_t));
			destIndex += destLineOffset + destWin.width();
			srcIndex  += srcLineOffset  + destWin.width();
		}
	}

	return true;
}
bool GrkImage::generateCompositeBounds(uint16_t compno,
										grkRectU32 src,
										uint32_t src_stride,
									    grkRectU32* destWin,
									    uint32_t* srcLineOffset)
{
	auto destComp = comps + compno;
	grkRectU32 destCompRect = grkRectU32(destComp->x0,
										destComp->y0,
										destComp->x0 + destComp->w,
					   	   	   	   	   	destComp->y0 + destComp->h);
	*srcLineOffset = src_stride - src.width();
	if(destCompRect.x0 < src.x0)
	{
		destWin->x0 = (uint32_t)(src.x0 - destCompRect.x0);
		if(destCompRect.x1 >= src.x1)
		{
			destWin->x1 = destWin->x0 + src.width();
		}
		else
		{
			destWin->x1 = destWin->x0 + (uint32_t)(destCompRect.x1 - src.x0);
			*srcLineOffset = src_stride - destWin->width();
		}
	}
	else
	{
		destWin->x0 = 0U;
		if(destCompRect.x1 >= src.x1)
		{
			destWin->x1 = destWin->x0 + src.width();
		}
		else
		{
			destWin->x1 = destWin->x0 + destComp->w;
			*srcLineOffset = (uint32_t)(src.x1 - destCompRect.x1);
		}
	}
	if(destCompRect.y0 < src.y0)
	{
		destWin->y0 = (uint32_t)(src.y0 - destCompRect.y0);
		destWin->y1 =
			destWin->y0 + ((destCompRect.y1 >= src.y1) ? src.height() : (uint32_t)(destCompRect.y1 - src.y0));
	}
	else
	{
		destWin->y1 = destWin->y0 + src.height();
	}
	if(destWin->width() > destComp->w || destWin->height() > destComp->h)
		return false;

	return true;
}

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
	FileFormatDecompress::free_color(&color);
	delete[] iptc_buf;
	delete[] xmp_buf;
}

} // namespace grk
