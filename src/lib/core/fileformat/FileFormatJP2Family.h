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
#include <string>
#include <stack>

namespace grk
{
const uint32_t JP2_JP = 0x6a502020; /** JPEG 2000 signature box */
const uint32_t JP2_SIG = 0x0d0a870a; /** JPEG 2000 signature */
const uint32_t JP2_FTYP = 0x66747970; /** File type box */
const uint32_t JP2_JP2 = 0x6a703220; /** File type fields */
const uint32_t JP2_JPH = 0x6A706820;

// JP2 Header
const uint32_t JP2_JP2H = 0x6a703268; /** JP2 header box (super-box) */
const uint32_t JP2_IHDR = 0x69686472; /** Image header box */
const uint32_t JP2_BPCC = 0x62706363; /** Bits per component box */
const uint32_t JP2_COLR = 0x636f6c72; /** Colour specification box */
const uint32_t JP2_PCLR = 0x70636c72; /** Palette box */
const uint32_t JP2_CMAP = 0x636d6170; /** Component Mapping box */
const uint32_t JP2_CDEF = 0x63646566; /** Channel Definition box */
const uint32_t JP2_RES = 0x72657320; /** Resolution box (super-box) */
const uint32_t JP2_CAPTURE_RES = 0x72657363; /** Capture resolution box */
const uint32_t JP2_DISPLAY_RES = 0x72657364; /** Display resolution box */

#define GRK_BOX_SIZE 1024
#define GRK_RESOLUTION_BOX_SIZE (4 + 4 + 10)
#define JP2_MAX_NUM_UUIDS 128

enum JP2_STATE
{
  JP2_STATE_NONE = 0x0,
  JP2_STATE_SIGNATURE = 0x1,
  JP2_STATE_FILE_TYPE = 0x2,
  JP2_STATE_HEADER = 0x4,
  JP2_STATE_CODESTREAM = 0x8,
  JP2_STATE_END_CODESTREAM = 0x10
};

typedef std::function<bool(uint8_t* headerData, uint32_t headerSize)> BOX_FUNC;

struct FindHandlerInfo
{
  FindHandlerInfo(BOX_FUNC handler, bool superBox) : handler_(handler), superBox_(superBox) {}
  bool valid(void)
  {
    return handler_ || superBox_;
  }
  BOX_FUNC handler_;
  bool superBox_;
};

typedef std::function<uint8_t*(uint32_t* len)> WRITE_FUNC;
struct BoxWriteHandler
{
  BoxWriteHandler() : handler(nullptr), data_(nullptr), size_(0) {}
  WRITE_FUNC handler;
  uint8_t* data_;
  uint32_t size_;
};

struct Box
{
  Box() : length(0), type(0), byteCount(0) {}
  uint64_t length;
  uint32_t type;
  uint64_t byteCount;
};

struct UUIDBox : public Box, Buffer8
{
  UUIDBox()
  {
    memset(uuid, 0, sizeof(uuid));
  }
  UUIDBox(const uint8_t myuuid[16], uint8_t* buf, size_t size) : Box(), Buffer8(buf, size, false)
  {
    memcpy(uuid, myuuid, 16);
  }
  uint8_t uuid[16];
};

struct ComponentInfo
{
  ComponentInfo() : bpc(0) {}
  uint8_t bpc;
};

/**
  Association box (defined in ITU 15444-2 Annex M 11.1 )
*/
struct AsocBox : Box, Buffer8
{
  ~AsocBox() override
  {
    dealloc();
  }
  void dealloc() override
  {
    Buffer8::dealloc();
    for(auto& as : children)
    {
      delete as;
    }
    children.clear();
  }
  std::string label;
  std::vector<AsocBox*> children;
};

/**
 JPEG 2000 file format reader/writer
 */
class FileFormatJP2Family
{
public:
  FileFormatJP2Family(IStream* stream);
  virtual ~FileFormatJP2Family();

protected:
  void init(grk_decompress_parameters* param);
  bool readHeaderProcedure();
  bool readHeader(grk_header_info* header_info, GrkImage* headerImage);
  virtual GrkImage* getHeaderImage(void) = 0;
  grk_color* getColour(void);
  std::string getBoxName(Box box);
  /**
   * @brief Finds the BOX_FUNC related to given box id.
   *
   * @param	id	the id of the handler to fetch.
   *
   * @return see @ref FindHandlerInfo
   */
  const FindHandlerInfo img_find_handler(uint32_t id);

