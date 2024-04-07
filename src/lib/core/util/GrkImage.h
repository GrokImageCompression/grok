#pragma once

#include "grk_includes.h"

namespace grk
{
struct Tile;
struct CodingParams;
struct TileComponent;

const uint32_t singleTileRowsPerStrip = 32;

class GrkImageMeta : public grk_image_meta
{
 public:
   GrkImageMeta();
   virtual ~GrkImageMeta();
   void releaseColor(void);
   void releaseColorPalatte();
   void allocPalette(uint8_t num_channels, uint16_t num_entries);
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
   static bool allocData(grk_image_comp* imageComp, bool clear);
   static bool allocData(grk_image_comp* imageComp);
   /**
	* Allocate data for tile compositing
	*
	* @return true if successful
	*/
   bool allocCompositeData(void);
   bool supportsStripCache(CodingParams* cp);

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
   bool composite(const GrkImage* src);
   bool compositeInterleaved(const GrkImage* src);
   bool compositeInterleaved(const Tile* src, uint32_t yBegin, uint32_t yEnd);
   bool greyToRGB(void);
   bool convertToRGB(bool wholeTileDecompress);
   bool applyColourManagement(void);
   bool applyICC(void);
   bool validateICC(void);
   void convertPrecision(void);
   bool execUpsample(void);
   void all_components_data_free(void);
   void postReadHeader(CodingParams* cp);
   void validateColourSpace(void);
   bool isSubsampled();
   bool validateZeroed(void);
   bool applyColour(void);
   bool apply_palette_clr(void);
   bool check_color(void);
   void apply_channel_definition(void);
   void allocPalette(uint8_t num_channels, uint16_t num_entries);
   uint32_t width(void) const;
   uint32_t height(void) const;
   void print(void) const;
   bool componentsEqual(bool checkPrecision);
   bool componentsEqual(uint16_t firstNComponents, bool checkPrecision);

 private:
   ~GrkImage();
   static void single_component_data_free(grk_image_comp* comp);
   std::string getColourSpaceString(void);
   std::string getICCColourSpaceString(cmsColorSpaceSignature color_space);
   bool isValidICCColourSpace(uint32_t signature);
   bool needsConversionToRGB(void);
   bool isOpacity(uint16_t compno);
   bool compositePlanar(const GrkImage* srcImg);
   bool generateCompositeBounds(const grk_image_comp* srcComp, uint16_t destCompno,
								grk_rect32* destWin);
   bool generateCompositeBounds(grk_rect32 src, uint16_t destCompno, grk_rect32* destWin);
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
   bool cieLabToRGB(void);
   bool componentsEqual(grk_image_comp* src, grk_image_comp* dest, bool checkPrecision);
   static void copyComponent(grk_image_comp* src, grk_image_comp* dest);
   void scaleComponent(grk_image_comp* component, uint8_t precision);
};

} // namespace grk
