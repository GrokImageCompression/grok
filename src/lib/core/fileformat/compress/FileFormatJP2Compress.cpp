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
#include "TLMMarker.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "FileFormatJP2Family.h"
#include "FileFormatJP2Compress.h"
#include "CodecScheduler.h"
#include "CodeblockCompress.h"
#include "Codec.h"

#include <algorithm>
#include "ITileProcessor.h"
#include "ITileProcessorCompress.h"
#include "FileFormatJP2Decompress.h"
#include "CodeStreamCompress.h"

namespace grk
{
void MycmsLogErrorHandlerFunction([[maybe_unused]] cmsContext ContextID,
                                  [[maybe_unused]] cmsUInt32Number ErrorCode, const char* Text)
{
  grklog.warn(" LCMS error: {}", Text);
}

FileFormatJP2Compress::FileFormatJP2Compress(IStream* stream)
    : FileFormatJP2Family(stream), codeStream(new CodeStreamCompress(stream))
{}
FileFormatJP2Compress::~FileFormatJP2Compress()
{
  grk_unref(inputImage_);
  delete codeStream;
}

GrkImage* FileFormatJP2Compress::getHeaderImage(void)
{
  return codeStream->getHeaderImage();
}

grk_color* FileFormatJP2Compress::getColour(void)
{
  if(!inputImage_ || !inputImage_->meta)
    return nullptr;

  return &inputImage_->meta->color;
}
bool FileFormatJP2Compress::write_signature(void)
{
  return FileFormatJP2Family::write_signature(codeStream->getStream(), JP2_JP);
}
bool FileFormatJP2Compress::write_jp2c(void)
{
  auto stream = codeStream->getStream();
  assert(stream);

  assert(stream->hasSeek());

  uint64_t codestream_exit = stream->tell();
  if(!stream->seek(codestream_offset))
  {
    grklog.error("Failed to seek in the stream.");
    return false;
  }

  /* size of code stream */
  uint64_t actualLength = codestream_exit - codestream_offset;
  // initialize signalledLength to 0, indicating length was not known
  // when file was written
  uint32_t signalledLength = 0;
  if(needs_xl_jp2c_box_length)
    signalledLength = 1;
  else
  {
    if(actualLength < (uint64_t)1 << 32)
      signalledLength = (uint32_t)actualLength;
  }
  if(!stream->write(signalledLength))
    return false;
  if(!stream->write(JP2_JP2C))
    return false;
  // XL box
  if(signalledLength == 1)
  {
    if(!stream->write<uint64_t>(actualLength))
      return false;
  }
  if(!stream->seek(codestream_exit))
  {
    grklog.error("Failed to seek in the stream.");
    return false;
  }

  return true;
}
bool FileFormatJP2Compress::write_ftyp(void)
{
  return FileFormatJP2Family::write_ftyp(codeStream->getStream(), JP2_FTYP);
}
bool FileFormatJP2Compress::write_uuids(void)
{
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  // write the uuids
  for(size_t i = 0; i < numUuids; ++i)
  {
    auto uuid = uuids + i;
    if(uuid->buf() && uuid->num_elts())
    {
      /* write box size */
      stream->write((uint32_t)(8 + 16 + uuid->num_elts()));

      /* JP2_UUID */
      stream->write(JP2_UUID);

      /* uuid  */
      stream->writeBytes(uuid->uuid, 16);

      /* uuid data */
      stream->writeBytes(uuid->buf(), (uint32_t)uuid->num_elts());
    }
  }
  return true;
}
bool FileFormatJP2Compress::write_jp2h(void)
{
  BoxWriteHandler writers[32];
  int32_t i, nb_writers = 0;
  /* size of data for super box*/
  uint32_t jp2h_size = 8;
  bool result = true;
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  writers[nb_writers++].handler =
      std::bind(&FileFormatJP2Compress::write_ihdr, this, std::placeholders::_1);
  if(bpc == 0xFF)
    writers[nb_writers++].handler =
        std::bind(&FileFormatJP2Compress::write_bpc, this, std::placeholders::_1);
  writers[nb_writers++].handler =
      std::bind(&FileFormatJP2Compress::write_colr, this, std::placeholders::_1);
  if(inputImage_->meta)
  {
    if(getColour()->channel_definition)
      writers[nb_writers++].handler =
          std::bind(&FileFormatJP2Compress::write_channel_definition, this, std::placeholders::_1);
    if(getColour()->palette)
    {
      writers[nb_writers++].handler =
          std::bind(&FileFormatJP2Compress::write_palette_clr, this, std::placeholders::_1);
      writers[nb_writers++].handler =
          std::bind(&FileFormatJP2Compress::write_component_mapping, this, std::placeholders::_1);
    }
  }
  if(has_display_resolution || has_capture_resolution)
  {
    bool storeCapture = capture_resolution[0] > 0 && capture_resolution[1] > 0;
    bool storeDisplay = display_resolution[0] > 0 && display_resolution[1] > 0;
    if(storeCapture || storeDisplay)
      writers[nb_writers++].handler =
          std::bind(&FileFormatJP2Compress::write_res, this, std::placeholders::_1);
  }
  for(i = 0; i < nb_writers; ++i)
  {
    auto current_writer = writers + i;
    current_writer->data_ = current_writer->handler(&current_writer->size_);
    if(current_writer->data_ == nullptr)
    {
      grklog.error("Not enough memory to hold JP2 Header data");
      result = false;
      break;
    }
    jp2h_size += current_writer->size_;
  }

  if(!result)
  {
    for(i = 0; i < nb_writers; ++i)
    {
      auto current_writer = writers + i;
      grk_free(current_writer->data_);
    }
    return false;
  }

  /* write super box size */
  if(!stream->write(jp2h_size))
    result = false;
  if(!stream->write(JP2_JP2H))
    result = false;

  if(result)
  {
    for(i = 0; i < nb_writers; ++i)
    {
      auto current_writer = writers + i;
      if(stream->writeBytes(current_writer->data_, current_writer->size_) != current_writer->size_)
      {
        result = false;
        break;
      }
    }
  }
  for(i = 0; i < nb_writers; ++i)
  {
    auto current_writer = writers + i;
    grk_free(current_writer->data_);
  }

  return result;
}
uint8_t* FileFormatJP2Compress::write_palette_clr(uint32_t* p_nb_bytes_written)
{
  auto palette = getColour()->palette;
  assert(palette);

  uint32_t bytesPerEntry = 0;
  for(uint32_t i = 0; i < palette->num_channels; ++i)
    bytesPerEntry += ((palette->channel_prec[i] + 7U) / 8U);

  uint32_t boxSize =
      4U + 4U + 2U + 1U + palette->num_channels + bytesPerEntry * palette->num_entries;

  uint8_t* paletteBuf = (uint8_t*)grk_malloc(boxSize);
  uint8_t* palette_ptr = paletteBuf;

  /* box size */
  grk_write(&palette_ptr, boxSize);

  /* PCLR */
  grk_write(&palette_ptr, (uint32_t)JP2_PCLR);

  // number of LUT entries
  grk_write(&palette_ptr, palette->num_entries);

  // number of channels
  grk_write(&palette_ptr, palette->num_channels);

  for(uint8_t i = 0; i < palette->num_channels; ++i)
    grk_write(&palette_ptr, (uint8_t)(palette->channel_prec[i] - 1U)); // Bi

  // LUT values for all components
  auto lut_ptr = palette->lut;
  for(uint16_t j = 0; j < palette->num_entries; ++j)
  {
    for(uint8_t i = 0; i < palette->num_channels; ++i)
    {
      uint32_t bytes_to_write = (uint32_t)((palette->channel_prec[i] + 7U) >> 3);
      grk_write(palette_ptr, *lut_ptr, bytes_to_write); /* Cji */
      lut_ptr++;
      palette_ptr += bytes_to_write;
    }
  }

  *p_nb_bytes_written = boxSize;

  return paletteBuf;
}
uint8_t* FileFormatJP2Compress::write_component_mapping(uint32_t* p_nb_bytes_written)
{
  auto palette = getColour()->palette;
  uint32_t boxSize = 4 + 4 + palette->num_channels * 4U;

  auto cmapBuf = (uint8_t*)grk_malloc(boxSize);
  auto cmapPtr = cmapBuf;

  /* box size */
  grk_write(&cmapPtr, boxSize);

  /* CMAP */
  grk_write(&cmapPtr, (uint32_t)JP2_CMAP);

  for(uint32_t i = 0; i < palette->num_channels; ++i)
  {
    auto map = palette->component_mapping + i;
    grk_write(&cmapPtr, map->component); /* CMP^i */
    grk_write(&cmapPtr, map->mapping_type); /* MTYP^i */
    grk_write(&cmapPtr, map->palette_column); /* PCOL^i */
  }

  *p_nb_bytes_written = boxSize;

  return cmapBuf;
}
uint8_t* FileFormatJP2Compress::write_colr(uint32_t* p_nb_bytes_written)
{
  /* room for 8 bytes for box 3 for common data and variable upon profile*/
  uint32_t colr_size = 11;
  assert(p_nb_bytes_written != nullptr);
  assert(meth == 1 || meth == 2);

  switch(meth)
  {
    case 1:
      colr_size += 4; /* EnumCS */
      break;
    case 2:
      assert(getColour()->icc_profile_len); /* ICC profile */
      colr_size += getColour()->icc_profile_len;
      break;
    default:
      return nullptr;
  }

  auto colr_data = (uint8_t*)grk_calloc(1, colr_size);
  if(!colr_data)
    return nullptr;

  auto current_colr_ptr = colr_data;

  /* write box size */
  grk_write(&current_colr_ptr, colr_size);

  /* BPCC */
  grk_write(&current_colr_ptr, (uint32_t)JP2_COLR);

  /* METH */
  grk_write(&current_colr_ptr, meth);
  /* PRECEDENCE */
  grk_write(&current_colr_ptr, precedence);
  /* APPROX */
  grk_write(&current_colr_ptr, approx);

  /* Meth value is restricted to 1 or 2 (Table I.9 of part 1) */
  if(meth == 1)
  {
    /* EnumCS */
    grk_write(&current_colr_ptr, enumcs);
  }
  else
  {
    const auto clr = getColour();
    /* ICC profile */
    if(meth == 2)
    {
      memcpy(current_colr_ptr, clr->icc_profile_buf, clr->icc_profile_len);
      current_colr_ptr += clr->icc_profile_len;
    }
  }
  *p_nb_bytes_written = colr_size;

  return colr_data;
}
uint8_t* FileFormatJP2Compress::write_channel_definition(uint32_t* p_nb_bytes_written)
{
  /* 8 bytes for box, 2 for n */
  uint32_t cdef_size = 10;
  auto clr = getColour();
  assert(p_nb_bytes_written != nullptr);
  assert(clr->channel_definition != nullptr);
  assert(clr->channel_definition->descriptions != nullptr);
  assert(clr->channel_definition->num_channel_descriptions > 0U);

  cdef_size += 6U * clr->channel_definition->num_channel_descriptions;

  auto cdef_data = (uint8_t*)grk_malloc(cdef_size);
  if(!cdef_data)
    return nullptr;

  auto current_cdef_ptr = cdef_data;

  /* write box size */
  grk_write(&current_cdef_ptr, cdef_size);

  /* BPCC */
  grk_write(&current_cdef_ptr, (uint32_t)JP2_CDEF);

  /* N */
  grk_write(&current_cdef_ptr, clr->channel_definition->num_channel_descriptions);

  for(uint16_t i = 0U; i < clr->channel_definition->num_channel_descriptions; ++i)
  {
    /* Cni */
    grk_write(&current_cdef_ptr, clr->channel_definition->descriptions[i].channel);
    /* Typi */
    grk_write(&current_cdef_ptr, clr->channel_definition->descriptions[i].typ);
    /* Asoci */
    grk_write(&current_cdef_ptr, clr->channel_definition->descriptions[i].asoc);
  }
  *p_nb_bytes_written = cdef_size;

  return cdef_data;
}
uint8_t* FileFormatJP2Compress::write_bpc(uint32_t* p_nb_bytes_written)
{
  assert(p_nb_bytes_written != nullptr);

  uint32_t i;
  /* room for 8 bytes for box and 1 byte for each component */
  uint32_t bpcc_size = 8U + numcomps;

  auto bpcc_data = (uint8_t*)grk_calloc(1, bpcc_size);
  if(!bpcc_data)
    return nullptr;

  auto current_bpc_ptr = bpcc_data;

  /* write box size */
  grk_write(&current_bpc_ptr, bpcc_size);

  /* BPCC */
  grk_write(&current_bpc_ptr, (uint32_t)JP2_BPCC);

  for(i = 0; i < numcomps; ++i)
    grk_write(&current_bpc_ptr, comps[i].bpc);
  *p_nb_bytes_written = bpcc_size;

  return bpcc_data;
}
uint8_t* FileFormatJP2Compress::write_res(uint32_t* p_nb_bytes_written)
{
  uint8_t *res_data = nullptr, *current_res_ptr = nullptr;
  assert(p_nb_bytes_written);

  bool storeCapture = capture_resolution[0] > 0 && capture_resolution[1] > 0;
  bool storeDisplay = display_resolution[0] > 0 && display_resolution[1] > 0;

  uint32_t size = (4 + 4) + GRK_RESOLUTION_BOX_SIZE;
  if(storeCapture && storeDisplay)
    size += GRK_RESOLUTION_BOX_SIZE;

  res_data = (uint8_t*)grk_calloc(1, size);
  if(!res_data)
    return nullptr;

  current_res_ptr = res_data;

  /* write super-box size */
  grk_write(&current_res_ptr, size);

  /* Super-box ID */
  grk_write(&current_res_ptr, (uint32_t)JP2_RES);

  if(storeCapture)
    write_res_box(capture_resolution[0], capture_resolution[1], JP2_CAPTURE_RES, &current_res_ptr);
  if(storeDisplay)
    write_res_box(display_resolution[0], display_resolution[1], JP2_DISPLAY_RES, &current_res_ptr);
  *p_nb_bytes_written = size;

  return res_data;
}

// https://shreevatsa.wordpress.com/2011/01/10/not-all-best-rational-approximations-are-the-convergents-of-the-continued-fraction/
void FileFormatJP2Compress::find_cf(double x, uint16_t* num, uint16_t* den)
{
  // number of terms in continued fraction.
  // 15 is the max without precision errors for M_PI
  const size_t MAX_ITER = 15;
  const double eps = 1.0 / USHRT_MAX;
  long p[MAX_ITER], q[MAX_ITER], a[MAX_ITER];

  size_t i;
  // The first two convergents are 0/1 and 1/0
  p[0] = 0;
  q[0] = 1;

  p[1] = 1;
  q[1] = 0;
  // The rest of the convergents (and continued fraction)
  for(i = 2; i < MAX_ITER; ++i)
  {
    a[i] = lrint(floor(x));
    p[i] = a[i] * p[i - 1] + p[i - 2];
    q[i] = a[i] * q[i - 1] + q[i - 2];
    bool overflow = (p[i] > USHRT_MAX) || (q[i] > USHRT_MAX);
    if(fabs(x - (double)a[i]) < eps || overflow)
      break;
    x = 1.0 / (x - (double)a[i]);
  }

  *num = (uint16_t)(p[i - 1]);
  *den = (uint16_t)(q[i - 1]);
}
void FileFormatJP2Compress::write_res_box(double resx, double resy, uint32_t box_id,
                                          uint8_t** current_res_ptr)
{
  /* write box size */
  grk_write(current_res_ptr, (uint32_t)GRK_RESOLUTION_BOX_SIZE);

  /* Box ID */
  grk_write(current_res_ptr, box_id);

  double res[2];
  // y is written first, then x
  res[0] = resy;
  res[1] = resx;

  uint16_t num[2];
  uint16_t den[2];
  int32_t exponent[2];

  for(size_t i = 0; i < 2; ++i)
  {
    // special case when res[i] is a whole number.
    exponent[i] = 0;
    double r = res[i];
    while(floor(r) == r)
    {
      if(r <= USHRT_MAX)
        break;
      r /= 10;
      exponent[i]++;
    }
    if(floor(r) == r)
    {
      num[i] = (uint16_t)r;
      den[i] = 1;
      continue;
    }
    //////////////////////////////////////////

    exponent[i] = (int32_t)log10(res[i]);
    if(exponent[i] < 1)
      exponent[i] = 0;
    if(exponent[i] >= 1)
      res[i] /= pow(10, exponent[i]);
    find_cf(res[i], num + i, den + i);
  }
  for(size_t i = 0; i < 2; ++i)
  {
    grk_write(current_res_ptr, num[i]);
    grk_write(current_res_ptr, den[i]);
  }
  for(size_t i = 0; i < 2; ++i)
    grk_write(current_res_ptr, (uint8_t)exponent[i]);
}
uint8_t* FileFormatJP2Compress::write_xml(uint32_t* p_nb_bytes_written)
{
  return write_buffer(JP2_XML, &xml, p_nb_bytes_written);
}
bool FileFormatJP2Compress::write_xml_boxes(void)
{
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  // write the primary xml box
  if(xml.buf() && xml.num_elts())
  {
    uint32_t size = 0;
    auto data = write_xml(&size);
    if(data)
    {
      if(stream->writeBytes(data, size) != size)
      {
        grk_free(data);
        return false;
      }
      grk_free(data);
    }
  }

  // write additional xml boxes
  for(uint32_t i = 0; i < numXmlBoxes; ++i)
  {
    auto& xb = xml_boxes[i];
    if(xb.buf() && xb.num_elts())
    {
      uint32_t size = 0;
      auto data = write_buffer(JP2_XML, &xb, &size);
      if(data)
      {
        if(stream->writeBytes(data, size) != size)
        {
          grk_free(data);
          return false;
        }
        grk_free(data);
      }
    }
  }

  return true;
}
bool FileFormatJP2Compress::write_ipr(void)
{
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  if(!ipr.buf() || !ipr.num_elts())
    return true;

  uint32_t size = 0;
  auto data = write_buffer(JP2_JP2I, &ipr, &size);
  if(!data)
    return false;

  bool result = (stream->writeBytes(data, size) == size);
  grk_free(data);

  return result;
}

uint8_t* FileFormatJP2Compress::write_ihdr(uint32_t* p_nb_bytes_written)
{
  assert(p_nb_bytes_written);

  /* default image header is 22 bytes wide */
  auto ihdr_data = (uint8_t*)grk_calloc(1, 22);
  if(ihdr_data == nullptr)
    return nullptr;

  auto current_ihdr_ptr = ihdr_data;

  /* write box size */
  grk_write(&current_ihdr_ptr, (uint32_t)22);

  /* IHDR */
  grk_write(&current_ihdr_ptr, (uint32_t)JP2_IHDR);

  /* HEIGHT */
  grk_write(&current_ihdr_ptr, h);

  /* WIDTH */
  grk_write(&current_ihdr_ptr, w);

  /* NC */
  grk_write(&current_ihdr_ptr, numcomps);

  /* BPC */
  grk_write(&current_ihdr_ptr, bpc);

  /* C : Always 7 */
  grk_write(&current_ihdr_ptr, C);

  /* UnkC, colorspace unknown */
  grk_write(&current_ihdr_ptr, UnkC);

  /* IPR, no intellectual property */
  grk_write(&current_ihdr_ptr, IPR);

  *p_nb_bytes_written = 22;

  return ihdr_data;
}
bool FileFormatJP2Compress::start(void)
{
  /* validation of the parameters codec */
  if(!default_validation())
    return false;

  /* customization of the compressing */
  init_header_writing();

  // estimate if codec stream may be larger than 2^32 bytes
  auto image = transcode_mode_ ? inputImage_ : codeStream->getHeaderImage();
  uint64_t image_size = 0;
  for(auto i = 0U; i < image->numcomps; ++i)
  {
    auto comp = image->comps + i;
    image_size += (uint64_t)comp->w * comp->h * ((comp->prec + 7U) / 8);
  }
  needs_xl_jp2c_box_length = (image_size > (uint64_t)1 << 30) ? true : false;

  /* write header */
  if(!exec(procedure_list_))
    return false;

  /* In transcode mode, skip codestream header writing */
  if(transcode_mode_)
    return true;

  return codeStream->start();
}
void FileFormatJP2Compress::setTranscodeOptions(const grk_cparameters* parameters)
{
  write_tlm_transcode_ = parameters->write_tlm;
  write_plt_transcode_ = parameters->write_plt;
  write_sop_transcode_ = parameters->write_sop;
  write_eph_transcode_ = parameters->write_eph;
  max_layers_transcode_ = parameters->max_layers_transcode;
  max_res_transcode_ = parameters->max_res_transcode;
  transcode_prog_order_ = parameters->transcode_prog_order;
  transcode_src_ = parameters->transcode_src;
}

bool FileFormatJP2Compress::init(grk_cparameters* parameters, GrkImage* image)
{
  uint16_t i;
  uint8_t depth_0;
  uint32_t sign = 0;
  uint32_t alpha_count = 0;
  uint16_t color_channels = 0U;

  if(!parameters || !image)
    return false;

  inputImage_ = grk_ref(image);
  transcode_mode_ = parameters->transcode;
  if(transcode_mode_)
    setTranscodeOptions(parameters);

  cmsSetLogErrorHandler(MycmsLogErrorHandlerFunction);

  /* In transcode mode, skip codestream encoding setup */
  if(!transcode_mode_)
  {
    if(codeStream->init(parameters, inputImage_) == false)
      return false;
  }

  /* Profile box */
  brand = parameters->cblk_sty == GRK_CBLKSTY_HT_ONLY ? JP2_JPH : JP2_JP2; /* BR */
  jpx_branding_ = parameters->jpx_branding;
  write_rreq_ = parameters->write_rreq;
  geoboxes_after_jp2c_ = parameters->geoboxes_after_jp2c;
  if(write_rreq_)
  {
    num_rreq_standard_features_ = parameters->num_rreq_standard_features;
    for(uint8_t sf = 0; sf < num_rreq_standard_features_ && sf < 8; ++sf)
      rreq_standard_features_[sf] = parameters->rreq_standard_features[sf];
  }
  if(jpx_branding_)
    brand = JP2_JPX;
  bool jph_branding = (brand == JP2_JPH);
  minversion = 0; /* MinV */
  numcl = jpx_branding_ ? 2 : 1;
  cl = (uint32_t*)grk_malloc(sizeof(uint32_t) * numcl);
  if(!cl)
  {
    grklog.error("Not enough memory when set up the JP2 compressor");
    return false;
  }
  if(jph_branding)
  {
    /* ISO/IEC 15444-15: a JPH file's compatibility list is 'jph ' only
       (matches the OpenHTJ2K reference writer; a Part-1 reader cannot
       decode an HT codestream, so 'jp2 ' would be a false claim). */
    cl[0] = JP2_JPH;
  }
  else
  {
    cl[0] = JP2_JP2;
    if(jpx_branding_)
      cl[1] = JP2_JPX;
  }

  /* Image Header box */
  numcomps = inputImage_->numcomps; /* NC */
  comps = new ComponentInfo[numcomps];

  h = inputImage_->y1 - inputImage_->y0;
  w = inputImage_->x1 - inputImage_->x0;
  depth_0 = (uint8_t)(inputImage_->comps[0].prec - 1);
  sign = inputImage_->comps[0].sgnd;
  bpc = (uint8_t)(depth_0 + (sign << 7));
  for(i = 1; i < inputImage_->numcomps; i++)
  {
    uint32_t depth = inputImage_->comps[i].prec - 1U;
    sign = inputImage_->comps[i].sgnd;
    if(depth_0 != depth)
      bpc = 0xFF;
  }
  C = 7; /* C : Always 7 */
  UnkC = 0; /* UnkC, colorspace specified in colr box */
  IPR = (inputImage_->meta && inputImage_->meta->ipr_len && inputImage_->meta->ipr_data) ? 1 : 0;

  /* bit per component box */
  for(i = 0; i < inputImage_->numcomps; i++)
  {
    comps[i].bpc = (uint8_t)(inputImage_->comps[i].prec - 1);
    if(inputImage_->comps[i].sgnd)
      comps[i].bpc = (uint8_t)(comps[i].bpc + (1 << 7));
  }

  inputImage_->validateICC();

  /* Colour Specification box */
  if(inputImage_->color_space == GRK_CLRSPC_ICC)
  {
    meth = 2;
    enumcs = GRK_ENUM_CLRSPC_UNKNOWN;
  }
  else
  {
    meth = 1;
    if(inputImage_->color_space == GRK_CLRSPC_CMYK)
      enumcs = GRK_ENUM_CLRSPC_CMYK;
    else if(inputImage_->color_space == GRK_CLRSPC_DEFAULT_CIE)
      enumcs = GRK_ENUM_CLRSPC_CIE;
    else if(inputImage_->color_space == GRK_CLRSPC_SRGB)
      enumcs = GRK_ENUM_CLRSPC_SRGB; /* sRGB as defined by IEC 61966-2-1 */
    else if(inputImage_->color_space == GRK_CLRSPC_GRAY)
      enumcs = GRK_ENUM_CLRSPC_GRAY; /* greyscale */
    else if(inputImage_->color_space == GRK_CLRSPC_SYCC)
      enumcs = GRK_ENUM_CLRSPC_SYCC; /* YUV */
    else if(inputImage_->color_space == GRK_CLRSPC_EYCC)
      enumcs = GRK_ENUM_CLRSPC_EYCC; /* YUV */
    else
    {
      grklog.error("Unsupported colour space enumeration %u", inputImage_->color_space);
      return false;
    }
  }

  // transfer buffer to uuid
  if(inputImage_->meta)
  {
    const uint32_t metadataFlags = parameters->metadata_write_flags;
    auto writesMetadata = [metadataFlags](uint32_t typeFlag) {
      if(metadataFlags & GRK_METADATA_WRITE_NONE)
        return false;
      return metadataFlags == GRK_METADATA_WRITE_ALL || (metadataFlags & typeFlag) != 0;
    };
    if(writesMetadata(GRK_METADATA_WRITE_IPTC) && inputImage_->meta->iptc_len &&
       inputImage_->meta->iptc_buf)
      uuids[numUuids++] =
          UUIDBox(IPTC_UUID, inputImage_->meta->iptc_buf, inputImage_->meta->iptc_len);

    if(writesMetadata(GRK_METADATA_WRITE_XMP) && inputImage_->meta->xmp_len &&
       inputImage_->meta->xmp_buf)
      uuids[numUuids++] = UUIDBox(XMP_UUID, inputImage_->meta->xmp_buf, inputImage_->meta->xmp_len);

    if(writesMetadata(GRK_METADATA_WRITE_EXIF) && inputImage_->meta->exif_len &&
       inputImage_->meta->exif_buf)
      uuids[numUuids++] =
          UUIDBox(EXIF_UUID, inputImage_->meta->exif_buf, inputImage_->meta->exif_len);

    if(inputImage_->meta->geotiff_len && inputImage_->meta->geotiff_buf)
      uuids[numUuids++] =
          UUIDBox(GEOTIFF_UUID, inputImage_->meta->geotiff_buf, inputImage_->meta->geotiff_len);

    if(inputImage_->meta->ipr_len && inputImage_->meta->ipr_data)
    {
      ipr.alloc(inputImage_->meta->ipr_len);
      if(ipr.buf())
        memcpy(ipr.buf(), inputImage_->meta->ipr_data, inputImage_->meta->ipr_len);
    }
    // XML box
    if(inputImage_->meta->xml_len && inputImage_->meta->xml_buf)
    {
      xml.alloc(inputImage_->meta->xml_len);
      if(xml.buf())
        memcpy(xml.buf(), inputImage_->meta->xml_buf, inputImage_->meta->xml_len);
    }
    // Build asoc tree from flat representation
    if(inputImage_->meta->asoc_boxes && inputImage_->meta->num_asoc_boxes > 0)
      buildAsocTree(inputImage_->meta->asoc_boxes, inputImage_->meta->num_asoc_boxes);
  }
  /* Channel Definition box */
  for(i = 0; i < inputImage_->numcomps; i++)
  {
    if(inputImage_->comps[i].type != GRK_CHANNEL_TYPE_COLOUR)
    {
      alpha_count++;
      // technically, this is an error, but we will let it pass
      if(inputImage_->comps[i].sgnd)
        grklog.warn("signed alpha channel %u", i);
    }
  }
  switch(enumcs)
  {
    case GRK_ENUM_CLRSPC_CMYK:
      color_channels = 4;
      break;
    case GRK_ENUM_CLRSPC_CIE:
    case GRK_ENUM_CLRSPC_SRGB:
    case GRK_ENUM_CLRSPC_SYCC:
    case GRK_ENUM_CLRSPC_EYCC:
      color_channels = 3;
      break;
    case GRK_ENUM_CLRSPC_GRAY:
      color_channels = 1;
      break;
    default:
      break;
  }
  if(alpha_count)
  {
    if(!inputImage_->meta)
      inputImage_->meta = grk_image_meta_new();
    auto clr = getColour();
    clr->channel_definition = new grk_channel_definition();
    clr->channel_definition->descriptions = new grk_channel_description[inputImage_->numcomps];
    clr->channel_definition->num_channel_descriptions = inputImage_->numcomps;
    for(i = 0U; i < color_channels; i++)
    {
      clr->channel_definition->descriptions[i].channel = i;
      clr->channel_definition->descriptions[i].typ = GRK_CHANNEL_TYPE_COLOUR;
      clr->channel_definition->descriptions[i].asoc = (uint16_t)(i + 1U);
    }
    for(; i < inputImage_->numcomps; i++)
    {
      clr->channel_definition->descriptions[i].channel = i;
      clr->channel_definition->descriptions[i].typ = inputImage_->comps[i].type;
      clr->channel_definition->descriptions[i].asoc = inputImage_->comps[i].association;
    }
  }
  precedence = 0; /* PRECEDENCE */
  approx = 0; /* APPROX */
  has_capture_resolution =
      parameters->write_capture_resolution || parameters->write_capture_resolution_from_file;
  if(parameters->write_capture_resolution)
  {
    for(i = 0; i < 2; ++i)
      capture_resolution[i] = parameters->capture_resolution[i];
  }
  else if(parameters->write_capture_resolution_from_file)
  {
    for(i = 0; i < 2; ++i)
      capture_resolution[i] = parameters->capture_resolution_from_file[i];
  }
  if(parameters->write_display_resolution)
  {
    has_display_resolution = true;
    display_resolution[0] = parameters->display_resolution[0];
    display_resolution[1] = parameters->display_resolution[1];
    // if display resolution equals (0,0), then use capture resolution
    // if available
    if(parameters->display_resolution[0] == 0 && parameters->display_resolution[1] == 0)
    {
      if(has_capture_resolution)
      {
        display_resolution[0] = parameters->capture_resolution[0];
        display_resolution[1] = parameters->capture_resolution[1];
      }
      else
      {
        has_display_resolution = false;
      }
    }
  }

  return true;
}
uint64_t FileFormatJP2Compress::compress(grk_plugin_tile* tile)
{
  auto rc = codeStream->compress(tile);
  if(rc && !end())
    return 0;

  // flush any buffered data to the output stream
  if(rc)
  {
    auto stream = codeStream->getStream();
    if(stream && !stream->flush())
      return 0;
  }

  return rc;
}
bool FileFormatJP2Compress::startsWithCodestream(IStream* stream)
{
  uint8_t first[2];
  const bool isCodestream =
      stream->read(first, nullptr, 2) == 2 && ((first[0] << 8) | first[1]) == SOC;
  if(!stream->seek(0))
    throw std::runtime_error("transcode: failed to seek to start of source");
  return isCodestream;
}

FileFormatJP2Compress::CodestreamExtent FileFormatJP2Compress::locateCodestream(IStream* srcStream)
{
  if(startsWithCodestream(srcStream))
    return {0, srcStream->numBytesLeft()};

  Box box;
  uint32_t boxHeaderBytesRead;
  const bool codeStreamBoxWasRead = false;
  while(read_box_header(&box, srcStream, &boxHeaderBytesRead, codeStreamBoxWasRead))
  {
    const uint64_t dataLength = box.length - boxHeaderBytesRead;
    if(box.type == JP2_JP2C)
      return {srcStream->tell(), dataLength};
    if(dataLength > 0 && !srcStream->skip((int64_t)dataLength))
      throw std::runtime_error("transcode: failed to skip box data in source");
  }
  throw std::runtime_error("transcode: no JP2C (codestream) box found in source");
}

uint64_t FileFormatJP2Compress::writeTranscodedCodestream(IStream* srcStream,
                                                          CodestreamExtent extent)
{
  if(!srcStream->seek(extent.offset))
  {
    grklog.error("transcode: failed to seek to codestream in source");
    return 0;
  }

  auto dstStream = codeStream->getStream();
  assert(dstStream);

  uint64_t totalWritten = 0;
  /* The full marker-patching path (transcodeCodestream) must be taken
     whenever *any* marker mutation is requested, not just TLM. Otherwise
     SOP/EPH/PLT/progression/quality-reduction requests would silently fall
     through to the verbatim copy path and have no effect. */
  bool needMarkerPatching = write_tlm_transcode_ || write_plt_transcode_ || write_sop_transcode_ ||
                            write_eph_transcode_ || max_layers_transcode_ > 0 ||
                            max_res_transcode_ > 0 || transcode_prog_order_ != GRK_PROG_UNKNOWN;
  if(needMarkerPatching)
  {
    totalWritten = transcodeCodestream(srcStream, extent.offset, extent.length);
  }
  else
  {
    /* Copy the raw codestream verbatim */
    const size_t copyBufSize = 1024 * 1024;
    auto copyBuf = std::make_unique<uint8_t[]>(copyBufSize);
    uint64_t remaining = extent.length;

    while(remaining > 0)
    {
      size_t toRead = (size_t)std::min((uint64_t)copyBufSize, remaining);
      size_t bytesRead = srcStream->read(copyBuf.get(), nullptr, toRead);
      if(bytesRead == 0)
      {
        grklog.error("transcode: unexpected end of source stream");
        return 0;
      }
      if(dstStream->writeBytes(copyBuf.get(), bytesRead) != bytesRead)
      {
        grklog.error("transcode: failed to write codestream to destination");
        return 0;
      }
      remaining -= bytesRead;
      totalWritten += bytesRead;
    }
  }

  return totalWritten;
}

uint64_t FileFormatJP2Compress::transcode(IStream* srcStream)
{
  const uint64_t totalWritten = writeTranscodedCodestream(srcStream, locateCodestream(srcStream));
  if(totalWritten == 0)
    return 0;

  /* Finalize: write jp2c box header and any post-codestream boxes */
  if(!end())
    return 0;

  if(!codeStream->getStream()->flush())
    return 0;

  return totalWritten;
}

uint64_t FileFormatJP2Compress::transcodeToRawCodestream(const grk_cparameters* parameters,
                                                         IStream* srcStream)
{
  setTranscodeOptions(parameters);
  const uint64_t totalWritten = writeTranscodedCodestream(srcStream, locateCodestream(srcStream));
  if(totalWritten == 0 || !codeStream->getStream()->flush())
    return 0;
  return totalWritten;
}

uint64_t FileFormatJP2Compress::transcodeCodestream(IStream* srcStream, uint64_t csStart,
                                                    uint64_t csLength)
{
  auto dstStream = codeStream->getStream();
  uint64_t csEnd = csStart + csLength;

  // Helpers to read big-endian values from source stream
  auto readU16 = [srcStream](uint16_t* val) -> bool {
    uint8_t b[2];
    if(srcStream->read(b, nullptr, 2) != 2)
      return false;
    *val = (uint16_t)((b[0] << 8) | b[1]);
    return true;
  };
  auto readU32 = [srcStream](uint32_t* val) -> bool {
    uint8_t b[4];
    if(srcStream->read(b, nullptr, 4) != 4)
      return false;
    *val =
        (uint32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]);
    return true;
  };

