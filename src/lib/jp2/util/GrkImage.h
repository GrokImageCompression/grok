#pragma once

#include "grk_includes.h"

namespace grk {

struct grk_tile;
struct CodingParams;


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
	* @param doAllocation  true if data is to be allocated, otherwise false
	*
	* @return 		     a new image if successful, otherwise nullptr
	* */
	static GrkImage *  create(uint16_t numcmpts,
			 	 	 	 	 	grk_image_cmptparm  *cmptparms,
								GRK_COLOR_SPACE clrspc,
								bool doAllocation);

	/**
	 * Allocate data for single image component
	 *
	 * @param image         image
	 *
	 * @return 		      true if successful
	 */
	static bool allocData(	grk_image_comp *image);


	/**
	 * Allocate data buffer to mirror "mirror" image
	 *
	 * @param mirror mirror image
	 *
	 * @return true if successful
	 */
	bool allocMirrorData(GrkImage *src);


	/**
	 * tile_data stores only the decompressed resolutions, in the actual precision
	 * of the decompressed image. This method copies a sub-region of this region
	 * into p_output_image (which stores data in 32 bit precision)
	 *
	 * @param p_output_image:
	 *
	 * @return:
	 */
	bool copy(grk_tile *tile,CodingParams *cp);

	/**
	 * Copy only header of image and its component header (no data are copied)
	 * if dest image have data, they will be freed
	 *
	 * @param	dest	the dest image
	 *
	 * @return 	true if successful
	 *
	 */
	bool copyHeader(GrkImage *dest);

	GrkImage* duplicate();
	GrkImage* duplicate(const grk_tile* tile_src);

	bool reduceDimensions(uint32_t reduce);

	/**
	 Transfer data to dest for each component, and null out "this" data.
	 Assumption:  "this" and dest have the same number of components
	 */
	void transferData(GrkImage *dest);
};


}
