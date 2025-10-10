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

#include "grk_includes.h"

const bool debugBoxes = false;

namespace grk
{
FileFormatJP2Family::FileFormatJP2Family(IStream* stream)
    : procedure_list_(new std::vector<PROCEDURE_FUNC>()), brand(0), minversion(0), numcl(0),
      cl(nullptr), w(0), h(0), numcomps(0), bpc(0), C(0), UnkC(0), IPR(0), meth(0), approx(0),
      enumcs(GRK_ENUM_CLRSPC_UNKNOWN), precedence(0), comps(nullptr), has_capture_resolution(false),
      has_display_resolution(false), io_xml_(false), numUuids(0), jp2_state(0), headerError_(false),
      headerRead_(false), stream_(stream)
{
  for(uint32_t i = 0; i < 2; ++i)
  {
    capture_resolution[i] = 0;
    display_resolution[i] = 0;
  }

  header = {{JP2_JP, [this](uint8_t* data, uint32_t len) { return read_signature(data, len); }},
            {JP2_FTYP, [this](uint8_t* data, uint32_t len) { return read_ftyp(data, len); }}};

  img_header = {
      {JP2_IHDR, [this](uint8_t* data, uint32_t len) { return read_ihdr(data, len); }},
      {JP2_COLR, [this](uint8_t* data, uint32_t len) { return read_colr(data, len); }},
      {JP2_BPCC, [this](uint8_t* data, uint32_t len) { return read_bpc(data, len); }},
      {JP2_PCLR, [this](uint8_t* data, uint32_t len) { return read_palette_clr(data, len); }},
      {JP2_CMAP, [this](uint8_t* data, uint32_t len) { return read_component_mapping(data, len); }},
      {JP2_CDEF,
       [this](uint8_t* data, uint32_t len) { return read_channel_definition(data, len); }},
      {JP2_RES, [this](uint8_t* data, uint32_t len) { return read_res(data, len); }}};
}
FileFormatJP2Family::~FileFormatJP2Family()
{
  xml.dealloc();
  grk_free(cl);
  for(uint32_t i = 0; i < numUuids; ++i)
    (uuids + i)->dealloc();
  delete[] comps;
  delete procedure_list_;
}
void FileFormatJP2Family::init(grk_decompress_parameters* parameters)
{
  io_xml_ = parameters->io_xml;
  if(parameters->outfile[0])
    xml_outfile_ = std::string(parameters->outfile) + ".xml";
}
grk_color* FileFormatJP2Family::getColour(void)
{
  return &getHeaderImage()->meta->color;
}
const FindHandlerInfo FileFormatJP2Family::img_find_handler(uint32_t id)
{
  auto res = img_header.find(id);
  bool superBox = res != img_header.end() && res->second == nullptr;
  auto handler = res != img_header.end() ? res->second : nullptr;
  return FindHandlerInfo(handler, superBox);
}

void FileFormatJP2Family::skip(uint8_t** headerData, uint32_t* headerSize, uint32_t skip)
{
  if(skip > *headerSize)
    throw std::runtime_error("skip: not enough bytes to read data");
  *headerData += skip;
  *headerSize -= skip;
}

bool FileFormatJP2Family::readHeader(grk_header_info* header_info, GrkImage* headerImage)
{
  if(headerError_)
    return false;

  if(!headerRead_)
  {
    /* read header */
    if(!readHeaderProcedure())
    {
      headerError_ = true;
      return false;
    }
    headerRead_ = true;

    if(!headerImage->check_color(numcomps))
    {
      headerError_ = true;
      return false;
    }
    switch(enumcs)
    {
      case GRK_ENUM_CLRSPC_CMYK:
        headerImage->color_space = GRK_CLRSPC_CMYK;
        break;
      case GRK_ENUM_CLRSPC_CIE:
        if(headerImage->meta->color.icc_profile_buf)
        {
          if(((uint32_t*)headerImage->meta->color.icc_profile_buf)[1] == GRK_DEFAULT_CIELAB_SPACE)
            headerImage->color_space = GRK_CLRSPC_DEFAULT_CIE;
          else
            headerImage->color_space = GRK_CLRSPC_CUSTOM_CIE;
        }
        else
        {
          grklog.error("CIE Lab image: ICC profile buffer not present");
          headerError_ = true;
          return false;
        }
        break;
      case GRK_ENUM_CLRSPC_SRGB:
        headerImage->color_space = GRK_CLRSPC_SRGB;
        break;
      case GRK_ENUM_CLRSPC_GRAY:
        headerImage->color_space = GRK_CLRSPC_GRAY;
        break;
      case GRK_ENUM_CLRSPC_SYCC:
        headerImage->color_space = GRK_CLRSPC_SYCC;
        break;
      case GRK_ENUM_CLRSPC_EYCC:
        headerImage->color_space = GRK_CLRSPC_EYCC;
        break;
      default:
        headerImage->color_space = GRK_CLRSPC_UNKNOWN;
        break;
    }
    if(has_capture_resolution)
    {
      headerImage->has_capture_resolution = true;
      for(uint8_t i = 0; i < 2; ++i)
        headerImage->capture_resolution[i] = capture_resolution[i];
    }
    if(has_display_resolution)
    {
      headerImage->has_display_resolution = true;
      for(uint8_t i = 0; i < 2; ++i)
        headerImage->display_resolution[i] = display_resolution[i];
    }

    // set file format fields in header info
    if(header_info)
    {
      // store xml to header and optionally to file
      if(xml.buf() && xml.num_elts())
      {
        header_info->xml_data = xml.buf();
        header_info->xml_data_len = xml.num_elts();
        if(io_xml_ && !xml_outfile_.empty())
        {
          auto fp = fopen(xml_outfile_.c_str(), "wb");
          if(!fp)
          {
            grklog.error("grk_decompress: unable to open file %d for writing xml to",
                         xml_outfile_.c_str());
            return false;
          }
          if(fwrite(header_info->xml_data, 1, header_info->xml_data_len, fp) !=
             header_info->xml_data_len)
          {
            grklog.error("grk_decompress: unable to write all xml data to file %s",
                         xml_outfile_.c_str());
            fclose(fp);
            return false;
          }
          if(fclose(fp))
          {
            grklog.error("grk_decompress: error closing file %s", xml_outfile_.c_str());
            return false;
          }
        }
      }

      // retrieve ASOCs
      header_info->num_asocs = 0;
      if(!root_asoc.children.empty())
        serializeAsoc(&root_asoc, header_info->asocs, &header_info->num_asocs, 0);
    }
  }

  return true;
}

static uint32_t toBigEndian(uint32_t value)
{
#ifdef GROK_BIG_ENDIAN
  return value; // Already in big-endian
#else
  return ((value & 0xFF000000) >> 24) | ((value & 0x00FF0000) >> 8) | ((value & 0x0000FF00) << 8) |
         ((value & 0x000000FF) << 24);
#endif
}

void FileFormatJP2Family::updateSuperBoxes(uint64_t boxBytes)
{
  if(!superBoxes_.empty())
  {
    superBoxes_.top().byteCount += boxBytes;
    // Pop all complete super boxes
    while(!superBoxes_.empty() && superBoxes_.top().byteCount == superBoxes_.top().length)
    {
      Box completedBox = superBoxes_.top(); // Copy the top before popping
      superBoxes_.pop();
      if(!superBoxes_.empty())
        superBoxes_.top().byteCount += completedBox.byteCount;
    }
  }
}

bool FileFormatJP2Family::readHeaderProcedure(void)
{
  Box box;
  uint64_t last_data_size = GRK_BOX_SIZE;
  uint32_t current_data_size;

  bool rc = false;

  bool zeroCopy = stream_->supportsZeroCopy();
  uint8_t* current_data = nullptr;
  if(!zeroCopy)
  {
    current_data = (uint8_t*)grk_calloc(1, last_data_size);
    if(!current_data)
    {
      grklog.error("Not enough memory to handle JPEG 2000 file header");
      return false;
    }
  }

  try
  {
    uint32_t boxHeaderBytesRead;
    bool codeStreamBoxWasRead = false;
    while(read_box_header(&box, stream_, &boxHeaderBytesRead, codeStreamBoxWasRead))
    {
      switch(box.type)
      {
        case JP2_JP2C:
          if(jp2_state & JP2_STATE_HEADER)
          {
            jp2_state |= JP2_STATE_CODESTREAM;
            rc = true;
            goto cleanup;
          }
          else
          {
            grklog.error("corrupt JPEG 2000 code stream");
            goto cleanup;
          }
          codeStreamBoxWasRead = true;
          break;
        default:
          break;
      }
      auto handlerInfo = find_handler(box.type);
      auto misplacedHandlerInfo = img_find_handler(box.type);
      current_data_size = (uint32_t)(box.length - boxHeaderBytesRead);
      if(handlerInfo.valid() || misplacedHandlerInfo.valid())
      {
        if(!handlerInfo.valid())
        {
          grklog.warn("Found a misplaced '%c%c%c%c' box outside jp2h box",
                      (uint8_t)(box.type >> 24), (uint8_t)(box.type >> 16),
                      (uint8_t)(box.type >> 8), (uint8_t)(box.type >> 0));
          if(jp2_state & JP2_STATE_HEADER)
          {
            /* read anyway, we already have jp2h */
            handlerInfo = misplacedHandlerInfo;
          }
          else
          {
            grklog.warn("JPEG2000 Header box not read yet, '%c%c%c%c' box will be ignored",
                        (uint8_t)(box.type >> 24), (uint8_t)(box.type >> 16),
                        (uint8_t)(box.type >> 8), (uint8_t)(box.type >> 0));
            if(!stream_->skip(current_data_size))
            {
              grklog.warn("Problem with skipping JPEG2000 box, stream error");
              // ignore error and return true if code stream box has already been read
              // (we don't worry about any boxes after code stream)
              rc = (jp2_state & JP2_STATE_CODESTREAM) ? true : false;
              goto cleanup;
            }
            continue;
          }
        }
        if(current_data_size > stream_->numBytesLeft())
        {
          /* do not even try to malloc if we can't read */
          grklog.error("Invalid box size %" PRIu64 " for box '%c%c%c%c'. Need %u bytes, %" PRIu64
                       " bytes remaining ",
                       box.length, (uint8_t)(box.type >> 24), (uint8_t)(box.type >> 16),
                       (uint8_t)(box.type >> 8), (uint8_t)(box.type >> 0), current_data_size,
                       stream_->numBytesLeft());
          goto cleanup;
        }
        if(!zeroCopy && current_data_size > last_data_size)
        {
          uint8_t* new_current_data = (uint8_t*)grk_realloc(current_data, current_data_size);
          if(!new_current_data)
          {
            grklog.error("Not enough memory to handle JPEG 2000 box");
            goto cleanup;
          }
          current_data = new_current_data;
          last_data_size = current_data_size;
        }
        if(current_data_size == 0)
        {
          grklog.error("Problem with reading JPEG2000 box, stream error");
          goto cleanup;
        }
        if(debugBoxes)
          grklog.info("Processing header box of type 0x%x, name %s, size %d", box.type,
                      getBoxName(box).c_str(), box.length);
        if(handlerInfo.superBox_)
        {
          box.byteCount = boxHeaderBytesRead;
          superBoxes_.push(box);
          // grklog.info("Push box 0x%x, length %d", box.type, box.length);
        }
        else
        {
          auto bytesRead = (uint32_t)stream_->read(zeroCopy ? nullptr : current_data, &current_data,
                                                   current_data_size);
          if(bytesRead != current_data_size)
          {
            grklog.error("Problem with reading JPEG2000 box, stream error");
            goto cleanup;
          }
          if(!handlerInfo.handler_(current_data, current_data_size))
            goto cleanup;

          updateSuperBoxes(boxHeaderBytesRead + current_data_size);
        }
      }
      else
      {
        if(!(jp2_state & JP2_STATE_SIGNATURE))
        {
          grklog.error("Malformed JP2 file format: first box must be JPEG 2000 signature box");
          goto cleanup;
        }
        if(!(jp2_state & JP2_STATE_FILE_TYPE))
        {
          grklog.error("Malformed JP2 file format: second box must be file type box");
          goto cleanup;
        }
        grklog.warn("Ignoring unknown box of type 0x%x, name %s, size %d", box.type,
                    getBoxName(box).c_str(), current_data_size);

        if(!stream_->skip(current_data_size))
        {
          grklog.warn("Problem with skipping JPEG2000 box, stream error");
          // ignore error and return true if code stream box has already been read
          // (we don't worry about any boxes after code stream)
          rc = (jp2_state & JP2_STATE_CODESTREAM) ? true : false;
          goto cleanup;
        }
        updateSuperBoxes(boxHeaderBytesRead + current_data_size);
      }
    }
    assert(superBoxes_.empty());
    rc = true;
  }
  catch([[maybe_unused]] const CorruptJP2BoxException& ex)
  {
    rc = false;
  }
  catch([[maybe_unused]] const std::runtime_error& re)
  {
    grklog.error("%s", re.what());
    rc = false;
  }
cleanup:
  if(!zeroCopy)
    grk_free(current_data);

  return rc;
}

uint8_t* FileFormatJP2Family::write_buffer(uint32_t boxId, Buffer8* buffer,
                                           uint32_t* p_nb_bytes_written)
{
  assert(p_nb_bytes_written);

  /* need 8 bytes for box plus buffer->len bytes for buffer*/
  uint32_t total_size = 8 + (uint32_t)buffer->num_elts();
  auto data = (uint8_t*)grk_calloc(1, total_size);
  if(!data)
    return nullptr;

  uint8_t* current_ptr = data;

  /* write box size */
  grk_write(&current_ptr, total_size);

  /* write box id */
  grk_write(&current_ptr, boxId);

  /* write buffer data */
  memcpy(current_ptr, buffer->buf(), buffer->num_elts());

  *p_nb_bytes_written = total_size;

  return data;
}
bool FileFormatJP2Family::write_signature(IStream* stream, uint32_t sig)
{
  assert(stream);

  /* write box length */
  if(!stream->write((uint32_t)12))
    return false;
  /* writes box type */
  if(!stream->write(sig))
    return false;
  /* writes magic number*/
  return stream->write(JP2_SIG);
}
bool FileFormatJP2Family::write_ftyp(IStream* stream, uint32_t file_type)
{
  assert(stream);

  uint32_t ftyp_size = 16 + 4 * numcl;
  bool result = true;

  if(!stream->write(ftyp_size))
  {
    result = false;
    goto end;
  }
  if(!stream->write(file_type))
  {
    result = false;
    goto end;
  }
  if(!stream->write(brand))
  {
    result = false;
    goto end;
  }
  /* MinV */
  if(!stream->write(minversion))
  {
    result = false;
    goto end;
  }

  /* CL */
  for(uint32_t i = 0; i < numcl; i++)
  {
    if(!stream->write(cl[i]))
    {
      result = false;
      goto end;
    }
  }

end:
  if(!result)
    grklog.error("Error while writing ftyp data to stream");
  return result;
}

const FindHandlerInfo FileFormatJP2Family::find_handler(uint32_t id)
{
  auto res = header.find(id);
  bool superBox = res != header.end() && !res->second;
  auto handler = res != header.end() ? res->second : BOX_FUNC{};
  return FindHandlerInfo(handler, superBox);
}

bool FileFormatJP2Family::read_signature(uint8_t* headerData, uint32_t headerSize)
{
  uint32_t magic_number;
  assert(headerData != nullptr);

  if(jp2_state != JP2_STATE_NONE)
  {
    grklog.error("The signature box must be the first box in the file.");
    return false;
  }
  /* assure length of data is correct (4 -> magic number) */
  if(headerSize != 4)
  {
    grklog.error("Error with JP signature Box size");
    return false;
  }
  /* rearrange data */
  grk_read(&headerData, &magic_number);
  if(magic_number != JP2_SIG)
  {
    grklog.error("Error with JP Signature : bad magic number");
    return false;
  }
  jp2_state |= JP2_STATE_SIGNATURE;

  return true;
}

bool FileFormatJP2Family::read_ftyp(uint8_t* headerData, uint32_t headerSize)
{
  uint32_t i, remaining_bytes;
  assert(headerData != nullptr);

  if(jp2_state != JP2_STATE_SIGNATURE)
  {
    grklog.error("The ftyp box must be the second box in the file.");
    return false;
  }
  /* assure length of data is correct */
  if(headerSize < 8)
  {
    grklog.error("Error with FTYP signature Box size");
    return false;
  }
  grk_read(&headerData, &brand); /* BR */
  grk_read(&headerData, &minversion); /* MinV */
  remaining_bytes = headerSize - 8;
  /* the number of remaining bytes should be a multiple of 4 */
  if((remaining_bytes & 0x3) != 0)
  {
    grklog.error("Error with FTYP signature Box size");
    return false;
  }
  /* div by 4 */
  numcl = remaining_bytes >> 2;
  if(numcl)
  {
    cl = (uint32_t*)grk_calloc(numcl, sizeof(uint32_t));
    if(cl == nullptr)
    {
      grklog.error("Not enough memory with FTYP Box");
      return false;
    }
  }
  for(i = 0; i < numcl; ++i)
    grk_read(&headerData, &cl[i]); /* CLi */
  jp2_state |= JP2_STATE_FILE_TYPE;

  return true;
}

std::string FileFormatJP2Family::getBoxName(Box box)
{
  char boxTypeChars[5]; // Extra byte for null terminator
  auto be_boxType = toBigEndian(box.type);
  std::memcpy(boxTypeChars, &be_boxType, 4);
  boxTypeChars[4] = '\0'; // Null-terminate the string

  return std::string(boxTypeChars);
}

bool FileFormatJP2Family::read_jp2h(uint8_t* headerData, uint32_t headerSize)
{
  assert(headerData != nullptr);

  /* make sure the box is well placed */
  if((jp2_state & JP2_STATE_FILE_TYPE) != JP2_STATE_FILE_TYPE)
  {
    grklog.error("FTYP box must be first box in the file.");
    return false;
  }
  bool has_ihdr = false;
  /* iterate while remaining data */
  while(headerSize)
  {
    uint32_t box_size;
    Box box;
    if(!read_box_header(&box, headerData, &box_size, (uint64_t)headerSize))
      return false;
    headerData += box_size;
    if(box_size > headerSize)
      throw std::runtime_error("read_jp2h: not enough bytes to read data");
    headerSize -= box_size;
    if(debugBoxes)
      grklog.info("Processing image header box of type 0x%x, name %s, size %d", box.type,
                  getBoxName(box).c_str(), box.length);
    auto current_handler = img_find_handler(box.type);
    uint32_t box_data_length = (uint32_t)(box.length - box_size);
    if(current_handler.handler_)
    {
      try
      {
        if(!current_handler.handler_(headerData, box_data_length))
          return false;
      }
      catch([[maybe_unused]] const std::runtime_error& re)
      {
        grklog.error("%s", re.what());
        return false;
      }
    }
    if(box.type == JP2_IHDR)
      has_ihdr = true;
    headerData += box_data_length;
    headerSize -= box_data_length;
  }
  if(!has_ihdr)
  {
    grklog.error("Stream error while reading JP2 Header box: no 'ihdr' box.");
    return false;
  }
  jp2_state |= JP2_STATE_HEADER;

  return true;
}

bool FileFormatJP2Family::read_ihdr(uint8_t* headerData, uint32_t headerSize)
{
  assert(headerData != nullptr);
  if(comps != nullptr)
  {
    grklog.warn("Ignoring IHDR box. First ihdr box already read");
    return true;
  }
  if(headerSize != 14)
  {
    grklog.error("Corrupt IHDR box: size %d should equal 14", headerSize);
    return false;
  }
  grk_read(&headerData, &h); /* HEIGHT */
  grk_read(&headerData, &w); /* WIDTH */
  if(w == 0 || h == 0)
  {
    grklog.error("IHDR box: invalid dimensions: (%u,%u)", w, h);
    return false;
  }
  grk_read(&headerData, &numcomps); /* NC */
  if((numcomps == 0) || (numcomps > maxNumComponentsJ2K))
  {
    grklog.error("IHDR box: num components=%u does not conform to standard", numcomps);
    return false;
  }
  /* allocate memory for components */
  comps = new ComponentInfo[numcomps];
  grk_read(&headerData, &bpc); /* BPC */
  ///////////////////////////////////////////////////
  // (bits per component == precision -1)
  // Value of 0xFF indicates that bits per component
  // varies by component

  // Otherwise, low 7 bits of bpc determine bits per component,
  // and high bit set indicates signed data,
  // unset indicates unsigned data
  if(((bpc != 0xFF) && ((bpc & 0x7F) > (GRK_MAX_SUPPORTED_IMAGE_PRECISION - 1))))
  {
    grklog.error("IHDR box: bpc=%u not supported.", bpc);
    return false;
  }
  grk_read(&headerData, &C); /* C */
  /* Should be equal to 7 cf. chapter about image header box */
  if(C != 7)
  {
    grklog.error("IHDR box: compression type: %u indicates"
                 " a non-conformant JP2 file.",
                 C);
    return false;
  }
  grk_read(&headerData, &UnkC); /* UnkC */
  // UnkC must be binary : {0,1}
  if((UnkC > 1))
  {
    grklog.error("IHDR box: UnkC=%u does not conform to standard", UnkC);
    return false;
  }
  grk_read(&headerData, &IPR); /* IPR */
  // IPR must be binary : {0,1}
  if((IPR > 1))
  {
    grklog.error("IHDR box: IPR=%u does not conform to standard", IPR);
    return false;
  }

  return true;
}
double FileFormatJP2Family::calc_res(uint16_t num, uint16_t den, uint8_t exponent)
{
  if(den == 0)
    return 0;

  return ((double)num / den) * pow(10, exponent);
}
bool FileFormatJP2Family::read_res_box(uint32_t* id, uint32_t* num, uint32_t* den,
                                       uint32_t* exponent, uint8_t** p_resolution_data)
{
  uint32_t box_size = 4 + 4 + 10;
  uint32_t size = 0;
  grk_read(p_resolution_data, &size);
  if(size != box_size)
  {
    grklog.warn("Bad resolution box : signalled single res box size %d should equal "
                "required single res box size %d. Ignoring.",
                size, box_size);
    return true;
  }
  grk_read(p_resolution_data, id);
  grk_read(*p_resolution_data, num + 1, 2);
  *p_resolution_data += 2;
  grk_read(*p_resolution_data, den + 1, 2);
  *p_resolution_data += 2;
  grk_read(*p_resolution_data, num, 2);
  *p_resolution_data += 2;
  grk_read(*p_resolution_data, den, 2);
  *p_resolution_data += 2;
  grk_read((*p_resolution_data)++, exponent + 1, 1);
  grk_read((*p_resolution_data)++, exponent, 1);

  return true;
}
bool FileFormatJP2Family::read_res(uint8_t* p_resolution_data, uint32_t resolution_size)
{
  assert(p_resolution_data != nullptr);
  uint32_t num_boxes = resolution_size / GRK_RESOLUTION_BOX_SIZE;
  if(num_boxes == 0 || num_boxes > 2 || (resolution_size % GRK_RESOLUTION_BOX_SIZE))
  {
    grklog.warn("Bad resolution box : total box size equals %d while single res box size "
                "equals %d. Ignoring.",
                resolution_size, GRK_RESOLUTION_BOX_SIZE);
    return true;
  }
  while(resolution_size > 0)
  {
    uint32_t id;
    uint32_t num[2];
    uint32_t den[2];
    uint32_t exponent[2];

    if(!read_res_box(&id, num, den, exponent, &p_resolution_data))
      return false;
    double* res;
    switch(id)
    {
      case JP2_CAPTURE_RES:
        res = capture_resolution;
        has_capture_resolution = true;
        break;
      case JP2_DISPLAY_RES:
        res = display_resolution;
        has_display_resolution = true;
        break;
      default:
        return false;
    }
    for(uint8_t i = 0; i < 2; ++i)
      res[i] = calc_res((uint16_t)num[i], (uint16_t)den[i], (uint8_t)exponent[i]);
    resolution_size -= GRK_RESOLUTION_BOX_SIZE;
  }

  return true;
}
bool FileFormatJP2Family::read_bpc(uint8_t* p_bpc_header_data, uint32_t bpc_header_size)
{
  assert(p_bpc_header_data != nullptr);

  if(bpc != 0xFF)
  {
    grklog.warn("A BPC header box is available although BPC given by the IHDR box"
                " (%u) indicate components bit depth is constant",
                bpc);
  }
  if(bpc_header_size != numcomps)
  {
    grklog.error("Bad BPC header box (bad size)");
    return false;
  }

  /* read info for each component */
  for(uint16_t i = 0; i < numcomps; ++i)
  {
    /* read each BPC component */
    grk_read(&p_bpc_header_data, &comps[i].bpc);
  }

  return true;
}
bool FileFormatJP2Family::read_channel_definition([[maybe_unused]] uint8_t* p_cdef_header_data,
                                                  uint32_t cdef_header_size)
{
  assert(p_cdef_header_data != nullptr);
  bool rc = false;
  auto clr = getColour();

  /* Part 1, I.5.3.6: 'The shall be at most one Channel Definition box
   * inside a JP2 Header box.'*/
  if(clr->channel_definition)
    return false;

  if(cdef_header_size < 2)
  {
    grklog.error("CDEF box: Insufficient data.");
    return false;
  }
  uint16_t num_channel_descriptions;
  grk_read(&p_cdef_header_data, &num_channel_descriptions); /* N */
  if(num_channel_descriptions == 0U)
  {
    grklog.error("CDEF box: Number of channel definitions is equal to zero.");
    return false;
  }
  if(cdef_header_size < 2 + (uint32_t)(uint16_t)num_channel_descriptions * 6)
  {
    grklog.error("CDEF box: Insufficient data.");
    return false;
  }
  clr->channel_definition = new grk_channel_definition();
  clr->channel_definition->descriptions = new grk_channel_description[num_channel_descriptions];
  clr->channel_definition->num_channel_descriptions = (uint16_t)num_channel_descriptions;
  auto cdef_info = clr->channel_definition->descriptions;
  for(uint16_t i = 0; i < num_channel_descriptions; ++i)
  {
    grk_read(&p_cdef_header_data, &cdef_info[i].channel); /* Cn^i */
    grk_read(&p_cdef_header_data, &cdef_info[i].typ); /* Typ^i */
    if(cdef_info[i].typ > 2 && cdef_info[i].typ != GRK_CHANNEL_TYPE_UNSPECIFIED)
    {
      grklog.error("CDEF box : Illegal channel type %u", cdef_info[i].typ);
      goto cleanup;
    }
    grk_read(&p_cdef_header_data, &cdef_info[i].asoc); /* Asoc^i */
    if(cdef_info[i].asoc > 3 && cdef_info[i].asoc != GRK_CHANNEL_ASSOC_UNASSOCIATED)
    {
      grklog.error("CDEF box : Illegal channel association %u", cdef_info[i].asoc);
      goto cleanup;
    }
  }

  // cdef sanity check
  // 1. check for multiple descriptions of the same channel with different types
  for(uint16_t i = 0; i < clr->channel_definition->num_channel_descriptions; ++i)
  {
    auto info_i = cdef_info[i];
    for(uint16_t j = 0; j < clr->channel_definition->num_channel_descriptions; ++j)
    {
      auto info_j = cdef_info[j];
      if(i != j && info_i.channel == info_j.channel && info_i.typ != info_j.typ)
      {
        grklog.error("CDEF box : multiple descriptions of channel %u with differing types "
                     ": %u and %u.",
                     info_i.channel, info_i.typ, info_j.typ);
        goto cleanup;
      }
    }
  }

  // 2. check that type/association pairs are unique
  for(uint16_t i = 0; i < clr->channel_definition->num_channel_descriptions; ++i)
  {
    auto info_i = cdef_info[i];
    for(uint16_t j = 0; j < clr->channel_definition->num_channel_descriptions; ++j)
    {
      auto info_j = cdef_info[j];
      if(i != j && info_i.channel != info_j.channel && info_i.typ == info_j.typ &&
         info_i.asoc == info_j.asoc &&
         (info_i.typ != GRK_CHANNEL_TYPE_UNSPECIFIED ||
          info_i.asoc != GRK_CHANNEL_ASSOC_UNASSOCIATED))
      {
        grklog.error("CDEF box : channels %u and %u share same type/association pair (%u,%u).",
                     info_i.channel, info_j.channel, info_j.typ, info_j.asoc);
        goto cleanup;
      }
    }
  }
  rc = true;
cleanup:
  if(!rc)
  {
    delete[] clr->channel_definition->descriptions;
    delete clr->channel_definition;
    clr->channel_definition = nullptr;
  }

  return rc;
}
bool FileFormatJP2Family::read_colr(uint8_t* p_colr_header_data, uint32_t colr_header_size)
{
  assert(p_colr_header_data != nullptr);

  if(colr_header_size < 3)
  {
    grklog.error("Bad COLR header box (bad size)");
    return false;
  }

  auto clr = getColour();

  /* Part 1, I.5.3.3 : 'A conforming JP2 reader shall ignore all colour
   * specification boxes after the first.'
   */
  if(clr->has_colour_specification_box)
  {
    grklog.warn("A conforming JP2 reader shall ignore all colour specification boxes after the "
                "first, so we ignore this one.");
    return true;
  }
  grk_read(&p_colr_header_data, &meth); /* METH */
  grk_read(&p_colr_header_data, &precedence); /* PRECEDENCE */
  grk_read(&p_colr_header_data, &approx); /* APPROX */
  if(meth == 1)
  {
    if(colr_header_size < 7)
    {
      grklog.error("Bad COLR header box (bad size: %u)", colr_header_size);
      return false;
    }
    uint32_t temp;
    grk_read(&p_colr_header_data, &temp); /* EnumCS */
    if(temp != GRK_ENUM_CLRSPC_UNKNOWN && temp != GRK_ENUM_CLRSPC_CMYK &&
       temp != GRK_ENUM_CLRSPC_CIE && temp != GRK_ENUM_CLRSPC_SRGB &&
       temp != GRK_ENUM_CLRSPC_GRAY && temp != GRK_ENUM_CLRSPC_SYCC && temp != GRK_ENUM_CLRSPC_EYCC)
    {
      grklog.warn("Invalid colour space enumeration %u. Ignoring colour box", temp);
      return true;
    }
    enumcs = (GRK_ENUM_COLOUR_SPACE)temp;
    if((colr_header_size > 7) && (enumcs != GRK_ENUM_CLRSPC_CIE))
    { /* handled below for CIELab) */
      grklog.warn("Bad COLR header box (bad size: %u)", colr_header_size);
    }
    if(enumcs == GRK_ENUM_CLRSPC_CIE)
    {
      uint32_t* cielab;
      bool nonDefaultLab = colr_header_size == 35;
      // only two ints are needed for default CIELab space
      cielab = (uint32_t*)new uint8_t[(nonDefaultLab ? 9 : 2) * sizeof(uint32_t)];
      if(cielab == nullptr)
      {
        grklog.error("Not enough memory for cielab");
        return false;
      }
      cielab[0] = GRK_ENUM_CLRSPC_CIE; /* enumcs */
      cielab[1] = GRK_DEFAULT_CIELAB_SPACE;

      if(colr_header_size == 35)
      {
        uint32_t rl, ol, ra, oa, rb, ob, il;
        grk_read(&p_colr_header_data, &rl);
        grk_read(&p_colr_header_data, &ol);
        grk_read(&p_colr_header_data, &ra);
        grk_read(&p_colr_header_data, &oa);
        grk_read(&p_colr_header_data, &rb);
        grk_read(&p_colr_header_data, &ob);
        grk_read(&p_colr_header_data, &il);

        cielab[1] = GRK_CUSTOM_CIELAB_SPACE;
        cielab[2] = rl;
        cielab[4] = ra;
        cielab[6] = rb;
        cielab[3] = ol;
        cielab[5] = oa;
        cielab[7] = ob;
        cielab[8] = il;
      }
      else if(colr_header_size != 7)
      {
        grklog.warn("Bad COLR header box (CIELab, bad size: %u)", colr_header_size);
      }
      clr->icc_profile_buf = (uint8_t*)cielab;
      clr->icc_profile_len = 0;
    }
    clr->has_colour_specification_box = true;
  }
  else if(meth == 2)
  {
    /* ICC profile */
    uint32_t icc_len = (uint32_t)(colr_header_size - 3);
    if(icc_len == 0)
    {
      grklog.error("ICC profile buffer length equals zero");
      return false;
    }
    clr->icc_profile_buf = new uint8_t[(size_t)icc_len];
    memcpy(clr->icc_profile_buf, p_colr_header_data, icc_len);
    clr->icc_profile_len = icc_len;
    clr->has_colour_specification_box = true;
  }
  else
  {
    /*	ISO/IEC 15444-1:2004 (E), Table I.9 Legal METH values:
     conforming JP2 reader shall ignore the entire Colour Specification box.*/
    grklog.warn("COLR BOX meth value is not a regular value (%u); "
                "ignoring Colour Specification box. ",
                meth);
  }

  return true;
}
bool FileFormatJP2Family::read_component_mapping(uint8_t* component_mapping_header_data,
                                                 uint32_t component_mapping_header_size)
{
  uint8_t channel, num_channels;
  assert(component_mapping_header_data != nullptr);

  /* Need num_channels: */
  if(getColour()->palette == nullptr)
  {
    grklog.error("Need to read a PCLR box before the CMAP box.");
    return false;
  }
  /* Part 1, I.5.3.5: 'There shall be at most one Component Mapping box
   * inside a JP2 Header box' :
   */
  if(getColour()->palette->component_mapping)
  {
    grklog.error("Only one CMAP box is allowed.");
    return false;
  }
  num_channels = getColour()->palette->num_channels;
  if(component_mapping_header_size < (uint32_t)num_channels * 4)
  {
    grklog.error("Insufficient data for CMAP box.");
    return false;
  }
  auto component_mapping = new grk_component_mapping_comp[num_channels];
  for(channel = 0; channel < num_channels; ++channel)
  {
    auto mapping = component_mapping + channel;
    grk_read(&component_mapping_header_data, &mapping->component); /* CMP^i */
    grk_read(&component_mapping_header_data, &mapping->mapping_type); /* MTYP^i */
    if(mapping->mapping_type > 1)
    {
      grklog.error("Component mapping type %u for channel %u is greater than 1.",
                   mapping->mapping_type, channel);
      delete[] component_mapping;
      return false;
    }
    grk_read(&component_mapping_header_data, &mapping->palette_column); /* PCOL^i */
  }
  getColour()->palette->component_mapping = component_mapping;

  return true;
}
bool FileFormatJP2Family::read_palette_clr(uint8_t* p_pclr_header_data, uint32_t pclr_header_size)
{
  auto orig_header_data = p_pclr_header_data;
  assert(p_pclr_header_data != nullptr);
  if(getColour()->palette)
    return false;
  if(pclr_header_size < 3)
    return false;
  uint16_t num_entries;
  grk_read(&p_pclr_header_data, &num_entries); /* NE */
  if((num_entries == 0U) || (num_entries > 1024U))
  {
    grklog.error("Invalid PCLR box. Reports %u palette entries", (int)num_entries);
    return false;
  }
  uint8_t num_channels;
  grk_read(&p_pclr_header_data, &num_channels); /* NPC */
  if(!num_channels)
  {
    grklog.error("Invalid PCLR box : 0 palette columns");
    return false;
  }
  if(pclr_header_size < 3 + (uint32_t)num_channels)
    return false;
  getHeaderImage()->allocPalette(num_channels, num_entries);
  auto jp2_pclr = getColour()->palette;
  for(uint8_t i = 0; i < num_channels; ++i)
  {
    uint8_t val;
    grk_read(&p_pclr_header_data, &val); /* Bi */
    jp2_pclr->channel_prec[i] = (uint8_t)((val & 0x7f) + 1);
    if(jp2_pclr->channel_prec[i] > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
    {
      grklog.error("Palette : channel precision %u is greater than supported palette channel "
                   "precision %u",
                   jp2_pclr->channel_prec[i], GRK_MAX_SUPPORTED_IMAGE_PRECISION);
      return false;
    }
    jp2_pclr->channel_sign[i] = (val & 0x80) ? true : false;
    if(jp2_pclr->channel_sign[i])
    {
      grklog.error("Palette : signed channel not supported");
      return false;
    }
  }
  auto lut = jp2_pclr->lut;
  for(uint16_t j = 0; j < num_entries; ++j)
  {
    for(uint8_t i = 0; i < num_channels; ++i)
    {
      uint32_t bytes_to_read = (uint32_t)((jp2_pclr->channel_prec[i] + 7) >> 3);
      if((ptrdiff_t)pclr_header_size <
         (ptrdiff_t)(p_pclr_header_data - orig_header_data) + (ptrdiff_t)bytes_to_read)
      {
        grklog.error("Palette : box too short");
        return false;
      }
      grk_read(p_pclr_header_data, lut++, bytes_to_read); /* Cji */
      p_pclr_header_data += bytes_to_read;
    }
  }

  return true;
}

bool FileFormatJP2Family::read_box_header(Box* box, uint8_t* data, uint32_t* bytesRead,
                                          uint64_t availableBytes)
{
  assert(data != nullptr);
  assert(box != nullptr);
  assert(bytesRead != nullptr);

  if(availableBytes < 8)
  {
    grklog.error("box must be at least 8 bytes in size");
    return false;
  }
  /* process read data */
  uint32_t L = 0;
  grk_read(&data, &L);
  box->length = L;
  grk_read(&data, &box->type);
  *bytesRead = 8;
  /* read XL parameter */
  if(box->length == 1)
  {
    if(availableBytes < 16)
    {
      grklog.error("Cannot handle XL box of less than 16 bytes");
      return false;
    }
    grk_read(data, &box->length);
    data += 8;
    *bytesRead += 8;

    if(box->length == 0)
    {
      grklog.error("Cannot handle box of undefined sizes");
      return false;
    }
  }
  else if(box->length == 0)
  {
    grklog.error("Cannot handle box of undefined sizes");
    return false;
  }
  if(box->length < *bytesRead)
  {
    grklog.error("Box length is inconsistent.");
    return false;
  }
  if(box->length > availableBytes)
  {
    grklog.error("Stream error while reading JP2 Header box %x: box length %" PRIu64
                 " is larger than "
                 "available stream bytes %" PRIu64 ".",
                 box->type, box->length, availableBytes);
    return false;
  }
  return true;
}

bool FileFormatJP2Family::read_box_header(Box* box, IStream* stream, uint32_t* p_number_bytes_read,
                                          bool codeStreamBoxWasRead)
{
  assert(stream);
  assert(box);
  assert(p_number_bytes_read);

  uint8_t* data_header[8];
  uint8_t* dataPtr = (uint8_t*)data_header;
  bool zeroCopy = stream->supportsZeroCopy();
  *p_number_bytes_read = (uint32_t)stream->read(zeroCopy ? nullptr : dataPtr, &dataPtr, 8);

  // we reached EOS
  if(*p_number_bytes_read < 8)
    return false;

  /* process read data */
  uint32_t L = 0;
  grk_read(dataPtr, &L);
  box->length = L;
  grk_read(dataPtr + 4, &box->type);
  if(box->length == 0)
  {
    // treat this as final box if it is the code stream box,
    // or if the code stream box has already been read. Otherwise,
    // treat this as a corrupt box, since we reject a code stream without
    // any code stream box
    if(box->type == JP2_JP2C || codeStreamBoxWasRead)
    {
      box->length = stream->numBytesLeft() + 8U;
    }
    else
    {
      grklog.error("box 0x%x is signalled as final box, but code stream box has not been read.",
                   box->type);
      throw CorruptJP2BoxException();
    }
    return true;
  }
  /* read XL  */
  else if(box->length == 1)
  {
    uint32_t bytesRead = (uint32_t)stream->read(zeroCopy ? nullptr : dataPtr, &dataPtr, 8);
    // we reached EOS
    if(bytesRead < 8)
      return false;
    grk_read(dataPtr, &box->length);
    *p_number_bytes_read += bytesRead;
  }
  if(box->length < *p_number_bytes_read)
  {
    grklog.error("invalid box size %" PRIu64 " (%x)", box->length, box->type);
    throw CorruptJP2BoxException();
  }

  return true;
}
bool FileFormatJP2Family::read_asoc(uint8_t* header_data, uint32_t header_data_size)
{
  assert(header_data);

  // 12 == sizeof(asoc tag) + sizeof(child size) + sizeof(child tag)
  if(header_data_size <= 12)
  {
    grklog.error("ASOC super box can't be empty");
    return false;
  }
  try
  {
    read_asoc(&root_asoc, &header_data, &header_data_size, header_data_size);
  }
  catch([[maybe_unused]] const BadAsocException& bae)
  {
    return false;
  }

  return true;
}
uint32_t FileFormatJP2Family::read_asoc(AsocBox* parent, uint8_t** header_data,
                                        uint32_t* header_data_size, uint32_t asocSize)
{
  assert(*header_data);
  if(asocSize < 8)
  {
    grklog.error("ASOC box must be at least 8 bytes in size");
    throw BadAsocException();
  }
  // create asoc
  auto childAsoc = new AsocBox();
  parent->children.push_back(childAsoc);

  // read all children
  uint32_t asocBytesUsed = 0;
  while(asocBytesUsed < asocSize && *header_data_size > 8)
  {
    uint32_t childSize = 0;
    grk_read(header_data, &childSize);
    if(childSize < 8)
    {
      grklog.error("JP2 box must be at least 8 bytes in size");
      throw BadAsocException();
    }

    *header_data_size -= 4;
    childSize -= 4;
    asocBytesUsed += 4;

    uint32_t childTag = 0;
    grk_read(header_data, &childTag);
    *header_data_size -= 4;
    childSize -= 4;
    asocBytesUsed += 4;

    if(childSize > *header_data_size)
    {
      grklog.error("Not enough space in ASOC box for child box");
      throw BadAsocException();
    }

    switch(childTag)
    {
      case JP2_LBL:
        childAsoc->label = std::string((const char*)*header_data, childSize);
        *header_data += childSize;
        *header_data_size -= childSize;
        asocBytesUsed += childSize;
        break;
      case JP2_ASOC:
        asocBytesUsed += read_asoc(childAsoc, header_data, header_data_size, childSize);
        break;
      case JP2_XML:
        childAsoc->alloc(childSize);
        memcpy(childAsoc->buf(), *header_data, childSize);
        *header_data += childSize;
        *header_data_size -= childSize;
        asocBytesUsed += childSize;
        break;
      default:
        grklog.error("ASOC box has unknown tag 0x%x", childTag);
        throw BadAsocException();
        break;
    }
  }
  if(asocBytesUsed < asocSize)
  {
    grklog.error("ASOC box has extra bytes");
    throw BadAsocException();
  }

  return asocBytesUsed;
}
void FileFormatJP2Family::serializeAsoc(AsocBox* asoc, grk_asoc* serial_asocs, uint32_t* num_asocs,
                                        uint32_t level)
{
  if(*num_asocs == GRK_NUM_ASOC_BOXES_SUPPORTED)
  {
    grklog.warn("Image contains more than maximum supported number of ASOC boxes (%u). Ignoring "
                "the rest",
                GRK_NUM_ASOC_BOXES_SUPPORTED);
    return;
  }
  auto as_c = serial_asocs + *num_asocs;
  as_c->label = asoc->label.c_str();
  as_c->level = level;
  as_c->xml = asoc->buf();
  as_c->xml_len = (uint32_t)asoc->num_elts();
  (*num_asocs)++;
  /*
  if (as_c->level > 0) {
    grklog.info("%s", as_c->label);
    if (as_c->xml)
      grklog.info("%s", std::string((char*)as_c->xml, as_c->xml_len).c_str());
  }
  */
  for(auto& child : asoc->children)
    serializeAsoc(child, serial_asocs, num_asocs, level + 1);
}

bool FileFormatJP2Family::exec(std::vector<PROCEDURE_FUNC>* procs)
{
  assert(procs);

  for(auto it = procs->begin(); it != procs->end(); ++it)
  {
    if(!(*it)())
      return false;
  }
  procs->clear();

  return true;
}

} // namespace grk