  // 0xFF30 to 0xFF3F markers have no marker segment in the standard
  auto markerHasNoSegment = [](uint16_t markerCode) {
    return markerCode >= 0xFF30 && markerCode <= 0xFF3F;
  };

  srcStream->seek(csStart);

  // Read and verify SOC
  uint16_t marker;
  if(!readU16(&marker) || marker != SOC)
  {
    grklog.error("transcode: expected SOC at start of codestream");
    return 0;
  }

  // --- Phase 1: Scan main header markers ---
  struct MainHeaderMarker
  {
    uint64_t offset;
    uint16_t type;
    uint16_t segLen; // Lxxx value (includes 2 bytes for length field itself)
  };
  std::vector<MainHeaderMarker> mainHeaderMarkers;

  while(srcStream->tell() < csEnd)
  {
    uint64_t pos = srcStream->tell();
    if(!readU16(&marker))
    {
      grklog.error("transcode: failed to read marker in main header");
      return 0;
    }

    if(marker == SOT || marker == EOC)
    {
      srcStream->seek(pos);
      break;
    }

    uint16_t segLen = 0;
    if(!markerHasNoSegment(marker) && !readU16(&segLen))
    {
      grklog.error("transcode: failed to read marker segment length");
      return 0;
    }

    MainHeaderMarker mhm;
    mhm.offset = pos;
    mhm.type = marker;
    mhm.segLen = segLen;
    mainHeaderMarkers.push_back(mhm);

    if(segLen > 2)
    {
      if(!srcStream->skip(segLen - 2))
      {
        grklog.error("transcode: failed to skip main header marker data");
        return 0;
      }
    }
  }

