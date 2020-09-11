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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
namespace grk {

/** @defgroup JP2 JP2 - JPEG 2000 file format reader/writer */
/*@{*/

#define GRK_BOX_SIZE	1024
#define GRK_RESOLUTION_BOX_SIZE (4+4+10)

/** @name Local static functions */
/*@{*/

/**
 * Reads a IHDR box - Image Header box
 *
 * @param	p_image_header_data			pointer to actual data (already read from file)
 * @param	fileFormat							JPEG 2000 code stream.
 * @param	image_header_size			the size of the image header
 
 *
 * @return	true if the image header is valid, false else.
 */
static bool jp2_read_ihdr(FileFormat *fileFormat, uint8_t *p_image_header_data,
		uint32_t image_header_size);

/**
 * Writes the Image Header box - Image Header box.
 *
 * @param fileFormat					JPEG 2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_ihdr(FileFormat *fileFormat, uint32_t *p_nb_bytes_written);

/**
 * Read XML box
 *
 * @param	fileFormat					JPEG 2000 file codec.
 * @param	p_xml_data			pointer to actual data (already read from file)
 * @param	xml_size			size of the xml data
 
 *
 * @return	true if the image header is valid, false else.
 */
static bool jp2_read_xml(FileFormat *fileFormat, uint8_t *p_xml_data, uint32_t xml_size);

/**
 * Write XML box
 *
 * @param fileFormat					JPEG 2000 file codec.
 * @param p_nb_bytes_written		pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_xml(FileFormat *fileFormat, uint32_t *p_nb_bytes_written);

/**
 * Write buffer box
 *
 * @param boxId					box id.
 * @param buffer					buffer with data
 * @param fileFormat					JPEG 2000 file codec.
 * @param p_nb_bytes_written		pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_buffer(uint32_t boxId, grk_jp2_buffer *buffer,
		uint32_t *p_nb_bytes_written);

/**
 * Read a UUID box
 *
 * @param	fileFormat					JPEG 2000 file codec.
 * @param	p_header_data		pointer to actual data (already read from file)
 * @param	p_header_data_size	size of data
 
 *
 * @return	true if the image header is valid, false else.
 */
static bool jp2_read_uuid(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_data_size);

/**
 * Reads a Resolution box
 *
 * @param	p_resolution_data			pointer to actual data (already read from file)
 * @param	fileFormat							JPEG 2000 code stream.
 * @param	resolution_size			the size of the image header
 
 *
 * @return	true if the image header is valid, false else.
 */
static bool jp2_read_res(FileFormat *fileFormat, uint8_t *p_resolution_data,
		uint32_t resolution_size);

/**
 * Writes the Resolution box
 *
 * @param fileFormat					JPEG 2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_res(FileFormat *fileFormat, uint32_t *p_nb_bytes_written);

/**
 * Writes the Bit per Component box.
 *
 * @param	fileFormat						JPEG 2000 file codec.
 * @param	p_nb_bytes_written		pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_bpcc(FileFormat *fileFormat, uint32_t *p_nb_bytes_written);

/**
 * Reads a Bit per Component box.
 *
 * @param	p_bpc_header_data			pointer to actual data (already read from file)
 * @param	fileFormat							JPEG 2000 code stream.
 * @param	bpc_header_size			the size of the bpc header
 
 *
 * @return	true if the bpc header is valid, false else.
 */
static bool jp2_read_bpcc(FileFormat *fileFormat, uint8_t *p_bpc_header_data,
		uint32_t bpc_header_size);

static bool jp2_read_cdef(FileFormat *fileFormat, uint8_t *p_cdef_header_data,
		uint32_t cdef_header_size);

static void jp2_apply_cdef(grk_image *image, grk_jp2_color *color);

/**
 * Writes the Channel Definition box.
 *
 * @param fileFormat					JPEG 2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_cdef(FileFormat *fileFormat, uint32_t *p_nb_bytes_written);

/**
 * Writes the Colour Specification box.
 *
 * @param fileFormat					JPEG 2000 file codec.
 * @param p_nb_bytes_written	pointer to store the nb of bytes written by the function.
 *
 * @return	the data being copied.
 */
static uint8_t* jp2_write_colr(FileFormat *fileFormat, uint32_t *p_nb_bytes_written);

/**
 * Writes a FTYP box - File type box
 *
 * @param	fileFormat			JPEG 2000 code stream.
 
 *
 * @return	true if writing was successful.
 */
static bool jp2_write_ftyp(FileFormat *fileFormat);

/**
 * Reads a a FTYP box - File type box
 *
 * @param	p_header_data	the data contained in the FTYP box.
 * @param	fileFormat				JPEG 2000 code stream.
 * @param	header_size	the size of the data contained in the FTYP box.
 .
 *
 * @return true if the FTYP box is valid.
 */
static bool jp2_read_ftyp(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_size);

static bool jp2_skip_jp2c(FileFormat *fileFormat);

/**
 * Reads the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param	p_header_data	the data contained in the file header box.
 * @param	fileFormat				JPEG 2000 code stream.
 * @param	header_size	the size of the data contained in the file header box.
 .
 *
 * @return true if the JP2 Header box was successfully recognized.
 */
static bool jp2_read_jp2h(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_size);

static bool jp2_write_uuids(FileFormat *fileFormat);

/**
 * Writes the Jpeg2000 code stream Header box - JP2C Header box. This function must be called AFTER the coding has been done.
 *
 * @param	fileFormat			JPEG 2000 code stream.
 
 *
 * @return true if writing was successful.
 */
static bool jp2_write_jp2c(FileFormat *fileFormat);

/**
 * Reads a JPEG 2000 file signature box.
 *
 * @param	p_header_data	the data contained in the signature box.
 * @param	fileFormat				JPEG 2000 code stream.
 * @param	header_size	the size of the data contained in the signature box.
 .
 *
 * @return true if the file signature box is valid.
 */
static bool jp2_read_jp(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_size);

/**
 * Writes a JPEG 2000 file signature box.
 *
 * @param stream buffered stream.
 * @param	fileFormat			JPEG 2000 code stream.
 
 *
 * @return true if writing was successful.
 */
static bool jp2_write_jp(FileFormat *fileFormat);

/**
 Apply collected palette data
 @param color Collector for profile, cdef and pclr data
 @param image
 */
static bool jp2_apply_pclr(grk_image *image, grk_jp2_color *color);

static void jp2_free_pclr(grk_jp2_color *color);

/**
 * Collect palette data
 *
 * @param fileFormat 			JP2 file format
 * @param p_pclr_header_data    pclr header data
 * @param pclr_header_size    	pclr header data size
 *
 * @return true if successful
 */
static bool jp2_read_pclr(FileFormat *fileFormat, uint8_t *p_pclr_header_data,
		uint32_t pclr_header_size);

/**
 * Collect component mapping data
 *
 * @param fileFormat          JP2 file format
 * @param p_cmap_header_data  cmap header data
 * @param cmap_header_size 	  cmap header data size

 *
 * @return true if successful
 */

static bool jp2_read_cmap(FileFormat *fileFormat, uint8_t *p_cmap_header_data,
		uint32_t cmap_header_size);

/**
 * Reads the Color Specification box.
 *
 * @param	p_colr_header_data			pointer to actual data (already read from file)
 * @param	fileFormat							JPEG 2000 code stream.
 * @param	colr_header_size			the size of the color header
 
 *
 * @return	true if the bpc header is valid, false else.
 */
static bool jp2_read_colr(FileFormat *fileFormat, uint8_t *p_colr_header_data,
		uint32_t colr_header_size);

/*@}*/

/*@}*/

/**
 * Sets up the procedures to do on writing header after the code stream.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_init_end_header_writing(FileFormat *fileFormat);

/**
 * Sets up the procedures to do on reading header after the code stream.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_init_end_header_reading(FileFormat *fileFormat);

/**
 * Reads a JPEG 2000 file header structure.
 *
 * @param fileFormat the JPEG 2000 file header structure.
 * @param stream the stream to read data from.
 
 *
 * @return true if the box is valid.
 */
static bool jp2_read_header_procedure(FileFormat *fileFormat);

/**
 * Executes the given procedures on the given codec.
 *
 * @param	p_procedure_list	the list of procedures to execute
 * @param	fileFormat					JPEG 2000 code stream to execute the procedures on.
 *
 * @return	true				if all the procedures were successfully executed.
 */
static bool jp2_exec(FileFormat *fileFormat, std::vector<jp2_procedure> *p_procedure_list);

/**
 * Reads a box header.
 *
 * @param	stream						the input stream to read data from.
 * @param	box						the box structure to fill.
 * @param	p_number_bytes_read		number of bytes read from the stream
 
 *
 * @return	true if the box is recognized, false otherwise
 */
static bool jp2_read_box_hdr(grk_jp2_box *box, uint32_t *p_number_bytes_read,
		BufferedStream *stream);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool jp2_init_compress_validation(FileFormat *fileFormat);

/**
 * Sets up the procedures to do on writing header. Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_init_header_writing(FileFormat *fileFormat);

static bool jp2_default_validation(FileFormat *fileFormat);

/**
 * Finds the image execution function related to the given box id.
 *
 * @param	id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const grk_jp2_header_handler* jp2_img_find_handler(uint32_t id);

/**
 * Finds the execution function related to the given box id.
 *
 * @param	id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const grk_jp2_header_handler* jp2_find_handler(uint32_t id);

static const grk_jp2_header_handler jp2_header[] = { { JP2_JP, jp2_read_jp }, {
JP2_FTYP, jp2_read_ftyp }, { JP2_JP2H, jp2_read_jp2h },
		{ JP2_XML, jp2_read_xml }, { JP2_UUID, jp2_read_uuid } };

static const grk_jp2_header_handler jp2_img_header[] = { { JP2_IHDR,
		jp2_read_ihdr }, { JP2_COLR, jp2_read_colr },
		{ JP2_BPCC, jp2_read_bpcc }, { JP2_PCLR, jp2_read_pclr }, { JP2_CMAP,
				jp2_read_cmap }, { JP2_CDEF, jp2_read_cdef }, { JP2_RES,
				jp2_read_res }

};

/**
 * Reads a box header.
 *
 * @param	box						the box structure to fill.
 * @param	p_data					the character string to read data from.
 * @param	p_number_bytes_read		number of bytes read from the stream
 * @param	p_box_max_size			the maximum number of bytes in the box.
 *
 * @return	true if the box is recognized, false otherwise
 */
static bool jp2_read_box(grk_jp2_box *box, uint8_t *p_data,
		uint32_t *p_number_bytes_read, uint64_t p_box_max_size);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool jp2_init_decompress_validation(FileFormat *fileFormat);

/**
 * Sets up the procedures to do on reading header.
 * Developers wanting to extend the library can add their own writing procedures.
 */
static bool jp2_init_header_reading(FileFormat *fileFormat);

/***
 * Read box length and type only
 *
 *
 * returns: true if box header was read successfully, otherwise false
 * throw:   CorruptJP2BoxException if box is corrupt
 * Note: box length is never 0
 *
 */
static bool jp2_read_box_hdr(grk_jp2_box *box, uint32_t *p_number_bytes_read,
		BufferedStream *stream) {
	uint8_t data_header[8];

	assert(stream != nullptr);
	assert(box != nullptr);
	assert(p_number_bytes_read != nullptr);

	*p_number_bytes_read = (uint32_t) stream->read(data_header, 8);
	// we reached EOS
	if (*p_number_bytes_read < 8)
		return false;

	/* process read data */
	uint32_t L = 0;
	grk_read<uint32_t>(data_header, &L, 4);
	box->length = L;
	grk_read<uint32_t>(data_header + 4, &(box->type), 4);

	if (box->length == 0) { /* last box */
		box->length = stream->get_number_byte_left() + 8U;
		return true;
	}

	/* read XL  */
	if (box->length == 1) {
		uint32_t nb_bytes_read = (uint32_t) stream->read(data_header, 8);
		// we reached EOS
		if (nb_bytes_read < 8)
			return false;
		grk_read<uint64_t>(data_header, &box->length);
		*p_number_bytes_read += nb_bytes_read;
	}
	if (box->length < *p_number_bytes_read) {
		GRK_ERROR("invalid box size %" PRIu64 " (%x)", box->length,
				box->type);
		throw CorruptJP2BoxException();
	}
	return true;
}

static bool jp2_read_ihdr(FileFormat *fileFormat, uint8_t *p_image_header_data,
		uint32_t image_header_size) {
	assert(p_image_header_data != nullptr);
	assert(fileFormat != nullptr);

	if (fileFormat->comps != nullptr) {
		GRK_WARN("Ignoring ihdr box. First ihdr box already read");
		return true;
	}

	if (image_header_size != GRK_ENUM_CLRSPC_CIE) {
		GRK_ERROR("Bad image header box (bad size)");
		return false;
	}

	grk_read<uint32_t>(p_image_header_data, &(fileFormat->h), 4); /* HEIGHT */
	p_image_header_data += 4;
	grk_read<uint32_t>(p_image_header_data, &(fileFormat->w), 4); /* WIDTH */
	p_image_header_data += 4;

	if ((fileFormat->w == 0) || (fileFormat->h == 0)) {
		GRK_ERROR(
				"JP2 IHDR box: invalid dimensions: (%u,%u)",fileFormat->w,fileFormat->h);
		return false;
	}

	grk_read<uint32_t>(p_image_header_data, &(fileFormat->numcomps), 2); /* NC */
	p_image_header_data += 2;

	if ((fileFormat->numcomps == 0) || (fileFormat->numcomps > max_num_components)) {
		GRK_ERROR(
				"JP2 IHDR box: num components=%u does not conform to standard",
				fileFormat->numcomps);
		return false;
	}

	/* allocate memory for components */
	fileFormat->comps = (grk_jp2_comps*) grk_calloc(fileFormat->numcomps,
			sizeof(grk_jp2_comps));
	if (fileFormat->comps == 0) {
		GRK_ERROR("Not enough memory to handle image header (ihdr)");
		return false;
	}

	grk_read<uint32_t>(p_image_header_data++, &(fileFormat->bpc), 1); /* BPC */

	///////////////////////////////////////////////////
	// (bits per component == precision -1)
	// Value of 0xFF indicates that bits per component
	// varies by component

	// Otherwise, low 7 bits of bpc determine bits per component,
	// and high bit set indicates signed data,
	// unset indicates unsigned data
	if (((fileFormat->bpc != 0xFF)
			&& ((fileFormat->bpc & 0x7F) > (max_supported_precision - 1)))) {
		GRK_ERROR("JP2 IHDR box: bpc=%u not supported.", fileFormat->bpc);
		return false;
	}

	grk_read<uint32_t>(p_image_header_data++, &(fileFormat->C), 1); /* C */

	/* Should be equal to 7 cf. chapter about image header box */
	if (fileFormat->C != 7) {
		GRK_ERROR(
				"JP2 IHDR box: compression type: %u indicates"
				" a non-conformant JP2 file.",
				fileFormat->C);
		return false;
	}

	grk_read<uint32_t>(p_image_header_data++, &(fileFormat->UnkC), 1); /* UnkC */

	// UnkC must be binary : {0,1}
	if ((fileFormat->UnkC > 1)) {
		GRK_ERROR("JP2 IHDR box: UnkC=%u does not conform to standard",
				fileFormat->UnkC);
		return false;
	}

	grk_read<uint32_t>(p_image_header_data++, &(fileFormat->IPR), 1); /* IPR */

	// IPR must be binary : {0,1}
	if ((fileFormat->IPR > 1)) {
		GRK_ERROR("JP2 IHDR box: IPR=%u does not conform to standard",
				fileFormat->IPR);
		return false;
	}

	return true;
}

static uint8_t* jp2_write_ihdr(FileFormat *fileFormat, uint32_t *p_nb_bytes_written) {
	assert(fileFormat != nullptr);
	assert(p_nb_bytes_written != nullptr);

	/* default image header is 22 bytes wide */
	auto ihdr_data = (uint8_t*) grk_calloc(1, 22);
	if (ihdr_data == nullptr)
		return nullptr;


	auto current_ihdr_ptr = ihdr_data;

	/* write box size */
	grk_write<uint32_t>(current_ihdr_ptr, 22, 4);
	current_ihdr_ptr += 4;

	/* IHDR */
	grk_write<uint32_t>(current_ihdr_ptr, JP2_IHDR, 4);
	current_ihdr_ptr += 4;

	/* HEIGHT */
	grk_write<uint32_t>(current_ihdr_ptr, fileFormat->h, 4);
	current_ihdr_ptr += 4;

	/* WIDTH */
	grk_write<uint32_t>(current_ihdr_ptr, fileFormat->w, 4);
	current_ihdr_ptr += 4;

	/* NC */
	grk_write<uint32_t>(current_ihdr_ptr, fileFormat->numcomps, 2);
	current_ihdr_ptr += 2;

	/* BPC */
	grk_write<uint32_t>(current_ihdr_ptr++, fileFormat->bpc, 1);

	/* C : Always 7 */
	grk_write<uint32_t>(current_ihdr_ptr++, fileFormat->C, 1);

	/* UnkC, colorspace unknown */
	grk_write<uint32_t>(current_ihdr_ptr++, fileFormat->UnkC, 1);

	/* IPR, no intellectual property */
	grk_write<uint32_t>(current_ihdr_ptr++, fileFormat->IPR, 1);

	*p_nb_bytes_written = 22;

	return ihdr_data;
}

static uint8_t* jp2_write_buffer(uint32_t boxId, grk_jp2_buffer *buffer,
		uint32_t *p_nb_bytes_written) {
	assert(p_nb_bytes_written != nullptr);

	/* need 8 bytes for box plus buffer->len bytes for buffer*/
	uint32_t total_size = 8 + (uint32_t) buffer->len;
	auto data = (uint8_t*) grk_calloc(1, total_size);
	if (!data)
		return nullptr;

	uint8_t *current_ptr = data;

	/* write box size */
	grk_write<uint32_t>(current_ptr, total_size, 4);
	current_ptr += 4;

	/* write box id */
	grk_write<uint32_t>(current_ptr, boxId, 4);
	current_ptr += 4;

	/* write buffer data */
	memcpy(current_ptr, buffer->buffer, buffer->len);

	*p_nb_bytes_written = total_size;

	return data;
}

static bool jp2_read_xml(FileFormat *fileFormat, uint8_t *p_xml_data, uint32_t xml_size) {
	if (!p_xml_data || !xml_size) {
		return false;
	}
	fileFormat->xml.alloc(xml_size);
	if (!fileFormat->xml.buffer) {
		fileFormat->xml.len = 0;
		return false;
	}
	memcpy(fileFormat->xml.buffer, p_xml_data, xml_size);
	return true;
}

static uint8_t* jp2_write_xml(FileFormat *fileFormat, uint32_t *p_nb_bytes_written) {
	assert(fileFormat != nullptr);
	return jp2_write_buffer(JP2_XML, &fileFormat->xml, p_nb_bytes_written);
}

static bool jp2_read_uuid(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_size) {
	if (!p_header_data || header_size < 16)
		return false;

	if (fileFormat->numUuids == JP2_MAX_NUM_UUIDS) {
		GRK_WARN(
				"Reached maximum (%u) number of UUID boxes read - ignoring UUID box",
				JP2_MAX_NUM_UUIDS);
		return false;
	}
	auto uuid = fileFormat->uuids + fileFormat->numUuids;
	memcpy(uuid->uuid, p_header_data, 16);
	p_header_data += 16;
	uuid->alloc(header_size - 16);
	memcpy(uuid->buffer, p_header_data, uuid->len);
	fileFormat->numUuids++;

	return true;

}

double calc_res(uint16_t num, uint16_t den, uint8_t exponent) {
	if (den == 0)
		return 0;
	return ((double) num / den) * pow(10, exponent);
}

static bool jp2_read_res_box(uint32_t *id, uint32_t *num, uint32_t *den,
		uint32_t *exponent, uint8_t **p_resolution_data) {
	uint32_t box_size = 4 + 4 + 10;
	uint32_t size = 0;
	grk_read<uint32_t>(*p_resolution_data, &size, 4);
	*p_resolution_data += 4;
	if (size != box_size)
		return false;

	grk_read<uint32_t>(*p_resolution_data, id, 4);
	*p_resolution_data += 4;

	grk_read<uint32_t>(*p_resolution_data, num + 1, 2);
	*p_resolution_data += 2;

	grk_read<uint32_t>(*p_resolution_data, den + 1, 2);
	*p_resolution_data += 2;

	grk_read<uint32_t>(*p_resolution_data, num, 2);
	*p_resolution_data += 2;

	grk_read<uint32_t>(*p_resolution_data, den, 2);
	*p_resolution_data += 2;

	grk_read<uint32_t>((*p_resolution_data)++, exponent + 1, 1);
	grk_read<uint32_t>((*p_resolution_data)++, exponent, 1);

	return true;

}

static bool jp2_read_res(FileFormat *fileFormat, uint8_t *p_resolution_data,
		uint32_t resolution_size) {
	assert(p_resolution_data != nullptr);
	assert(fileFormat != nullptr);

	uint32_t num_boxes = resolution_size / GRK_RESOLUTION_BOX_SIZE;
	if (num_boxes == 0 || num_boxes > 2
			|| (resolution_size % GRK_RESOLUTION_BOX_SIZE)) {
		GRK_ERROR("Bad resolution box (bad size)");
		return false;
	}

	while (resolution_size > 0) {
		uint32_t id;
		uint32_t num[2];
		uint32_t den[2];
		uint32_t exponent[2];

		if (!jp2_read_res_box(&id, num, den, exponent, &p_resolution_data))
			return false;

		double *res;
		switch (id) {
		case JP2_CAPTURE_RES:
			res = fileFormat->capture_resolution;
			fileFormat->has_capture_resolution = true;
			break;
		case JP2_DISPLAY_RES:
			res = fileFormat->display_resolution;
			fileFormat->has_display_resolution = true;
			break;
		default:
			return false;
		}
		for (int i = 0; i < 2; ++i)
			res[i] = calc_res((uint16_t) num[i], (uint16_t) den[i],
					(uint8_t) exponent[i]);
		resolution_size -= GRK_RESOLUTION_BOX_SIZE;
	}
	return true;
}

void find_cf(double x, uint32_t *num, uint32_t *den) {
	// number of terms in continued fraction.
	// 15 is the max without precision errors for M_PI
#define MAX 15
	const double eps = 1.0 / USHRT_MAX;
	long p[MAX], q[MAX], a[MAX];

	int i;
	//The first two convergents are 0/1 and 1/0
	p[0] = 0;
	q[0] = 1;

	p[1] = 1;
	q[1] = 0;
	//The rest of the convergents (and continued fraction)
	for (i = 2; i < MAX; ++i) {
		a[i] = lrint(floor(x));
		p[i] = a[i] * p[i - 1] + p[i - 2];
		q[i] = a[i] * q[i - 1] + q[i - 2];
		//printf("%ld:  %ld/%ld\n", a[i], p[i], q[i]);
		if (fabs(x - (double) a[i]) < eps || (p[i] > USHRT_MAX)
				|| (q[i] > USHRT_MAX))
			break;
		x = 1.0 / (x - (double) a[i]);
	}
	*num = (uint32_t) p[i - 1];
	*den = (uint32_t) q[i - 1];
}

static void jp2_write_res_box(double resx, double resy, uint32_t box_id,
		uint8_t **current_res_ptr) {
	/* write box size */
	grk_write<uint32_t>(*current_res_ptr, GRK_RESOLUTION_BOX_SIZE, 4);
	*current_res_ptr += 4;

	/* Box ID */
	grk_write<uint32_t>(*current_res_ptr, box_id, 4);
	*current_res_ptr += 4;

	double res[2];
	// y is written first, then x
	res[0] = resy;
	res[1] = resx;

	uint32_t num[2];
	uint32_t den[2];
	int32_t exponent[2];

	for (size_t i = 0; i < 2; ++i) {
		exponent[i] = (int32_t) log10(res[i]);
		if (exponent[i] < 1)
			exponent[i] = 0;
		if (exponent[i] >= 1) {
			res[i] /= pow(10, exponent[i]);
		}
		find_cf(res[i], num + i, den + i);
	}
	for (size_t i = 0; i < 2; ++i) {
		grk_write<uint32_t>(*current_res_ptr, num[i], 2);
		*current_res_ptr += 2;
		grk_write<uint32_t>(*current_res_ptr, den[i], 2);
		*current_res_ptr += 2;
	}
	for (size_t i = 0; i < 2; ++i) {
		grk_write<uint32_t>(*current_res_ptr, (uint32_t)exponent[i], 1);
		*current_res_ptr += 1;
	}
}

static uint8_t* jp2_write_res(FileFormat *fileFormat, uint32_t *p_nb_bytes_written) {
	uint8_t *res_data = nullptr, *current_res_ptr = nullptr;
	assert(fileFormat);
	assert(p_nb_bytes_written);

	bool storeCapture = fileFormat->capture_resolution[0] > 0
			&& fileFormat->capture_resolution[1] > 0;

	bool storeDisplay = fileFormat->display_resolution[0] > 0
			&& fileFormat->display_resolution[1] > 0;

	uint32_t size = (4 + 4) + GRK_RESOLUTION_BOX_SIZE;
	if (storeCapture && storeDisplay) {
		size += GRK_RESOLUTION_BOX_SIZE;
	}

	res_data = (uint8_t*) grk_calloc(1, size);
	if (!res_data)
		return nullptr;

	current_res_ptr = res_data;

	/* write super-box size */
	grk_write<uint32_t>(current_res_ptr, size, 4);
	current_res_ptr += 4;

	/* Super-box ID */
	grk_write<uint32_t>(current_res_ptr, JP2_RES, 4);
	current_res_ptr += 4;

	if (storeCapture) {
		jp2_write_res_box(fileFormat->capture_resolution[0],
				fileFormat->capture_resolution[1],
				JP2_CAPTURE_RES, &current_res_ptr);
	}
	if (storeDisplay) {
		jp2_write_res_box(fileFormat->display_resolution[0],
				fileFormat->display_resolution[1],
				JP2_DISPLAY_RES, &current_res_ptr);
	}
	*p_nb_bytes_written = size;

	return res_data;
}

static uint8_t* jp2_write_bpcc(FileFormat *fileFormat, uint32_t *p_nb_bytes_written) {
	assert(fileFormat != nullptr);
	assert(p_nb_bytes_written != nullptr);

	uint32_t i;
	/* room for 8 bytes for box and 1 byte for each component */
	uint32_t bpcc_size = 8 + fileFormat->numcomps;


	auto bpcc_data = (uint8_t*) grk_calloc(1, bpcc_size);
	if (!bpcc_data)
		return nullptr;

	auto current_bpcc_ptr = bpcc_data;

	/* write box size */
	grk_write<uint32_t>(current_bpcc_ptr, bpcc_size, 4);
	current_bpcc_ptr += 4;

	/* BPCC */
	grk_write<uint32_t>(current_bpcc_ptr, JP2_BPCC, 4);
	current_bpcc_ptr += 4;

	for (i = 0; i < fileFormat->numcomps; ++i)
		grk_write(current_bpcc_ptr++, fileFormat->comps[i].bpcc);
	*p_nb_bytes_written = bpcc_size;

	return bpcc_data;
}

static bool jp2_read_bpcc(FileFormat *fileFormat, uint8_t *p_bpc_header_data,
		uint32_t bpc_header_size) {
	uint32_t i;

	assert(p_bpc_header_data != nullptr);
	assert(fileFormat != nullptr);

	if (fileFormat->bpc != 255) {
		GRK_WARN(
				"A BPCC header box is available although BPC given by the IHDR box (%u) indicate components bit depth is constant",
				fileFormat->bpc);
	}

	/* and length is relevant */
	if (bpc_header_size != fileFormat->numcomps) {
		GRK_ERROR("Bad BPCC header box (bad size)");
		return false;
	}

	/* read info for each component */
	for (i = 0; i < fileFormat->numcomps; ++i) {
		grk_read(p_bpc_header_data++, &fileFormat->comps[i].bpcc); /* read each BPCC component */
	}

	return true;
}
static uint8_t* jp2_write_cdef(FileFormat *fileFormat, uint32_t *p_nb_bytes_written) {
	/* 8 bytes for box, 2 for n */
	uint32_t cdef_size = 10;
	uint32_t value;
	uint16_t i;

	assert(fileFormat != nullptr);
	assert(p_nb_bytes_written != nullptr);
	assert(fileFormat->color.jp2_cdef != nullptr);
	assert(fileFormat->color.jp2_cdef->info != nullptr);
	assert(fileFormat->color.jp2_cdef->n > 0U);

	cdef_size += 6U * fileFormat->color.jp2_cdef->n;

	auto cdef_data = (uint8_t*) grk_malloc(cdef_size);
	if (!cdef_data)
		return nullptr;


	auto current_cdef_ptr = cdef_data;

	/* write box size */
	grk_write<uint32_t>(current_cdef_ptr, cdef_size, 4);
	current_cdef_ptr += 4;

	/* BPCC */
	grk_write<uint32_t>(current_cdef_ptr, JP2_CDEF, 4);
	current_cdef_ptr += 4;

	value = fileFormat->color.jp2_cdef->n;
	/* N */
	grk_write<uint32_t>(current_cdef_ptr, value, 2);
	current_cdef_ptr += 2;

	for (i = 0U; i < fileFormat->color.jp2_cdef->n; ++i) {
		value = fileFormat->color.jp2_cdef->info[i].cn;
		/* Cni */
		grk_write<uint32_t>(current_cdef_ptr, value, 2);
		current_cdef_ptr += 2;
		value = fileFormat->color.jp2_cdef->info[i].typ;
		/* Typi */
		grk_write<uint32_t>(current_cdef_ptr, value, 2);
		current_cdef_ptr += 2;
		value = fileFormat->color.jp2_cdef->info[i].asoc;
		/* Asoci */
		grk_write<uint32_t>(current_cdef_ptr, value, 2);
		current_cdef_ptr += 2;
	}
	*p_nb_bytes_written = cdef_size;

	return cdef_data;
}

static uint8_t* jp2_write_colr(FileFormat *fileFormat, uint32_t *p_nb_bytes_written) {
	/* room for 8 bytes for box 3 for common data and variable upon profile*/
	uint32_t colr_size = 11;

	assert(fileFormat != nullptr);
	assert(p_nb_bytes_written != nullptr);
	assert(fileFormat->meth == 1 || fileFormat->meth == 2);

	switch (fileFormat->meth) {
	case 1:
		colr_size += 4; /* EnumCS */
		break;
	case 2:
		assert(fileFormat->color.icc_profile_len); /* ICC profile */
		colr_size += fileFormat->color.icc_profile_len;
		break;
	default:
		return nullptr;
	}

	auto colr_data = (uint8_t*) grk_calloc(1, colr_size);
	if (!colr_data)
		return nullptr;

	auto current_colr_ptr = colr_data;

	/* write box size */
	grk_write<uint32_t>(current_colr_ptr, colr_size, 4);
	current_colr_ptr += 4;

	/* BPCC */
	grk_write<uint32_t>(current_colr_ptr, JP2_COLR, 4);
	current_colr_ptr += 4;

	/* METH */
	grk_write<uint32_t>(current_colr_ptr, fileFormat->meth, 1);
	++current_colr_ptr;

	/* PRECEDENCE */
	grk_write<uint32_t>(current_colr_ptr, fileFormat->precedence, 1);
	++current_colr_ptr;

	/* APPROX */
	grk_write<uint32_t>(current_colr_ptr, fileFormat->approx, 1);
	++current_colr_ptr;

	/* Meth value is restricted to 1 or 2 (Table I.9 of part 1) */
	if (fileFormat->meth == 1) {
		/* EnumCS */
		grk_write<uint32_t>(current_colr_ptr, fileFormat->enumcs, 4);
	} else {
		/* ICC profile */
		if (fileFormat->meth == 2) {
			memcpy(current_colr_ptr, fileFormat->color.icc_profile_buf,
					fileFormat->color.icc_profile_len);
			current_colr_ptr += fileFormat->color.icc_profile_len;
		}
	}
	*p_nb_bytes_written = colr_size;

	return colr_data;
}

static void jp2_free_pclr(grk_jp2_color *color) {
	if (color) {
		if (color->jp2_pclr) {
			grk_free(color->jp2_pclr->channel_sign);
			grk_free(color->jp2_pclr->channel_size);
			grk_free(color->jp2_pclr->entries);
			grk_free(color->jp2_pclr->cmap);
			grk_free(color->jp2_pclr);
			color->jp2_pclr = nullptr;
		}
	}
}

static bool jp2_check_color(grk_image *image, grk_jp2_color *color) {
	uint16_t i;

	/* testcase 4149.pdf.SIGSEGV.cf7.3501 */
	if (color->jp2_cdef) {
		auto info = color->jp2_cdef->info;
		uint16_t n = color->jp2_cdef->n;
		uint32_t nr_channels = image->numcomps; /* FIXME image->numcomps == fileFormat->numcomps before color is applied ??? */

		/* cdef applies to cmap channels if any */
		if (color->jp2_pclr && color->jp2_pclr->cmap)
			nr_channels = (uint32_t) color->jp2_pclr->nr_channels;


		for (i = 0; i < n; i++) {
			if (info[i].cn >= nr_channels) {
				GRK_ERROR("Invalid component index %u (>= %u).", info[i].cn,
						nr_channels);
				return false;
			}
			if (info[i].asoc == GRK_COMPONENT_ASSOC_UNASSOCIATED)
				continue;

			if (info[i].asoc > 0
					&& (uint32_t) (info[i].asoc - 1) >= nr_channels) {
				GRK_ERROR("Invalid component index %u (>= %u).",
						info[i].asoc - 1, nr_channels);
				return false;
			}
		}

		/* issue 397 */
		/* ISO 15444-1 states that if cdef is present, it shall contain a complete list of channel definitions. */
		while (nr_channels > 0) {
			for (i = 0; i < n; ++i) {
				if ((uint32_t) info[i].cn == (nr_channels - 1U))
					break;
			}
			if (i == n) {
				GRK_ERROR("Incomplete channel definitions.");
				return false;
			}
			--nr_channels;
		}
	}

	/* testcases 451.pdf.SIGSEGV.f4c.3723, 451.pdf.SIGSEGV.5b5.3723 and
	 66ea31acbb0f23a2bbc91f64d69a03f5_signal_sigsegv_13937c0_7030_5725.pdf */
	if (color->jp2_pclr && color->jp2_pclr->cmap) {
		uint16_t nr_channels = color->jp2_pclr->nr_channels;
		auto cmap = color->jp2_pclr->cmap;
		bool *pcol_usage = nullptr;
		bool is_sane = true;

		/* verify that all original components match an existing one */
		for (i = 0; i < nr_channels; i++) {
			if (cmap[i].cmp >= image->numcomps) {
				GRK_ERROR("Invalid component index %u (>= %u).", cmap[i].cmp,
						image->numcomps);
				is_sane = false;
				goto cleanup;
			}
		}

		pcol_usage = (bool*) grk_calloc(nr_channels, sizeof(bool));
		if (!pcol_usage) {
			GRK_ERROR("Unexpected OOM.");
			return false;
		}
		/* verify that no component is targeted more than once */
		for (i = 0; i < nr_channels; i++) {
			uint16_t pcol = cmap[i].pcol;
			if (cmap[i].mtyp != 0 && cmap[i].mtyp != 1) {
				GRK_ERROR("Unexpected MTYP value.");
				is_sane = false;
				goto cleanup;
			}
			if (pcol >= nr_channels) {
				GRK_ERROR(
						"Invalid component/palette index for direct mapping %u.",
						pcol);
				is_sane = false;
				goto cleanup;
			} else if (pcol_usage[pcol] && cmap[i].mtyp == 1) {
				GRK_ERROR("Component %u is mapped twice.", pcol);
				is_sane = false;
				goto cleanup;
			} else if (cmap[i].mtyp == 0 && cmap[i].pcol != 0) {
				/* I.5.3.5 PCOL: If the value of the MTYP field for this channel is 0, then
				 * the value of this field shall be 0. */
				GRK_ERROR("Direct use at #%u however pcol=%u.", i, pcol);
				is_sane = false;
				goto cleanup;
			} else
				pcol_usage[pcol] = true;
		}
		/* verify that all components are targeted at least once */
		for (i = 0; i < nr_channels; i++) {
			if (!pcol_usage[i] && cmap[i].mtyp != 0) {
				GRK_ERROR("Component %u doesn't have a mapping.", i);
				is_sane = false;
				goto cleanup;
			}
		}
		/* Issue 235/447 weird cmap */
		if (1 && is_sane && (image->numcomps == 1U)) {
			for (i = 0; i < nr_channels; i++) {
				if (!pcol_usage[i]) {
					is_sane = 0U;
					GRK_WARN(
							"Component mapping seems wrong. Trying to correct.",
							i);
					break;
				}
			}
			if (!is_sane) {
				is_sane = true;
				for (i = 0; i < nr_channels; i++) {
					cmap[i].mtyp = 1U;
					cmap[i].pcol = (uint8_t) i;
				}
			}
		}
		cleanup:
		grk_free(pcol_usage);
		if (!is_sane)
			return false;
	}

	return true;
}

static bool jp2_apply_pclr(grk_image *image, grk_jp2_color *color) {
	uint32_t j, max;
	uint16_t i, nr_channels, cmp, pcol;
	int32_t k, top_k;

	auto channel_size = color->jp2_pclr->channel_size;
	auto channel_sign = color->jp2_pclr->channel_sign;
	auto entries = color->jp2_pclr->entries;
	auto cmap = color->jp2_pclr->cmap;
	nr_channels = color->jp2_pclr->nr_channels;

	for (i = 0; i < nr_channels; ++i) {
		/* Palette mapping: */
		cmp = cmap[i].cmp;
		if (image->comps[cmp].data == nullptr) {
			GRK_ERROR(
					"image->comps[%u].data == nullptr in grk_jp2_apply_pclr().",
					i);
			return false;
		}
	}

	auto old_comps = image->comps;
	auto new_comps = (grk_image_comp*) grk_malloc(
			nr_channels * sizeof(grk_image_comp));
	if (!new_comps) {
		GRK_ERROR("Memory allocation failure in grk_jp2_apply_pclr().");
		return false;
	}
	for (i = 0; i < nr_channels; ++i) {
		pcol = cmap[i].pcol;
		cmp = cmap[i].cmp;

		/* Direct use */
		if (cmap[i].mtyp == 0) {
			assert(pcol == 0);
			new_comps[i] = old_comps[cmp];
			new_comps[i].data = nullptr;
		} else {
			assert(i == pcol);
			new_comps[pcol] = old_comps[cmp];
			new_comps[pcol].data = nullptr;
		}

		/* Palette mapping: */
		if (!grk_image_single_component_data_alloc(new_comps + i)) {
			while (i > 0) {
				--i;
				grk_aligned_free(new_comps[i].data);
			}
			grk_free(new_comps);
			GRK_ERROR("Memory allocation failure in grk_jp2_apply_pclr().");
			return false;
		}
		new_comps[i].prec = channel_size[i];
		new_comps[i].sgnd = channel_sign[i];
	}

	top_k = color->jp2_pclr->nr_entries - 1;

	for (i = 0; i < nr_channels; ++i) {
		/* Palette mapping: */
		cmp = cmap[i].cmp;
		pcol = cmap[i].pcol;
		auto src = old_comps[cmp].data;
		assert(src);
		max = new_comps[pcol].stride * new_comps[pcol].h;

		/* Direct use: */
		if (cmap[i].mtyp == 0) {
			assert(cmp == 0);
			auto dst = new_comps[i].data;
			assert(dst);
			for (j = 0; j < max; ++j) {
				dst[j] = src[j];
			}
		} else {
			assert(i == pcol);
			auto dst = new_comps[pcol].data;
			assert(dst);
			for (j = 0; j < max; ++j) {
				/* The index */
				if ((k = src[j]) < 0)
					k = 0;
				else if (k > top_k)
					k = top_k;

				/* The colour */
				dst[j] = (int32_t) entries[k * nr_channels + pcol];
			}
		}
	}

	max = image->numcomps;
	for (i = 0; i < max; ++i)
		grk_image_single_component_data_free(old_comps + i);
	grk_free(old_comps);
	image->comps = new_comps;
	image->numcomps = nr_channels;

	return true;

}/* apply_pclr() */

static bool jp2_read_pclr(FileFormat *fileFormat, uint8_t *p_pclr_header_data,
		uint32_t pclr_header_size) {
	uint16_t nr_entries, nr_channels;
	uint16_t i, j;
	uint32_t value;
	auto orig_header_data = p_pclr_header_data;

	assert(p_pclr_header_data != nullptr);
	assert(fileFormat != nullptr);

	(void) pclr_header_size;

	if (fileFormat->color.jp2_pclr)
		return false;

	if (pclr_header_size < 3)
		return false;

	grk_read<uint32_t>(p_pclr_header_data, &value, 2); /* NE */
	p_pclr_header_data += 2;
	nr_entries = (uint16_t) value;
	if ((nr_entries == 0U) || (nr_entries > 1024U)) {
		GRK_ERROR("Invalid PCLR box. Reports %u entries", (int) nr_entries);
		return false;
	}

	grk_read<uint32_t>(p_pclr_header_data, &value, 1); /* NPC */
	++p_pclr_header_data;
	nr_channels = (uint16_t) value;
	if (nr_channels == 0U) {
		GRK_ERROR("Invalid PCLR box. Reports 0 palette columns");
		return false;
	}

	if (pclr_header_size < 3 + (uint32_t) nr_channels)
		return false;

	auto entries = (uint32_t*) grk_malloc(
			(size_t) nr_channels * nr_entries * sizeof(uint32_t));
	if (!entries)
		return false;
	auto channel_size = (uint8_t*) grk_malloc(nr_channels);
	if (!channel_size) {
		grk_free(entries);
		return false;
	}
	auto channel_sign = (uint8_t*) grk_malloc(nr_channels);
	if (!channel_sign) {
		grk_free(entries);
		grk_free(channel_size);
		return false;
	}

	auto jp2_pclr = (grk_jp2_pclr*) grk_malloc(sizeof(grk_jp2_pclr));
	if (!jp2_pclr) {
		grk_free(entries);
		grk_free(channel_size);
		grk_free(channel_sign);
		return false;
	}

	jp2_pclr->channel_sign = channel_sign;
	jp2_pclr->channel_size = channel_size;
	jp2_pclr->entries = entries;
	jp2_pclr->nr_entries = nr_entries;
	jp2_pclr->nr_channels = (uint8_t) value;
	jp2_pclr->cmap = nullptr;

	fileFormat->color.jp2_pclr = jp2_pclr;

	for (i = 0; i < nr_channels; ++i) {
		grk_read<uint32_t>(p_pclr_header_data++, &value, 1); /* Bi */

		channel_size[i] = (uint8_t) ((value & 0x7f) + 1);
		channel_sign[i] = (value & 0x80) ? 1 : 0;
	}

	for (j = 0; j < nr_entries; ++j) {
		for (i = 0; i < nr_channels; ++i) {
			uint32_t bytes_to_read = (uint32_t) ((channel_size[i] + 7) >> 3);

			if (bytes_to_read > sizeof(uint32_t))
				bytes_to_read = sizeof(uint32_t);
			if ((ptrdiff_t) pclr_header_size
					< (ptrdiff_t) (p_pclr_header_data - orig_header_data)
							+ (ptrdiff_t) bytes_to_read)
				return false;

			grk_read<uint32_t>(p_pclr_header_data, &value, bytes_to_read); /* Cji */
			p_pclr_header_data += bytes_to_read;
			*entries = (uint32_t) value;
			entries++;
		}
	}

	return true;
}

static bool jp2_read_cmap(FileFormat *fileFormat, uint8_t *p_cmap_header_data,
		uint32_t cmap_header_size) {
	uint8_t i, nr_channels;
	uint32_t value;

	assert(fileFormat != nullptr);
	assert(p_cmap_header_data != nullptr);

	/* Need nr_channels: */
	if (fileFormat->color.jp2_pclr == nullptr) {
		GRK_ERROR("Need to read a PCLR box before the CMAP box.");
		return false;
	}

	/* Part 1, I.5.3.5: 'There shall be at most one Component Mapping box
	 * inside a JP2 Header box' :
	 */
	if (fileFormat->color.jp2_pclr->cmap) {
		GRK_ERROR("Only one CMAP box is allowed.");
		return false;
	}

	nr_channels = fileFormat->color.jp2_pclr->nr_channels;
	if (cmap_header_size < (uint32_t) nr_channels * 4) {
		GRK_ERROR("Insufficient data for CMAP box.");
		return false;
	}

	auto cmap = (grk_jp2_cmap_comp*) grk_malloc(
			nr_channels * sizeof(grk_jp2_cmap_comp));
	if (!cmap)
		return false;

	for (i = 0; i < nr_channels; ++i) {
		grk_read<uint32_t>(p_cmap_header_data, &value, 2); /* CMP^i */
		p_cmap_header_data += 2;
		cmap[i].cmp = (uint16_t) value;

		grk_read<uint32_t>(p_cmap_header_data++, &value, 1); /* MTYP^i */
		cmap[i].mtyp = (uint8_t) value;

		grk_read<uint32_t>(p_cmap_header_data++, &value, 1); /* PCOL^i */
		cmap[i].pcol = (uint8_t) value;
	}

	fileFormat->color.jp2_pclr->cmap = cmap;

	return true;
}

static void jp2_apply_cdef(grk_image *image, grk_jp2_color *color) {
	auto info = color->jp2_cdef->info;
	uint16_t n = color->jp2_cdef->n;

	for (uint16_t i = 0; i < n; ++i) {
		/* WATCH: asoc_index = asoc - 1 ! */
		uint16_t asoc = info[i].asoc;
		uint16_t cn = info[i].cn;

		if (cn >= image->numcomps) {
			GRK_WARN("jp2_apply_cdef: cn=%u, numcomps=%u", cn,
					image->numcomps);
			continue;
		}
		image->comps[cn].type = (GRK_COMPONENT_TYPE)info[i].typ;

		// no need to do anything further if this is not a colour channel,
		// or if this channel is associated with the whole image
		if ( info[i].typ != GRK_COMPONENT_TYPE_COLOUR ||
				info[i].asoc == GRK_COMPONENT_ASSOC_WHOLE_IMAGE)
			continue;

		if (info[i].typ == GRK_COMPONENT_TYPE_COLOUR &&
				asoc > image->numcomps) {
			GRK_WARN("jp2_apply_cdef: association=%u > numcomps=%u", asoc,
					image->numcomps);
			continue;
		}
		uint16_t asoc_index = (uint16_t) (asoc - 1);

		/* Swap only if color channel */
		if ((cn != asoc_index) && (info[i].typ == GRK_COMPONENT_TYPE_COLOUR)) {
			grk_image_comp saved;
			uint16_t j;

			memcpy(&saved, &image->comps[cn], sizeof(grk_image_comp));
			memcpy(&image->comps[cn], &image->comps[asoc_index],
					sizeof(grk_image_comp));
			memcpy(&image->comps[asoc_index], &saved, sizeof(grk_image_comp));

			/* Swap channels in following channel definitions, don't bother with j <= i that are already processed */
			for (j = (uint16_t) (i + 1U); j < n; ++j) {
				if (info[j].cn == cn) {
					info[j].cn = asoc_index;
				} else if (info[j].cn == asoc_index) {
					info[j].cn = cn;
				}
				/* asoc is related to color index. Do not update. */
			}
		}
	}

	grk_free(color->jp2_cdef->info);
	grk_free(color->jp2_cdef);
	color->jp2_cdef = nullptr;

}/* jp2_apply_cdef() */

static bool jp2_read_cdef(FileFormat *fileFormat, uint8_t *p_cdef_header_data,
		uint32_t cdef_header_size) {
	uint16_t i;
	uint32_t value;

	assert(fileFormat != nullptr);
	assert(p_cdef_header_data != nullptr);

	(void) cdef_header_size;

	/* Part 1, I.5.3.6: 'The shall be at most one Channel Definition box
	 * inside a JP2 Header box.'*/
	if (fileFormat->color.jp2_cdef)
		return false;

	if (cdef_header_size < 2) {
		GRK_ERROR("CDEF box: Insufficient data.");
		return false;
	}

	grk_read<uint32_t>(p_cdef_header_data, &value, 2); /* N */
	p_cdef_header_data += 2;

	if ((uint16_t) value == 0) { /* szukw000: FIXME */
		GRK_ERROR("CDEF box: Number of channel description is equal to zero.");
		return false;
	}

	if (cdef_header_size < 2 + (uint32_t) (uint16_t) value * 6) {
		GRK_ERROR("CDEF box: Insufficient data.");
		return false;
	}

	auto cdef_info = (grk_jp2_cdef_info*) grk_malloc(
			value * sizeof(grk_jp2_cdef_info));
	if (!cdef_info)
		return false;

	fileFormat->color.jp2_cdef = (grk_jp2_cdef*) grk_malloc(sizeof(grk_jp2_cdef));
	if (!fileFormat->color.jp2_cdef) {
		grk_free(cdef_info);
		return false;
	}
	fileFormat->color.jp2_cdef->info = cdef_info;
	fileFormat->color.jp2_cdef->n = (uint16_t) value;

	for (i = 0; i < fileFormat->color.jp2_cdef->n; ++i) {
		grk_read<uint32_t>(p_cdef_header_data, &value, 2); /* Cn^i */
		p_cdef_header_data += 2;
		cdef_info[i].cn = (uint16_t) value;

		grk_read<uint32_t>(p_cdef_header_data, &value, 2); /* Typ^i */
		p_cdef_header_data += 2;
		if (value > 2 && value != GRK_COMPONENT_TYPE_UNSPECIFIED){
			GRK_ERROR(
					"CDEF box : Illegal channel type %u",value);
			return false;
		}
		cdef_info[i].typ = (uint16_t) value;

		grk_read<uint32_t>(p_cdef_header_data, &value, 2); /* Asoc^i */
		if (value > 3 && value != GRK_COMPONENT_ASSOC_UNASSOCIATED){
			GRK_ERROR(
					"CDEF box : Illegal channel association %u",value);
			return false;
		}
		p_cdef_header_data += 2;
		cdef_info[i].asoc = (uint16_t) value;
	}

	// cdef sanity check
	// 1. check for multiple descriptions of the same component with different types
	for (i = 0; i < fileFormat->color.jp2_cdef->n; ++i) {
		auto infoi = cdef_info[i];
		for (uint16_t j = 0; j < fileFormat->color.jp2_cdef->n; ++j) {
			auto infoj = cdef_info[j];
			if (i != j && infoi.cn == infoj.cn && infoi.typ != infoj.typ) {
				GRK_ERROR(
						"CDEF box : multiple descriptions of component, %u, with differing types : %u and %u.",
						infoi.cn, infoi.typ, infoj.typ);
				return false;
			}
		}
	}

	// 2. check that type/association pairs are unique
	for (i = 0; i < fileFormat->color.jp2_cdef->n; ++i) {
		auto infoi = cdef_info[i];
		for (uint16_t j = 0; j < fileFormat->color.jp2_cdef->n; ++j) {
			auto infoj = cdef_info[j];
			if (i != j &&
				infoi.cn != infoj.cn &&
				infoi.typ == infoj.typ	&&
				infoi.asoc == infoj.asoc &&
				(infoi.typ != GRK_COMPONENT_TYPE_UNSPECIFIED ||
						infoi.asoc != GRK_COMPONENT_ASSOC_UNASSOCIATED )   ) {
				GRK_ERROR(
						"CDEF box : components %u and %u share same type/association pair (%u,%u).",
						infoi.cn, infoj.cn, infoj.typ, infoj.asoc);
				return false;
			}
		}
	}

	return true;
}

static bool jp2_read_colr(FileFormat *fileFormat, uint8_t *p_colr_header_data,
		uint32_t colr_header_size) {
	assert(fileFormat != nullptr);
	assert(p_colr_header_data != nullptr);

	if (colr_header_size < 3) {
		GRK_ERROR("Bad COLR header box (bad size)");
		return false;
	}

	/* Part 1, I.5.3.3 : 'A conforming JP2 reader shall ignore all colour
	 * specification boxes after the first.'
	 */
	if (fileFormat->color.jp2_has_colour_specification_box) {
		GRK_WARN(
				"A conforming JP2 reader shall ignore all colour specification boxes after the first, so we ignore this one.");
		return true;
	}
	grk_read<uint32_t>(p_colr_header_data++, &fileFormat->meth, 1); /* METH */
	grk_read<uint32_t>(p_colr_header_data++, &fileFormat->precedence, 1); /* PRECEDENCE */
	grk_read<uint32_t>(p_colr_header_data++, &fileFormat->approx, 1); /* APPROX */

	if (fileFormat->meth == 1) {
        uint32_t temp;
		if (colr_header_size < 7) {
			GRK_ERROR("Bad COLR header box (bad size: %u)", colr_header_size);
			return false;
		}
		grk_read<uint32_t>(p_colr_header_data, &temp); /* EnumCS */
		p_colr_header_data += 4;

		fileFormat->enumcs = (GRK_ENUM_COLOUR_SPACE)temp;
		if ((colr_header_size > 7) && (fileFormat->enumcs != GRK_ENUM_CLRSPC_CIE)) { /* handled below for CIELab) */
			/* testcase Altona_Technical_v20_x4.pdf */
			GRK_WARN("Bad COLR header box (bad size: %u)", colr_header_size);
		}

		if (fileFormat->enumcs == GRK_ENUM_CLRSPC_CIE) {
			uint32_t *cielab;
			bool nonDefaultLab = colr_header_size == 35;
			// only two ints are needed for default CIELab space
			cielab = (uint32_t*) new uint8_t[(nonDefaultLab ? 9 : 2) * sizeof(uint32_t)];
			if (cielab == nullptr) {
				GRK_ERROR("Not enough memory for cielab");
				return false;
			}
			cielab[0] = GRK_ENUM_CLRSPC_CIE; /* enumcs */
			cielab[1] = GRK_DEFAULT_CIELAB_SPACE;

			if (colr_header_size == 35) {
				uint32_t rl, ol, ra, oa, rb, ob, il;
				grk_read<uint32_t>(p_colr_header_data, &rl, 4);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &ol, 4);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &ra, 4);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &oa, 4);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &rb, 4);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &ob, 4);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &il, 4);
				p_colr_header_data += 4;

				cielab[1] = GRK_CUSTOM_CIELAB_SPACE;
				cielab[2] = rl;
				cielab[4] = ra;
				cielab[6] = rb;
				cielab[3] = ol;
				cielab[5] = oa;
				cielab[7] = ob;
				cielab[8] = il;
			} else if (colr_header_size != 7) {
				GRK_WARN("Bad COLR header box (CIELab, bad size: %u)",
						colr_header_size);
			}
			fileFormat->color.icc_profile_buf = (uint8_t*) cielab;
			fileFormat->color.icc_profile_len = 0;
		}
		fileFormat->color.jp2_has_colour_specification_box = 1;
	} else if (fileFormat->meth == 2) {
		/* ICC profile */
		uint32_t icc_len = (uint32_t) (colr_header_size - 3);
		if (icc_len == 0) {
			GRK_ERROR("ICC profile buffer length equals zero");
			return false;
		}
		fileFormat->color.icc_profile_buf = new uint8_t[(size_t) icc_len];
		memcpy(fileFormat->color.icc_profile_buf, p_colr_header_data, icc_len);
		fileFormat->color.icc_profile_len = icc_len;
		fileFormat->color.jp2_has_colour_specification_box = 1;
	} else if (fileFormat->meth > 2) {
		/*	ISO/IEC 15444-1:2004 (E), Table I.9 Legal METH values:
		 conforming JP2 reader shall ignore the entire Colour Specification box.*/
		GRK_WARN("COLR BOX meth value is not a regular value (%u), "
				"so we will ignore the entire Colour Specification box. ",
				fileFormat->meth);
	}
	return true;
}

