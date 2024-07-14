#include <grk_includes.h>

namespace grk
{
/**
 * return false if :
 * 1. any component's data buffer is NULL
 * 2. any component's precision is either 0 or greater than GRK_MAX_SUPPORTED_IMAGE_PRECISION
 * 3. any component's signedness does not match another component's signedness
 * 4. any component's precision does not match another component's precision
 * 5. any component's width,stride or height does not match another component's width,stride or
 * height (if equalPrecision is true)
 *
 */
bool GrkImage::allComponentsSanityCheck(bool equalPrecision)
{
   if(numcomps == 0)
	  return false;
   auto comp0 = comps;

   if(!comp0->data)
   {
	  Logger::logger_.error("component 0 : data is null.");
	  return false;
   }
   if(comp0->prec == 0 || comp0->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
   {
	  Logger::logger_.warn("component 0 precision %d is not supported.", 0, comp0->prec);
	  return false;
   }

   for(uint16_t i = 1U; i < numcomps; ++i)
   {
	  auto compi = comps + i;

	  if(!comp0->data)
	  {
		 Logger::logger_.warn("component %d : data is null.", i);
		 return false;
	  }
	  if(equalPrecision && comp0->prec != compi->prec)
	  {
		 Logger::logger_.warn("precision %d of component %d"
							  " differs from precision %d of component 0.",
							  compi->prec, i, comp0->prec);
		 return false;
	  }
	  if(comp0->sgnd != compi->sgnd)
	  {
		 Logger::logger_.warn("signedness %d of component %d"
							  " differs from signedness %d of component 0.",
							  compi->sgnd, i, comp0->sgnd);
		 return false;
	  }
	  if(comp0->w != compi->w)
	  {
		 Logger::logger_.warn("width %d of component %d"
							  " differs from width %d of component 0.",
							  compi->sgnd, i, comp0->sgnd);
		 return false;
	  }
	  if(comp0->stride != compi->stride)
	  {
		 Logger::logger_.warn("stride %d of component %d"
							  " differs from stride %d of component 0.",
							  compi->sgnd, i, comp0->sgnd);
		 return false;
	  }
	  if(comp0->h != compi->h)
	  {
		 Logger::logger_.warn("height %d of component %d"
							  " differs from height %d of component 0.",
							  compi->sgnd, i, comp0->sgnd);
		 return false;
	  }
   }
   return true;
}

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
		 auto src = org_cmp->data;
		 auto dst = new_cmp->data;

		 /* need to take into account dx & dy */
		 uint32_t xoff = org_cmp->dx * org_cmp->x0 - x0;
		 uint32_t yoff = org_cmp->dy * org_cmp->y0 - y0;
		 if((xoff >= org_cmp->dx) || (yoff >= org_cmp->dy))
		 {
			Logger::logger_.error(
				"upsample: Invalid image/component parameters found when upsampling");
			delete[] new_components;
			return false;
		 }

		 uint32_t y;
		 for(y = 0U; y < yoff; ++y)
		 {
			memset(dst, 0U, new_cmp->w * sizeof(int32_t));
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
				  memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
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
			   memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
			   dst += new_cmp->stride;
			}
		 }
	  }
	  else
	  {
		 memcpy(new_cmp->data, org_cmp->data,
				(size_t)org_cmp->stride * org_cmp->h * sizeof(int32_t));
	  }
   }
   all_components_data_free();
   delete[] comps;
   comps = new_components;

   return true;
}

template<typename T>
void clip(grk_image_comp* component, uint8_t precision)
{
   uint32_t stride_diff = component->stride - component->w;
   assert(precision <= GRK_MAX_SUPPORTED_IMAGE_PRECISION);
   auto data = component->data;
   T maximum = (std::numeric_limits<T>::max)();
   T minimum = (std::numeric_limits<T>::min)();
   size_t index = 0;
   for(uint32_t j = 0; j < component->h; ++j)
   {
	  for(uint32_t i = 0; i < component->w; ++i)
	  {
		 data[index] = (int32_t)std::clamp<T>((T)data[index], minimum, maximum);
		 index++;
	  }
	  index += stride_diff;
   }
   component->prec = precision;
}

