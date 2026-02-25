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

#include <exception>
#include <stdexcept>

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "IStream.h"
#include "StreamIO.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
}
#include "CodeStream.h"
#include "FileFormatJP2Family.h"
#include "FileFormatMJ2.h"
#include "FileFormatMJ2Decompress.h"

namespace grk
{

FileFormatMJ2Decompress::FileFormatMJ2Decompress(IStream* stream) : FileFormatMJ2(stream)
{
  std::unordered_map<uint32_t, BOX_FUNC> handlers = {
      {MJ2_MOOV, nullptr},
      {MJ2_MVHD, [this](uint8_t* data, uint32_t len) { return read_mvhd(data, len); }},
      {MJ2_TRAK, nullptr},
      {MJ2_TKHD, [this](uint8_t* data, uint32_t len) { return read_tkhd(data, len); }},
      {MJ2_MDIA, nullptr},
      {MJ2_MDHD, [this](uint8_t* data, uint32_t len) { return read_mdhd(data, len); }},
      {MJ2_MINF, nullptr},
      {MJ2_DINF, nullptr},
      {MJ2_STBL, nullptr},
      {MJ2_HDLR, [this](uint8_t* data, uint32_t len) { return read_hdlr(data, len); }},
      {MJ2_VMHD, [this](uint8_t* data, uint32_t len) { return read_vmhd(data, len); }},
      {MJ2_DREF, [this](uint8_t* data, uint32_t len) { return read_dref(data, len); }},
      {MJ2_STSD, [this](uint8_t* data, uint32_t len) { return read_stsd(data, len); }},
      {MJ2_STTS, [this](uint8_t* data, uint32_t len) { return read_stts(data, len); }},
      {MJ2_STSC, [this](uint8_t* data, uint32_t len) { return read_stsc(data, len); }},
      {MJ2_STSZ, [this](uint8_t* data, uint32_t len) { return read_stsz(data, len); }},
      {MJ2_STCO, [this](uint8_t* data, uint32_t len) { return read_stco(data, len); }},
      {MJ2_MDAT, [this](uint8_t* data, uint32_t len) { return read_mdat(data, len); }}

  };
  header.insert(handlers.begin(), handlers.end());

  handlers = {{MJ2_FIEL, [this](uint8_t* data, uint32_t len) { return read_fiel(data, len); }},
              {MJ2_JP2P, [this](uint8_t* data, uint32_t len) { return read_jp2p(data, len); }},
              {MJ2_JP2X, [this](uint8_t* data, uint32_t len) { return read_jp2x(data, len); }},
              {MJ2_JSUB, [this](uint8_t* data, uint32_t len) { return read_jsub(data, len); }},
              {MJ2_ORFO, [this](uint8_t* data, uint32_t len) { return read_orfo(data, len); }}};
  img_header.insert(handlers.begin(), handlers.end());

  headerImage_ = new GrkImage();
  headerImage_->meta = grk_image_meta_new();
}

GrkImage* FileFormatMJ2Decompress::getHeaderImage(void)
{
  return headerImage_;
}

void FileFormatMJ2Decompress::read_version_and_flag(uint8_t** headerData, uint8_t& version,
                                                    uint32_t& flag)
{
  grk_read(headerData, &version);
  grk_read(headerData, &flag, 3);
}
bool FileFormatMJ2Decompress::read_version_and_flag_check(uint8_t** headerData,
                                                          uint32_t* headerSize, uint8_t maxVersion,
                                                          std::set<uint32_t> allowedFlags)
{
  uint8_t version;
  uint32_t flag;
  read_version_and_flag(headerData, version, flag);
  *headerSize -= 4;
  if(version > maxVersion)
  {
    grklog.error("MJ2 version %d not supported", version);
    return false;
  }
  if(allowedFlags.find(flag) == allowedFlags.end())
  {
    grklog.error("MJ2 flag %d not supported", flag);
    return false;
  }

  return true;
}

bool FileFormatMJ2Decompress::read_fiel(uint8_t* headerData, uint32_t headerSize)
{
  auto tk = current_track_;
  grk_read(&headerData, &headerSize, &tk->fieldcount_);
  grk_read(&headerData, &headerSize, &tk->fieldorder_);

  return true;
}
bool FileFormatMJ2Decompress::read_jp2p(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  auto tk = current_track_;
  uint32_t num_br = headerSize / 4;
  for(uint32_t i = 0; i < num_br; i++)
  {
    uint32_t br;
    grk_read(&headerData, &headerSize, &br);
    tk->br_.push_back(br);
  }

  return true;
}
bool FileFormatMJ2Decompress::read_jp2x([[maybe_unused]] uint8_t* headerData,
                                        [[maybe_unused]] uint32_t headerSize)
{
  // assert(headerSize < 256);
  // auto tk = current_track_;
  // tk->num_jp2x_ = (uint8_t)headerSize;
  // tk->jp2xdata_ = new uint8_t[tk->num_jp2x_];

  // for(uint8_t i = 0; i < tk->num_jp2x_; i++)
  //   tk->jp2xdata_[i] = headerData[i];

  throw new std::runtime_error("unimplemented");
}
bool FileFormatMJ2Decompress::read_jsub(uint8_t* headerData, uint32_t headerSize)
{
  auto tk = current_track_;
  grk_read(&headerData, &headerSize, &tk->hsub_);
  grk_read(&headerData, &headerSize, &tk->vsub_);
  grk_read(&headerData, &headerSize, &tk->hoff_);
  grk_read(&headerData, &headerSize, &tk->voff_);

  return true;
}
bool FileFormatMJ2Decompress::read_orfo(uint8_t* headerData, uint32_t headerSize)
{
  auto tk = current_track_;
  grk_read(&headerData, &headerSize, &tk->or_fieldcount_);
  grk_read(&headerData, &headerSize, &tk->or_fieldorder_);

  return true;
}

bool FileFormatMJ2Decompress::read_mvhd(uint8_t* headerData, uint32_t headerSize)
{
  if(headerSize != 100)
  {
    grklog.error("MVHD box corrupt");
    return false;
  }

  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  grk_read(&headerData, &headerSize, &creation_time_);
  grk_read(&headerData, &headerSize, &modification_time_);
  grk_read(&headerData, &headerSize, &timescale_);
  grk_read(&headerData, &headerSize, &duration_);
  grk_read(&headerData, &headerSize, &rate_);
  grk_read(&headerData, &headerSize, &volume_);

  /* Reserved */
  skip(&headerData, &headerSize, 10);

  for(uint32_t i = 0; i < 9; ++i)
    grk_read(&headerData, &headerSize, trans_matrix_ + i);

  skip(&headerData, &headerSize, 24);
  grk_read(&headerData, &headerSize, &next_tk_id_);

  return true;
}
bool FileFormatMJ2Decompress::read_tkhd(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {1, 2, 3, 4}))
    return false;
  auto trck = new mj2_tk();

  grk_read(&headerData, &headerSize, &trck->creation_time_);
  grk_read(&headerData, &headerSize, &trck->modification_time_);

  int32_t track_id;
  grk_read(&headerData, &headerSize, &track_id);

  // reserved
  skip(&headerData, &headerSize, 4);

  grk_read(&headerData, &headerSize, &trck->duration_);

  // reserved
  skip(&headerData, &headerSize, 8);

  grk_read(&headerData, &headerSize, &trck->layer_);

  // predefined
  skip(&headerData, &headerSize, 2);

  grk_read(&headerData, &headerSize, &trck->volume_);

  // reserved
  skip(&headerData, &headerSize, 2);

  for(uint32_t i = 0; i < 9; ++i)
    grk_read(&headerData, &headerSize, trck->trans_matrix_ + i);

  grk_read(&headerData, &headerSize, &trck->visual_w_);
  grk_read(&headerData, &headerSize, &trck->visual_h_);

  tracks_[track_id] = trck;
  current_track_ = trck;

  return true;
}
bool FileFormatMJ2Decompress::read_mdhd(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  grk_read(&headerData, &headerSize, &current_track_->creation_time_);
  grk_read(&headerData, &headerSize, &current_track_->modification_time_);
  grk_read(&headerData, &headerSize, &current_track_->timescale_);
  grk_read(&headerData, &headerSize, &current_track_->duration_);
  grk_read(&headerData, &headerSize, &current_track_->language_);

  return true;
}
bool FileFormatMJ2Decompress::read_mdat([[maybe_unused]] uint8_t* headerData,
                                        [[maybe_unused]] uint32_t headerSize)
{
  return true;
}

