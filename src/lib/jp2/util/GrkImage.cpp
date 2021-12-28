#include <grk_includes.h>
#include "lcms2.h"

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
		grk_image_all_components_data_free(this);
		delete[] comps;
	}
	if(meta)
		grk_object_unref(&meta->obj);
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
							grk_image_cmptparm* cmptparms,
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
		grk_image_all_components_data_free(dest);
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

/**
 * Allocate data buffer to mirror "mirror" image
 *
 * @param mirror mirror image
 *
 * @return true if successful
 */
bool GrkImage::allocData(void)
{
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

/**
 * Copy image data to composite image
 *
 * @param srcImg 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositeInterleavedFrom(const GrkImage* srcImg)
{
	// check the all components are equal
	for(uint16_t compno = 1; compno < srcImg->numcomps; compno++){
		if (!componentsEqual(srcImg->comps, srcImg->comps+compno))
			return false;
	}

	auto srcComp = srcImg->comps;
	auto destComp = comps;

	grkRectU32 destWin;
	uint32_t srcLineOffset;

	if(!generateCompositeBounds(srcComp, 0, &destWin, &srcLineOffset))
	{
		GRK_WARN("GrkImage::compositeFrom: cannot generate composite bounds for component %d",
				 0);
		return false;
	}
	if(!destComp->data)
	{
		GRK_WARN("GrkImage::compositeFrom: null data for destination component %d", 0);
		return false;
	}

	if(!srcComp->data)
	{
		GRK_WARN("GrkImage::compositeFrom: null data for source component %d", 0);
		return false;
	}

	size_t srcIndex = 0;
	auto destIndex = (size_t)destWin.x0 + (size_t)destWin.y0 * destComp->stride;
	size_t destLineOffset = (size_t)destComp->stride - (size_t)destWin.width();
	for(uint32_t j = 0; j < destWin.height(); ++j)
	{
		//memcpy(destComp->data + destIndex, src_ptr + srcIndex,destWin.width() * sizeof(int32_t));

		// pack all components into destination buffer



		destIndex += destLineOffset + destWin.width();
		srcIndex  += srcLineOffset  + destWin.width();
	}

	return true;
}

/**
 * Copy image data to composite image
 *
 * @param srcImg 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositeFrom(const GrkImage* srcImg)
{
	for(uint16_t compno = 0; compno < srcImg->numcomps; compno++)
	{
		auto srcComp = srcImg->comps + compno;
		auto destComp = comps + compno;

		grkRectU32 destWin;
		uint32_t srcLineOffset;

		if(!generateCompositeBounds(srcComp, compno, &destWin, &srcLineOffset))
		{
			GRK_WARN("GrkImage::compositeFrom: cannot generate composite bounds for component %d",
					 compno);
			continue;
		}
		if(!destComp->data)
		{
			GRK_WARN("GrkImage::compositeFrom: null data for destination component %d", compno);
			continue;
		}

		if(!srcComp->data)
		{
			GRK_WARN("GrkImage::compositeFrom: null data for source component %d", compno);
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

/**
 * return false if :
 * 1. any component's data buffer is NULL
 * 2. any component's precision is either 0 or greater than GRK_MAX_SUPPORTED_IMAGE_PRECISION
 * 3. any component's signedness does not match another component's signedness
 * 4. any component's precision does not match another component's precision
 *    (if equalPrecision is true)
 *
 */
bool GrkImage::allComponentsSanityCheck(bool equalPrecision)
{
	if(numcomps == 0)
		return false;
	auto comp0 = comps;

	if(!comp0->data)
	{
		GRK_ERROR("component 0 : data is null.");
		return false;
	}
	if(comp0->prec == 0 || comp0->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
	{
		GRK_WARN("component 0 precision {} is not supported.", 0, comp0->prec);
		return false;
	}

	for(uint16_t i = 1U; i < numcomps; ++i)
	{
		auto compi = comps + i;

		if(!comp0->data)
		{
			GRK_WARN("component {} : data is null.", i);
			return false;
		}
		if(equalPrecision && comp0->prec != compi->prec)
		{
			GRK_WARN("precision {} of component {}"
						 " differs from precision {} of component 0.",
						 compi->prec, i, comp0->prec);
			return false;
		}
		if(comp0->sgnd != compi->sgnd)
		{
			GRK_WARN("signedness {} of component {}"
						 " differs from signedness {} of component 0.",
						 compi->sgnd, i, comp0->sgnd);
			return false;
		}
	}
	return true;
}

bool GrkImage::execUpsample(void)
{
	if (!upsample)
		return true;

	if(!comps)
		return false;

	grk_image_comp* new_components = nullptr;
	bool upsampleNeeded = false;

	for(uint32_t compno = 0U; compno < numcomps; ++compno)
	{
		if(!(comps + compno))
			return false;
		if((comps[compno].dx > 1U) || (comps[compno].dy > 1U))
		{
			upsampleNeeded = true;
			break;
		}
	}
	if(!upsampleNeeded)
		return true;

	new_components = new grk_image_comp[numcomps];
	memset(new_components, 0, numcomps * sizeof(grk_image_comp));
	for(uint32_t compno = 0U; compno < numcomps; ++compno)
	{
		auto new_cmp = new_components + compno;
		copyComponent(comps + compno, new_cmp);
		new_cmp->dx = 1;
		new_cmp->dy = 1;
		new_cmp->w = x1 - x0;
		new_cmp->h = y1 - y0;
		if (!allocData(new_cmp)){
			delete[] new_components;
			return false;
		}
	}

	for(uint32_t compno = 0U; compno < numcomps; ++compno)
	{
		auto new_cmp = new_components + compno;
		auto org_cmp = comps + compno;

		new_cmp->type = org_cmp->type;

		if((org_cmp->dx > 1U) || (org_cmp->dy > 1U))
		{
			auto src = org_cmp->data;
			auto dst = new_cmp->data;

			/* need to take into account dx & dy */
			uint32_t xoff = org_cmp->dx * org_cmp->x0 - x0;
			uint32_t yoff = org_cmp->dy * org_cmp->y0 - y0;
			if((xoff >= org_cmp->dx) || (yoff >= org_cmp->dy))
			{
				GRK_ERROR("upsample: Invalid image/component parameters found when upsampling");
				delete[] new_components;
				return false;
			}

			uint32_t y;
			for(y = 0U; y < yoff; ++y)
			{
				memset(dst, 0U, new_cmp->w * sizeof(int32_t));
				dst += new_cmp->stride;
			}

			if(new_cmp->h > (org_cmp->dy - 1U))
			{ /* check subtraction overflow for really small images */
				for(; y < new_cmp->h - (org_cmp->dy - 1U); y += org_cmp->dy)
				{
					uint32_t x, dy;
					uint32_t xorg = 0;
					for(x = 0U; x < xoff; ++x)
						dst[x] = 0;

					if(new_cmp->w > (org_cmp->dx - 1U))
					{ /* check subtraction overflow for really small images */
						for(; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg)
						{
							for(uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
								dst[x + dx] = src[xorg];
						}
					}
					for(; x < new_cmp->w; ++x)
						dst[x] = src[xorg];
					dst += new_cmp->stride;

					for(dy = 1U; dy < org_cmp->dy; ++dy)
					{
						memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
						dst += new_cmp->stride;
					}
					src += org_cmp->stride;
				}
			}
			if(y < new_cmp->h)
			{
				uint32_t x;
				uint32_t xorg = 0;
				for(x = 0U; x < xoff; ++x)
					dst[x] = 0;

				if(new_cmp->w > (org_cmp->dx - 1U))
				{ /* check subtraction overflow for really small images */
					for(; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg)
					{
						for(uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
							dst[x + dx] = src[xorg];
					}
				}
				for(; x < new_cmp->w; ++x)
					dst[x] = src[xorg];
				dst += new_cmp->stride;
				++y;
				for(; y < new_cmp->h; ++y)
				{
					memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
					dst += new_cmp->stride;
				}
			}
		}
		else
		{
			memcpy(new_cmp->data, org_cmp->data, org_cmp->stride * org_cmp->h * sizeof(int32_t));
		}
	}

	delete[] comps;
	comps = new_components;

	return true;
}


template<typename T>
void clip(grk_image_comp* component, uint8_t precision)
{
	uint32_t stride_diff = component->stride - component->w;
	assert(precision <= 16);
	auto data = component->data;
	T maximum = (std::numeric_limits<T>::max)();
	T minimum = (std::numeric_limits<T>::min)();
	size_t index = 0;
	for(uint32_t j = 0; j < component->h; ++j)
	{
		for(uint32_t i = 0; i < component->w; ++i)
		{
			data[index] = (int32_t)std::clamp<T>((T)data[index], minimum, maximum);
			index++;
		}
		index += stride_diff;
	}
	component->prec = precision;
}


void GrkImage::scaleComponent(grk_image_comp* component, uint8_t precision)
{
	if(component->prec == precision)
		return;
	uint32_t stride_diff = component->stride - component->w;
	auto data = component->data;
	if(component->prec < precision)
	{
		int32_t scale = 1 << (uint32_t)(precision - component->prec);
		size_t index = 0;
		for(uint32_t j = 0; j < component->h; ++j)
		{
			for(uint32_t i = 0; i < component->w; ++i)
			{
				data[index] = data[index] * scale;
				index++;
			}
			index += stride_diff;
		}
	}
	else
	{
		int32_t scale = 1 << (uint32_t)(component->prec - precision);
		size_t index = 0;
		for(uint32_t j = 0; j < component->h; ++j)
		{
			for(uint32_t i = 0; i < component->w; ++i)
			{
				data[index] = data[index] / scale;
				index++;
			}
			index += stride_diff;
		}
	}
	component->prec = precision;
}

void GrkImage::convertPrecision(void){
	if (precision) {
		for(uint32_t compno = 0; compno < numcomps; ++compno)
		{
			uint32_t precisionno = compno;
			if(precisionno >= numPrecision)
				precisionno = numPrecision - 1U;
			uint8_t prec = precision[precisionno].prec;
			auto comp = comps + compno;
			if(prec == 0)
				prec = comp->prec;

			switch(precision[precisionno].mode)
			{
				case GRK_PREC_MODE_CLIP:
					{
						if(comp->sgnd)
							clip<int32_t>(comp, prec);
						else
							clip<uint32_t>(comp, prec);
					}
					break;
				case GRK_PREC_MODE_SCALE:
					scaleComponent(comp,prec);
					break;
				default:
					break;
			}
		}
	}

	if (decompressFormat == GRK_JPG_FMT) {
		uint8_t prec = comps[0].prec;
		if(prec < 8 && numcomps > 1)
		{ /* GRAY_ALPHA, RGB, RGB_ALPHA */
			for(uint16_t i = 0; i < numcomps; ++i)
				scaleComponent(comps+i, 8);
			prec = 8;
		}
		else if((prec > 1) && (prec < 8) && ((prec == 6) || ((prec & 1) == 1)))
		{ /* GRAY with non native precision */
			if((prec == 5) || (prec == 6))
				prec = 8;
			else
				prec++;
			for(uint16_t i = 0; i < numcomps; ++i)
				scaleComponent(comps+i, prec);
		}
	} else if (decompressFormat == GRK_PNG_FMT){
		uint16_t nr_comp = numcomps;
		if(nr_comp > 4)
		{
			GRK_WARN("PNG: number of components {} is "
						 "greater than 4. Truncating to 4",
						 nr_comp);
			nr_comp = 4;
		}
		uint8_t prec = comps[0].prec;
		if(prec > 8 && prec < 16)
		{
			prec = 16;
		}
		else if(prec < 8 && nr_comp > 1)
		{ /* GRAY_ALPHA, RGB, RGB_ALPHA */
			prec = 8;
		}
		else if((prec > 1) && (prec < 8) && ((prec == 6) || ((prec & 1) == 1)))
		{ /* GRAY with non native precision */
			if((prec == 5) || (prec == 6))
				prec = 8;
			else
				prec++;
		}
		for(uint16_t i = 0; i < nr_comp; ++i)
			scaleComponent(comps+i, prec);
	}

}

bool GrkImage::greyToRGB(void){
	if(numcomps != 1)
		return false;

	if (!forceRGB || color_space != GRK_CLRSPC_GRAY)
		return false;

	auto new_components = new grk_image_comp[3];
	memset(new_components, 0, 3 * sizeof(grk_image_comp));
	for (uint16_t i = 0; i < 3; ++i){
		auto src = comps;
		auto dest = new_components + i;
		copyComponent(src, dest);
		if (!allocData(dest)){
			delete [] new_components;
			return false;
		}
		size_t dataSize = (uint64_t)src->stride * src->h * sizeof(uint32_t);
		memcpy(dest->data, src->data, dataSize);
	}

	delete[] comps;
	comps = new_components;
	numcomps = 3;
	color_space = GRK_CLRSPC_SRGB;

	return true;
}

bool GrkImage::convertToRGB(void){
	bool oddFirstX = x0 & 1;
	bool oddFirstY = y0 & 1;
	bool convert = (decompressFormat != GRK_UNK_FMT && decompressFormat != GRK_TIF_FMT) || forceRGB;
	if(color_space == GRK_CLRSPC_UNKNOWN &&
	   numcomps == 3 &&
	   comps[0].dx == 1 && comps[0].dy == 1 &&
	   comps[1].dx == comps[2].dx &&
	   comps[1].dy == comps[2].dy &&
	   (comps[1].dx ==2 || comps[1].dy ==2) &&
	   (comps[2].dx ==2 || comps[2].dy ==2) )
		color_space = GRK_CLRSPC_SYCC;

	switch(color_space)
	{
		case GRK_CLRSPC_SYCC:
			if(numcomps != 3)
			{
				GRK_ERROR("grk_decompress: YCC: number of components {} "
							  "not equal to 3 ",
							  numcomps);
				return false;
			}
			if(convert)
			{
				if(!color_sycc_to_rgb(oddFirstX, oddFirstY))
					GRK_WARN("grk_decompress: sYCC to RGB colour conversion failed");
			}
			break;
		case GRK_CLRSPC_EYCC:
			if(numcomps != 3)
			{
				GRK_ERROR("grk_decompress: YCC: number of components {} "
							  "not equal to 3 ",
							  numcomps);
				return false;
			}
			if (convert && !color_esycc_to_rgb())
				GRK_WARN("grk_decompress: eYCC to RGB colour conversion failed");
			break;
		case GRK_CLRSPC_CMYK:
			if(numcomps != 4)
			{
				GRK_ERROR("grk_decompress: CMYK: number of components {} "
							  "not equal to 4 ",
							  numcomps);
				return false;
			}
			if(convert && !color_cmyk_to_rgb())
				GRK_WARN("grk_decompress: CMYK to RGB colour conversion failed");
			break;
		default:
			break;
	}

	return true;

}

grk_image* GrkImage::createRGB(uint16_t numcmpts, uint32_t w, uint32_t h,
												uint8_t prec)
{
	if(!numcmpts)
	{
		GRK_WARN("createRGB: number of components cannot be zero.");
		return nullptr;
	}

	auto cmptparms = new grk_image_cmptparm[numcmpts];
	if(!cmptparms)
	{
		GRK_WARN("createRGB: out of memory.");
		return nullptr;
	}
	uint32_t compno = 0U;
	for(compno = 0U; compno < numcmpts; ++compno)
	{
		memset(cmptparms + compno, 0, sizeof(grk_image_cmptparm));
		cmptparms[compno].dx = 1;
		cmptparms[compno].dy = 1;
		cmptparms[compno].w = w;
		cmptparms[compno].h = h;
		cmptparms[compno].x0 = 0U;
		cmptparms[compno].y0 = 0U;
		cmptparms[compno].prec = prec;
		cmptparms[compno].sgnd = 0U;
	}
	auto img = grk_image_new(this,numcmpts, (grk_image_cmptparm*)cmptparms, GRK_CLRSPC_SRGB, true);
	delete[] cmptparms;

	return img;
}

/*--------------------------------------------------------
 Matrix for sYCC, Amendment 1 to IEC 61966-2-1

 Y  |  0.299   0.587    0.114  |    R
 Cb | -0.1687 -0.3312   0.5    | x  G
 Cr |  0.5    -0.4187  -0.0812 |    B

 Inverse:

 R   |1        -3.68213e-05    1.40199     |    Y
 G = |1.00003  -0.344125      -0.714128    | x  Cb - 2^(prec - 1)
 B   |0.999823  1.77204       -8.04142e-06 |    Cr - 2^(prec - 1)

 -----------------------------------------------------------*/
void GrkImage::sycc_to_rgb(int32_t offset, int32_t upb, int32_t y, int32_t cb, int32_t cr,
						int32_t* out_r, int32_t* out_g, int32_t* out_b)
{
	int32_t r, g, b;

	cb -= offset;
	cr -= offset;
	r = y + (int32_t)(1.402 * cr);
	if(r < 0)
		r = 0;
	else if(r > upb)
		r = upb;
	*out_r = r;

	g = y - (int32_t)(0.344 * cb + 0.714 * cr);
	if(g < 0)
		g = 0;
	else if(g > upb)
		g = upb;
	*out_g = g;

	b = y + (int32_t)(1.772 * cb);
	if(b < 0)
		b = 0;
	else if(b > upb)
		b = upb;
	*out_b = b;
}

bool GrkImage::sycc444_to_rgb(void)
{
	int32_t *d0, *d1, *d2, *r, *g, *b;
	auto dst =
		createRGB(3, comps[0].w, comps[0].h, comps[0].prec);
	if(!dst)
		return false;

	int32_t upb = (int32_t)comps[0].prec;
	int32_t offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	uint32_t w = comps[0].w;
	uint32_t src_stride_diff = comps[0].stride - w;
	uint32_t dst_stride_diff = dst->comps[0].stride - dst->comps[0].w;
	uint32_t h = comps[0].h;

	auto y = comps[0].data;
	auto cb = comps[1].data;
	auto cr = comps[2].data;

	d0 = r = dst->comps[0].data;
	d1 = g = dst->comps[1].data;
	d2 = b = dst->comps[2].data;

	dst->comps[0].data = nullptr;
	dst->comps[1].data = nullptr;
	dst->comps[2].data = nullptr;

	for(uint32_t j = 0; j < h; ++j)
	{
		for(uint32_t i = 0; i < w; ++i)
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
		y += src_stride_diff;
		cb += src_stride_diff;
		cr += src_stride_diff;
		r += dst_stride_diff;
		g += dst_stride_diff;
		b += dst_stride_diff;
	}

	grk_image_all_components_data_free(this);
	comps[0].data = d0;
	comps[1].data = d1;
	comps[2].data = d2;
	color_space = GRK_CLRSPC_SRGB;

	for(uint32_t i = 0; i < numcomps; ++i)
		comps[i].stride = dst->comps[i].stride;
	grk_object_unref(&dst->obj);
	dst = nullptr;

	return true;
} /* sycc444_to_rgb() */

bool GrkImage::sycc422_to_rgb(bool oddFirstX)
{
	auto dst =
		createRGB(3, comps[0].w, comps[0].h, comps[0].prec);
	if(!dst)
		return false;

	int32_t upb = comps[0].prec;
	int32_t offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	uint32_t w = comps[0].w;
	uint32_t dst_stride_diff = dst->comps[0].stride - dst->comps[0].w;
	uint32_t src_stride_diff = comps[0].stride - w;
	uint32_t src_stride_diff_chroma = comps[1].stride - comps[1].w;
	uint32_t h = comps[0].h;

	int32_t *d0, *d1, *d2, *r, *g, *b;

	auto y = comps[0].data;
	if(!y)
	{
		GRK_WARN("sycc422_to_rgb: null luma channel");
		return false;
	}
	auto cb = comps[1].data;
	auto cr = comps[2].data;
	if(!cb || !cr)
	{
		GRK_WARN("sycc422_to_rgb: null chroma channel");
		return false;
	}

	d0 = r = dst->comps[0].data;
	d1 = g = dst->comps[1].data;
	d2 = b = dst->comps[2].data;

	dst->comps[0].data = nullptr;
	dst->comps[1].data = nullptr;
	dst->comps[2].data = nullptr;

	/* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
	uint32_t loopmaxw = w;
	if(oddFirstX)
		loopmaxw--;

	for(uint32_t i = 0U; i < h; ++i)
	{
		if(oddFirstX)
			sycc_to_rgb(offset, upb, *y++, 0, 0, r++, g++, b++);

		uint32_t j;
		for(j = 0U; j < (loopmaxw & ~(size_t)1U); j += 2U)
		{
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
		}
		if(j < loopmaxw)
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);

		y += src_stride_diff;
		cb += src_stride_diff_chroma;
		cr += src_stride_diff_chroma;
		r += dst_stride_diff;
		g += dst_stride_diff;
		b += dst_stride_diff;
	}
	grk_image_all_components_data_free(this);

	comps[0].data = d0;
	comps[1].data = d1;
	comps[2].data = d2;

	comps[1].w = comps[2].w = comps[0].w;
	comps[1].h = comps[2].h = comps[0].h;
	comps[1].dx = comps[2].dx = comps[0].dx;
	comps[1].dy = comps[2].dy = comps[0].dy;
	color_space = GRK_CLRSPC_SRGB;

	for(uint32_t i = 0; i < numcomps; ++i)
		comps[i].stride = dst->comps[i].stride;
	grk_object_unref(&dst->obj);
	dst = nullptr;

	return true;

} /* sycc422_to_rgb() */

bool GrkImage::sycc420_to_rgb(bool oddFirstX, bool oddFirstY)
{
	auto dst = createRGB(3, comps[0].w, comps[0].h,
											 comps[0].prec);
	if(!dst)
		return false;

	int32_t upb = comps[0].prec;
	int32_t offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	uint32_t w = comps[0].w;
	uint32_t h = comps[0].h;

	int32_t* src[3];
	int32_t* dest[3];
	int32_t* dest_ptr[3];
	uint32_t stride_src[3];
	uint32_t stride_src_diff[3];

	uint32_t stride_dest = dst->comps[0].stride;
	uint32_t stride_dest_diff = dst->comps[0].stride - dst->comps[0].w;

	for(uint32_t i = 0; i < 3; ++i)
	{
		auto srcComp = comps + i;
		src[i] = srcComp->data;
		stride_src[i] = srcComp->stride;
		stride_src_diff[i] = srcComp->stride - srcComp->w;

		dest[i] = dest_ptr[i] = dst->comps[i].data;
		dst->comps[i].data = nullptr;
	}

	uint32_t loopmaxw = w;
	uint32_t loopmaxh = h;

	/* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
	if(oddFirstX)
		loopmaxw--;
	/* if img->y0 is odd, then first line shall use Cb/Cr = 0 */
	if(oddFirstY)
		loopmaxh--;

	if(oddFirstX)
	{
		for(size_t j = 0U; j < w; ++j)
			sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
		src[0] += stride_src_diff[0];
		for(uint32_t i = 0; i < 3; ++i)
			dest_ptr[i] += stride_dest_diff;
	}
	size_t i;
	for(i = 0U; i < (loopmaxh & ~(size_t)1U); i += 2U)
	{
		auto ny = src[0] + stride_src[0];
		auto nr = dest_ptr[0] + stride_dest;
		auto ng = dest_ptr[1] + stride_dest;
		auto nb = dest_ptr[2] + stride_dest;

		if(oddFirstY)
		{
			sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *ny++, *src[1], *src[2], nr++, ng++, nb++);
		}
		uint32_t j;
		for(j = 0U; j < (loopmaxw & ~(size_t)1U); j += 2U)
		{
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
						dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
						dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *ny++, *src[1], *src[2], nr++, ng++, nb++);
			sycc_to_rgb(offset, upb, *ny++, *src[1]++, *src[2]++, nr++, ng++, nb++);
		}
		if(j < loopmaxw)
		{
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
						dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *ny++, *src[1]++, *src[2]++, nr++, ng++, nb++);
		}
		src[0] += stride_src_diff[0] + stride_src[0];
		src[1] += stride_src_diff[1];
		src[2] += stride_src_diff[2];
		for(uint32_t k = 0; k < 3; ++k)
			dest_ptr[k] += stride_dest_diff + stride_dest;
	}
	// last row has no sub-sampling
	if(i < loopmaxh)
	{
		uint32_t j;
		for(j = 0U; j < (w & ~(size_t)1U); j += 2U)
		{
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
						dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *src[0]++, *src[1]++, *src[2]++, dest_ptr[0]++, dest_ptr[1]++,
						dest_ptr[2]++);
		}
		if(j < w)
			sycc_to_rgb(offset, upb, *src[0], *src[1], *src[2], dest_ptr[0], dest_ptr[1],
						dest_ptr[2]);
	}

	grk_image_all_components_data_free(this);
	for(uint32_t k = 0; k < 3; ++k)
	{
		comps[k].data = dest[k];
		comps[k].stride = dst->comps[k].stride;
	}

	comps[1].w = comps[2].w = comps[0].w;
	comps[1].h = comps[2].h = comps[0].h;
	comps[1].dx = comps[2].dx = comps[0].dx;
	comps[1].dy = comps[2].dy = comps[0].dy;
	color_space = GRK_CLRSPC_SRGB;

	grk_object_unref(&dst->obj);
	dst = nullptr;

	return true;

} /* sycc420_to_rgb() */

bool GrkImage::color_sycc_to_rgb(bool oddFirstX, bool oddFirstY)
{
	if(numcomps < 3)
	{
		GRK_WARN("color_sycc_to_rgb: number of components {} is less than 3."
					 " Unable to convert",
					 numcomps);
		return false;
	}
	bool rc;

	if((comps[0].dx == 1) && (comps[1].dx == 2) && (comps[2].dx == 2) &&
	   (comps[0].dy == 1) && (comps[1].dy == 2) && (comps[2].dy == 2))
	{ /* horizontal and vertical sub-sample */
		rc = sycc420_to_rgb(oddFirstX, oddFirstY);
	}
	else if((comps[0].dx == 1) && (comps[1].dx == 2) && (comps[2].dx == 2) &&
			(comps[0].dy == 1) && (comps[1].dy == 1) && (comps[2].dy == 1))
	{ /* horizontal sub-sample only */
		rc = sycc422_to_rgb( oddFirstX);
	}
	else if((comps[0].dx == 1) && (comps[1].dx == 1) && (comps[2].dx == 1) &&
			(comps[0].dy == 1) && (comps[1].dy == 1) && (comps[2].dy == 1))
	{ /* no sub-sample */
		rc = sycc444_to_rgb();
	}
	else
	{
		GRK_WARN("color_sycc_to_rgb:  Invalid sub-sampling: ({},{}), ({},{}), ({},{})."
					 " Unable to convert.",
					 comps[0].dx, comps[0].dy, comps[1].dx, comps[1].dy,
					 comps[2].dx, comps[2].dy);
		rc = false;
	}
	if(rc)
		color_space = GRK_CLRSPC_SRGB;

	return rc;

} /* color_sycc_to_rgb() */

bool GrkImage::color_cmyk_to_rgb(void)
{
	uint32_t w = comps[0].w;
	uint32_t h = comps[0].h;

	if((numcomps < 4) || !allComponentsSanityCheck(true))
		return false;

	float sC = 1.0F / (float)((1 << comps[0].prec) - 1);
	float sM = 1.0F / (float)((1 << comps[1].prec) - 1);
	float sY = 1.0F / (float)((1 << comps[2].prec) - 1);
	float sK = 1.0F / (float)((1 << comps[3].prec) - 1);

	uint32_t stride_diff = comps[0].stride - w;
	size_t dest_index = 0;
	for(uint32_t j = 0; j < h; ++j)
	{
		for(uint32_t i = 0; i < w; ++i)
		{
			/* CMYK values from 0 to 1 */
			float C = (float)(comps[0].data[dest_index]) * sC;
			float M = (float)(comps[1].data[dest_index]) * sM;
			float Y = (float)(comps[2].data[dest_index]) * sY;
			float K = (float)(comps[3].data[dest_index]) * sK;

			/* Invert all CMYK values */
			C = 1.0F - C;
			M = 1.0F - M;
			Y = 1.0F - Y;
			K = 1.0F - K;

			/* CMYK -> RGB : RGB results from 0 to 255 */
			comps[0].data[dest_index] = (int32_t)(255.0F * C * K); /* R */
			comps[1].data[dest_index] = (int32_t)(255.0F * M * K); /* G */
			comps[2].data[dest_index] = (int32_t)(255.0F * Y * K); /* B */
			dest_index++;
		}
		dest_index += stride_diff;
	}

	grk_image_single_component_data_free(comps + 3);
	comps[0].prec = 8;
	comps[1].prec = 8;
	comps[2].prec = 8;
	numcomps = (uint16_t)(numcomps - 1U);
	color_space = GRK_CLRSPC_SRGB;

	for(uint32_t i = 3; i < numcomps; ++i)
		memcpy(&(comps[i]), &(comps[i + 1]), sizeof(comps[i]));

	return true;

} /* color_cmyk_to_rgb() */

// assuming unsigned data !
bool GrkImage::color_esycc_to_rgb(void)
{
	int32_t flip_value = (1 << (comps[0].prec - 1));
	int32_t max_value = (1 << comps[0].prec) - 1;

	if((numcomps < 3) || !allComponentsSanityCheck(true))
		return false;

	uint32_t w = comps[0].w;
	uint32_t h = comps[0].h;

	bool sign1 = comps[1].sgnd;
	bool sign2 = comps[2].sgnd;

	uint32_t stride_diff = comps[0].stride - w;
	size_t dest_index = 0;
	for(uint32_t j = 0; j < h; ++j)
	{
		for(uint32_t i = 0; i < w; ++i)
		{
			int32_t y = comps[0].data[dest_index];
			int32_t cb = comps[1].data[dest_index];
			int32_t cr = comps[2].data[dest_index];

			if(!sign1)
				cb -= flip_value;
			if(!sign2)
				cr -= flip_value;

			int32_t val = (int32_t)(y - 0.0000368 * cb + 1.40199 * cr + 0.5);

			if(val > max_value)
				val = max_value;
			else if(val < 0)
				val = 0;
			comps[0].data[dest_index] = val;

			val = (int32_t)(1.0003 * y - 0.344125 * cb - 0.7141128 * cr + 0.5);

			if(val > max_value)
				val = max_value;
			else if(val < 0)
				val = 0;
			comps[1].data[dest_index] = val;

			val = (int32_t)(0.999823 * y + 1.77204 * cb - 0.000008 * cr + 0.5);

			if(val > max_value)
				val = max_value;
			else if(val < 0)
				val = 0;
			comps[2].data[dest_index] = val;
			dest_index++;
		}
		dest_index += stride_diff;
	}
	color_space = GRK_CLRSPC_SRGB;

	return true;

} /* color_esycc_to_rgb() */

void GrkImage::applyColourManagement(void){
	if(meta && meta->color.icc_profile_buf)
	{
		bool isTiff = decompressFormat == GRK_TIF_FMT;
		bool canStoreCIE = isTiff && color_space == GRK_CLRSPC_DEFAULT_CIE;
		bool isCIE =
			color_space == GRK_CLRSPC_DEFAULT_CIE || color_space == GRK_CLRSPC_CUSTOM_CIE;
		if(isCIE)
		{
			if(!canStoreCIE || forceRGB)
			{
				if(!forceRGB)
					GRK_WARN(
						" Input file is in CIE colour space,\n"
						"but the codec is unable to store this information in the "
						"output file .\n"
						"The output image will therefore be converted to sRGB before saving.");
				if(!cieLabToRGB())
					GRK_WARN("Unable to convert L*a*b image to sRGB");
			}
		}
		else
		{
			// A TIFF,PNG, BMP or JPEG image can store the ICC profile,
			// so no need to apply it in this case,
			// (unless we are forcing to RGB).
			// Otherwise, we apply the profile
			bool canStoreICC = (decompressFormat == GRK_TIF_FMT ||
									decompressFormat == GRK_PNG_FMT ||
									decompressFormat == GRK_JPG_FMT ||
									decompressFormat == GRK_BMP_FMT);
			if(forceRGB || !canStoreICC)
			{
				if(!forceRGB)
				{
					GRK_WARN(" Input file contains a color profile,\n"
								 "but the codec is unable to store this profile"
								 " in the output file .\n"
								 "The profile will therefore be applied to the output"
								 " image before saving.");
				}
				applyICC();
			}
		}
	}
}


/*#define DEBUG_PROFILE*/
void GrkImage::applyICC(void)
{
	cmsColorSpaceSignature in_space;
	cmsColorSpaceSignature out_space;
	cmsUInt32Number intent = 0;
	cmsHTRANSFORM transform = nullptr;
	cmsHPROFILE in_prof = nullptr;
	cmsHPROFILE out_prof = nullptr;
	cmsUInt32Number in_type, out_type;
	size_t nr_samples, max;
	uint32_t prec, w, stride_diff, h;
	GRK_COLOR_SPACE oldspace;
	grk_image* new_image = nullptr;
	if(numcomps == 0 || !allComponentsSanityCheck(true))
		return;
	if(!meta)
		return;
	in_prof = cmsOpenProfileFromMem(meta->color.icc_profile_buf,
									meta->color.icc_profile_len);
#ifdef DEBUG_PROFILE
	FILE* icm = fopen("debug.icm", "wb");
	fwrite(color.icc_profile_buf, 1, color.icc_profile_len, icm);
	fclose(icm);
#endif

	if(in_prof == nullptr)
		return;

	in_space = cmsGetPCS(in_prof);
	out_space = cmsGetColorSpace(in_prof);
	intent = cmsGetHeaderRenderingIntent(in_prof);

	w = comps[0].w;
	stride_diff = comps[0].stride - w;
	h = comps[0].h;

	if(!w || !h)
		goto cleanup;

	prec = comps[0].prec;
	oldspace = color_space;

	if(out_space == cmsSigRgbData)
	{ /* enumCS 16 */
		uint32_t i, nr_comp = numcomps;

		if(nr_comp > 4)
		{
			nr_comp = 4;
		}
		for(i = 1; i < nr_comp; ++i)
		{
			if(comps[0].dx != comps[i].dx)
				break;
			if(comps[0].dy != comps[i].dy)
				break;
			if(comps[0].prec != comps[i].prec)
				break;
			if(comps[0].sgnd != comps[i].sgnd)
				break;
		}
		if(i != nr_comp)
			goto cleanup;

		if(prec <= 8)
		{
			in_type = TYPE_RGB_8;
			out_type = TYPE_RGB_8;
		}
		else
		{
			in_type = TYPE_RGB_16;
			out_type = TYPE_RGB_16;
		}
		out_prof = cmsCreate_sRGBProfile();
		color_space = GRK_CLRSPC_SRGB;
	}
	else if(out_space == cmsSigGrayData)
	{ /* enumCS 17 */
		in_type = TYPE_GRAY_8;
		out_type = TYPE_RGB_8;
		out_prof = cmsCreate_sRGBProfile();
		if(forceRGB)
			color_space = GRK_CLRSPC_SRGB;
		else
			color_space = GRK_CLRSPC_GRAY;
	}
	else if(out_space == cmsSigYCbCrData)
	{ /* enumCS 18 */
		in_type = TYPE_YCbCr_16;
		out_type = TYPE_RGB_16;
		out_prof = cmsCreate_sRGBProfile();
		color_space = GRK_CLRSPC_SRGB;
	}
	else
	{
#ifdef DEBUG_PROFILE
		spdlog::error("{}:{}: color_apply_icc_profile\n\tICC Profile has unknown "
					  "output colorspace(%#x)({}{}{}{})\n\tICC Profile ignored.",
					  __FILE__, __LINE__, out_space, (out_space >> 24) & 0xff,
					  (out_space >> 16) & 0xff, (out_space >> 8) & 0xff, out_space & 0xff);
#endif
		return;
	}

#ifdef DEBUG_PROFILE
	spdlog::error("{}:{}:color_apply_icc_profile\n\tchannels({}) prec({}) w({}) h({})"
				  "\n\tprofile: in({}) out({})",
				  __FILE__, __LINE__, numcomps, prec, max_w, max_h, (void*)in_prof,
				  (void*)out_prof);

	spdlog::error("\trender_intent ({})\n\t"
				  "color_space: in({})({}{}{}{})   out:({})({}{}{}{})\n\t"
				  "       type: in({})              out:({})",
				  intent, in_space, (in_space >> 24) & 0xff, (in_space >> 16) & 0xff,
				  (in_space >> 8) & 0xff, in_space & 0xff,

				  out_space, (out_space >> 24) & 0xff, (out_space >> 16) & 0xff,
				  (out_space >> 8) & 0xff, out_space & 0xff,

				  in_type, out_type);
#else
	(void)prec;
	(void)in_space;
#endif /* DEBUG_PROFILE */

	transform = cmsCreateTransform(in_prof, in_type, out_prof, out_type, intent, 0);

	cmsCloseProfile(in_prof);
	in_prof = nullptr;
	cmsCloseProfile(out_prof);
	out_prof = nullptr;

	if(transform == nullptr)
	{
#ifdef DEBUG_PROFILE
		spdlog::error("{}:{}:color_apply_icc_profile\n\tcmsCreateTransform failed. "
					  "ICC Profile ignored.",
					  __FILE__, __LINE__);
#endif
		color_space = oldspace;
		return;
	}
	if(numcomps > 2)
	{ /* RGB, RGBA */
		if(prec <= 8)
		{
			uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf = nullptr;
			max = (size_t)w * h;
			nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint8_t);
			in = inbuf = new uint8_t[nr_samples];
			if(!in)
			{
				goto cleanup;
			}
			out = outbuf = new uint8_t[nr_samples];
			if(!out)
			{
				delete[] inbuf;
				goto cleanup;
			}

			auto r = comps[0].data;
			auto g = comps[1].data;
			auto b = comps[2].data;

			size_t src_index = 0;
			size_t dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
					in[dest_index++] = (uint8_t)r[src_index];
					in[dest_index++] = (uint8_t)g[src_index];
					in[dest_index++] = (uint8_t)b[src_index];
					src_index++;
				}
				src_index += stride_diff;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

			src_index = 0;
			dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
					r[dest_index] = (int32_t)out[src_index++];
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
					dest_index++;
				}
				dest_index += stride_diff;
			}
			delete[] inbuf;
			delete[] outbuf;
		}
		else
		{
			uint16_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf = nullptr;
			max = (size_t)w * h;
			nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint16_t);
			in = inbuf = new uint16_t[nr_samples];
			if(!in)
				goto cleanup;
			out = outbuf = new uint16_t[nr_samples];
			if(!out)
			{
				delete[] inbuf;
				goto cleanup;
			}
			auto r = comps[0].data;
			auto g = comps[1].data;
			auto b = comps[2].data;

			size_t src_index = 0;
			size_t dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
					in[dest_index++] = (uint16_t)r[src_index];
					in[dest_index++] = (uint16_t)g[src_index];
					in[dest_index++] = (uint16_t)b[src_index];
					src_index++;
				}
				src_index += stride_diff;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

			src_index = 0;
			dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
					r[dest_index] = (int32_t)out[src_index++];
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
					dest_index++;
				}
				dest_index += stride_diff;
			}
			delete[] inbuf;
			delete[] outbuf;
		}
	}
	else
	{ /* GRAY, GRAYA */
		uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf = nullptr;

		max = (size_t)w * h;
		nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint8_t);
		auto newComps = new grk_image_comp[numcomps + 2U];
		for(uint32_t i = 0; i < numcomps + 2U; ++i)
		{
			if(i < numcomps)
				newComps[i] = comps[i];
			else
				memset(newComps + 1, 0, sizeof(grk_image_comp));
		}
		delete[] comps;
		comps = newComps;

		in = inbuf = new uint8_t[nr_samples];
		if(!in)
			goto cleanup;
		out = outbuf = new uint8_t[nr_samples];
		if(!out)
		{
			delete[] inbuf;
			goto cleanup;
		}

		new_image = createRGB(2, comps[0].w, comps[0].h,
												  comps[0].prec);
		if(!new_image)
		{
			delete[] inbuf;
			delete[] outbuf;
			goto cleanup;
		}

		if(numcomps == 2)
			comps[3] = comps[1];

		comps[1] = comps[0];
		comps[2] = comps[0];

		comps[1].data = new_image->comps[0].data;
		comps[2].data = new_image->comps[1].data;

		new_image->comps[0].data = nullptr;
		new_image->comps[1].data = nullptr;

		grk_object_unref(&new_image->obj);
		new_image = nullptr;

		if(forceRGB)
			numcomps = (uint16_t)(2 + numcomps);

		auto r = comps[0].data;

		size_t src_index = 0;
		size_t dest_index = 0;
		for(uint32_t j = 0; j < h; ++j)
		{
			for(uint32_t i = 0; i < w; ++i)
			{
				in[dest_index++] = (uint8_t)r[src_index];
				src_index++;
			}
			src_index += stride_diff;
		}

		cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

		auto g = comps[1].data;
		auto b = comps[2].data;

		src_index = 0;
		dest_index = 0;
		for(uint32_t j = 0; j < h; ++j)
		{
			for(uint32_t i = 0; i < w; ++i)
			{
				r[dest_index] = (int32_t)out[src_index++];
				if(forceRGB)
				{
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
				}
				else
				{
					src_index += 2;
				}
				dest_index++;
			}
			dest_index += stride_diff;
		}

		delete[] inbuf;
		delete[] outbuf;
	} /* if(image->numcomps */
