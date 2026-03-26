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

#include <set>
#include <vector>

namespace grk
{

class FileFormatMJ2Decompress : public IDecompressor, public FileFormatMJ2
{
public:
  FileFormatMJ2Decompress(IStream* stream);
  virtual ~FileFormatMJ2Decompress();
  bool readHeader(grk_header_info* header_info) override;
  GrkImage* getImage(uint16_t tile_index, bool wait) override;
  GrkImage* getImage(void) override;
  void init(grk_decompress_parameters* param) override;
  grk_progression_state getProgressionState(uint16_t tile_index) override;
  bool setProgressionState(grk_progression_state state) override;
  bool decompress(grk_plugin_tile* tile) override;
  bool decompressTile(uint16_t tile_index) override;
  void dump(uint32_t flag, FILE* outputFileStream) override;
  void wait(grk_wait_swath* swath) override;

  uint32_t getNumSamples(void) override;
  bool decompressSample(uint32_t sampleIndex) override;
  GrkImage* getSampleImage(uint32_t sampleIndex) override;

private:
  bool decompressSampleInternal(uint32_t sampleIndex);
  GrkImage* getHeaderImage(void) override;
  void read_version_and_flag(uint8_t** headerData, uint8_t& version, uint32_t& flag);
  bool read_version_and_flag_check(uint8_t** headerData, uint32_t* headerSize, uint8_t maxVersion,
                                   std::set<uint32_t> allowedFlags);
  bool read_mvhd(uint8_t* headerData, uint32_t headerSize);
  bool read_tkhd(uint8_t* headerData, uint32_t headerSize);
  bool read_mdhd(uint8_t* headerData, uint32_t headerSize);
  bool read_mdat(uint8_t* headerData, uint32_t headerSize);
  bool read_hdlr(uint8_t* headerData, uint32_t headerSize);
  bool read_vmhd(uint8_t* headerData, uint32_t headerSize);
  bool read_dref(uint8_t* headerData, uint32_t headerSize);
  bool read_stsd(uint8_t* headerData, uint32_t headerSize);
  bool read_stts(uint8_t* headerData, uint32_t headerSize);
  bool read_stsc(uint8_t* headerData, uint32_t headerSize);
  bool read_stsz(uint8_t* headerData, uint32_t headerSize);
  bool read_stco(uint8_t* headerData, uint32_t headerSize);
  bool read_smj2(uint8_t* headerData, uint32_t headerSize);

  bool read_fiel(uint8_t* headerData, uint32_t headerSize);
  bool read_jp2p(uint8_t* headerData, uint32_t headerSize);
  bool read_jp2x(uint8_t* headerData, uint32_t headerSize);
  bool read_jsub(uint8_t* headerData, uint32_t headerSize);
  bool read_orfo(uint8_t* headerData, uint32_t headerSize);

  bool read_url(uint8_t* headerData, uint32_t headerSize);
  bool read_urn(uint8_t* headerData, uint32_t headerSize);

  void tts_decompact(mj2_tk* tk);
  void stsc_decompact(mj2_tk* tk);
  void stco_decompact(mj2_tk* tk);

  grk_decompress_parameters decompressParams_;
  bool decompressParamsSet_;
  std::vector<GrkImage*> decompressedImages_;
};
} // namespace grk
