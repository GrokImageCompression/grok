/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include <cstdio>
#include <cstdlib>
#include "grk_apps_config.h"
#include "grok.h"
#include "TIFFFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"

#ifndef GROK_HAVE_LIBTIFF
# error GROK_HAVE_LIBTIFF_NOT_DEFINED
#endif /* GROK_HAVE_LIBTIFF */

#include <tiffio.h>
#include "color.h"
#include <cassert>
#include <memory>
#include <string>

static void convert_tif_3uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_5uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_7uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_9uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_10uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_11uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_12uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_13uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_14uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_15uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
static void convert_tif_16uto32s(const uint16_t *pSrc, int32_t *pDst, size_t length,
		bool invert);

static void tiff_error(const char *msg, void *client_data){
	(void)client_data;
	if (msg){
		std::string out = std::string("libtiff: ") + msg;
		spdlog::error(out);
	}
}
static void tiff_warn(const char *msg, void *client_data){
	(void)client_data;
	if (msg){
		std::string out = std::string("libtiff: ") + msg;
		spdlog::warn(out);
	}
}

static bool tiffWarningHandlerVerbose = true;
void MyTiffErrorHandler(const char *module, const char *fmt, va_list ap) {
	(void) module;
    grk::log(tiff_error, nullptr,fmt,ap);
}

void MyTiffWarningHandler(const char *module, const char *fmt, va_list ap) {
	(void) module;
	if (tiffWarningHandlerVerbose)
		grk::log(tiff_warn, nullptr,fmt,ap);
}

void tiffSetErrorAndWarningHandlers(bool verbose) {
	tiffWarningHandlerVerbose = verbose;
	TIFFSetErrorHandler(MyTiffErrorHandler);
	TIFFSetWarningHandler(MyTiffWarningHandler);
}

static bool readTiffPixelsUnsigned(TIFF *tif, grk_image_comp *comps,
		uint32_t numcomps, uint16_t tiSpp, uint16_t tiPC, uint16_t tiPhoto,
		uint32_t chroma_subsample_x, uint32_t chroma_subsample_y);

static std::string getSampleFormatString(uint16_t tiSampleFormat){
	switch(tiSampleFormat){
		case SAMPLEFORMAT_UINT:
			return "UINT";
			break;
		case SAMPLEFORMAT_INT:
			return "INT";
			break;
		case SAMPLEFORMAT_IEEEFP:
			return "IEEEFP";
			break;
		case SAMPLEFORMAT_VOID:
			return "VOID";
			break;
		case SAMPLEFORMAT_COMPLEXINT:
			return "COMPLEXINT";
			break;
		case SAMPLEFORMAT_COMPLEXIEEEFP:
			return "COMPLEXIEEEFP";
			break;
		default:
			return "unknown";
	}
}

static std::string getColourFormatString(uint16_t tiPhoto){
	switch(tiPhoto){
		case PHOTOMETRIC_MINISWHITE:
			return "MINISWHITE";
			break;
		case PHOTOMETRIC_MINISBLACK:
			return "MINISBLACK";
			break;
		case PHOTOMETRIC_RGB:
			return "RGB";
			break;
		case PHOTOMETRIC_PALETTE:
			return "PALETTE";
			break;
		case PHOTOMETRIC_MASK:
			return "MASK";
			break;
		case PHOTOMETRIC_SEPARATED:
			return "SEPARATED";
			break;
		case PHOTOMETRIC_YCBCR:
			return "YCBCR";
			break;
		case PHOTOMETRIC_CIELAB:
			return "CIELAB";
			break;
		case PHOTOMETRIC_ICCLAB:
			return "ICCLAB";
			break;
		case PHOTOMETRIC_ITULAB:
			return "ITULAB";
			break;
		case PHOTOMETRIC_CFA:
			return "CFA";
			break;
		case PHOTOMETRIC_LOGL:
			return "LOGL";
			break;
		case PHOTOMETRIC_LOGLUV:
			return "LOGLUV";
			break;
		default:
			return "unknown";
	}
}


static void set_resolution(double *res, float resx, float resy, short resUnit) {
	// resolution is in pels / metre
	res[0] = resx;
	res[1] = resy;

	switch (resUnit) {
	case RESUNIT_INCH:
		//2.54 cm / inch
		res[0] *= 100 / 2.54;
		res[1] *= 100 / 2.54;
		break;
		// cm
	case RESUNIT_CENTIMETER:
		res[0] *= 100;
		res[1] *= 100;
		break;
	default:
		break;
	}
}


