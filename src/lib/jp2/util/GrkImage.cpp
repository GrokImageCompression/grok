#include <grk_includes.h>

namespace grk {

GrkImage::GrkImage(){
	memset((grk_image*)(this), 0, sizeof(grk_image));
}
GrkImage::~GrkImage(){
	// delete parent
	if (comps) {
		grk_image_all_components_data_free(this);
		grk::grk_free(comps);
	}
	FileFormat::free_color(&color);
	delete[] iptc_buf;
	delete[] xmp_buf;
}

GrkImage *  GrkImage::create(uint16_t numcmpts,
		 grk_image_cmptparm  *cmptparms, GRK_COLOR_SPACE clrspc, bool doAllocation) {
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

/**
 * Copy only header of image and its component header (no data are copied)
 * if dest image have data, they will be freed
 *
 * @param	dest	the dest image
 *
 */
bool GrkImage::copyHeader(GrkImage *dest) {
	assert(dest != nullptr);

	dest->x0 = x0;
	dest->y0 = y0;
	dest->x1 = x1;
	dest->y1 = y1;

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
 Transfer data to dest for each component, and null out this data.
 Assumption:  this and dest have the same number of components
 */
void GrkImage::transferData(GrkImage *dest) {
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

GrkImage* GrkImage::duplicate(const grk_tile* tile_src_data){
	auto dest = new GrkImage();

	if (!copyHeader(dest)) {
		delete dest;
		return nullptr;
	}
	for (uint32_t compno = 0; compno < tile_src_data->numcomps; ++compno){
		auto src_comp = tile_src_data->comps + compno;
		auto dest_comp = dest->comps + compno;
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

/**
 * Allocate data buffer to mirror "mirror" image
 *
 * @param mirror mirror image
 *
 * @return true if successful
 */
bool GrkImage::allocMirrorData(GrkImage* mirror){
	assert(numcomps == mirror->numcomps);

	for (uint32_t i = 0; i < numcomps; i++) {
		auto comp_dest = comps + i;

		if (comp_dest->w  == 0 || comp_dest->h == 0) {
			GRK_ERROR("Output component %d has invalid dimensions %u x %u",
					i, comp_dest->w, comp_dest->h);
			return false;
		}
		if (!comp_dest->data) {
			if (!GrkImage::allocData(comp_dest)){
				GRK_ERROR("Failed to allocate pixel data for component %d, with dimensions %u x %u",
						i, comp_dest->w, comp_dest->h);
				return false;
			}
			memset(comp_dest->data, 0,	(uint64_t)comp_dest->stride * comp_dest->h * sizeof(int32_t));
		}
	}

	return true;

}

/**
 * tile_data stores only the decompressed resolutions, in the actual precision
 * of the decompressed image. This method copies a sub-region of this region
 * into p_output_image (which stores data in 32 bit precision)
 *
 * @param p_output_image:
 *
 * @return:
 */
bool GrkImage::copy(grk_tile *tile,CodingParams *cp) {
	for (uint32_t i = 0; i < tile->numcomps; i++) {
		auto tilec = tile->comps + i;
		auto comp_dest = comps + i;

		/* Border of the current output component. (x0_dest,y0_dest)
		 * corresponds to origin of dest buffer */
		auto reduce = cp->m_coding_params.m_dec.m_reduce;
		uint32_t x0_dest = ceildivpow2<uint32_t>(comp_dest->x0, reduce);
		uint32_t y0_dest = ceildivpow2<uint32_t>(comp_dest->y0, reduce);
		/* can't overflow given that image->x1 is uint32 */
		uint32_t x1_dest = x0_dest + comp_dest->w;
		uint32_t y1_dest = y0_dest + comp_dest->h;

		grk_rect_u32 src_dim = tilec->getBuffer()->bounds();
		uint32_t width_src = (uint32_t) src_dim.width();
		uint32_t stride_src = tilec->getBuffer()->getWindow()->stride;
		uint32_t height_src = (uint32_t) src_dim.height();

		/* Compute the area (0, 0, off_x1_src, off_y1_src)
		 * of the input buffer (decompressed tile component) which will be moved
		 * to the output buffer. Compute the area of the output buffer (off_x0_dest,
		 * off_y0_dest, width_dest, height_dest)  which will be modified
		 * by this input area.
		 * */
		uint32_t life_off_src = stride_src - width_src;
		uint32_t off_x0_dest = 0;
		uint32_t width_dest = 0;
		if (x0_dest < src_dim.x0) {
			off_x0_dest = (uint32_t) (src_dim.x0 - x0_dest);
			if (x1_dest >= src_dim.x1) {
				width_dest = width_src;
			} else {
				width_dest = (uint32_t) (x1_dest - src_dim.x0);
				life_off_src = stride_src - width_dest;
			}
		} else {
			off_x0_dest = 0U;
			if (x1_dest >= src_dim.x1) {
				width_dest = width_src;
			} else {
				width_dest = comp_dest->w;
				life_off_src = (uint32_t) (src_dim.x1 - x1_dest);
			}
		}

		uint32_t off_y0_dest = 0;
		uint32_t height_dest = 0;
		if (y0_dest < src_dim.y0) {
			off_y0_dest = (uint32_t) (src_dim.y0 - y0_dest);
			if (y1_dest >= src_dim.y1) {
				height_dest = height_src;
			} else {
				height_dest = (uint32_t) (y1_dest - src_dim.y0);
			}
		} else {
			off_y0_dest = 0;
			if (y1_dest >= src_dim.y1) {
				height_dest = height_src;
			} else {
				height_dest = comp_dest->h;
			}
		}
		if (width_dest > comp_dest->w || height_dest > comp_dest->h)
			return false;
		if (width_src > tilec->width() || height_src > tilec->height())
			return false;

		size_t src_ind = 0;
		auto dest_ind = (size_t) off_x0_dest
				  	  + (size_t) off_y0_dest * comp_dest->stride;
		size_t line_off_dest =  (size_t) comp_dest->stride - (size_t) width_dest;
		auto src_ptr = tilec->getBuffer()->getWindow()->data;
		for (uint32_t j = 0; j < height_dest; ++j) {
			memcpy(comp_dest->data + dest_ind, src_ptr + src_ind,width_dest * sizeof(int32_t));
			dest_ind += width_dest + line_off_dest;
			src_ind  += width_dest + life_off_src;
		}
		tilec->release_mem(true);
	}

	return true;
}


}

