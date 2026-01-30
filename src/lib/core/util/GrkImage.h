/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

namespace grk
{
struct Tile;
struct CodingParams;
struct TileComponent;

const uint32_t singleTileRowsPerStrip = 32;

constexpr uint32_t GRK_CIE_DAY = ((((uint32_t)'C') << 24) + (((uint32_t)'T') << 16));
constexpr uint32_t GRK_CIE_D50 = ((uint32_t)0x00443530);
constexpr uint32_t GRK_CIE_D65 = ((uint32_t)0x00443635);
constexpr uint32_t GRK_CIE_D75 = ((uint32_t)0x00443735);
constexpr uint32_t GRK_CIE_SA = ((uint32_t)0x00005341);
constexpr uint32_t GRK_CIE_SC = ((uint32_t)0x00005343);
constexpr uint32_t GRK_CIE_F2 = ((uint32_t)0x00004632);
constexpr uint32_t GRK_CIE_F7 = ((uint32_t)0x00004637);
constexpr uint32_t GRK_CIE_F11 = ((uint32_t)0x00463131);

/**
 * @class GrkImage
 * @brief Stores header and data for an image
 */
class GrkImage : public grk_image
{
  friend GrkObjectWrapperImpl<GrkImage>;

public:
  /**
   * @brief Constructs a GrkImage
   */
  GrkImage();
  /**
   * @brief Creates a GrkImage
   *
   * @param src           image source (see @ref grk_image)
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
   * @param clear         clear image data if true
   *
   * @return 		      true if successful
   */
  static bool allocData(grk_image_comp* imageComp, bool clear);

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
  bool allocCompositeData(void);

  /**
   * @brief Copies only header of image and its component header
   *
   * No data is copied. If dest has data then it will be freed
   * @param	dest destination @GrkImage
   */
  void copyHeaderTo(GrkImage* dest) const;
  /**
   Transfer data to dest for each component, and null out "this" data.
   Assumption:  "this" and dest have the same number of components
   */
  void transferDataTo(GrkImage* dest);
  GrkImage* extractFrom(const Tile* tile_src) const;
  GrkImage* duplicate(void) const;
  bool composite(const GrkImage* src);
  bool greyToRGB(void);
  bool applyColourManagement(void);
  bool validateICC(void);
  void all_components_data_free(void);
  void postReadHeader(CodingParams* cp);
  void validateColourSpace(void);
  bool isSubsampled() const;

  bool check_color(uint16_t signalledNumComps);
  void apply_channel_definition(void);
  void allocPalette(uint8_t num_channels, uint16_t num_entries);
  uint32_t width(void) const;
  uint32_t height(void) const;
  void print(void) const;
  bool componentsEqual(bool checkPrecision) const;
  bool componentsEqual(uint16_t firstNComponents, bool checkPrecision) const;
  static void setDataToNull(grk_image_comp* comp);

  /**
   * @brief Gets unreduced, non-subsampled image bounds
   *
   * Component bounds my differ due to subsampling and reduction
   */
  Rect32 getBounds(void) const;

  /**
   * @brief Generates subsampled and reduced bounds for components
   *
   * If data has been allocated and the new width or height differ
   * from old with or height respectively, then data is de-allocated
   */
  bool subsampleAndReduce(uint8_t reduce);

  bool compositeInterleaved(const Tile* src, uint32_t yBegin, uint32_t yEnd)
  {
    switch(comps->data_type)
    {
      case GRK_INT_32:
        return compositeInterleaved_T<int32_t>(src, yBegin, yEnd);
      // case GRK_INT_16:
      //   return compositeInterleaved_T<int16_t>(src,yBegin,yEnd);
      // case GRK_INT_8:
      //   return compositeInterleaved_T<int8_t>(src,yBegin,yEnd);
      // case GRK_FLOAT:
      //   return compositeInterleaved_T<float>(src,yBegin,yEnd);
      // case GRK_DOUBLE:
      //   return compositeInterleaved_T<double>(src,yBegin,yEnd);
      default:
        return false;
    }
  }
  bool postProcess(void)
  {
    switch(comps->data_type)
    {
      case GRK_INT_32:
        return postProcess_T<int32_t>();
      // case GRK_INT_16:
      //   return postProcess_T<int16_t>();
      // case GRK_INT_8:
      //   return postProcess_T<int8_t>();
      // case GRK_FLOAT:
      //   return postProcess_T<float>();
      // case GRK_DOUBLE:
      //   return postProcess_T<double>();
      default:
        return false;
    }
  }

  bool applyColour(void)
  {
    switch(comps->data_type)
    {
      case GRK_INT_32:
        return applyColour_T<int32_t>();
      // case GRK_INT_16:
      //   return applyColour_T<int16_t>();
      // case GRK_INT_8:
      //   return applyColour_T<int8_t>();
      // case GRK_FLOAT:
      //   return applyColour_T<float>();
      // case GRK_DOUBLE:
      //   return applyColour_T<double>();
      default:
        return false;
    }
  }

  void transferDataFrom(const Tile* tile_src_data);

private:
  /**
   * @brief Destroys a GrkImage
   *
   * This is private because this struct
   */
  ~GrkImage();

  template<typename T>
  bool applyColour_T(void);

  template<typename T>
  bool convertToRGB_T();

  template<typename T>
  bool compositeInterleaved_T(const Tile* src, uint32_t yBegin, uint32_t yEnd);

  template<typename T>
  bool postProcess_T(void);

  static size_t sizeOfDataType(grk_data_type type);
  static void single_component_data_free(grk_image_comp* comp);
  std::string getColourSpaceString(void) const;
  std::string getICCColourSpaceString(cmsColorSpaceSignature color_space) const;
  bool isValidICCColourSpace(uint32_t signature) const;
  bool needsConversionToRGB(void) const;
  bool isOpacity(uint16_t compno) const;
  bool generateCompositeBounds(const grk_image_comp* srcComp, uint16_t destCompno, Rect32* destWin);
  bool generateCompositeBounds(Rect32 src, uint16_t destCompno, Rect32* destWin);
  bool allComponentsSanityCheck(bool equalPrecision) const;
  grk_image* createRGB(uint16_t numcmpts, uint32_t w, uint32_t h, uint8_t prec);
  bool componentsEqual(grk_image_comp* src, grk_image_comp* dest, bool checkPrecision) const;
  static void copyComponent(grk_image_comp* src, grk_image_comp* dest);

public:
  template<typename T>
  void convertPrecision(void);

  template<typename T>
  bool apply_palette_clr();

  template<typename T>
  bool execUpsample(void);

  template<typename T>
  void transferDataFrom_T(const Tile* tile_src_data);

  /**
   * Interleave image data and copy to interleaved composite image
   *
   * @param src 	source image
   *
   * @return			true if successful
   */
  template<typename T>
  bool compositeInterleaved(uint16_t srcNumComps, grk_image_comp* srcComps);

  /*#define DEBUG_PROFILE*/
  template<typename T>
  bool applyICC(void);

private:
  template<typename T>
  void scaleComponent(grk_image_comp* component, uint8_t precision);