static bool readTiffPixelsUnsigned(TIFF *tif,
									grk_image_comp *comps,
									uint32_t numcomps,
									uint16_t tiSpp,
									uint16_t tiPC,
									uint16_t tiPhoto,
									uint32_t chroma_subsample_x,
									uint32_t chroma_subsample_y) {
	if (!tif)
		return false;

	bool success = true;
	cvtTo32 cvtTifTo32s = nullptr;
	cvtInterleavedToPlanar cvtToPlanar = nullptr;
	int32_t *planes[maxNumComponents];
	tsize_t rowStride;
	bool invert;
	tdata_t buf = nullptr;
	tstrip_t strip;
	tsize_t strip_size;
	uint32_t currentPlane = 0;
	int32_t *buffer32s = nullptr;
	bool subsampled = chroma_subsample_x != 1 || chroma_subsample_y != 1;
	size_t luma_block = chroma_subsample_x * chroma_subsample_y;
    size_t unitSize = luma_block + 2;

	switch (comps[0].prec) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
		cvtTifTo32s = cvtTo32_LUT[comps[0].prec];
		break;
		/* others are specific to TIFF */
	case 3:
		cvtTifTo32s = convert_tif_3uto32s;
		break;
	case 5:
		cvtTifTo32s = convert_tif_5uto32s;
		break;
	case 7:
		cvtTifTo32s = convert_tif_7uto32s;
		break;
	case 9:
		cvtTifTo32s = convert_tif_9uto32s;
		break;
	case 10:
		cvtTifTo32s = convert_tif_10uto32s;
		break;
	case 11:
		cvtTifTo32s = convert_tif_11uto32s;
		break;
	case 12:
		cvtTifTo32s = convert_tif_12uto32s;
		break;
	case 13:
		cvtTifTo32s = convert_tif_13uto32s;
		break;
	case 14:
		cvtTifTo32s = convert_tif_14uto32s;
		break;
	case 15:
		cvtTifTo32s = convert_tif_15uto32s;
		break;
	case 16:
		cvtTifTo32s = (cvtTo32) convert_tif_16uto32s;
		break;
	default:
		/* never here */
		break;
	}
	cvtToPlanar = cvtInterleavedToPlanar_LUT[numcomps];
	if (tiPC == PLANARCONFIG_SEPARATE) {
		cvtToPlanar = cvtInterleavedToPlanar_LUT[1]; /* override */
		tiSpp = 1U; /* consider only one sample per plane */
	}

	strip_size = TIFFStripSize(tif);
	buf = _TIFFmalloc(strip_size);
	if (buf == nullptr) {
		success = false;
		goto local_cleanup;
	}
	rowStride = (comps[0].w * tiSpp * comps[0].prec + 7U) / 8U;
	buffer32s = new int32_t[(size_t) comps[0].w * tiSpp];
	strip = 0;
	invert = tiPhoto == PHOTOMETRIC_MINISWHITE;
	for (uint32_t j = 0; j < numcomps; j++)
		planes[j] = comps[j].data;
	do {
		auto comp = comps + currentPlane;
		planes[0] = comp->data;
		uint32_t height = 0;
        // if width % chroma_subsample_x != 0...
        size_t units = (comp->w + chroma_subsample_x - 1) / chroma_subsample_x;
        // each coded row will be padded to fill unit
        size_t padding = (units * chroma_subsample_x - comp->w);
        if (subsampled){
        	rowStride = (tsize_t)(units * unitSize);
        }
		size_t xpos = 0;
		for (; (height <comp->h) && (strip < TIFFNumberOfStrips(tif)); strip++) {
			tsize_t ssize = TIFFReadEncodedStrip(tif, strip, buf, strip_size);
			if (ssize < 1 || ssize > strip_size) {
				spdlog::error("tiftoimage: Bad value for ssize({}) "
						"vs. strip_size({}).",
						(long long) ssize, (long long) strip_size);
				success = false;
				goto local_cleanup;
			}
			assert(ssize >= rowStride);
			const uint8_t *datau8 = (const uint8_t*) buf;
			while (ssize >= rowStride) {
				if (chroma_subsample_x == 1 && chroma_subsample_y == 1) {
					cvtTifTo32s(datau8, buffer32s, (size_t) comp->w * tiSpp,
							invert);
					cvtToPlanar(buffer32s, planes, (size_t) comp->w);
					for (uint32_t k = 0; k < numcomps; ++k)
						planes[k] += comp->stride ;
					datau8 += rowStride;
					ssize -= rowStride;
					height++;
				}
				else {
					uint32_t strideDiffCb = comps[1].stride - comps[1].w;
					uint32_t strideDiffCr = comps[2].stride - comps[2].w;
					for (size_t i = 0; i < (size_t)rowStride; i+=unitSize) {
						//process a unit
						//1. luma
						for (size_t k = 0; k < chroma_subsample_y; ++k) {
							for (size_t j =0; j < chroma_subsample_x; ++j){
								bool accept = height+k< comp->h && xpos+j < comp->w;
								if (accept)
									planes[0][xpos + j + k * comp->stride] = datau8[j];
							}
							datau8 += chroma_subsample_x;
						}
						//2. chroma
                     	*planes[1]++ = *datau8++;
                    	*planes[2]++ = *datau8++;

                    	//3. increment raster x
                    	xpos+=chroma_subsample_x;
                    	if (xpos >= comp->w){
                    		datau8 += padding;
                    		xpos = 0;
                    		planes[0] += comp->stride * chroma_subsample_y;
                    		planes[1] += strideDiffCb;
                    		planes[2] += strideDiffCr;
                    		height+= chroma_subsample_y;
                    	}
					}
					ssize -= rowStride;
				}
			}
		}
		currentPlane++;
	} while ((tiPC == PLANARCONFIG_SEPARATE) && (currentPlane < numcomps));
	local_cleanup: delete[] buffer32s;
	if (buf)
		_TIFFfree(buf);
	return success;
}

template<typename T> bool readTiffPixelsSigned(TIFF *tif, grk_image_comp *comps,
		uint32_t numcomps, uint16_t tiSpp, uint16_t tiPC) {
	if (!tif)
		return false;

	bool success = true;
	cvtInterleavedToPlanar cvtToPlanar = nullptr;
	int32_t *planes[maxNumComponents];
	tsize_t rowStride;
	tdata_t buf = nullptr;
	tstrip_t strip;
	tsize_t strip_size;
	uint32_t currentPlane = 0;
	int32_t *buffer32s = nullptr;

	cvtToPlanar = cvtInterleavedToPlanar_LUT[numcomps];
	if (tiPC == PLANARCONFIG_SEPARATE) {
		cvtToPlanar = cvtInterleavedToPlanar_LUT[1]; /* override */
		tiSpp = 1U; /* consider only one sample per plane */
	}

	strip_size = TIFFStripSize(tif);
	buf = _TIFFmalloc(strip_size);
	if (buf == nullptr) {
		success = false;
		goto local_cleanup;
	}
	rowStride = (comps[0].w * tiSpp * comps[0].prec + 7U) / 8U;
	buffer32s = new int32_t[(size_t) comps[0].w * tiSpp];
	strip = 0;
	for (uint32_t j = 0; j < numcomps; j++)
		planes[j] = comps[j].data;
	do {
		grk_image_comp *comp = comps + currentPlane;
		planes[0] = comp->data; /* to manage planar data */
		uint32_t height = comp->h;
		/* Read the Image components */
		for (; (height > 0) && (strip < TIFFNumberOfStrips(tif)); strip++) {
			tsize_t ssize = TIFFReadEncodedStrip(tif, strip, buf, strip_size);
			if (ssize < 1 || ssize > strip_size) {
				spdlog::error("tiftoimage: Bad value for ssize({}) "
						"vs. strip_size({}).",
						(long long) ssize, (long long) strip_size);
				success = false;
				goto local_cleanup;
			}
			const T *data = (const T*) buf;
			while (ssize >= rowStride) {
				for (size_t i = 0; i < (size_t) comp->w * tiSpp; ++i)
					buffer32s[i] = data[i];
				cvtToPlanar(buffer32s, planes, (size_t) comp->w);
				for (uint32_t k = 0; k < numcomps; ++k)
					planes[k] += comp->stride;
				data += (size_t)rowStride/sizeof(T);
				ssize -= rowStride;
				height--;
			}
		}
		currentPlane++;
	} while ((tiPC == PLANARCONFIG_SEPARATE) && (currentPlane < numcomps));
	local_cleanup: delete[] buffer32s;
	if (buf)
		_TIFFfree(buf);
	return success;

}

//rec 601 conversion factors, multiplied by 1000
const uint32_t rec_601_luma[3] {299, 587, 114};

TIFFFormat::TIFFFormat() : tif(nullptr),
							chroma_subsample_x(1),
							chroma_subsample_y(1),
							cvtPxToCx(nullptr),
							cvt32sToTif(nullptr)
{
	for (uint32_t i = 0; i < maxNumComponents; ++i)
		planes[i]=nullptr;

}
TIFFFormat::~TIFFFormat(){

}