cleanup:
	if(in_prof)
		cmsCloseProfile(in_prof);
	if(out_prof)
		cmsCloseProfile(out_prof);
	if(transform)
		cmsDeleteTransform(transform);
} /* applyICC() */


// transform LAB colour space to sRGB @ 16 bit precision
bool GrkImage::cieLabToRGB(void)
{
	// sanity checks
	if(numcomps == 0 || !allComponentsSanityCheck(true))
		return false;
	if(!meta)
		return false;
	size_t i;
	for(i = 1U; i < numcomps; ++i)
	{
		auto comp0 = comps;
		auto compi = comps + i;

		if(comp0->prec != compi->prec)
			break;
		if(comp0->sgnd != compi->sgnd)
			break;
		if(comp0->stride != compi->stride)
			break;
	}
	if(i != numcomps)
	{
		GRK_WARN("All components must have same precision, sign and stride");
		return false;
	}

	auto row = (uint32_t*)meta->color.icc_profile_buf;
	auto enumcs = (GRK_ENUM_COLOUR_SPACE)row[0];
	if(enumcs != GRK_ENUM_CLRSPC_CIE)
	{ /* CIELab */
		GRK_WARN("{}:{}:\n\tenumCS {} not handled. Ignoring.", __FILE__, __LINE__, enumcs);
		return false;
	}

	bool defaultType = true;
	color_space = GRK_CLRSPC_SRGB;
	uint32_t illuminant = GRK_CIE_D50;
	cmsCIExyY WhitePoint;
	defaultType = row[1] == GRK_DEFAULT_CIELAB_SPACE;
	int32_t *L, *a, *b, *red, *green, *blue;
	int32_t *src[3], *dst[3];
	// range, offset and precision for L,a and b coordinates
	double r_L, o_L, r_a, o_a, r_b, o_b, prec_L, prec_a, prec_b;
	double minL, maxL, mina, maxa, minb, maxb;
	cmsUInt16Number RGB[3];
	auto dest_img = createRGB(3, comps[0].w, comps[0].h,
												  comps[0].prec);
	if(!dest_img)
		return false;

	prec_L = (double)comps[0].prec;
	prec_a = (double)comps[1].prec;
	prec_b = (double)comps[2].prec;

	if(defaultType)
	{ // default Lab space
		r_L = 100;
		r_a = 170;
		r_b = 200;
		o_L = 0;
		o_a = pow(2, prec_a - 1); // 2 ^ (prec_b - 1)
		o_b = 3 * pow(2, prec_b - 3); // 0.75 * 2 ^ (prec_b - 1)
	}
	else
	{
		r_L = row[2];
		r_a = row[4];
		r_b = row[6];
		o_L = row[3];
		o_a = row[5];
		o_b = row[7];
		illuminant = row[8];
	}
	switch(illuminant)
	{
		case GRK_CIE_D50:
			break;
		case GRK_CIE_D65:
			cmsWhitePointFromTemp(&WhitePoint, 6504);
			break;
		case GRK_CIE_D75:
			cmsWhitePointFromTemp(&WhitePoint, 7500);
			break;
		case GRK_CIE_SA:
			cmsWhitePointFromTemp(&WhitePoint, 2856);
			break;
		case GRK_CIE_SC:
			cmsWhitePointFromTemp(&WhitePoint, 6774);
			break;
		case GRK_CIE_F2:
			cmsWhitePointFromTemp(&WhitePoint, 4100);
			break;
		case GRK_CIE_F7:
			cmsWhitePointFromTemp(&WhitePoint, 6500);
			break;
		case GRK_CIE_F11:
			cmsWhitePointFromTemp(&WhitePoint, 4000);
			break;
		default:
			GRK_WARN("Unrecognized illuminant {} in CIELab colour space. "
						 "Setting to default Daylight50",
						 illuminant);
			illuminant = GRK_CIE_D50;
			break;
	}

	// Lab input profile
	auto in = cmsCreateLab4Profile(illuminant == GRK_CIE_D50 ? nullptr : &WhitePoint);
	// sRGB output profile
	auto out = cmsCreate_sRGBProfile();
	auto transform =
		cmsCreateTransform(in, TYPE_Lab_DBL, out, TYPE_RGB_16, INTENT_PERCEPTUAL, 0);

	cmsCloseProfile(in);
	cmsCloseProfile(out);
	if(transform == nullptr)
	{
		grk_object_unref(&dest_img->obj);
		return false;
	}

	L = src[0] = comps[0].data;
	a = src[1] = comps[1].data;
	b = src[2] = comps[2].data;

	if(!L || !a || !b)
	{
		GRK_WARN("color_cielab_to_rgb: null L*a*b component");
		return false;
	}

	red = dst[0] = dest_img->comps[0].data;
	green = dst[1] = dest_img->comps[1].data;
	blue = dst[2] = dest_img->comps[2].data;

	dest_img->comps[0].data = nullptr;
	dest_img->comps[1].data = nullptr;
	dest_img->comps[2].data = nullptr;

	grk_object_unref(&dest_img->obj);
	dest_img = nullptr;

	minL = -(r_L * o_L) / (pow(2, prec_L) - 1);
	maxL = minL + r_L;

	mina = -(r_a * o_a) / (pow(2, prec_a) - 1);
	maxa = mina + r_a;

	minb = -(r_b * o_b) / (pow(2, prec_b) - 1);
	maxb = minb + r_b;

	uint32_t stride_diff = comps[0].stride - comps[0].w;
	size_t dest_index = 0;
	for(uint32_t j = 0; j < comps[0].h; ++j)
	{
		for(uint32_t k = 0; k < comps[0].w; ++k)
		{
			cmsCIELab Lab;
			Lab.L = minL + (double)(*L) * (maxL - minL) / (pow(2, prec_L) - 1);
			++L;
			Lab.a = mina + (double)(*a) * (maxa - mina) / (pow(2, prec_a) - 1);
			++a;
			Lab.b = minb + (double)(*b) * (maxb - minb) / (pow(2, prec_b) - 1);
			++b;

			cmsDoTransform(transform, &Lab, RGB, 1);

			red[dest_index] = RGB[0];
			green[dest_index] = RGB[1];
			blue[dest_index] = RGB[2];
			dest_index++;
		}
		dest_index += stride_diff;
		L += stride_diff;
		a += stride_diff;
		b += stride_diff;
	}
	cmsDeleteTransform(transform);
	for(i = 0; i < 3; ++i)
	{
		auto comp = comps + i;
		grk_image_single_component_data_free(comp);
		comp->data = dst[i];
		comp->prec = 16;
	}
	color_space = GRK_CLRSPC_SRGB;

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