  // --- Phase 2: Scan tile parts ---
  struct TilePartHeaderMarker
  {
    uint64_t offset;
    uint16_t type;
    uint16_t segLen;
  };
  struct TilePartInfo
  {
    uint16_t tileIndex;
    uint32_t length; // byte length from SOT to end of tile-part data
    uint64_t srcOffset; // absolute position of SOT in source
    bool psotWasZero;
    std::vector<TilePartHeaderMarker> headerMarkers; // markers between SOT and SOD
    uint64_t sodOffset; // absolute position of SOD in source
    uint32_t dataSize; // bytes after SOD to end of tile part
  };
  std::vector<TilePartInfo> tileParts;

  while(srcStream->tell() < csEnd)
  {
    uint64_t sotPos = srcStream->tell();
    if(!readU16(&marker))
      break;

    if(marker == EOC)
      break;

    if(marker != SOT)
    {
      grklog.error("transcode: expected SOT at offset %lu, got 0x%04X", (unsigned long)sotPos,
                   marker);
      return 0;
    }

    uint16_t Lsot;
    if(!readU16(&Lsot))
      return 0;
    uint16_t Isot;
    if(!readU16(&Isot))
      return 0;
    uint32_t Psot;
    if(!readU32(&Psot))
      return 0;
    // Skip TPsot(1) + TNsot(1)
    srcStream->skip(2);

    TilePartInfo tp;
    tp.tileIndex = Isot;
    tp.srcOffset = sotPos;
    tp.psotWasZero = (Psot == 0);

    if(Psot == 0)
    {
      // Last tile part extends from SOT to EOC (exclusive)
      tp.length = (uint32_t)(csEnd - sotPos - 2);
    }
    else
    {
      tp.length = Psot;
    }

    // Scan tile-part header markers (between SOT and SOD)
    tp.sodOffset = 0;
    tp.dataSize = 0;
    uint64_t tpEnd = sotPos + tp.length;
    while(srcStream->tell() < tpEnd)
    {
      uint64_t mPos = srcStream->tell();
      if(!readU16(&marker))
        break;
      if(marker == SOD)
      {
        tp.sodOffset = mPos;
        tp.dataSize = (uint32_t)(tpEnd - (mPos + 2));
        break;
      }
      uint16_t sl = 0;
      if(!markerHasNoSegment(marker) && !readU16(&sl))
        break;
      TilePartHeaderMarker tphm;
      tphm.offset = mPos;
      tphm.type = marker;
      tphm.segLen = sl;
      tp.headerMarkers.push_back(tphm);
      if(sl > 2)
        srcStream->skip(sl - 2);
    }

    tileParts.push_back(tp);
    if(Psot == 0)
      break;
    srcStream->seek(sotPos + Psot);
  }