  /** Copy planar image data to planar composite image
   *
   * @param src 	source image
   *
   * @return			true if successful
   **/
  template<typename T>
  bool compositePlanar(uint16_t srcNumComps, grk_image_comp* srcComps);
  /*--------------------------------------------------------
  Matrix for sYCC, Amendment 1 to IEC 61966-2-1

  Y  |  0.299   0.587    0.114  |    R
  Cb | -0.1687 -0.3312   0.5    | x  G
  Cr |  0.5    -0.4187  -0.0812 |    B

  Inverse:

  R   |1        -3.68213e-05    1.40199     |    Y
  G = |1.00003  -0.344125      -0.714128    | x  Cb - 2^(prec - 1)
  B   |0.999823  1.77204       -8.04142e-06 |    Cr - 2^(prec - 1)

  -----------------------------------------------------------*/
  template<typename T>
  void sycc_to_rgb(T offset, T upb, T y, T cb, T cr, T* out_r, T* out_g, T* out_b);

  template<typename T>
  bool sycc444_to_rgb(void);

  template<typename T>
  bool sycc422_to_rgb(bool oddFirstX);

  template<typename T>
  bool sycc420_to_rgb(bool oddFirstX, bool oddFirstY);

  template<typename T>
  bool color_sycc_to_rgb(bool oddFirstX, bool oddFirstY);

  template<typename T>
  bool color_cmyk_to_rgb(void);

  // assuming unsigned data !
  template<typename T>
  bool color_esycc_to_rgb(void);

