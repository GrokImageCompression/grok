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
class FileFormatCompress : public FileFormat, public ICodeStreamCompress
{
 public:
   FileFormatCompress(BufferedStream* stream);
   virtual ~FileFormatCompress();

   bool init(grk_cparameters* p_param, GrkImage* p_image);
   bool start(void);
   uint64_t compress(grk_plugin_tile* tile);

 private:
   bool end(void);
   grk_color* getColour(void);
   void find_cf(double x, uint16_t* num, uint16_t* den);
   void write_res_box(double resx, double resy, uint32_t box_id, uint8_t** current_res_ptr);
   uint8_t* write_res(uint32_t* p_nb_bytes_written);
   uint8_t* write_bpc(uint32_t* p_nb_bytes_written);
   uint8_t* write_colr(uint32_t* p_nb_bytes_written);
   uint8_t* write_component_mapping(uint32_t* p_nb_bytes_written);
   uint8_t* write_palette_clr(uint32_t* p_nb_bytes_written);
   uint8_t* write_channel_definition(uint32_t* p_nb_bytes_written);
   bool write_jp2h(void);
   uint8_t* write_ihdr(uint32_t* p_nb_bytes_written);
   uint8_t* write_buffer(uint32_t boxId, grk_buf8* buffer, uint32_t* p_nb_bytes_written);
   bool write_uuids(void);
   bool write_ftyp(void);
   bool write_jp2c(void);
   bool write_jp(void);
   bool default_validation(void);
   void init_header_writing();
   void init_end_header_writing(void);
   void init_compressValidation(void);
   uint8_t* write_xml(uint32_t* p_nb_bytes_written);
   bool skip_jp2c(void);

   CodeStreamCompress* codeStream;
   bool needs_xl_jp2c_box_length;
   uint64_t j2k_codestream_offset;
   GrkImage* inputImage_;
};

} // namespace grk
