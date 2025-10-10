/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

class FileFormatMJ2Compress : public FileFormatJP2Compress
{
public:
  FileFormatMJ2Compress(IStream* stream);
  virtual ~FileFormatMJ2Compress();
};

} // namespace grk
