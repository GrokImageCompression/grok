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
// Constants below are only used in decompress path (not needed by compress)
const uint32_t JP2_DTBL = 0x6474626c; /** Data Reference box */
const uint32_t JP2_UINF = 0x75696e66; /** UUID info box (super-box) */
const uint32_t JP2_ULST = 0x756c7374; /** UUID list box */
const uint32_t JP2_URL = 0x75726c20; /** Data entry URL box */
const uint32_t JP2_JUMB = 0x6a756d62; /** JUMBF super box (ISO/IEC 19566-5) */

const uint8_t IPTC_UUID[16] = {0x33, 0xC7, 0xA4, 0xD2, 0xB8, 0x1D, 0x47, 0x23,
                               0xA0, 0xBA, 0xF1, 0xA3, 0xE0, 0x97, 0xAD, 0x38};
const uint8_t XMP_UUID[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                              0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};
// EXIF UUID: ASCII "JpgTiffExif->JP2" (ExifTool convention)
const uint8_t EXIF_UUID[16] = {0x4A, 0x70, 0x67, 0x54, 0x69, 0x66, 0x66, 0x45,
                               0x78, 0x69, 0x66, 0x2D, 0x3E, 0x4A, 0x50, 0x32};
// EXIF UUID written by Photoshop/Adobe JPEG2000 plugin
const uint8_t EXIF_UUID_PS[16] = {0x05, 0x37, 0xCD, 0xAB, 0x9D, 0x0C, 0x44, 0x31,
                                  0xA7, 0x2A, 0xFA, 0x56, 0x1F, 0x2A, 0x11, 0x3E};
// GeoTIFF UUID (GeoJP2): used by GDAL and other GIS tools for georeferencing
const uint8_t GEOTIFF_UUID[16] = {0xB1, 0x4B, 0xF8, 0xBD, 0x08, 0x3D, 0x4B, 0x43,
                                  0xA5, 0xAE, 0x8C, 0xD7, 0xD5, 0xA6, 0xCE, 0x03};
// MSIG UUID: MapInfo worldfile-style georeferencing (legacy)
const uint8_t MSIG_UUID[16] = {0x96, 0xA9, 0xF1, 0xF1, 0xDC, 0x98, 0x40, 0x2D,
                               0xA7, 0xAE, 0xD6, 0x8E, 0x34, 0x45, 0x18, 0x09};

class FileFormatJP2Decompress final : public FileFormatJP2Family, public IDecompressor
{
public:
  FileFormatJP2Decompress(IStream* stream);
  ~FileFormatJP2Decompress();

  bool readHeader(grk_header_info* header_info) override;
  GrkImage* getImage(uint16_t tile_index, bool wait) override;
  GrkImage* getImage(void) override;
  void init(grk_decompress_parameters* param) override;
  grk_progression_state getProgressionState(uint16_t tile_index) override;
  bool setProgressionState(grk_progression_state state) override;
  bool decompress(grk_plugin_tile* tile) override;
  bool decompressTile(uint16_t tile_index) override;
  bool end(void);
  bool postProcess(GrkImage* img);
  void dump(uint32_t flag, FILE* outputFileStream) override;
  void wait(grk_wait_swath* swath) override;
  void scheduleSwathCopy(const grk_wait_swath* swath, grk_swath_buffer* buf) override;
  void waitSwathCopy() override;

private:
  GrkImage* getHeaderImage(void) override;

  bool read_xml(uint8_t* p_xml_data, uint32_t xml_size);
  bool read_uuid(uint8_t* headerData, uint32_t headerSize);
  bool read_ipr(uint8_t* headerData, uint32_t headerSize);

protected:
  CodeStreamDecompress* codeStream;
};

} // namespace grk
