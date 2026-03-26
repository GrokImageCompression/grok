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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "StreamIO.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "FileFormatJP2Family.h"
#include "FileFormatJP2Compress.h"
#include "CodecScheduler.h"
#include "CodeblockCompress.h"
#include "ITileProcessor.h"
#include "ITileProcessorCompress.h"
#include "CodeStreamCompress.h"
#include "FileFormatMJ2.h"
#include "FileFormatMJ2Compress.h"

namespace grk
{

FileFormatMJ2Compress::FileFormatMJ2Compress(IStream* stream)
    : FileFormatJP2Compress(stream), mdat_offset_(0), timescale_(30000), frame_rate_(30),
      finalized_(false), compressParams_{}
{}

FileFormatMJ2Compress::~FileFormatMJ2Compress()
{
  if(!finalized_ && !sampleRecords_.empty())
    finalize();
}

bool FileFormatMJ2Compress::init(grk_cparameters* param, GrkImage* image)
{
  if(!FileFormatJP2Compress::init(param, image))
    return false;

  compressParams_ = *param;

  // override the brand for MJ2
  brand = MJ2_MJ2;
  cl[0] = MJ2_MJ2;

  return true;
}

bool FileFormatMJ2Compress::write_mj2_signature(void)
{
  return FileFormatJP2Family::write_signature(codeStream->getStream(), JP2_JP);
}

bool FileFormatMJ2Compress::write_mj2_ftyp(void)
{
  return FileFormatJP2Family::write_ftyp(codeStream->getStream(), JP2_FTYP);
}

bool FileFormatMJ2Compress::write_mdat_header(void)
{
  auto stream = codeStream->getStream();

  // record mdat start position so we can fix up the size later
  mdat_offset_ = stream->tell();

  // write placeholder size (will be fixed later) + box type
  if(!stream->write((uint32_t)0))
    return false;
  if(!stream->write((uint32_t)MJ2_MDAT))
    return false;

  return true;
}

bool FileFormatMJ2Compress::write_mdat_finalize(void)
{
  auto stream = codeStream->getStream();

  uint64_t mdat_end = stream->tell();
  uint64_t mdat_size = mdat_end - mdat_offset_;

  // seek back to mdat header and write actual size
  if(!stream->seek(mdat_offset_))
    return false;
  if(!stream->write((uint32_t)mdat_size))
    return false;
  // seek back to end
  if(!stream->seek(mdat_end))
    return false;

  return true;
}

bool FileFormatMJ2Compress::start(void)
{
  auto stream = codeStream->getStream();
  if(!stream->hasSeek())
  {
    grklog.error("MJ2 compress requires a seekable stream");
    return false;
  }

  // write file-level header boxes
  if(!write_mj2_signature())
    return false;
  if(!write_mj2_ftyp())
    return false;

  // write mdat header (size placeholder)
  if(!write_mdat_header())
    return false;

  return true;
}

uint64_t FileFormatMJ2Compress::compress(grk_plugin_tile* tile)
{
  auto stream = codeStream->getStream();

  // record sample start offset (absolute file position)
  uint64_t sample_offset = stream->tell();

  // write JP2C box header placeholder (will be fixed)
  if(!stream->write((uint32_t)0))
    return 0;
  if(!stream->write((uint32_t)MJ2_JP2C))
    return 0;

  // initialize and compress the J2K codestream for this frame
  if(!codeStream->start())
    return 0;

  auto rc = codeStream->compress(tile);
  if(!rc)
    return 0;

  uint64_t sample_end = stream->tell();
  uint32_t sample_size = (uint32_t)(sample_end - sample_offset);

  // seek back and write actual JP2C box size
  if(!stream->seek(sample_offset))
    return 0;
  if(!stream->write(sample_size))
    return 0;
  if(!stream->seek(sample_end))
    return 0;

  MJ2SampleRecord rec;
  rec.offset = sample_offset;
  rec.size = sample_size;
  sampleRecords_.push_back(rec);

  return rc;
}

uint64_t FileFormatMJ2Compress::compressFrame(GrkImage* image, grk_plugin_tile* tile)
{
  auto stream = codeStream->getStream();

  uint64_t sample_offset = stream->tell();

  if(!stream->write((uint32_t)0))
    return 0;
  if(!stream->write((uint32_t)MJ2_JP2C))
    return 0;

  // create a fresh per-frame CodeStreamCompress using the shared output stream
  auto frameCS = new CodeStreamCompress(stream);
  if(!frameCS->init(&compressParams_, image))
  {
    delete frameCS;
    return 0;
  }
  if(!frameCS->start())
  {
    delete frameCS;
    return 0;
  }
  auto rc = frameCS->compress(tile);
  delete frameCS;

  if(!rc)
    return 0;

  uint64_t sample_end = stream->tell();
  uint32_t sample_size = (uint32_t)(sample_end - sample_offset);

  if(!stream->seek(sample_offset))
    return 0;
  if(!stream->write(sample_size))
    return 0;
  if(!stream->seek(sample_end))
    return 0;

  MJ2SampleRecord rec;
  rec.offset = sample_offset;
  rec.size = sample_size;
  sampleRecords_.push_back(rec);

  return rc;
}

bool FileFormatMJ2Compress::finalize(void)
{
  if(finalized_)
    return true;
  finalized_ = true;

  if(sampleRecords_.empty())
    return true;

  if(!write_mdat_finalize())
    return false;
  if(!write_moov())
    return false;
  // flush remaining buffered data to disk
  if(!codeStream->getStream()->flush())
    return false;

  return true;
}

// ========== MOOV box writing ==========

uint8_t* FileFormatMJ2Compress::write_mvhd(uint32_t* p_nb_bytes_written)
{
  // MVHD: version(1) + flags(3) + creation_time(4) + modification_time(4)
  //   + timescale(4) + duration(4) + rate(4) + volume(2) + reserved(10)
  //   + matrix(36) + pre_defined(24) + next_track_ID(4) = 108 bytes content
  //   + 8 bytes box header = 108 total content
  uint32_t box_size = 8 + 100;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_MVHD);

  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // creation_time
  grk_write(&ptr, (uint32_t)0);
  // modification_time
  grk_write(&ptr, (uint32_t)0);
  // timescale
  grk_write(&ptr, timescale_);
  // duration = num_samples * (timescale / frame_rate)
  uint32_t sample_delta = timescale_ / frame_rate_;
  uint32_t duration = (uint32_t)sampleRecords_.size() * sample_delta;
  grk_write(&ptr, duration);
  // rate = 1.0 (fixed point 16.16)
  grk_write(&ptr, (uint32_t)0x00010000);
  // volume = 1.0 (fixed point 8.8)
  grk_write(&ptr, (int16_t)0x0100);
  // reserved (10 bytes)
  ptr += 10;
  // unity matrix: [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000]
  uint32_t matrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
  for(int i = 0; i < 9; i++)
    grk_write(&ptr, matrix[i]);
  // pre-defined (24 bytes)
  ptr += 24;
  // next_track_ID
  grk_write(&ptr, (uint32_t)2);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_tkhd(uint32_t* p_nb_bytes_written)
{
  // TKHD: 8 + 84 content bytes = 92 total
  uint32_t box_size = 8 + 84;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_TKHD);