bool TIFFFormat::encodeHeader(grk_image *image, const std::string &filename,
		uint32_t compressionParam) {
	m_image = image;
	m_fileName = filename;

	int tiPhoto;
	bool success = false;
	int32_t firstExtraChannel = -1;
	uint32_t num_colour_channels = 0;
	size_t numExtraChannels = 0;
	planes[0] = m_image->comps[0].data;
	uint32_t numcomps = m_image->numcomps;
	bool sgnd = m_image->comps[0].sgnd;
	uint32_t width = m_image->comps[0].w;
	uint32_t height = m_image->comps[0].h;
	uint32_t bps =  m_image->comps[0].prec;
	size_t units = m_image->comps->w;
	bool subsampled = grk::isSubsampled(m_image);
	tsize_t stride, rowsPerStrip;

	if (grk::isSubsampled(m_image) && !grk::isChromaSubsampled(m_image)){
		spdlog::error("TIFFFormat::encodeHeader: only support subsampled image with subsampled chroma channels");
		return false;
	}
	assert(m_image);
	assert(m_fileName.c_str());
	if (m_image->color_space == GRK_CLRSPC_CMYK) {
		if (numcomps < 4U) {
			spdlog::error("TIFFFormat::encodeHeader: CMYK images shall be composed of at least 4 planes.");

			return false;
		}
		tiPhoto = PHOTOMETRIC_SEPARATED;
		if (numcomps > 4U) {
			spdlog::warn("TIFFFormat::encodeHeader: number of components {} is "
						"greater than 4. Truncating to 4", numcomps);
			numcomps = 4U;
		}
	} else if (numcomps > 2U) {
		switch (m_image->color_space){
		case GRK_CLRSPC_EYCC:
		case GRK_CLRSPC_SYCC:
			if (subsampled && numcomps != 3){
				spdlog::error("TIFFFormat::encodeHeader: subsampled YCbCr m_image with alpha not supported.");
				goto cleanup;
			}
			chroma_subsample_x = m_image->comps[1].dx;
			chroma_subsample_y = m_image->comps[1].dy;
			tiPhoto = PHOTOMETRIC_YCBCR;
			break;
		case GRK_CLRSPC_DEFAULT_CIE:
		case GRK_CLRSPC_CUSTOM_CIE:
			tiPhoto = sgnd ? PHOTOMETRIC_CIELAB : PHOTOMETRIC_ICCLAB;
			break;
		default:
			tiPhoto = PHOTOMETRIC_RGB;
			break;
		}
	} else {
		tiPhoto = PHOTOMETRIC_MINISBLACK;
	}

	if (bps == 0) {
		spdlog::error("TIFFFormat::encodeHeader: m_image precision is zero.");
		goto cleanup;
	}

	if (numcomps > maxNumComponents){
		spdlog::error(
				"TIFFFormat::encodeHeader: number of components {} must be <= {}", numcomps,maxNumComponents);
		goto cleanup;
	}

	if (!grk::all_components_sanity_check(m_image,true))
		goto cleanup;

	cvtPxToCx = cvtPlanarToInterleaved_LUT[numcomps];
	switch (bps) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
		cvt32sToTif = cvtFrom32_LUT[bps];
		break;
	case 3:
		cvt32sToTif = convert_tif_32sto3u;
		break;
	case 5:
		cvt32sToTif = convert_tif_32sto5u;
		break;
	case 7:
		cvt32sToTif = convert_tif_32sto7u;
		break;
	case 9:
		cvt32sToTif = convert_tif_32sto9u;
		break;
	case 10:
		cvt32sToTif = convert_tif_32sto10u;
		break;
	case 11:
		cvt32sToTif = convert_tif_32sto11u;
		break;
	case 12:
		cvt32sToTif = convert_tif_32sto12u;
		break;
	case 13:
		cvt32sToTif = convert_tif_32sto13u;
		break;
	case 14:
		cvt32sToTif = convert_tif_32sto14u;
		break;
	case 15:
		cvt32sToTif = convert_tif_32sto15u;
		break;
	case 16:
		cvt32sToTif =  convert_tif_32sto16u;
		break;
	default:
		break;
	}
	// extra channels
	for (uint32_t i = 0U; i < numcomps; ++i) {
		if (m_image->comps[i].type != GRK_COMPONENT_TYPE_COLOUR) {
			if (firstExtraChannel == -1)
				firstExtraChannel = (int32_t)i;
			numExtraChannels++;
		}
		planes[i] = m_image->comps[i].data;
	}
	// TIFF assumes that alpha channels occur as last channels in m_image.
	if (numExtraChannels > 0) {
		num_colour_channels = (uint32_t)(numcomps - (uint32_t)numExtraChannels);
		if ((uint32_t)firstExtraChannel < num_colour_channels) {
			spdlog::warn("TIFFFormat::encodeHeader: TIFF requires that non-colour channels occur as "
						"last channels in m_image. "
						"TIFFTAG_EXTRASAMPLES tag for extra channels will not be set");
			numExtraChannels = 0;
		}
	}
	tif = TIFFOpen(m_fileName.c_str(), "wb");
	if (!tif) {
		spdlog::error("TIFFFormat::encodeHeader:failed to open {} for writing", m_fileName.c_str());
		goto cleanup;
	}
	// calculate rows per strip, base on target 8K strip size
	if (subsampled){
		units = (width + chroma_subsample_x - 1) / chroma_subsample_x;
		stride = ((width * chroma_subsample_y + units * 2) * bps + 7)/8;
		rowsPerStrip = (chroma_subsample_y * 8 * 1024 * 1024) / stride;
	} else {
		stride = (width * numcomps * bps + 7U) / 8U;
		rowsPerStrip = (16 * 1024 * 1024) / stride;
	}
	if (rowsPerStrip & 1)
		rowsPerStrip++;
	if (rowsPerStrip > height)
		rowsPerStrip = height;


	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
	TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT,
			sgnd ? SAMPLEFORMAT_INT : SAMPLEFORMAT_UINT);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, numcomps);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bps);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, tiPhoto);
	TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, rowsPerStrip);
	if( tiPhoto == PHOTOMETRIC_YCBCR )	{
		float refBlackWhite[6] = {0.0,255.0,128.0,255.0,128.0,255.0};
		float YCbCrCoefficients[3] = {0.299f,0.587f,0.114f};

		TIFFSetField( tif, TIFFTAG_YCBCRSUBSAMPLING, chroma_subsample_x, chroma_subsample_y);
		TIFFSetField(tif, TIFFTAG_REFERENCEBLACKWHITE, refBlackWhite);
		TIFFSetField(tif, TIFFTAG_YCBCRCOEFFICIENTS, YCbCrCoefficients);
		TIFFSetField(tif, TIFFTAG_YCBCRPOSITIONING, YCBCRPOSITION_CENTERED);
	}
	switch(compressionParam){
	case COMPRESSION_ADOBE_DEFLATE:
#ifdef ZIP_SUPPORT
		TIFFSetField(tif, TIFFTAG_COMPRESSION, compressionParam); // zip compression
#endif
		break;
	default:
		if (compressionParam != 0)
			TIFFSetField(tif, TIFFTAG_COMPRESSION, compressionParam);
	 }

	if (m_image->color.icc_profile_buf) {
		if (m_image->color_space == GRK_CLRSPC_ICC)
			TIFFSetField(tif, TIFFTAG_ICCPROFILE, m_image->color.icc_profile_len,
					m_image->color.icc_profile_buf);
	}

	if (m_image->xmp_buf && m_image->xmp_len)
		TIFFSetField(tif, TIFFTAG_XMLPACKET, m_image->xmp_len, m_image->xmp_buf);

	if (m_image->iptc_buf && m_image->iptc_len) {
		auto iptc_buf = m_image->iptc_buf;
		auto iptc_len = m_image->iptc_len;

		// length must be multiple of 4
		uint8_t *new_iptf_buf = nullptr;
		iptc_len += (4 - (iptc_len & 0x03));
		if (iptc_len != m_image->iptc_len) {
			new_iptf_buf = (uint8_t*) calloc(iptc_len, 1);
			if (!new_iptf_buf)
				goto cleanup;
			memcpy(new_iptf_buf, m_image->iptc_buf, m_image->iptc_len);
			iptc_buf = new_iptf_buf;
		}

		// Tag is of type TIFF_LONG, so byte length is divided by four
		if (TIFFIsByteSwapped(tif))
			TIFFSwabArrayOfLong((uint32_t*) iptc_buf, iptc_len / 4);
		TIFFSetField(tif, TIFFTAG_RICHTIFFIPTC, (uint32_t) iptc_len / 4,
				(void*) iptc_buf);
	}

	if (m_image->capture_resolution[0] > 0 && m_image->capture_resolution[1] > 0) {
		TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_CENTIMETER); // cm
		for (int i = 0; i < 2; ++i) {
			TIFFSetField(tif, TIFFTAG_XRESOLUTION,
					m_image->capture_resolution[0] / 100);
			TIFFSetField(tif, TIFFTAG_YRESOLUTION,
					m_image->capture_resolution[1] / 100);
		}
	}

	if (numExtraChannels) {
		std::unique_ptr<uint16[]> out(new uint16[numExtraChannels]);
		numExtraChannels = 0;
		for (uint32_t i = 0U; i < numcomps; ++i) {
			auto comp = m_image->comps + i;
			if (comp->type != GRK_COMPONENT_TYPE_COLOUR) {
				if (comp->type == GRK_COMPONENT_TYPE_OPACITY ||
					comp->type == GRK_COMPONENT_TYPE_PREMULTIPLIED_OPACITY){
					out[numExtraChannels++] =
							(m_image->comps[i].type == GRK_COMPONENT_TYPE_OPACITY) ?
									EXTRASAMPLE_UNASSALPHA : EXTRASAMPLE_ASSOCALPHA;
				} else {
					out[numExtraChannels++] = EXTRASAMPLE_UNSPECIFIED;
				}
			}
		}
		TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, numExtraChannels, out.get());
	}
	success = true;