  if(tileParts.empty())
  {
    grklog.error("transcode: no tile parts found in codestream");
    return 0;
  }

  uint32_t numTileParts = (uint32_t)tileParts.size();

  // --- Phase 2.5: Extract packet info (if any packet-level feature is active) ---
  bool needPacketInfo = write_plt_transcode_ || write_sop_transcode_ || write_eph_transcode_ ||
                        max_layers_transcode_ > 0 || max_res_transcode_ > 0 ||
                        transcode_prog_order_ != GRK_PROG_UNKNOWN;

  const bool stripPoc = transcode_prog_order_ != GRK_PROG_UNKNOWN;
  auto readScod = [srcStream](uint64_t codOffset) {
    uint8_t scod = 0;
    if(!srcStream->seek(codOffset + 4) || srcStream->read(&scod, nullptr, 1) != 1)
      throw std::runtime_error("transcode: failed to read COD marker");
    return scod;
  };
  uint8_t mainScod = 0;
  uint16_t numComponents = 0;
  for(const auto& mhm : mainHeaderMarkers)
  {
    if(mhm.type == COD)
      mainScod = readScod(mhm.offset);
    // SIZ layout: Csiz follows 38 bytes of marker, length, Rsiz and eight 32 bit fields
    if(mhm.type == SIZ && (!srcStream->seek(mhm.offset + 38) || !readU16(&numComponents)))
      throw std::runtime_error("transcode: failed to read SIZ marker");
    if(mhm.type == SIZ && (transcode_prog_order_ == GRK_RPCL || transcode_prog_order_ == GRK_PCRL))
    {
      // SIZ layout: Ssiz(1) XRsiz(1) YRsiz(1) per component, from byte 40
      for(uint16_t component = 0; component < numComponents; ++component)
      {
        uint8_t subsampling[3];
        if(!srcStream->seek(mhm.offset + 40 + 3ULL * component) ||
           srcStream->read(subsampling, nullptr, 3) != 3)
          throw std::runtime_error("transcode: failed to read SIZ marker");
        if((subsampling[1] & (subsampling[1] - 1)) || (subsampling[2] & (subsampling[2] - 1)))
          throw std::runtime_error("transcode: RPCL and PCRL are illegal when component "
                                   "subsampling is not a power of two");
      }
    }
  }
  uint8_t resolutionsRemoved = 0;

