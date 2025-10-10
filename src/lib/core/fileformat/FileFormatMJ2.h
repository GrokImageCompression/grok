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

#include <unordered_map>

namespace grk
{

#define MJ2_MJ2 0x6d6a7032
#define MJ2_MJ2S 0x6d6a3273
#define MJ2_MDAT 0x6d646174
#define MJ2_MOOV 0x6d6f6f76
#define MJ2_MVHD 0x6d766864
#define MJ2_TRAK 0x7472616b
#define MJ2_TKHD 0x746b6864
#define MJ2_MDIA 0x6d646961
#define MJ2_MDHD 0x6d646864
#define MJ2_MHDR 0x6d686472
#define MJ2_HDLR 0x68646C72
#define MJ2_MINF 0x6d696e66
#define MJ2_VMHD 0x766d6864
#define MJ2_SMHD 0x736d6864
#define MJ2_HMHD 0x686d6864
#define MJ2_DINF 0x64696e66
#define MJ2_DREF 0x64726566
#define MJ2_URL 0x75726c20
#define MJ2_URN 0x75726e20
#define MJ2_STBL 0x7374626c
#define MJ2_STSD 0x73747364
#define MJ2_STTS 0x73747473
#define MJ2_STSC 0x73747363
#define MJ2_STSZ 0x7374737a
#define MJ2_STCO 0x7374636f
#define MJ2_MOOF 0x6d6f6f66
#define MJ2_FREE 0x66726565
#define MJ2_SKIP 0x736b6970
#define MJ2_JP2C 0x6a703263
#define MJ2_FIEL 0x6669656c
#define MJ2_JP2P 0x6a703270
#define MJ2_JP2X 0x6a703278
#define MJ2_JSUB 0x6a737562
#define MJ2_ORFO 0x6f72666f
#define MJ2_MVEX 0x6d766578
#define MJ2_JP2 0x6a703220
#define MJ2_J2P0 0x4a325030

#define MJ2_VIDE 0x76696465
#define MJ2_SOUN 0x736F756E
#define MJ2_HINT 0x68696E74

#define MJ2_TRACK_TYPE_VIDEO 0
#define MJ2_TRACK_TYPE_SOUND 1
#define MJ2_TRACK_TYPE_HINT 2

/**
Time To Sample
*/
struct mj2_tts
{
  mj2_tts() : samples_count_(0), samples_delta_(0) {}
  uint32_t samples_count_;
  int32_t samples_delta_;
};

/**
Chunk
*/
struct mj2_chunk
{
  mj2_chunk() : num_samples_(0), samples_descr_idx_(0), offset_(0) {}
  uint32_t num_samples_;
  uint32_t samples_descr_idx_;
  uint32_t offset_;
};

/**
Sample to chunk
*/
struct mj2_sampletochunk
{
  mj2_sampletochunk() : first_chunk_(0), samples_per_chunk_(0), samples_descr_idx_(0) {}
  uint32_t first_chunk_;
  uint32_t samples_per_chunk_;
  uint32_t samples_descr_idx_;
};

/**
Sample
*/
struct mj2_sample
{
  mj2_sample() : samples_size_(0), offset_(0), samples_delta_(0) {}
  uint32_t samples_size_;
  uint32_t offset_;
  int32_t samples_delta_;
};

/**
URL
*/
struct mj2_url
{
  mj2_url()
  {
    for(uint32_t i = 0; i < 4; ++i)
      location_[i] = 0;
  }
  int32_t location_[4];
};

/**
URN
*/
struct mj2_urn
{
  mj2_urn()
  {
    for(uint32_t i = 0; i < 2; ++i)
      name_[i] = 0;

    for(uint32_t i = 0; i < 4; ++i)
      location_[i] = 0;
  }
  int32_t name_[2];
  int32_t location_[4];
};

/**
Video Track Parameters
*/
struct mj2_tk
{
  mj2_tk();
  ~mj2_tk();
  int32_t track_type_;
  uint32_t creation_time_;
  uint32_t modification_time_;
  int32_t duration_;
  int32_t timescale_;
  int16_t layer_;
  int16_t volume_;
  int16_t language_;
  int16_t balance_;
  int16_t maxPDUsize_;
  int16_t avgPDUsize_;
  int32_t maxbitrate_;
  int32_t avgbitrate_;
  int32_t slidingavgbitrate_;
  int16_t graphicsmode_;
  int16_t opcolor_[3];
  std::vector<mj2_url> url_;
  std::vector<mj2_urn> urn_;
  int32_t Dim_[2];
  int16_t w_;
  int16_t h_;
  int32_t visual_w_;
  int32_t visual_h_;
  int32_t CbCr_subsampling_dx_;
  int32_t CbCr_subsampling_dy_;
  int32_t samples_rate_;
  int32_t samples_description_;
  int32_t horizresolution_;
  int32_t vertresolution_;
  int32_t compressorname_[8];
  int16_t depth_;
  uint8_t fieldcount_;
  uint8_t fieldorder_;
  uint8_t or_fieldcount_;
  uint8_t or_fieldorder_;
  std::vector<uint32_t> br_;
  uint8_t num_jp2x_;
  uint8_t* jp2xdata_;
  uint8_t hsub_;
  uint8_t vsub_;
  uint8_t hoff_;
  uint8_t voff_;
  int32_t trans_matrix_[9];
  uint32_t num_samples_;
  int32_t transorm_;
  int32_t handler_type_;
  uint32_t name_size_;
  uint8_t same_sample_size_;
  std::vector<mj2_tts> tts_;
  std::vector<mj2_chunk> chunks_;
  std::vector<mj2_sampletochunk> sampletochunk_;
  std::string name_;
  std::vector<mj2_sample> samples_;
};

class FileFormatMJ2 : public FileFormatJP2Family
{
public:
  FileFormatMJ2(IStream* stream);
  virtual ~FileFormatMJ2();

protected:
  GrkImage* getHeaderImage(void) override;
  // stores header image information (decompress/compress)
  // decompress: components are subsampled and resolution-reduced
  GrkImage* headerImage_;
  uint32_t creation_time_;
  uint32_t modification_time_;
  int32_t timescale_;
  uint32_t duration_;
  int32_t rate_;
  int32_t num_vtk_;
  int32_t num_stk_;
  int32_t num_htk_;
  int16_t volume_;
  int32_t trans_matrix_[9];
  int32_t next_tk_id_;
  std::unordered_map<int32_t, mj2_tk*> tracks_;
  mj2_tk* current_track_;
};

} // namespace grk
