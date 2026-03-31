/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
 */

#pragma once

namespace grk
{
class FileFormatJP2Compress : public FileFormatJP2Family, public ICompressor
{
public:
  FileFormatJP2Compress(IStream* stream);
  virtual ~FileFormatJP2Compress();

  bool init(grk_cparameters* param, GrkImage* image) override;
  bool start(void) override;
  uint64_t compress(grk_plugin_tile* tile) override;

  /* Transcode: write JP2 boxes then copy raw codestream from source */
  uint64_t transcode(IStream* srcStream);

protected:
  GrkImage* getHeaderImage(void) override;
  uint8_t* write_ihdr(uint32_t* p_nb_bytes_written);
  uint8_t* write_colr(uint32_t* p_nb_bytes_written);
  CodeStreamCompress* codeStream = nullptr;
  GrkImage* inputImage_ = nullptr;

private:
  bool end(void);
  grk_color* getColour(void);
  void find_cf(double x, uint16_t* num, uint16_t* den);
  void write_res_box(double resx, double resy, uint32_t box_id, uint8_t** current_res_ptr);
  uint8_t* write_res(uint32_t* p_nb_bytes_written);
  uint8_t* write_bpc(uint32_t* p_nb_bytes_written);
  uint8_t* write_component_mapping(uint32_t* p_nb_bytes_written);
  uint8_t* write_palette_clr(uint32_t* p_nb_bytes_written);
  uint8_t* write_channel_definition(uint32_t* p_nb_bytes_written);
  bool write_jp2h(void);
  bool write_ftyp(void);
  bool write_signature(void);

  bool write_uuids(void);
  bool write_jp2c(void);
  bool default_validation(void);
  void init_header_writing();
  void init_end_header_writing(void);
  uint8_t* write_xml(uint32_t* p_nb_bytes_written);
  bool write_xml_boxes(void);
  bool write_ipr(void);
  bool write_asoc_boxes(void);
  bool write_rreq(void);
  bool skip_jp2c(void);
  uint32_t calcAsocSize(AsocBox* asoc);
  bool writeAsocBox(IStream* stream, AsocBox* asoc);
  void buildAsocTree(const grk_asoc* flat, uint32_t count);

  bool needs_xl_jp2c_box_length = false;
  uint64_t codestream_offset = 0;
  bool jpx_branding_ = false;
  bool write_rreq_ = false;
  uint16_t rreq_standard_features_[8] = {};
  uint8_t num_rreq_standard_features_ = 0;
  bool geoboxes_after_jp2c_ = false;
  bool transcode_mode_ = false;
};

} // namespace grk
