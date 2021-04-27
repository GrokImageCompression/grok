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

#include "grk_includes.h"

namespace grk
{
FileFormatDecompress::FileFormatDecompress(IBufferedStream* stream)
	: FileFormat(), m_headerError(false), codeStream(new CodeStreamDecompress(stream)), jp2_state(0)
{
	header = {{JP2_JP, [this](uint8_t* data, uint32_t len) { return read_jp(data, len); }},
			  {JP2_FTYP, [this](uint8_t* data, uint32_t len) { return read_ftyp(data, len); }},
			  {JP2_JP2H, [this](uint8_t* data, uint32_t len) { return read_jp2h(data, len); }},
			  {JP2_XML, [this](uint8_t* data, uint32_t len) { return read_xml(data, len); }},
			  {JP2_UUID, [this](uint8_t* data, uint32_t len) { return read_uuid(data, len); }},
			  {JP2_ASOC, [this](uint8_t* data, uint32_t len) { return read_asoc(data, len); }}};
	img_header = {
		{JP2_IHDR, [this](uint8_t* data, uint32_t len) { return read_ihdr(data, len); }},
		{JP2_COLR, [this](uint8_t* data, uint32_t len) { return read_colr(data, len); }},
		{JP2_BPCC, [this](uint8_t* data, uint32_t len) { return read_bpc(data, len); }},
		{JP2_PCLR, [this](uint8_t* data, uint32_t len) { return read_palette_clr(data, len); }},
		{JP2_CMAP,
		 [this](uint8_t* data, uint32_t len) { return read_component_mapping(data, len); }},
		{JP2_CDEF,
		 [this](uint8_t* data, uint32_t len) { return read_channel_definition(data, len); }},
		{JP2_RES, [this](uint8_t* data, uint32_t len) { return read_res(data, len); }}};
}
FileFormatDecompress::~FileFormatDecompress()
{
	delete codeStream;
}
void FileFormatDecompress::alloc_palette(grk_color* color, uint8_t num_channels,
										 uint16_t num_entries)
{
	assert(color);
	assert(num_channels);
	assert(num_entries);

	free_palette_clr(color);
	auto jp2_pclr = new grk_palette_data();
	jp2_pclr->channel_sign = new bool[num_channels];
	jp2_pclr->channel_prec = new uint8_t[num_channels];
	jp2_pclr->lut = new int32_t[num_channels * num_entries];
	jp2_pclr->num_entries = num_entries;
	jp2_pclr->num_channels = num_channels;
	jp2_pclr->component_mapping = nullptr;
	color->palette = jp2_pclr;
}
void FileFormatDecompress::free_palette_clr(grk_color* color)
{
	if(color)
	{
		if(color->palette)
		{
			delete[] color->palette->channel_sign;
			delete[] color->palette->channel_prec;
			delete[] color->palette->lut;
			delete[] color->palette->component_mapping;
			delete color->palette;
			color->palette = nullptr;
		}
	}
}
void FileFormatDecompress::free_color(grk_color* color)
{
	assert(color);
	free_palette_clr(color);
	delete[] color->icc_profile_buf;
	color->icc_profile_buf = nullptr;
	color->icc_profile_len = 0;
	if(color->channel_definition)
	{
		delete[] color->channel_definition->descriptions;
		delete color->channel_definition;
		color->channel_definition = nullptr;
	}
}
void FileFormatDecompress::init_end_header_reading(void)
{
	m_procedure_list->push_back(std::bind(&FileFormatDecompress::readHeaderProcedureImpl, this));
}
bool FileFormatDecompress::read_asoc(uint8_t* header_data, uint32_t header_data_size)
{
	assert(header_data);

	// 12 == sizoef(asoc tag) + sizeof(child size) + sizeof(child tag)
	if(header_data_size <= 12)
	{
		GRK_ERROR("ASOC super box can't be empty");
		return false;
	}

	try
	{
		read_asoc(&root_asoc, &header_data, &header_data_size, header_data_size);
	}
	catch(BadAsocException& bae)
	{
		GRK_UNUSED(bae);
		return false;
	}

	return true;
}
void FileFormatDecompress::serializeAsoc(AsocBox* asoc, grk_asoc* serial_asocs, uint32_t* num_asocs,
										 uint32_t level)
{
	if(*num_asocs == GRK_NUM_ASOC_BOXES_SUPPORTED)
	{
		GRK_WARN("Image contains more than maximum supported number of ASOC boxes (%d). Ignoring "
				 "the rest",
				 GRK_NUM_ASOC_BOXES_SUPPORTED);
		return;
	}
	auto as_c = serial_asocs + *num_asocs;
	as_c->label = asoc->label.c_str();
	as_c->level = level;
	as_c->xml = asoc->buf;
	as_c->xml_len = (uint32_t)asoc->len;
	(*num_asocs)++;
	/*
	if (as_c->level > 0) {
		GRK_INFO("%s", as_c->label);
		if (as_c->xml)
			GRK_INFO("%s", std::string((char*)as_c->xml, as_c->xml_len).c_str());
	}
	*/
	for(auto& child : asoc->children)
		serializeAsoc(child, serial_asocs, num_asocs, level + 1);
}
GrkImage* FileFormatDecompress::getImage(uint16_t tileIndex)
{
	return codeStream->getImage(tileIndex);
}
GrkImage* FileFormatDecompress::getImage(void)
{
	return codeStream->getImage();
}
/** Main header reading function handler */
bool FileFormatDecompress::readHeader(grk_header_info* header_info)
{
	if(m_headerError)
		return false;

	bool needsHeaderRead = !codeStream->getHeaderImage();
	if(needsHeaderRead)
	{
		m_procedure_list->push_back(
			std::bind(&FileFormatDecompress::readHeaderProcedureImpl, this));

		/* validation of the parameters codec */
		if(!exec(m_validation_list))
		{
			m_headerError = true;
			return false;
		}
		/* read header */
		if(!exec(m_procedure_list))
		{
			m_headerError = true;
			return false;
		}
	}
	// set file format fields in header info
	if(header_info)
	{
		// retrieve ASOCs
		header_info->num_asocs = 0;
		if(!root_asoc.children.empty())
			serializeAsoc(&root_asoc, header_info->asocs, &header_info->num_asocs, 0);
		header_info->xml_data = xml.buf;
		header_info->xml_data_len = xml.len;
	}
	if(!codeStream->readHeader(header_info))
	{
		m_headerError = true;
		return false;
	}

	if(needsHeaderRead)
	{
		auto image = (GrkImage*)codeStream->getCompositeImage();
		if(!check_color(image, &color))
		{
			m_headerError = true;
			return false;
		}
		if(has_capture_resolution)
		{
			image->has_capture_resolution = true;
			for(int i = 0; i < 2; ++i)
				image->capture_resolution[i] = capture_resolution[i];
		}
		if(has_display_resolution)
		{
			image->has_display_resolution = true;
			for(int i = 0; i < 2; ++i)
				image->display_resolution[i] = display_resolution[i];
		}
		/* Set Image Color Space */
		switch(enumcs)
		{
			case GRK_ENUM_CLRSPC_CMYK:
				image->color_space = GRK_CLRSPC_CMYK;
				break;
			case GRK_ENUM_CLRSPC_CIE:
				if(color.icc_profile_buf)
				{
					if(((uint32_t*)color.icc_profile_buf)[1] == GRK_DEFAULT_CIELAB_SPACE)
						image->color_space = GRK_CLRSPC_DEFAULT_CIE;
					else
						image->color_space = GRK_CLRSPC_CUSTOM_CIE;
				}
				else
				{
					GRK_ERROR("CIE Lab image requires ICC profile buffer set");
					m_headerError = true;
					return false;
				}
				break;
			case GRK_ENUM_CLRSPC_SRGB:
				image->color_space = GRK_CLRSPC_SRGB;
				break;
			case GRK_ENUM_CLRSPC_GRAY:
				image->color_space = GRK_CLRSPC_GRAY;
				break;
			case GRK_ENUM_CLRSPC_SYCC:
				image->color_space = GRK_CLRSPC_SYCC;
				break;
			case GRK_ENUM_CLRSPC_EYCC:
				image->color_space = GRK_CLRSPC_EYCC;
				break;
			default:
				image->color_space = GRK_CLRSPC_UNKNOWN;
				break;
		}
		if(meth == 2 && color.icc_profile_buf)
			image->color_space = GRK_CLRSPC_ICC;
		// check RGB subsampling
		if(image->color_space == GRK_CLRSPC_SRGB)
		{
			for(uint16_t i = 1; i < image->numcomps; ++i)
			{
				auto comp = image->comps + i;
				if(comp->dx != image->comps->dx || comp->dy != image->comps->dy)
				{
					GRK_ERROR(
						"sRGB colour space mandates uniform sampling in all three components");
					m_headerError = true;
					return false;
				}
			}
		}
		// retrieve icc profile
		if(color.icc_profile_buf)
		{
			image->createMeta();
			image->meta->color.icc_profile_buf = color.icc_profile_buf;
			image->meta->color.icc_profile_len = color.icc_profile_len;
			color.icc_profile_buf = nullptr;
			color.icc_profile_len = 0;
		}
		for(int i = 0; i < 2; ++i)
		{
			image->capture_resolution[i] = capture_resolution[i];
			image->display_resolution[i] = display_resolution[i];
		}
		// retrieve special uuids
		for(uint32_t i = 0; i < numUuids; ++i)
		{
			auto uuid = uuids + i;
			if(memcmp(uuid->uuid, IPTC_UUID, 16) == 0)
			{
				// make sure image meta exists
				image->createMeta();
				if (image->meta->iptc_buf){
					GRK_WARN("Attempt to set a second IPTC buffer. Ignoring");
				} else 	if (uuid->len) {
					image->meta->iptc_len = uuid->len;
					image->meta->iptc_buf = new uint8_t[uuid->len];
					memcpy(image->meta->iptc_buf, uuid->buf,uuid->len );
				}
			}
			else if(memcmp(uuid->uuid, XMP_UUID, 16) == 0)
			{
				// make sure image meta exists
				image->createMeta();
				if (image->meta->xmp_buf){
					GRK_WARN("Attempt to set a second XMP buffer. Ignoring");
				} else if (uuid->len) {
					image->meta->xmp_len = uuid->len;
					image->meta->xmp_buf = new uint8_t[uuid->len];
					memcpy(image->meta->xmp_buf, uuid->buf,uuid->len );
				}
			}
		}
	}

	return true;
}
bool FileFormatDecompress::setDecompressWindow(grkRectU32 window)
{
	return codeStream->setDecompressWindow(window);
}
/** Set up decompressor function handler */
void FileFormatDecompress::initDecompress(grk_dparameters* parameters)
{
	/* set up the J2K codec */
	codeStream->initDecompress(parameters);

	/* further JP2 initializations go here */
	color.has_colour_specification_box = false;
}
bool FileFormatDecompress::decompress(grk_plugin_tile* tile)
{
	if(!codeStream->decompress(tile))
	{
		GRK_ERROR("Failed to decompress JP2 file");
		return false;
	}

	return applyColour();
}
bool FileFormatDecompress::decompressTile(uint16_t tileIndex)
{
	if(!codeStream->decompressTile(tileIndex))
	{
		GRK_ERROR("Failed to decompress JP2 file");
		return false;
	}

	return applyColour();
}
/** Reading function used after code stream if necessary */
bool FileFormatDecompress::endDecompress(void)
{
	init_end_header_reading();
	if(!exec(m_procedure_list))
		return false;

	return codeStream->endDecompress();
}
bool FileFormatDecompress::applyColour(void)
{
	auto images = codeStream->getAllImages();
	for(auto& img : images)
	{
		if(!applyColour(img))
			return false;
	}

	return true;
}
bool FileFormatDecompress::applyColour(GrkImage* img)
{
	if(!img || img->color_applied)
		return true;

	if(color.palette)
	{
		/* Part 1, I.5.3.4: Either both or none : */
		if(!color.palette->component_mapping)
			free_palette_clr(&(color));
		else
		{
			if(!apply_palette_clr(img, &(color)))
				return false;
		}
	}

	/* Apply channel definitions if needed */
	if(color.channel_definition)
		apply_channel_definition(img, &color);

	img->color_applied = true;
	return true;
}
uint32_t FileFormatDecompress::read_asoc(AsocBox* parent, uint8_t** header_data,
										 uint32_t* header_data_size, uint32_t asocSize)
{
	assert(*header_data);
	if(asocSize < 8)
	{
		GRK_ERROR("ASOC box must be at least 8 bytes in size");
		throw BadAsocException();
	}
	uint32_t asocBytesUsed = 0;

	// create asoc
	auto childAsoc = new AsocBox();
	parent->children.push_back(childAsoc);

	// read all children
	while(asocBytesUsed<asocSize&& * header_data_size> 8)
	{
		uint32_t childSize = 0;
		grk_read<uint32_t>(*header_data, &childSize);
		if(childSize < 8)
		{
			GRK_ERROR("JP2 box must be at least 8 bytes in size");
			throw BadAsocException();
		}

		*header_data += 4;
		*header_data_size -= 4;
		childSize -= 4;
		asocBytesUsed += 4;

		uint32_t childTag = 0;
		grk_read<uint32_t>(*header_data, &childTag);
		*header_data += 4;
		*header_data_size -= 4;
		childSize -= 4;
		asocBytesUsed += 4;

		if(childSize > *header_data_size)
		{
			GRK_ERROR("Not enough space in ASOC box for child box");
			throw BadAsocException();
		}

		switch(childTag)
		{
			case JP2_LBL:
				childAsoc->label = std::string((const char*)*header_data, childSize);
				*header_data += childSize;
				*header_data_size -= childSize;
				asocBytesUsed += childSize;
				break;
			case JP2_ASOC:
				asocBytesUsed += read_asoc(childAsoc, header_data, header_data_size, childSize);
				break;
			case JP2_XML:
				childAsoc->alloc(childSize);
				memcpy(childAsoc->buf, *header_data, childSize);
				*header_data += childSize;
				*header_data_size -= childSize;
				asocBytesUsed += childSize;
				break;
			default:
				GRK_ERROR("ASOC box has unknown tag 0x%x", childTag);
				throw BadAsocException();
				break;
		}
	}
	if(asocBytesUsed < asocSize)
	{
		GRK_ERROR("ASOC box has extra bytes");
		throw BadAsocException();
	}

	return asocBytesUsed;
}
void FileFormatDecompress::dump(uint32_t flag, FILE* outputFileStream)
{
	codeStream->dump(flag, outputFileStream);
}
bool FileFormatDecompress::readHeaderProcedureImpl(void)
{
	FileFormatBox box;
	uint32_t bytesRead;
	uint64_t last_data_size = GRK_BOX_SIZE;
	uint32_t current_data_size;

	auto stream = codeStream->getStream();
	assert(stream != nullptr);
	bool rc = false;

	auto current_data = (uint8_t*)grkCalloc(1, last_data_size);
	if(!current_data)
	{
		GRK_ERROR("Not enough memory to handle JPEG 2000 file header");
		return false;
	}
	try
	{
		while(read_box_hdr(&box, &bytesRead, stream))
		{
			/* is it the code stream box ? */
			if(box.type == JP2_JP2C)
			{
				if(jp2_state & JP2_STATE_HEADER)
				{
					jp2_state |= JP2_STATE_CODESTREAM;
					rc = true;
					goto cleanup;
				}
				else
				{
					GRK_ERROR("bad placed jpeg code stream");
					goto cleanup;
				}
			}
			auto current_handler = find_handler(box.type);
			auto current_handler_misplaced = img_find_handler(box.type);
			current_data_size = (uint32_t)(box.length - bytesRead);
			if(current_handler || current_handler_misplaced)
			{
				if(current_handler == nullptr)
				{
					GRK_WARN("Found a misplaced '%c%c%c%c' box outside jp2h box",
							 (uint8_t)(box.type >> 24), (uint8_t)(box.type >> 16),
							 (uint8_t)(box.type >> 8), (uint8_t)(box.type >> 0));
					if(jp2_state & JP2_STATE_HEADER)
					{
						/* read anyway, we already have jp2h */
						current_handler = current_handler_misplaced;
					}
					else
					{
						GRK_WARN("JPEG2000 Header box not read yet, '%c%c%c%c' box will be ignored",
								 (uint8_t)(box.type >> 24), (uint8_t)(box.type >> 16),
								 (uint8_t)(box.type >> 8), (uint8_t)(box.type >> 0));
						jp2_state |= JP2_STATE_UNKNOWN;
						if(!stream->skip(current_data_size))
						{
							GRK_WARN("Problem with skipping JPEG2000 box, stream error");
							// ignore error and return true if code stream box has already been read
							// (we don't worry about any boxes after code stream)
							rc = (jp2_state & JP2_STATE_CODESTREAM) ? true : false;
							goto cleanup;
						}
						continue;
					}
				}
				if(current_data_size > stream->numBytesLeft())
				{
					/* do not even try to malloc if we can't read */
					GRK_ERROR("Invalid box size %" PRIu64
							  " for box '%c%c%c%c'. Need %u bytes, %" PRIu64 " bytes remaining ",
							  box.length, (uint8_t)(box.type >> 24), (uint8_t)(box.type >> 16),
							  (uint8_t)(box.type >> 8), (uint8_t)(box.type >> 0), current_data_size,
							  stream->numBytesLeft());
					goto cleanup;
				}
				if(current_data_size > last_data_size)
				{
					uint8_t* new_current_data =
						(uint8_t*)grkRealloc(current_data, current_data_size);
					if(!new_current_data)
					{
						GRK_ERROR("Not enough memory to handle JPEG 2000 box");
						goto cleanup;
					}
					current_data = new_current_data;
					last_data_size = current_data_size;
				}
				if(current_data_size == 0)
				{
					GRK_ERROR("Problem with reading JPEG2000 box, stream error");
					goto cleanup;
				}
				bytesRead = (uint32_t)stream->read(current_data, current_data_size);
				if(bytesRead != current_data_size)
				{
					GRK_ERROR("Problem with reading JPEG2000 box, stream error");
					goto cleanup;
				}
				if(!current_handler(current_data, current_data_size))
				{
					goto cleanup;
				}
			}
			else
			{
				if(!(jp2_state & JP2_STATE_SIGNATURE))
				{
					GRK_ERROR(
						"Malformed JP2 file format: first box must be JPEG 2000 signature box");
					goto cleanup;
				}
				if(!(jp2_state & JP2_STATE_FILE_TYPE))
				{
					GRK_ERROR("Malformed JP2 file format: second box must be file type box");
					goto cleanup;
				}
				jp2_state |= JP2_STATE_UNKNOWN;
				if(!stream->skip(current_data_size))
				{
					GRK_WARN("Problem with skipping JPEG2000 box, stream error");
					// ignore error and return true if code stream box has already been read
					// (we don't worry about any boxes after code stream)
					rc = (jp2_state & JP2_STATE_CODESTREAM) ? true : false;
					goto cleanup;
				}
			}
		}
		rc = true;
	}
	catch(CorruptJP2BoxException& ex)
	{
		GRK_UNUSED(ex);
		rc = false;
	}
cleanup:
	grkFree(current_data);
	return rc;
}
/***
 * Read box length and type only
 *
 *
 * returns: true if box header was read successfully, otherwise false
 * throw:   CorruptJP2BoxException if box is corrupt
 * Note: box length is never 0
 *
 */
bool FileFormatDecompress::read_box_hdr(FileFormatBox* box, uint32_t* p_number_bytes_read,
										IBufferedStream* stream)
{
	uint8_t data_header[8];

	assert(stream != nullptr);
	assert(box != nullptr);
	assert(p_number_bytes_read != nullptr);

	*p_number_bytes_read = (uint32_t)stream->read(data_header, 8);
	// we reached EOS
	if(*p_number_bytes_read < 8)
		return false;

	/* process read data */
	uint32_t L = 0;
	grk_read<uint32_t>(data_header, &L);
	box->length = L;
	grk_read<uint32_t>(data_header + 4, &(box->type));
	if(box->length == 0)
	{ /* last box */
		box->length = stream->numBytesLeft() + 8U;
		return true;
	}
	/* read XL  */
	if(box->length == 1)
	{
		uint32_t bytesRead = (uint32_t)stream->read(data_header, 8);
		// we reached EOS
		if(bytesRead < 8)
			return false;
		grk_read<uint64_t>(data_header, &box->length);
		*p_number_bytes_read += bytesRead;
	}
	if(box->length < *p_number_bytes_read)
	{
		GRK_ERROR("invalid box size %" PRIu64 " (%x)", box->length, box->type);
		throw CorruptJP2BoxException();
	}
	return true;
}
bool FileFormatDecompress::read_ihdr(uint8_t* p_image_header_data, uint32_t image_header_size)
{
	assert(p_image_header_data != nullptr);
	if(comps != nullptr)
	{
		GRK_WARN("Ignoring ihdr box. First ihdr box already read");
		return true;
	}
	if(image_header_size != GRK_ENUM_CLRSPC_CIE)
	{
		GRK_ERROR("Bad image header box (bad size)");
		return false;
	}
	grk_read<uint32_t>(p_image_header_data, &(h)); /* HEIGHT */
	p_image_header_data += 4;
	grk_read<uint32_t>(p_image_header_data, &(w)); /* WIDTH */
	p_image_header_data += 4;

	if((w == 0) || (h == 0))
	{
		GRK_ERROR("JP2 IHDR box: invalid dimensions: (%u,%u)", w, h);
		return false;
	}
	grk_read<uint16_t>(p_image_header_data, &numcomps); /* NC */
	p_image_header_data += 2;
	if((numcomps == 0) || (numcomps > maxNumComponentsJ2K))
	{
		GRK_ERROR("JP2 IHDR box: num components=%u does not conform to standard", numcomps);
		return false;
	}
	/* allocate memory for components */
	comps = new ComponentInfo[numcomps];
	grk_read<uint8_t>(p_image_header_data++, &bpc); /* BPC */
	///////////////////////////////////////////////////
	// (bits per component == precision -1)
	// Value of 0xFF indicates that bits per component
	// varies by component

	// Otherwise, low 7 bits of bpc determine bits per component,
	// and high bit set indicates signed data,
	// unset indicates unsigned data
	if(((bpc != 0xFF) && ((bpc & 0x7F) > (maxSupportedPrecisionGRK - 1))))
	{
		GRK_ERROR("JP2 IHDR box: bpc=%u not supported.", bpc);
		return false;
	}
	grk_read<uint8_t>(p_image_header_data++, &C); /* C */
	/* Should be equal to 7 cf. chapter about image header box */
	if(C != 7)
	{
		GRK_ERROR("JP2 IHDR box: compression type: %u indicates"
				  " a non-conformant JP2 file.",
				  C);
		return false;
	}
	grk_read<uint8_t>(p_image_header_data++, &UnkC); /* UnkC */
	// UnkC must be binary : {0,1}
	if((UnkC > 1))
	{
		GRK_ERROR("JP2 IHDR box: UnkC=%u does not conform to standard", UnkC);
		return false;
	}
	grk_read<uint8_t>(p_image_header_data++, &IPR); /* IPR */
	// IPR must be binary : {0,1}
	if((IPR > 1))
	{
		GRK_ERROR("JP2 IHDR box: IPR=%u does not conform to standard", IPR);
		return false;
	}

	return true;
}
bool FileFormatDecompress::read_xml(uint8_t* p_xml_data, uint32_t xml_size)
{
	if(!p_xml_data || !xml_size)
		return false;

	xml.alloc(xml_size);
	if(!xml.buf)
	{
		xml.len = 0;
		return false;
	}
	memcpy(xml.buf, p_xml_data, xml_size);
	return true;
}
bool FileFormatDecompress::read_uuid(uint8_t* headerData, uint32_t header_size)
{
	if(!headerData || header_size < 16)
		return false;
	if (header_size == 16){
		GRK_WARN("Read UUID box with no data - ignoring");
		return false;
	}
	if(numUuids == JP2_MAX_NUM_UUIDS)
	{
		GRK_WARN("Reached maximum (%u) number of UUID boxes read - ignoring UUID box",
				 JP2_MAX_NUM_UUIDS);
		return false;
	}
	auto uuid = uuids + numUuids;
	memcpy(uuid->uuid, headerData, 16);
	headerData += 16;
	uuid->alloc(header_size - 16);
	memcpy(uuid->buf, headerData, uuid->len);
	numUuids++;

	return true;
}
double FileFormatDecompress::calc_res(uint16_t num, uint16_t den, uint8_t exponent)
{
	if(den == 0)
		return 0;
	return ((double)num / den) * pow(10, exponent);
}
bool FileFormatDecompress::read_res_box(uint32_t* id, uint32_t* num, uint32_t* den,
										uint32_t* exponent, uint8_t** p_resolution_data)
{
	uint32_t box_size = 4 + 4 + 10;
	uint32_t size = 0;
	grk_read<uint32_t>(*p_resolution_data, &size);
	*p_resolution_data += 4;
	if(size != box_size)
		return false;
	grk_read<uint32_t>(*p_resolution_data, id);
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
bool FileFormatDecompress::read_res(uint8_t* p_resolution_data, uint32_t resolution_size)
{
	assert(p_resolution_data != nullptr);
	uint32_t num_boxes = resolution_size / GRK_RESOLUTION_BOX_SIZE;
	if(num_boxes == 0 || num_boxes > 2 || (resolution_size % GRK_RESOLUTION_BOX_SIZE))
	{
		GRK_ERROR("Bad resolution box (bad size)");
		return false;
	}
	while(resolution_size > 0)
	{
		uint32_t id;
		uint32_t num[2];
		uint32_t den[2];
		uint32_t exponent[2];

		if(!read_res_box(&id, num, den, exponent, &p_resolution_data))
			return false;
		double* res;
		switch(id)
		{
			case JP2_CAPTURE_RES:
				res = capture_resolution;
				has_capture_resolution = true;
				break;
			case JP2_DISPLAY_RES:
				res = display_resolution;
				has_display_resolution = true;
				break;
			default:
				return false;
		}
		for(int i = 0; i < 2; ++i)
			res[i] = calc_res((uint16_t)num[i], (uint16_t)den[i], (uint8_t)exponent[i]);
		resolution_size -= GRK_RESOLUTION_BOX_SIZE;
	}
	return true;
}
bool FileFormatDecompress::read_bpc(uint8_t* p_bpc_header_data, uint32_t bpc_header_size)
{
	assert(p_bpc_header_data != nullptr);

	if(bpc != 0xFF)
	{
		GRK_WARN("A BPC header box is available although BPC given by the IHDR box"
				 " (%u) indicate components bit depth is constant",
				 bpc);
	}
	if(bpc_header_size != numcomps)
	{
		GRK_ERROR("Bad BPC header box (bad size)");
		return false;
	}

	/* read info for each component */
	for(uint32_t i = 0; i < numcomps; ++i)
	{
		/* read each BPC component */
		grk_read(p_bpc_header_data++, &comps[i].bpc);
	}

	return true;
}
void FileFormatDecompress::apply_channel_definition(GrkImage* image, grk_color* color)
{
	auto info = color->channel_definition->descriptions;
	uint16_t n = color->channel_definition->num_channel_descriptions;

	for(uint16_t i = 0; i < n; ++i)
	{
		/* WATCH: asoc_index = asoc - 1 ! */
		uint16_t asoc = info[i].asoc;
		uint16_t cn = info[i].cn;

		if(cn >= image->numcomps)
		{
			GRK_WARN("apply_channel_definition: cn=%u, numcomps=%u", cn, image->numcomps);
			continue;
		}
		image->comps[cn].type = (GRK_COMPONENT_TYPE)info[i].typ;

		// no need to do anything further if this is not a colour channel,
		// or if this channel is associated with the whole image
		if(info[i].typ != GRK_COMPONENT_TYPE_COLOUR ||
		   info[i].asoc == GRK_COMPONENT_ASSOC_WHOLE_IMAGE)
			continue;

		if(info[i].typ == GRK_COMPONENT_TYPE_COLOUR && asoc > image->numcomps)
		{
			GRK_WARN("apply_channel_definition: association=%u > numcomps=%u", asoc,
					 image->numcomps);
			continue;
		}
		uint16_t asoc_index = (uint16_t)(asoc - 1);

		/* Swap only if color channel */
		if((cn != asoc_index) && (info[i].typ == GRK_COMPONENT_TYPE_COLOUR))
		{
			grk_image_comp saved;
			uint16_t j;

			memcpy(&saved, &image->comps[cn], sizeof(grk_image_comp));
			memcpy(&image->comps[cn], &image->comps[asoc_index], sizeof(grk_image_comp));
			memcpy(&image->comps[asoc_index], &saved, sizeof(grk_image_comp));

			/* Swap channels in following channel definitions, don't bother with j <= i that are
			 * already processed */
			for(j = (uint16_t)(i + 1U); j < n; ++j)
			{
				if(info[j].cn == cn)
					info[j].cn = asoc_index;
				else if(info[j].cn == asoc_index)
					info[j].cn = cn;
				/* asoc is related to color index. Do not update. */
			}
		}
	}
}
bool FileFormatDecompress::read_channel_definition(uint8_t* p_cdef_header_data,
												   uint32_t cdef_header_size)
{
	uint16_t i;
	assert(p_cdef_header_data != nullptr);

	(void)cdef_header_size;

	/* Part 1, I.5.3.6: 'The shall be at most one Channel Definition box
	 * inside a JP2 Header box.'*/
	if(color.channel_definition)
		return false;

	if(cdef_header_size < 2)
	{
		GRK_ERROR("CDEF box: Insufficient data.");
		return false;
	}
	uint16_t num_channel_descriptions;
	grk_read<uint16_t>(p_cdef_header_data, &num_channel_descriptions); /* N */
	p_cdef_header_data += 2;

	if(num_channel_descriptions == 0U)
	{
		GRK_ERROR("CDEF box: Number of channel definitions is equal to zero.");
		return false;
	}
	if(cdef_header_size < 2 + (uint32_t)(uint16_t)num_channel_descriptions * 6)
	{
		GRK_ERROR("CDEF box: Insufficient data.");
		return false;
	}
	color.channel_definition = new grk_channel_definition();
	color.channel_definition->descriptions = new grk_channel_description[num_channel_descriptions];
	color.channel_definition->num_channel_descriptions = (uint16_t)num_channel_descriptions;
	auto cdef_info = color.channel_definition->descriptions;
	for(i = 0; i < num_channel_descriptions; ++i)
	{
		grk_read<uint16_t>(p_cdef_header_data, &cdef_info[i].cn); /* Cn^i */
		p_cdef_header_data += 2;

		grk_read<uint16_t>(p_cdef_header_data, &cdef_info[i].typ); /* Typ^i */
		p_cdef_header_data += 2;
		if(cdef_info[i].typ > 2 && cdef_info[i].typ != GRK_COMPONENT_TYPE_UNSPECIFIED)
		{
			GRK_ERROR("CDEF box : Illegal channel type %u", cdef_info[i].typ);
			return false;
		}
		grk_read<uint16_t>(p_cdef_header_data, &cdef_info[i].asoc); /* Asoc^i */
		if(cdef_info[i].asoc > 3 && cdef_info[i].asoc != GRK_COMPONENT_ASSOC_UNASSOCIATED)
		{
			GRK_ERROR("CDEF box : Illegal channel association %u", cdef_info[i].asoc);
			return false;
		}
		p_cdef_header_data += 2;
	}

	// cdef sanity check
	// 1. check for multiple descriptions of the same component with different types
	for(i = 0; i < color.channel_definition->num_channel_descriptions; ++i)
	{
		auto infoi = cdef_info[i];
		for(uint16_t j = 0; j < color.channel_definition->num_channel_descriptions; ++j)
		{
			auto infoj = cdef_info[j];
			if(i != j && infoi.cn == infoj.cn && infoi.typ != infoj.typ)
			{
				GRK_ERROR("CDEF box : multiple descriptions of component, %u, with differing types "
						  ": %u and %u.",
						  infoi.cn, infoi.typ, infoj.typ);
				return false;
			}
		}
	}

	// 2. check that type/association pairs are unique
	for(i = 0; i < color.channel_definition->num_channel_descriptions; ++i)
	{
		auto infoi = cdef_info[i];
		for(uint16_t j = 0; j < color.channel_definition->num_channel_descriptions; ++j)
		{
			auto infoj = cdef_info[j];
			if(i != j && infoi.cn != infoj.cn && infoi.typ == infoj.typ &&
			   infoi.asoc == infoj.asoc &&
			   (infoi.typ != GRK_COMPONENT_TYPE_UNSPECIFIED ||
				infoi.asoc != GRK_COMPONENT_ASSOC_UNASSOCIATED))
			{
				GRK_ERROR(
					"CDEF box : components %u and %u share same type/association pair (%u,%u).",
					infoi.cn, infoj.cn, infoj.typ, infoj.asoc);
				return false;
			}
		}
	}

	return true;
}
bool FileFormatDecompress::read_colr(uint8_t* p_colr_header_data, uint32_t colr_header_size)
{
	assert(p_colr_header_data != nullptr);

	if(colr_header_size < 3)
	{
		GRK_ERROR("Bad COLR header box (bad size)");
		return false;
	}

	/* Part 1, I.5.3.3 : 'A conforming JP2 reader shall ignore all colour
	 * specification boxes after the first.'
	 */
	if(color.has_colour_specification_box)
	{
		GRK_WARN("A conforming JP2 reader shall ignore all colour specification boxes after the "
				 "first, so we ignore this one.");
		return true;
	}
	grk_read<uint8_t>(p_colr_header_data++, &meth); /* METH */
	grk_read<uint8_t>(p_colr_header_data++, &precedence); /* PRECEDENCE */
	grk_read<uint8_t>(p_colr_header_data++, &approx); /* APPROX */

	if(meth == 1)
	{
		uint32_t temp;
		if(colr_header_size < 7)
		{
			GRK_ERROR("Bad COLR header box (bad size: %u)", colr_header_size);
			return false;
		}
		grk_read<uint32_t>(p_colr_header_data, &temp); /* EnumCS */
		p_colr_header_data += 4;

		if(temp != GRK_ENUM_CLRSPC_UNKNOWN && temp != GRK_ENUM_CLRSPC_CMYK &&
		   temp != GRK_ENUM_CLRSPC_CIE && temp != GRK_ENUM_CLRSPC_SRGB &&
		   temp != GRK_ENUM_CLRSPC_GRAY && temp != GRK_ENUM_CLRSPC_SYCC &&
		   temp != GRK_ENUM_CLRSPC_EYCC)
		{
			GRK_WARN("Invalid colour space enumeration %u. Ignoring colour box", temp);
			return true;
		}
		enumcs = (GRK_ENUM_COLOUR_SPACE)temp;
		if((colr_header_size > 7) && (enumcs != GRK_ENUM_CLRSPC_CIE))
		{ /* handled below for CIELab) */
			/* testcase Altona_Technical_v20_x4.pdf */
			GRK_WARN("Bad COLR header box (bad size: %u)", colr_header_size);
		}
		if(enumcs == GRK_ENUM_CLRSPC_CIE)
		{
			uint32_t* cielab;
			bool nonDefaultLab = colr_header_size == 35;
			// only two ints are needed for default CIELab space
			cielab = (uint32_t*)new uint8_t[(nonDefaultLab ? 9 : 2) * sizeof(uint32_t)];
			if(cielab == nullptr)
			{
				GRK_ERROR("Not enough memory for cielab");
				return false;
			}
			cielab[0] = GRK_ENUM_CLRSPC_CIE; /* enumcs */
			cielab[1] = GRK_DEFAULT_CIELAB_SPACE;

			if(colr_header_size == 35)
			{
				uint32_t rl, ol, ra, oa, rb, ob, il;
				grk_read<uint32_t>(p_colr_header_data, &rl);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &ol);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &ra);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &oa);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &rb);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &ob);
				p_colr_header_data += 4;
				grk_read<uint32_t>(p_colr_header_data, &il);
				p_colr_header_data += 4;

