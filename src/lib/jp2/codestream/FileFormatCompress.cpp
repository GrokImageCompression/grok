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

 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
typedef std::function<uint8_t*(uint32_t* len)> WRITE_FUNC;
struct BoxWriteHandler
{
	BoxWriteHandler() : handler(nullptr), data_(nullptr), size_(0) {}
	WRITE_FUNC handler;
	uint8_t* data_;
	uint32_t size_;
};

FileFormatCompress::FileFormatCompress(IBufferedStream* stream)
	: FileFormat(), codeStream(new CodeStreamCompress(stream)), needs_xl_jp2c_box_length(false),
	  j2k_codestream_offset(0)
{}
FileFormatCompress::~FileFormatCompress()
{
	delete codeStream;
}
bool FileFormatCompress::write_jp(void)
{
	auto stream = codeStream->getStream();
	assert(stream != nullptr);

	/* write box length */
	if(!stream->writeInt(12))
		return false;
	/* writes box type */
	if(!stream->writeInt(JP2_JP))
		return false;
	/* writes magic number*/
	if(!stream->writeInt(0x0d0a870a))
		return false;
	return true;
}
bool FileFormatCompress::write_jp2c(void)
{
	auto stream = codeStream->getStream();
	assert(stream != nullptr);

	assert(stream->hasSeek());

	uint64_t j2k_codestream_exit = stream->tell();
	if(!stream->seek(j2k_codestream_offset))
	{
		GRK_ERROR("Failed to seek in the stream.");
		return false;
	}

	/* size of code stream */
	uint64_t actualLength = j2k_codestream_exit - j2k_codestream_offset;
	// initialize signalledLength to 0, indicating length was not known
	// when file was written
	uint32_t signaledLength = 0;
	if(needs_xl_jp2c_box_length)
		signaledLength = 1;
	else
	{
		if(actualLength < (uint64_t)1 << 32)
			signaledLength = (uint32_t)actualLength;
	}
	if(!stream->writeInt(signaledLength))
		return false;
	if(!stream->writeInt(JP2_JP2C))
		return false;
	// XL box
	if(signaledLength == 1)
	{
		if(!stream->write64(actualLength))
			return false;
	}
	if(!stream->seek(j2k_codestream_exit))
	{
		GRK_ERROR("Failed to seek in the stream.");
		return false;
	}

	return true;
}
bool FileFormatCompress::write_ftyp(void)
{
	auto stream = codeStream->getStream();
	assert(stream != nullptr);

	uint32_t i;
	uint32_t ftyp_size = 16 + 4 * numcl;
	bool result = true;

	if(!stream->writeInt(ftyp_size))
	{
		result = false;
		goto end;
	}
	if(!stream->writeInt(JP2_FTYP))
	{
		result = false;
		goto end;
	}
	if(!stream->writeInt(brand))
	{
		result = false;
		goto end;
	}
	/* MinV */
	if(!stream->writeInt(minversion))
	{
		result = false;
		goto end;
	}

	/* CL */
	for(i = 0; i < numcl; i++)
	{
		if(!stream->writeInt(cl[i]))
		{
			result = false;
			goto end;
		}
	}

end:
	if(!result)
		GRK_ERROR("Error while writing ftyp data to stream");
	return result;
}
bool FileFormatCompress::write_uuids(void)
{
	auto stream = codeStream->getStream();
	assert(stream != nullptr);
	;

	// write the uuids
	for(size_t i = 0; i < numUuids; ++i)
	{
		auto uuid = uuids + i;
		if(uuid->buf && uuid->len)
		{
			/* write box size */
			stream->writeInt((uint32_t)(8 + 16 + uuid->len));

			/* JP2_UUID */
			stream->writeInt(JP2_UUID);

			/* uuid  */
			stream->writeBytes(uuid->uuid, 16);

			/* uuid data */
			stream->writeBytes(uuid->buf, (uint32_t)uuid->len);
		}
	}
	return true;
}
bool FileFormatCompress::write_jp2h(void)
{
	BoxWriteHandler writers[32];
	int32_t i, nb_writers = 0;
	/* size of data for super box*/
	uint32_t jp2h_size = 8;
	bool result = true;
	auto stream = codeStream->getStream();
	assert(stream != nullptr);

	writers[nb_writers++].handler =
		std::bind(&FileFormatCompress::write_ihdr, this, std::placeholders::_1);
	if(bpc == 0xFF)
		writers[nb_writers++].handler =
			std::bind(&FileFormatCompress::write_bpc, this, std::placeholders::_1);
	writers[nb_writers++].handler =
		std::bind(&FileFormatCompress::write_colr, this, std::placeholders::_1);
	if(color.channel_definition)
		writers[nb_writers++].handler =
			std::bind(&FileFormatCompress::write_channel_definition, this, std::placeholders::_1);
	if(color.palette)
	{
		writers[nb_writers++].handler =
			std::bind(&FileFormatCompress::write_palette_clr, this, std::placeholders::_1);
		writers[nb_writers++].handler =
			std::bind(&FileFormatCompress::write_component_mapping, this, std::placeholders::_1);
	}
	if(has_display_resolution || has_capture_resolution)
	{
		bool storeCapture = capture_resolution[0] > 0 && capture_resolution[1] > 0;
		bool storeDisplay = display_resolution[0] > 0 && display_resolution[1] > 0;
		if(storeCapture || storeDisplay)
			writers[nb_writers++].handler =
				std::bind(&FileFormatCompress::write_res, this, std::placeholders::_1);
	}
	if(xml.buf && xml.len)
		writers[nb_writers++].handler =
			std::bind(&FileFormatCompress::write_xml, this, std::placeholders::_1);
	for(i = 0; i < nb_writers; ++i)
	{
		auto current_writer = writers + i;
		current_writer->data_ = current_writer->handler(&(current_writer->size_));
		if(current_writer->data_ == nullptr)
		{
			GRK_ERROR("Not enough memory to hold JP2 Header data");
			result = false;
			break;
		}
		jp2h_size += current_writer->size_;
	}

	if(!result)
	{
		for(i = 0; i < nb_writers; ++i)
		{
			auto current_writer = writers + i;
			grkFree(current_writer->data_);
		}
		return false;
	}

	/* write super box size */
	if(!stream->writeInt(jp2h_size))
		result = false;
	if(!stream->writeInt(JP2_JP2H))
		result = false;

	if(result)
	{
		for(i = 0; i < nb_writers; ++i)
		{
			auto current_writer = writers + i;
			if(stream->writeBytes(current_writer->data_, current_writer->size_) !=
			   current_writer->size_)
			{
				result = false;
				break;
			}
		}
	}
	/* cleanup */
	for(i = 0; i < nb_writers; ++i)
	{
		auto current_writer = writers + i;
		grkFree(current_writer->data_);
	}

	return result;
}
uint8_t* FileFormatCompress::write_palette_clr(uint32_t* p_nb_bytes_written)
{
	auto palette = color.palette;
	assert(palette);

	uint32_t bytesPerEntry = 0;
	for(uint32_t i = 0; i < palette->num_channels; ++i)
		bytesPerEntry += ((palette->channel_prec[i] + 7U) / 8U);

	uint32_t boxSize =
		4U + 4U + 2U + 1U + palette->num_channels + bytesPerEntry * palette->num_entries;

	uint8_t* paletteBuf = (uint8_t*)grkMalloc(boxSize);
	uint8_t* palette_ptr = paletteBuf;

	/* box size */
	grk_write<uint32_t>(palette_ptr, boxSize);
	palette_ptr += 4;

	/* PCLR */
	grk_write<uint32_t>(palette_ptr, JP2_PCLR);
	palette_ptr += 4;

	// number of LUT entries
	grk_write<uint16_t>(palette_ptr, palette->num_entries);
	palette_ptr += 2;

	// number of channels
	grk_write<uint8_t>(palette_ptr++, palette->num_channels);

	for(uint8_t i = 0; i < palette->num_channels; ++i)
		grk_write<uint8_t>(palette_ptr++, (uint8_t)(palette->channel_prec[i] - 1U)); // Bi

	// LUT values for all components
	auto lut_ptr = palette->lut;
	for(uint16_t j = 0; j < palette->num_entries; ++j)
	{
		for(uint8_t i = 0; i < palette->num_channels; ++i)
		{
			uint32_t bytes_to_write = (uint32_t)((palette->channel_prec[i] + 7U) >> 3);
			grk_write<int32_t>(palette_ptr, *lut_ptr, bytes_to_write); /* Cji */
			lut_ptr++;
			palette_ptr += bytes_to_write;
		}
	}

	*p_nb_bytes_written = boxSize;

	return paletteBuf;
}
uint8_t* FileFormatCompress::write_component_mapping(uint32_t* p_nb_bytes_written)
{
	auto palette = color.palette;
	uint32_t boxSize = 4 + 4 + palette->num_channels * 4U;

	uint8_t* cmapBuf = (uint8_t*)grkMalloc(boxSize);
	uint8_t* cmapPtr = cmapBuf;

	/* box size */
	grk_write<uint32_t>(cmapPtr, boxSize);
	cmapPtr += 4;

	/* CMAP */
	grk_write<uint32_t>(cmapPtr, JP2_CMAP);
	cmapPtr += 4;

	for(uint32_t i = 0; i < palette->num_channels; ++i)
	{
		auto map = palette->component_mapping + i;
		grk_write<uint16_t>(cmapPtr, map->component_index); /* CMP^i */
		cmapPtr += 2;
		grk_write<uint8_t>(cmapPtr++, map->mapping_type); /* MTYP^i */
		grk_write<uint8_t>(cmapPtr++, map->palette_column); /* PCOL^i */
	}

	*p_nb_bytes_written = boxSize;

	return cmapBuf;
}
uint8_t* FileFormatCompress::write_colr(uint32_t* p_nb_bytes_written)
{
	/* room for 8 bytes for box 3 for common data and variable upon profile*/
	uint32_t colr_size = 11;
	assert(p_nb_bytes_written != nullptr);
	assert(meth == 1 || meth == 2);

	switch(meth)
	{
		case 1:
			colr_size += 4; /* EnumCS */
			break;
		case 2:
			assert(color.icc_profile_len); /* ICC profile */
			colr_size += color.icc_profile_len;
			break;
		default:
			return nullptr;
	}

	auto colr_data = (uint8_t*)grkCalloc(1, colr_size);
	if(!colr_data)
		return nullptr;

	auto current_colr_ptr = colr_data;

	/* write box size */
	grk_write<uint32_t>(current_colr_ptr, colr_size, 4);
	current_colr_ptr += 4;

	/* BPCC */
	grk_write<uint32_t>(current_colr_ptr, JP2_COLR, 4);
	current_colr_ptr += 4;

	/* METH */
	grk_write<uint8_t>(current_colr_ptr++, meth);
	/* PRECEDENCE */
	grk_write<uint8_t>(current_colr_ptr++, precedence);
	/* APPROX */
	grk_write<uint8_t>(current_colr_ptr++, approx);

	/* Meth value is restricted to 1 or 2 (Table I.9 of part 1) */
	if(meth == 1)
	{
		/* EnumCS */
		grk_write<uint32_t>(current_colr_ptr, enumcs, 4);
	}
	else
	{
		/* ICC profile */
		if(meth == 2)
		{
			memcpy(current_colr_ptr, color.icc_profile_buf, color.icc_profile_len);
			current_colr_ptr += color.icc_profile_len;
		}
	}
	*p_nb_bytes_written = colr_size;

	return colr_data;
}
uint8_t* FileFormatCompress::write_channel_definition(uint32_t* p_nb_bytes_written)
{
	/* 8 bytes for box, 2 for n */
	uint32_t cdef_size = 10;
	assert(p_nb_bytes_written != nullptr);
	assert(color.channel_definition != nullptr);
	assert(color.channel_definition->descriptions != nullptr);
	assert(color.channel_definition->num_channel_descriptions > 0U);

	cdef_size += 6U * color.channel_definition->num_channel_descriptions;

	auto cdef_data = (uint8_t*)grkMalloc(cdef_size);
	if(!cdef_data)
		return nullptr;

	auto current_cdef_ptr = cdef_data;

	/* write box size */
	grk_write<uint32_t>(current_cdef_ptr, cdef_size, 4);
	current_cdef_ptr += 4;

	/* BPCC */
	grk_write<uint32_t>(current_cdef_ptr, JP2_CDEF, 4);
	current_cdef_ptr += 4;

	/* N */
	grk_write<uint16_t>(current_cdef_ptr, color.channel_definition->num_channel_descriptions);
	current_cdef_ptr += 2;

	for(uint16_t i = 0U; i < color.channel_definition->num_channel_descriptions; ++i)
	{
		/* Cni */
		grk_write<uint16_t>(current_cdef_ptr, color.channel_definition->descriptions[i].channel);
		current_cdef_ptr += 2;
		/* Typi */
		grk_write<uint16_t>(current_cdef_ptr, color.channel_definition->descriptions[i].typ);
		current_cdef_ptr += 2;
		/* Asoci */
		grk_write<uint16_t>(current_cdef_ptr, color.channel_definition->descriptions[i].asoc);
		current_cdef_ptr += 2;
	}
	*p_nb_bytes_written = cdef_size;

	return cdef_data;
}
uint8_t* FileFormatCompress::write_bpc(uint32_t* p_nb_bytes_written)
{
	assert(p_nb_bytes_written != nullptr);

	uint32_t i;
	/* room for 8 bytes for box and 1 byte for each component */
	uint32_t bpcc_size = 8U + numcomps;

	auto bpcc_data = (uint8_t*)grkCalloc(1, bpcc_size);
	if(!bpcc_data)
		return nullptr;

	auto current_bpc_ptr = bpcc_data;

	/* write box size */
	grk_write<uint32_t>(current_bpc_ptr, bpcc_size, 4);
	current_bpc_ptr += 4;

	/* BPCC */
	grk_write<uint32_t>(current_bpc_ptr, JP2_BPCC, 4);
	current_bpc_ptr += 4;

	for(i = 0; i < numcomps; ++i)
		grk_write(current_bpc_ptr++, comps[i].bpc);
	*p_nb_bytes_written = bpcc_size;

	return bpcc_data;
}
uint8_t* FileFormatCompress::write_res(uint32_t* p_nb_bytes_written)
{
	uint8_t *res_data = nullptr, *current_res_ptr = nullptr;
	assert(p_nb_bytes_written);

	bool storeCapture = capture_resolution[0] > 0 && capture_resolution[1] > 0;

	bool storeDisplay = display_resolution[0] > 0 && display_resolution[1] > 0;

	uint32_t size = (4 + 4) + GRK_RESOLUTION_BOX_SIZE;
	if(storeCapture && storeDisplay)
		size += GRK_RESOLUTION_BOX_SIZE;

	res_data = (uint8_t*)grkCalloc(1, size);
	if(!res_data)
		return nullptr;

	current_res_ptr = res_data;

	/* write super-box size */
	grk_write<uint32_t>(current_res_ptr, size, 4);
	current_res_ptr += 4;

	/* Super-box ID */
	grk_write<uint32_t>(current_res_ptr, JP2_RES, 4);
	current_res_ptr += 4;

	if(storeCapture)
		write_res_box(capture_resolution[0], capture_resolution[1], JP2_CAPTURE_RES,
					  &current_res_ptr);
	if(storeDisplay)
		write_res_box(display_resolution[0], display_resolution[1], JP2_DISPLAY_RES,
					  &current_res_ptr);
	*p_nb_bytes_written = size;

	return res_data;
}
void FileFormatCompress::find_cf(double x, uint32_t* num, uint32_t* den)
{
	// number of terms in continued fraction.
	// 15 is the max without precision errors for M_PI
#define MAX 15
	const double eps = 1.0 / USHRT_MAX;
	long p[MAX], q[MAX], a[MAX];

	int i;
	// The first two convergents are 0/1 and 1/0
	p[0] = 0;
	q[0] = 1;

	p[1] = 1;
	q[1] = 0;
	// The rest of the convergents (and continued fraction)
	for(i = 2; i < MAX; ++i)
	{
		a[i] = lrint(floor(x));
		p[i] = a[i] * p[i - 1] + p[i - 2];
		q[i] = a[i] * q[i - 1] + q[i - 2];
		// printf("%ld:  %ld/%ld\n", a[i], p[i], q[i]);
		if(fabs(x - (double)a[i]) < eps || (p[i] > USHRT_MAX) || (q[i] > USHRT_MAX))
			break;
		x = 1.0 / (x - (double)a[i]);
	}
	*num = (uint32_t)p[i - 1];
	*den = (uint32_t)q[i - 1];
}
void FileFormatCompress::write_res_box(double resx, double resy, uint32_t box_id,
									   uint8_t** current_res_ptr)
{
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

	for(size_t i = 0; i < 2; ++i)
	{
		exponent[i] = (int32_t)log10(res[i]);
		if(exponent[i] < 1)
			exponent[i] = 0;
		if(exponent[i] >= 1)
		{
			res[i] /= pow(10, exponent[i]);
		}
		find_cf(res[i], num + i, den + i);
	}
	for(size_t i = 0; i < 2; ++i)
	{
		grk_write<uint16_t>(*current_res_ptr, (uint16_t)num[i]);
		*current_res_ptr += 2;
		grk_write<uint16_t>(*current_res_ptr, (uint16_t)den[i]);
		*current_res_ptr += 2;
	}
	for(size_t i = 0; i < 2; ++i)
	{
		grk_write<uint8_t>(*current_res_ptr, (uint8_t)exponent[i]);
		*current_res_ptr += 1;
	}
}
uint8_t* FileFormatCompress::write_xml(uint32_t* p_nb_bytes_written)
{
	return write_buffer(JP2_XML, &xml, p_nb_bytes_written);
}
uint8_t* FileFormatCompress::write_buffer(uint32_t boxId, grkBufferU8* buffer,
										  uint32_t* p_nb_bytes_written)
{
	assert(p_nb_bytes_written != nullptr);

	/* need 8 bytes for box plus buffer->len bytes for buffer*/
	uint32_t total_size = 8 + (uint32_t)buffer->len;
	auto data = (uint8_t*)grkCalloc(1, total_size);
	if(!data)
		return nullptr;

	uint8_t* current_ptr = data;

	/* write box size */
	grk_write<uint32_t>(current_ptr, total_size, 4);
	current_ptr += 4;

	/* write box id */
	grk_write<uint32_t>(current_ptr, boxId, 4);
	current_ptr += 4;

	/* write buffer data */
	memcpy(current_ptr, buffer->buf, buffer->len);

	*p_nb_bytes_written = total_size;

	return data;
}
uint8_t* FileFormatCompress::write_ihdr(uint32_t* p_nb_bytes_written)
{
	assert(p_nb_bytes_written != nullptr);

	/* default image header is 22 bytes wide */
	auto ihdr_data = (uint8_t*)grkCalloc(1, 22);
	if(ihdr_data == nullptr)
		return nullptr;

	auto current_ihdr_ptr = ihdr_data;

	/* write box size */
	grk_write<uint32_t>(current_ihdr_ptr, 22, 4);
	current_ihdr_ptr += 4;

	/* IHDR */
	grk_write<uint32_t>(current_ihdr_ptr, JP2_IHDR, 4);
	current_ihdr_ptr += 4;

	/* HEIGHT */
	grk_write<uint32_t>(current_ihdr_ptr, h, 4);
	current_ihdr_ptr += 4;

	/* WIDTH */
	grk_write<uint32_t>(current_ihdr_ptr, w, 4);
	current_ihdr_ptr += 4;

	/* NC */
	grk_write<uint16_t>(current_ihdr_ptr, numcomps);
	current_ihdr_ptr += 2;

	/* BPC */
	grk_write<uint8_t>(current_ihdr_ptr++, bpc);

	/* C : Always 7 */
	grk_write<uint8_t>(current_ihdr_ptr++, C);

	/* UnkC, colorspace unknown */
	grk_write<uint8_t>(current_ihdr_ptr++, UnkC);

	/* IPR, no intellectual property */
	grk_write<uint8_t>(current_ihdr_ptr++, IPR);

	*p_nb_bytes_written = 22;

	return ihdr_data;
}
bool FileFormatCompress::startCompress(void)
{
	/* customization of the validation */
	init_compressValidation();

	/* validation of the parameters codec */
	if(!exec(validation_list_))
		return false;

	/* customization of the compressing */
	init_header_writing();

	// estimate if codec stream may be larger than 2^32 bytes
	auto p_image = codeStream->getHeaderImage();
	uint64_t image_size = 0;
	for(auto i = 0U; i < p_image->numcomps; ++i)
	{
		auto comp = p_image->comps + i;
		image_size += (uint64_t)comp->w * comp->h * ((comp->prec + 7U) / 8);
	}
	needs_xl_jp2c_box_length = (image_size > (uint64_t)1 << 30) ? true : false;

	/* write header */
	if(!exec(procedure_list_))
		return false;

	return codeStream->startCompress();
}
bool FileFormatCompress::initCompress(grk_cparameters* parameters, GrkImage* image)
{
	uint32_t i;
	uint8_t depth_0;
	uint32_t sign = 0;
	uint32_t alpha_count = 0;
	uint32_t color_channels = 0U;

	if(!parameters || !image)
		return false;

	if(codeStream->initCompress(parameters, image) == false)
		return false;

	/* Profile box */

	brand = JP2_JP2; /* BR */
	minversion = 0; /* MinV */
	numcl = 1;
	cl = (uint32_t*)grkMalloc(sizeof(uint32_t));
	if(!cl)
	{
		GRK_ERROR("Not enough memory when set up the JP2 compressor");
		return false;
	}
	cl[0] = JP2_JP2; /* CL0 : JP2 */

	/* Image Header box */
	numcomps = image->numcomps; /* NC */
	comps = new ComponentInfo[numcomps];

	h = image->y1 - image->y0;
	w = image->x1 - image->x0;
	depth_0 = (uint8_t)(image->comps[0].prec - 1);
	sign = image->comps[0].sgnd;
	bpc = (uint8_t)(depth_0 + (sign << 7));
	for(i = 1; i < image->numcomps; i++)
	{
		uint32_t depth = image->comps[i].prec - 1U;
		sign = image->comps[i].sgnd;
		if(depth_0 != depth)
			bpc = 0xFF;
	}
	C = 7; /* C : Always 7 */
	UnkC = 0; /* UnkC, colorspace specified in colr box */
	IPR = 0; /* IPR, no intellectual property */

	/* bit per component box */
	for(i = 0; i < image->numcomps; i++)
	{
		comps[i].bpc = (uint8_t)(image->comps[i].prec - 1);
		if(image->comps[i].sgnd)
			comps[i].bpc = (uint8_t)(comps[i].bpc + (1 << 7));
	}

	/* Colour Specification box */
	if(image->color_space == GRK_CLRSPC_ICC)
	{
		meth = 2;
		enumcs = GRK_ENUM_CLRSPC_UNKNOWN;
		if(image->meta && image->meta->color.icc_profile_buf)
		{
			// clean up existing icc profile in this struct
			if(color.icc_profile_buf)
			{
				delete[] color.icc_profile_buf;
				color.icc_profile_buf = nullptr;
			}
			// copy icc profile from image to this struct
			color.icc_profile_len = image->meta->color.icc_profile_len;
			color.icc_profile_buf = new uint8_t[color.icc_profile_len];
			memcpy(color.icc_profile_buf, image->meta->color.icc_profile_buf,
				   color.icc_profile_len);
		}
	}
	else
	{
		meth = 1;
		if(image->color_space == GRK_CLRSPC_CMYK)
			enumcs = GRK_ENUM_CLRSPC_CMYK;
		else if(image->color_space == GRK_CLRSPC_DEFAULT_CIE)
			enumcs = GRK_ENUM_CLRSPC_CIE;
		else if(image->color_space == GRK_CLRSPC_SRGB)
			enumcs = GRK_ENUM_CLRSPC_SRGB; /* sRGB as defined by IEC 61966-2-1 */
		else if(image->color_space == GRK_CLRSPC_GRAY)
			enumcs = GRK_ENUM_CLRSPC_GRAY; /* greyscale */
		else if(image->color_space == GRK_CLRSPC_SYCC)
			enumcs = GRK_ENUM_CLRSPC_SYCC; /* YUV */
		else if(image->color_space == GRK_CLRSPC_EYCC)
			enumcs = GRK_ENUM_CLRSPC_EYCC; /* YUV */
		else
		{
			GRK_ERROR("Unsupported colour space enumeration %d", image->color_space);
			return false;
		}
	}

	// transfer buffer to uuid
	if(image->meta)
	{
		if(image->meta->iptc_len && image->meta->iptc_buf)
		{
			uuids[numUuids++] =
				UUIDBox(IPTC_UUID, image->meta->iptc_buf, image->meta->iptc_len, true);
			image->meta->iptc_buf = nullptr;
			image->meta->iptc_len = 0;
		}

		// transfer buffer to uuid
		if(image->meta->xmp_len && image->meta->xmp_buf)
		{
			uuids[numUuids++] = UUIDBox(XMP_UUID, image->meta->xmp_buf, image->meta->xmp_len, true);
			image->meta->xmp_buf = nullptr;
			image->meta->xmp_len = 0;
		}
	}

	/* Channel Definition box */
	for(i = 0; i < image->numcomps; i++)
	{
		if(image->comps[i].type != GRK_CHANNEL_TYPE_COLOUR)
		{
			alpha_count++;
			// technically, this is an error, but we will let it pass
			if(image->comps[i].sgnd)
				GRK_WARN("signed alpha channel %u", i);
		}
	}

	switch(enumcs)
	{
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
			break;
	}
	if(alpha_count)
	{
		color.channel_definition = new grk_channel_definition();
		/* no memset needed, all values will be overwritten except if
		 * color.channel_definition->descriptions allocation fails, */
		/* in which case color.channel_definition->descriptions will be nullptr => valid for
		 * destruction */
		color.channel_definition->descriptions = new grk_channel_description[image->numcomps];
		/* cast is valid : image->numcomps [1,16384] */
		color.channel_definition->num_channel_descriptions = (uint16_t)image->numcomps;
		for(i = 0U; i < color_channels; i++)
		{
			/* cast is valid : image->numcomps [1,16384] */
			color.channel_definition->descriptions[i].channel = (uint16_t)i;
			color.channel_definition->descriptions[i].typ = GRK_CHANNEL_TYPE_COLOUR;
			/* No overflow + cast is valid : image->numcomps [1,16384] */
			color.channel_definition->descriptions[i].asoc = (uint16_t)(i + 1U);
		}
		for(; i < image->numcomps; i++)
		{
			/* cast is valid : image->numcomps [1,16384] */
			color.channel_definition->descriptions[i].channel = (uint16_t)i;
			color.channel_definition->descriptions[i].typ = image->comps[i].type;
			color.channel_definition->descriptions[i].asoc = image->comps[i].association;
		}
	}

	if(image->meta && image->meta->color.palette)
	{
		color.palette = image->meta->color.palette;
		image->meta->color.palette = nullptr;
	}

	precedence = 0; /* PRECEDENCE */
	approx = 0; /* APPROX */
	has_capture_resolution =
		parameters->write_capture_resolution || parameters->write_capture_resolution_from_file;
	if(parameters->write_capture_resolution)
	{
		for(i = 0; i < 2; ++i)
			capture_resolution[i] = parameters->capture_resolution[i];
	}
	else if(parameters->write_capture_resolution_from_file)
	{
		for(i = 0; i < 2; ++i)
			capture_resolution[i] = parameters->capture_resolution_from_file[i];
	}
	if(parameters->write_display_resolution)
	{
		has_display_resolution = true;
		display_resolution[0] = parameters->display_resolution[0];
		display_resolution[1] = parameters->display_resolution[1];
		// if display resolution equals (0,0), then use capture resolution
		// if available
		if(parameters->display_resolution[0] == 0 && parameters->display_resolution[1] == 0)
		{
			if(has_capture_resolution)
			{
				display_resolution[0] = parameters->capture_resolution[0];
				display_resolution[1] = parameters->capture_resolution[1];
			}
			else
			{
				has_display_resolution = false;
			}
		}
	}

	return true;
}
bool FileFormatCompress::compress(grk_plugin_tile* tile)
{
	return codeStream->compress(tile);
}
bool FileFormatCompress::compressTile(uint16_t tileIndex, uint8_t* p_data, uint64_t data_size)
{
	return codeStream->compressTile(tileIndex, p_data, data_size);
}
bool FileFormatCompress::endCompress(void)
{
	/* customization of the end compressing */
	init_end_header_writing();
	if(!codeStream->endCompress())
		return false;

	/* write header */
	return exec(procedure_list_);
}
void FileFormatCompress::init_end_header_writing(void)
{
	procedure_list_->push_back(std::bind(&FileFormatCompress::write_jp2c, this));
}
void FileFormatCompress::init_compressValidation(void)
{
	validation_list_->push_back(std::bind(&FileFormatCompress::default_validation, this));
}
void FileFormatCompress::init_header_writing(void)
{
	procedure_list_->push_back(std::bind(&FileFormatCompress::write_jp, this));
	procedure_list_->push_back(std::bind(&FileFormatCompress::write_ftyp, this));
	procedure_list_->push_back(std::bind(&FileFormatCompress::write_jp2h, this));
	procedure_list_->push_back(std::bind(&FileFormatCompress::write_uuids, this));
	procedure_list_->push_back(std::bind(&FileFormatCompress::skip_jp2c, this));
}
bool FileFormatCompress::skip_jp2c(void)
{
	auto stream = codeStream->getStream();
	assert(stream != nullptr);

	j2k_codestream_offset = stream->tell();
	int64_t skip_bytes = needs_xl_jp2c_box_length ? 16 : 8;

	return stream->skip(skip_bytes);
}
bool FileFormatCompress::default_validation(void)
{
	bool is_valid = true;
	uint32_t i;
	auto stream = codeStream->getStream();
	assert(stream != nullptr);

	/* JPEG2000 codec validation */

	/* POINTER validation */
	/* make sure a j2k codec is present */
	is_valid &= (codeStream != nullptr);

	/* make sure a procedure list is present */
	is_valid &= (procedure_list_ != nullptr);

	/* make sure a validation list is present */
	is_valid &= (validation_list_ != nullptr);

	/* PARAMETER VALIDATION */
	/* number of components */
	/* precision */
	for(i = 0; i < numcomps; ++i)
		is_valid &= ((comps[i].bpc & 0x7FU) < 38U); /* 0 is valid, ignore sign for check */

	/* METH */
	is_valid &= ((meth > 0) && (meth < 3));

	/* stream validation */
	/* back and forth is needed */
	is_valid &= stream->hasSeek();

	return is_valid;
}
} // namespace grk
