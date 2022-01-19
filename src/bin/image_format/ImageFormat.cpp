/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
 */

#include "ImageFormat.h"
#include "convert.h"
#include <algorithm>
#include "common.h"
#include "FileStreamIO.h"
#include <lcms2.h>

ImageFormat::ImageFormat()
	: image_(nullptr), rowCount_(0), numStrips_(0),
	  fileIO_(new FileStreamIO()), fileStream_(nullptr),
	  fileName_(""), compressionLevel_(GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT),
	  useStdIO_(false),
	  encodeState(IMAGE_FORMAT_UNENCODED),
	  stripCount(0),
	  num_reclaimed_(0)
{}

ImageFormat& ImageFormat::operator=(const ImageFormat& rhs)
{
	if(this != &rhs)
	{ // self-assignment check expected
		image_ = rhs.image_;
		rowCount_ = rhs.rowCount_;
		numStrips_ = rhs.numStrips_;
		fileIO_ = nullptr;
		fileStream_ = nullptr;
		fileName_ = "";
		compressionLevel_ = rhs.compressionLevel_;
		useStdIO_ = rhs.useStdIO_;
	}
	return *this;
}

ImageFormat::~ImageFormat()
{
	delete fileIO_;
}

uint32_t ImageFormat::getEncodeState(void){
	return encodeState;
}
bool ImageFormat::openFile(void){
	bool rc = fileIO_->open(fileName_, "w");
	if (rc)
		fileStream_ = ((FileStreamIO*)fileIO_)->getFileStream();

	return rc;
}
bool ImageFormat::encodeInit(grk_image* image,
							const std::string& filename,
							uint32_t compressionLevel) {
	compressionLevel_ = compressionLevel;
	fileName_ = filename;
	image_ = image;
	useStdIO_ = grk::useStdio(fileName_);

	return true;
}
bool ImageFormat::encodePixels(grk_serialize_buf pixels,
							grk_serialize_buf* reclaimed,
							uint32_t max_reclaimed,
							uint32_t *num_reclaimed){
	(void)pixels;
	(void)reclaimed;
	(void)max_reclaimed;
	(void)num_reclaimed;

	return false;
}

bool ImageFormat::encodeFinish(void)
{
	bool rc = fileIO_->close();
	delete fileIO_;
	fileIO_ = nullptr;
	fileStream_ = nullptr;
	fileName_ = "";

	return rc;
}

bool ImageFormat::isHeaderEncoded(void){
	return ( (encodeState & IMAGE_FORMAT_ENCODED_HEADER) == IMAGE_FORMAT_ENCODED_HEADER );
}
bool ImageFormat::open(std::string fileName, std::string mode)
{
	return fileIO_->open(fileName, mode);
}
bool ImageFormat::write(GrkSerializeBuf buffer)
{
	bool rc =  fileIO_->write(buffer,reclaimed_,reclaimSize,&num_reclaimed_);
#ifdef GROK_HAVE_URING
	for (uint32_t i = 0; i < num_reclaimed_; ++i)
		pool.put(GrkSerializeBuf(reclaimed_[i]));
#else
	if (buffer.pooled)
		pool.put(buffer);
#endif
	num_reclaimed_ = 0;
	return rc;

}
bool ImageFormat::read(uint8_t* buf, size_t len)
{
	return fileIO_->read(buf, len);
}

bool ImageFormat::seek(int64_t pos)
{
	return fileIO_->seek(pos);
}

uint32_t ImageFormat::maxY(uint32_t rows)
{
	return std::min<uint32_t>(rowCount_ + rows, image_->comps[0].h);
}

uint8_t ImageFormat::getImagePrec(void){
	if (!image_)
		return 0;
	return image_->precision ? image_->precision->prec : image_->comps[0].prec;
}
uint16_t ImageFormat::getImageNumComps(void){
	if (!image_)
		return 0;
	if (image_->meta && image_->meta->color.palette)
		return image_->meta->color.palette->num_channels;

	return (image_->forceRGB  && image_->numcomps < 3) ? 3 : image_->numcomps;
}
GRK_COLOR_SPACE ImageFormat::getImageColourSpace(void){
	if (!image_)
		return GRK_CLRSPC_UNKNOWN;
	return image_->forceRGB ? GRK_CLRSPC_SRGB : image_->color_space;
}

void ImageFormat::scaleComponent(grk_image_comp* component, uint8_t precision)
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
			{
				data[index] = data[index] * scale;
				index++;
			}
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
			{
				data[index] = data[index] / scale;
				index++;
			}
			index += stride_diff;
		}
	}
	component->prec = precision;
}

