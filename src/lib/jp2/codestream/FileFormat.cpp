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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include "grk_includes.h"
#include <string>

namespace grk {

FileFormat::FileFormat(bool isDecoder, BufferedStream *stream) : codeStream(new CodeStream(isDecoder,stream)),
										m_validation_list(new std::vector<PROCEDURE_FUNC>()),
										m_procedure_list(new std::vector<PROCEDURE_FUNC>()),
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
										m_compress(nullptr),
										m_decompress(nullptr)
{
	for (uint32_t i = 0; i < 2; ++i) {
		capture_resolution[i] = 0;
		display_resolution[i] = 0;
	}

	/* Color structure */
	color.icc_profile_buf = nullptr;
	color.icc_profile_len = 0;
	color.channel_definition = nullptr;
	color.palette = nullptr;
	color.has_colour_specification_box = false;

}
FileFormat::~FileFormat() {
	delete codeStream;
	delete[] comps;
	grk_free(cl);
	FileFormatDecompress::free_color(&color);
	xml.dealloc();
	for (uint32_t i = 0; i < numUuids; ++i)
		(uuids + i)->dealloc();
	delete m_validation_list;
	delete m_procedure_list;
	delete m_compress;
	delete m_decompress;
}
CodeStream* FileFormat::getCodeStream(void){
	return m_decompress->getCodeStream();
}
void FileFormat::createCompress(void){
	m_compress = new FileFormatCompress(codeStream->getStream());
}
bool FileFormat::startCompress(void){
	return m_compress->startCompress();
}
bool FileFormat::initCompress(grk_cparameters  *parameters,GrkImage *image){
	  return m_compress->initCompress(parameters, image);
}
bool FileFormat::compress(grk_plugin_tile* tile){
	return m_compress->compress(tile);
}
bool FileFormat::compressTile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size){
	return m_compress->compressTile(tile_index, p_data, data_size);
}
bool FileFormat::endCompress(void){
	return m_compress->endCompress();
}
void FileFormat::createDecompress(void){
	m_decompress = new FileFormatDecompress(codeStream->getStream());
}
GrkImage* FileFormat::getImage(uint16_t tileIndex){
	return m_decompress->getImage(tileIndex);
}
GrkImage* FileFormat::getImage(void){
	return m_decompress->getImage();
}
/** Main header reading function handler */
bool FileFormat::readHeader(grk_header_info  *header_info){
	return m_decompress->readHeader(header_info);
}
bool FileFormat::setDecompressWindow(grk_rect_u32 window){
	return m_decompress->setDecompressWindow(window);
}
/** Set up decompressor function handler */
void FileFormat::initDecompress(grk_dparameters  *parameters){
	return m_decompress->initDecompress(parameters);
}
bool FileFormat::decompress( grk_plugin_tile *tile){
	return m_decompress->decompress(tile);
}
bool FileFormat::decompressTile(uint16_t tile_index) {
	return m_decompress->decompressTile(tile_index);
}
/** Reading function used after code stream if necessary */
bool FileFormat::endDecompress(void){
	return m_decompress->endDecompress();
}
void FileFormat::dump(uint32_t flag, FILE *out_stream){
	return m_decompress->dump(flag, out_stream);
}

}
