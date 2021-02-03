#include <grk_includes.h>

namespace grk {

GrkImage::GrkImage(){
	memset((grk_image*)(this), 0, sizeof(grk_image));
}
GrkImage::~GrkImage(){
	if (comps) {
		grk_image_all_components_data_free(this);
		grk::grk_free(comps);
	}
	FileFormat::free_color(&color);
	delete[] iptc_buf;
	delete[] xmp_buf;
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
		image->comps = ( grk_image_comp  * ) grk::grk_calloc(1,
				image->numcomps * sizeof( grk_image_comp) );
		if (!image->comps) {
			grk::GRK_ERROR("Unable to allocate memory for image.");
			grk_image_destroy(image);
			return nullptr;
		}
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
				grk_image_destroy(image);
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

bool GrkImage::reduceDimensions(uint32_t reduce){
    for (uint32_t compno = 0; compno < numcomps; ++compno) {
        auto img_comp = comps + compno;
        uint32_t temp1,temp2;

        if (x0 > (uint32_t)INT_MAX ||
                y0 > (uint32_t)INT_MAX ||
                x1 > (uint32_t)INT_MAX ||
                y1 > (uint32_t)INT_MAX) {
            GRK_ERROR("Image coordinates above INT_MAX are not supported.");
            return false;
        }

        img_comp->x0 = ceildiv<uint32_t>(x0,img_comp->dx);
        img_comp->y0 = ceildiv<uint32_t>(y0, img_comp->dy);
        uint32_t comp_x1 = ceildiv<uint32_t>(x1, img_comp->dx);
        uint32_t comp_y1 = ceildiv<uint32_t>(y1, img_comp->dy);

        temp1 = ceildivpow2<uint32_t>(comp_x1, reduce);
        temp2 = ceildivpow2<uint32_t>(img_comp->x0, reduce);
        if (temp1 <= temp2) {
            GRK_ERROR("Size x of the decompressed component image is incorrect (comp[%u].w=%u).",
                          compno, (int32_t)temp1 - (int32_t)temp2);
            return false;
        }
        img_comp->w  = (uint32_t)(temp1 - temp2);
        assert(img_comp->w);

        temp1 = ceildivpow2<uint32_t>(comp_y1, reduce);
        temp2 = ceildivpow2<uint32_t>(img_comp->y0, reduce);
         if (temp1 <= temp2) {
            GRK_ERROR("Size y of the decompressed component image is incorrect (comp[%u].h=%u).",
                          compno, (int32_t)temp1 - (int32_t)temp2);
            return false;
        }
        img_comp->h = (uint32_t)(temp1 - temp2);
        assert(img_comp->h);
    }

    return true;
}

/**
 * Copy only header of image and its component header (no data copied)
 * if dest image has data, it will be freed
 *
 * @param	dest	the dest image
 *
 * @return true if successful
 *
 */
bool GrkImage::copyHeader(GrkImage *dest) {
	assert(dest != nullptr);

	dest->x0 = x0;
	dest->y0 = y0;
	dest->x1 = x1;
	dest->y1 = y1;

	assert(!dest->comps);

	if (dest->comps) {
		grk_image_all_components_data_free(dest);
		grk_free(dest->comps);
		dest->comps = nullptr;
	}
	dest->numcomps = numcomps;
	dest->comps = ( grk_image_comp  * ) grk_malloc(dest->numcomps * sizeof( grk_image_comp) );
	if (!dest->comps) {
		dest->comps = nullptr;
		dest->numcomps = 0;
		return false;
	}

	for (uint32_t compno = 0; compno < dest->numcomps; compno++) {
		memcpy(&(dest->comps[compno]), &(comps[compno]),sizeof( grk_image_comp) );
		dest->comps[compno].data = nullptr;
	}

	dest->color_space = color_space;
	auto color_dest = &dest->color;
	auto color_src = &color;
	delete [] color_dest->icc_profile_buf;
	color_dest->icc_profile_len = color_src->icc_profile_len;
	if (color_dest->icc_profile_len) {
		color_dest->icc_profile_buf = new uint8_t[color_dest->icc_profile_len];
		memcpy(color_dest->icc_profile_buf, color_src->icc_profile_buf,
				color_src->icc_profile_len);
	} else
		color_dest->icc_profile_buf = nullptr;
	if (color.palette){
		auto pal_src = color.palette;
		if (pal_src->num_channels && pal_src->num_entries){
			FileFormat::alloc_palette(&dest->color, pal_src->num_channels, pal_src->num_entries);
			auto pal_dest = dest->color.palette;
			memcpy(pal_dest->channel_prec, pal_src->channel_prec, pal_src->num_channels * sizeof(uint8_t) );
			memcpy(pal_dest->channel_sign, pal_src->channel_sign, pal_src->num_channels * sizeof(bool) );

			pal_dest->component_mapping = new grk_component_mapping_comp[pal_dest->num_channels];
			memcpy(pal_dest->component_mapping, pal_src->component_mapping,
									pal_src->num_channels * sizeof(grk_component_mapping_comp));

			memcpy(pal_dest->lut, pal_src->lut, pal_src->num_channels * pal_src->num_entries * sizeof(uint32_t));
		}
	}

	return true;
}


bool GrkImage::allocData(grk_image_comp  *comp) {
	if (!comp)
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
	comp->owns_data = true;
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
		dest_comp->owns_data = src_comp->owns_data;
		if (src_comp->stride){
			dest_comp->stride = src_comp->stride;
			assert(dest_comp->stride >= dest_comp->w);
		}
		src_comp->data = nullptr;
	}
}

GrkImage* GrkImage::duplicate(void){
	auto dest = new GrkImage();

	if (!copyHeader(dest)) {
		delete dest;
		return nullptr;
	}
	for (uint32_t compno = 0; compno < numcomps; ++compno){
		auto src_comp = comps + compno;
		auto dest_comp = dest->comps + compno;
		if (src_comp->data)
			memcpy(dest_comp->data, src_comp->data, src_comp->w * src_comp->stride);
	}

	return dest;
}

/**
 * Create new image and copy tile buffer data in
 *
 * @param tile_src_data	tile source data
 *
 * @return new GrkImage if successful
 *
 */
GrkImage* GrkImage::duplicate(const grk_tile* tile_src_data){
	auto dest = new GrkImage();

	if (!copyHeader(dest)) {
		delete dest;
		return nullptr;
	}
	for (uint32_t compno = 0; compno < tile_src_data->numcomps; ++compno){
		auto src_comp = tile_src_data->comps + compno;
		auto dest_comp = dest->comps + compno;
		auto tileBounds = src_comp->getBuffer()->bounds();
		// ! force component dimensions to tile dimensions
		dest_comp->w = tileBounds.width();
		dest_comp->h = tileBounds.height();
		if (!src_comp->getBuffer()->getWindow()->data)
			continue;
		if (!allocData(dest_comp)){
			delete dest;
			return nullptr;
		}
		src_comp->getBuffer()->getWindow()->copy_data(dest_comp->data, dest_comp->w, dest_comp->h, dest_comp->stride);
	}

	return dest;
}

void GrkImage::transferDataFrom(const grk_tile* tile_src_data){
	for (uint16_t compno = 0; compno < numcomps; compno++) {
		auto src_comp = tile_src_data->comps + compno;
		auto dest_comp = comps + compno;

		//transfer memory from tile component to output image
		src_comp->getBuffer()->transfer(&dest_comp->data, &dest_comp->owns_data, &dest_comp->stride);
		assert(dest_comp->stride >= dest_comp->w);
	}
}

/**
 * Copy tile data to composite image
 *
 * tile_data stores only the decompressed resolutions, in the actual precision
 * of the decompressed image. This method copies a sub-region of this region
 * into the image (which stores data in 32 bit precision). Tile data will be released
 * after compositing is complete
 *
 * @param src_tile 	source tile
 * @param cp		coding parameters
 *
 * @return:			true if successful
 */
bool GrkImage::compositeFrom(grk_tile *src_tile,CodingParams *cp) {
	for (uint32_t i = 0; i < src_tile->numcomps; i++) {
		auto src_comp = src_tile->comps + i;
		auto dest_comp = comps + i;

		grk_rect_u32 src,dest,dest_win;
		uint32_t src_stride = (src_comp->getBuffer()->getWindow())->stride;

		src = src_comp->getBuffer()->bounds();
		assert( src.width() <= src_comp->width() && src.height() <= src_comp->height());

		/* Compute the area (0, 0, src_x1, src_y1)
		 * of the input buffer (decompressed tile component) which will be copied
		 * to the output buffer.
		 *
		 * Compute the area of the output buffer (dest_win_x0, dest_win_y0, dest_width, dest_height)
		 * which will be modified by this input area.
		 *
	     */
		/* Border of the current output component. (x0_dest,y0_dest)
		 * corresponds to origin of dest buffer */
		auto reduce = cp->m_coding_params.m_dec.m_reduce;
		dest.x0 = ceildivpow2<uint32_t>(dest_comp->x0, reduce);
		dest.y0 = ceildivpow2<uint32_t>(dest_comp->y0, reduce);
		/* can't overflow given that image->x1 is uint32 */
		dest.x1 = dest.x0 + dest_comp->w;
		dest.y1 = dest.y0 + dest_comp->h;

		uint32_t src_line_off = src_stride - src.width();
		if (dest.x0 < src.x0) {
			dest_win.x0 = (uint32_t) (src.x0 - dest.x0);
			if (dest.x1 >= src.x1) {
				dest_win.x1 = dest_win.x0 + src.width();
			} else {
				dest_win.x1 = dest_win.x0 + (uint32_t) (dest.x1 - src.x0);
				src_line_off = src_stride - dest_win.width();
			}
		} else {
			dest_win.x0 = 0U;
			if (dest.x1 >= src.x1) {
				dest_win.x1 = dest_win.x0 + src.width();
			} else {
				dest_win.x1 = dest_win.x0 + dest_comp->w;
				src_line_off = (uint32_t) (src.x1 - dest.x1);
			}
		}
		if (dest.y0 < src.y0) {
			dest_win.y0 = (uint32_t) (src.y0 - dest.y0);
			dest_win.y1 = dest_win.y0 + ((dest.y1 >= src.y1) ?  src.height() : (uint32_t) (dest.y1 - src.y0));
		} else {
			dest_win.y1 = dest_win.y0 + src.height();
		}
		if (dest_win.width() > dest_comp->w || dest_win.height() > dest_comp->h)
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
		src_comp->release_mem(true);
	}

	return true;
}

}
