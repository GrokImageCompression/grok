#pragma once

namespace grk {

struct grk_tile;


class GrkImage : public grk_image {
public:
	GrkImage();
	~GrkImage();

	/**
	* Create image
	*
	* @param numcmpts      number of components
	* @param cmptparms     component parameters
	* @param clrspc        image color space
	* @param allocData		true if data is to be allocated, otherwise false
	*
	* @return 		     a new image if successful, otherwise nullptr
	* */
	static GrkImage *  image_create(uint16_t numcmpts,
			 	 	 	 	 	grk_image_cmptparm  *cmptparms,
								GRK_COLOR_SPACE clrspc,
								bool allocData);


	GrkImage* make_copy();

	GrkImage* make_copy(const grk_tile* tile_src);

	/**
	 * Copy only header of image and its component header (no data are copied)
	 * if dest image have data, they will be freed
	 *
	 * @param	dest	the dest image
	 *
	 * @return 	true if successful
	 *
	 */
	bool copy_image_header(GrkImage *dest);

	/**
	 * Allocate data for single image component
	 *
	 * @param image         image
	 *
	 * @return 		      true if successful
	 */
	static bool image_single_component_data_alloc(	grk_image_comp *image);


	bool update_image_dimensions(uint32_t reduce);

	/**
	 *
	 *
	 Transfer data from src to dest for each component, and null out src data.
	 Assumption:  src and dest have the same number of components
	 */
	void transfer_image_data(GrkImage *dest);

};


}
