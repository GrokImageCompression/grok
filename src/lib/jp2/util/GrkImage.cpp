#include <grk_includes.h>

namespace grk
{
GrkImage::GrkImage() : ownsData(true)
{
	memset((grk_image*)(this), 0, sizeof(grk_image));
	obj.wrapper = new GrkObjectWrapperImpl(this);
}
GrkImage::~GrkImage()
{
	if(ownsData && comps)
	{
		grk_image_all_components_data_free(this);
		delete[] comps;
	}
	if(meta)
		grk_object_unref(&meta->obj);
}

GrkImage* GrkImage::create(uint16_t numcmpts, grk_image_cmptparm* cmptparms, GRK_COLOR_SPACE clrspc,
						   bool doAllocation)
{
	auto image = new GrkImage();
	image->color_space = clrspc;
	image->numcomps = numcmpts;
	image->decodeFormat = GRK_UNK_FMT;
	image->forceRGB = false;
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
	dest->decodeFormat = decodeFormat;
	dest->forceRGB = forceRGB;
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
		auto dest_comp = comps + i;

		if(dest_comp->w == 0 || dest_comp->h == 0)
		{
			GRK_ERROR("Output component %d has invalid dimensions %u x %u", i, dest_comp->w,
					  dest_comp->h);
			return false;
		}
		if(!dest_comp->data)
		{
			if(!GrkImage::allocData(dest_comp))
			{
				GRK_ERROR("Failed to allocate pixel data for component %d, with dimensions %u x %u",
						  i, dest_comp->w, dest_comp->h);
				return false;
			}
			memset(dest_comp->data, 0,
				   (uint64_t)dest_comp->stride * dest_comp->h * sizeof(int32_t));
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
		auto src_comp = comps + compno;
		auto dest_comp = dest->comps + compno;

		grk_image_single_component_data_free(dest_comp);
		dest_comp->data = src_comp->data;
		if(src_comp->stride)
		{
			dest_comp->stride = src_comp->stride;
			assert(dest_comp->stride >= dest_comp->w);
		}
		src_comp->data = nullptr;
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
		auto src_comp = src_tile->comps + compno;
		auto src_buffer = src_comp->getBuffer();
		auto src_bounds = src_buffer->bounds();

		auto dest_comp = destImage->comps + compno;
		dest_comp->x0 = src_bounds.x0;
		dest_comp->y0 = src_bounds.y0;
		dest_comp->w = src_bounds.width();
		dest_comp->h = src_bounds.height();
	}

	destImage->transferDataFrom(src_tile);

	return destImage;
}

void GrkImage::transferDataFrom(const Tile* tile_src_data)
{
	for(uint16_t compno = 0; compno < numcomps; compno++)
	{
		auto src_comp = tile_src_data->comps + compno;
		auto dest_comp = comps + compno;

		// transfer memory from tile component to output image
		src_comp->getBuffer()->transfer(&dest_comp->data, &dest_comp->stride);
		if(dest_comp->data)
			assert(dest_comp->stride >= dest_comp->w);
	}
}

bool GrkImage::generateCompositeBounds(uint16_t compno, grkRectU32* src, uint32_t src_stride,
									   grkRectU32* dest, grkRectU32* dest_win,
									   uint32_t* src_line_off)
{
	auto dest_comp = comps + compno;
	*dest = grkRectU32(dest_comp->x0, dest_comp->y0, dest_comp->x0 + dest_comp->w,
					   dest_comp->y0 + dest_comp->h);
	*src_line_off = src_stride - src->width();
	if(dest->x0 < src->x0)
	{
		dest_win->x0 = (uint32_t)(src->x0 - dest->x0);
		if(dest->x1 >= src->x1)
		{
			dest_win->x1 = dest_win->x0 + src->width();
		}
		else
		{
			dest_win->x1 = dest_win->x0 + (uint32_t)(dest->x1 - src->x0);
			*src_line_off = src_stride - dest_win->width();
		}
	}
	else
	{
		dest_win->x0 = 0U;
		if(dest->x1 >= src->x1)
		{
			dest_win->x1 = dest_win->x0 + src->width();
		}
		else
		{
			dest_win->x1 = dest_win->x0 + dest_comp->w;
			*src_line_off = (uint32_t)(src->x1 - dest->x1);
		}
	}
	if(dest->y0 < src->y0)
	{
		dest_win->y0 = (uint32_t)(src->y0 - dest->y0);
		dest_win->y1 =
			dest_win->y0 + ((dest->y1 >= src->y1) ? src->height() : (uint32_t)(dest->y1 - src->y0));
	}
	else
	{
		dest_win->y1 = dest_win->y0 + src->height();
	}
	if(dest_win->width() > dest_comp->w || dest_win->height() > dest_comp->h)
		return false;

	return true;
}

bool GrkImage::generateCompositeBounds(const grk_image_comp* src_comp, uint16_t compno,
									   grkRectU32* src, grkRectU32* dest, grkRectU32* dest_win,
									   uint32_t* src_line_off)
{
	*src = grkRectU32(src_comp->x0, src_comp->y0, src_comp->x0 + src_comp->w,
					  src_comp->y0 + src_comp->h);

	return generateCompositeBounds(compno, src, src_comp->stride, dest, dest_win, src_line_off);
}

bool GrkImage::generateCompositeBounds(const TileComponent* src_comp, uint16_t compno,
									   grkRectU32* src, grkRectU32* dest, grkRectU32* dest_win,
									   uint32_t* src_line_off)
{
	*src = src_comp->getBuffer()->bounds();
	assert(src->width() <= src_comp->width() && src->height() <= src_comp->height());

	return generateCompositeBounds(compno, src,
								   (src_comp->getBuffer()->getResWindowBufferHighestREL())->stride,
								   dest, dest_win, src_line_off);
}

/**
 * Copy tile data to composite image
 *
 * tile stores only the decompressed resolutions, in the actual precision
 * of the decompressed image. This method copies tile buffer
 * into composite image.
 *
 *
 * @param src_tile 	source tile
 *
 * @return:			true if successful
 */
bool GrkImage::compositeFrom(const Tile* src_tile)
{
	for(uint16_t compno = 0; compno < src_tile->numcomps; compno++)
	{
		auto src_comp = src_tile->comps + compno;
		auto dest_comp = comps + compno;

		grkRectU32 src, dest, dest_win;
		uint32_t src_line_off;

		if(!generateCompositeBounds(src_comp, compno, &src, &dest, &dest_win, &src_line_off))
			return false;

		size_t src_ind = 0;
		auto dest_ind = (size_t)dest_win.x0 + (size_t)dest_win.y0 * dest_comp->stride;
		size_t dest_line_off = (size_t)dest_comp->stride - (size_t)dest_win.width();
		auto src_ptr = src_comp->getBuffer()->getResWindowBufferHighestREL()->getBuffer();
		for(uint32_t j = 0; j < dest_win.height(); ++j)
		{
			memcpy(dest_comp->data + dest_ind, src_ptr + src_ind,
				   dest_win.width() * sizeof(int32_t));
			dest_ind += dest_win.width() + dest_line_off;
			src_ind += dest_win.width() + src_line_off;
		}
	}

	return true;
}

/**
 * Copy image data to composite image
 *
 * @param src_image 	source image
 *
 * @return:			true if successful
 */
bool GrkImage::compositeFrom(const GrkImage* src_image)
{
	for(uint16_t compno = 0; compno < src_image->numcomps; compno++)
	{
		auto src_comp = src_image->comps + compno;
		auto dest_comp = comps + compno;

		grkRectU32 src, dest, dest_win;
		uint32_t src_line_off;

		if(!generateCompositeBounds(src_comp, compno, &src, &dest, &dest_win, &src_line_off))
		{
			GRK_WARN("GrkImage::compositeFrom: cannot generate composite bounds for component %d",
					 compno);
			continue;
		}
		if(!dest_comp->data)
		{
			GRK_WARN("GrkImage::compositeFrom: null data for destination component %d", compno);
			continue;
		}

		if(!src_comp->data)
		{
			GRK_WARN("GrkImage::compositeFrom: null data for source component %d", compno);
			continue;
		}

		size_t src_ind = 0;
		auto dest_ind = (size_t)dest_win.x0 + (size_t)dest_win.y0 * dest_comp->stride;
		size_t dest_line_off = (size_t)dest_comp->stride - (size_t)dest_win.width();
		auto src_ptr = src_comp->data;
		for(uint32_t j = 0; j < dest_win.height(); ++j)
		{
			memcpy(dest_comp->data + dest_ind, src_ptr + src_ind,
				   dest_win.width() * sizeof(int32_t));
			dest_ind += dest_win.width() + dest_line_off;
			src_ind += dest_win.width() + src_line_off;
		}
	}

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

bool GrkImage::colorConvert(void){
	bool oddFirstX = x0 & 1;
	bool oddFirstY = y0 & 1;
	bool isTiff = decodeFormat == GRK_TIF_FMT;
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
			if(!isTiff || forceRGB)
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
			if ((!isTiff || forceRGB) && !color_esycc_to_rgb())
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
			if((!isTiff || forceRGB ) && !color_cmyk_to_rgb())
				GRK_WARN("grk_decompress: CMYK to RGB colour conversion failed");
			break;
		default:
			break;
	}

	return true;

}

grk_image* GrkImage::create_rgb_no_subsample_image(uint16_t numcmpts, uint32_t w, uint32_t h,
												uint8_t prec)
{
	if(!numcmpts)
	{
		GRK_WARN("create_rgb_no_subsample_image: number of components cannot be zero.");
		return nullptr;
	}

	auto cmptparms = new grk_image_cmptparm[numcmpts];
	if(!cmptparms)
	{
		GRK_WARN("create_rgb_no_subsample_image: out of memory.");
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
	auto img = grk_image_new(numcmpts, (grk_image_cmptparm*)cmptparms, GRK_CLRSPC_SRGB, true);
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
		create_rgb_no_subsample_image(3, comps[0].w, comps[0].h, comps[0].prec);
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
		create_rgb_no_subsample_image(3, comps[0].w, comps[0].h, comps[0].prec);
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
	auto dst = create_rgb_no_subsample_image(3, comps[0].w, comps[0].h,
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
		auto src_comp = comps + i;
		src[i] = src_comp->data;
		stride_src[i] = src_comp->stride;
		stride_src_diff[i] = src_comp->stride - src_comp->w;

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