cleanup:
	return success;
}

bool TIFFFormat::write(uint32_t *strip, void* buf, tmsize_t toWrite, tmsize_t *written){
	*written = 	TIFFWriteEncodedStrip(tif, (*strip)++, buf, toWrite);
	if (*written == -1){
		spdlog::error("TIFFFormat::encodeStrip: error in TIFFWriteEncodedStrip");
		return false;
	}
	if (*written != toWrite){
		spdlog::error("TIFFFormat::encodeStrip: Bytes written {} does not equal bytes to write {}", *written, toWrite);
		return false;
	}

	return true;
}


bool TIFFFormat::encodeStrip(uint32_t rows){
	(void)rows;

	bool success = false;
	bool subsampled = grk::isSubsampled(m_image);
	uint32_t width = m_image->comps[0].w;
	uint32_t height = m_image->comps[0].h;
	size_t units = m_image->comps->w;
	uint32_t bps =  m_image->comps[0].prec;

	uint32_t numcomps = m_image->numcomps;
	tsize_t stride, rowsPerStrip;
	uint32_t strip = 0;
	int32_t *buffer32s = nullptr;
	int32_t adjust =
			(m_image->comps[0].sgnd && m_image->comps[0].prec < 8) ?
					1 << (m_image->comps[0].prec - 1) : 0;

	// calculate rows per strip, base on target 8K strip size
	if (subsampled){
		units = (width + chroma_subsample_x - 1) / chroma_subsample_x;
		stride = ((width * chroma_subsample_y + units * 2) * bps + 7)/8;
		rowsPerStrip = (chroma_subsample_y * 8 * 1024 * 1024) / stride;
	} else {
		stride = (width * numcomps * bps + 7U) / 8U;
		rowsPerStrip = (16 * 1024 * 1024) / stride;
	}
	if (rowsPerStrip & 1)
		rowsPerStrip++;
	if (rowsPerStrip > height)
		rowsPerStrip = height;

	auto strip_size = TIFFStripSize(tif);
	auto buf = _TIFFmalloc(strip_size);
	if (buf == nullptr)
		goto cleanup;

	buffer32s = (int32_t*) malloc((size_t) width * numcomps * sizeof(int32_t));
	if (buffer32s == nullptr)
		goto cleanup;

	if (subsampled){
		tmsize_t bytesToWrite = 0;
		auto bufptr = (int8_t*)buf;
		for (uint32_t h = 0; h < height; h+=chroma_subsample_y) {
			if (h > 0 &&  (h % rowsPerStrip == 0)){
				tmsize_t written = 0;
				if (!write(&strip, buf, bytesToWrite, &written)){
					success = false;
					goto cleanup;
				}
				bufptr = (int8_t*)buf;
				bytesToWrite = 0;
			}
			size_t xpos = 0;
			for (uint32_t u = 0; u < units; ++u){
				for (size_t sub_h = 0; sub_h < chroma_subsample_y; ++sub_h) {
					size_t sub_x;
					for (sub_x =0; sub_x < chroma_subsample_x; ++sub_x){
						bool accept = h+sub_h<height && xpos+sub_x < width;
						*bufptr++ = accept ? (int8_t)planes[0][xpos + sub_x + sub_h * m_image->comps[0].stride] : 0;
						bytesToWrite++;
					}
				}
				//2. chroma
				*bufptr++ = (int8_t)*planes[1]++;
				*bufptr++ = (int8_t)*planes[2]++;
				bytesToWrite += 2;
				xpos+=chroma_subsample_x;
			}
			planes[0] += m_image->comps[0].stride * chroma_subsample_y;
			planes[1] += m_image->comps[1].stride - m_image->comps[1].w;
			planes[2] += m_image->comps[2].stride - m_image->comps[2].w;
		}
		//cleanup (
		if (bytesToWrite) {
			tmsize_t written = 0;
			if (!write(&strip, buf, bytesToWrite, &written)){
				success = false;
				goto cleanup;
			}
		}
	} else {
		tmsize_t h = 0;
		tmsize_t h_start = 0;
		while (h < height){
			tmsize_t bytesToWrite = 0;
			for (h = h_start; h < h_start + rowsPerStrip && (h < height); h++) {
				cvtPxToCx(planes, buffer32s, (size_t) width, adjust);
				cvt32sToTif(buffer32s, (uint8_t*) buf + bytesToWrite, (size_t) width * numcomps);
				for (uint32_t k = 0; k < numcomps; ++k)
					planes[k] += m_image->comps[k].stride;
				bytesToWrite +=  stride;
			}
			tmsize_t written = 0;
			if (!write(&strip, buf, bytesToWrite, &written)){
				success = false;
				goto cleanup;
			}

			h_start += (h - h_start);
		}
	}
	success = true;

cleanup:

	if (buffer32s)
		free(buffer32s);
	buffer32s = nullptr;

	if (buf)
		_TIFFfree((void*) buf);
	buf = nullptr;

	return success;
}
bool TIFFFormat::encodeFinish(void){
	if (tif)
		TIFFClose(tif);
	tif = nullptr;
	return true;
}
grk_image* TIFFFormat::decode(const std::string &filename,
		grk_cparameters *parameters) {
	bool found_assocalpha = false;
	size_t alpha_count = 0;
	uint16_t chroma_subsample_x = 1;
	uint16_t chroma_subsample_y = 1;
	GRK_COLOR_SPACE color_space = GRK_CLRSPC_UNKNOWN;
	grk_image_cmptparm cmptparm[maxNumComponents];
	grk_image *image = nullptr;
	uint16_t tiBps = 0, tiPhoto = 0, tiSf = SAMPLEFORMAT_UINT, tiSpp = 0, tiPC = 0;
	bool hasTiSf = false;
	short tiResUnit = 0;
	float tiXRes = 0, tiYRes = 0;
	uint32_t tiWidth = 0, tiHeight = 0;
	bool is_cinema = GRK_IS_CINEMA(parameters->rsiz);
	bool success = false;
	bool isCIE = false;
	uint16 compress;
	float *luma = nullptr, *refBlackWhite= nullptr;
	uint16 *red_orig=nullptr, *green_orig=nullptr, *blue_orig=nullptr;

	tif = TIFFOpen(filename.c_str(), "r");
	if (!tif) {
		spdlog::error("tiftoimage:Failed to open {} for reading", filename);
		return 0;
	}

	TIFFGetField(tif, TIFFTAG_COMPRESSION, &compress);
	TIFFGetFieldDefaulted(tif, TIFFTAG_IMAGEWIDTH, &tiWidth);
	TIFFGetFieldDefaulted(tif, TIFFTAG_IMAGELENGTH, &tiHeight);
	TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &tiBps);
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &tiSpp);
	TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &tiPhoto);
	TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &tiPC);
	hasTiSf = TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &tiSf) == 1;

	TIFFGetFieldDefaulted(tif, TIFFTAG_REFERENCEBLACKWHITE,
		&refBlackWhite);

	uint32_t w = tiWidth;
	uint32_t h = tiHeight;
	uint16_t numcomps = 0;
	uint32_t icclen = 0;
	uint8_t *iccbuf = nullptr;
	uint8_t *iptc_buf = nullptr;
	uint32_t iptc_len = 0;
	uint8_t *xmp_buf = nullptr;
	uint32_t xmp_len = 0;
	uint16 *sampleinfo = nullptr;
	uint16 extrasamples = 0;
	bool hasXRes = false, hasYRes = false, hasResUnit = false;
	bool isSigned = (tiSf == SAMPLEFORMAT_INT);

	// 1. sanity checks

	// check for supported photometric interpretation
	if (tiPhoto != PHOTOMETRIC_MINISBLACK && tiPhoto != PHOTOMETRIC_MINISWHITE
			&& tiPhoto != PHOTOMETRIC_RGB && tiPhoto != PHOTOMETRIC_ICCLAB
			&& tiPhoto != PHOTOMETRIC_CIELAB
			&& tiPhoto != PHOTOMETRIC_YCBCR
			&& tiPhoto != PHOTOMETRIC_SEPARATED
			&& tiPhoto != PHOTOMETRIC_PALETTE) {
		spdlog::error("tiftoimage: Unsupported color format {}.\n"
				"Only RGB(A), GRAY(A), CIELAB, YCC, CMYK and PALETTE have been implemented.",
				getColourFormatString(tiPhoto));
		goto cleanup;
	}
	//check for rec601
	if (tiPhoto == PHOTOMETRIC_YCBCR) {
		TIFFGetFieldDefaulted(tif, TIFFTAG_YCBCRCOEFFICIENTS, &luma);
		for (size_t i = 0; i < 3; ++i){
			if ((uint32_t)(luma[i] * 1000.0f + 0.5f) != rec_601_luma[i]){
				spdlog::error("tiftoimage: YCbCr image with unsupported non Rec. 601 colour space;");
				spdlog::error("YCbCrCoefficients: {},{},{}",luma[0],luma[1],luma[2]);
				spdlog::error("Please convert to sRGB before compressing.");
				goto cleanup;
			}
		}
	}
	// check sample format
	if (hasTiSf && tiSf != SAMPLEFORMAT_UINT && tiSf != SAMPLEFORMAT_INT) {
		spdlog::error("tiftoimage: Unsupported sample format: {}.", getSampleFormatString(tiSf));
		goto cleanup;
	}
	if (tiSpp == 0 ) {
		spdlog::error("tiftoimage: Samples per pixel must be non-zero");
		goto cleanup;
	}
	if (tiBps > 16U || tiBps == 0) {
		spdlog::error("tiftoimage: Unsupported precision {}. Maximum 16 Bits supported.", tiBps);
		goto cleanup;
	}
	if (tiWidth == 0 || tiHeight == 0) {
		spdlog::error("tiftoimage: Width({}) and height({}) must both "
				"be non-zero", tiWidth, tiHeight);
		goto cleanup;

	}
	TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES, &extrasamples,
			&sampleinfo);

	// 2. initialize image components and signed/unsigned
	memset(&cmptparm[0], 0, maxNumComponents * sizeof(grk_image_cmptparm));
	if ((tiPhoto == PHOTOMETRIC_RGB) && (is_cinema) && (tiBps != 12U)) {
		spdlog::warn("tiftoimage: Input image bitdepth is {} bits\n"
					"TIF conversion has automatically rescaled to 12-bits\n"
					"to comply with cinema profiles.", tiBps);
	} else {
		is_cinema = 0U;
	}
	numcomps = extrasamples;
	switch (tiPhoto) {
	case PHOTOMETRIC_PALETTE:
		if (isSigned) {
		   spdlog::error("tiftoimage: Signed palette image not supported");
		   goto cleanup;
		}
		color_space = GRK_CLRSPC_SRGB;
		numcomps++;
		break;
	case PHOTOMETRIC_MINISBLACK:
	case PHOTOMETRIC_MINISWHITE:
		color_space = GRK_CLRSPC_GRAY;
		numcomps++;
		break;
	case PHOTOMETRIC_RGB:
		color_space = GRK_CLRSPC_SRGB;
		numcomps += 3;
		break;
	case PHOTOMETRIC_CIELAB:
	case PHOTOMETRIC_ICCLAB:
		isCIE = true;
		color_space = GRK_CLRSPC_DEFAULT_CIE;
		numcomps += 3;
		break;
	case PHOTOMETRIC_YCBCR:
		// jpeg library is needed to convert from YCbCr to RGB
		if (compress == COMPRESSION_OJPEG ||
				compress == COMPRESSION_JPEG){
			   spdlog::error("tiftoimage: YCbCr image with JPEG compression"
					   " is not supported");
			   goto cleanup;
		}
		else if (compress == COMPRESSION_PACKBITS) {
			   spdlog::error("tiftoimage: YCbCr image with PACKBITS compression"
					   " is not supported");
			   goto cleanup;
		}
		color_space = GRK_CLRSPC_SYCC;
		numcomps += 3;
		TIFFGetFieldDefaulted( tif, TIFFTAG_YCBCRSUBSAMPLING, &chroma_subsample_x, &chroma_subsample_y);
		if (chroma_subsample_x != 1 || chroma_subsample_y != 1){
		   if (isSigned) {
			   spdlog::error("tiftoimage: chroma subsampling {},{} with signed data is not supported",
					   chroma_subsample_x,chroma_subsample_y );
			   goto cleanup;
		   }
		   if (numcomps != 3) {
			   spdlog::error("tiftoimage: chroma subsampling {},{} with alpha channel(s) not supported",
					   chroma_subsample_x,chroma_subsample_y );
			   goto cleanup;
		   }
		}

		break;
	case PHOTOMETRIC_SEPARATED:
		color_space = GRK_CLRSPC_CMYK;
		numcomps += 4;
		break;
	default:
		spdlog::error("tiftoimage: Unsupported colour space {}.",tiPhoto );
		goto cleanup;
		break;
	}
	if (tiPhoto == PHOTOMETRIC_CIELAB) {
		if (hasTiSf && (tiSf != SAMPLEFORMAT_INT)) {
			spdlog::warn("tiftoimage: Input image is in CIE colour space"
					" but sample format is unsigned int. Forcing to signed int");
		}
		isSigned = true;
	} else if (tiPhoto == PHOTOMETRIC_ICCLAB) {
		if (hasTiSf && (tiSf != SAMPLEFORMAT_UINT)) {
			spdlog::warn("tiftoimage: Input image is in ICC CIE colour"
					" space but sample format is signed int. Forcing to unsigned int");
		}
		isSigned = false;
	}

	if (isSigned) {
		if (tiPhoto == PHOTOMETRIC_MINISWHITE) {
			spdlog::error("tiftoimage: signed image with "
					"MINISWHITE format is not supported");
			goto cleanup;
		}
		if (tiBps != 8 && tiBps != 16){
			spdlog::error("tiftoimage: signed image with bit"
					" depth {} is not supported", tiBps);
			goto cleanup;
		}
	}
	if (numcomps > maxNumComponents){
		spdlog::error("tiftoimage: number of components "
				"{} must be <= %u", numcomps,maxNumComponents);
		goto cleanup;
	}

	// 4. create image
	for (uint32_t j = 0; j < numcomps; j++) {
		auto img_comp = cmptparm + j;
		img_comp->prec = (uint8_t)tiBps;
		bool chroma = (j==1 || j==2);
		img_comp->dx = chroma ? chroma_subsample_x : 1;
		img_comp->dy = chroma ? chroma_subsample_y : 1;
		img_comp->w = grk::ceildiv<uint32_t>(w, img_comp->dx);
		img_comp->h = grk::ceildiv<uint32_t>(h, img_comp->dy);
	}
	image = grk_image_create(numcomps, &cmptparm[0], color_space,true);
	if (!image)
		goto cleanup;

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->x1 =	image->x0 + (w - 1) * 1 + 1;
	if (image->x1 <= image->x0) {
		spdlog::error("tiftoimage: Bad value for image->x1({}) vs. "
				"image->x0({}).", image->x1, image->x0);
		goto cleanup;
	}
	image->y0 = parameters->image_offset_y0;
	image->y1 =	image->y0 + (h - 1) * 1 + 1;
	if (image->y1 <= image->y0) {
		spdlog::error("tiftoimage: Bad value for image->y1({}) vs. "
				"image->y0({}).", image->y1, image->y0);
		goto cleanup;
	}


	if (tiPhoto == PHOTOMETRIC_PALETTE){
		if (!TIFFGetField(tif, TIFFTAG_COLORMAP,
			&red_orig, &green_orig, &blue_orig)) {
			spdlog::error("Missing required \"Colormap\" tag");
			goto cleanup;
		}
		uint16_t palette_num_entries = (uint16_t)(1U << tiBps);
		uint8_t num_channels = 3U;
		grk::alloc_palette(&image->color, num_channels,  (uint16_t)palette_num_entries);
		auto cmap = new _grk_component_mapping_comp[num_channels];
		for (uint8_t i = 0; i < num_channels; ++i){
			cmap[i].component_index = 0;
			cmap[i].mapping_type = 1;
			cmap[i].palette_column = i;
			image->color.palette->channel_prec[i] = 16;
			image->color.palette->channel_sign[i] = false;
		}
		image->color.palette->component_mapping = cmap;
		auto lut_ptr = image->color.palette->lut;
		for (uint16_t i = 0; i < palette_num_entries; i++){
			*lut_ptr++ = red_orig[i];
			*lut_ptr++ = green_orig[i];
			*lut_ptr++ = blue_orig[i];
		}
	}


	for (uint32_t j = 0; j < numcomps; j++) {
		// handle non-colour channel
		uint16_t numColourChannels = numcomps - extrasamples;
		auto comp = image->comps + j;

		if (extrasamples > 0 && j >= numColourChannels) {
			comp->type = GRK_COMPONENT_TYPE_UNSPECIFIED;
			comp->association = GRK_COMPONENT_ASSOC_UNASSOCIATED;
			auto alphaType = sampleinfo[j - numColourChannels];
			if (alphaType == EXTRASAMPLE_ASSOCALPHA) {
				if (found_assocalpha){
					spdlog::warn("tiftoimage: Found more than one associated alpha channel");
				}
				alpha_count++;
				comp->type = GRK_COMPONENT_TYPE_PREMULTIPLIED_OPACITY;
				found_assocalpha = true;
			}
			else if (alphaType == EXTRASAMPLE_UNASSALPHA) {
				alpha_count++;
				comp->type = GRK_COMPONENT_TYPE_OPACITY;
			}
			else {
				// some older mono or RGB images may have alpha channel
				// stored as EXTRASAMPLE_UNSPECIFIED
				if ((color_space == GRK_CLRSPC_GRAY && numcomps == 2) ||
						(color_space == GRK_CLRSPC_SRGB && numcomps == 4) ) {
					alpha_count++;
					comp->type = GRK_COMPONENT_TYPE_OPACITY;
				}
			}
		}
		if (comp->type == GRK_COMPONENT_TYPE_OPACITY ||
			comp->type == GRK_COMPONENT_TYPE_PREMULTIPLIED_OPACITY){
				switch(alpha_count){
				case 1:
					comp->association = GRK_COMPONENT_ASSOC_WHOLE_IMAGE;
					break;
				case 2:
					comp->association = GRK_COMPONENT_ASSOC_UNASSOCIATED;
					break;
				default:
					comp->type = GRK_COMPONENT_TYPE_UNSPECIFIED;
					comp->association = GRK_COMPONENT_ASSOC_UNASSOCIATED;
					break;
				}

		}
		comp->sgnd = isSigned;
	}

	// 5. extract capture resolution
	hasXRes = TIFFGetFieldDefaulted(tif, TIFFTAG_XRESOLUTION, &tiXRes) == 1;
	hasYRes = TIFFGetFieldDefaulted(tif, TIFFTAG_YRESOLUTION, &tiYRes) == 1;
	hasResUnit = TIFFGetFieldDefaulted(tif, TIFFTAG_RESOLUTIONUNIT, &tiResUnit) == 1;
	if (hasXRes && hasYRes && hasResUnit && tiResUnit != RESUNIT_NONE) {
		set_resolution(parameters->capture_resolution_from_file, tiXRes, tiYRes,
				tiResUnit);
		parameters->write_capture_resolution_from_file = true;
		image->capture_resolution[0] = tiXRes;
		image->capture_resolution[1] = tiYRes;
	}
	// 6. extract embedded ICC profile (with sanity check on binary size of profile)
	// note: we ignore ICC profile for CIE images as JPEG 2000 can't signal both
	// CIE and ICC
	if (!isCIE) {
		if ((TIFFGetFieldDefaulted(tif, TIFFTAG_ICCPROFILE, &icclen, &iccbuf) == 1)
				&& icclen > 0 && icclen < grk::maxICCProfileBufferLen) {
			image->color.icc_profile_buf = new uint8_t[icclen];
			memcpy(image->color.icc_profile_buf, iccbuf, icclen);
			image->color.icc_profile_len = icclen;
			image->color_space = GRK_CLRSPC_ICC;
		}
	}
	// 7. extract IPTC meta-data
	if (TIFFGetFieldDefaulted(tif, TIFFTAG_RICHTIFFIPTC, &iptc_len, &iptc_buf) == 1) {
		if (TIFFIsByteSwapped(tif))
			TIFFSwabArrayOfLong((uint32*) iptc_buf, iptc_len);
		// since TIFFTAG_RICHTIFFIPTC is of type TIFF_LONG, we must multiply
		// by 4 to get the length in bytes
		image->iptc_len = iptc_len * 4;
		image->iptc_buf = new uint8_t[iptc_len];
		memcpy(image->iptc_buf, iptc_buf, iptc_len);
	}
	// 8. extract XML meta-data
	if (TIFFGetFieldDefaulted(tif, TIFFTAG_XMLPACKET, &xmp_len, &xmp_buf) == 1) {
		image->xmp_len = xmp_len;
		image->xmp_buf = new uint8_t[xmp_len];
		memcpy(image->xmp_buf, xmp_buf, xmp_len);
	}
	// 9. read pixel data
	if (isSigned) {
		if (tiBps == 8)
			success =  readTiffPixelsSigned<int8_t>(tif, image->comps, numcomps, tiSpp,
						tiPC);
		else
			success =  readTiffPixelsSigned<int16_t>(tif, image->comps, numcomps, tiSpp,
						tiPC);
	}
	else {
		success = readTiffPixelsUnsigned(tif,
										image->comps,
										numcomps,
										tiSpp,
										tiPC,
										tiPhoto,
										chroma_subsample_x,
										chroma_subsample_y);
	}
	cleanup: if (tif)
		TIFFClose(tif);
	if (success) {
		if (is_cinema) {
			for (uint32_t j = 0; j < numcomps; ++j)
				scale_component(&(image->comps[j]), 12);
		}
		return image;
	}
	if (image)
		grk_image_destroy(image);

	return nullptr;
}

