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

#include <vector>

namespace grk
{

/**
Compression parameters
*/
struct mj2_cparameters
{
  char infile[GRK_PATH_LEN];
  char outfile[GRK_PATH_LEN];
  /** input file format 0:MJ2 */
  int32_t decod_format;
  /** output file format 0:YUV */
  int32_t cod_format;
  /** Portion of the image coded */
  int32_t Dim[2];
  /** YUV Frame width */
  int32_t w;
  /** YUV Frame height */
  int32_t h;
  /*   Sample rate of YUV 4:4:4, 4:2:2 or 4:2:0 */
  int32_t CbCr_subsampling_dx;
  /*   Sample rate of YUV 4:4:4, 4:2:2 or 4:2:0 */
  int32_t CbCr_subsampling_dy;
  /*   Video Frame Rate  */
  int32_t frame_rate;
  /*   In YUV files, numcomps always considered as 3 */
  int32_t numcomps;
  /*   In YUV files, precision always considered as 8 */
  int32_t prec;
  uint32_t meth;
  uint32_t enumcs;
};

/**
 * Records the offset and size of a compressed sample within the mdat box
 */
struct MJ2SampleRecord
{
  uint64_t offset; // absolute file offset of JP2C box start
  uint32_t size; // total size including JP2C box header
};

class FileFormatMJ2Compress : public FileFormatJP2Compress
{
public:
  FileFormatMJ2Compress(IStream* stream);
  virtual ~FileFormatMJ2Compress();

  bool init(grk_cparameters* param, GrkImage* image) override;
  bool start(void) override;
  uint64_t compress(grk_plugin_tile* tile) override;
  uint64_t compressFrame(GrkImage* image, grk_plugin_tile* tile) override;
  bool finalize(void) override;

private:
  bool write_mj2_signature(void);
  bool write_mj2_ftyp(void);
  bool write_moov(void);
  uint8_t* write_mvhd(uint32_t* p_nb_bytes_written);
  uint8_t* write_trak(uint32_t* p_nb_bytes_written);
  uint8_t* write_tkhd(uint32_t* p_nb_bytes_written);
  uint8_t* write_mdia(uint32_t* p_nb_bytes_written);
  uint8_t* write_mdhd(uint32_t* p_nb_bytes_written);
  uint8_t* write_hdlr(uint32_t* p_nb_bytes_written);
  uint8_t* write_minf(uint32_t* p_nb_bytes_written);
  uint8_t* write_vmhd(uint32_t* p_nb_bytes_written);
  uint8_t* write_dinf(uint32_t* p_nb_bytes_written);
  uint8_t* write_stbl(uint32_t* p_nb_bytes_written);
  uint8_t* write_stsd(uint32_t* p_nb_bytes_written);
  uint8_t* write_smj2(uint32_t* p_nb_bytes_written);
  uint8_t* write_stts(uint32_t* p_nb_bytes_written);
  uint8_t* write_stsc(uint32_t* p_nb_bytes_written);
  uint8_t* write_stsz(uint32_t* p_nb_bytes_written);
  uint8_t* write_stco(uint32_t* p_nb_bytes_written);
  bool write_mdat_header(void);
  bool write_mdat_finalize(void);

  uint64_t mdat_offset_;
  uint32_t timescale_;
  uint32_t frame_rate_;
  bool finalized_;
  grk_cparameters compressParams_;
  std::vector<MJ2SampleRecord> sampleRecords_;
};

} // namespace grk