static bool jp2_write_jp2h(FileFormat *fileFormat) {
	grk_jp2_img_header_writer_handler writers[32];
	int32_t i, nb_writers = 0;
	/* size of data for super box*/
	uint32_t jp2h_size = 8;
	bool result = true;
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);

	memset(writers, 0, sizeof(writers));

	writers[nb_writers++].handler = jp2_write_ihdr;
	if (fileFormat->bpc == 255)
		writers[nb_writers++].handler = jp2_write_bpcc;
	writers[nb_writers++].handler = jp2_write_colr;
	if (fileFormat->color.jp2_cdef) {
		writers[nb_writers++].handler = jp2_write_cdef;
	}
	if (fileFormat->has_display_resolution || fileFormat->has_capture_resolution) {
		bool storeCapture = fileFormat->capture_resolution[0] > 0
				&& fileFormat->capture_resolution[1] > 0;
		bool storeDisplay = fileFormat->display_resolution[0] > 0
				&& fileFormat->display_resolution[1] > 0;
		if (storeCapture || storeDisplay)
			writers[nb_writers++].handler = jp2_write_res;
	}
	if (fileFormat->xml.buffer && fileFormat->xml.len)
		writers[nb_writers++].handler = jp2_write_xml;
	for (i = 0; i < nb_writers; ++i) {
		auto current_writer = writers + i;
		current_writer->m_data = current_writer->handler(fileFormat,
				&(current_writer->m_size));
		if (current_writer->m_data == nullptr) {
			GRK_ERROR("Not enough memory to hold JP2 Header data");
			result = false;
			break;
		}
		jp2h_size += current_writer->m_size;
	}

	if (!result) {
		for (i = 0; i < nb_writers; ++i) {
			auto current_writer = writers + i;
			if (current_writer->m_data != nullptr) {
				grk_free(current_writer->m_data);
			}
		}
		return false;
	}

	/* write super box size */
	if (!stream->write_int(jp2h_size)) {
		GRK_ERROR("Stream error while writing JP2 Header box");
		result = false;
	}
	if (!stream->write_int(JP2_JP2H)) {
		GRK_ERROR("Stream error while writing JP2 Header box");
		result = false;
	}

	if (result) {
		for (i = 0; i < nb_writers; ++i) {
			auto current_writer = writers + i;
			if (stream->write_bytes(current_writer->m_data,
					current_writer->m_size) != current_writer->m_size) {
				GRK_ERROR("Stream error while writing JP2 Header box");
				result = false;
				break;
			}
		}
	}
	/* cleanup */
	for (i = 0; i < nb_writers; ++i) {
		auto current_writer = writers + i;
		grk_free(current_writer->m_data);
	}

	return result;
}