void GrkImage::scaleComponent(grk_image_comp* component, uint8_t precision)
{
   if(component->prec == precision)
	  return;
   uint32_t stride_diff = component->stride - component->w;
   auto data = component->data;
   if(component->prec < precision)
   {
	  int32_t scale = 1 << (uint32_t)(precision - component->prec);
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
	  int32_t scale = 1 << (uint32_t)(component->prec - precision);
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
			case GRK_PREC_MODE_CLIP: {
			   if(comp->sgnd)
				  clip<int32_t>(comp, prec);
			   else
				  clip<uint32_t>(comp, prec);
			}
			break;
			case GRK_PREC_MODE_SCALE:
			   scaleComponent(comp, prec);
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
			scaleComponent(comps + i, 8);
	  }
	  else if((prec > 1) && (prec < 8) && ((prec == 6) || ((prec & 1) == 1)))
	  { /* GRAY with non native precision */
		 if((prec == 5) || (prec == 6))
			prec = 8;
		 else
			prec++;
		 for(uint16_t i = 0; i < numcomps; ++i)
			scaleComponent(comps + i, prec);
	  }
   }
   else if(decompress_fmt == GRK_FMT_PNG)
   {
	  uint16_t nr_comp = numcomps;
	  if(nr_comp > 4)
	  {
		 Logger::logger_.warn("PNG: number of components %d is "
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
		 scaleComponent(comps + i, prec);
   }
}

bool GrkImage::greyToRGB(void)
{
   if(numcomps != 1)
	  return true;

   if(!force_rgb || color_space != GRK_CLRSPC_GRAY)
	  return true;

   auto new_components = new grk_image_comp[3];
   memset(new_components, 0, 3 * sizeof(grk_image_comp));
   for(uint16_t i = 0; i < 3; ++i)
   {
	  auto dest = new_components + i;
	  copyComponent(comps, dest);
	  // alloc data for new components
	  if(i > 0)
	  {
		 if(!allocData(dest))
		 {
			delete[] new_components;
			return false;
		 }
		 size_t dataSize = (uint64_t)comps->stride * comps->h * sizeof(uint32_t);
		 memcpy(dest->data, comps->data, dataSize);
	  }
   }

   // attach first new component to old component
   new_components->data = comps->data;
   new_components->stride = comps->stride;
   comps->data = nullptr;
   all_components_data_free();
   delete[] comps;
   comps = new_components;
   numcomps = 3;
   color_space = GRK_CLRSPC_SRGB;

   return true;
}
/***
 * Check if decompress format requires conversion
 */
bool GrkImage::needsConversionToRGB(void)
{
   return (((color_space == GRK_CLRSPC_SYCC || color_space == GRK_CLRSPC_EYCC ||
			 color_space == GRK_CLRSPC_CMYK) &&
			(decompress_fmt != GRK_FMT_UNK && decompress_fmt != GRK_FMT_TIF)) ||
		   force_rgb);
}
bool GrkImage::convertToRGB(bool wholeTileDecompress)
{
   bool oddFirstX = x0 & 1;
   bool oddFirstY = y0 & 1;
   if(!wholeTileDecompress)
   {
	  oddFirstX = false;
	  oddFirstY = false;
   }
   bool convert = needsConversionToRGB();
   switch(color_space)
   {
	  case GRK_CLRSPC_SYCC:
		 if(numcomps != 3)
		 {
			Logger::logger_.error("grk_decompress: YCC: number of components %d "
								  "not equal to 3 ",
								  numcomps);
			return false;
		 }
		 if(convert)
		 {
			if(!color_sycc_to_rgb(oddFirstX, oddFirstY))
			   Logger::logger_.warn("grk_decompress: sYCC to RGB colour conversion failed");
		 }
		 break;
	  case GRK_CLRSPC_EYCC:
		 if(numcomps != 3)
		 {
			Logger::logger_.error("grk_decompress: YCC: number of components %d "
								  "not equal to 3 ",
								  numcomps);
			return false;
		 }
		 if(convert && !color_esycc_to_rgb())
			Logger::logger_.warn("grk_decompress: eYCC to RGB colour conversion failed");
		 break;
	  case GRK_CLRSPC_CMYK:
		 if(numcomps != 4)
		 {
			Logger::logger_.error("grk_decompress: CMYK: number of components %d "
								  "not equal to 4 ",
								  numcomps);
			return false;
		 }
		 if(convert && !color_cmyk_to_rgb())
			Logger::logger_.warn("grk_decompress: CMYK to RGB colour conversion failed");
		 break;
	  default:
		 break;
   }

   return true;
}

grk_image* GrkImage::createRGB(uint16_t numcmpts, uint32_t w, uint32_t h, uint8_t prec)
{
   if(!numcmpts)
   {
	  Logger::logger_.warn("createRGB: number of components cannot be zero.");
	  return nullptr;
   }

   auto cmptparms = new grk_image_comp[numcmpts];
   uint16_t compno = 0U;
   for(compno = 0U; compno < numcmpts; ++compno)
   {
	  memset(cmptparms + compno, 0, sizeof(grk_image_comp));
	  cmptparms[compno].dx = 1;
	  cmptparms[compno].dy = 1;
	  cmptparms[compno].w = w;
	  cmptparms[compno].h = h;
	  cmptparms[compno].x0 = 0U;
	  cmptparms[compno].y0 = 0U;
	  cmptparms[compno].prec = prec;
	  cmptparms[compno].sgnd = 0U;
   }
   auto img = GrkImage::create(this, numcmpts, (grk_image_comp*)cmptparms, GRK_CLRSPC_SRGB, true);
   delete[] cmptparms;

   return img;
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
void GrkImage::sycc_to_rgb(int32_t offset, int32_t upb, int32_t y, int32_t cb, int32_t cr,
						   int32_t* out_r, int32_t* out_g, int32_t* out_b)
{
   int32_t r, g, b;

   cb -= offset;
   cr -= offset;
   r = y + (int32_t)(1.402 * cr);
   if(r < 0)
	  r = 0;
   else if(r > upb)
	  r = upb;
   *out_r = r;

   g = y - (int32_t)(0.344 * cb + 0.714 * cr);
   if(g < 0)
	  g = 0;
   else if(g > upb)
	  g = upb;
   *out_g = g;

   b = y + (int32_t)(1.772 * cb);
   if(b < 0)
	  b = 0;
   else if(b > upb)
	  b = upb;
   *out_b = b;
}

bool GrkImage::sycc444_to_rgb(void)
{
   int32_t *d0, *d1, *d2, *r, *g, *b;
   auto dst = createRGB(3, comps[0].w, comps[0].h, comps[0].prec);
   if(!dst)
	  return false;

   int32_t offset = 1 << (comps[0].prec - 1);
   int32_t upb = (1 << comps[0].prec) - 1;

   uint32_t w = comps[0].w;
   uint32_t src_stride_diff = comps[0].stride - w;
   uint32_t dst_stride_diff = dst->comps[0].stride - dst->comps[0].w;
   uint32_t h = comps[0].h;

   auto y = comps[0].data;
   auto cb = comps[1].data;
   auto cr = comps[2].data;

   d0 = r = dst->comps[0].data;
   d1 = g = dst->comps[1].data;
   d2 = b = dst->comps[2].data;

   dst->comps[0].data = nullptr;
   dst->comps[1].data = nullptr;
   dst->comps[2].data = nullptr;

   for(uint32_t j = 0; j < h; ++j)
   {
	  for(uint32_t i = 0; i < w; ++i)
		 sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
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

   for(uint32_t i = 0; i < numcomps; ++i)
	  comps[i].stride = dst->comps[i].stride;
   grk_object_unref(&dst->obj);

   return true;
} /* sycc444_to_rgb() */

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
	  Logger::logger_.warn("incorrect subsampled width %u", comps[1].w);
	  return false;
   }

   auto dst = createRGB(3, w, h, comps[0].prec);
   if(!dst)
	  return false;

   int32_t offset = 1 << (comps[0].prec - 1);
   int32_t upb = (1 << comps[0].prec) - 1;

   uint32_t dst_stride_diff = dst->comps[0].stride - dst->comps[0].w;
   uint32_t src_stride_diff = comps[0].stride - w;
   uint32_t src_stride_diff_chroma = comps[1].stride - comps[1].w;

   int32_t *d0, *d1, *d2, *r, *g, *b;

   auto y = comps[0].data;
   if(!y)
   {
	  Logger::logger_.warn("sycc422_to_rgb: null luma channel");
	  return false;
   }
   auto cb = comps[1].data;
   auto cr = comps[2].data;
   if(!cb || !cr)
   {
	  Logger::logger_.warn("sycc422_to_rgb: null chroma channel");
	  return false;
   }

   d0 = r = dst->comps[0].data;
   d1 = g = dst->comps[1].data;
   d2 = b = dst->comps[2].data;

   dst->comps[0].data = nullptr;
   dst->comps[1].data = nullptr;
   dst->comps[2].data = nullptr;

   for(uint32_t i = 0U; i < h; ++i)
   {
	  if(oddFirstX)
		 sycc_to_rgb(offset, upb, *y++, 0, 0, r++, g++, b++);

	  uint32_t j;
	  for(j = 0U; j < (loopWidth & ~(size_t)1U); j += 2U)
	  {
		 sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
		 sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
	  }
	  if(j < loopWidth)
		 sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);

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
	  comps[i].stride = dst->comps[i].stride;
   grk_object_unref(&dst->obj);

   return true;

} /* sycc422_to_rgb() */

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
	  Logger::logger_.warn("incorrect subsampled width %u", comps[1].w);
	  return false;
   }
   if((loopHeight + 1) / 2 != comps[1].h)
   {
	  Logger::logger_.warn("incorrect subsampled height %u", comps[1].h);
	  return false;
   }

   auto dst = createRGB(3, w, h, comps[0].prec);
   if(!dst)
	  return false;

   int32_t offset = 1 << (comps[0].prec - 1);
   int32_t upb = (1 << comps[0].prec) - 1;

   int32_t* src[3];
   int32_t* dest[3];
   int32_t* dest_ptr[3];
   uint32_t stride_src[3];
   uint32_t stride_src_diff[3];

   uint32_t stride_dest = dst->comps[0].stride;
   uint32_t stride_dest_diff = dst->comps[0].stride - w;

   for(uint32_t i = 0; i < 3; ++i)
   {
	  auto srcComp = comps + i;
	  src[i] = srcComp->data;
	  stride_src[i] = srcComp->stride;
	  stride_src_diff[i] = srcComp->stride - srcComp->w;

	  dest[i] = dest_ptr[i] = dst->comps[i].data;
	  dst->comps[i].data = nullptr;
   }
   // if img->y0 is odd, then first line shall use Cb/Cr = 0
   if(oddFirstY)
   {
	  for(size_t j = 0U; j < w; ++j)
		 sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
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
		 sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
		 sycc_to_rgb(offset, upb, *nextY++, *src[1], *src[2], nextRed++, nextGreen++, nextBlue++);
	  }
	  uint32_t j;
	  for(j = 0U; j < (loopWidth & ~(size_t)1U); j += 2U)
	  {
		 sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
					 dest_ptr[2]++);
		 sycc_to_rgb(offset, upb, *nextY++, *src[1], *src[2], nextRed++, nextGreen++, nextBlue++);

		 sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
					 dest_ptr[2]++);
		 sycc_to_rgb(offset, upb, *nextY++, *src[1]++, *src[2]++, nextRed++, nextGreen++,
					 nextBlue++);
	  }
	  if(j < loopWidth)
	  {
		 sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
					 dest_ptr[2]++);
		 sycc_to_rgb(offset, upb, *nextY++, *src[1]++, *src[2]++, nextRed++, nextGreen++,
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
		 sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
	  uint32_t j;
	  for(j = 0U; j < (loopWidth & ~(size_t)1U); j += 2U)
	  {
		 sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++,
					 dest_ptr[2]++);
		 sycc_to_rgb(offset, upb, *src[0]++, *src[1]++, *src[2]++, dest_ptr[0]++, dest_ptr[1]++,
					 dest_ptr[2]++);
	  }
	  if(j < loopWidth)
		 sycc_to_rgb(offset, upb, *src[0], *src[1], *src[2], dest_ptr[0], dest_ptr[1], dest_ptr[2]);
   }

   all_components_data_free();
   for(uint32_t k = 0; k < 3; ++k)
   {
	  comps[k].data = dest[k];
	  comps[k].stride = dst->comps[k].stride;
   }
   comps[1].w = comps[2].w = comps[0].w;
   comps[1].h = comps[2].h = comps[0].h;
   comps[1].dx = comps[2].dx = comps[0].dx;
   comps[1].dy = comps[2].dy = comps[0].dy;
   color_space = GRK_CLRSPC_SRGB;
   grk_object_unref(&dst->obj);

   return true;

} /* sycc420_to_rgb() */