#define INV_MASK_16 0xFFFF
#define INV_MASK_15 ((1<<15)-1)
#define INV_MASK_14 ((1<<14)-1)
#define INV_MASK_13 ((1<<13)-1)
#define INV_MASK_12 ((1<<12)-1)
#define INV_MASK_11 ((1<<11)-1)
#define INV_MASK_10 ((1<<10)-1)
#define INV_MASK_9 ((1<<9)-1)
#define INV_MASK_8 0xFF
#define INV_MASK_7 ((1<<7)-1)
#define INV_MASK_6 ((1<<6)-1)
#define INV_MASK_5 ((1<<5)-1)
#define INV_MASK_4 ((1<<4)-1)
#define INV_MASK_3 ((1<<3)-1)
#define INV_MASK_2 ((1<<2)-1)

#define GETBITS(dest, nb, mask, invert) { \
	int needed = (nb); \
	uint32_t dst = 0U; \
	if (available == 0) { \
		val = *pSrc++; \
		available = 8; \
	} \
	while (needed > available) { \
		dst |= val & ((1U << available) - 1U); \
		needed -= available; \
		dst <<= needed; \
		val = *pSrc++; \
		available = 8; \
	} \
	dst |= (val >> (available - needed)) & ((1U << needed) - 1U); \
	available -= needed; \
	dest = INV((int32_t)dst, mask,invert); \
}