bool FileFormatMJ2Decompress::read_hdlr(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  // reserved
  skip(&headerData, &headerSize, 4);

  grk_read(&headerData, &headerSize, &current_track_->handler_type_);

  // reserved
  skip(&headerData, &headerSize, 12);

  current_track_->name_size_ = headerSize;
  auto name = std::make_unique<char[]>(current_track_->name_size_ + 1);
  name[current_track_->name_size_] = 0;
  memcpy(name.get(), headerData, current_track_->name_size_);
  current_track_->name_ = std::string(name.get());

  return true;
}
bool FileFormatMJ2Decompress::read_vmhd(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {1}))
    return false;

  auto tk = current_track_;
  tk->track_type_ = MJ2_TRACK_TYPE_VIDEO;
  grk_read(&headerData, &headerSize, &tk->graphicsmode_);
  grk_read(&headerData, &headerSize, tk->opcolor_);
  grk_read(&headerData, &headerSize, tk->opcolor_ + 1);
  grk_read(&headerData, &headerSize, tk->opcolor_ + 2);

  return true;
}

bool FileFormatMJ2Decompress::read_url(uint8_t* headerData, uint32_t headerSize)
{
  uint8_t version;
  uint32_t flag;
  grk_read(&headerData, &headerSize, &version);
  grk_read(&headerData, &headerSize, &flag, 3);
  if(version > 0)
  {
    grklog.error("MJ2 version %d not supported", version);
    return false;
  }
  if(flag != 1)
  { /* If flag = 1 --> media data in file */
    mj2_url url;
    for(uint32_t i = 0; i < 4; ++i)
      grk_read(&headerData, &headerSize, url.location_ + i);
    current_track_->url_.push_back(url);
  }

  return true;
}