static bool jp2_write_uuids(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);;

	// write the uuids
	for (size_t i = 0; i < fileFormat->numUuids; ++i) {
		auto uuid = fileFormat->uuids + i;
		if (uuid->buffer && uuid->len) {
			/* write box size */
			stream->write_int((uint32_t) (8 + 16 + uuid->len));

			/* JP2_UUID */
			stream->write_int(JP2_UUID);

			/* uuid  */
			stream->write_bytes(uuid->uuid, 16);

			/* uuid data */
			stream->write_bytes(uuid->buffer, (uint32_t) uuid->len);
		}
	}
	return true;
}

static bool jp2_write_ftyp(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);

	uint32_t i;
	uint32_t ftyp_size = 16 + 4 * fileFormat->numcl;
	bool result = true;

	if (!stream->write_int(ftyp_size)) {
		result = false;
		goto end;
	}
	if (!stream->write_int(JP2_FTYP)) {
		result = false;
		goto end;
	}
	if (!stream->write_int(fileFormat->brand)) {
		result = false;
		goto end;
	}
	/* MinV */
	if (!stream->write_int(fileFormat->minversion)) {
		result = false;
		goto end;
	}

	/* CL */
	for (i = 0; i < fileFormat->numcl; i++) {
		if (!stream->write_int(fileFormat->cl[i])) {
			result = false;
			goto end;
		}
	}

	end: if (!result)
		GRK_ERROR("Error while writing ftyp data to stream");
	return result;
}