void ImageFormat::allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries)
{
	assert(color);
	assert(num_channels);
	assert(num_entries);

	auto jp2_pclr = new grk_palette_data();
	jp2_pclr->channel_sign = new bool[num_channels];
	jp2_pclr->channel_prec = new uint8_t[num_channels];
	jp2_pclr->lut = new int32_t[num_channels * num_entries];
	jp2_pclr->num_entries = num_entries;
	jp2_pclr->num_channels = num_channels;
	jp2_pclr->component_mapping = nullptr;
	color->palette = jp2_pclr;
}

void ImageFormat::copy_icc(grk_image* dest, uint8_t* iccbuf, uint32_t icclen)
{
	create_meta(dest);
	dest->meta->color.icc_profile_buf = new uint8_t[icclen];
	memcpy(dest->meta->color.icc_profile_buf, iccbuf, icclen);
	dest->meta->color.icc_profile_len = icclen;
	dest->color_space = GRK_CLRSPC_ICC;
}
void ImageFormat::create_meta(grk_image* img)
{
	if(img && !img->meta)
		img->meta = grk_image_meta_new();
}

bool ImageFormat::validate_icc(GRK_COLOR_SPACE colourSpace, uint8_t* iccbuf, uint32_t icclen)
{
	bool rc = true;
	auto in_prof = cmsOpenProfileFromMem(iccbuf, icclen);
	if(in_prof)
	{
		auto cmsColorSpaceSignature = cmsGetColorSpace(in_prof);
		switch(cmsColorSpaceSignature)
		{
			case cmsSigLabData:
				rc =
					(colourSpace == GRK_CLRSPC_DEFAULT_CIE || colourSpace == GRK_CLRSPC_CUSTOM_CIE);
				break;
			case cmsSigYCbCrData:
				rc = (colourSpace == GRK_CLRSPC_SYCC || colourSpace == GRK_CLRSPC_EYCC);
				break;
			case cmsSigRgbData:
				rc = colourSpace == GRK_CLRSPC_SRGB;
				break;
			case cmsSigGrayData:
				rc = colourSpace == GRK_CLRSPC_GRAY;
				break;
			case cmsSigCmykData:
				rc = colourSpace == GRK_CLRSPC_CMYK;
				break;
			default:
				rc = false;
				break;
		}
		cmsCloseProfile(in_prof);
	}

	return rc;
}

/**
 * return false if :
 * 1. any component's precision is either 0 or greater than GRK_MAX_SUPPORTED_IMAGE_PRECISION
 * 2. any component's signedness does not match another component's signedness
 * 3. any component's precision does not match another component's precision
 *    (if equalPrecision is true)
 *
 */
bool ImageFormat::allComponentsSanityCheck(grk_image* image, bool checkEqualPrecision)
{
	assert(image);
	if(image->numcomps == 0)
		return false;
	if (image_->precision)
		checkEqualPrecision = false;
	auto comp0 = image->comps;
	if(comp0->prec == 0 || comp0->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
	{
		spdlog::warn("component 0 precision {} is not supported.", 0, comp0->prec);
		return false;
	}

	for(uint16_t i = 1U; i < image->numcomps; ++i)
	{
		auto comp_i = image->comps + i;
		if(checkEqualPrecision && comp0->prec != comp_i->prec)
		{
			spdlog::warn("precision {} of component {}"
						 " differs from precision {} of component 0.",
						 comp_i->prec, i, comp0->prec);
			return false;
		}
		if(comp0->sgnd != comp_i->sgnd)
		{
			spdlog::warn("signedness {} of component {}"
						 " differs from signedness {} of component 0.",
						 comp_i->sgnd, i, comp0->sgnd);
			return false;
		}
	}
	return true;
}

bool ImageFormat::areAllComponentsSameSubsampling(grk_image* image)
{
	assert(image);
	if(image->numcomps == 1)
		return true;
	if (image->upsample || image->forceRGB)
		return true;
	auto comp0 = image->comps;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		auto comp = image->comps + i;
		if(comp->dx != comp0->dx || comp->dy != comp0->dy)
		{
			spdlog::error("Not all components have same sub-sampling");
			return false;
		}
	}
	return true;
}

bool ImageFormat::isFinalOutputSubsampled(grk_image* image)
{
	assert(image);
	if (image->upsample || image->forceRGB)
		return false;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		if(image->comps[i].dx != 1 || image->comps[i].dy != 1)
			return true;
	}
	return false;
}

bool ImageFormat::isChromaSubsampled(grk_image* image)
{
	assert(image);
	if(image->numcomps < 3 || image->forceRGB || image->upsample)
		return false;
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		auto comp = image->comps + i;
		switch(i)
		{
			case 1:
			case 2:
				if(comp->type != GRK_COMPONENT_TYPE_COLOUR)
					return false;
				break;
			default:
				if(comp->dx != 1 || comp->dy != 1)
					return false;
				break;
		}
	}
	auto compB = image->comps + 1;
	auto compR = image->comps + 2;

	return (compB->dx == compR->dx && compB->dy == compR->dy);
}