  uint32_t read_asoc(AsocBox* parent, uint8_t** header_data, uint32_t* header_data_size,
                     uint32_t asocSize);
  bool read_asoc(uint8_t* header_data, uint32_t header_data_size);
  void serializeAsoc(AsocBox* asoc, grk_asoc* serial_asocs, uint32_t* num_asocs, uint32_t level);
  /***
   * Read box length and type only
   *
   *
   * returns: true if box header was read successfully, otherwise false
   * throw:   CorruptJP2BoxException if box is corrupt
   * Note: box length is never 0
   *
   */
  bool read_box_header(Box* box, IStream* stream, uint32_t* bytesRead, bool codeStreamBoxWasRead);
  bool read_box_header(Box* box, uint8_t* p_data, uint32_t* bytesRead, uint64_t availableBytes);

  uint8_t* write_buffer(uint32_t boxId, Buffer8* buffer, uint32_t* p_nb_bytes_written);
  bool write_ftyp(IStream* stream, uint32_t file_type);
  bool write_signature(IStream* stream, uint32_t sig);

  bool exec(std::vector<PROCEDURE_FUNC>* procs);
  /** list of execution procedures */
  std::vector<PROCEDURE_FUNC>* procedure_list_;

  uint32_t brand;
  uint32_t minversion;
  uint32_t numcl;
  uint32_t* cl;

  // IHDR ///////////////////////////
  uint32_t w;
  uint32_t h;
  uint16_t numcomps;
  uint8_t bpc;
  uint8_t C;
  uint8_t UnkC;
  uint8_t IPR;
  //////////////////////////////////

  uint8_t meth;
  uint8_t approx;
  GRK_ENUM_COLOUR_SPACE enumcs;
  uint8_t precedence;
  ComponentInfo* comps;

  bool has_capture_resolution;
  double capture_resolution[2];
  bool has_display_resolution;
  double display_resolution[2];

  Buffer8 xml;
  bool io_xml_;
  std::string xml_outfile_;

  UUIDBox uuids[JP2_MAX_NUM_UUIDS];
  uint32_t numUuids;

  std::unordered_map<uint32_t, BOX_FUNC> header;
  AsocBox root_asoc;
  uint32_t jp2_state;

  bool headerError_;
  bool headerRead_;

  std::stack<Box> superBoxes_;

  std::unordered_map<uint32_t, BOX_FUNC> img_header;

  IStream* stream_;

  /**
   * @brief Reads the Jpeg2000 file Header box - JP2 Header box
   *
   * (warning, this is a super box).
   *
   * @param	headerData	the data contained in the file header box.
   * @param	headerSize	the size of the data contained in the file header box.
   *
   * @return true if the JP2 Header box was successfully recognized.
   */
  bool read_jp2h(uint8_t* headerData, uint32_t headerSize);
  void skip(uint8_t** headerData, uint32_t* headerSize, uint32_t skip);

private:
  double calc_res(uint16_t num, uint16_t den, uint8_t exponent);
  bool read_ihdr(uint8_t* p_image_header_data, uint32_t image_header_size);
  bool read_res_box(uint32_t* id, uint32_t* num, uint32_t* den, uint32_t* exponent,
                    uint8_t** p_resolution_data);
  bool read_res(uint8_t* p_resolution_data, uint32_t resolution_size);
  bool read_bpc(uint8_t* p_bpc_header_data, uint32_t bpc_header_size);
  bool read_channel_definition(uint8_t* p_cdef_header_data, uint32_t cdef_header_size);
  bool read_colr(uint8_t* p_colr_header_data, uint32_t colr_header_size);
  bool read_component_mapping(uint8_t* component_mapping_header_data,
                              uint32_t component_mapping_header_size);
  bool read_palette_clr(uint8_t* p_pclr_header_data, uint32_t pclr_header_size);

  /**
   * @brief Reads a JPEG 2000 file signature box.
   *
   * @param	headerData	the data contained in the signature box.
   * @param	headerSize	the size of the data contained in the signature box.
   *
   * @return true if the file signature box is valid.
   */
  bool read_signature(uint8_t* headerData, uint32_t headerSize);
  /**
   * @brief Reads a FTYP box - File type box
   *
   * @param	headerData	the data contained in the FTYP box.
   * @param	headerSize	the size of the data contained in the FTYP box.
   *
   * @return true if the FTYP box is valid.
   */
  bool read_ftyp(uint8_t* headerData, uint32_t headerSize);

  const FindHandlerInfo find_handler(uint32_t id);

  void updateSuperBoxes(uint64_t boxBytes);
};

} // namespace grk