static bool jp2_write_jp2c(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);

	assert(stream->has_seek());

	uint64_t j2k_codestream_exit = stream->tell();
	if (!stream->seek(fileFormat->j2k_codestream_offset)) {
		GRK_ERROR("Failed to seek in the stream.");
		return false;
	}

	/* size of code stream */
	uint64_t actualLength = j2k_codestream_exit - fileFormat->j2k_codestream_offset;
	// initialize signalledLength to 0, indicating length was not known
	// when file was written
	uint32_t signaledLength = 0;
	if (fileFormat->needs_xl_jp2c_box_length)
		signaledLength = 1;
	else {
		if (actualLength < (uint64_t) 1 << 32)
			signaledLength = (uint32_t) actualLength;
	}
	if (!stream->write_int(signaledLength))
		return false;
	if (!stream->write_int(JP2_JP2C))
		return false;
	// XL box
	if (signaledLength == 1) {
		if (!stream->write_64(actualLength))
			return false;
	}
	if (!stream->seek(j2k_codestream_exit)) {
		GRK_ERROR("Failed to seek in the stream.");
		return false;
	}

	return true;
}

static bool jp2_write_jp(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);

	/* write box length */
	if (!stream->write_int(12))
		return false;
	/* writes box type */
	if (!stream->write_int(JP2_JP))
		return false;
	/* writes magic number*/
	if (!stream->write_int(0x0d0a870a))
		return false;
	return true;
}