  // Per-tile-part packet info (for PLT generation and packet-level copy)
  struct TilePartPacket
  {
    uint64_t srcOffset;
    uint32_t totalLength; // original total length of this packet
    uint32_t headerLength; // header bytes (for EPH injection point)
    uint16_t sopNumber; // the standard restarts this count at zero in each tile
    bool sourceHasSop;
    bool injectSop;
    bool injectEph;
    std::vector<uint8_t> packedHeader;
    uint32_t outputLength() const
    {
      return totalLength + (uint32_t)packedHeader.size() + (injectSop ? 6U : 0U) +
             (injectEph ? 2U : 0U);
    }
  };
  std::vector<std::vector<TilePartPacket>> tpPackets(numTileParts);
  std::vector<bool> tilePartRewritten(numTileParts, false);
  std::vector<uint32_t> pltMarkerSizes(numTileParts, 0);

  if(needPacketInfo)
  {
    // Decompress the source file (T2 only) to record packet info
    grk_stream_params decStreamParams = transcode_src_;
    grk_decompress_parameters dparams{};
    auto* decCodec = grk_decompress_init(&decStreamParams, &dparams);
    if(!decCodec)
    {
      grklog.error("transcode: failed to init decompressor for packet info extraction");
      return 0;
    }
    grk_header_info hdr{};
    if(!grk_decompress_read_header(decCodec, &hdr))
    {
      grklog.error("transcode: failed to read header for packet info extraction");
      grk_object_unref(decCodec);
      return 0;
    }
    // Enable packet info recording
    auto* codecImpl = Codec::getImpl(decCodec);
    auto* cp = codecImpl->decompressor_->getCodingParams();
    cp->recordPacketLengths_ = true;
    cp->recordMissingPackets_ = transcode_prog_order_ != GRK_PROG_UNKNOWN;
    cp->recordedPacketInfo_.resize((size_t)cp->t_grid_width_ * cp->t_grid_height_);

    // T2-only decompress to drive packet parsing (T1 skipped via recordPacketLengths_)
    if(!grk_decompress(decCodec, nullptr))
    {
      grklog.error("transcode: failed to decompress source for packet info extraction");
      grk_object_unref(decCodec);
      return 0;
    }

    // Map recorded packet info to tile parts
    auto& recordedInfo = cp->recordedPacketInfo_;
    uint8_t mostResolutions = 0;
    for(const auto& packets : recordedInfo)
      for(const auto& pkt : packets)
        mostResolutions = std::max(mostResolutions, pkt.numResolutions);
    if(max_res_transcode_ > 0 && mostResolutions > max_res_transcode_)
      resolutionsRemoved = (uint8_t)(mostResolutions - max_res_transcode_);
    // Part 2 DFS levels split one axis or both
    const bool hasDfs = std::any_of(mainHeaderMarkers.begin(), mainHeaderMarkers.end(),
                                    [](const MainHeaderMarker& m) { return m.type == DFS; });
    if(resolutionsRemoved > 0 && hasDfs)
    {
      grk_object_unref(decCodec);
      throw std::runtime_error("transcode: --max-res does not support Part 2 DFS markers");
    }
    const uint32_t reducedGridMask = (1U << resolutionsRemoved) - 1U;
    if((uint32_t)cp->t_grid_width_ * cp->t_grid_height_ > 1 &&
       ((cp->t_width_ | cp->t_height_) & reducedGridMask))
    {
      grk_object_unref(decCodec);
      throw std::runtime_error(
          "transcode: --max-res needs a tile size that is a multiple of the removed scale");
    }
    for(const auto& packets : recordedInfo)
      for(const auto& pkt : packets)
        if(pkt.numResolutions <= resolutionsRemoved)
        {
          grk_object_unref(decCodec);
          throw std::runtime_error(
              "transcode: --max-res removes more resolutions than a component has");
        }
    std::map<uint16_t, std::vector<uint32_t>> tpIndicesByTile;
    for(uint32_t tpIdx = 0; tpIdx < numTileParts; ++tpIdx)
      tpIndicesByTile[tileParts[tpIdx].tileIndex].push_back(tpIdx);

    for(auto& [tileIdx, tpIndices] : tpIndicesByTile)
    {
      if(tileIdx >= recordedInfo.size() || recordedInfo[tileIdx].empty())
        continue;

      auto& packets = recordedInfo[tileIdx];

      size_t tilePartPosition = 0;
      uint64_t bytesConsumed = 0;
      for(auto& pkt : packets)
      {
        while(tilePartPosition < tpIndices.size() &&
              bytesConsumed + pkt.totalLength > tileParts[tpIndices[tilePartPosition]].dataSize)
        {
          ++tilePartPosition;
          bytesConsumed = 0;
        }
        if(tilePartPosition == tpIndices.size())
          throw std::runtime_error("transcode: packets exceed the tile part data in source");
        pkt.srcTilePart = tpIndices[tilePartPosition];
        pkt.srcOffset = tileParts[pkt.srcTilePart].sodOffset + 2 + bytesConsumed;
        bytesConsumed += pkt.totalLength;
      }

      // Apply filtering (layer truncation and resolution stripping)
      std::vector<CodingParams::RecordedPacketInfo> filtered;
      for(auto& pkt : packets)
      {
        if(max_layers_transcode_ > 0 && pkt.layno >= max_layers_transcode_)
          continue;
        if(pkt.resno + resolutionsRemoved >= pkt.numResolutions)
          continue;
        filtered.push_back(pkt);
      }

      // Apply progression reorder (sort by new progression order)
      if(transcode_prog_order_ != GRK_PROG_UNKNOWN)
      {
        std::stable_sort(filtered.begin(), filtered.end(),
                         [this](const CodingParams::RecordedPacketInfo& a,
                                const CodingParams::RecordedPacketInfo& b) {
                           switch(transcode_prog_order_)
                           {
                             case GRK_LRCP:
                               if(a.layno != b.layno)
                                 return a.layno < b.layno;
                               if(a.resno != b.resno)
                                 return a.resno < b.resno;
                               if(a.compno != b.compno)
                                 return a.compno < b.compno;
                               return a.precinctIndex < b.precinctIndex;
                             case GRK_RLCP:
                               if(a.resno != b.resno)
                                 return a.resno < b.resno;
                               if(a.layno != b.layno)
                                 return a.layno < b.layno;
                               if(a.compno != b.compno)
                                 return a.compno < b.compno;
                               return a.precinctIndex < b.precinctIndex;
                             case GRK_RPCL:
                               if(a.resno != b.resno)
                                 return a.resno < b.resno;
                               if(a.positionY != b.positionY)
                                 return a.positionY < b.positionY;
                               if(a.positionX != b.positionX)
                                 return a.positionX < b.positionX;
                               if(a.compno != b.compno)
                                 return a.compno < b.compno;
                               return a.layno < b.layno;
                             case GRK_PCRL:
                               if(a.positionY != b.positionY)
                                 return a.positionY < b.positionY;
                               if(a.positionX != b.positionX)
                                 return a.positionX < b.positionX;
                               if(a.compno != b.compno)
                                 return a.compno < b.compno;
                               if(a.resno != b.resno)
                                 return a.resno < b.resno;
                               return a.layno < b.layno;
                             case GRK_CPRL:
                               if(a.compno != b.compno)
                                 return a.compno < b.compno;
                               if(a.positionY != b.positionY)
                                 return a.positionY < b.positionY;
                               if(a.positionX != b.positionX)
                                 return a.positionX < b.positionX;
                               if(a.resno != b.resno)
                                 return a.resno < b.resno;
                               return a.layno < b.layno;
                             default:
                               return false;
                           }
                         });
      }

      uint8_t tileScod = mainScod;
      for(const auto& hm : tileParts[tpIndices[0]].headerMarkers)
        if(hm.type == COD)
          tileScod = readScod(hm.offset);

      const bool reorganizing = max_layers_transcode_ > 0 || max_res_transcode_ > 0 ||
                                transcode_prog_order_ != GRK_PROG_UNKNOWN;
      uint32_t sopNumber = 0;
      for(const auto& pkt : filtered)
      {
        TilePartPacket tpp{};
        tpp.srcOffset = pkt.srcOffset;
        tpp.totalLength = pkt.totalLength;
        tpp.headerLength = pkt.headerLength;
        tpp.sopNumber = (uint16_t)(sopNumber++ & 0xFFFF);
        tpp.sourceHasSop = tileScod & CP_CSTY_SOP;
        tpp.injectSop = write_sop_transcode_ && !tpp.sourceHasSop;
        tpp.injectEph = pkt.missingFromStream ? (write_eph_transcode_ || (tileScod & CP_CSTY_EPH))
                                              : (write_eph_transcode_ && !(tileScod & CP_CSTY_EPH));
        tpp.packedHeader = pkt.packedHeader;
        tpPackets[reorganizing ? tpIndices[0] : pkt.srcTilePart].push_back(tpp);
      }
      for(auto tpIdx : tpIndices)
        tilePartRewritten[tpIdx] = true;
    }

    // Compute PLT marker sizes (using adjusted packet lengths)
    if(write_plt_transcode_)
    {
      for(uint32_t tpIdx = 0; tpIdx < numTileParts; ++tpIdx)
      {
        if(tpPackets[tpIdx].empty())
          continue;
        PLMarker pltSizer;
        pltSizer.pushInit(true);
        for(const auto& tpp : tpPackets[tpIdx])
          pltSizer.pushPL(tpp.outputLength());
        pltMarkerSizes[tpIdx] = pltSizer.getTotalBytesWritten();
      }
    }

    grk_object_unref(decCodec);
  }