  // transform LAB colour space to sRGB @ 16 bit precision
  template<typename T>
  bool cieLabToRGB(void);
};

template<typename T>
bool GrkImage::cieLabToRGB(void)
{
  // sanity checks
  if(numcomps == 0 || !allComponentsSanityCheck(true))
    return false;
  if(numcomps < 3)
  {
    grklog.warn("cieLabToRGB: there must be at least three components");
    return false;
  }
  if(numcomps > 3)
    grklog.warn("cieLabToRGB: there are more than three components : extra components will be "
                "ignored.");
  if(!meta)
    return false;
  size_t i;
  for(i = 1U; i < numcomps; ++i)
  {
    auto comp0 = comps;
    auto compi = comps + i;

    if(comp0->stride != compi->stride)
      break;

    if(comp0->w != compi->w)
      break;

    if(comp0->h != compi->h)
      break;
  }
  if(i != numcomps)
  {
    grklog.warn("cieLabToRGB: all components must have same dimensions, precision and sign");
    return false;
  }

  auto row = (uint32_t*)meta->color.icc_profile_buf;
  auto enumcs = (GRK_ENUM_COLOUR_SPACE)row[0];
  if(enumcs != GRK_ENUM_CLRSPC_CIE)
  { /* CIELab */
    grklog.warn("enumCS %d not handled. Ignoring.", enumcs);
    return false;
  }

  bool defaultType = true;
  color_space = GRK_CLRSPC_SRGB;
  defaultType = row[1] == GRK_DEFAULT_CIELAB_SPACE;
  T *L, *a, *b, *red, *green, *blue;
  // range, offset and precision for L,a and b coordinates
  double r_L, o_L, r_a, o_a, r_b, o_b, prec_L, prec_a, prec_b;
  double minL, maxL, mina, maxa, minb, maxb;
  cmsUInt16Number RGB[3];
  prec_L = (double)comps[0].prec;
  prec_a = (double)comps[1].prec;
  prec_b = (double)comps[2].prec;

  uint32_t illuminant = GRK_CIE_D50;
  if(defaultType)
  { // default Lab space
    r_L = 100;
    r_a = 170;
    r_b = 200;
    o_L = 0;
    o_a = pow(2, prec_a - 1); // 2 ^ (prec_b - 1)
    o_b = 3 * pow(2, prec_b - 3); // 0.75 * 2 ^ (prec_b - 1)
  }
  else
  {
    r_L = row[2];
    r_a = row[4];
    r_b = row[6];
    o_L = row[3];
    o_a = row[5];
    o_b = row[7];
    illuminant = row[8];
  }
  cmsCIExyY WhitePoint;
  switch(illuminant)
  {
    case GRK_CIE_D50:
      break;
    case GRK_CIE_D65:
      cmsWhitePointFromTemp(&WhitePoint, 6504);
      break;
    case GRK_CIE_D75:
      cmsWhitePointFromTemp(&WhitePoint, 7500);
      break;
    case GRK_CIE_SA:
      cmsWhitePointFromTemp(&WhitePoint, 2856);
      break;
    case GRK_CIE_SC:
      cmsWhitePointFromTemp(&WhitePoint, 6774);
      break;
    case GRK_CIE_F2:
      cmsWhitePointFromTemp(&WhitePoint, 4100);
      break;
    case GRK_CIE_F7:
      cmsWhitePointFromTemp(&WhitePoint, 6500);
      break;
    case GRK_CIE_F11:
      cmsWhitePointFromTemp(&WhitePoint, 4000);
      break;
    default:
      grklog.warn("Unrecognized illuminant %d in CIELab colour space. "
                  "Setting to default Daylight50",
                  illuminant);
      illuminant = GRK_CIE_D50;
      break;
  }

  // Lab input profile
  auto in = cmsCreateLab4Profile(illuminant == GRK_CIE_D50 ? nullptr : &WhitePoint);
  // sRGB output profile
  auto out = cmsCreate_sRGBProfile();
  auto transform = cmsCreateTransform(in, TYPE_Lab_DBL, out, TYPE_RGB_16, INTENT_PERCEPTUAL, 0);

  cmsCloseProfile(in);
  cmsCloseProfile(out);
  if(transform == nullptr)
    return false;

  L = (T*)comps[0].data;
  a = (T*)comps[1].data;
  b = (T*)comps[2].data;

  if(!L || !a || !b)
  {
    grklog.warn("color_cielab_to_rgb: null L*a*b component");
    return false;
  }

  auto dest_img = createRGB(3, comps[0].w, comps[0].h, comps[0].prec);
  if(!dest_img)
    return false;

  red = (T*)dest_img->comps[0].data;
  green = (T*)dest_img->comps[1].data;
  blue = (T*)dest_img->comps[2].data;

  uint32_t src_stride_diff = comps[0].stride - comps[0].w;
  uint32_t dest_stride_diff = dest_img->comps[0].stride - dest_img->comps[0].w;

  minL = -(r_L * o_L) / (pow(2, prec_L) - 1);
  maxL = minL + r_L;

  mina = -(r_a * o_a) / (pow(2, prec_a) - 1);
  maxa = mina + r_a;

  minb = -(r_b * o_b) / (pow(2, prec_b) - 1);
  maxb = minb + r_b;

  size_t dest_index = 0;
  for(uint32_t j = 0; j < comps[0].h; ++j)
  {
    for(uint32_t k = 0; k < comps[0].w; ++k)
    {
      cmsCIELab Lab;
      Lab.L = minL + (double)(*L) * (maxL - minL) / (pow(2, prec_L) - 1);
      ++L;
      Lab.a = mina + (double)(*a) * (maxa - mina) / (pow(2, prec_a) - 1);
      ++a;
      Lab.b = minb + (double)(*b) * (maxb - minb) / (pow(2, prec_b) - 1);
      ++b;

      cmsDoTransform(transform, &Lab, RGB, 1);

      red[dest_index] = RGB[0];
      green[dest_index] = RGB[1];
      blue[dest_index] = RGB[2];
      dest_index++;
    }
    dest_index += dest_stride_diff;
    L += src_stride_diff;
    a += src_stride_diff;
    b += src_stride_diff;
  }
  cmsDeleteTransform(transform);

  for(i = 0; i < numcomps; ++i)
    single_component_data_free(comps + i);

  numcomps = 3;
  for(i = 0; i < numcomps; ++i)
  {
    auto srcComp = comps + i;
    auto destComp = dest_img->comps + i;

    srcComp->prec = 16;
    srcComp->stride = destComp->stride;
    srcComp->data = destComp->data;
  }
  // clean up dest image
  setDataToNull(dest_img->comps);
  setDataToNull(dest_img->comps + 1);
  setDataToNull(dest_img->comps + 2);
  grk_unref(dest_img);

  color_space = GRK_CLRSPC_SRGB;

  return true;
}

template<typename T>
void clip(grk_image_comp* component, uint8_t precision)
{
  uint32_t stride_diff = component->stride - component->w;
  assert(precision <= GRK_MAX_SUPPORTED_IMAGE_PRECISION);
  auto data = (T*)component->data;
  size_t index = 0;

  // Define clamping bounds based on type and signedness
  T minimum, maximum;
  if constexpr(std::is_floating_point_v<T>)
  {
    // For floating-point types, use normalized ranges
    if(component->sgnd)
    {
      minimum = -1.0f;
      maximum = 1.0f;
    }
    else
    {
      minimum = 0.0f;
      maximum = 1.0f;
    }
  }
  else
  {
    // For integer types, compute bounds based on precision
    if(component->sgnd)
    {
      minimum = static_cast<T>(-(1LL << (precision - 1)));
      maximum = static_cast<T>((1LL << (precision - 1)) - 1);
    }
    else
    {
      minimum = static_cast<T>(0);
      maximum = static_cast<T>((1ULL << precision) - 1);
    }
  }

  // Clip the data
  for(uint32_t j = 0; j < component->h; ++j)
  {
    for(uint32_t i = 0; i < component->w; ++i)
    {
      data[index] = std::clamp<T>(data[index], minimum, maximum);
      index++;
    }
    index += stride_diff;
  }
  component->prec = precision;
}

template<typename T>
void GrkImage::convertPrecision(void)
{
  if(precision)
  {
    for(uint16_t compno = 0; compno < numcomps; ++compno)
    {
      uint32_t precisionno = compno;
      if(precisionno >= num_precision)
        precisionno = num_precision - 1U;
      uint8_t prec = precision[precisionno].prec;
      auto comp = comps + compno;
      if(prec == 0)
        prec = comp->prec;
      switch(precision[precisionno].mode)
      {
        case GRK_PREC_MODE_CLIP:
          clip<T>(comp, prec);
          break;
        case GRK_PREC_MODE_SCALE:
          scaleComponent<T>(comp, prec);
          break;
        default:
          break;
      }
    }
  }
  if(decompress_fmt == GRK_FMT_JPG)
  {
    uint8_t prec = comps[0].prec;
    if(prec < 8 && numcomps > 1)
    { /* GRAY_ALPHA, RGB, RGB_ALPHA */
      for(uint16_t i = 0; i < numcomps; ++i)
        scaleComponent<T>(comps + i, 8);
    }
    else if((prec > 1) && (prec < 8) && ((prec == 6) || ((prec & 1) == 1)))
    { /* GRAY with non native precision */
      if((prec == 5) || (prec == 6))
        prec = 8;
      else
        prec++;
      for(uint16_t i = 0; i < numcomps; ++i)
        scaleComponent<T>(comps + i, prec);
    }
  }
  else if(decompress_fmt == GRK_FMT_PNG)
  {
    uint16_t nr_comp = numcomps;
    if(nr_comp > 4)
    {
      grklog.warn("PNG: number of components %d is "
                  "greater than 4. Truncating to 4",
                  nr_comp);
      nr_comp = 4;
    }
    uint8_t prec = comps[0].prec;
    if(prec > 8 && prec < 16)
    {
      prec = 16;
    }
    else if(prec < 8 && nr_comp > 1)
    { /* GRAY_ALPHA, RGB, RGB_ALPHA */
      prec = 8;
    }
    else if((prec > 1) && (prec < 8) && ((prec == 6) || ((prec & 1) == 1)))
    { /* GRAY with non native precision */
      if((prec == 5) || (prec == 6))
        prec = 8;
      else
        prec++;
    }
    for(uint16_t i = 0; i < nr_comp; ++i)
      scaleComponent<T>(comps + i, prec);
  }
}

// assuming unsigned data !
template<typename T>
bool GrkImage::color_esycc_to_rgb(void)
{
  T flip_value = (T)((1 << (comps[0].prec - 1)));
  T max_value = (T)((1 << comps[0].prec) - 1);

  if((numcomps < 3) || !allComponentsSanityCheck(true))
    return false;

  uint32_t w = comps[0].w;
  uint32_t h = comps[0].h;

  bool sign1 = comps[1].sgnd;
  bool sign2 = comps[2].sgnd;

  uint32_t stride_diff = comps[0].stride - w;
  size_t dest_index = 0;
  auto yd = (T*)comps[0].data;
  auto bd = (T*)comps[1].data;
  auto rd = (T*)comps[2].data;
  for(uint32_t j = 0; j < h; ++j)
  {
    for(uint32_t i = 0; i < w; ++i)
    {
      T y = yd[dest_index];
      T cb = bd[dest_index];
      T cr = rd[dest_index];

      if(!sign1)
        cb -= flip_value;
      if(!sign2)
        cr -= flip_value;

      T val = (T)(y - 0.0000368 * cb + 1.40199 * cr + 0.5);

      if(val > max_value)
        val = max_value;
      else if(val < 0)
        val = 0;
      yd[dest_index] = val;

      val = (T)(1.0003 * y - 0.344125 * cb - 0.7141128 * cr + 0.5);

      if(val > max_value)
        val = max_value;
      else if(val < 0)
        val = 0;
      bd[dest_index] = val;

      val = (T)(0.999823 * y + 1.77204 * cb - 0.000008 * cr + 0.5);

      if(val > max_value)
        val = max_value;
      else if(val < 0)
        val = 0;
      rd[dest_index] = val;
      dest_index++;
    }
    dest_index += stride_diff;
  }
  color_space = GRK_CLRSPC_SRGB;

  return true;

} /* color_esycc_to_rgb() */

template<typename T>
bool GrkImage::color_cmyk_to_rgb(void)
{
  uint32_t w = comps[0].w;
  uint32_t h = comps[0].h;

  if((numcomps < 4) || !allComponentsSanityCheck(true))
    return false;

  float sC = 1.0F / (float)((1 << comps[0].prec) - 1);
  float sM = 1.0F / (float)((1 << comps[1].prec) - 1);
  float sY = 1.0F / (float)((1 << comps[2].prec) - 1);
  float sK = 1.0F / (float)((1 << comps[3].prec) - 1);

  uint32_t stride_diff = comps[0].stride - w;
  size_t dest_index = 0;
  auto cd = (T*)comps[0].data;
  auto md = (T*)comps[1].data;
  auto yd = (T*)comps[2].data;
  auto kd = (T*)comps[3].data;

  for(uint32_t j = 0; j < h; ++j)
  {
    for(uint32_t i = 0; i < w; ++i)
    {
      /* CMYK values from 0 to 1 */
      float C = (float)(cd[dest_index]) * sC;
      float M = (float)(md[dest_index]) * sM;
      float Y = (float)(yd[dest_index]) * sY;
      float K = (float)(kd[dest_index]) * sK;

      /* Invert all CMYK values */
      C = 1.0F - C;
      M = 1.0F - M;
      Y = 1.0F - Y;
      K = 1.0F - K;

      /* CMYK -> RGB : RGB results from 0 to 255 */
      cd[dest_index] = (T)(255.0F * C * K); /* R */
      md[dest_index] = (T)(255.0F * M * K); /* G */
      yd[dest_index] = (T)(255.0F * Y * K); /* B */
      dest_index++;
    }
    dest_index += stride_diff;
  }

  single_component_data_free(comps + 3);
  comps[0].prec = 8;
  comps[1].prec = 8;
  comps[2].prec = 8;
  numcomps = (uint16_t)(numcomps - 1U);
  color_space = GRK_CLRSPC_SRGB;

  for(uint16_t i = 3; i < numcomps; ++i)
    memcpy(&(comps[i]), &(comps[i + 1]), sizeof(comps[i]));

  return true;

} /* color_cmyk_to_rgb() */

template<typename T>
bool GrkImage::color_sycc_to_rgb(bool oddFirstX, bool oddFirstY)
{
  if(numcomps != 3)
  {
    grklog.warn("color_sycc_to_rgb: number of components %d is not equal to 3."
                " Unable to convert",
                numcomps);
    return false;
  }

  bool rc;

  if((comps[0].dx == 1) && (comps[1].dx == 2) && (comps[2].dx == 2) && (comps[0].dy == 1) &&
     (comps[1].dy == 2) && (comps[2].dy == 2))
  { /* horizontal and vertical sub-sample */
    rc = sycc420_to_rgb<T>(oddFirstX, oddFirstY);
  }
  else if((comps[0].dx == 1) && (comps[1].dx == 2) && (comps[2].dx == 2) && (comps[0].dy == 1) &&
          (comps[1].dy == 1) && (comps[2].dy == 1))
  { /* horizontal sub-sample only */
    rc = sycc422_to_rgb<T>(oddFirstX);
  }
  else if((comps[0].dx == 1) && (comps[1].dx == 1) && (comps[2].dx == 1) && (comps[0].dy == 1) &&
          (comps[1].dy == 1) && (comps[2].dy == 1))
  { /* no sub-sample */
    rc = sycc444_to_rgb<T>();
  }
  else
  {
    grklog.warn("color_sycc_to_rgb:  Invalid sub-sampling: (%d,%d), (%d,%d), (%d,%d)."
                " Unable to convert.",
                comps[0].dx, comps[0].dy, comps[1].dx, comps[1].dy, comps[2].dx, comps[2].dy);
    rc = false;
  }
  if(rc)
    color_space = GRK_CLRSPC_SRGB;

  return rc;

} /* color_sycc_to_rgb() */

template<typename T>
void GrkImage::scaleComponent(grk_image_comp* component, uint8_t precision)
{
  if(component->prec == precision)
    return;
  uint32_t stride_diff = component->stride - component->w;
  auto data = (T*)component->data;
  if(component->prec < precision)
  {
    T scale = 1 << (uint32_t)(precision - component->prec);
    size_t index = 0;
    for(uint32_t j = 0; j < component->h; ++j)
    {
      for(uint32_t i = 0; i < component->w; ++i)
        data[index++] *= scale;
      index += stride_diff;
    }
  }
  else
  {
    T scale = 1 << (uint32_t)(component->prec - precision);
    size_t index = 0;
    for(uint32_t j = 0; j < component->h; ++j)
    {
      for(uint32_t i = 0; i < component->w; ++i)
        data[index++] /= scale;
      index += stride_diff;
    }
  }
  component->prec = precision;
}

/** Copy planar image data to planar composite image
 *
 * @param src 	source image
 *
 * @return			true if successful
 **/
template<typename T>
bool GrkImage::compositePlanar(uint16_t srcNumComps, grk_image_comp* srcComps)
{
  for(uint16_t compno = 0; compno < srcNumComps; compno++)
  {
    auto destComp = comps + compno;
    if(!destComp->data)
      continue;
    Rect32 destWin;
    auto srcComp = srcComps + compno;
    if(!generateCompositeBounds(srcComp, compno, &destWin))
    {
      grklog.warn("GrkImage::compositePlanar: cannot generate composite bounds for component %u",
                  compno);
      continue;
    }
    if(!srcComp->data)
    {
      grklog.warn("GrkImage::compositePlanar: null data for source component %u", compno);
      continue;
    }
    size_t srcIndex = 0;
    auto destIndex = (size_t)destWin.x0 + (size_t)destWin.y0 * destComp->stride;
    size_t destLineOffset = (size_t)destComp->stride - (size_t)destWin.width();
    auto src_ptr = (T*)srcComp->data;
    uint32_t srcLineOffset = srcComp->stride - srcComp->w;
    for(uint32_t j = 0; j < destWin.height(); ++j)
    {
      memcpy((T*)destComp->data + destIndex, src_ptr + srcIndex,
             (size_t)destWin.width() * sizeOfDataType(destComp->data_type));
      destIndex += destLineOffset + destWin.width();
      srcIndex += srcLineOffset + destWin.width();
    }
  }

  return true;
}

/*--------------------------------------------------------
Matrix for sYCC, Amendment 1 to IEC 61966-2-1

Y  |  0.299   0.587    0.114  |    R
Cb | -0.1687 -0.3312   0.5    | x  G
Cr |  0.5    -0.4187  -0.0812 |    B

Inverse:

R   |1        -3.68213e-05    1.40199     |    Y
G = |1.00003  -0.344125      -0.714128    | x  Cb - 2^(prec - 1)
B   |0.999823  1.77204       -8.04142e-06 |    Cr - 2^(prec - 1)

-----------------------------------------------------------*/
template<typename T>
void GrkImage::sycc_to_rgb(T offset, T upb, T y, T cb, T cr, T* out_r, T* out_g, T* out_b)
{
  T r, g, b;

  cb -= offset;
  cr -= offset;
  r = y + (T)(1.402 * cr);
  if(r < 0)
    r = 0;
  else if(r > upb)
    r = upb;
  *out_r = r;

  g = y - (T)(0.344 * cb + 0.714 * cr);
  if(g < 0)
    g = 0;
  else if(g > upb)
    g = upb;
  *out_g = g;

  b = y + (T)(1.772 * cb);
  if(b < 0)
    b = 0;
  else if(b > upb)
    b = upb;
  *out_b = b;
}

template<typename T>
bool GrkImage::sycc444_to_rgb(void)
{
  T *d0, *d1, *d2, *r, *g, *b;
  auto dst = createRGB(3, comps[0].w, comps[0].h, comps[0].prec);
  if(!dst)
    return false;

  T offset = (T)(1 << (comps[0].prec - 1));
  T upb = (T)((1 << comps[0].prec) - 1);

  uint32_t w = comps[0].w;
  uint32_t src_stride_diff = comps[0].stride - w;
  uint32_t dst_stride_diff = dst->comps[0].stride - dst->comps[0].w;
  uint32_t h = comps[0].h;

  auto y = (T*)comps[0].data;
  auto cb = (T*)comps[1].data;
  auto cr = (T*)comps[2].data;

  d0 = r = (T*)dst->comps[0].data;
  d1 = g = (T*)dst->comps[1].data;
  d2 = b = (T*)dst->comps[2].data;

  dst->comps[0].data = nullptr;
  dst->comps[1].data = nullptr;
  dst->comps[2].data = nullptr;

  for(uint32_t j = 0; j < h; ++j)
  {
    for(uint32_t i = 0; i < w; ++i)
      sycc_to_rgb<T>(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
    y += src_stride_diff;
    cb += src_stride_diff;
    cr += src_stride_diff;
    r += dst_stride_diff;
    g += dst_stride_diff;
    b += dst_stride_diff;
  }

  all_components_data_free();
  comps[0].data = d0;
  comps[1].data = d1;
  comps[2].data = d2;
  color_space = GRK_CLRSPC_SRGB;

  for(uint16_t i = 0; i < numcomps; ++i)
  {
    comps[i].stride = dst->comps[i].stride;
    comps[i].owns_data = true;
  }
  grk_unref(dst);

  return true;
} /* sycc444_to_rgb() */

template<typename T>
bool GrkImage::sycc422_to_rgb(bool oddFirstX)
{
  /* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
  uint32_t w = comps[0].w;
  uint32_t h = comps[0].h;
  uint32_t loopWidth = w;
  if(oddFirstX)
    loopWidth--;
  // sanity check
  if((loopWidth + 1) / 2 != comps[1].w)
  {
    grklog.warn("incorrect subsampled width %u", comps[1].w);
    return false;
  }

  auto dst = createRGB(3, w, h, comps[0].prec);
  if(!dst)
    return false;

  T offset = (T)(1 << (comps[0].prec - 1));
  T upb = (T)((1 << comps[0].prec) - 1);

  uint32_t dst_stride_diff = dst->comps[0].stride - dst->comps[0].w;
  uint32_t src_stride_diff = comps[0].stride - w;
  uint32_t src_stride_diff_chroma = comps[1].stride - comps[1].w;

  T *d0, *d1, *d2, *r, *g, *b;

  auto y = (T*)comps[0].data;
  if(!y)
  {
    grklog.warn("sycc422_to_rgb: null luma channel");
    return false;
  }
  auto cb = (T*)comps[1].data;
  auto cr = (T*)comps[2].data;
  if(!cb || !cr)
  {
    grklog.warn("sycc422_to_rgb: null chroma channel");
    return false;
  }

  d0 = r = (T*)dst->comps[0].data;
  d1 = g = (T*)dst->comps[1].data;
  d2 = b = (T*)dst->comps[2].data;

  dst->comps[0].data = nullptr;
  dst->comps[1].data = nullptr;
  dst->comps[2].data = nullptr;

  for(uint32_t i = 0U; i < h; ++i)
  {
    if(oddFirstX)
      sycc_to_rgb<T>(offset, upb, *y++, 0, 0, r++, g++, b++);

    uint32_t j;
    for(j = 0U; j < (loopWidth & ~(size_t)1U); j += 2U)
    {
      sycc_to_rgb<T>(offset, upb, *y++, *cb, *cr, r++, g++, b++);
      sycc_to_rgb<T>(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
    }
    if(j < loopWidth)
      sycc_to_rgb<T>(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);

    y += src_stride_diff;
    cb += src_stride_diff_chroma;
    cr += src_stride_diff_chroma;
    r += dst_stride_diff;
    g += dst_stride_diff;
    b += dst_stride_diff;
  }
  all_components_data_free();

  comps[0].data = d0;
  comps[1].data = d1;
  comps[2].data = d2;

  comps[1].w = comps[2].w = w;
  comps[1].h = comps[2].h = h;
  comps[1].dx = comps[2].dx = comps[0].dx;
  comps[1].dy = comps[2].dy = comps[0].dy;
  color_space = GRK_CLRSPC_SRGB;

  for(uint32_t i = 0; i < numcomps; ++i)
  {
    comps[i].stride = dst->comps[i].stride;
    comps[i].owns_data = true;
  }
  grk_unref(dst);

  return true;

} /* sycc422_to_rgb() */

template<typename T>
bool GrkImage::sycc420_to_rgb(bool oddFirstX, bool oddFirstY)
{
  uint32_t w = comps[0].w;
  uint32_t h = comps[0].h;
  uint32_t loopWidth = w;
  // if img->x0 is odd, then first column shall use Cb/Cr = 0
  // this is handled in the loop below
  if(oddFirstX)
    loopWidth--;
  uint32_t loopHeight = h;
  // if img->y0 is odd, then first line shall use Cb/Cr = 0
  if(oddFirstY)
    loopHeight--;

  // sanity check
  if((loopWidth + 1) / 2 != comps[1].w)
  {
    grklog.warn("incorrect subsampled width %u", comps[1].w);
    return false;
  }
  if((loopHeight + 1) / 2 != comps[1].h)
  {
    grklog.warn("incorrect subsampled height %u", comps[1].h);
    return false;
  }

  auto rgbImg = createRGB(3, w, h, comps[0].prec);
  if(!rgbImg)
    return false;

  T offset = (T)(1 << (comps[0].prec - 1));
  T upb = (T)((1 << comps[0].prec) - 1);

  uint32_t stride_src[3];
  uint32_t stride_src_diff[3];

  uint32_t stride_dest = rgbImg->comps[0].stride;
  uint32_t stride_dest_diff = rgbImg->comps[0].stride - w;

  T* src[3];
  T* dest_ptr[3];
  for(uint32_t i = 0; i < 3; ++i)
  {
    auto srcComp = comps + i;
    src[i] = (T*)srcComp->data;
    stride_src[i] = srcComp->stride;
    stride_src_diff[i] = srcComp->stride - srcComp->w;

    dest_ptr[i] = (T*)rgbImg->comps[i].data;
  }
  // if img->y0 is odd, then first line shall use Cb/Cr = 0
  if(oddFirstY)
  {
    for(size_t j = 0U; j < w; ++j)
      sycc_to_rgb<T>(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
    src[0] += stride_src_diff[0];
    for(uint32_t i = 0; i < 3; ++i)
      dest_ptr[i] += stride_dest_diff;
  }

  size_t i;
  for(i = 0U; i < (loopHeight & ~(size_t)1U); i += 2U)
  {
    auto nextY = src[0] + stride_src[0];
    auto nextRed = dest_ptr[0] + stride_dest;
    auto nextGreen = dest_ptr[1] + stride_dest;
    auto nextBlue = dest_ptr[2] + stride_dest;
    // if img->x0 is odd, then first column shall use Cb/Cr = 0
    if(oddFirstX)
    {
      sycc_to_rgb<T>(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
      sycc_to_rgb<T>(offset, upb, *nextY++, *src[1], *src[2], nextRed++, nextGreen++, nextBlue++);
    }
    uint32_t j;
    for(j = 0U; j < (loopWidth & ~(size_t)1U); j += 2U)
    {
      sycc_to_rgb<T>(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
                     dest_ptr[2]++);
      sycc_to_rgb<T>(offset, upb, *nextY++, *src[1], *src[2], nextRed++, nextGreen++, nextBlue++);

      sycc_to_rgb<T>(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
                     dest_ptr[2]++);
      sycc_to_rgb<T>(offset, upb, *nextY++, *src[1]++, *src[2]++, nextRed++, nextGreen++,
                     nextBlue++);
    }
    if(j < loopWidth)
    {
      sycc_to_rgb<T>(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
                     dest_ptr[2]++);
      sycc_to_rgb<T>(offset, upb, *nextY++, *src[1]++, *src[2]++, nextRed++, nextGreen++,
                     nextBlue++);
    }
    for(uint32_t k = 0; k < 3; ++k)
    {
      dest_ptr[k] += stride_dest_diff + stride_dest;
      src[k] += stride_src_diff[k];
    }
    src[0] += stride_src[0];
  }
  // final odd row has no sub-sampling
  if(i < loopHeight)
  {
    // if img->x0 is odd, then first column shall use Cb/Cr = 0
    if(oddFirstX)
      sycc_to_rgb<T>(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
    uint32_t j;
    for(j = 0U; j < (loopWidth & ~(size_t)1U); j += 2U)
    {
      sycc_to_rgb<T>(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
                     dest_ptr[2]++);
      sycc_to_rgb<T>(offset, upb, *src[0]++, *src[1]++, *src[2]++, dest_ptr[0]++, dest_ptr[1]++,
                     dest_ptr[2]++);
    }
    if(j < loopWidth)
      sycc_to_rgb<T>(offset, upb, *src[0], *src[1], *src[2], dest_ptr[0], dest_ptr[1], dest_ptr[2]);
  }

  all_components_data_free();
  for(uint32_t k = 0; k < 3; ++k)
  {
    comps[k].data = rgbImg->comps[k].data;
    comps[k].stride = rgbImg->comps[k].stride;
    comps[k].owns_data = true;
    rgbImg->comps[k].data = nullptr;
  }
  grk_unref(rgbImg);

  comps[1].w = comps[2].w = comps[0].w;
  comps[1].h = comps[2].h = comps[0].h;
  comps[1].dx = comps[2].dx = comps[0].dx;
  comps[1].dy = comps[2].dy = comps[0].dy;
  color_space = GRK_CLRSPC_SRGB;

  return true;

} /* sycc420_to_rgb() */

template<typename T>
bool GrkImage::apply_palette_clr()
{
  if(palette_applied)
    return true;

  auto clr = &meta->color;
  auto pal = clr->palette;
  auto channel_prec = pal->channel_prec;
  auto channel_sign = pal->channel_sign;
  auto lut = pal->lut;
  auto component_mapping = pal->component_mapping;
  uint16_t num_channels = pal->num_channels;

  // sanity check on component mapping
  for(uint16_t channel = 0; channel < num_channels; ++channel)
  {
    auto mapping = component_mapping + channel;
    uint16_t compno = mapping->component;
    auto comp = comps + compno;
    if(compno >= numcomps)
    {
      grklog.error("apply_palette_clr: component mapping component number %u for channel %u "
                   "must be less than number of image components %u",
                   compno, channel, numcomps);
      return false;
    }
    if(comp->data == nullptr)
    {
      grklog.error("comps[%u].data == nullptr"
                   " in apply_palette_clr().",
                   compno);
      return false;
    }
    if(comp->prec > pal->num_entries)
    {
      grklog.error("Precision %u of component %u is greater than "
                   "number of palette entries %u",
                   compno, comps[compno].prec, pal->num_entries);
      return false;
    }
    uint16_t paletteColumn = mapping->palette_column;
    switch(mapping->mapping_type)
    {
      case 0:
        if(paletteColumn != 0)
        {
          grklog.error("apply_palette_clr: channel %u with direct component mapping: "
                       "non-zero palette column %u not allowed",
                       channel, paletteColumn);
          return false;
        }
        break;
      case 1:
        if(comp->sgnd)
        {
          grklog.error("apply_palette_clr: channel %u with non-direct component mapping: "
                       "cannot be signed",
                       channel);
          return false;
        }
        break;
    }
  }
  auto oldComps = comps;
  auto newComps = new grk_image_comp[num_channels];
  memset(newComps, 0, num_channels * sizeof(grk_image_comp));
  for(uint16_t channel = 0; channel < num_channels; ++channel)
  {
    auto mapping = component_mapping + channel;
    uint16_t compno = mapping->component;
    // Direct mapping
    uint16_t componentIndex = channel;

    if(mapping->mapping_type != 0)
      componentIndex = mapping->palette_column;

    newComps[componentIndex] = oldComps[compno];
    setDataToNull(newComps + componentIndex);

    if(!GrkImage::allocData(newComps + channel))
    {
      while(channel > 0)
      {
        --channel;
        grk_aligned_free(newComps[channel].data);
      }
      delete[] newComps;
      grklog.error("Memory allocation failure in apply_palette_clr().");
      return false;
    }
    newComps[channel].prec = channel_prec[channel];
    newComps[channel].sgnd = channel_sign[channel];
  }
  int32_t top_k = pal->num_entries - 1;
  for(uint16_t channel = 0; channel < num_channels; ++channel)
  {
    /* Palette mapping: */
    auto mapping = component_mapping + channel;
    uint16_t compno = mapping->component;
    auto src = (T*)oldComps[compno].data;
    switch(mapping->mapping_type)
    {
      case 0: {
        size_t num_pixels = (size_t)newComps[channel].stride * newComps[channel].h;
        memcpy(newComps[channel].data, src, num_pixels * sizeof(T));
      }
      break;
      case 1: {
        uint16_t palette_column = mapping->palette_column;
        auto dst = (T*)newComps[palette_column].data;
        uint32_t diff = (uint32_t)(newComps[palette_column].stride - newComps[palette_column].w);
        size_t ind = 0;
        // note: 1 <= n <= 255
        for(uint32_t n = 0; n < newComps[palette_column].h; ++n)
        {
          for(uint32_t m = 0; m < newComps[palette_column].w; ++m)
          {
            int32_t k = 0;
            if((k = src[ind]) < 0)
              k = 0;
            else if(k > top_k)
              k = top_k;
            dst[ind++] = (T)lut[k * num_channels + palette_column];
          }
          ind += diff;
        }
      }
      break;
    }
  }
  for(uint16_t i = 0; i < numcomps; ++i)
    single_component_data_free(oldComps + i);
  delete[] oldComps;
  comps = newComps;
  numcomps = num_channels;
  palette_applied = true;

  return true;
}

template<typename T>
bool GrkImage::execUpsample(void)
{
  if(!upsample)
    return true;

  if(!comps)
    return false;

  grk_image_comp* new_components = nullptr;
  bool upsampleNeeded = false;

  for(uint16_t compno = 0U; compno < numcomps; ++compno)
  {
    if((comps[compno].dx > 1U) || (comps[compno].dy > 1U))
    {
      upsampleNeeded = true;
      break;
    }
  }
  if(!upsampleNeeded)
    return true;

  new_components = new grk_image_comp[numcomps];
  memset(new_components, 0, numcomps * sizeof(grk_image_comp));
  for(uint16_t compno = 0U; compno < numcomps; ++compno)
  {
    auto new_cmp = new_components + compno;
    copyComponent(comps + compno, new_cmp);
    new_cmp->dx = 1;
    new_cmp->dy = 1;
    new_cmp->w = x1 - x0;
    new_cmp->h = y1 - y0;
    if(!allocData(new_cmp))
    {
      delete[] new_components;
      return false;
    }
  }
  for(uint16_t compno = 0U; compno < numcomps; ++compno)
  {
    auto new_cmp = new_components + compno;
    auto org_cmp = comps + compno;
    if((org_cmp->dx > 1U) || (org_cmp->dy > 1U))
    {
      auto src = (T*)org_cmp->data;
      auto dst = (T*)new_cmp->data;

      /* need to take into account dx & dy */
      uint32_t xoff = org_cmp->dx * org_cmp->x0 - x0;
      uint32_t yoff = org_cmp->dy * org_cmp->y0 - y0;
      if((xoff >= org_cmp->dx) || (yoff >= org_cmp->dy))
      {
        grklog.error("upsample: Invalid image/component parameters found when upsampling");
        delete[] new_components;
        return false;
      }

      uint32_t y;
      for(y = 0U; y < yoff; ++y)
      {
        memset(dst, 0U, (size_t)new_cmp->w * sizeof(T));
        dst += new_cmp->stride;
      }

      if(new_cmp->h > (org_cmp->dy - 1U))
      { /* check subtraction overflow for really small images */
        for(; y < new_cmp->h - (org_cmp->dy - 1U); y += org_cmp->dy)
        {
          uint32_t x, dy;
          uint32_t xorg = 0;
          for(x = 0U; x < xoff; ++x)
            dst[x] = 0;

          if(new_cmp->w > (org_cmp->dx - 1U))
          { /* check subtraction overflow for really small images */
            for(; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg)
            {
              for(uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
                dst[x + dx] = src[xorg];
            }
          }
          for(; x < new_cmp->w; ++x)
            dst[x] = src[xorg];
          dst += new_cmp->stride;

          for(dy = 1U; dy < org_cmp->dy; ++dy)
          {
            memcpy(dst, dst - new_cmp->stride, (size_t)new_cmp->w * sizeof(T));
            dst += new_cmp->stride;
          }
          src += org_cmp->stride;
        }
      }
      if(y < new_cmp->h)
      {
        uint32_t x;
        uint32_t xorg = 0;
        for(x = 0U; x < xoff; ++x)
          dst[x] = 0;

        if(new_cmp->w > (org_cmp->dx - 1U))
        { /* check subtraction overflow for really small images */
          for(; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg)
          {
            for(uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
              dst[x + dx] = src[xorg];
          }
        }
        for(; x < new_cmp->w; ++x)
          dst[x] = src[xorg];
        dst += new_cmp->stride;
        ++y;
        for(; y < new_cmp->h; ++y)
        {
          memcpy(dst, dst - new_cmp->stride, (size_t)new_cmp->w * sizeof(T));
          dst += new_cmp->stride;
        }
      }
    }
    else
    {
      memcpy(new_cmp->data, org_cmp->data, (size_t)org_cmp->stride * org_cmp->h * sizeof(T));
    }
  }
  all_components_data_free();
  delete[] comps;
  comps = new_components;

  return true;
}

/**
 * Interleave image data and copy to interleaved composite image
 *
 * @param src 	source image
 *
 * @return			true if successful
 */
template<typename T>
bool GrkImage::compositeInterleaved(uint16_t srcNumComps, grk_image_comp* srcComps)
{
  auto srcComp = srcComps;
  auto destComp = comps;
  Rect32 destWin;
  for(uint16_t i = 0; i < srcNumComps; ++i)
  {
    if(!(srcComps + i)->data)
    {
      grklog.warn("GrkImage::compositeInterleaved: null data for source component %u", i);
      return true;
    }
  }
  if(!generateCompositeBounds(srcComp, 0, &destWin))
  {
    grklog.warn("GrkImage::compositeInterleaved: cannot generate composite bounds");
    return false;
  }
  uint8_t prec = destComp->prec;
  switch(decompress_fmt)
  {
    case GRK_FMT_TIF:
      break;
    case GRK_FMT_PXM:
      prec = prec > 8 ? 16 : 8;
      break;
    default:
      return false;
      break;
  }
  auto destStride = grk::PlanarToInterleaved<T>::getPackedBytes(srcNumComps, destComp->w, prec);
  auto destx0 = grk::PlanarToInterleaved<T>::getPackedBytes(srcNumComps, destWin.x0, prec);
  auto destIndex = (uint64_t)destWin.y0 * destStride + (uint64_t)destx0;
  auto iter = InterleaverFactory<T>::makeInterleaver(
      prec == 16 && decompress_fmt != GRK_FMT_TIF ? packer16BitBE : prec);
  if(!iter)
    return false;
  auto planes = std::make_unique<T*[]>(numcomps);
  for(uint16_t i = 0; i < srcNumComps; ++i)
    planes[i] = (T*)(srcComps + i)->data;
  iter->interleave(const_cast<T**>(planes.get()), srcNumComps, interleaved_data.data + destIndex,
                   destWin.width(), srcComp->stride, destStride, destWin.height(), 0);
  delete iter;

  return true;
}

/*#define DEBUG_PROFILE*/
template<typename T>
bool GrkImage::applyICC(void)
{
  cmsUInt32Number out_space;
  cmsUInt32Number intent = 0;
  cmsHTRANSFORM transform = nullptr;
  cmsHPROFILE in_prof = nullptr;
  cmsHPROFILE out_prof = nullptr;
  cmsUInt32Number in_type, out_type;
  size_t nr_samples, componentSize;
  uint32_t prec, w, stride_diff, h;
  GRK_COLOR_SPACE oldspace;
  bool rc = false;

  if(!validateICC())
    return false;

  if(numcomps == 0 || !allComponentsSanityCheck(true))
    return false;
  if(!meta || !meta->color.icc_profile_buf || !meta->color.icc_profile_len)
    return false;
  in_prof = cmsOpenProfileFromMem(meta->color.icc_profile_buf, meta->color.icc_profile_len);
  if(!in_prof)
    goto cleanup;

  // auto in_space = cmsGetPCS(in_prof);
  out_space = cmsGetColorSpace(in_prof);
  intent = cmsGetHeaderRenderingIntent(in_prof);

  w = comps[0].w;
  stride_diff = comps[0].stride - w;
  h = comps[0].h;
  if(!w || !h)
    goto cleanup;
  componentSize = (size_t)w * h;

  prec = comps[0].prec;
  oldspace = color_space;

  if(out_space == cmsSigRgbData)
  { /* enumCS 16 */
    uint16_t i, nr_comp = numcomps;
    if(nr_comp > 4)
      nr_comp = 4;

    for(i = 1; i < nr_comp; ++i)
    {
      if(comps[0].dx != comps[i].dx)
        break;
      if(comps[0].dy != comps[i].dy)
        break;
      if(comps[0].prec != comps[i].prec)
        break;
      if(comps[0].sgnd != comps[i].sgnd)
        break;
    }
    if(i != nr_comp)
      goto cleanup;

    if(prec <= 8)
    {
      in_type = TYPE_RGB_8;
      out_type = TYPE_RGB_8;
    }
    else
    {
      in_type = TYPE_RGB_16;
      out_type = TYPE_RGB_16;
    }
    out_prof = cmsCreate_sRGBProfile();
    color_space = GRK_CLRSPC_SRGB;
  }
  else if(out_space == cmsSigGrayData)
  { /* enumCS 17 */
    in_type = TYPE_GRAY_8;
    out_type = TYPE_RGB_8;
    out_prof = cmsCreate_sRGBProfile();
    if(force_rgb)
      color_space = GRK_CLRSPC_SRGB;
    else
      color_space = GRK_CLRSPC_GRAY;
  }
  else if(out_space == cmsSigYCbCrData)
  { /* enumCS 18 */
    in_type = TYPE_YCbCr_16;
    out_type = TYPE_RGB_16;
    out_prof = cmsCreate_sRGBProfile();
    color_space = GRK_CLRSPC_SRGB;
  }
  else
  {
    grklog.warn("Apply ICC profile has unknown "
                "output color space (%#x)\nICC profile ignored.",
                out_space);
    goto cleanup;
  }
  transform = cmsCreateTransform(in_prof, in_type, out_prof, out_type, intent, 0);
  if(!transform)
  {
    color_space = oldspace;
    goto cleanup;
  }

  if(numcomps > 2)
  { /* RGB, RGBA */
    if(prec <= 8)
    {
      nr_samples = componentSize * 3U;
      auto inbuf = new uint8_t[nr_samples];
      auto outbuf = new uint8_t[nr_samples];

      auto r = (T*)comps[0].data;
      auto g = (T*)comps[1].data;
      auto b = (T*)comps[2].data;

      size_t src_index = 0;
      size_t dest_index = 0;
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t i = 0; i < w; ++i)
        {
          inbuf[dest_index++] = (uint8_t)r[src_index];
          inbuf[dest_index++] = (uint8_t)g[src_index];
          inbuf[dest_index++] = (uint8_t)b[src_index];
          src_index++;
        }
        src_index += stride_diff;
      }

      if(w > UINT32_MAX / 3)
      {
        grklog.error("Image width of {} converted to sample size 3 will overflow.", w);
        goto cleanup;
      }

      cmsDoTransformLineStride(transform, inbuf, outbuf, w, h, 3 * w, 3 * w, 0, 0);

      src_index = 0;
      dest_index = 0;
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t i = 0; i < w; ++i)
        {
          r[dest_index] = (T)outbuf[src_index++];
          g[dest_index] = (T)outbuf[src_index++];
          b[dest_index] = (T)outbuf[src_index++];
          dest_index++;
        }
        dest_index += stride_diff;
      }
      delete[] inbuf;
      delete[] outbuf;
    }
    else
    {
      nr_samples = componentSize * 3U * sizeof(uint16_t);
      auto inbuf = new uint16_t[nr_samples];
      auto outbuf = new uint16_t[nr_samples];

      auto r = (T*)comps[0].data;
      auto g = (T*)comps[1].data;
      auto b = (T*)comps[2].data;

      size_t src_index = 0;
      size_t dest_index = 0;
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t i = 0; i < w; ++i)
        {
          inbuf[dest_index++] = (uint16_t)r[src_index];
          inbuf[dest_index++] = (uint16_t)g[src_index];
          inbuf[dest_index++] = (uint16_t)b[src_index];
          src_index++;
        }
        src_index += stride_diff;
      }

      if(w > UINT32_MAX / (3 * sizeof(uint16_t)))
      {
        grklog.error("Image width of {} converted to sample size 3 @ 16 bits will overflow.", w);
        goto cleanup;
      }
      cmsDoTransformLineStride(transform, inbuf, outbuf, w, h, 3 * w * sizeof(uint16_t),
                               3 * w * sizeof(uint16_t), 0, 0);
      src_index = 0;
      dest_index = 0;
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t i = 0; i < w; ++i)
        {
          r[dest_index] = (T)outbuf[src_index++];
          g[dest_index] = (T)outbuf[src_index++];
          b[dest_index] = (T)outbuf[src_index++];
          dest_index++;
        }
        dest_index += stride_diff;
      }
      delete[] inbuf;
      delete[] outbuf;
    }
  }
  else
  { /* GRAY, GRAYA */
    nr_samples = componentSize * 3U;
    auto newComps = new grk_image_comp[numcomps + 2U];
    for(uint16_t i = 0; i < numcomps + 2U; ++i)
    {
      if(i < numcomps)
        newComps[i] = comps[i];
      else
        memset(newComps + i, 0, sizeof(grk_image_comp));
    }
    delete[] comps;
    comps = newComps;
    auto inbuf = new uint8_t[nr_samples];
    auto outbuf = new uint8_t[nr_samples];
    if(force_rgb)
    {
      if(numcomps == 2)
        comps[3] = comps[1];
      comps[1] = comps[0];
      setDataToNull(comps + 1);
      allocData(comps + 1);
      comps[2] = comps[0];
      setDataToNull(comps + 2);
      allocData(comps + 2);
      numcomps = (uint16_t)(2 + numcomps);
    }
    auto r = (T*)comps[0].data;
    size_t src_index = 0;
    size_t dest_index = 0;
    for(uint32_t j = 0; j < h; ++j)
    {
      for(uint32_t i = 0; i < w; ++i)
        inbuf[dest_index++] = (uint8_t)r[src_index++];
      src_index += stride_diff;
    }
    if(w > UINT32_MAX / 3)
    {
      grklog.error("Image width of {} converted to sample size 3 will overflow.", w);
      goto cleanup;
    }
    cmsDoTransformLineStride(transform, inbuf, outbuf, w, h, w, 3 * w, 0, 0);
    T *g = nullptr, *b = nullptr;
    if(force_rgb)
    {
      g = (T*)comps[1].data;
      b = (T*)comps[2].data;
    }
    src_index = 0;
    dest_index = 0;
    for(uint32_t j = 0; j < h; ++j)
    {
      for(uint32_t i = 0; i < w; ++i)
      {
        r[dest_index] = (T)outbuf[src_index++];
        if(force_rgb)
        {
          g[dest_index] = (T)outbuf[src_index++];
          b[dest_index] = (T)outbuf[src_index++];
        }
        else
        {
          src_index += 2;
        }
        dest_index++;
      }
      dest_index += stride_diff;
    }
    delete[] inbuf;
    delete[] outbuf;
  } /* if(image->numcomps */
  rc = true;
  delete[] meta->color.icc_profile_buf;
  meta->color.icc_profile_buf = nullptr;
  meta->color.icc_profile_len = 0;
cleanup:
  if(in_prof)
    cmsCloseProfile(in_prof);
  if(out_prof)
    cmsCloseProfile(out_prof);
  if(transform)
    cmsDeleteTransform(transform);