static void convert_tif_3uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 5)), INV_MASK_3, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x1FU) >> 2)), INV_MASK_3,invert);
		pDst[i + 2] = INV((int32_t )(((val0 & 0x3U) << 1) | (val1 >> 7)), INV_MASK_3, invert);
		pDst[i + 3] = INV((int32_t )(((val1 & 0x7FU) >> 4)), INV_MASK_3,invert);
		pDst[i + 4] = INV((int32_t )(((val1 & 0xFU) >> 1)), INV_MASK_3, invert);
		pDst[i + 5] = INV((int32_t )(((val1 & 0x1U) << 2) | (val2 >> 6)),INV_MASK_3, invert);
		pDst[i + 6] = INV((int32_t )(((val2 & 0x3FU) >> 3)), INV_MASK_3,invert);
		pDst[i + 7] = INV((int32_t )(((val2 & 0x7U))), INV_MASK_3, invert);

	}

	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 3, INV_MASK_3, invert)
	}
}
static void convert_tif_5uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 3)), INV_MASK_5, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x7U) << 2) | (val1 >> 6)),INV_MASK_5, invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0x3FU) >> 1)), INV_MASK_5,invert);
		pDst[i + 3] = INV((int32_t )(((val1 & 0x1U) << 4) | (val2 >> 4)),	INV_MASK_5, invert);
		pDst[i + 4] = INV((int32_t )(((val2 & 0xFU) << 1) | (val3 >> 7)),	INV_MASK_5, invert);
		pDst[i + 5] = INV((int32_t )(((val3 & 0x7FU) >> 2)), INV_MASK_5,	invert);
		pDst[i + 6] = INV((int32_t )(((val3 & 0x3U) << 3) | (val4 >> 5)),	INV_MASK_5, invert);
		pDst[i + 7] = INV((int32_t )(((val4 & 0x1FU))), INV_MASK_5, invert);

	}

	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 5, INV_MASK_5, invert)
	}
}
static void convert_tif_7uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 1)), INV_MASK_7, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x1U) << 6) | (val1 >> 2)),INV_MASK_7, invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0x3U) << 5) | (val2 >> 3)),INV_MASK_7, invert);
		pDst[i + 3] = INV((int32_t )(((val2 & 0x7U) << 4) | (val3 >> 4)),INV_MASK_7, invert);
		pDst[i + 4] = INV((int32_t )(((val3 & 0xFU) << 3) | (val4 >> 5)),INV_MASK_7, invert);
		pDst[i + 5] = INV((int32_t )(((val4 & 0x1FU) << 2) | (val5 >> 6)),	INV_MASK_7, invert);
		pDst[i + 6] = INV((int32_t )(((val5 & 0x3FU) << 1) | (val6 >> 7)),	INV_MASK_7, invert);
		pDst[i + 7] = INV((int32_t )(((val6 & 0x7FU))), INV_MASK_7, invert);

	}

	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 7, INV_MASK_7, invert)
	}
}
static void convert_tif_9uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 1) | (val1 >> 7)), INV_MASK_9,invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x7FU) << 2) | (val2 >> 6)),	INV_MASK_9, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0x3FU) << 3) | (val3 >> 5)),	INV_MASK_9, invert);
		pDst[i + 3] = INV((int32_t )(((val3 & 0x1FU) << 4) | (val4 >> 4)),	INV_MASK_9, invert);
		pDst[i + 4] = INV((int32_t )(((val4 & 0xFU) << 5) | (val5 >> 3)),	INV_MASK_9, invert);
		pDst[i + 5] = INV((int32_t )(((val5 & 0x7U) << 6) | (val6 >> 2)),	INV_MASK_9, invert);
		pDst[i + 6] = INV((int32_t )(((val6 & 0x3U) << 7) | (val7 >> 1)),	INV_MASK_9, invert);
		pDst[i + 7] = INV((int32_t )(((val7 & 0x1U) << 8) | (val8)), INV_MASK_9,
				invert);

	}
	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 9, INV_MASK_9, invert)
	}
}

