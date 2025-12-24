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
const uint32_t JP2_JP2C = 0x6a703263; /** Contiguous code stream box */
const uint32_t JP2_DTBL = 0x6474626c; /** Data Reference box */
const uint32_t JP2_JP2I = 0x6a703269; /** Intellectual property box */
const uint32_t JP2_XML = 0x786d6c20; /** XML box */
const uint32_t JP2_UUID = 0x75756964; /** UUID box */
const uint32_t JP2_UINF = 0x75696e66; /** UUID info box (super-box) */
const uint32_t JP2_ULST = 0x756c7374; /** UUID list box */
const uint32_t JP2_URL = 0x75726c20; /** Data entry URL box */
const uint32_t JP2_ASOC = 0x61736f63; /** Associated data box*/
const uint32_t JP2_LBL = 0x6c626c20; /** Label box*/

const uint8_t IPTC_UUID[16] = {0x33, 0xC7, 0xA4, 0xD2, 0xB8, 0x1D, 0x47, 0x23,
                               0xA0, 0xBA, 0xF1, 0xA3, 0xE0, 0x97, 0xAD, 0x38};
const uint8_t XMP_UUID[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                              0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};

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

private:
  GrkImage* getHeaderImage(void) override;

  bool read_xml(uint8_t* p_xml_data, uint32_t xml_size);
  bool read_uuid(uint8_t* headerData, uint32_t headerSize);

protected:
  CodeStreamDecompress* codeStream;
};

} // namespace grk