  auto droppedFromTilePart = [stripPoc, needPacketInfo](uint16_t type) {
    return type == PLT || (stripPoc && type == POC) || (needPacketInfo && type == PPT);
  };
  // COD: marker(2) Lcod(2) Scod(1) SGcod(4) levels(1) ... one precinct size per resolution
  // COC: marker(2) Lcoc(2) Ccoc(1 or 2) Scoc(1) levels(1) ... one precinct size per resolution
  // QCD: marker(2) Lqcd(2) Sqcd(1) SPqcd, QCC: marker(2) Lqcc(2) Cqcc(1 or 2) Sqcc(1) SPqcc
  const uint32_t componentIndexBytes = numComponents < 257 ? 1 : 2;
  auto removeResolutionsFromMarker = [resolutionsRemoved, componentIndexBytes](
                                         uint8_t* buf, uint16_t type, uint32_t markerTotalLen) {
    if(resolutionsRemoved == 0 || (type != COD && type != COC && type != QCD && type != QCC))
      return markerTotalLen;
    if(type == COD || type == COC)
    {
      const uint32_t stylePosition = type == COD ? 4 : 4 + componentIndexBytes;
      const uint32_t levelsPosition = type == COD ? 9 : stylePosition + 1;
      if(buf[levelsPosition] < resolutionsRemoved)
        throw std::runtime_error(
            "transcode: --max-res removes more resolutions than a component has");
      buf[levelsPosition] = (uint8_t)(buf[levelsPosition] - resolutionsRemoved);
      if(buf[stylePosition] & CP_CSTY_PRT)
        markerTotalLen -= resolutionsRemoved;
    }
    else
    {
      const uint32_t stylePosition = type == QCD ? 4 : 4 + componentIndexBytes;
      const uint8_t quantizationStyle = buf[stylePosition] & 0x1F;
      // derived quantization signals one value that applies at every decomposition count
      if(quantizationStyle == 1)
        return markerTotalLen;
      const uint32_t bytesPerBand = quantizationStyle == 0 ? 1 : 2;
      const uint32_t bytesRemoved = 3U * resolutionsRemoved * bytesPerBand;
      if(markerTotalLen < stylePosition + 1 + bytesRemoved + bytesPerBand)
        throw std::runtime_error("transcode: quantization marker has too few bands");
      markerTotalLen -= bytesRemoved;
    }
    buf[2] = (uint8_t)((markerTotalLen - 2) >> 8);
    buf[3] = (uint8_t)(markerTotalLen - 2);
    return markerTotalLen;
  };

  // Compute adjusted tile-part lengths
  std::vector<uint32_t> adjustedLengths(numTileParts);
  for(uint32_t i = 0; i < numTileParts; ++i)
  {
    uint32_t newLength = sotMarkerSegmentLen + pltMarkerSizes[i] + 2;
    for(const auto& hm : tileParts[i].headerMarkers)
    {
      if(droppedFromTilePart(hm.type))
        continue;
      uint32_t markerLength = 2U + hm.segLen;
      if(resolutionsRemoved > 0)
      {
        std::vector<uint8_t> marker(markerLength);
        if(!srcStream->seek(hm.offset) ||
           srcStream->read(marker.data(), nullptr, markerLength) != markerLength)
          throw std::runtime_error("transcode: failed to read tile-part header marker");
        markerLength = removeResolutionsFromMarker(marker.data(), hm.type, markerLength);
      }
      newLength += markerLength;
    }
    if(tilePartRewritten[i])
      for(const auto& tpp : tpPackets[i])
        newLength += tpp.outputLength();
    else
      newLength += tileParts[i].dataSize;
    adjustedLengths[i] = newLength;
  }

  // --- Phase 3: Write new codestream ---
  uint64_t totalWritten = 0;

  // Determine whether TLM should appear in the output: either explicitly
  // requested, or the source already contained TLM (whose values must be
  // updated because tile-part lengths may have changed).
  bool hadTlm = false;
  for(const auto& mhm : mainHeaderMarkers)
  {
    if(mhm.type == TLM)
    {
      hadTlm = true;
      break;
    }
  }
  bool writeTlm = write_tlm_transcode_ || hadTlm;

  // 3a. Write SOC
  if(!dstStream->write(SOC))
    return 0;
  totalWritten += 2;

  // 3b. Copy main header markers, stripping any existing TLM and patching COD/SIZ
  const size_t copyBufSize = 1024 * 1024;
  auto copyBuf = std::make_unique<uint8_t[]>(copyBufSize);