static void convert_tif_10uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 2) | (val1 >> 6)), INV_MASK_10,invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x3FU) << 4) | (val2 >> 4)),	INV_MASK_10, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0xFU) << 6) | (val3 >> 2)),	INV_MASK_10, invert);
		pDst[i + 3] = INV((int32_t )(((val3 & 0x3U) << 8) | val4), INV_MASK_10,	invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t )((val0 << 2) | (val1 >> 6)), INV_MASK_10,
				invert);

		if (length > 1U) {
			uint32_t val2 = *pSrc++;
			pDst[i + 1] = INV((int32_t )(((val1 & 0x3FU) << 4) | (val2 >> 4)),	INV_MASK_10, invert);
			if (length > 2U) {
				uint32_t val3 = *pSrc++;
				pDst[i + 2] = INV((int32_t )(((val2 & 0xFU) << 6) | (val3 >> 2)),INV_MASK_10, invert);
			}
		}
	}
}

static void convert_tif_11uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 3) | (val1 >> 5)), INV_MASK_11,invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x1FU) << 6) | (val2 >> 2)),	INV_MASK_11, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0x3U) << 9) | (val3 << 1) | (val4 >> 7)),	INV_MASK_11, invert);
		pDst[i + 3] = INV((int32_t )(((val4 & 0x7FU) << 4) | (val5 >> 4)),	INV_MASK_11, invert);
		pDst[i + 4] = INV((int32_t )(((val5 & 0xFU) << 7) | (val6 >> 1)),	INV_MASK_11, invert);
		pDst[i + 5] = INV((int32_t )(((val6 & 0x1U) << 10) | (val7 << 2) | (val8 >> 6)),INV_MASK_11, invert);
		pDst[i + 6] = INV((int32_t )(((val8 & 0x3FU) << 5) | (val9 >> 3)),INV_MASK_11, invert);
		pDst[i + 7] = INV((int32_t )(((val9 & 0x7U) << 8) | (val10)),INV_MASK_11, invert);

	}
	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 11, INV_MASK_11, invert)
	}
}
static void convert_tif_12uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 4) | (val1 >> 4)), INV_MASK_12,invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0xFU) << 8) | val2), INV_MASK_12,	invert);
	}
	if (length & 1U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		pDst[i + 0] = INV((int32_t )((val0 << 4) | (val1 >> 4)), INV_MASK_12,invert);
	}
}

