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

GrkImage *  GrkImage::image_create(uint16_t numcmpts,
		 grk_image_cmptparm  *cmptparms, GRK_COLOR_SPACE clrspc, bool allocData) {
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

/**
 * Copy only header of image and its component header (no data are copied)
 * if dest image have data, they will be freed
 *
 * @param	dest	the dest image
 *
 */
bool GrkImage::copy_image_header(GrkImage *dest) {
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


bool GrkImage::image_single_component_data_alloc(grk_image_comp  *comp) {
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

bool GrkImage::update_image_dimensions(uint32_t reduce){
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
 Transfer data from src to dest for each component, and null out src data.
 Assumption:  src and dest have the same number of components
 */
void GrkImage::transfer_image_data(GrkImage *dest) {
	if (!dest || !comps || !dest->comps
			|| numcomps != dest->numcomps)
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

GrkImage* GrkImage::make_copy(void){
	auto dest = new GrkImage();

	if (!copy_image_header(dest)) {
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

GrkImage* GrkImage::make_copy(const grk_tile* tile_src){
	auto dest = new GrkImage();

	if (!copy_image_header(dest)) {
		delete dest;
		return nullptr;
	}
	for (uint32_t compno = 0; compno < tile_src->numcomps; ++compno){
		auto src_comp = tile_src->comps + compno;
		auto dest_comp = dest->comps + compno;
		if (!src_comp->getBuffer()->getWindow()->data)
			continue;
		if (!image_single_component_data_alloc(dest_comp)){
			delete dest;
			return nullptr;
		}
		src_comp->getBuffer()->getWindow()->copy_data(dest_comp->data, dest_comp->w, dest_comp->h, dest_comp->stride);
	}

	return dest;
}


}