/* ----------------------------------------------------------------------- */
/* JP2 compress interface                                             */
/* ----------------------------------------------------------------------- */

static bool jp2_init_end_header_writing(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);

	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_write_jp2c);
	//custom procedures here

	return true;
}

static bool jp2_init_end_header_reading(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);

	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_read_header_procedure);
	//custom procedures here

	return true;
}

static bool jp2_default_validation(FileFormat *fileFormat) {
	bool is_valid = true;
	uint32_t i;
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);

	/* JPEG2000 codec validation */

	/* STATE checking */
	/* make sure the state is at 0 */
	is_valid &= (fileFormat->jp2_state == JP2_STATE_NONE);

	/* make sure not reading a jp2h ???? WEIRD */
	is_valid &= (fileFormat->jp2_img_state == JP2_IMG_STATE_NONE);

	/* POINTER validation */
	/* make sure a j2k codec is present */
	is_valid &= (fileFormat->codeStream != nullptr);

	/* make sure a procedure list is present */
	is_valid &= (fileFormat->m_procedure_list != nullptr);

	/* make sure a validation list is present */
	is_valid &= (fileFormat->m_validation_list != nullptr);

	/* PARAMETER VALIDATION */
	/* number of components */
	/* precision */
	for (i = 0; i < fileFormat->numcomps; ++i) {
		is_valid &= ((fileFormat->comps[i].bpcc & 0x7FU) < 38U); /* 0 is valid, ignore sign for check */
	}
	/* METH */
	is_valid &= ((fileFormat->meth > 0) && (fileFormat->meth < 3));

	/* stream validation */
	/* back and forth is needed */
	is_valid &= stream->has_seek();

	return is_valid;
}