bool FileFormatMJ2Decompress::read_urn(uint8_t* headerData, uint32_t headerSize)
{
  uint8_t version;
  uint32_t flag;
  grk_read(&headerData, &headerSize, &version);
  grk_read(&headerData, &headerSize, &flag, 3);
  if(version > 0)
  {
    grklog.error("MJ2 version %d not supported", version);
    return false;
  }

  if(flag != 1)
  { /* If flag = 1 --> media data in file */
    mj2_urn urn;
    for(uint32_t i = 0; i < 4; i++)
      grk_read(&headerData, &headerSize, urn.name_ + i);

    for(uint32_t i = 0; i < 4; i++)
      grk_read(&headerData, &headerSize, urn.location_ + i);
    current_track_->urn_.push_back(urn);
  }
  return true;
}

bool FileFormatMJ2Decompress::read_dref(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  uint32_t entry_count;
  grk_read(&headerData, &headerSize, &entry_count);
  for(uint32_t i = 0; i < entry_count; i++)
  {
    Box box;
    uint32_t bytesRead;
    if(!read_box_header(&box, headerData, &bytesRead, headerSize))
      return false;
    headerData += bytesRead;
    headerSize -= bytesRead;
    switch(box.type)
    {
      case MJ2_URL:
        if(!read_url(headerData, headerSize))
        {
          return false;
        }
        break;
      case MJ2_URN:
        if(!read_urn(headerData, headerSize))
        {
          return false;
        }
        break;
      default:
        grklog.error("MJ2 unknown marker %u", box.type);
        return false;
        break;
    }
  }

  return true;
}

/*
 * Read the SMJ2 box
 *
 * Visual Sample Entry Description
 *
 */
bool FileFormatMJ2Decompress::read_smj2(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  auto tk = current_track_;

  skip(&headerData, &headerSize, 4);
  skip(&headerData, &headerSize, 2); /* Pre-defined */
  skip(&headerData, &headerSize, 2); /* Reserved */
  skip(&headerData, &headerSize, 4); /* Pre-defined */
  skip(&headerData, &headerSize, 4); /* Pre-defined */
  skip(&headerData, &headerSize, 4); /* Pre-defined */

  grk_read(&headerData, &headerSize, &tk->w_);
  grk_read(&headerData, &headerSize, &tk->h_);
  grk_read(&headerData, &headerSize, &tk->horizresolution_);
  grk_read(&headerData, &headerSize, &tk->vertresolution_);

  skip(&headerData, &headerSize, 4); /* Reserved */
  skip(&headerData, &headerSize, 2); /* Pre-defined */

  for(uint32_t i = 0; i < 8; ++i)
    grk_read(&headerData, &headerSize, tk->compressorname_ + i);
  grk_read(&headerData, &headerSize, &tk->depth_);
  skip(&headerData, &headerSize, 2); /* Pre-defined */

  uint32_t box_size;
  Box box;
  if(!read_box_header(&box, headerData, &box_size, (uint64_t)headerSize))
    return false;
  headerData += box_size;
  headerSize -= box_size;
  if(box.type != JP2_JP2H)
  {
    grklog.error("Expected jp2h box but got %s box", getBoxName(box).c_str());
    return false;
  }
  read_jp2h(headerData, headerSize);

  return true;
}

