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

#include "grk_includes.h"

namespace grk
{
FileFormatJP2Decompress::FileFormatJP2Decompress(IStream* stream)
    : FileFormatJP2Family(stream), codeStream(new CodeStreamDecompress(stream))
{
  std::unordered_map<uint32_t, BOX_FUNC> handlers = {
      {JP2_JP2H, [this](uint8_t* data, uint32_t len) { return read_jp2h(data, len); }},
      {JP2_XML, [this](uint8_t* data, uint32_t len) { return read_xml(data, len); }},
      {JP2_UUID, [this](uint8_t* data, uint32_t len) { return read_uuid(data, len); }},
      {JP2_ASOC, [this](uint8_t* data, uint32_t len) { return read_asoc(data, len); }}};
  header.insert(handlers.begin(), handlers.end());

  codeStream->setPostPostProcess([this](GrkImage* img) { return postProcess(img); });
}
FileFormatJP2Decompress::~FileFormatJP2Decompress()
{
  delete codeStream;
}

GrkImage* FileFormatJP2Decompress::getHeaderImage(void)
{
  return codeStream->getHeaderImage();
}
GrkImage* FileFormatJP2Decompress::getImage(uint16_t tile_index, bool wait)
{
  return codeStream->getImage(tile_index, wait);
}
GrkImage* FileFormatJP2Decompress::getImage(void)
{
  return codeStream->getImage();
}

bool FileFormatJP2Decompress::readHeader(grk_header_info* header_info)
{
  if(headerRead_)
    return true;
  auto image = getHeaderImage();
  if(!FileFormatJP2Family::readHeader(header_info, image))
    return false;
  if(codeStream->needsHeaderRead())
  {
    if(!codeStream->readHeader(header_info))
    {
      headerError_ = true;
      return false;
    }
    image = codeStream->getImage();
    image->validateICC();

    // check RGB subsampling
    if(image->color_space == GRK_CLRSPC_SRGB)
    {
      for(uint16_t i = 1; i < image->numcomps; ++i)
      {
        auto comp = image->comps + i;
        if(comp->dx != image->comps->dx || comp->dy != image->comps->dy)
        {
          grklog.error("sRGB colour space mandates uniform sampling in all three components");
          headerError_ = true;
          return false;
        }
      }
    }

    // retrieve special uuids
    for(uint32_t i = 0; i < numUuids; ++i)
    {
      auto uuid = uuids + i;
      if(memcmp(uuid->uuid, IPTC_UUID, 16) == 0)
      {
        if(image->meta->iptc_buf)
        {
          grklog.warn("Attempt to set a second IPTC buffer. Ignoring");
        }
        else if(uuid->num_elts())
        {
          image->meta->iptc_len = uuid->num_elts();
          image->meta->iptc_buf = new uint8_t[uuid->num_elts()];
          memcpy(image->meta->iptc_buf, uuid->buf(), uuid->num_elts());
        }
      }
      else if(memcmp(uuid->uuid, XMP_UUID, 16) == 0)
      {
        if(image->meta->xmp_buf)
        {
          grklog.warn("Attempt to set a second XMP buffer. Ignoring");
        }
        else if(uuid->num_elts())
        {
          image->meta->xmp_len = uuid->num_elts();
          image->meta->xmp_buf = new uint8_t[uuid->num_elts()];
          memcpy(image->meta->xmp_buf, uuid->buf(), uuid->num_elts());
        }
      }
    }
  }

  return true;
}
bool FileFormatJP2Decompress::setProgressionState(grk_progression_state state)
{
  return codeStream->setProgressionState(state);
}
grk_progression_state FileFormatJP2Decompress::getProgressionState(uint16_t tile_index)
{
  return codeStream->getProgressionState(tile_index);
}
void FileFormatJP2Decompress::wait(grk_wait_swath* swath)
{
  codeStream->wait(swath);
}

/** Set up decompressor function handler */
void FileFormatJP2Decompress::init(grk_decompress_parameters* parameters)
{
  FileFormatJP2Family::init(parameters);
  /* set up the J2K codec */
  codeStream->init(parameters);
}
bool FileFormatJP2Decompress::decompress(grk_plugin_tile* tile)
{
  if(!codeStream->decompress(tile))
  {
    grklog.error("Failed to decompress JP2 file");
    return false;
  }

  return true;
}
bool FileFormatJP2Decompress::postProcess(GrkImage* img)
{
  // apply channel definitions
  auto clr = getColour();
  if(clr->channel_definition)
  {
    auto info = clr->channel_definition->descriptions;
    uint16_t n = clr->channel_definition->num_channel_descriptions;
    bool hasPalette = clr->palette && clr->palette->component_mapping;

    for(uint16_t i = 0; i < n; ++i)
    {
      uint16_t channel = info[i].channel;
      // auto img = codeStream->getImage();
      if(!hasPalette && channel >= img->numcomps)
      {
        grklog.error("channel definition: channel=%u must be strictly less than numcomps=%u",
                     channel, img->numcomps);
        return false;
      }
      img->comps[channel].type = (GRK_CHANNEL_TYPE)info[i].typ;
    }
  }
  return true;
}
bool FileFormatJP2Decompress::decompressTile(uint16_t tile_index)
{
  return codeStream->decompressTile(tile_index);
}
void FileFormatJP2Decompress::dump(uint32_t flag, FILE* outputFileStream)
{
  codeStream->dump(flag, outputFileStream);
}

bool FileFormatJP2Decompress::read_xml(uint8_t* p_xml_data, uint32_t xml_size)
{
  if(!p_xml_data || !xml_size)
    return false;

  xml.alloc(xml_size);
  if(!xml.buf())
  {
    xml.set_num_elts(0);
    return false;
  }
  memcpy(xml.buf(), p_xml_data, xml_size);

  return true;
}
bool FileFormatJP2Decompress::read_uuid(uint8_t* headerData, uint32_t headerSize)
{
  if(!headerData || headerSize < 16)
    return false;

  if(headerSize == 16)
  {
    grklog.warn("Read UUID box with no data - ignoring");
    return false;
  }
  if(numUuids == JP2_MAX_NUM_UUIDS)
  {
    grklog.warn("Reached maximum (%u) number of UUID boxes read - ignoring UUID box",
                JP2_MAX_NUM_UUIDS);
    return false;
  }
  auto uuid = uuids + numUuids;
  memcpy(uuid->uuid, headerData, 16);
  headerData += 16;
  uuid->alloc(headerSize - 16);
  memcpy(uuid->buf(), headerData, uuid->num_elts());
  numUuids++;

  return true;
}

} // namespace grk