static bool jp2_read_header_procedure(FileFormat *fileFormat) {
	grk_jp2_box box;
	uint32_t nb_bytes_read;
	uint64_t last_data_size = GRK_BOX_SIZE;
	uint32_t current_data_size;
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);
	bool rc = true;

	auto current_data = (uint8_t*) grk_calloc(1, last_data_size);
	if (!current_data) {
		GRK_ERROR("Not enough memory to handle JPEG 2000 file header");
		return false;
	}

	try {
		while (jp2_read_box_hdr(&box, &nb_bytes_read, stream)) {
			/* is it the code stream box ? */
			if (box.type == JP2_JP2C) {
				if (fileFormat->jp2_state & JP2_STATE_HEADER) {
					fileFormat->jp2_state |= JP2_STATE_CODESTREAM;
					rc = true;
					goto cleanup;
				} else {
					GRK_ERROR("bad placed jpeg code stream");
					rc = false;
					goto cleanup;
				}
			}

			auto current_handler = jp2_find_handler(box.type);
			auto current_handler_misplaced = jp2_img_find_handler(box.type);
			current_data_size = (uint32_t) (box.length - nb_bytes_read);

			if ((current_handler != nullptr)
					|| (current_handler_misplaced != nullptr)) {
				if (current_handler == nullptr) {
					GRK_WARN(
							"Found a misplaced '%c%c%c%c' box outside jp2h box",
							(uint8_t) (box.type >> 24),
							(uint8_t) (box.type >> 16),
							(uint8_t) (box.type >> 8),
							(uint8_t) (box.type >> 0));
					if (fileFormat->jp2_state & JP2_STATE_HEADER) {
						/* read anyway, we already have jp2h */
						current_handler = current_handler_misplaced;
					} else {
						GRK_WARN(
								"JPEG2000 Header box not read yet, '%c%c%c%c' box will be ignored",
								(uint8_t) (box.type >> 24),
								(uint8_t) (box.type >> 16),
								(uint8_t) (box.type >> 8),
								(uint8_t) (box.type >> 0));
						fileFormat->jp2_state |= JP2_STATE_UNKNOWN;
						if (!stream->skip(current_data_size)) {
							GRK_WARN(
									"Problem with skipping JPEG2000 box, stream error");
							// ignore error and return true if code stream box has already been read
							// (we don't worry about any boxes after code stream)
							rc = (fileFormat->jp2_state & JP2_STATE_CODESTREAM) ?
									true : false;
							goto cleanup;
						}
						continue;
					}
				}
				if (current_data_size > stream->get_number_byte_left()) {
					/* do not even try to malloc if we can't read */
					GRK_ERROR(
							"Invalid box size %" PRIu64 " for box '%c%c%c%c'. Need %u bytes, %" PRIu64 " bytes remaining ",
							box.length, (uint8_t) (box.type >> 24),
							(uint8_t) (box.type >> 16),
							(uint8_t) (box.type >> 8),
							(uint8_t) (box.type >> 0), current_data_size,
							stream->get_number_byte_left());
					rc = false;
					goto cleanup;
				}
				if (current_data_size > last_data_size) {
					uint8_t *new_current_data = (uint8_t*) grk_realloc(
							current_data, current_data_size);
					if (!new_current_data) {
						GRK_ERROR("Not enough memory to handle JPEG 2000 box");
						rc = false;
						goto cleanup;
					}
					current_data = new_current_data;
					last_data_size = current_data_size;
				}

				nb_bytes_read = (uint32_t) stream->read(current_data,
						current_data_size);
				if (nb_bytes_read != current_data_size) {
					GRK_ERROR(
							"Problem with reading JPEG2000 box, stream error");
					rc = false;
					goto cleanup;
				}

				if (!current_handler->handler(fileFormat, current_data,
						current_data_size)) {
					rc = false;
					goto cleanup;
				}
			} else {
				if (!(fileFormat->jp2_state & JP2_STATE_SIGNATURE)) {
					GRK_ERROR(
							"Malformed JP2 file format: first box must be JPEG 2000 signature box");
					rc = false;
					goto cleanup;
				}
				if (!(fileFormat->jp2_state & JP2_STATE_FILE_TYPE)) {
					GRK_ERROR(
							"Malformed JP2 file format: second box must be file type box");
					rc = false;
					goto cleanup;

				}
				fileFormat->jp2_state |= JP2_STATE_UNKNOWN;
				if (!stream->skip(current_data_size)) {
					GRK_WARN(
							"Problem with skipping JPEG2000 box, stream error");
					// ignore error and return true if code stream box has already been read
					// (we don't worry about any boxes after code stream)
					rc = (fileFormat->jp2_state & JP2_STATE_CODESTREAM) ? true : false;
					goto cleanup;
				}
			}
		}
	} catch (CorruptJP2BoxException &ex) {
		rc = false;
	}
	cleanup: grk_free(current_data);
	return rc;
}

/**
 * Executes the given procedures on the given codec.
 *
 * @param	procs	the list of procedures to execute
 * @param	fileFormat					JPEG 2000 code stream to execute the procedures on.
 * @param	stream					the stream to execute the procedures on.
 *
 * @return	true				if all the procedures were successfully executed.
 */
static bool jp2_exec(FileFormat *fileFormat, std::vector<jp2_procedure> *procs) {
	bool result = true;

	assert(procs);
	assert(fileFormat != nullptr);

	for (auto it = procs->begin(); it != procs->end(); ++it) {
		auto p = (jp2_procedure) *it;
		result = result && (p)(fileFormat);
	}
	procs->clear();

	return result;
}

static const grk_jp2_header_handler* jp2_find_handler(uint32_t id) {
	auto handler_size =
			sizeof(jp2_header) / sizeof(grk_jp2_header_handler);

	for (uint32_t i = 0; i < handler_size; ++i) {
		if (jp2_header[i].id == id)
			return &jp2_header[i];
	}

	return nullptr;
}

/**
 * Finds the image execution function related to the given box id.
 *
 * @param	id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
static const grk_jp2_header_handler* jp2_img_find_handler(uint32_t id) {
	auto handler_size =
			sizeof(jp2_img_header) / sizeof(grk_jp2_header_handler);

	for (uint32_t i = 0; i < handler_size; ++i) {
		if (jp2_img_header[i].id == id)
			return &jp2_img_header[i];
	}

	return nullptr;
}

/**
 * Reads a JPEG 2000 file signature box.
 *
 * @param	p_header_data	the data contained in the signature box.
 * @param	fileFormat				JPEG 2000 code stream.
 * @param	header_size	the size of the data contained in the signature box.
 .
 *
 * @return true if the file signature box is valid.
 */
static bool jp2_read_jp(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_size)	{
	uint32_t magic_number;

	assert(p_header_data != nullptr);
	assert(fileFormat != nullptr);

	if (fileFormat->jp2_state != JP2_STATE_NONE) {
		GRK_ERROR("The signature box must be the first box in the file.");
		return false;
	}

	/* assure length of data is correct (4 -> magic number) */
	if (header_size != 4) {
		GRK_ERROR("Error with JP signature Box size");
		return false;
	}

	/* rearrange data */
	grk_read<uint32_t>(p_header_data, &magic_number, 4);
	if (magic_number != 0x0d0a870a) {
		GRK_ERROR("Error with JP Signature : bad magic number");
		return false;
	}
	fileFormat->jp2_state |= JP2_STATE_SIGNATURE;

	return true;
}

/**
 * Reads a a FTYP box - File type box
 *
 * @param	p_header_data	the data contained in the FTYP box.
 * @param	fileFormat				JPEG 2000 code stream.
 * @param	header_size	the size of the data contained in the FTYP box.
 .
 *
 * @return true if the FTYP box is valid.
 */
static bool jp2_read_ftyp(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t header_size) {
	uint32_t i, remaining_bytes;

	assert(p_header_data != nullptr);
	assert(fileFormat != nullptr);

	if (fileFormat->jp2_state != JP2_STATE_SIGNATURE) {
		GRK_ERROR("The ftyp box must be the second box in the file.");
		return false;
	}

	/* assure length of data is correct */
	if (header_size < 8) {
		GRK_ERROR("Error with FTYP signature Box size");
		return false;
	}

	grk_read<uint32_t>(p_header_data, &fileFormat->brand, 4); /* BR */
	p_header_data += 4;

	grk_read<uint32_t>(p_header_data, &fileFormat->minversion, 4); /* MinV */
	p_header_data += 4;

	remaining_bytes = header_size - 8;

	/* the number of remaining bytes should be a multiple of 4 */
	if ((remaining_bytes & 0x3) != 0) {
		GRK_ERROR("Error with FTYP signature Box size");
		return false;
	}

	/* div by 4 */
	fileFormat->numcl = remaining_bytes >> 2;
	if (fileFormat->numcl) {
		fileFormat->cl = (uint32_t*) grk_calloc(fileFormat->numcl, sizeof(uint32_t));
		if (fileFormat->cl == nullptr) {
			GRK_ERROR("Not enough memory with FTYP Box");
			return false;
		}
	}
	for (i = 0; i < fileFormat->numcl; ++i) {
		grk_read<uint32_t>(p_header_data, &fileFormat->cl[i], 4); /* CLi */
		p_header_data += 4;
	}
	fileFormat->jp2_state |= JP2_STATE_FILE_TYPE;

	return true;
}

