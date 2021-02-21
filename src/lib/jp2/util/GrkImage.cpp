#include <grk_includes.h>

namespace grk {

GrkImage::GrkImage() : ownsData(true){
	memset((grk_image*)(this), 0, sizeof(grk_image));
	obj.wrappee = new GrkObjectImpl(this);
}
GrkImage::~GrkImage(){
	if (ownsData && comps) {
		grk_image_all_components_data_free(this);
		delete[] comps;
	}
	grk_object_unref(&meta->obj);
	delete (GrkObject*)obj.wrappee;
}

GrkImage *  GrkImage::create(uint16_t numcmpts,
		 	 	 	 	 	 grk_image_cmptparm  *cmptparms,
							 GRK_COLOR_SPACE clrspc,
							 bool doAllocation) {
	auto image = new GrkImage();

	if (image) {
		image->color_space = clrspc;
		image->numcomps = numcmpts;
		/* allocate memory for the per-component information */
		image->comps = new grk_image_comp[image->numcomps];
		memset(image->comps,0,image->numcomps * sizeof(grk_image_comp));

		/* create the individual image components */
		for (uint32_t compno = 0; compno < numcmpts; compno++) {
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
			if (doAllocation && !allocData(comp)) {
				grk::GRK_ERROR("Unable to allocate memory for image.");
				delete image;
				return nullptr;
			}
			comp->type = GRK_COMPONENT_TYPE_COLOUR;
			switch(compno){
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
	}

	return image;
}

bool GrkImage::subsampleAndReduce(uint32_t reduce){
    for (uint32_t compno = 0; compno < numcomps; ++compno) {
        auto comp = comps + compno;

        if (x0 > (uint32_t)INT_MAX ||
            y0 > (uint32_t)INT_MAX ||
            x1 > (uint32_t)INT_MAX ||
            y1 > (uint32_t)INT_MAX) {
				GRK_ERROR("Image coordinates above INT_MAX are not supported.");
				return false;
        }

        // sub-sample and reduce component origin
        comp->x0 = ceildiv<uint32_t>(x0,comp->dx);
        comp->y0 = ceildiv<uint32_t>(y0, comp->dy);

        comp->x0 = ceildivpow2<uint32_t>(comp->x0, reduce);
        comp->y0 = ceildivpow2<uint32_t>(comp->y0, reduce);

        uint32_t comp_x1 = ceildiv<uint32_t>(x1, comp->dx);
        comp_x1 = ceildivpow2<uint32_t>(comp_x1, reduce);
        if (comp_x1 <= comp->x0) {
            GRK_ERROR("component %d: x1 (%d) is <= x0 (%d).",
                          compno, comp_x1, comp->x0);
            return false;
        }
        comp->w  = (uint32_t)(comp_x1 - comp->x0);
        assert(comp->w);

        uint32_t comp_y1 = ceildiv<uint32_t>(y1, comp->dy);
        comp_y1 = ceildivpow2<uint32_t>(comp_y1, reduce);
         if (comp_y1 <= comp->y0) {
             GRK_ERROR("component %d: y1 (%d) is <= y0 (%d).",
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
void GrkImage::copyHeader(GrkImage *dest) {
	if (!dest)
		return;

	dest->x0 = x0;
	dest->y0 = y0;
	dest->x1 = x1;
	dest->y1 = y1;

	if (dest->comps) {
		grk_image_all_components_data_free(dest);
		delete[] dest->comps;
		dest->comps = nullptr;
	}
	dest->numcomps = numcomps;
	dest->comps =  new grk_image_comp[dest->numcomps];
	for (uint32_t compno = 0; compno < dest->numcomps; compno++) {
		memcpy(&(dest->comps[compno]), &(comps[compno]),sizeof( grk_image_comp) );
		dest->comps[compno].data = nullptr;
	}

	dest->color_space = color_space;
	if (has_capture_resolution){
		dest->capture_resolution[0] = capture_resolution[0];
		dest->capture_resolution[1] = capture_resolution[1];
	}
	if (has_display_resolution){
		dest->display_resolution[0] = display_resolution[0];
		dest->display_resolution[1] = display_resolution[1];
	}
	if (meta){
		GrkImageMeta *temp = (GrkImageMeta*)meta;
		grk_object_ref(&temp->obj);
		dest->meta = meta;
	}
}

void GrkImage::createMeta(){
	if (!meta)
		meta = new GrkImageMeta();
}

bool GrkImage::allocData(grk_image_comp  *comp) {
	if (!comp || comp->w == 0 || comp->h == 0)
		return false;
	comp->stride = grk_make_aligned_width(comp->w);
	assert(comp->stride);
	assert(!comp->data);

	size_t dataSize = (uint64_t) comp->stride * comp->h * sizeof(uint32_t);
	auto data = (int32_t*) grk_aligned_malloc(dataSize);
	if (!data) {
		grk::GRK_ERROR("Failed to allocate aligned memory buffer of dimensions %u x %u "
				"@ alignment %d",comp->stride, comp->h, grk::default_align);
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
bool GrkImage::allocData(void){
	for (uint32_t i = 0; i < numcomps; i++) {
		auto dest_comp = comps + i;

		if (dest_comp->w  == 0 || dest_comp->h == 0) {
			GRK_ERROR("Output component %d has invalid dimensions %u x %u",
					i, dest_comp->w, dest_comp->h);
			return false;
		}
		if (!dest_comp->data) {
			if (!GrkImage::allocData(dest_comp)){
				GRK_ERROR("Failed to allocate pixel data for component %d, with dimensions %u x %u",
						i, dest_comp->w, dest_comp->h);
				return false;
			}
			memset(dest_comp->data, 0,	(uint64_t)dest_comp->stride * dest_comp->h * sizeof(int32_t));
		}
	}

	return true;

}


/**
 Transfer data to dest for each component, and null out this data.
 Assumption:  this and dest have the same number of components
 */
void GrkImage::transferDataTo(GrkImage *dest) {
	if (!dest || !comps || !dest->comps	|| numcomps != dest->numcomps)
		return;

	for (uint32_t compno = 0; compno < numcomps; compno++) {
		auto src_comp = comps + compno;
		auto dest_comp = dest->comps + compno;

		grk_image_single_component_data_free(dest_comp);
		dest_comp->data = src_comp->data;
		if (src_comp->stride){
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
GrkImage* GrkImage::duplicate(const grk_tile* src_tile){
	auto destImage = new GrkImage();
	copyHeader(destImage);
	destImage->x0 = src_tile->x0;
	destImage->y0 = src_tile->y0;
	destImage->x1 = src_tile->x1;
	destImage->y1 = src_tile->y1;

	for (uint16_t compno = 0; compno < src_tile->numcomps; ++compno){
		auto src_comp 	= src_tile->comps + compno;
		auto src_buffer = src_comp->getBuffer();
		auto src_bounds = src_buffer->bounds();

		auto dest_comp 	= destImage->comps + compno;
		dest_comp->x0 = src_bounds.x0;
		dest_comp->y0 = src_bounds.y0;
		dest_comp->w = src_bounds.width();
		dest_comp->h = src_bounds.height();
	}

	destImage->transferDataFrom(src_tile);

	return destImage;
}

void GrkImage::transferDataFrom(const grk_tile* tile_src_data){
	for (uint16_t compno = 0; compno < numcomps; compno++) {
		auto src_comp = tile_src_data->comps + compno;
		auto dest_comp = comps + compno;

		//transfer memory from tile component to output image
		src_comp->getBuffer()->transfer(&dest_comp->data, &dest_comp->stride);
		if (dest_comp->data)
			assert(dest_comp->stride >= dest_comp->w);
	}
}

bool GrkImage::generateCompositeBounds(	uint16_t compno,
										grk_rect_u32 *src,
										uint32_t src_stride,
										grk_rect_u32 *dest,
										grk_rect_u32 *dest_win,
										uint32_t *src_line_off){
	auto dest_comp = comps + compno;
	*dest = grk_rect_u32(dest_comp->x0,
						dest_comp->y0,
						dest_comp->x0 + dest_comp->w,
						dest_comp->y0 + dest_comp->h);
	*src_line_off = src_stride - src->width();
	if (dest->x0 < src->x0) {
		dest_win->x0 = (uint32_t) (src->x0 - dest->x0);
		if (dest->x1 >= src->x1) {
			dest_win->x1 = dest_win->x0 + src->width();
		} else {
			dest_win->x1 = dest_win->x0 + (uint32_t) (dest->x1 - src->x0);
			*src_line_off = src_stride - dest_win->width();
		}
	} else {
		dest_win->x0 = 0U;
		if (dest->x1 >= src->x1) {
			dest_win->x1 = dest_win->x0 + src->width();
		} else {
			dest_win->x1 = dest_win->x0 + dest_comp->w;
			*src_line_off = (uint32_t) (src->x1 - dest->x1);
		}
	}
	if (dest->y0 < src->y0) {
		dest_win->y0 = (uint32_t) (src->y0 - dest->y0);
		dest_win->y1 = dest_win->y0 + ((dest->y1 >= src->y1) ?  src->height() : (uint32_t) (dest->y1 - src->y0));
	} else {
		dest_win->y1 = dest_win->y0 + src->height();
	}
	if (dest_win->width() > dest_comp->w || dest_win->height() > dest_comp->h)
		return false;


	return true;

}


bool GrkImage::generateCompositeBounds(const grk_image_comp *src_comp,
										uint16_t compno,
										grk_rect_u32 *src,
										grk_rect_u32 *dest,
										grk_rect_u32 *dest_win,
										uint32_t *src_line_off){
	*src = grk_rect_u32(src_comp->x0,
						src_comp->y0,
						src_comp->x0 + src_comp->w,
						src_comp->y0 + src_comp->h);

	return generateCompositeBounds(compno,
									src,
									src_comp->stride,
									dest,
									dest_win,
									src_line_off);

}

bool GrkImage::generateCompositeBounds(const TileComponent *src_comp,
										uint16_t compno,
										grk_rect_u32 *src,
										grk_rect_u32 *dest,
										grk_rect_u32 *dest_win,
										uint32_t *src_line_off){
	*src = src_comp->getBuffer()->bounds();
	assert( src->width() <= src_comp->width() && src->height() <= src_comp->height());

	return generateCompositeBounds(compno,
									src,
									(src_comp->getBuffer()->getWindow())->stride,
									dest,
									dest_win,
									src_line_off);
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
bool GrkImage::compositeFrom(const grk_tile *src_tile) {
	for (uint16_t compno = 0; compno < src_tile->numcomps; compno++) {
		auto src_comp 	= src_tile->comps + compno;
		auto dest_comp 	= comps + compno;

		grk_rect_u32 src,dest,dest_win;
		uint32_t src_line_off;

		if (!generateCompositeBounds(src_comp,compno,&src,&dest,&dest_win,&src_line_off))
			return false;

		size_t src_ind = 0;
		auto dest_ind = (size_t) dest_win.x0  + (size_t) dest_win.y0 * dest_comp->stride;
		size_t dest_line_off =  (size_t) dest_comp->stride - (size_t) dest_win.width();
		auto src_ptr = src_comp->getBuffer()->getWindow()->data;
		for (uint32_t j = 0; j < dest_win.height(); ++j) {
			memcpy(dest_comp->data + dest_ind, src_ptr + src_ind,dest_win.width() * sizeof(int32_t));
			dest_ind += dest_win.width() + dest_line_off;
			src_ind  += dest_win.width() + src_line_off;
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
bool GrkImage::compositeFrom(const GrkImage *src_image) {
	for (uint16_t compno = 0; compno < src_image->numcomps; compno++) {
		auto src_comp 	= src_image->comps + compno;
		auto dest_comp 	= comps + compno;

		grk_rect_u32 src,dest,dest_win;
		uint32_t src_line_off;

		if (!generateCompositeBounds(src_comp,compno,&src,&dest,&dest_win,&src_line_off))
			return false;

		size_t src_ind = 0;
		auto dest_ind = (size_t) dest_win.x0  + (size_t) dest_win.y0 * dest_comp->stride;
		size_t dest_line_off =  (size_t) dest_comp->stride - (size_t) dest_win.width();
		auto src_ptr = src_comp->data;
		for (uint32_t j = 0; j < dest_win.height(); ++j) {
			memcpy(dest_comp->data + dest_ind, src_ptr + src_ind,dest_win.width() * sizeof(int32_t));
			dest_ind += dest_win.width() + dest_line_off;
			src_ind  += dest_win.width() + src_line_off;
		}
	}

	return true;
}

GrkImageMeta::GrkImageMeta() {
	obj.wrappee = new GrkObjectImpl(this);
	iptc_buf = nullptr;
	iptc_len = 0;
	xmp_buf = nullptr;
	xmp_len = 0;
	memset(&color, 0, sizeof(color));
}

GrkImageMeta::~GrkImageMeta(){
	FileFormatDecompress::free_color(&color);
	delete[] iptc_buf;
	delete[] xmp_buf;
	delete (GrkObject*)obj.wrappee;
}

}
