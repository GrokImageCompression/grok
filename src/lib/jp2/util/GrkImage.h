#pragma once

#include "grk_includes.h"

namespace grk
{
struct Tile;
struct CodingParams;
struct TileComponent;

class GrkImageMeta : public grk_image_meta
{
  public:
	GrkImageMeta();
	virtual ~GrkImageMeta();
};

class GrkImage : public grk_image
{
	friend GrkObjectWrapperImpl<GrkImage>;

  public:
	GrkImage();
	bool subsampleAndReduce(uint32_t reduce);
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
	static GrkImage* create(uint16_t numcmpts, grk_image_cmptparm* cmptparms,
							GRK_COLOR_SPACE clrspc, bool doAllocation);
	/**
	 * Allocate data for single image component
	 *
	 * @param image         image
	 *
	 * @return 		      true if successful
	 */
	static bool allocData(grk_image_comp* image);
	/**
	 * Allocate data
	 *
	 * @return true if successful
	 */
	bool allocData();
	/**
	 * Copy only header of image and its component header (no data are copied)
	 * if dest image have data, they will be freed
	 *
	 * @param	dest	the dest image
	 *
	 *
	 */
	void copyHeader(GrkImage* dest);
	/**
	 Transfer data to dest for each component, and null out "this" data.
	 Assumption:  "this" and dest have the same number of components
	 */
	void transferDataTo(GrkImage* dest);
	void transferDataFrom(const Tile* tile_src_data);
	GrkImage* duplicate(const Tile* tile_src);
	/**
	 * Copy tile to composite image
	 *
	 * tile_data stores only the decompressed resolutions, in the actual precision
	 * of the decompressed image. This method copies a sub-region of this region
	 * into p_output_image (which stores data in 32 bit precision)
	 *
	 * @param src_tile 	source tile
	 *
	 * @return:			true if successful
	 */
	bool compositeFrom(const Tile* src_tile);
	bool compositeFrom(const GrkImage* src_img);
	bool generateCompositeBounds(const TileComponent* src_comp, uint16_t compno, grkRectU32* src,
								 grkRectU32* dest, grkRectU32* dest_win, uint32_t* src_line_off);

	bool generateCompositeBounds(const grk_image_comp* src_comp, uint16_t compno, grkRectU32* src,
								 grkRectU32* dest, grkRectU32* dest_win, uint32_t* src_line_off);
	bool generateCompositeBounds(uint16_t compno, grkRectU32* src, uint32_t src_stride,
								 grkRectU32* dest, grkRectU32* dest_win, uint32_t* src_line_off);
	void createMeta();

	bool allComponentsSanityCheck(bool equalPrecision);
	grk_image* create_rgb_no_subsample_image(uint16_t numcmpts, uint32_t w, uint32_t h,	uint8_t prec);
	void sycc_to_rgb(int32_t offset, int32_t upb, int32_t y, int32_t cb, int32_t cr,
							int32_t* out_r, int32_t* out_g, int32_t* out_b);
	bool sycc444_to_rgb(void);
	bool sycc422_to_rgb(bool oddFirstX);
	bool sycc420_to_rgb(bool oddFirstX, bool oddFirstY);
	bool color_sycc_to_rgb(bool oddFirstX, bool oddFirstY);
	bool color_cmyk_to_rgb(void);
	bool color_esycc_to_rgb(void);
  private:
	~GrkImage();
	bool ownsData;
};

} // namespace grk