static void convert_tif_13uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;
		uint32_t val11 = *pSrc++;
		uint32_t val12 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 5) | (val1 >> 3)), INV_MASK_13,invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x7U) << 10) | (val2 << 2) | (val3 >> 6)),INV_MASK_13, invert);
		pDst[i + 2] = INV((int32_t )(((val3 & 0x3FU) << 7) | (val4 >> 1)),	INV_MASK_13, invert);
		pDst[i + 3] = INV((int32_t )(((val4 & 0x1U) << 12) | (val5 << 4) | (val6 >> 4)),INV_MASK_13, invert);
		pDst[i + 4] = INV((int32_t )(((val6 & 0xFU) << 9) | (val7 << 1) | (val8 >> 7)),	INV_MASK_13, invert);
		pDst[i + 5] = INV((int32_t )(((val8 & 0x7FU) << 6) | (val9 >> 2)),	INV_MASK_13, invert);
		pDst[i + 6] = INV((int32_t )(((val9 & 0x3U) << 11) | (val10 << 3) | (val11 >> 5)),	INV_MASK_13, invert);
		pDst[i + 7] = INV((int32_t )(((val11 & 0x1FU) << 8) | (val12)),	INV_MASK_13, invert);

	}
	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 13, INV_MASK_13, invert)
	}
}

static void convert_tif_14uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 6) | (val1 >> 2)), INV_MASK_14,invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)),INV_MASK_14, invert);
		pDst[i + 2] = INV((int32_t )(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)),INV_MASK_14, invert);
		pDst[i + 3] = INV((int32_t )(((val5 & 0x3FU) << 8) | val6), INV_MASK_14,invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = (int32_t) ((val0 << 6) | (val1 >> 2));

		if (length > 1U) {
			uint32_t val2 = *pSrc++;
			uint32_t val3 = *pSrc++;
			pDst[i + 1] = INV((int32_t )(((val1 & 0x3U) << 12) | (val2 << 4)| (val3 >> 4)), INV_MASK_14, invert);
			if (length > 2U) {
				uint32_t val4 = *pSrc++;
				uint32_t val5 = *pSrc++;
				pDst[i + 2] = INV((int32_t )(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)), INV_MASK_14, invert);
			}
		}
	}
}

static void convert_tif_15uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;
		uint32_t val11 = *pSrc++;
		uint32_t val12 = *pSrc++;
		uint32_t val13 = *pSrc++;
		uint32_t val14 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 7) | (val1 >> 1)), (1 << 15) - 1,	invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x1U) << 14) | (val2 << 6) | (val3 >> 2)), INV_MASK_15, invert);
		pDst[i + 2] = INV((int32_t )(((val3 & 0x3U) << 13) | (val4 << 5) | (val5 >> 3)), INV_MASK_15, invert);
		pDst[i + 3] = INV((int32_t )(((val5 & 0x7U) << 12) | (val6 << 4) | (val7 >> 4)), INV_MASK_15, invert);
		pDst[i + 4] = INV((int32_t )(((val7 & 0xFU) << 11) | (val8 << 3) | (val9 >> 5)), INV_MASK_15, invert);
		pDst[i + 5] = INV((int32_t )(((val9 & 0x1FU) << 10) | (val10 << 2) 	| (val11 >> 6)), INV_MASK_15, invert);
		pDst[i + 6] = INV((int32_t )(((val11 & 0x3FU) << 9) | (val12 << 1) 	| (val13 >> 7)), INV_MASK_15, invert);
		pDst[i + 7] = INV((int32_t )(((val13 & 0x7FU) << 8) | (val14)), INV_MASK_15, invert);

	}
	length = length & 7U;
	if (length) {
		uint32_t val;
		int available = 0;
		for (size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 15, INV_MASK_15, invert)
	}
}

/* seems that libtiff decodes this to machine endianness */
static void convert_tif_16uto32s(const uint16_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	for (size_t i = 0; i < length; i++)
		pDst[i] = INV(pSrc[i], 0xFFFF, invert);
}