  // COD layout: marker(2) + Lcod(2) + Scod(1) + SGcod(4: progOrder(1) + numLayers(2) + mct(1))
  //             + SPcod(variable)
  auto patchCod = [this](uint8_t* buf, uint32_t markerTotalLen) {
    if(markerTotalLen < 9)
      return;
    if(write_sop_transcode_)
      buf[4] |= CP_CSTY_SOP;
    if(write_eph_transcode_)
      buf[4] |= CP_CSTY_EPH;
    if(transcode_prog_order_ != GRK_PROG_UNKNOWN)
      buf[5] = (uint8_t)transcode_prog_order_;
    const uint16_t signalledLayers = (uint16_t)((buf[6] << 8) | buf[7]);
    if(max_layers_transcode_ > 0 && max_layers_transcode_ < signalledLayers)
    {
      buf[6] = (uint8_t)(max_layers_transcode_ >> 8);
      buf[7] = (uint8_t)(max_layers_transcode_);
    }
  };

  for(auto& mhm : mainHeaderMarkers)
  {
    if(mhm.type == TLM || (needPacketInfo && (mhm.type == PLM || mhm.type == PPM)) ||
       (stripPoc && mhm.type == POC))
      continue;

    uint32_t markerTotalLen = 2 + mhm.segLen;
    srcStream->seek(mhm.offset);

    uint8_t* buf;
    std::unique_ptr<uint8_t[]> largeBuf;
    if(markerTotalLen <= copyBufSize)
    {
      buf = copyBuf.get();
    }
    else
    {
      largeBuf = std::make_unique<uint8_t[]>(markerTotalLen);
      buf = largeBuf.get();
    }

    if(srcStream->read(buf, nullptr, markerTotalLen) != markerTotalLen)
    {
      grklog.error("transcode: failed to read main header marker");
      return 0;
    }

    if(mhm.type == COD)
      patchCod(buf, markerTotalLen);
    markerTotalLen = removeResolutionsFromMarker(buf, mhm.type, markerTotalLen);

    // Patch SIZ marker: image dimensions if resolution stripping
    // SIZ layout: marker(2) + Lsiz(2) + Rsiz(2) + Xsiz(4) + Ysiz(4)
    //             + XOsiz(4) + YOsiz(4) + XTsiz(4) + YTsiz(4) + XTOsiz(4) + YTOsiz(4) + ...
    if(mhm.type == SIZ && resolutionsRemoved > 0 && markerTotalLen >= 42)
    {
      {
        uint32_t divFactor = 1u << resolutionsRemoved;

        // Helper to read/write big-endian u32 from buffer
        auto readBE32 = [](const uint8_t* p) -> uint32_t {
          return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        };
        auto writeBE32 = [](uint8_t* p, uint32_t v) {
          p[0] = (uint8_t)(v >> 24);
          p[1] = (uint8_t)(v >> 16);
          p[2] = (uint8_t)(v >> 8);
          p[3] = (uint8_t)v;
        };

        uint32_t Xsiz = readBE32(buf + 6);
        uint32_t Ysiz = readBE32(buf + 10);
        uint32_t XOsiz = readBE32(buf + 14);
        uint32_t YOsiz = readBE32(buf + 18);
        uint32_t XTsiz = readBE32(buf + 22);
        uint32_t YTsiz = readBE32(buf + 26);
        uint32_t XTOsiz = readBE32(buf + 30);
        uint32_t YTOsiz = readBE32(buf + 34);

        // Compute reduced dimensions using ceiling division
        auto ceilDiv = [](uint32_t n, uint32_t d) -> uint32_t { return (n + d - 1) / d; };
        writeBE32(buf + 6, ceilDiv(Xsiz, divFactor));
        writeBE32(buf + 10, ceilDiv(Ysiz, divFactor));
        writeBE32(buf + 14, ceilDiv(XOsiz, divFactor));
        writeBE32(buf + 18, ceilDiv(YOsiz, divFactor));
        writeBE32(buf + 22, ceilDiv(XTsiz, divFactor));
        writeBE32(buf + 26, ceilDiv(YTsiz, divFactor));
        writeBE32(buf + 30, ceilDiv(XTOsiz, divFactor));
        writeBE32(buf + 34, ceilDiv(YTOsiz, divFactor));

        // Also patch component sub-sampling factors
        // Components start at offset 38: Ssiz(1) + XRsiz(1) + YRsiz(1) per component
      }
    }

    if(dstStream->writeBytes(buf, markerTotalLen) != markerTotalLen)
    {
      grklog.error("transcode: failed to write main header marker");
      return 0;
    }
    totalWritten += markerTotalLen;
  }

  // 3c. Write new TLM marker with adjusted tile-part lengths (only if
  //      the source already had TLM or if TLM was explicitly requested)
  if(writeTlm)
  {
    for(uint32_t i = 0; i < numTileParts; ++i)
    {
      if(i % maxTilePartsPerTlmSegment == 0)
      {
        uint32_t numInSegment = std::min(numTileParts - i, maxTilePartsPerTlmSegment);
        if(!TLMMarker::writeSegmentHeader(dstStream, i / maxTilePartsPerTlmSegment, numInSegment))
          return 0;
        totalWritten += tlm_marker_start_bytes;
      }
      if(!dstStream->write(tileParts[i].tileIndex))
        return 0;
      totalWritten += 2;
      if(!dstStream->write(adjustedLengths[i]))
        return 0;
      totalWritten += 4;
    }
  }

  // 3d. Write tile parts with PLT insertion and packet-level processing
  for(uint32_t tpIdx = 0; tpIdx < numTileParts; ++tpIdx)
  {
    auto& tp = tileParts[tpIdx];

    // Write SOT with adjusted Psot
    uint8_t sotHeader[sotMarkerSegmentLen];
    srcStream->seek(tp.srcOffset);
    if(srcStream->read(sotHeader, nullptr, sotMarkerSegmentLen) != sotMarkerSegmentLen)
    {
      grklog.error("transcode: failed to read SOT header");
      return 0;
    }
    // Overwrite Psot with adjusted length
    uint32_t newPsot = adjustedLengths[tpIdx];
    sotHeader[6] = (uint8_t)(newPsot >> 24);
    sotHeader[7] = (uint8_t)(newPsot >> 16);
    sotHeader[8] = (uint8_t)(newPsot >> 8);
    sotHeader[9] = (uint8_t)(newPsot);
    if(dstStream->writeBytes(sotHeader, sotMarkerSegmentLen) != sotMarkerSegmentLen)
    {
      grklog.error("transcode: failed to write SOT header");
      return 0;
    }
    totalWritten += sotMarkerSegmentLen;

    // Copy kept tile-part header markers from source
    for(auto& hm : tp.headerMarkers)
    {
      if(droppedFromTilePart(hm.type))
        continue;
      uint32_t hmLen = 2 + hm.segLen;
      srcStream->seek(hm.offset);
      uint8_t* buf;
      std::unique_ptr<uint8_t[]> largeBuf2;
      if(hmLen <= copyBufSize)
        buf = copyBuf.get();
      else
      {
        largeBuf2 = std::make_unique<uint8_t[]>(hmLen);
        buf = largeBuf2.get();
      }
      if(srcStream->read(buf, nullptr, hmLen) != hmLen)
      {
        grklog.error("transcode: failed to read tile-part header marker");
        return 0;
      }
      if(hm.type == COD)
        patchCod(buf, hmLen);
      hmLen = removeResolutionsFromMarker(buf, hm.type, hmLen);
      if(dstStream->writeBytes(buf, hmLen) != hmLen)
      {
        grklog.error("transcode: failed to write tile-part header marker");
        return 0;
      }
      totalWritten += hmLen;
    }

    // Write PLT markers for this tile part
    if(pltMarkerSizes[tpIdx] > 0 && !tpPackets[tpIdx].empty())
    {
      PLMarker pltWriter(dstStream);
      pltWriter.pushInit(true);
      for(const auto& tpp : tpPackets[tpIdx])
        pltWriter.pushPL(tpp.outputLength());
      if(!pltWriter.write())
      {
        grklog.error("transcode: failed to write PLT markers");
        return 0;
      }
      totalWritten += pltWriter.getTotalBytesWritten();
    }

    // Write SOD
    if(!dstStream->write(SOD))
      return 0;
    totalWritten += 2;

    // Copy packet data — either packet-by-packet or as a blob
    if(tilePartRewritten[tpIdx])
    {
      // Packet-level copy: read each packet from source, inject SOP/EPH
      for(const auto& tpp : tpPackets[tpIdx])
      {
        if(srcStream->tell() != tpp.srcOffset && !srcStream->seek(tpp.srcOffset))
        {
          grklog.error("transcode: failed to seek to packet in source");
          return 0;
        }
        // Write SOP marker before packet
        if(tpp.injectSop)
        {
          uint8_t sop[6];
          sop[0] = 0xFF;
          sop[1] = 0x91;
          sop[2] = 0x00;
          sop[3] = 0x04;
          sop[4] = (uint8_t)(tpp.sopNumber >> 8);
          sop[5] = (uint8_t)(tpp.sopNumber);
          if(dstStream->writeBytes(sop, 6) != 6)
          {
            grklog.error("transcode: failed to write SOP marker");
            return 0;
          }
          totalWritten += 6;
        }

        // Copy packet header bytes
        uint32_t hdrLen = tpp.headerLength;
        uint32_t dataLen = tpp.totalLength - hdrLen;

        if(hdrLen > 0)
        {
          uint32_t remaining = hdrLen;
          while(remaining > 0)
          {
            size_t toRead = std::min((size_t)remaining, copyBufSize);
            size_t bytesRead = srcStream->read(copyBuf.get(), nullptr, toRead);
            if(bytesRead == 0)
            {
              grklog.error("transcode: unexpected end of source stream reading packet header");
              return 0;
            }
            // SOP numbers in the source follow the source packet order
            if(tpp.sourceHasSop && remaining == hdrLen && bytesRead >= 6 && copyBuf[0] == 0xFF &&
               copyBuf[1] == 0x91)
            {
              copyBuf[4] = (uint8_t)(tpp.sopNumber >> 8);
              copyBuf[5] = (uint8_t)(tpp.sopNumber);
            }
            if(dstStream->writeBytes(copyBuf.get(), bytesRead) != bytesRead)
            {
              grklog.error("transcode: failed to write packet header");
              return 0;
            }
            remaining -= (uint32_t)bytesRead;
            totalWritten += bytesRead;
          }
        }

        if(!tpp.packedHeader.empty())
        {
          const auto packedLength = tpp.packedHeader.size();
          if(dstStream->writeBytes(tpp.packedHeader.data(), packedLength) != packedLength)
          {
            grklog.error("transcode: failed to write packet header");
            return 0;
          }
          totalWritten += packedLength;
        }

        // Write EPH marker after packet header
        if(tpp.injectEph)
        {
          uint8_t eph[2] = {0xFF, 0x92};
          if(dstStream->writeBytes(eph, 2) != 2)
          {
            grklog.error("transcode: failed to write EPH marker");
            return 0;
          }
          totalWritten += 2;
        }

        // Copy packet data bytes
        if(dataLen > 0)
        {
          uint32_t remaining = dataLen;
          while(remaining > 0)
          {
            size_t toRead = std::min((size_t)remaining, copyBufSize);
            size_t bytesRead = srcStream->read(copyBuf.get(), nullptr, toRead);
            if(bytesRead == 0)
            {
              grklog.error("transcode: unexpected end of source stream reading packet data");
              return 0;
            }
            if(dstStream->writeBytes(copyBuf.get(), bytesRead) != bytesRead)
            {
              grklog.error("transcode: failed to write packet data");
              return 0;
            }
            remaining -= (uint32_t)bytesRead;
            totalWritten += bytesRead;
          }
        }
      }
    }
    else
    {
      // Blob copy: copy all packet data as-is from source
      srcStream->seek(tp.sodOffset + 2); // skip SOD in source
      uint32_t dataRemaining = tp.dataSize;
      while(dataRemaining > 0)
      {
        size_t toRead = std::min((size_t)dataRemaining, copyBufSize);
        size_t bytesRead = srcStream->read(copyBuf.get(), nullptr, toRead);
        if(bytesRead == 0)
        {
          grklog.error("transcode: unexpected end of source stream");
          return 0;
        }
        if(dstStream->writeBytes(copyBuf.get(), bytesRead) != bytesRead)
        {
          grklog.error("transcode: failed to write tile part data");
          return 0;
        }
        dataRemaining -= (uint32_t)bytesRead;
        totalWritten += bytesRead;
      }
    }
  }