  // version=0, flags=3 (track_enabled | track_in_movie)
  grk_write(&ptr, (uint32_t)0x00000003);
  // creation_time
  grk_write(&ptr, (uint32_t)0);
  // modification_time
  grk_write(&ptr, (uint32_t)0);
  // track_ID
  grk_write(&ptr, (uint32_t)1);
  // reserved
  grk_write(&ptr, (uint32_t)0);
  // duration
  uint32_t sample_delta = timescale_ / frame_rate_;
  uint32_t duration = (uint32_t)sampleRecords_.size() * sample_delta;
  grk_write(&ptr, duration);
  // reserved (8 bytes)
  ptr += 8;
  // layer
  grk_write(&ptr, (int16_t)0);
  // alternate_group (predefined)
  grk_write(&ptr, (int16_t)0);
  // volume (0 for video)
  grk_write(&ptr, (int16_t)0);
  // reserved
  grk_write(&ptr, (int16_t)0);
  // unity matrix
  uint32_t matrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
  for(int i = 0; i < 9; i++)
    grk_write(&ptr, matrix[i]);
  // width (fixed point 16.16)
  auto image = codeStream->getHeaderImage();
  grk_write(&ptr, (uint32_t)((image->x1 - image->x0) << 16));
  // height (fixed point 16.16)
  grk_write(&ptr, (uint32_t)((image->y1 - image->y0) << 16));

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_mdhd(uint32_t* p_nb_bytes_written)
{
  // MDHD: 8 + 24 content = 32 total
  uint32_t box_size = 8 + 24;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_MDHD);

  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // creation_time
  grk_write(&ptr, (uint32_t)0);
  // modification_time
  grk_write(&ptr, (uint32_t)0);
  // timescale
  grk_write(&ptr, timescale_);
  // duration
  uint32_t sample_delta = timescale_ / frame_rate_;
  uint32_t duration = (uint32_t)sampleRecords_.size() * sample_delta;
  grk_write(&ptr, duration);
  // language (undetermined = 0x55C4)
  grk_write(&ptr, (int16_t)0x55C4);
  // pre-defined
  grk_write(&ptr, (int16_t)0);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_hdlr(uint32_t* p_nb_bytes_written)
{
  // HDLR: 8 + 25 content = 33 total
  const char* name = "vide";
  uint32_t name_len = (uint32_t)strlen(name) + 1; // including null terminator
  uint32_t box_size = 8 + 20 + name_len;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_HDLR);

  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // pre-defined
  grk_write(&ptr, (uint32_t)0);
  // handler_type = "vide"
  grk_write(&ptr, (uint32_t)MJ2_VIDE);
  // reserved (12 bytes)
  ptr += 12;
  // name (null-terminated)
  memcpy(ptr, name, name_len);
  ptr += name_len;

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_vmhd(uint32_t* p_nb_bytes_written)
{
  // VMHD: 8 + 12 content = 20 total
  uint32_t box_size = 8 + 12;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_VMHD);

