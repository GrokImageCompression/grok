#pragma once

#include <grk_includes.h>

namespace grk {

/**
* Create image
*
* @param numcmpts      number of components
* @param cmptparms     component parameters
* @param clrspc        image color space
* @param allocData		true if data is to be allocated, otherwise false
*
* @return returns      a new image if successful, otherwise nullptr
* */
grk_image *  image_create(uint16_t numcmpts,
		 	 	 	 	 	grk_image_cmptparm  *cmptparms,
							GRK_COLOR_SPACE clrspc,
							bool allocData);

/**
 * Deallocate all resources associated with an image
 *
 * @param image         image
 */
void image_destroy(grk_image *image);


/**
 * Copy only header of image and its component header (no data are copied)
 * if dest image have data, they will be freed
 *
 * @param	image_src		the src image
 * @param	image_dest	the dest image
 *
 */
void copy_image_header(const grk_image *image_src,grk_image *image_dest);

/**
 * Allocate data for single image component
 *
 * @param image         image
 */
bool image_single_component_data_alloc(	grk_image_comp *image);

bool update_image_dimensions(grk_image* image, uint32_t reduce);

/**
 Transfer data from src to dest for each component, and null out src data.
 Assumption:  src and dest have the same number of components
 */
void transfer_image_data(grk_image *src, grk_image *dest);



}
