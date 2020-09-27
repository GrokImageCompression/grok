/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
#include "BMPFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"
#include <taskflow/taskflow.hpp>
#include "FileUringIO.h"

// `MBED` in big endian format
const uint32_t BMP_ICC_PROFILE_EMBEDDED = 0x4d424544;
const uint32_t fileHeaderSize = 14;

const uint32_t BITMAPCOREHEADER_LENGTH = 12U;
const uint32_t BITMAPINFOHEADER_LENGTH = 40U;
const uint32_t BITMAPV2INFOHEADER_LENGTH = 52U;
const uint32_t BITMAPV3INFOHEADER_LENGTH = 56U;
const uint32_t BITMAPV4HEADER_LENGTH = 108U;
const uint32_t BITMAPV5HEADER_LENGTH = 124U;

const uint32_t os2_palette_element_len = 3;
const uint32_t palette_element_len = 4;

template<typename T> void get_int(T **buf, T *val) {
	*val = grk::endian<T>((*buf)[0], false);
	(*buf)++;
}

template<typename T> void put_int(T **buf, T val) {
	*buf[0] =grk::endian<T>(val, false);
	(*buf)++;
}

grk_image* BMPFormat::bmp1toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint8_t const *const*pLUT) {
	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;
	auto pSrc = pData + (height - 1U) * srcStride;
	if (image->numcomps == 1U) {
		bmp_applyLUT8u_1u32s_C1R(pSrc, -(int32_t) srcStride, image->comps[0].data,
				(int32_t) image->comps[0].stride, pLUT[0], width, height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) image->comps[0].stride;
		pDstStride[1] = (int32_t) image->comps[0].stride;
		pDstStride[2] = (int32_t) image->comps[0].stride;
		bmp_applyLUT8u_1u32s_C1P3R(pSrc, -(int32_t) srcStride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}

grk_image* BMPFormat::bmp4toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint8_t const *const*pLUT) {
	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;
	auto pSrc = pData + (height - 1U) * srcStride;
	if (image->numcomps == 1U) {
		bmp_applyLUT8u_4u32s_C1R(pSrc, -(int32_t) srcStride, image->comps[0].data,
				(int32_t) image->comps[0].stride, pLUT[0], width, height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) image->comps[0].stride;
		pDstStride[1] = (int32_t) image->comps[0].stride;
		pDstStride[2] = (int32_t) image->comps[0].stride;
		bmp_applyLUT8u_4u32s_C1P3R(pSrc, -(int32_t) srcStride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}


grk_image* BMPFormat::bmp8toimage(const uint8_t *pData, uint32_t srcStride,
								grk_image *image, uint8_t const *const*pLUT,
								bool topDown) {
	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;
	auto pSrc = topDown ? pData : (pData + (height - 1U) * srcStride);
	int32_t s_stride = topDown ? (int32_t)srcStride : (-(int32_t)srcStride);
	if (image->numcomps == 1U) {
		bmp_applyLUT8u_8u32s_C1R(pSrc,
								s_stride,
								image->comps[0].data,
								(int32_t) image->comps[0].stride,
								pLUT[0],
								width,
								height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) image->comps[0].stride;
		pDstStride[1] = (int32_t) image->comps[0].stride;
		pDstStride[2] = (int32_t) image->comps[0].stride;
		bmp_applyLUT8u_8u32s_C1P3R(pSrc, s_stride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}

bool BMPFormat::bmp_read_file_header(GRK_BITMAPFILEHEADER *fileHeader, GRK_BITMAPINFOHEADER *infoHeader) {
	memset(infoHeader, 0, sizeof(*infoHeader));
    const size_t len = fileHeaderSize + sizeof(uint32_t);
	uint8_t temp[len];
	uint32_t *temp_ptr = (uint32_t*)temp;
	if (!readFromFile(temp, len))
		return false;
	get_int((uint16_t**)&temp_ptr, &fileHeader->bfType);
	if (fileHeader->bfType != 19778) {
		spdlog::error("Not a BMP file");
		return false;
	}
	get_int(&temp_ptr, &fileHeader->bfSize);
	get_int((uint16_t**)&temp_ptr, &fileHeader->bfReserved1);
	get_int((uint16_t**)&temp_ptr, &fileHeader->bfReserved2);
	get_int(&temp_ptr, &fileHeader->bfOffBits);
	get_int(&temp_ptr, &infoHeader->biSize);

	return true;
}

bool BMPFormat::bmp_read_info_header(GRK_BITMAPFILEHEADER *fileHeader, GRK_BITMAPINFOHEADER *infoHeader) {
    const size_t len_initial = infoHeader->biSize - sizeof(uint32_t);
	uint8_t temp[sizeof(GRK_BITMAPINFOHEADER)];
	uint32_t *temp_ptr = (uint32_t*)temp;
	if (!readFromFile(temp, len_initial))
		return false;

	switch (infoHeader->biSize) {
	case BITMAPCOREHEADER_LENGTH:
	case BITMAPINFOHEADER_LENGTH:
	case BITMAPV2INFOHEADER_LENGTH:
	case BITMAPV3INFOHEADER_LENGTH:
	case BITMAPV4HEADER_LENGTH:
	case BITMAPV5HEADER_LENGTH:
		break;
	default:
		spdlog::error("unknown BMP header size {}", infoHeader->biSize);
		return false;
	}
	bool is_os2 = infoHeader->biSize == BITMAPCOREHEADER_LENGTH;
	if (is_os2){	//OS2
		int16_t temp;
		get_int((int16_t**)&temp_ptr, &temp);
		infoHeader->biWidth = temp;
		get_int((int16_t**)&temp_ptr, &temp);
		infoHeader->biHeight = temp;
	} else {
		get_int((int32_t**)&temp_ptr, &infoHeader->biWidth);
		get_int((int32_t**)&temp_ptr, &infoHeader->biHeight);
	}
	get_int((uint16_t**)&temp_ptr, &infoHeader->biPlanes);
	get_int((uint16_t**)&temp_ptr, &infoHeader->biBitCount);
	// sanity check
	if (infoHeader->biBitCount > 32){
		spdlog::error("Bit count {} not supported.",infoHeader->biBitCount);
		return false;
	}
	if (infoHeader->biSize >= BITMAPINFOHEADER_LENGTH) {
		get_int(&temp_ptr, &infoHeader->biCompression);
		get_int(&temp_ptr, &infoHeader->biSizeImage);
		get_int((int32_t**)&temp_ptr,  &infoHeader->biXpelsPerMeter);
		get_int((int32_t**)&temp_ptr,  &infoHeader->biYpelsPerMeter);
		get_int(&temp_ptr, &infoHeader->biClrUsed);
		get_int(&temp_ptr, &infoHeader->biClrImportant);
		//re-adjust header size
		//note: fileHeader->bfSize may include ICC profile length if ICC is present, in which case
		//defacto_header_size will be greater than BITMAPV5HEADER_LENGTH. This is not a problem below
		//since we truncate the defacto size at BITMAPV5HEADER_LENGTH.
		uint32_t defacto_header_size =
				fileHeader->bfSize - fileHeaderSize  -
					infoHeader->biClrUsed * (uint32_t)sizeof(uint32_t) - infoHeader->biSizeImage;
		if (defacto_header_size > infoHeader->biSize) {
			infoHeader->biSize = std::min<uint32_t>(defacto_header_size,BITMAPV5HEADER_LENGTH);
			 const size_t len_remaining = infoHeader->biSize - (len_initial + sizeof(uint32_t));
			 if (!readFromFile(temp + len_initial, len_remaining))
				return false;

		}
	}
	if (infoHeader->biSize >= BITMAPV2INFOHEADER_LENGTH) {
		get_int(&temp_ptr, &infoHeader->biRedMask);
		get_int(&temp_ptr, &infoHeader->biGreenMask);
		get_int(&temp_ptr, &infoHeader->biBlueMask);
	}
	if (infoHeader->biSize >= BITMAPV3INFOHEADER_LENGTH) {
		get_int(&temp_ptr, (uint32_t*)&infoHeader->biAlphaMask);
	}
	if (infoHeader->biSize >= BITMAPV4HEADER_LENGTH) {
		get_int(&temp_ptr, &infoHeader->biColorSpaceType);
		memcpy(infoHeader->biColorSpaceEP, temp_ptr,sizeof(infoHeader->biColorSpaceEP) );
		temp_ptr += sizeof(infoHeader->biColorSpaceEP)/sizeof(uint32_t);
		get_int(&temp_ptr, &infoHeader->biRedGamma);
		get_int(&temp_ptr, &infoHeader->biGreenGamma);
		get_int(&temp_ptr, &infoHeader->biBlueGamma);
	}
	if (infoHeader->biSize >= BITMAPV5HEADER_LENGTH) {
		get_int(&temp_ptr, &infoHeader->biIntent);
		get_int(&temp_ptr, &infoHeader->biIccProfileOffset);
		get_int(&temp_ptr, &infoHeader->biIccProfileSize);
		get_int(&temp_ptr, &infoHeader->biReserved);
	}
	return true;
}

bool BMPFormat::bmp_read_raw_data(uint8_t *pData, uint32_t stride, uint32_t height) {
	return readFromFile(pData, stride * height);
}

bool BMPFormat::bmp_read_rle8_data(uint8_t *pData, uint32_t stride,
		uint32_t width, uint32_t height) {
	uint32_t x = 0, y = 0, written = 0;
	uint8_t *pix;
	const uint8_t *beyond;
	uint8_t *pixels = new uint8_t[Info_h.biSizeImage];
	if (!readFromFile(pixels, Info_h.biSizeImage)){
		delete[] pixels;
		return false;
	}
	uint8_t *pixels_ptr = pixels;
	beyond = pData + stride * height;
	pix = pData;

	while (y < height) {
		int c = *pixels_ptr++;
		if (c) {
			int j;
			uint8_t c1 = *pixels_ptr++;
			for (j = 0;
					(j < c) && (x < width) && ((size_t) pix < (size_t) beyond);
					j++, x++, pix++) {
				*pix = c1;
				written++;
			}
		} else {
			c = *pixels_ptr++;
			if (c == 0x00) { /* EOL */
				x = 0;
				++y;
				pix = pData + y * stride + x;
			} else if (c == 0x01) { /* EOP */
				break;
			} else if (c == 0x02) { /* MOVE by dxdy */
				c = *pixels_ptr++;
				x += (uint32_t) c;
				c = *pixels_ptr++;
				y += (uint32_t) c;
				pix = pData + y * stride + x;
			} else { /* 03 .. 255 */
				int j;
				for (j = 0;
						(j < c) && (x < width)
								&& ((size_t) pix < (size_t) beyond);
						j++, x++, pix++) {
					uint8_t c1 = *pixels_ptr++;
					*pix = c1;
					written++;
				}
				if ((uint32_t) c & 1U) { /* skip padding byte */
					pixels_ptr++;
				}
			}
		}
	}/* while() */
	if (written != width * height) {
		spdlog::error(
				"Number of pixels written does not match specified image dimensions.");
		return false;
	}
	return true;
}
bool BMPFormat::bmp_read_rle4_data(uint8_t *pData, uint32_t stride,
		uint32_t width, uint32_t height) {
	uint32_t x = 0, y = 0, written = 0;
	uint8_t *pix;
	const uint8_t *beyond;
	uint8_t *pixels = new uint8_t[Info_h.biSizeImage];
	if (!readFromFile(pixels, Info_h.biSizeImage)){
		delete[] pixels;
		return false;
	}
	uint8_t *pixels_ptr = pixels;
	beyond = pData + stride * height;
	pix = pData;
	while (y < height) {
		int c = *pixels_ptr++;
		if (c) {/* encoded mode */
			int j;
			uint8_t c1 = *pixels_ptr++;

			for (j = 0;
					(j < c) && (x < width) && ((size_t) pix < (size_t) beyond);
					j++, x++, pix++) {
				*pix = (uint8_t) ((j & 1) ? (c1 & 0x0fU) : ((c1 >> 4) & 0x0fU));
				written++;
			}
		} else { /* absolute mode */
			c = *pixels_ptr++;
			if (c == 0x00) { /* EOL */
				x = 0;
				y++;
				pix = pData + y * stride;
			} else if (c == 0x01) { /* EOP */
				break;
			} else if (c == 0x02) { /* MOVE by dxdy */
				c = *pixels_ptr++;
				x += (uint32_t) c;

				c = *pixels_ptr++;
				y += (uint32_t) c;
				pix = pData + y * stride + x;
			} else { /* 03 .. 255 : absolute mode */
				int j;
				uint8_t c1 = 0U;

				for (j = 0;	(j < c) && (x < width) && ((size_t) pix < (size_t) beyond);
						j++, x++, pix++) {
					if ((j & 1) == 0)
						c1 = *pixels_ptr++;
					*pix = (uint8_t) ((j & 1) ? (c1 & 0x0fU) : ((c1 >> 4) & 0x0fU));
					written++;
				}
				 /* skip padding byte */
				if (((c & 3) == 1) || ((c & 3) == 2))
					pixels_ptr++;
			}
		}
	} /* while(y < height) */
	if (written != width * height) {
		spdlog::error(
				"Number of pixels written does not match specified image dimensions.");
		return false;
	}
	delete[] pixels;
	return true;
}
uint32_t BMPFormat::getPaddedWidth(){
	assert(m_image);
	return ((m_image->numcomps *  m_image->comps[0].w + 3) >> 2) << 2;
}

BMPFormat::BMPFormat(void) : m_srcIndex(0)
{
#ifdef GROK_HAVE_URING
	delete m_fileIO;
	m_fileIO = new FileUringIO();
#endif
}

bool BMPFormat::encodeHeader(grk_image *image, const std::string &filename, uint32_t compressionParam){
	(void) compressionParam;
	if (!ImageFormat::encodeHeader(image,filename,compressionParam))
		return false;
	bool ret = false;
	uint32_t w = m_image->comps[0].w;
	uint32_t h = m_image->comps[0].h;
	uint32_t padW = getPaddedWidth();
	uint32_t image_size = padW * h;
	uint32_t colours_used, lut_size;
	uint32_t full_header_size, info_header_size, icc_size=0;
	uint32_t header_plus_lut = 0;
	uint8_t *header_buf = nullptr, *header_ptr = nullptr;
	uint32_t w_dest = getPaddedWidth();

   	m_rowsPerStrip = (8 * 1024 * 1024) / w_dest;
   	if (m_rowsPerStrip == 0)
   		m_rowsPerStrip = 2;
	if (m_rowsPerStrip & 1)
		m_rowsPerStrip++;
	if (m_rowsPerStrip > h)
		m_rowsPerStrip = h;

	if (!grk::all_components_sanity_check(m_image,false))
		goto cleanup;
	if (m_image->numcomps != 1 &&
			(m_image->numcomps != 3 && m_image->numcomps != 4) ) {
		spdlog::error("Unsupported number of components: {}",
				m_image->numcomps);
		goto cleanup;
	}
	if (grk::isSubsampled(m_image)) {
		spdlog::error("Sub-sampled images not supported");
		goto cleanup;
	}
	for (uint32_t i = 0; i < m_image->numcomps; ++i) {
		if (m_image->comps[i].prec == 0) {
			spdlog::error("Unsupported precision: 0 for component {}",i);
			goto cleanup;
		}
	}
	colours_used = (m_image->numcomps == 3) ? 0 : 256 ;
	lut_size = colours_used * sizeof(uint32_t) ;
	full_header_size = fileHeaderSize + BITMAPINFOHEADER_LENGTH;
	if (m_image->icc_profile_buf){
		full_header_size = fileHeaderSize + sizeof(GRK_BITMAPINFOHEADER);
		icc_size = m_image->icc_profile_len;
	}
	info_header_size = full_header_size - fileHeaderSize;
	header_plus_lut = full_header_size + lut_size;

	header_ptr = header_buf = new uint8_t[header_plus_lut];
	*header_ptr++ = 'B';
	*header_ptr++ = 'M';

	/* FILE HEADER */
	// total size
	put_int((uint32_t**)(&header_ptr), full_header_size + lut_size + image_size + icc_size);
	// reserved
	put_int((uint32_t**)(&header_ptr), 0U);
	put_int((uint32_t**)(&header_ptr), full_header_size + lut_size);
	/* INFO HEADER   */
	put_int((uint32_t**)(&header_ptr), info_header_size);
	put_int((uint32_t**)(&header_ptr), w);
	put_int((uint32_t**)(&header_ptr), h);
	put_int((uint16_t**)(&header_ptr), (uint16_t)1);
	put_int((uint16_t**)(&header_ptr), (uint16_t)(m_image->numcomps * 8));
	put_int((uint32_t**)(&header_ptr), 0U);
	put_int((uint32_t**)(&header_ptr), image_size);
	for (uint32_t i = 0; i < 2; ++i){
		double cap = m_image->capture_resolution[i] ?
				m_image->capture_resolution[i] : 7834;
		put_int((uint32_t**)(&header_ptr), (uint32_t)(cap + 0.5f));
	}
	put_int((uint32_t**)(&header_ptr), colours_used);
	put_int((uint32_t**)(&header_ptr), colours_used);
	if (m_image->icc_profile_buf){
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), BMP_ICC_PROFILE_EMBEDDED);
		memset(header_ptr, 0, 36);
		header_ptr += 36;
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), 0U);
		put_int((uint32_t**)(&header_ptr), info_header_size +  lut_size + image_size);
		put_int((uint32_t**)(&header_ptr), m_image->icc_profile_len);
		put_int((uint32_t**)(&header_ptr), 0U);
	}
	// 1024-byte LUT
	if (m_image->numcomps ==1) {
		for (uint32_t i = 0; i < 256; i++) {
			*header_ptr++ = (uint8_t)i;
			*header_ptr++ = (uint8_t)i;
			*header_ptr++ = (uint8_t)i;
			*header_ptr++ = 0;
		}
	}
	if (!writeToFile(header_buf, header_plus_lut))
		goto cleanup;
	ret = true;
cleanup:
	delete[] header_buf;

	return ret;
}

bool BMPFormat::encodeStrip(uint32_t rows){
	(void)rows;
/*
	  tf::Executor executor;
	  tf::Taskflow taskflow;

	  auto [A, B, C, D] = taskflow.emplace(
	    [] () { std::cout << "TaskA\n"; },               //  task dependency graph
	    [] () { std::cout << "TaskB\n"; },               //
	    [] () { std::cout << "TaskC\n"; },               //          +---+
	    [] () { std::cout << "TaskD\n"; }                //    +---->| B |-----+
	  );                                                 //    |     +---+     |
	                                                     //  +---+           +-v-+
	  A.precede(B);  // A runs before B                  //  | A |           | D |
	  A.precede(C);  // A runs before C                  //  +---+           +-^-+
	  B.precede(D);  // B runs before D                  //    |     +---+     |
	  C.precede(D);  // C runs before D                  //    +---->| C |-----+
	                                                     //          +---+
	  executor.run(taskflow).wait();
*/
	bool ret = false;
	auto w = m_image->comps[0].w;
	auto h = m_image->comps[0].h;
	auto numcomps = m_image->numcomps;
	auto stride_src = m_image->comps[0].stride;
	m_srcIndex = stride_src * (h - 1);
	uint32_t w_dest = getPaddedWidth();
	uint32_t pad_dest = (4 - ((numcomps * w) &3 )) &3;
	if (pad_dest == 4)
		pad_dest = 0;

	int trunc[4] = {0,0,0,0};
	float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	int32_t shift[4] = {0,0,0,0};

	for (uint32_t compno = 0; compno < numcomps; ++compno){
		if (m_image->comps[compno].prec > 8) {
				trunc[compno] = (int) m_image->comps[compno].prec - 8;
				spdlog::warn("BMP conversion: truncating component {} from {} bits to 8 bits",
						compno, m_image->comps[compno].prec);
		} else if (m_image->comps[0].prec < 8) {
			scale[compno]= 255.0f/(1U << m_image->comps[compno].prec);
			spdlog::warn("BMP conversion: scaling component {} from {} bits to 8 bits",
					compno, m_image->comps[compno].prec);
		}
		shift[compno] = (m_image->comps[compno].sgnd ?
				1 << (m_image->comps[compno].prec - 1) : 0);
	}

	auto destBuff = new uint8_t[m_rowsPerStrip * w_dest];
	// zero out padding at end of line
	if (pad_dest){
		uint8_t *ptr = destBuff + w_dest - pad_dest;
		for (uint32_t m=0; m < m_rowsPerStrip; ++m) {
			memset(ptr, 0, pad_dest);
			ptr += w_dest;
		}
	}
	while ( m_rowCount < h) {
		uint64_t destInd = 0;
		uint32_t k_max = std::min<uint32_t>(m_rowsPerStrip, (uint32_t)(h - m_rowCount));
		for (uint32_t k = 0; k < k_max; k++) {
			for (uint32_t i = 0; i < w; i++) {
				uint8_t rc[4];
				for (uint32_t compno = 0; compno < numcomps; ++compno){
					int32_t r = m_image->comps[compno].data[m_srcIndex + i];
					r += shift[compno];
					if (trunc[compno] || (scale[compno] != 1.0f) ){
						if (trunc[compno])
							r = ((r >> trunc[compno]) + ((r >> (trunc[compno] - 1)) % 2));
						else
							r = (int32_t)(((float)r * scale[compno]) + 0.5f);
						if (r > 255)
							r = 255;
						else if (r < 0)
							r = 0;
					}
					rc[compno] = (uint8_t)r;
				}
				if (numcomps == 1) {
					destBuff[destInd++] = rc[0];
				} else {
					destBuff[destInd++] = rc[2];
					destBuff[destInd++] = rc[1];
					destBuff[destInd++] = rc[0];
					if (numcomps == 4)
						destBuff[destInd++] = rc[3];
				}
			}
			destInd += pad_dest;
			m_srcIndex -= stride_src;
		}
		if (!writeToFile(destBuff, destInd))
			goto cleanup;
		m_rowCount+=k_max;
	}


	ret = true;
cleanup:
	delete[] destBuff;

	return ret;
}
bool BMPFormat::encodeFinish(void){
	if (m_image->icc_profile_buf) {
		if (!writeToFile(m_image->icc_profile_buf,m_image->icc_profile_len))
			return false;
	}

	return ImageFormat::encodeFinish();
}

grk_image *  BMPFormat::decode(const std::string &fname,  grk_cparameters  *parameters){
	grk_image_cmptparm cmptparm[4]; /* maximum of 4 components */
	uint8_t lut_R[256], lut_G[256], lut_B[256];
	uint8_t const *pLUT[3];
	grk_image *image = nullptr;
	uint32_t palette_len, numcmpts = 1U;
	bool l_result = false;
	uint8_t *pData = nullptr;
	uint32_t bmpStride;
	pLUT[0] = lut_R;
	pLUT[1] = lut_G;
	pLUT[2] = lut_B;
	bool handled = true;
	bool topDown = false;
	bool is_os2 = false;
	uint8_t *pal  = nullptr;

	m_image = image;
	if (!openFile(fname, "r"))
		return nullptr;

	if (!bmp_read_file_header(&File_h, &Info_h))
		goto cleanup;
	if (!bmp_read_info_header(&File_h, &Info_h))
		goto cleanup;
	is_os2 = Info_h.biSize == BITMAPCOREHEADER_LENGTH;
	if (is_os2){
		uint32_t num_entries = (File_h.bfOffBits - fileHeaderSize -
				BITMAPCOREHEADER_LENGTH) / os2_palette_element_len;
		if (num_entries !=  (uint32_t)(1 << Info_h.biBitCount)) {
			spdlog::error("OS2: calculated number of entries {} "
					"doesn't match (1 << bit count) {}", num_entries,
					(uint32_t)(1 << Info_h.biBitCount));
			goto cleanup;
		}
	}
	if (Info_h.biHeight < 0){
		topDown = true;
		Info_h.biHeight = -Info_h.biHeight;
	}
	/* Load palette */
	if (Info_h.biBitCount <= 8U) {
		memset(lut_R, 0, sizeof(lut_R));
		memset(lut_G, 0, sizeof(lut_G));
		memset(lut_B, 0, sizeof(lut_B));

		palette_len = Info_h.biClrUsed;
		if ((palette_len == 0U) && (Info_h.biBitCount <= 8U)) {
			palette_len = (1U << Info_h.biBitCount);
		}
		if (palette_len > 256U)
			palette_len = 256U;

		const uint32_t palette_bytes =
				palette_len * (is_os2 ? os2_palette_element_len : palette_element_len);
		pal = new uint8_t[palette_bytes];
		if (!readFromFile(pal, palette_bytes))
			goto cleanup;
		uint8_t *pal_ptr = pal;

		if (palette_len > 0U) {
			uint8_t has_color = 0U;
			for (uint32_t i = 0U; i < palette_len; i++) {
				lut_B[i] = *pal_ptr++;
				lut_G[i] = *pal_ptr++;
				lut_R[i] =  *pal_ptr++;
				if (!is_os2) {
					pal_ptr++;
				}
				has_color |= (lut_B[i] ^ lut_G[i]) | (lut_G[i] ^ lut_R[i]);
			}
			if (has_color) {
				numcmpts = 3U;
			}
		}
	} else {
		numcmpts = 3U;
		if ((Info_h.biCompression == 3) && (Info_h.biAlphaMask != 0U)) {
			numcmpts++;
		}
	}

	if (Info_h.biWidth == 0 || Info_h.biHeight == 0)
		goto cleanup;
	if (Info_h.biBitCount > (((uint32_t) -1) - 31) / Info_h.biWidth)
		goto cleanup;

	bmpStride = ((Info_h.biWidth * Info_h.biBitCount + 31U) / 32U) * sizeof(uint32_t); /* rows are aligned on 32bits */
	if (Info_h.biBitCount == 4 && Info_h.biCompression == 2) { /* RLE 4 gets decoded as 8 bits data for now... */
		if (8 > (((uint32_t) -1) - 31) / Info_h.biWidth)
			goto cleanup;
		bmpStride = ((Info_h.biWidth * 8U + 31U) / 32U) * sizeof(uint32_t);
	}

	if (bmpStride > ((uint32_t) -1) / sizeof(uint8_t) / Info_h.biHeight)
		goto cleanup;
	pData = (uint8_t*) calloc(1, bmpStride * Info_h.biHeight * sizeof(uint8_t));
	if (pData == nullptr)
		goto cleanup;
	if (!seekInFile((long) File_h.bfOffBits))
		goto cleanup;

	switch (Info_h.biCompression) {
	case 0:
	case 3:
		/* read raw data */
		l_result = bmp_read_raw_data(pData, bmpStride, Info_h.biHeight);
		break;
	case 1:
		/* read rle8 data */
		l_result = bmp_read_rle8_data(pData, bmpStride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	case 2:
		/* read rle4 data */
		l_result = bmp_read_rle4_data(pData, bmpStride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	default:
		spdlog::error("Unsupported BMP compression");
		l_result = false;
		break;
	}
	if (!l_result) {
		goto cleanup;
	}

	/* create the image */
	memset(&cmptparm[0], 0, sizeof(cmptparm));
	for (uint32_t i = 0; i < 4U; i++) {
		auto img_comp = cmptparm + i;
		img_comp->prec = 8;
		img_comp->sgnd = false;
		img_comp->dx = parameters->subsampling_dx;
		img_comp->dy = parameters->subsampling_dy;
		img_comp->w = grk::ceildiv<uint32_t>(Info_h.biWidth, img_comp->dx);
		img_comp->h = grk::ceildiv<uint32_t>(Info_h.biHeight, img_comp->dy);
	}

	image = grk_image_create(numcmpts, &cmptparm[0],
			(numcmpts == 1U) ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB,true);
	if (!image) {
		goto cleanup;
	}
	// ICC profile
	if (Info_h.biSize == sizeof(GRK_BITMAPINFOHEADER)
			&& Info_h.biColorSpaceType == BMP_ICC_PROFILE_EMBEDDED
			&& Info_h.biIccProfileSize
			&& Info_h.biIccProfileSize < grk::maxICCProfileBufferLen) {

		//read in ICC profile
		if (!seekInFile( fileHeaderSize + Info_h.biIccProfileOffset))
			goto cleanup;

		//allocate buffer
		image->icc_profile_buf = new uint8_t[Info_h.biIccProfileSize];
		if (!readFromFile(image->icc_profile_buf,Info_h.biIccProfileSize)){
			spdlog::warn("Unable to read full ICC profile. Profile will be ignored.");
			delete[] image->icc_profile_buf;
			image->icc_profile_buf = nullptr;
			goto cleanup;
		}
		image->icc_profile_len = Info_h.biIccProfileSize;
		image->color_space = GRK_CLRSPC_ICC;
	}
	if (numcmpts == 4U) {
		image->comps[3].type = GRK_COMPONENT_TYPE_OPACITY;
	    image->comps[3].association = GRK_COMPONENT_ASSOC_WHOLE_IMAGE;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = image->x0 + (Info_h.biWidth - 1U) * parameters->subsampling_dx
			+ 1U;
	image->y1 = image->y0 + (Info_h.biHeight - 1U) * parameters->subsampling_dy
			+ 1U;

	/* Read the data */
	switch(Info_h.biCompression){
	case 0:
		switch (Info_h.biBitCount) {
			case 32:		/* RGBX */
				bmp_mask32toimage(pData, bmpStride, image, 0x00FF0000U, 0x0000FF00U,
						0x000000FFU, 0x00000000U);
				break;
			case 24:  	/*RGB */
				bmp24toimage(pData, bmpStride, image);
				break;
			case 16:	/*RGBX */
				bmp_mask16toimage(pData, bmpStride, image, 0x7C00U, 0x03E0U, 0x001FU,
						0x0000U);
				break;
			case 8: 	/* RGB 8bpp Indexed */
				bmp8toimage(pData, bmpStride, image, pLUT, topDown);
				break;
			case 4:    /* RGB 4bpp Indexed */
				bmp4toimage(pData, bmpStride, image, pLUT);
				break;
			case 1: 	/* Grayscale 1bpp Indexed */
				bmp1toimage(pData, bmpStride, image, pLUT);
				break;
			default:
				handled = false;
				break;
		}
		break;
		case 1:
			switch (Info_h.biBitCount) {
				case 8:		/*RLE8*/
					bmp8toimage(pData, bmpStride, image, pLUT, topDown);
					break;
				default:
					handled = false;
					break;
			}
		break;
	case 2:
		switch (Info_h.biBitCount) {
			case 4:		 /*RLE4*/
				bmp8toimage(pData, bmpStride, image, pLUT, topDown); /* RLE 4 gets decoded as 8 bits data for now */
				break;
			default:
				handled = false;
				break;
		}
		break;
	case 3:
		switch (Info_h.biBitCount) {
			case 32: /* BITFIELDS bit mask */
				if (Info_h.biRedMask && Info_h.biGreenMask && 	Info_h.biBlueMask) {
					bool fail = false;
					bool hasAlpha = image->numcomps > 3;
					// sanity check on bit masks
					uint32_t m[4] = {Info_h.biRedMask,
										Info_h.biGreenMask,
											Info_h.biBlueMask,
												Info_h.biAlphaMask};
					for (uint32_t i = 0; i < image->numcomps; ++i){
						int lead = grk::count_leading_zeros(m[i]);
						int trail = grk::count_trailing_zeros(m[i]);
						int cnt = grk::population_count(m[i]);
						// check contiguous
						if (lead + trail + cnt != 32){
							spdlog::error("RGB(A) bit masks must be contiguous");
							fail = true;
							break;
						}
						// check supported precision
						if (cnt > 16){
							spdlog::error("RGB(A) bit mask with precision ({0:d}) greater than 16 is not supported",cnt);
							fail = true;
						}
					}
					// check overlap
					if ( (m[0]&m[1]) || (m[0]&m[2]) || 	(m[1]&m[2]) )	{
						spdlog::error("RGB(A) bit masks must not overlap");
						fail = true;
					}
					if (hasAlpha && !fail){
						if ( (m[0]&m[3]) || (m[1]&m[3]) || (m[2]&m[3]) )	{
							spdlog::error("RGB(A) bit masks must not overlap");
							fail = true;
						}
					}
					if (fail){
						spdlog::error("RGB(A) bit masks:\n"
								"{0:b}\n"
								"{0:b}\n"
								"{0:b}\n"
								"{0:b}",m[0],m[1],m[2],m[3]);
						grk_image_destroy(image);
						image = nullptr;
						goto cleanup;
					}
				}else {
					spdlog::error("RGB(A) bit masks must be non-zero");
					handled = false;
					break;
				}

				bmp_mask32toimage(pData, bmpStride, image, Info_h.biRedMask,
						Info_h.biGreenMask, Info_h.biBlueMask, Info_h.biAlphaMask);
				break;
			case 16:  /* BITFIELDS bit mask*/
				if ((Info_h.biRedMask == 0U) && (Info_h.biGreenMask == 0U)
						&& (Info_h.biBlueMask == 0U)) {
					Info_h.biRedMask = 0xF800U;
					Info_h.biGreenMask = 0x07E0U;
					Info_h.biBlueMask = 0x001FU;
				}
				bmp_mask16toimage(pData, bmpStride, image, Info_h.biRedMask,
						Info_h.biGreenMask, Info_h.biBlueMask, Info_h.biAlphaMask);
				break;
			default:
				handled = false;
				break;
		}
		break;

	default:
		handled = false;
		break;
	}
	if (!handled){
		grk_image_destroy(image);
		image = nullptr;
		spdlog::error(
				"Precision [{}] does not match supported precision: "
				"24 bit RGB, 8 bit RGB, 4/8 bit RLE and 16/32 bit BITFIELD",
				Info_h.biBitCount);
	}
	cleanup:
		delete[] pal;
		free(pData);
		m_fileIO->close();
	return image;
}
