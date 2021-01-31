#include <GrkImage.h>

namespace grk {

grk_image *  image_create(uint16_t numcmpts,
		 grk_image_cmptparm  *cmptparms, GRK_COLOR_SPACE clrspc, bool allocData) {
	auto image = (grk_image * ) grk::grk_calloc(1, sizeof(grk_image));

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
			if (allocData && !image_single_component_data_alloc(comp)) {
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

void image_destroy(grk_image *image) {
	if (image) {
		if (image->comps) {
			grk_image_all_components_data_free(image);
			grk::grk_free(image->comps);
		}
		FileFormat::free_color(&image->color);
		delete[] image->iptc_buf;
		delete[] image->xmp_buf;
		grk::grk_free(image);
	}
}

/**
 * Copy only header of image and its component header (no data are copied)
 * if dest image have data, they will be freed
 *
 * @param	image_src		the src image
 * @param	image_dest	the dest image
 *
 */
bool copy_image_header(const grk_image *image_src,grk_image *image_dest) {
	assert(image_src != nullptr);
	assert(image_dest != nullptr);

	image_dest->x0 = image_src->x0;
	image_dest->y0 = image_src->y0;
	image_dest->x1 = image_src->x1;
	image_dest->y1 = image_src->y1;

	if (image_dest->comps) {
		grk_image_all_components_data_free(image_dest);
		grk_free(image_dest->comps);
		image_dest->comps = nullptr;
	}
	image_dest->numcomps = image_src->numcomps;
	image_dest->comps = ( grk_image_comp  * ) grk_malloc(image_dest->numcomps * sizeof( grk_image_comp) );
	if (!image_dest->comps) {
		image_dest->comps = nullptr;
		image_dest->numcomps = 0;
		return false;
	}

	for (uint32_t compno = 0; compno < image_dest->numcomps; compno++) {
		memcpy(&(image_dest->comps[compno]), &(image_src->comps[compno]),sizeof( grk_image_comp) );
		image_dest->comps[compno].data = nullptr;
	}

	image_dest->color_space = image_src->color_space;
	auto color_dest = &image_dest->color;
	auto color_src = &image_src->color;
	delete [] color_dest->icc_profile_buf;
	color_dest->icc_profile_len = color_src->icc_profile_len;
	if (color_dest->icc_profile_len) {
		color_dest->icc_profile_buf = new uint8_t[color_dest->icc_profile_len];
		memcpy(color_dest->icc_profile_buf, color_src->icc_profile_buf,
				color_src->icc_profile_len);
	} else
		color_dest->icc_profile_buf = nullptr;
	if (image_src->color.palette){
		auto pal_src = image_src->color.palette;
		if (pal_src->num_channels && pal_src->num_entries){
			FileFormat::alloc_palette(&image_dest->color, pal_src->num_channels, pal_src->num_entries);
			auto pal_dest = image_dest->color.palette;
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


bool image_single_component_data_alloc(grk_image_comp  *comp) {
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

bool update_image_dimensions(grk_image* image, uint32_t reduce){
    for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
        auto img_comp = image->comps + compno;
        uint32_t temp1,temp2;

        if (image->x0 > (uint32_t)INT_MAX ||
                image->y0 > (uint32_t)INT_MAX ||
                image->x1 > (uint32_t)INT_MAX ||
                image->y1 > (uint32_t)INT_MAX) {
            GRK_ERROR("Image coordinates above INT_MAX are not supported.");
            return false;
        }

        img_comp->x0 = ceildiv<uint32_t>(image->x0,img_comp->dx);
        img_comp->y0 = ceildiv<uint32_t>(image->y0, img_comp->dy);
        uint32_t comp_x1 = ceildiv<uint32_t>(image->x1, img_comp->dx);
        uint32_t comp_y1 = ceildiv<uint32_t>(image->y1, img_comp->dy);

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
 Transfer data from src to dest for each component, and null out src data.
 Assumption:  src and dest have the same number of components
 */
void transfer_image_data(grk_image *src, grk_image *dest) {
	if (!src || !dest || !src->comps || !dest->comps
			|| src->numcomps != dest->numcomps)
		return;

	for (uint32_t compno = 0; compno < src->numcomps; compno++) {
		auto src_comp = src->comps + compno;
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

grk_image* make_copy(const grk_image *src){
	auto dest = (grk_image * ) grk::grk_calloc(1, sizeof(grk_image));

	if (!copy_image_header(src,dest)) {
		image_destroy(dest);
		return nullptr;
	}
	for (uint32_t compno = 0; compno < src->numcomps; ++compno){
		auto src_comp = src->comps + compno;
		auto dest_comp = dest->comps + compno;
		if (src_comp->data)
			memcpy(dest_comp->data, src_comp->data, src_comp->w * src_comp->stride);
	}

	return dest;
}

grk_image* make_copy(const grk_image *src, const grk_tile* tile_src){
	auto dest = (grk_image * ) grk::grk_calloc(1, sizeof(grk_image));

	if (!copy_image_header(src,dest)) {
		image_destroy(dest);
		return nullptr;
	}
	for (uint32_t compno = 0; compno < tile_src->numcomps; ++compno){
		auto src_comp = tile_src->comps + compno;
		auto dest_comp = dest->comps + compno;
		if (!image_single_component_data_alloc(dest_comp)){
			image_destroy(dest);
			return nullptr;
		}
		src_comp->getBuffer()->getWindow()->copy_data(dest_comp->data, dest_comp->w, dest_comp->h, dest_comp->stride);
	}

	return dest;
}


}