  return rc;
} /* applyICC() */

template<typename T>
bool GrkImage::convertToRGB_T(void)
{
  bool convert = needsConversionToRGB();
  switch(color_space)
  {
    case GRK_CLRSPC_SYCC:
      if(numcomps != 3)
      {
        grklog.error("grk_decompress: YCC: number of components %d "
                     "not equal to 3 ",
                     numcomps);
        return false;
      }
      if(convert)
      {
        bool oddFirstX = x0 & 1;
        bool oddFirstY = y0 & 1;
        // todo: region decode should force region to even REGION x0 and y0 coordinates in order
        // to get correct sycc to rgb conversion. Image x0 and y0 parity shouldn't matter, but
        // rather the REGION x0 and y0 parity.
        // if(!wholeTileDecompress)
        // {
        //   oddFirstX = false;
        //   oddFirstY = false;
        // }
        if(!color_sycc_to_rgb<T>(oddFirstX, oddFirstY))
          grklog.warn("grk_decompress: sYCC to RGB colour conversion failed");
      }
      break;
    case GRK_CLRSPC_EYCC:
      if(numcomps != 3)
      {
        grklog.error("grk_decompress: YCC: number of components %d "
                     "not equal to 3 ",
                     numcomps);
        return false;
      }
      if(convert && !color_esycc_to_rgb<T>())
        grklog.warn("grk_decompress: eYCC to RGB colour conversion failed");
      break;
    case GRK_CLRSPC_CMYK:
      if(numcomps != 4)
      {
        grklog.error("grk_decompress: CMYK: number of components %d "
                     "not equal to 4 ",
                     numcomps);
        return false;
      }
      if(convert && !color_cmyk_to_rgb<T>())
        grklog.warn("grk_decompress: CMYK to RGB colour conversion failed");
      break;
    default:
      break;
  }

  return true;
}

template<typename T>
bool GrkImage::postProcess_T(void)
{
  if(!applyColour())
    return false;
  applyColourManagement();
  if(!convertToRGB_T<T>())
    return false;
  if(!greyToRGB())
    return false;
  convertPrecision<T>();

  return execUpsample<T>();
}

template<typename T>
bool GrkImage::applyColour_T(void)
{
  if(!meta)
    return false;
  if(meta->color.palette)
  {
    /* Part 1, I.5.3.4: Either both or none : */
    if(!meta->color.palette->component_mapping)
      ((GrkImageMeta*)meta)->releaseColorPalatte();
    else if(!apply_palette_clr<T>())
      return false;
  }
  if(meta->color.channel_definition)
    apply_channel_definition();

  return true;
}

} // namespace grk