bool GrkImage::color_sycc_to_rgb(bool oddFirstX, bool oddFirstY)
{
   if(numcomps != 3)
   {
	  Logger::logger_.warn("color_sycc_to_rgb: number of components %d is not equal to 3."
						   " Unable to convert",
						   numcomps);
	  return false;
   }

   bool rc;

   if((comps[0].dx == 1) && (comps[1].dx == 2) && (comps[2].dx == 2) && (comps[0].dy == 1) &&
	  (comps[1].dy == 2) && (comps[2].dy == 2))
   { /* horizontal and vertical sub-sample */
	  rc = sycc420_to_rgb(oddFirstX, oddFirstY);
   }
   else if((comps[0].dx == 1) && (comps[1].dx == 2) && (comps[2].dx == 2) && (comps[0].dy == 1) &&
		   (comps[1].dy == 1) && (comps[2].dy == 1))
   { /* horizontal sub-sample only */
	  rc = sycc422_to_rgb(oddFirstX);
   }
   else if((comps[0].dx == 1) && (comps[1].dx == 1) && (comps[2].dx == 1) && (comps[0].dy == 1) &&
		   (comps[1].dy == 1) && (comps[2].dy == 1))
   { /* no sub-sample */
	  rc = sycc444_to_rgb();
   }
   else
   {
	  Logger::logger_.warn("color_sycc_to_rgb:  Invalid sub-sampling: (%d,%d), (%d,%d), (%d,%d)."
						   " Unable to convert.",
						   comps[0].dx, comps[0].dy, comps[1].dx, comps[1].dy, comps[2].dx,
						   comps[2].dy);
	  rc = false;
   }
   if(rc)
	  color_space = GRK_CLRSPC_SRGB;

   return rc;

} /* color_sycc_to_rgb() */

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
   for(uint32_t j = 0; j < h; ++j)
   {
	  for(uint32_t i = 0; i < w; ++i)
	  {
		 /* CMYK values from 0 to 1 */
		 float C = (float)(comps[0].data[dest_index]) * sC;
		 float M = (float)(comps[1].data[dest_index]) * sM;
		 float Y = (float)(comps[2].data[dest_index]) * sY;
		 float K = (float)(comps[3].data[dest_index]) * sK;

		 /* Invert all CMYK values */
		 C = 1.0F - C;
		 M = 1.0F - M;
		 Y = 1.0F - Y;
		 K = 1.0F - K;

		 /* CMYK -> RGB : RGB results from 0 to 255 */
		 comps[0].data[dest_index] = (int32_t)(255.0F * C * K); /* R */
		 comps[1].data[dest_index] = (int32_t)(255.0F * M * K); /* G */
		 comps[2].data[dest_index] = (int32_t)(255.0F * Y * K); /* B */
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

   for(uint32_t i = 3; i < numcomps; ++i)
	  memcpy(&(comps[i]), &(comps[i + 1]), sizeof(comps[i]));

   return true;

} /* color_cmyk_to_rgb() */

// assuming unsigned data !
bool GrkImage::color_esycc_to_rgb(void)
{
   int32_t flip_value = (1 << (comps[0].prec - 1));
   int32_t max_value = (1 << comps[0].prec) - 1;

   if((numcomps < 3) || !allComponentsSanityCheck(true))
	  return false;

   uint32_t w = comps[0].w;
   uint32_t h = comps[0].h;

   bool sign1 = comps[1].sgnd;
   bool sign2 = comps[2].sgnd;

   uint32_t stride_diff = comps[0].stride - w;
   size_t dest_index = 0;
   for(uint32_t j = 0; j < h; ++j)
   {
	  for(uint32_t i = 0; i < w; ++i)
	  {
		 int32_t y = comps[0].data[dest_index];
		 int32_t cb = comps[1].data[dest_index];
		 int32_t cr = comps[2].data[dest_index];

		 if(!sign1)
			cb -= flip_value;
		 if(!sign2)
			cr -= flip_value;

		 int32_t val = (int32_t)(y - 0.0000368 * cb + 1.40199 * cr + 0.5);

		 if(val > max_value)
			val = max_value;
		 else if(val < 0)
			val = 0;
		 comps[0].data[dest_index] = val;

		 val = (int32_t)(1.0003 * y - 0.344125 * cb - 0.7141128 * cr + 0.5);

		 if(val > max_value)
			val = max_value;
		 else if(val < 0)
			val = 0;
		 comps[1].data[dest_index] = val;

		 val = (int32_t)(0.999823 * y + 1.77204 * cb - 0.000008 * cr + 0.5);

		 if(val > max_value)
			val = max_value;
		 else if(val < 0)
			val = 0;
		 comps[2].data[dest_index] = val;
		 dest_index++;
	  }
	  dest_index += stride_diff;
   }
   color_space = GRK_CLRSPC_SRGB;

   return true;

} /* color_esycc_to_rgb() */
std::string GrkImage::getColourSpaceString(void)
{
   std::string rc = "";
   switch(color_space)
   {
	  case GRK_CLRSPC_UNKNOWN:
		 rc = "unknown";
		 break;
	  case GRK_CLRSPC_SRGB:
		 rc = "sRGB";
		 break;
	  case GRK_CLRSPC_GRAY:
		 rc = "grayscale";
		 break;
	  case GRK_CLRSPC_SYCC:
		 rc = "SYCC";
		 break;
	  case GRK_CLRSPC_EYCC:
		 rc = "EYCC";
		 break;
	  case GRK_CLRSPC_CMYK:
		 rc = "CMYK";
		 break;
	  case GRK_CLRSPC_DEFAULT_CIE:
		 rc = "CIE";
		 break;
	  case GRK_CLRSPC_CUSTOM_CIE:
		 rc = "custom CIE";
		 break;
	  case GRK_CLRSPC_ICC:
		 rc = "ICC";
		 break;
   }

   return rc;
}
std::string GrkImage::getICCColourSpaceString(cmsColorSpaceSignature color_space)
{
   std::string rc = "";
   switch(color_space)
   {
	  case cmsSigLabData:
		 rc = "LAB";
		 break;
	  case cmsSigYCbCrData:
		 rc = "YCbCr";
		 break;
	  case cmsSigRgbData:
		 rc = "sRGB";
		 break;
	  case cmsSigGrayData:
		 rc = "grayscale";
		 break;
	  case cmsSigCmykData:
		 rc = "CMYK";
		 break;
	  default:
		 rc = "Unsupported";
		 break;
   }

   return rc;
}
bool GrkImage::isValidICCColourSpace(uint32_t signature)
{
   return (signature == cmsSigXYZData) || (signature == cmsSigLabData) ||
		  (signature == cmsSigLuvData) || (signature == cmsSigYCbCrData) ||
		  (signature == cmsSigYxyData) || (signature == cmsSigRgbData) ||
		  (signature == cmsSigGrayData) || (signature == cmsSigHsvData) ||
		  (signature == cmsSigHlsData) || (signature == cmsSigCmykData) ||
		  (signature == cmsSigCmyData) || (signature == cmsSigMCH1Data) ||
		  (signature == cmsSigMCH2Data) || (signature == cmsSigMCH3Data) ||
		  (signature == cmsSigMCH4Data) || (signature == cmsSigMCH5Data) ||
		  (signature == cmsSigMCH6Data) || (signature == cmsSigMCH7Data) ||
		  (signature == cmsSigMCH8Data) || (signature == cmsSigMCH9Data) ||
		  (signature == cmsSigMCHAData) || (signature == cmsSigMCHBData) ||
		  (signature == cmsSigMCHCData) || (signature == cmsSigMCHDData) ||
		  (signature == cmsSigMCHEData) || (signature == cmsSigMCHFData) ||
		  (signature == cmsSigNamedData) || (signature == cmsSig1colorData) ||
		  (signature == cmsSig2colorData) || (signature == cmsSig3colorData) ||
		  (signature == cmsSig4colorData) || (signature == cmsSig5colorData) ||
		  (signature == cmsSig6colorData) || (signature == cmsSig7colorData) ||
		  (signature == cmsSig8colorData) || (signature == cmsSig9colorData) ||
		  (signature == cmsSig10colorData) || (signature == cmsSig11colorData) ||
		  (signature == cmsSig12colorData) || (signature == cmsSig13colorData) ||
		  (signature == cmsSig14colorData) || (signature == cmsSig15colorData) ||
		  (signature == cmsSigLuvKData);
}
bool GrkImage::validateICC(void)
{
   if(!meta || !meta->color.icc_profile_buf)
	  return false;

   // check if already validated
   if(color_space == GRK_CLRSPC_ICC)
	  return true;

   // image colour space matches ICC colour space
   bool imageColourSpaceMatchesICCColourSpace = false;

   // image properties such as subsampling and number of components
   // are consistent with ICC colour space
   bool imagePropertiesMatchICCColourSpace = false;

   // colour space conversion is supported by the library
   // for this ICC colour space
   bool supportedICCColourSpace = false;

   uint32_t iccColourSpace = 0;
   auto in_prof = cmsOpenProfileFromMem(meta->color.icc_profile_buf, meta->color.icc_profile_len);
   if(in_prof)
   {
	  iccColourSpace = cmsGetColorSpace(in_prof);
	  if(!isValidICCColourSpace(iccColourSpace))
	  {
		 Logger::logger_.warn("Invalid ICC colour space 0x%x. Ignoring", iccColourSpace);
		 cmsCloseProfile(in_prof);

		 return false;
	  }
	  switch(iccColourSpace)
	  {
		 case cmsSigLabData:
			imageColourSpaceMatchesICCColourSpace =
				(color_space == GRK_CLRSPC_DEFAULT_CIE || color_space == GRK_CLRSPC_CUSTOM_CIE);
			imagePropertiesMatchICCColourSpace = numcomps >= 3;
			break;
		 case cmsSigYCbCrData:
			imageColourSpaceMatchesICCColourSpace =
				(color_space == GRK_CLRSPC_SYCC || color_space == GRK_CLRSPC_EYCC);
			if(numcomps < 3)
			   imagePropertiesMatchICCColourSpace = false;
			else
			{
			   auto compLuma = comps;
			   imagePropertiesMatchICCColourSpace =
				   compLuma->dx == 1 && compLuma->dy == 1 && isSubsampled();
			}
			break;
		 case cmsSigRgbData:
			imageColourSpaceMatchesICCColourSpace = color_space == GRK_CLRSPC_SRGB;
			imagePropertiesMatchICCColourSpace = numcomps >= 3 && !isSubsampled();
			supportedICCColourSpace = true;
			break;
		 case cmsSigGrayData:
			imageColourSpaceMatchesICCColourSpace = color_space == GRK_CLRSPC_GRAY;
			imagePropertiesMatchICCColourSpace = numcomps <= 2;
			supportedICCColourSpace = true;
			break;
		 case cmsSigCmykData:
			imageColourSpaceMatchesICCColourSpace = color_space == GRK_CLRSPC_CMYK;
			imagePropertiesMatchICCColourSpace = numcomps == 4 && !isSubsampled();
			break;
		 default:
			break;
	  }
	  cmsCloseProfile(in_prof);
   }
   else
   {
	  Logger::logger_.warn("Unable to parse ICC profile. Ignoring");
	  return false;
   }
   if(!supportedICCColourSpace)
   {
	  Logger::logger_.warn("Unsupported ICC colour space %s. Ignoring",
						   getICCColourSpaceString((cmsColorSpaceSignature)iccColourSpace).c_str());
	  return false;
   }
   if(color_space != GRK_CLRSPC_UNKNOWN && !imageColourSpaceMatchesICCColourSpace)
   {
	  Logger::logger_.warn("Signaled colour space %s doesn't match ICC colour space %s. Ignoring",
						   getColourSpaceString().c_str(),
						   getICCColourSpaceString((cmsColorSpaceSignature)iccColourSpace).c_str());
	  return false;
   }
   if(!imagePropertiesMatchICCColourSpace)
	  Logger::logger_.warn(
		  "Image subsampling / number of components do not match ICC colour space %s. Ignoring",
		  getICCColourSpaceString((cmsColorSpaceSignature)iccColourSpace).c_str());

   if(imagePropertiesMatchICCColourSpace)
	  color_space = GRK_CLRSPC_ICC;

   return imagePropertiesMatchICCColourSpace;
}

/**
 * Convert to sRGB
 */
bool GrkImage::applyColourManagement(void)
{
   if(!meta || !meta->color.icc_profile_buf)
	  return true;

   bool isTiff = decompress_fmt == GRK_FMT_TIF;
   bool canStoreCIE = isTiff && color_space == GRK_CLRSPC_DEFAULT_CIE;
   bool isCIE = color_space == GRK_CLRSPC_DEFAULT_CIE || color_space == GRK_CLRSPC_CUSTOM_CIE;
   // A TIFF,PNG, BMP or JPEG image can store the ICC profile,
   // so no need to apply it in this case,
   // (unless we are forcing to RGB).
   // Otherwise, we apply the profile
   bool canStoreICC = (decompress_fmt == GRK_FMT_TIF || decompress_fmt == GRK_FMT_PNG ||
					   decompress_fmt == GRK_FMT_JPG || decompress_fmt == GRK_FMT_BMP);

   bool shouldApplyColourManagement =
	   force_rgb || (decompress_fmt != GRK_FMT_UNK && meta && meta->color.icc_profile_buf &&
					((isCIE && !canStoreCIE) || !canStoreICC));
   if(!shouldApplyColourManagement)
	  return true;

   if(isCIE)
   {
	  if(!force_rgb)
		 Logger::logger_.warn(
			 " Input image is in CIE colour space,\n"
			 "but the codec is unable to store this information in the "
			 "output file .\n"
			 "The output image will therefore be converted to sRGB before saving.");
	  if(!cieLabToRGB())
	  {
		 Logger::logger_.error("Unable to convert L*a*b image to sRGB");
		 return false;
	  }
   }
   else
   {
	  if(validateICC())
	  {
		 if(!force_rgb)
		 {
			Logger::logger_.warn("");
			Logger::logger_.warn("The input image contains an ICC profile");
			Logger::logger_.warn("but the codec is unable to store this profile"
								 " in the output file.");
			Logger::logger_.warn("The profile will therefore be applied to the output"
								 " image before saving.");
			Logger::logger_.warn("");
		 }
		 if(!applyICC())
		 {
			Logger::logger_.warn("Unable to apply ICC profile");
			return false;
		 }
	  }
   }

   return true;
}

/*#define DEBUG_PROFILE*/
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
	  uint32_t i, nr_comp = numcomps;
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
	  Logger::logger_.warn("Apply ICC profile has unknown "
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

		 auto r = comps[0].data;
		 auto g = comps[1].data;
		 auto b = comps[2].data;

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

		 cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)componentSize);

		 src_index = 0;
		 dest_index = 0;
		 for(uint32_t j = 0; j < h; ++j)
		 {
			for(uint32_t i = 0; i < w; ++i)
			{
			   r[dest_index] = (int32_t)outbuf[src_index++];
			   g[dest_index] = (int32_t)outbuf[src_index++];
			   b[dest_index] = (int32_t)outbuf[src_index++];
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

		 auto r = comps[0].data;
		 auto g = comps[1].data;
		 auto b = comps[2].data;

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
		 cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)componentSize);
		 src_index = 0;
		 dest_index = 0;
		 for(uint32_t j = 0; j < h; ++j)
		 {
			for(uint32_t i = 0; i < w; ++i)
			{
			   r[dest_index] = (int32_t)outbuf[src_index++];
			   g[dest_index] = (int32_t)outbuf[src_index++];
			   b[dest_index] = (int32_t)outbuf[src_index++];
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
	  for(uint32_t i = 0; i < numcomps + 2U; ++i)
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
		 comps[1].data = nullptr;
		 allocData(comps + 1);
		 comps[2] = comps[0];
		 comps[2].data = nullptr;
		 allocData(comps + 2);
		 numcomps = (uint16_t)(2 + numcomps);
	  }
	  auto r = comps[0].data;
	  size_t src_index = 0;
	  size_t dest_index = 0;
	  for(uint32_t j = 0; j < h; ++j)
	  {
		 for(uint32_t i = 0; i < w; ++i)
			inbuf[dest_index++] = (uint8_t)r[src_index++];
		 src_index += stride_diff;
	  }
	  cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)componentSize);
	  int32_t *g = nullptr, *b = nullptr;
	  if(force_rgb)
	  {
		 g = comps[1].data;
		 b = comps[2].data;
	  }
	  src_index = 0;
	  dest_index = 0;
	  for(uint32_t j = 0; j < h; ++j)
	  {
		 for(uint32_t i = 0; i < w; ++i)
		 {
			r[dest_index] = (int32_t)outbuf[src_index++];
			if(force_rgb)
			{
			   g[dest_index] = (int32_t)outbuf[src_index++];
			   b[dest_index] = (int32_t)outbuf[src_index++];
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

// transform LAB colour space to sRGB @ 16 bit precision
bool GrkImage::cieLabToRGB(void)
{
   // sanity checks
   if(numcomps == 0 || !allComponentsSanityCheck(true))
	  return false;
   if(numcomps < 3)
   {
	  Logger::logger_.warn("cieLabToRGB: there must be at least three components");
	  return false;
   }
   if(numcomps > 3)
	  Logger::logger_.warn(
		  "cieLabToRGB: there are more than three components : extra components will be "
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
	  Logger::logger_.warn(
		  "cieLabToRGB: all components must have same dimensions, precision and sign");
	  return false;
   }

   auto row = (uint32_t*)meta->color.icc_profile_buf;
   auto enumcs = (GRK_ENUM_COLOUR_SPACE)row[0];
   if(enumcs != GRK_ENUM_CLRSPC_CIE)
   { /* CIELab */
	  Logger::logger_.warn("enumCS %d not handled. Ignoring.", enumcs);
	  return false;
   }

   bool defaultType = true;
   color_space = GRK_CLRSPC_SRGB;
   defaultType = row[1] == GRK_DEFAULT_CIELAB_SPACE;
   int32_t *L, *a, *b, *red, *green, *blue;
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
		 Logger::logger_.warn("Unrecognized illuminant %d in CIELab colour space. "
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

   L = comps[0].data;
   a = comps[1].data;
   b = comps[2].data;

   if(!L || !a || !b)
   {
	  Logger::logger_.warn("color_cielab_to_rgb: null L*a*b component");
	  return false;
   }

   auto dest_img = createRGB(3, comps[0].w, comps[0].h, comps[0].prec);
   if(!dest_img)
	  return false;

   red = dest_img->comps[0].data;
   green = dest_img->comps[1].data;
   blue = dest_img->comps[2].data;

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
   dest_img->comps[0].data = nullptr;
   dest_img->comps[1].data = nullptr;
   dest_img->comps[2].data = nullptr;
   grk_object_unref(&dest_img->obj);

   color_space = GRK_CLRSPC_SRGB;

   return true;
}

} // namespace grk