static bool jp2_skip_jp2c(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);
    auto stream = fileFormat->codeStream->getStream();
	assert(stream != nullptr);

	fileFormat->j2k_codestream_offset = stream->tell();
	int64_t skip_bytes = fileFormat->needs_xl_jp2c_box_length ? 16 : 8;

	return stream->skip(skip_bytes);
}

/**
 * Reads the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param	p_header_data	the data contained in the file header box.
 * @param	fileFormat				JPEG 2000 code stream.
 * @param	header_size	the size of the data contained in the file header box.
 .
 *
 * @return true if the JP2 Header box was successfully recognized.
 */
static bool jp2_read_jp2h(FileFormat *fileFormat, uint8_t *p_header_data,
		uint32_t hdr_size) {
	uint32_t box_size = 0;
	grk_jp2_box box;
	bool has_ihdr = 0;

	assert(p_header_data != nullptr);
	assert(fileFormat != nullptr);

	/* make sure the box is well placed */
	if ((fileFormat->jp2_state & JP2_STATE_FILE_TYPE) != JP2_STATE_FILE_TYPE) {
		GRK_ERROR("The  box must be the first box in the file.");
		return false;
	}

	fileFormat->jp2_img_state = JP2_IMG_STATE_NONE;

	int64_t header_size = (int64_t)hdr_size;

	/* iterate while remaining data */
	while (header_size) {
		if (!jp2_read_box(&box, p_header_data, &box_size, (uint64_t)header_size)) {
			GRK_ERROR("Stream error while reading JP2 Header box");
			return false;
		}

		auto current_handler = jp2_img_find_handler(box.type);
		uint32_t current_data_size = (uint32_t) (box.length - box_size);
		p_header_data += box_size;

		if (current_handler != nullptr) {
			if (!current_handler->handler(fileFormat, p_header_data,
					current_data_size)) {
				return false;
			}
		} else {
			fileFormat->jp2_img_state |= JP2_IMG_STATE_UNKNOWN;
		}

		if (box.type == JP2_IHDR)
			has_ihdr = 1;

		p_header_data += current_data_size;
		header_size -= box.length;
		if (header_size < 0) {
			GRK_ERROR("Error reading JP2 header box");
			return false;
		}
	}

	if (has_ihdr == 0) {
		GRK_ERROR("Stream error while reading JP2 Header box: no 'ihdr' box.");
		return false;
	}
	fileFormat->jp2_state |= JP2_STATE_HEADER;

	return true;
}

static bool jp2_read_box(grk_jp2_box *box, uint8_t *p_data,
		uint32_t *p_number_bytes_read, uint64_t p_box_max_size) {
	assert(p_data != nullptr);
	assert(box != nullptr);
	assert(p_number_bytes_read != nullptr);

	if (p_box_max_size < 8) {
		GRK_ERROR("box must be at least 8 bytes in size");
		return false;
	}

	/* process read data */
	uint32_t L = 0;
	grk_read<uint32_t>(p_data, &L, 4);
	box->length = L;
	p_data += 4;

	grk_read<uint32_t>(p_data, &box->type, 4);
	p_data += 4;

	*p_number_bytes_read = 8;

	/* read XL parameter */
	if (box->length == 1) {
		if (p_box_max_size < 16) {
			GRK_ERROR("Cannot handle XL box of less than 16 bytes");
			return false;
		}
		grk_read<uint64_t>(p_data, &box->length);
		p_data += 8;
		*p_number_bytes_read += 8;

		if (box->length == 0) {
			GRK_ERROR("Cannot handle box of undefined sizes");
			return false;
		}
	} else if (box->length == 0) {
		GRK_ERROR("Cannot handle box of undefined sizes");
		return false;
	}
	if (box->length < *p_number_bytes_read) {
		GRK_ERROR("Box length is inconsistent.");
		return false;
	}
	if (box->length > p_box_max_size) {
		GRK_ERROR(
				"Stream error while reading JP2 Header box: box length is inconsistent.");
		return false;
	}
	return true;
}

static bool jp2_init_compress_validation(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);
	fileFormat->m_validation_list->push_back((jp2_procedure) jp2_default_validation);

	return true;
}

static bool jp2_init_decompress_validation(FileFormat *fileFormat) {
	(void) fileFormat;
	assert(fileFormat != nullptr);

	/* add your custom validation procedure */

	return true;
}

static bool jp2_init_header_writing(FileFormat *fileFormat) {

	assert(fileFormat != nullptr);

	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_write_jp);
	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_write_ftyp);
	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_write_jp2h);
	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_write_uuids);
	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_skip_jp2c);
	//custom procedures here

	return true;
}

static bool jp2_init_header_reading(FileFormat *fileFormat) {
	assert(fileFormat != nullptr);

	fileFormat->m_procedure_list->push_back((jp2_procedure) jp2_read_header_procedure);
	//custom procedures here

	return true;
}

FileFormat::FileFormat(bool isDecoder, BufferedStream *stream) : codeStream(new CodeStream(isDecoder,stream)),
										m_validation_list(new std::vector<jp2_procedure>()),
										m_procedure_list(new std::vector<jp2_procedure>()),
										w(0),
										h(0),
										numcomps(0),
										bpc(0),
										C(0),
										UnkC(0),
										IPR(0),
										meth(0),
										approx(0),
										enumcs(GRK_ENUM_CLRSPC_UNKNOWN),
										precedence(0),
										brand(0),
										minversion(0),
										numcl(0),
										cl(nullptr),
										comps(nullptr),
										j2k_codestream_offset(0),
										needs_xl_jp2c_box_length(false),
										jp2_state(0),
										jp2_img_state(0),
										has_capture_resolution(false),
										has_display_resolution(false),
										numUuids(0),
										m_stream(stream)
{
	for (uint32_t i = 0; i < 2; ++i) {
		capture_resolution[i] = 0;
		display_resolution[i] = 0;
	}

	/* Color structure */
	color.icc_profile_buf = nullptr;
	color.icc_profile_len = 0;
	color.jp2_cdef = nullptr;
	color.jp2_pclr = nullptr;
	color.jp2_has_colour_specification_box = 0;

}

FileFormat::~FileFormat() {
	delete codeStream;
	grk_free(comps);
	grk_free(cl);
	delete[] color.icc_profile_buf;
	if (color.jp2_cdef) {
		grk_free(color.jp2_cdef->info);
		grk_free(color.jp2_cdef);
	}
	jp2_free_pclr(&color);
	delete m_validation_list;
	delete m_procedure_list;
	xml.dealloc();
	for (uint32_t i = 0; i < numUuids; ++i)
		(uuids + i)->dealloc();
}

/** Main header reading function handler */
bool FileFormat::read_header(grk_header_info  *header_info, grk_image **p_image){
	/* customization of the validation */
	if (!jp2_init_decompress_validation(this))
		return false;

	/* customization of the encoding */
	if (!jp2_init_header_reading(this))
		return false;

	/* validation of the parameters codec */
	if (!jp2_exec(this, m_validation_list))
		return false;

	/* read header */
	if (!jp2_exec(this, m_procedure_list))
		return false;

	if (header_info) {
		header_info->enumcs = enumcs;
		header_info->color = color;

		header_info->xml_data = xml.buffer;
		header_info->xml_data_len = xml.len;

		if (has_capture_resolution) {
			header_info->has_capture_resolution = true;
			for (int i = 0; i < 2; ++i)
				header_info->capture_resolution[i] = capture_resolution[i];
		}

		if (has_display_resolution) {
			header_info->has_display_resolution = true;
			for (int i = 0; i < 2; ++i)
				header_info->display_resolution[i] = display_resolution[i];
		}
	}
	if (!codeStream->read_header(header_info, p_image))
		return false;

	if (*p_image) {
		for (int i = 0; i < 2; ++i) {
			(*p_image)->capture_resolution[i] = capture_resolution[i];
			(*p_image)->display_resolution[i] = display_resolution[i];
		}
	}
	return true;
}

/** Decoding function */
bool FileFormat::decompress( grk_plugin_tile *tile,	 grk_image *p_image){

	if (!p_image)
		return false;

	/* J2K decoding */
	if (!codeStream->decompress(tile, p_image)) {
		GRK_ERROR("Failed to decompress JP2 file");
		return false;
	}

	if (!jp2_check_color(p_image, &(color)))
		return false;

	/* Set Image Color Space */
	switch (enumcs) {
	case GRK_ENUM_CLRSPC_CMYK:
		p_image->color_space = GRK_CLRSPC_CMYK;
		break;
	case GRK_ENUM_CLRSPC_CIE:
		if (color.icc_profile_buf) {
			if (((uint32_t*) color.icc_profile_buf)[1]
					== GRK_DEFAULT_CIELAB_SPACE)
				p_image->color_space = GRK_CLRSPC_DEFAULT_CIE;
			else
				p_image->color_space = GRK_CLRSPC_CUSTOM_CIE;
		} else {
			GRK_ERROR("CIE Lab image requires ICC profile buffer set");
			return false;
		}
		break;
	case GRK_ENUM_CLRSPC_SRGB:
		p_image->color_space = GRK_CLRSPC_SRGB;
		break;
	case GRK_ENUM_CLRSPC_GRAY:
		p_image->color_space = GRK_CLRSPC_GRAY;
		break;
	case GRK_ENUM_CLRSPC_SYCC:
		p_image->color_space = GRK_CLRSPC_SYCC;
		break;
	case GRK_ENUM_CLRSPC_EYCC:
		p_image->color_space = GRK_CLRSPC_EYCC;
		break;
	default:
		p_image->color_space = GRK_CLRSPC_UNKNOWN;
		break;
	}
	if (meth == 2 && color.icc_profile_buf)
		p_image->color_space = GRK_CLRSPC_ICC;

	if (color.jp2_pclr) {
		/* Part 1, I.5.3.4: Either both or none : */
		if (!color.jp2_pclr->cmap)
			jp2_free_pclr(&(color));
		else {
			if (!jp2_apply_pclr(p_image, &(color)))
				return false;
		}
	}

	/* Apply channel definitions if needed */
	if (color.jp2_cdef) {
		jp2_apply_cdef(p_image, &(color));
	}

	// retrieve icc profile
	if (color.icc_profile_buf) {
		p_image->icc_profile_buf = color.icc_profile_buf;
		p_image->icc_profile_len = color.icc_profile_len;
		color.icc_profile_buf = nullptr;
		color.icc_profile_len = 0;
	}

	// retrieve special uuids
	for (uint32_t i = 0; i < numUuids; ++i) {
		auto uuid = uuids + i;
		if (memcmp(uuid->uuid, IPTC_UUID, 16) == 0) {
			p_image->iptc_buf = uuid->buffer;
			p_image->iptc_len = uuid->len;
			uuid->buffer = nullptr;
			uuid->len = 0;
		} else if (memcmp(uuid->uuid, XMP_UUID, 16) == 0) {
			p_image->xmp_buf = uuid->buffer;
			p_image->xmp_len = uuid->len;
			uuid->buffer = nullptr;
			uuid->len = 0;
		}
	}

	return true;
}

/** Reading function used after code stream if necessary */
bool FileFormat::end_decompress(void){
	/* customization of the end encoding */
	if (!jp2_init_end_header_reading(this))
		return false;

	/* write header */
	if (!jp2_exec(this, m_procedure_list))
		return false;

	return codeStream->end_decompress();
}

/** Setup decoder function handler */
void FileFormat::init_decompress(grk_dparameters  *parameters){
	/* set up the J2K codec */
	codeStream->init_decompress(parameters);

	/* further JP2 initializations go here */
	color.jp2_has_colour_specification_box = 0;
}