bool FileFormatMJ2Decompress::read_stsd(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;
  uint32_t entry_count;
  grk_read(&headerData, &headerSize, &entry_count);
  switch(current_track_->track_type_)
  {
    case MJ2_TRACK_TYPE_VIDEO:
      for(uint32_t i = 0; i < entry_count; i++)
      {
        uint32_t box_size;
        Box box;
        if(!read_box_header(&box, headerData, &box_size, (uint64_t)headerSize))
          return false;
        if(box.type != MJ2_MJ2)
        {
          grklog.error("Expected MJ2_MJ2 box but parsed %d box", box.type);
          return false;
        }
        if(!read_smj2(headerData + box_size, headerSize - box_size))
          return false;
        headerSize -= (uint32_t)box.length;
        headerData += box.length;
      }
      break;
    case MJ2_TRACK_TYPE_SOUND:
    case MJ2_TRACK_TYPE_HINT:
      break;
  }

  return true;
}

/*
 * Time To Sample box Decompact
 *
 */
void FileFormatMJ2Decompress::tts_decompact(mj2_tk* tk)
{
  for(const auto& tts : tk->tts_)
  {
    tk->num_samples_ += tts.samples_count_;
    for(uint32_t j = 0; j < tts.samples_count_; j++)
    {
      mj2_sample sample;
      sample.samples_delta_ = tts.samples_delta_;
      tk->samples_.push_back(sample);
    }
  }
}

bool FileFormatMJ2Decompress::read_stts(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  auto tk = current_track_;
  uint32_t num_tts;
  grk_read(&headerData, &headerSize, &num_tts);
  for(uint32_t i = 0; i < num_tts; i++)
  {
    mj2_tts tts;
    grk_read(&headerData, &headerSize, &tts.samples_count_);
    grk_read(&headerData, &headerSize, &tts.samples_delta_);
    tk->tts_.push_back(tts);
  }

  tts_decompact(tk);

  return true;
}

/*
 * Sample To Chunk box Decompact
 *
 */
void FileFormatMJ2Decompress::stsc_decompact(mj2_tk* tk)
{
  if(tk->sampletochunk_.size() == 1)
  {
    auto num_chunks = (uint32_t)ceil((double)tk->samples_.size() /
                                     (double)tk->sampletochunk_[0].samples_per_chunk_);
    for(uint32_t k = 0; k < num_chunks; k++)
    {
      mj2_chunk chunk;
      chunk.num_samples_ = tk->sampletochunk_[0].samples_per_chunk_;
      tk->chunks_.push_back(chunk);
    }
  }
}

bool FileFormatMJ2Decompress::read_stsc(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;

  auto tk = current_track_;
  uint32_t num_samplestochunk;
  grk_read(&headerData, &headerSize, &num_samplestochunk);
  if(num_samplestochunk != 1)
  {
    grklog.error("STSC: only single sample per chunk is supported");
    return false;
  }
  for(uint32_t i = 0; i < num_samplestochunk; i++)
  {
    mj2_sampletochunk stc;
    grk_read(&headerData, &headerSize, &stc.first_chunk_);
    grk_read(&headerData, &headerSize, &stc.samples_per_chunk_);
    grk_read(&headerData, &headerSize, &stc.samples_descr_idx_);
    tk->sampletochunk_.push_back(stc);
  }
  stsc_decompact(tk); /* decompact sample to chunk box */

  return true;
}

