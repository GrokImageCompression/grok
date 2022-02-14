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
	static GrkImage* create(grk_image* src, uint16_t numcmpts, grk_image_comp* cmptparms,
							GRK_COLOR_SPACE clrspc, bool doAllocation);
	/**
	 * Allocate data for single image component
	 *
	 * @param imageComp         image component
	 *
	 * @return 		      true if successful
	 */
	static bool allocData(grk_image_comp* imageComp);
	/**
	 * Allocate data for tile compositing
	 *
	 * @return true if successful
	 */
	bool allocCompositeData(CodingParams* cp);

	bool canAllocInterleaved(CodingParams* cp);

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
	bool composite(const GrkImage* srcImg);
	bool compositeInterleaved(const GrkImage* srcImg);
	void createMeta();
	bool greyToRGB(void);
	bool convertToRGB(bool wholeTileDecompress);
	bool applyColourManagement(void);
	void convertPrecision(void);
	bool execUpsample(void);
	void all_components_data_free(void);
	void postReadHeader(CodingParams* cp);
	void validateColourSpace(void);
	bool isSubsampled();

  private:
	~GrkImage();
	bool needsConversionToRGB(void);
	bool isOpacity(uint16_t compno);
	bool compositePlanar(const GrkImage* srcImg);
	bool generateCompositeBounds(const grk_image_comp* srcComp, uint16_t compno,
								 grkRectU32* destWin, uint32_t* srcLineOffset);
	bool generateCompositeBounds(uint16_t compno, grkRectU32 src, uint32_t src_stride,
								 grkRectU32* destWin, uint32_t* srcLineOffset);
	bool allComponentsSanityCheck(bool equalPrecision);
	grk_image* createRGB(uint16_t numcmpts, uint32_t w, uint32_t h, uint8_t prec);
	void sycc_to_rgb(int32_t offset, int32_t upb, int32_t y, int32_t cb, int32_t cr, int32_t* out_r,
					 int32_t* out_g, int32_t* out_b);
	bool sycc444_to_rgb(void);
	bool sycc422_to_rgb(bool oddFirstX);
	bool sycc420_to_rgb(bool oddFirstX, bool oddFirstY);
	bool color_sycc_to_rgb(bool oddFirstX, bool oddFirstY);
	bool color_cmyk_to_rgb(void);
	bool color_esycc_to_rgb(void);
	bool applyICC(void);
	bool cieLabToRGB(void);
	bool componentsEqual(grk_image_comp* src, grk_image_comp* dest);
	static void copyComponent(grk_image_comp* src, grk_image_comp* dest);
	void scaleComponent(grk_image_comp* component, uint8_t precision);
};

} // namespace grk