bool FileFormat::set_decompress_area(grk_image *p_image,
					uint32_t start_x,
					uint32_t start_y,
					uint32_t end_x,
					uint32_t end_y){
	return codeStream->set_decompress_area(p_image, start_x, start_y, end_x, end_y);
}

bool FileFormat::start_compress(void){
	/* customization of the validation */
	if (!jp2_init_compress_validation(this))
		return false;

	/* validation of the parameters codec */
	if (!jp2_exec(this, m_validation_list))
		return false;

	/* customization of the encoding */
	if (!jp2_init_header_writing(this))
		return false;

	// estimate if codec stream may be larger than 2^32 bytes
	auto p_image = codeStream->m_input_image;
	uint64_t image_size = 0;
	for (auto i = 0U; i < p_image->numcomps; ++i) {
		auto comp = p_image->comps + i;
		image_size += (uint64_t) comp->w * comp->h * ((comp->prec + 7) / 8);
	}
	needs_xl_jp2c_box_length =
			(image_size > (uint64_t) 1 << 30) ? true : false;

	/* write header */
	if (!jp2_exec(this, m_procedure_list))
		return false;

	return codeStream->start_compress();
}

bool FileFormat::init_compress(grk_cparameters  *parameters,grk_image *image){
	uint32_t i;
	uint32_t depth_0;
	uint32_t sign = 0;
	uint32_t alpha_count = 0;
	uint32_t color_channels = 0U;

	if (!parameters || !image)
		return false;

	/* set up the J2K codec */
	/* ------------------- */
	if (codeStream->init_compress(parameters, image) == false)
		return false;

	/* set up the JP2 codec */
	/* ------------------- */

	/* Profile box */

	brand = JP2_JP2; /* BR */
	minversion = 0; /* MinV */
	numcl = 1;
	cl = (uint32_t*) grk_malloc(sizeof(uint32_t));
	if (!cl) {
		GRK_ERROR("Not enough memory when set up the JP2 encoder");
		return false;
	}
	cl[0] = JP2_JP2; /* CL0 : JP2 */

	/* Image Header box */
	numcomps = image->numcomps; /* NC */
	comps = (grk_jp2_comps*) grk_malloc(
			numcomps * sizeof(grk_jp2_comps));
	if (!comps) {
		GRK_ERROR("Not enough memory when set up the JP2 encoder");
		return false;
	}

	h = image->y1 - image->y0; /* HEIGHT */
	w = image->x1 - image->x0; /* WIDTH */
	depth_0 = image->comps[0].prec - 1;
	sign = image->comps[0].sgnd;
	bpc = depth_0 + (sign << 7);
	for (i = 1; i < image->numcomps; i++) {
		uint32_t depth = image->comps[i].prec - 1;
		sign = image->comps[i].sgnd;
		if (depth_0 != depth)
			bpc = 255;
	}
	C = 7; /* C : Always 7 */
	UnkC = 0; /* UnkC, colorspace specified in colr box */
	IPR = 0; /* IPR, no intellectual property */

	/* BitsPerComponent box */
	for (i = 0; i < image->numcomps; i++) {
		comps[i].bpcc = (uint8_t)(image->comps[i].prec - 1);
		if (image->comps[i].sgnd)
			comps[i].bpcc = (uint8_t)(comps[i].bpcc + (1 << 7));
	}

	/* Colour Specification box */
	if (image->color_space == GRK_CLRSPC_ICC) {
		meth = 2;
		enumcs = GRK_ENUM_CLRSPC_UNKNOWN;
		if (image->icc_profile_buf) {
			// clean up existing icc profile in this struct
			if (color.icc_profile_buf) {
				delete[] color.icc_profile_buf;
				color.icc_profile_buf = nullptr;
			}
			// copy icc profile from image to this struct
			color.icc_profile_len = image->icc_profile_len;
			color.icc_profile_buf = new uint8_t[color.icc_profile_len];
			memcpy(color.icc_profile_buf, image->icc_profile_buf,
					color.icc_profile_len);
		}
	} else {
		meth = 1;
		if (image->color_space == GRK_CLRSPC_CMYK)
			enumcs = GRK_ENUM_CLRSPC_CMYK;
		else if (image->color_space == GRK_CLRSPC_DEFAULT_CIE)
			enumcs = GRK_ENUM_CLRSPC_CIE;
		else if (image->color_space == GRK_CLRSPC_SRGB)
			enumcs = GRK_ENUM_CLRSPC_SRGB; /* sRGB as defined by IEC 61966-2-1 */
		else if (image->color_space == GRK_CLRSPC_GRAY)
			enumcs = GRK_ENUM_CLRSPC_GRAY; /* greyscale */
		else if (image->color_space == GRK_CLRSPC_SYCC)
			enumcs = GRK_ENUM_CLRSPC_SYCC; /* YUV */
		else if (image->color_space == GRK_CLRSPC_EYCC)
			enumcs = GRK_ENUM_CLRSPC_EYCC; /* YUV */
	}

	//transfer buffer to uuid
	if (image->iptc_len && image->iptc_buf) {
		uuids[numUuids++] = grk_jp2_uuid(IPTC_UUID, image->iptc_buf,
				image->iptc_len, true);
		image->iptc_buf = nullptr;
		image->iptc_len = 0;
	}

	//transfer buffer to uuid
	if (image->xmp_len && image->xmp_buf) {
		uuids[numUuids++] = grk_jp2_uuid(XMP_UUID, image->xmp_buf,
				image->xmp_len, true);
		image->xmp_buf = nullptr;
		image->xmp_len = 0;
	}

	/* Component Definition box */
	for (i = 0; i < image->numcomps; i++) {
		if (image->comps[i].type != GRK_COMPONENT_TYPE_COLOUR) {
			alpha_count++;
			// technically, this is an error, but we will let it pass
			if (image->comps[i].sgnd)
				GRK_WARN("signed alpha channel %u",i);
		}
	}

	switch (enumcs) {
	case GRK_ENUM_CLRSPC_CMYK:
		color_channels = 4;
		break;
	case GRK_ENUM_CLRSPC_CIE:
	case GRK_ENUM_CLRSPC_SRGB:
	case GRK_ENUM_CLRSPC_SYCC:
	case GRK_ENUM_CLRSPC_EYCC:
		color_channels = 3;
		break;
	case GRK_ENUM_CLRSPC_GRAY:
		color_channels = 1;
		break;
	default:
		// assume that last channel is alpha
		if (alpha_count) {
			if (image->numcomps > 1){
				color_channels = image->numcomps - 1;
				alpha_count = 1U;
			}
			else {
				alpha_count = 0U;
			}
		}
		break;
	}
	if (alpha_count) {
		color.jp2_cdef = (grk_jp2_cdef*) grk_malloc(sizeof(grk_jp2_cdef));
		if (!color.jp2_cdef) {
			GRK_ERROR("Not enough memory to set up the JP2 encoder");
			return false;
		}
		/* no memset needed, all values will be overwritten except if
		 * color.jp2_cdef->info allocation fails, */
		/* in which case color.jp2_cdef->info will be nullptr => valid for destruction */
		color.jp2_cdef->info = (grk_jp2_cdef_info*) grk_malloc(
				image->numcomps * sizeof(grk_jp2_cdef_info));
		if (!color.jp2_cdef->info) {
			/* memory will be freed by jp2_destroy */
			GRK_ERROR("Not enough memory to set up the JP2 encoder");
			return false;
		}
		color.jp2_cdef->n = (uint16_t) image->numcomps; /* cast is valid : image->numcomps [1,16384] */
		for (i = 0U; i < color_channels; i++) {
			color.jp2_cdef->info[i].cn = (uint16_t) i; /* cast is valid : image->numcomps [1,16384] */
			color.jp2_cdef->info[i].typ = GRK_COMPONENT_TYPE_COLOUR;
			color.jp2_cdef->info[i].asoc = (uint16_t) (i + 1U); /* No overflow + cast is valid : image->numcomps [1,16384] */
		}
		for (; i < image->numcomps; i++) {
			color.jp2_cdef->info[i].cn = (uint16_t) i; /* cast is valid : image->numcomps [1,16384] */
			color.jp2_cdef->info[i].typ = image->comps[i].type;
			color.jp2_cdef->info[i].asoc = image->comps[i].association;
		}
	}
	/*********************************************/

	precedence = 0; /* PRECEDENCE */
	approx = 0; /* APPROX */

	has_capture_resolution = parameters->write_capture_resolution ||
											parameters->write_capture_resolution_from_file;
	if (parameters->write_capture_resolution) {
		for (i = 0; i < 2; ++i) {
			capture_resolution[i] = parameters->capture_resolution[i];
		}
	} else if (parameters->write_capture_resolution_from_file) {
		for (i = 0; i < 2; ++i) {
			capture_resolution[i] =
					parameters->capture_resolution_from_file[i];
		}
	}
	if (parameters->write_display_resolution) {
		has_display_resolution = true;
		display_resolution[0] = parameters->display_resolution[0];
		display_resolution[1] = parameters->display_resolution[1];
		//if display resolution equals (0,0), then use capture resolution
		//if available
		if (parameters->display_resolution[0] == 0 &&
				parameters->display_resolution[1] == 0) {
			if (has_capture_resolution) {
				display_resolution[0] = parameters->capture_resolution[0];
				display_resolution[1] = parameters->capture_resolution[1];
			} else {
				has_display_resolution = false;
			}
		}
	}

	return true;
}

bool FileFormat::compress(grk_plugin_tile* tile){

	return codeStream->compress(tile);
}

bool FileFormat::compress_tile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size){

	return codeStream->compress_tile(tile_index, p_data, data_size);
}

bool FileFormat::end_compress(void){
	/* customization of the end encoding */
	if (!jp2_init_end_header_writing(this))
		return false;
	if (!codeStream->end_compress())
		return false;

	/* write header */
	return jp2_exec(this, m_procedure_list);
}

bool FileFormat::decompress_tile(grk_image *p_image,uint16_t tile_index) {
	if (!p_image)
		return false;

	if (!codeStream->decompress_tile(p_image, tile_index)) {
		GRK_ERROR("Failed to decompress JP2 file");
		return false;
	}

	if (!jp2_check_color(p_image, &(color)))
		return false;

	/* Set Image Color Space */
	if (enumcs == GRK_ENUM_CLRSPC_CMYK)
		p_image->color_space = GRK_CLRSPC_CMYK;
	else if (enumcs == GRK_ENUM_CLRSPC_SRGB)
		p_image->color_space = GRK_CLRSPC_SRGB;
	else if (enumcs == GRK_ENUM_CLRSPC_GRAY)
		p_image->color_space = GRK_CLRSPC_GRAY;
	else if (enumcs == GRK_ENUM_CLRSPC_SYCC)
		p_image->color_space = GRK_CLRSPC_SYCC;
	else if (enumcs == GRK_ENUM_CLRSPC_EYCC)
		p_image->color_space = GRK_CLRSPC_EYCC;
	else
		p_image->color_space = GRK_CLRSPC_UNKNOWN;

	if (color.jp2_pclr) {
		/* Part 1, I.5.3.4: Either both or none : */
		if (!color.jp2_pclr->cmap)
			jp2_free_pclr(&(color));
		else {
			if (!jp2_apply_pclr(p_image, &(color)))
				return false;
		}
	}

	/* Apply channel definitions if needed */
	if (color.jp2_cdef)
		jp2_apply_cdef(p_image, &(color));

	if (color.icc_profile_buf) {
		p_image->icc_profile_buf = color.icc_profile_buf;
		p_image->icc_profile_len = color.icc_profile_len;
		color.icc_profile_buf = nullptr;
		color.icc_profile_len = 0;
		p_image->color_space = GRK_CLRSPC_ICC;
	}

	return true;
}


void FileFormat::dump(int32_t flag, FILE *out_stream){
	j2k_dump(codeStream, flag, out_stream);
}

grk_codestream_info_v2* FileFormat::get_cstr_info(void){

	return j2k_get_cstr_info(codeStream);
}

grk_codestream_index* FileFormat::get_cstr_index(void){

	return j2k_get_cstr_index(codeStream);
}




}
