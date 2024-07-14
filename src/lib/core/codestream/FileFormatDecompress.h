/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

#pragma once

namespace grk
{
typedef std::function<bool(uint8_t* headerData, uint32_t header_size)> BOX_FUNC;

class FileFormatDecompress : public FileFormat, public ICodeStreamDecompress
{
 public:
   FileFormatDecompress(BufferedStream* stream);
   virtual ~FileFormatDecompress();
   bool readHeader(grk_header_info* header_info);
   GrkImage* getImage(uint16_t tile_index);
   GrkImage* getImage(void);
   void init(grk_decompress_core_params* p_param);
   bool setDecompressRegion(grk_rect_single region);
   bool decompress(grk_plugin_tile* tile);
   bool decompressTile(uint16_t tile_index);
   bool end(void);
   bool postProcess(void);
   bool preProcess(void);
   void dump(uint32_t flag, FILE* outputFileStream);

 private:
   grk_color* getColour(void);
   uint32_t read_asoc(AsocBox* parent, uint8_t** header_data, uint32_t* header_data_size,
					  uint32_t asocSize);
   bool readHeaderProcedureImpl(void);
   bool read_box_hdr(FileFormatBox* box, uint32_t* p_number_bytes_read, bool codeStreamBoxWasRead,
					 BufferedStream* stream);
   bool read_ihdr(uint8_t* p_image_header_data, uint32_t image_header_size);
   bool read_xml(uint8_t* p_xml_data, uint32_t xml_size);
   bool read_uuid(uint8_t* headerData, uint32_t header_size);
   bool read_res_box(uint32_t* id, uint32_t* num, uint32_t* den, uint32_t* exponent,
					 uint8_t** p_resolution_data);
   bool read_res(uint8_t* p_resolution_data, uint32_t resolution_size);
   double calc_res(uint16_t num, uint16_t den, uint8_t exponent);
   bool read_bpc(uint8_t* p_bpc_header_data, uint32_t bpc_header_size);
   bool read_channel_definition(uint8_t* p_cdef_header_data, uint32_t cdef_header_size);
   bool read_colr(uint8_t* p_colr_header_data, uint32_t colr_header_size);
   bool read_component_mapping(uint8_t* component_mapping_header_data,
							   uint32_t component_mapping_header_size);
   bool read_palette_clr(uint8_t* p_pclr_header_data, uint32_t pclr_header_size);
   const BOX_FUNC find_handler(uint32_t id);
   const BOX_FUNC img_find_handler(uint32_t id);
   bool read_jp(uint8_t* headerData, uint32_t header_size);
   bool read_ftyp(uint8_t* headerData, uint32_t header_size);
   bool read_jp2h(uint8_t* headerData, uint32_t header_size);
   bool read_box(FileFormatBox* box, uint8_t* p_data, uint32_t* p_number_bytes_read,
				 uint64_t p_box_max_size);
   bool read_asoc(uint8_t* header_data, uint32_t header_data_size);
   void serializeAsoc(AsocBox* asoc, grk_asoc* serial_asocs, uint32_t* num_asocs, uint32_t level);
   std::map<uint32_t, BOX_FUNC> header;
   std::map<uint32_t, BOX_FUNC> img_header;

   bool headerError_;
   AsocBox root_asoc;
   CodeStreamDecompress* codeStream;
   uint32_t jp2_state;
};

} // namespace grk