bool FileFormatMJ2Decompress::read_stsz(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;
  auto tk = current_track_;
  uint32_t samples_size;
  grk_read(&headerData, &headerSize, &samples_size);

  if(samples_size != 0)
  { /* Samples do have the same size */
    tk->same_sample_size_ = 1;
    for(auto& sample : tk->samples_)
      sample.samples_size_ = samples_size;
    // sample count = 1
    skip(&headerData, &headerSize, 4);
  }
  else
  {
    tk->same_sample_size_ = 0;
    uint32_t num_samples;
    grk_read(&headerData, &headerSize, &num_samples);
    if(tk->num_samples_ != num_samples)
    {
      grklog.error("STSZ: expected sample count %d does not match track sample"
                   " count %d",
                   num_samples, tk->num_samples_);
      return false;
    }
    for(auto& sample : tk->samples_)
      grk_read(&headerData, &headerSize, &sample.samples_size_);
  }
  return true;
}

void FileFormatMJ2Decompress::stco_decompact(mj2_tk* tk)
{
  uint32_t samples_count = 0;
  for(const auto& chunk : tk->chunks_)
  {
    uint32_t intra_chunk_offset = 0;
    for(uint32_t j = 0; j < chunk.num_samples_; j++)
    {
      tk->samples_[samples_count].offset_ = intra_chunk_offset + chunk.offset_;
      intra_chunk_offset += tk->samples_[samples_count].samples_size_;
      samples_count++;
    }
  }
}
bool FileFormatMJ2Decompress::read_stco(uint8_t* headerData, uint32_t headerSize)
{
  if(!read_version_and_flag_check(&headerData, &headerSize, 0, {0}))
    return false;
  auto tk = current_track_;
  uint32_t num_chunks;
  grk_read(&headerData, &headerSize, &num_chunks);
  if(tk->chunks_.size() != num_chunks)
  {
    grklog.error("STCO: number of chunks %d doesn't match track number of chunks %d", num_chunks,
                 tk->chunks_.size());

    return false;
  }
  for(auto& chunk : tk->chunks_)
    grk_read(&headerData, &headerSize, &chunk.offset_);

  stco_decompact(tk);

  return true;
}

GrkImage* FileFormatMJ2Decompress::getImage([[maybe_unused]] uint16_t tile_index,
                                            [[maybe_unused]] bool wait)
{
  return headerImage_;
};
GrkImage* FileFormatMJ2Decompress::getImage(void)
{
  return headerImage_;
};
void FileFormatMJ2Decompress::init([[maybe_unused]] grk_decompress_parameters* param) {};
bool FileFormatMJ2Decompress::decompressTile([[maybe_unused]] uint16_t tile_index)
{
  return true;
};
void FileFormatMJ2Decompress::dump([[maybe_unused]] uint32_t flag,
                                   [[maybe_unused]] FILE* outputFileStream) {};
void FileFormatMJ2Decompress::wait([[maybe_unused]] grk_wait_swath* swath) {}
bool FileFormatMJ2Decompress::readHeader(grk_header_info* header_info)
{
  bool rc = FileFormatJP2Family::readHeader(header_info, headerImage_);
  if(!rc)
    return false;
  if(current_track_)
  {
    for(const auto& sample : current_track_->samples_)
    {
      auto ptr = stream_->currPtr() + sample.offset_;
      // cross-check sample size with box length
      // as long as box is not XL
      uint32_t len;
      grk_read(ptr, &len);
      assert(len == 1 || len == sample.samples_size_);
    }
  }

  return true;
}
bool FileFormatMJ2Decompress::setProgressionState([[maybe_unused]] grk_progression_state state)
{
  return false;
}

grk_progression_state
    FileFormatMJ2Decompress::getProgressionState([[maybe_unused]] uint16_t tile_index)
{
  return {};
}

bool FileFormatMJ2Decompress::decompress([[maybe_unused]] grk_plugin_tile* tile)
{
  auto tk = current_track_;
  if(!current_track_)
    return false;
  for(uint32_t i = 0; i < current_track_->num_samples_; ++i)
  {
    std::string filename = "$HOME/temp/mj2_" + std::to_string(i) + "_.j2k";
    auto sample = tk->samples_[i];
    auto file = fopen(filename.c_str(), "wb");
    if(file == nullptr)
    {
      std::cerr << "Error opening file for writing." << std::endl;
      return false;
    }
    auto ptr = stream_->currPtr() + sample.offset_;
    // 8 bytes for jp2c box header
    size_t written = fwrite(ptr + 8, sizeof(char), sample.samples_size_, file);
    if(written != sample.samples_size_)
    {
      std::cerr << "Error writing to file." << std::endl;
      fclose(file);
      return 1;
    }
    fclose(file);
  }
  return true;
}

} // namespace grk