				cielab[1] = GRK_CUSTOM_CIELAB_SPACE;
				cielab[2] = rl;
				cielab[4] = ra;
				cielab[6] = rb;
				cielab[3] = ol;
				cielab[5] = oa;
				cielab[7] = ob;
				cielab[8] = il;
			}
			else if(colr_header_size != 7)
			{
				GRK_WARN("Bad COLR header box (CIELab, bad size: %u)", colr_header_size);
			}
			color.icc_profile_buf = (uint8_t*)cielab;
			color.icc_profile_len = 0;
		}
		color.has_colour_specification_box = true;
	}
	else if(meth == 2)
	{
		/* ICC profile */
		uint32_t icc_len = (uint32_t)(colr_header_size - 3);
		if(icc_len == 0)
		{
			GRK_ERROR("ICC profile buffer length equals zero");
			return false;
		}
		color.icc_profile_buf = new uint8_t[(size_t)icc_len];
		memcpy(color.icc_profile_buf, p_colr_header_data, icc_len);
		color.icc_profile_len = icc_len;
		color.has_colour_specification_box = true;
	}
	else
	{
		/*	ISO/IEC 15444-1:2004 (E), Table I.9 Legal METH values:
		 conforming JP2 reader shall ignore the entire Colour Specification box.*/
		GRK_WARN("COLR BOX meth value is not a regular value (%u), "
				 "so we will ignore the entire Colour Specification box. ",
				 meth);
	}
	return true;
}
bool FileFormatDecompress::check_color(GrkImage* image, grk_color* color)
{
	uint16_t i;
	/* testcase 4149.pdf.SIGSEGV.cf7.3501 */
	if(color->channel_definition)
	{
		auto info = color->channel_definition->descriptions;
		uint16_t n = color->channel_definition->num_channel_descriptions;
		uint32_t num_channels =
			image->numcomps; /* FIXME image->numcomps == numcomps before color is applied ??? */

		/* cdef applies to component_mapping channels if any */
		if(color->palette && color->palette->component_mapping)
			num_channels = (uint32_t)color->palette->num_channels;
		for(i = 0; i < n; i++)
		{
			if(info[i].cn >= num_channels)
			{
				GRK_ERROR("Invalid channel index %u (>= %u).", info[i].cn, num_channels);
				return false;
			}
			if(info[i].asoc == GRK_COMPONENT_ASSOC_UNASSOCIATED)
				continue;
			if(info[i].asoc > 0 && (uint32_t)(info[i].asoc - 1) >= num_channels)
			{
				GRK_ERROR("Invalid component association %u  (>= %u).", info[i].asoc - 1,
						  num_channels);
				return false;
			}
		}
		/* issue 397 */
		/* ISO 15444-1 states that if cdef is present, it shall contain a complete list of channel
		 * definitions. */
		while(num_channels > 0)
		{
			for(i = 0; i < n; ++i)
			{
				if((uint32_t)info[i].cn == (num_channels - 1U))
					break;
			}
			if(i == n)
			{
				GRK_ERROR("Incomplete channel definitions.");
				return false;
			}
			--num_channels;
		}
	}

	/* testcases 451.pdf.SIGSEGV.f4c.3723, 451.pdf.SIGSEGV.5b5.3723 and
	 66ea31acbb0f23a2bbc91f64d69a03f5_signal_sigsegv_13937c0_7030_5725.pdf */
	if(color->palette && color->palette->component_mapping)
	{
		uint16_t num_channels = color->palette->num_channels;
		auto component_mapping = color->palette->component_mapping;
		bool* pcol_usage = nullptr;
		bool is_sane = true;

		/* verify that all original components match an existing one */
		for(i = 0; i < num_channels; i++)
		{
			if(component_mapping[i].component_index >= image->numcomps)
			{
				GRK_ERROR("Invalid component index %u (>= %u).",
						  component_mapping[i].component_index, image->numcomps);
				is_sane = false;
				goto cleanup;
			}
		}
		pcol_usage = (bool*)grkCalloc(num_channels, sizeof(bool));
		if(!pcol_usage)
		{
			GRK_ERROR("Unexpected OOM.");
			return false;
		}
		/* verify that no component is targeted more than once */
		for(i = 0; i < num_channels; i++)
		{
			uint16_t palette_column = component_mapping[i].palette_column;
			if(component_mapping[i].mapping_type != 0 && component_mapping[i].mapping_type != 1)
			{
				GRK_ERROR("Unexpected MTYP value.");
				is_sane = false;
				goto cleanup;
			}
			if(palette_column >= num_channels)
			{
				GRK_ERROR("Invalid component/palette index for direct mapping %u.", palette_column);
				is_sane = false;
				goto cleanup;
			}
			else if(pcol_usage[palette_column] && component_mapping[i].mapping_type == 1)
			{
				GRK_ERROR("Component %u is mapped twice.", palette_column);
				is_sane = false;
				goto cleanup;
			}
			else if(component_mapping[i].mapping_type == 0 &&
					component_mapping[i].palette_column != 0)
			{
				/* I.5.3.5 PCOL: If the value of the MTYP field for this channel is 0, then
				 * the value of this field shall be 0. */
				GRK_ERROR("Direct use at #%u however palette_column=%u.", i, palette_column);
				is_sane = false;
				goto cleanup;
			}
			else
				pcol_usage[palette_column] = true;
		}
		/* verify that all components are targeted at least once */
		for(i = 0; i < num_channels; i++)
		{
			if(!pcol_usage[i] && component_mapping[i].mapping_type != 0)
			{
				GRK_ERROR("Component %u doesn't have a mapping.", i);
				is_sane = false;
				goto cleanup;
			}
		}
		/* Issue 235/447 weird component_mapping */
		if(is_sane && (image->numcomps == 1U))
		{
			for(i = 0; i < num_channels; i++)
			{
				if(!pcol_usage[i])
				{
					is_sane = 0U;
					GRK_WARN("Component mapping seems wrong. Trying to correct.", i);
					break;
				}
			}
			if(!is_sane)
			{
				is_sane = true;
				for(i = 0; i < num_channels; i++)
				{
					component_mapping[i].mapping_type = 1U;
					component_mapping[i].palette_column = (uint8_t)i;
				}
			}
		}
	cleanup:
		grkFree(pcol_usage);
		if(!is_sane)
			return false;
	}

	return true;
}
bool FileFormatDecompress::apply_palette_clr(GrkImage* image, grk_color* color)
{
	auto pal = color->palette;
	auto channel_prec = pal->channel_prec;
	auto channel_sign = pal->channel_sign;
	auto lut = pal->lut;
	auto component_mapping = pal->component_mapping;
	uint16_t num_channels = pal->num_channels;

	// sanity check on component mapping
	for(uint16_t channel = 0; channel < num_channels; ++channel)
	{
		auto mapping = component_mapping + channel;
		uint16_t compno = mapping->component_index;
		uint16_t paletteColumn = mapping->palette_column;
		auto comp = image->comps + compno;
		if (compno >= image->numcomps){
			GRK_ERROR("apply_palette_clr: component mapping component number %d for channel %d "
					"must be less than number of image components %d",compno, channel,image->numcomps);
			return false;
		}
		if(comp->data == nullptr)
		{
			GRK_ERROR("image->comps[%u].data == nullptr"
					  " in apply_palette_clr().",
					  compno);
			return false;
		}
		if (comp->prec > pal->num_entries){
			GRK_ERROR("Precision %d of component %d is greater than "
					"number of palette entries %d",
					compno,image->comps[compno].prec,pal->num_entries);
			return false;
		}
		switch (mapping->mapping_type){
			case 0:
				if (paletteColumn != 0){
					GRK_ERROR("apply_palette_clr: channel %d with direct component mapping: "
							"non-zero palette column %d not allowed",channel,paletteColumn);
					return false;
				}
				break;
			case 1:
				if (comp->sgnd){
					GRK_ERROR("apply_palette_clr: channel %d with non-direct component mapping: "
							"cannot be signed");
					return false;
				}
				break;
		}
	}
	auto oldComps = image->comps;
	auto newComps = new grk_image_comp[num_channels];
	for(uint16_t channel = 0; channel < num_channels; ++channel)
	{
		auto mapping = component_mapping + channel;
		uint16_t palette_column = mapping->palette_column;
		uint16_t compno = mapping->component_index;

		/* Direct mapping */
		if(mapping->mapping_type == 0)
		{
			newComps[channel] = oldComps[compno];
			newComps[channel].data = nullptr;
		}
		else
		{
			newComps[palette_column] = oldComps[compno];
			newComps[palette_column].data = nullptr;
		}
		if(!GrkImage::allocData(newComps + channel))
		{
			while(channel > 0)
			{
				--channel;
				grkAlignedFree(newComps[channel].data);
			}
			delete[] newComps;
			GRK_ERROR("Memory allocation failure in apply_palette_clr().");
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
		uint16_t compno = mapping->component_index;
		uint16_t palette_column = mapping->palette_column;
		auto src = oldComps[compno].data;
		switch(mapping->mapping_type){
		case 0:
			{
			auto src = oldComps[compno].data;
			size_t num_pixels = (size_t)newComps[channel].stride * newComps[channel].h;
			memcpy(newComps[channel].data,src,num_pixels * sizeof(int32_t));
			}
			break;
		case 1:
			{
			auto dst = newComps[palette_column].data;
			uint32_t diff =
				(uint32_t)(newComps[palette_column].stride - newComps[palette_column].w);
			size_t ind = 0;
			// note: 1 <= n <= 255
			for(uint32_t n = 0; n < newComps[palette_column].h; ++n)
			{
				int32_t k = 0;
				for(uint32_t m = 0; m < newComps[palette_column].w; ++m)
				{
					if((k = src[ind]) < 0)
						k = 0;
					else if(k > top_k)
						k = top_k;
					dst[ind++] = (int32_t)lut[k * num_channels + palette_column];
				}
				ind += diff;
			}
			}
			break;
		}
	}
	for(uint16_t i = 0; i < image->numcomps; ++i)
		grk_image_single_component_data_free(oldComps + i);
	delete[] oldComps;
	image->comps = newComps;
	image->numcomps = num_channels;

	return true;
}
bool FileFormatDecompress::read_component_mapping(uint8_t* component_mapping_header_data,
												  uint32_t component_mapping_header_size)
{
	uint8_t channel, num_channels;
	assert(component_mapping_header_data != nullptr);

	/* Need num_channels: */
	if(color.palette == nullptr)
	{
		GRK_ERROR("Need to read a PCLR box before the CMAP box.");
		return false;
	}
	/* Part 1, I.5.3.5: 'There shall be at most one Component Mapping box
	 * inside a JP2 Header box' :
	 */
	if(color.palette->component_mapping)
	{
		GRK_ERROR("Only one CMAP box is allowed.");
		return false;
	}
	num_channels = color.palette->num_channels;
	if(component_mapping_header_size < (uint32_t)num_channels * 4)
	{
		GRK_ERROR("Insufficient data for CMAP box.");
		return false;
	}
	auto component_mapping = new grk_component_mapping_comp[num_channels];
	for(channel = 0; channel < num_channels; ++channel)
	{
		auto mapping = component_mapping + channel;
		grk_read<uint16_t>(component_mapping_header_data,
						   &mapping->component_index); /* CMP^i */
		component_mapping_header_data += 2;
		grk_read<uint8_t>(component_mapping_header_data++,
						  &mapping->mapping_type); /* MTYP^i */
		if (mapping->mapping_type > 1){
			GRK_ERROR("Component mapping type %d for channel %d is greater than 1.",mapping->mapping_type,channel );
			delete[] component_mapping;
			return false;
		}
		grk_read<uint8_t>(component_mapping_header_data++,
						  &mapping->palette_column); /* PCOL^i */
	}
	color.palette->component_mapping = component_mapping;

	return true;
}
bool FileFormatDecompress::read_palette_clr(uint8_t* p_pclr_header_data, uint32_t pclr_header_size)
{
	auto orig_header_data = p_pclr_header_data;
	assert(p_pclr_header_data != nullptr);
	if(color.palette)
		return false;
	if(pclr_header_size < 3)
		return false;
	uint16_t num_entries;
	grk_read<uint16_t>(p_pclr_header_data, &num_entries); /* NE */
	p_pclr_header_data += 2;
	if((num_entries == 0U) || (num_entries > 1024U))
	{
		GRK_ERROR("Invalid PCLR box. Reports %u palette entries", (int)num_entries);
		return false;
	}
	uint8_t num_channels;
	grk_read<uint8_t>(p_pclr_header_data, &num_channels); /* NPC */
	++p_pclr_header_data;
	if(num_channels == 0U)
	{
		GRK_ERROR("Invalid PCLR box : 0 palette columns");
		return false;
	}
	if(pclr_header_size < 3 + (uint32_t)num_channels)
		return false;
	FileFormatDecompress::alloc_palette(&color, num_channels, num_entries);
	auto jp2_pclr = color.palette;
	for(uint8_t i = 0; i < num_channels; ++i)
	{
		uint8_t val;
		grk_read<uint8_t>(p_pclr_header_data++, &val); /* Bi */
		jp2_pclr->channel_prec[i] = (uint8_t)((val & 0x7f) + 1);
		if(jp2_pclr->channel_prec[i] > maxSupportedPrecisionGRK)
		{
			GRK_ERROR("Palette : channel precision %d is greater than supported palette channel "
					  "precision %d",
					  jp2_pclr->channel_prec[i],maxSupportedPrecisionGRK);
			return false;
		}
		jp2_pclr->channel_sign[i] = (val & 0x80) ? true : false;
		if(jp2_pclr->channel_sign[i])
		{
			GRK_ERROR("Palette : signed channel not supported");
			return false;
		}
	}
	auto lut = jp2_pclr->lut;
	for(uint16_t j = 0; j < num_entries; ++j)
	{
		for(uint8_t i = 0; i < num_channels; ++i)
		{
			uint32_t bytes_to_read = (uint32_t)((jp2_pclr->channel_prec[i] + 7) >> 3);
			if((ptrdiff_t)pclr_header_size <
			   (ptrdiff_t)(p_pclr_header_data - orig_header_data) + (ptrdiff_t)bytes_to_read)
			{
				GRK_ERROR("Palette : box too short");
				return false;
			}
			grk_read<int32_t>(p_pclr_header_data, lut++, bytes_to_read); /* Cji */
			p_pclr_header_data += bytes_to_read;
		}
	}

	return true;
}

const BOX_FUNC FileFormatDecompress::find_handler(uint32_t id)
{
	auto res = header.find(id);
	return (res != header.end() ? res->second : nullptr);
}
/**
 * Finds the image execution function related to the given box id.
 *
 * @param	id	the id of the handler to fetch.
 *
 * @return	the given handler or nullptr if it could not be found.
 */
const BOX_FUNC FileFormatDecompress::img_find_handler(uint32_t id)
{
	auto res = img_header.find(id);
	return (res != img_header.end() ? res->second : nullptr);
}
/**
 * Reads a JPEG 2000 file signature box.
 *
 * @param	headerData	the data contained in the signature box.
 * @param	header_size	the size of the data contained in the signature box.
 *
 * @return true if the file signature box is valid.
 */
bool FileFormatDecompress::read_jp(uint8_t* headerData, uint32_t header_size)
{
	uint32_t magic_number;
	assert(headerData != nullptr);

	if(jp2_state != JP2_STATE_NONE)
	{
		GRK_ERROR("The signature box must be the first box in the file.");
		return false;
	}
	/* assure length of data is correct (4 -> magic number) */
	if(header_size != 4)
	{
		GRK_ERROR("Error with JP signature Box size");
		return false;
	}
	/* rearrange data */
	grk_read<uint32_t>(headerData, &magic_number);
	if(magic_number != 0x0d0a870a)
	{
		GRK_ERROR("Error with JP Signature : bad magic number");
		return false;
	}
	jp2_state |= JP2_STATE_SIGNATURE;

	return true;
}
/**
 * Reads a a FTYP box - File type box
 *
 * @param	headerData	the data contained in the FTYP box.
 * @param	header_size	the size of the data contained in the FTYP box.
 *
 * @return true if the FTYP box is valid.
 */
bool FileFormatDecompress::read_ftyp(uint8_t* headerData, uint32_t header_size)
{
	uint32_t i, remaining_bytes;
	assert(headerData != nullptr);

	if(jp2_state != JP2_STATE_SIGNATURE)
	{
		GRK_ERROR("The ftyp box must be the second box in the file.");
		return false;
	}
	/* assure length of data is correct */
	if(header_size < 8)
	{
		GRK_ERROR("Error with FTYP signature Box size");
		return false;
	}
	grk_read<uint32_t>(headerData, &brand); /* BR */
	headerData += 4;
	grk_read<uint32_t>(headerData, &minversion); /* MinV */
	headerData += 4;
	remaining_bytes = header_size - 8;
	/* the number of remaining bytes should be a multiple of 4 */
	if((remaining_bytes & 0x3) != 0)
	{
		GRK_ERROR("Error with FTYP signature Box size");
		return false;
	}
	/* div by 4 */
	numcl = remaining_bytes >> 2;
	if(numcl)
	{
		cl = (uint32_t*)grkCalloc(numcl, sizeof(uint32_t));
		if(cl == nullptr)
		{
			GRK_ERROR("Not enough memory with FTYP Box");
			return false;
		}
	}
	for(i = 0; i < numcl; ++i)
	{
		grk_read<uint32_t>(headerData, &cl[i]); /* CLi */
		headerData += 4;
	}
	jp2_state |= JP2_STATE_FILE_TYPE;

	return true;
}
/**
 * Reads the Jpeg2000 file Header box - JP2 Header box (warning, this is a super box).
 *
 * @param	headerData	the data contained in the file header box.
 * @param	header_size	the size of the data contained in the file header box.
 *
 * @return true if the JP2 Header box was successfully recognized.
 */
bool FileFormatDecompress::read_jp2h(uint8_t* headerData, uint32_t header_size)
{
	assert(headerData != nullptr);

	/* make sure the box is well placed */
	if((jp2_state & JP2_STATE_FILE_TYPE) != JP2_STATE_FILE_TYPE)
	{
		GRK_ERROR("The  box must be the first box in the file.");
		return false;
	}
	bool has_ihdr = false;
	/* iterate while remaining data */
	while(header_size)
	{
		uint32_t box_size = 0;
		FileFormatBox box;
		if(!read_box(&box, headerData, &box_size, (uint64_t)header_size))
			return false;
		uint32_t box_data_length = (uint32_t)(box.length - box_size);
		headerData += box_size;

		auto current_handler = img_find_handler(box.type);
		if(current_handler != nullptr)
		{
			if(!current_handler(headerData, box_data_length))
				return false;
		}
		if(box.type == JP2_IHDR)
			has_ihdr = true;
		headerData += box_data_length;
		// this will never overflow since "jp2_read_box" checks for overflow
		header_size = header_size - (uint32_t)box.length;
	}
	if(!has_ihdr)
	{
		GRK_ERROR("Stream error while reading JP2 Header box: no 'ihdr' box.");
		return false;
	}
	jp2_state |= JP2_STATE_HEADER;

	return true;
}
bool FileFormatDecompress::read_box(FileFormatBox* box, uint8_t* p_data,
									uint32_t* p_number_bytes_read, uint64_t maxBoxSize)
{
	assert(p_data != nullptr);
	assert(box != nullptr);
	assert(p_number_bytes_read != nullptr);

	if(maxBoxSize < 8)
	{
		GRK_ERROR("box must be at least 8 bytes in size");
		return false;
	}
	/* process read data */
	uint32_t L = 0;
	grk_read<uint32_t>(p_data, &L);
	box->length = L;
	p_data += 4;
	grk_read<uint32_t>(p_data, &box->type);
	p_data += 4;
	*p_number_bytes_read = 8;
	/* read XL parameter */
	if(box->length == 1)
	{
		if(maxBoxSize < 16)
		{
			GRK_ERROR("Cannot handle XL box of less than 16 bytes");
			return false;
		}
		grk_read<uint64_t>(p_data, &box->length);
		p_data += 8;
		*p_number_bytes_read += 8;

		if(box->length == 0)
		{
			GRK_ERROR("Cannot handle box of undefined sizes");
			return false;
		}
	}
	else if(box->length == 0)
	{
		GRK_ERROR("Cannot handle box of undefined sizes");
		return false;
	}
	if(box->length < *p_number_bytes_read)
	{
		GRK_ERROR("Box length is inconsistent.");
		return false;
	}
	if(box->length > maxBoxSize)
	{
		GRK_ERROR("Stream error while reading JP2 Header box: box length %" PRIu64
				  " is larger than "
				  "maximum box length %" PRIu64 ".",
				  box->length, maxBoxSize);
		return false;
	}
	return true;
}

} // namespace grk