  // version=0, flags=1
  grk_write(&ptr, (uint32_t)0x00000001);
  // graphicsmode (0 = copy)
  grk_write(&ptr, (int16_t)0);
  // opcolor[3]
  grk_write(&ptr, (int16_t)0);
  grk_write(&ptr, (int16_t)0);
  grk_write(&ptr, (int16_t)0);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_dinf(uint32_t* p_nb_bytes_written)
{
  // DINF super-box containing DREF with one self-reference entry
  // DREF: version(4) + entry_count(4) + url_box(12) = 20 bytes content
  // url box: size(4) + type(4) + version(1) + flags(3) = 12 bytes (flag=1 means self-contained)
  uint32_t url_size = 12;
  uint32_t dref_size = 8 + 8 + url_size; // 8 header + 4 vf + 4 count + url
  uint32_t dinf_size = 8 + dref_size;

  auto data = (uint8_t*)grk_calloc(1, dinf_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  // DINF header
  grk_write(&ptr, dinf_size);
  grk_write(&ptr, (uint32_t)MJ2_DINF);

  // DREF header
  grk_write(&ptr, dref_size);
  grk_write(&ptr, (uint32_t)MJ2_DREF);
  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // entry_count = 1
  grk_write(&ptr, (uint32_t)1);
  // URL entry: size=12, type=url, version=0, flags=1 (self-contained)
  grk_write(&ptr, url_size);
  grk_write(&ptr, (uint32_t)MJ2_URL);
  // version=0, flags=1 (media data is in the same file)
  grk_write(&ptr, (uint32_t)0x00000001);

  *p_nb_bytes_written = dinf_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_stts(uint32_t* p_nb_bytes_written)
{
  // STTS: 8 + 4(vf) + 4(entry_count) + 8(entry) = 24 total
  uint32_t box_size = 8 + 4 + 4 + 8;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_STTS);
  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // entry_count = 1 (all samples have the same delta)
  grk_write(&ptr, (uint32_t)1);
  // sample_count
  grk_write(&ptr, (uint32_t)sampleRecords_.size());
  // sample_delta
  uint32_t sample_delta = timescale_ / frame_rate_;
  grk_write(&ptr, sample_delta);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_stsc(uint32_t* p_nb_bytes_written)
{
  // STSC: 8 + 4(vf) + 4(entry_count) + 12(entry) = 28 total
  uint32_t box_size = 8 + 4 + 4 + 12;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_STSC);
  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // entry_count = 1
  grk_write(&ptr, (uint32_t)1);
  // first_chunk = 1
  grk_write(&ptr, (uint32_t)1);
  // samples_per_chunk = 1 (one sample per chunk)
  grk_write(&ptr, (uint32_t)1);
  // sample_description_index = 1
  grk_write(&ptr, (uint32_t)1);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_stsz(uint32_t* p_nb_bytes_written)
{
  uint32_t num_samples = (uint32_t)sampleRecords_.size();
  // STSZ: 8 + 4(vf) + 4(sample_size) + 4(sample_count) + 4*num_samples
  uint32_t box_size = 8 + 4 + 4 + 4 + 4 * num_samples;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_STSZ);
  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // sample_size = 0 (sizes are specified per sample)
  grk_write(&ptr, (uint32_t)0);
  // sample_count
  grk_write(&ptr, num_samples);
  // individual sample sizes
  for(const auto& rec : sampleRecords_)
    grk_write(&ptr, rec.size);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_stco(uint32_t* p_nb_bytes_written)
{
  uint32_t num_chunks = (uint32_t)sampleRecords_.size();
  // STCO: 8 + 4(vf) + 4(entry_count) + 4*num_chunks
  uint32_t box_size = 8 + 4 + 4 + 4 * num_chunks;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_STCO);
  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // entry_count = num_chunks (one sample per chunk)
  grk_write(&ptr, num_chunks);
  // chunk offsets (absolute file positions)
  for(const auto& rec : sampleRecords_)
    grk_write(&ptr, (uint32_t)rec.offset);

  *p_nb_bytes_written = box_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_smj2(uint32_t* p_nb_bytes_written)
{
  auto image = codeStream->getHeaderImage();
  uint32_t img_w = image->x1 - image->x0;
  uint32_t img_h = image->y1 - image->y0;

  // build JP2H sub-box first
  uint32_t jp2h_size = 0;
  auto jp2h_data = FileFormatJP2Compress::write_ihdr(&jp2h_size);
  // we also need a COLR box
  uint32_t colr_size = 0;
  auto colr_data = FileFormatJP2Compress::write_colr(&colr_size);

  uint32_t jp2h_total = 8 + jp2h_size + colr_size;

  // SMJ2 visual sample entry:
  // reserved(6) + data_ref_index(2) + pre-defined(2) + reserved(2) + pre-defined(12)
  // + width(2) + height(2) + horizresolution(4) + vertresolution(4) + reserved(4)
  // + frame_count(2) + compressorname(32) + depth(2) + pre-defined(2)
  // = 78 bytes of fixed content after box header
  uint32_t smj2_fixed = 78;
  // total: mj2 box header(8) + smj2_fixed + jp2h super-box
  uint32_t smj2_size = 8 + smj2_fixed + jp2h_total;

  auto data = (uint8_t*)grk_calloc(1, smj2_size);
  if(!data)
  {
    grk_free(jp2h_data);
    grk_free(colr_data);
    return nullptr;
  }
  auto ptr = data;

  // box header
  grk_write(&ptr, smj2_size);
  grk_write(&ptr, (uint32_t)MJ2_MJ2);

  // reserved (6 bytes) + data_reference_index (2 bytes)
  ptr += 6;
  grk_write(&ptr, (int16_t)1);

  // pre-defined(2) + reserved(2) + pre-defined(12)
  ptr += 16;

  // width, height (as uint16)
  grk_write(&ptr, (int16_t)img_w);
  grk_write(&ptr, (int16_t)img_h);
  // horizresolution = 72 dpi (fixed point 16.16)
  grk_write(&ptr, (uint32_t)0x00480000);
  // vertresolution = 72 dpi
  grk_write(&ptr, (uint32_t)0x00480000);
  // reserved (4 bytes)
  ptr += 4;
  // frame_count = 1
  grk_write(&ptr, (int16_t)1);
  // compressorname (32 bytes, pascal string)
  ptr += 32;
  // depth = 24
  grk_write(&ptr, (int16_t)24);
  // pre-defined = -1
  grk_write(&ptr, (int16_t)-1);

  // JP2H super-box
  grk_write(&ptr, jp2h_total);
  grk_write(&ptr, (uint32_t)JP2_JP2H);
  memcpy(ptr, jp2h_data, jp2h_size);
  ptr += jp2h_size;
  memcpy(ptr, colr_data, colr_size);
  ptr += colr_size;

  grk_free(jp2h_data);
  grk_free(colr_data);

  *p_nb_bytes_written = smj2_size;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_stsd(uint32_t* p_nb_bytes_written)
{
  uint32_t smj2_size = 0;
  auto smj2_data = write_smj2(&smj2_size);
  if(!smj2_data)
    return nullptr;

  // STSD: 8 + 4(vf) + 4(entry_count) + smj2_data
  uint32_t box_size = 8 + 4 + 4 + smj2_size;
  auto data = (uint8_t*)grk_calloc(1, box_size);
  if(!data)
  {
    grk_free(smj2_data);
    return nullptr;
  }
  auto ptr = data;

  grk_write(&ptr, box_size);
  grk_write(&ptr, (uint32_t)MJ2_STSD);
  // version=0, flags=0
  grk_write(&ptr, (uint32_t)0);
  // entry_count = 1
  grk_write(&ptr, (uint32_t)1);
  memcpy(ptr, smj2_data, smj2_size);

  grk_free(smj2_data);

  *p_nb_bytes_written = box_size;
  return data;
}

// ========== Super-box assembly helpers ==========

static uint8_t* assemble_super_box(uint32_t box_type, uint8_t** sub_boxes, uint32_t* sub_sizes,
                                   uint32_t count, uint32_t* p_nb_bytes_written)
{
  uint32_t total = 8;
  for(uint32_t i = 0; i < count; i++)
    total += sub_sizes[i];

  auto data = (uint8_t*)grk_calloc(1, total);
  if(!data)
    return nullptr;
  auto ptr = data;

  grk_write(&ptr, total);
  grk_write(&ptr, box_type);

  for(uint32_t i = 0; i < count; i++)
  {
    memcpy(ptr, sub_boxes[i], sub_sizes[i]);
    ptr += sub_sizes[i];
    grk_free(sub_boxes[i]);
  }

  *p_nb_bytes_written = total;
  return data;
}

uint8_t* FileFormatMJ2Compress::write_stbl(uint32_t* p_nb_bytes_written)
{
  const uint32_t N = 5;
  uint8_t* boxes[N];
  uint32_t sizes[N];

  boxes[0] = write_stsd(&sizes[0]);
  boxes[1] = write_stts(&sizes[1]);
  boxes[2] = write_stsc(&sizes[2]);
  boxes[3] = write_stsz(&sizes[3]);
  boxes[4] = write_stco(&sizes[4]);

  for(uint32_t i = 0; i < N; i++)
  {
    if(!boxes[i])
    {
      for(uint32_t j = 0; j < i; j++)
        grk_free(boxes[j]);
      return nullptr;
    }
  }

  return assemble_super_box(MJ2_STBL, boxes, sizes, N, p_nb_bytes_written);
}

uint8_t* FileFormatMJ2Compress::write_minf(uint32_t* p_nb_bytes_written)
{
  const uint32_t N = 3;
  uint8_t* boxes[N];
  uint32_t sizes[N];

  boxes[0] = write_vmhd(&sizes[0]);
  boxes[1] = write_dinf(&sizes[1]);
  boxes[2] = write_stbl(&sizes[2]);

  for(uint32_t i = 0; i < N; i++)
  {
    if(!boxes[i])
    {
      for(uint32_t j = 0; j < i; j++)
        grk_free(boxes[j]);
      return nullptr;
    }
  }

  return assemble_super_box(MJ2_MINF, boxes, sizes, N, p_nb_bytes_written);
}

uint8_t* FileFormatMJ2Compress::write_mdia(uint32_t* p_nb_bytes_written)
{
  const uint32_t N = 3;
  uint8_t* boxes[N];
  uint32_t sizes[N];

  boxes[0] = write_mdhd(&sizes[0]);
  boxes[1] = write_hdlr(&sizes[1]);
  boxes[2] = write_minf(&sizes[2]);

  for(uint32_t i = 0; i < N; i++)
  {
    if(!boxes[i])
    {
      for(uint32_t j = 0; j < i; j++)
        grk_free(boxes[j]);
      return nullptr;
    }
  }

  return assemble_super_box(MJ2_MDIA, boxes, sizes, N, p_nb_bytes_written);
}

uint8_t* FileFormatMJ2Compress::write_trak(uint32_t* p_nb_bytes_written)
{
  const uint32_t N = 2;
  uint8_t* boxes[N];
  uint32_t sizes[N];

  boxes[0] = write_tkhd(&sizes[0]);
  boxes[1] = write_mdia(&sizes[1]);

  for(uint32_t i = 0; i < N; i++)
  {
    if(!boxes[i])
    {
      for(uint32_t j = 0; j < i; j++)
        grk_free(boxes[j]);
      return nullptr;
    }
  }

  return assemble_super_box(MJ2_TRAK, boxes, sizes, N, p_nb_bytes_written);
}

bool FileFormatMJ2Compress::write_moov(void)
{
  auto stream = codeStream->getStream();

  uint32_t mvhd_size = 0;
  auto mvhd_data = write_mvhd(&mvhd_size);
  if(!mvhd_data)
    return false;

  uint32_t trak_size = 0;
  auto trak_data = write_trak(&trak_size);
  if(!trak_data)
  {
    grk_free(mvhd_data);
    return false;
  }

  uint32_t moov_size = 8 + mvhd_size + trak_size;

  // write MOOV super-box header
  if(!stream->write(moov_size))
    goto fail;
  if(!stream->write((uint32_t)MJ2_MOOV))
    goto fail;

  if(stream->writeBytes(mvhd_data, mvhd_size) != mvhd_size)
    goto fail;
  if(stream->writeBytes(trak_data, trak_size) != trak_size)
    goto fail;

  grk_free(mvhd_data);
  grk_free(trak_data);
  return true;

fail:
  grk_free(mvhd_data);
  grk_free(trak_data);
  return false;
}

} // namespace grk