  // 3e. Write EOC
  if(!dstStream->write(EOC))
    return 0;
  totalWritten += 2;

  return totalWritten;
}
bool FileFormatJP2Compress::end(void)
{
  /* write header */
  init_end_header_writing();

  return exec(procedure_list_);
}
void FileFormatJP2Compress::init_end_header_writing(void)
{
  procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_jp2c, this));
  if(geoboxes_after_jp2c_)
  {
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_ipr, this));
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_asoc_boxes, this));
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_xml_boxes, this));
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_uuids, this));
  }
}

void FileFormatJP2Compress::init_header_writing(void)
{
  procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_signature, this));
  procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_ftyp, this));
  if(write_rreq_)
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_rreq, this));
  procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_jp2h, this));
  if(!geoboxes_after_jp2c_)
  {
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_xml_boxes, this));
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_ipr, this));
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_uuids, this));
    procedure_list_->push_back(std::bind(&FileFormatJP2Compress::write_asoc_boxes, this));
  }
  procedure_list_->push_back(std::bind(&FileFormatJP2Compress::skip_jp2c, this));
}
bool FileFormatJP2Compress::skip_jp2c(void)
{
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  codestream_offset = stream->tell();
  int64_t skip_bytes = needs_xl_jp2c_box_length ? 16 : 8;

  return stream->skip(skip_bytes);
}
bool FileFormatJP2Compress::default_validation(void)
{
  bool valid = true;
  uint32_t i;
  auto stream = codeStream->getStream();
  assert(stream != nullptr);
  valid &= (codeStream != nullptr);
  valid &= (procedure_list_ != nullptr);
  for(i = 0; i < numcomps; ++i)
    valid &= ((comps[i].bpc & 0x7FU) < maxPrecisionJ2K); /* 0 is valid, ignore sign for check */
  valid &= ((meth > 0) && (meth < 3));

  valid &= stream->hasSeek();

  return valid;
}

uint32_t FileFormatJP2Compress::calcAsocSize(AsocBox* asoc)
{
  // box header (8 bytes)
  uint32_t size = 8;

  // lbl box: 8 (header) + label length
  if(!asoc->label.empty())
    size += 8 + (uint32_t)asoc->label.size();

  // xml box: 8 (header) + data length
  if(asoc->buf() && asoc->num_elts() > 0)
    size += 8 + (uint32_t)asoc->num_elts();

  // nested asoc children
  for(const auto& child : asoc->children)
    size += calcAsocSize(child);

  return size;
}

bool FileFormatJP2Compress::writeAsocBox(IStream* stream, AsocBox* asoc)
{
  uint32_t boxSize = calcAsocSize(asoc);

  // write asoc box header
  if(!stream->write(boxSize))
    return false;
  if(!stream->write(JP2_ASOC))
    return false;

  // write lbl sub-box
  if(!asoc->label.empty())
  {
    uint32_t lblSize = 8 + (uint32_t)asoc->label.size();
    if(!stream->write(lblSize))
      return false;
    if(!stream->write(JP2_LBL))
      return false;
    if(stream->writeBytes((const uint8_t*)asoc->label.c_str(), (uint32_t)asoc->label.size()) !=
       (uint32_t)asoc->label.size())
      return false;
  }

  // write xml sub-box
  if(asoc->buf() && asoc->num_elts() > 0)
  {
    uint32_t xmlSize = 8 + (uint32_t)asoc->num_elts();
    if(!stream->write(xmlSize))
      return false;
    if(!stream->write(JP2_XML))
      return false;
    if(stream->writeBytes(asoc->buf(), (uint32_t)asoc->num_elts()) != (uint32_t)asoc->num_elts())
      return false;
  }

  // write nested asoc children
  for(const auto& child : asoc->children)
  {
    if(!writeAsocBox(stream, child))
      return false;
  }

  return true;
}

void FileFormatJP2Compress::buildAsocTree(const grk_asoc* flat, uint32_t count)
{
  if(!flat || count == 0)
    return;

  // Stack to track the current parent at each level.
  // Level 0 entries are children of root_asoc.
  std::vector<AsocBox*> stack;
  stack.push_back(&root_asoc);

  for(uint32_t i = 0; i < count; ++i)
  {
    auto& entry = flat[i];
    uint32_t level = entry.level;

    auto asoc = new AsocBox();
    if(entry.label)
      asoc->label = entry.label;
    if(entry.xml && entry.xml_len > 0)
    {
      asoc->alloc(entry.xml_len);
      memcpy(asoc->buf(), entry.xml, entry.xml_len);
    }

    // Ensure stack is deep enough for this level.
    // The parent is at stack[level].
    while(stack.size() > level + 1)
      stack.pop_back();

    auto parent = stack.back();
    parent->children.push_back(asoc);
    stack.push_back(asoc);
  }
}

bool FileFormatJP2Compress::write_asoc_boxes(void)
{
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  for(auto& child : root_asoc.children)
  {
    if(!writeAsocBox(stream, child))
      return false;
  }
  return true;
}

bool FileFormatJP2Compress::write_rreq(void)
{
  auto stream = codeStream->getStream();
  assert(stream != nullptr);

  if(!write_rreq_ || num_rreq_standard_features_ == 0)
    return true;

  // rreq box: 8 (header) + 1 (ML) + 1 (FUAM) + 1 (DCM) + 2 (NSF)
  //           + NSF * (2 (SF) + 1 (SM)) + 2 (NVF)
  uint32_t boxSize = 8 + 1 + 1 + 1 + 2 + static_cast<uint32_t>(num_rreq_standard_features_) * 3 + 2;

  if(!stream->write(boxSize))
    return false;
  if(!stream->write(JP2_RREQ))
    return false;

  // ML = 1 (1 byte mask length)
  if(!stream->write((uint8_t)1))
    return false;

  // FUAM - fully understand all mask
  uint8_t fuam = 0;
  for(uint8_t i = 0; i < num_rreq_standard_features_ && i < 8; ++i)
    fuam |= (uint8_t)(0x80 >> i);
  if(!stream->write(fuam))
    return false;

  // DCM - decode completely mask
  if(!stream->write((uint8_t)0x80))
    return false;

  // NSF
  if(!stream->write((uint16_t)num_rreq_standard_features_))
    return false;

  // Standard features
  for(uint8_t i = 0; i < num_rreq_standard_features_; ++i)
  {
    if(!stream->write(rreq_standard_features_[i]))
      return false;
    if(!stream->write((uint8_t)(0x80 >> i)))
      return false;
  }

  // NVF = 0 (no vendor features)
  if(!stream->write((uint16_t)0))
    return false;

  return true;
}

} // namespace grk
