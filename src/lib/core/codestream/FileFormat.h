/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#pragma once
#include <string>

namespace grk
{
/**
 @file FileFormat.h
 @brief The JPEG 2000 file format Reader/Writer (JP2)

 */

/** @defgroup JP2 JP2 - JPEG 2000 file format reader/writer */
/*@{*/

#define JP2_JP 0x6a502020 /**< JPEG 2000 signature box */
#define JP2_FTYP 0x66747970 /**< File type box */
#define JP2_JP2H 0x6a703268 /**< JP2 header box (super-box) */
#define JP2_IHDR 0x69686472 /**< Image header box */
#define JP2_COLR 0x636f6c72 /**< Colour specification box */
#define JP2_JP2C 0x6a703263 /**< Contiguous code stream box */
#define JP2_PCLR 0x70636c72 /**< Palette box */
#define JP2_CMAP 0x636d6170 /**< Component Mapping box */
#define JP2_CDEF 0x63646566 /**< Channel Definition box */
#define JP2_DTBL 0x6474626c /**< Data Reference box */
#define JP2_BPCC 0x62706363 /**< Bits per component box */
#define JP2_JP2 0x6a703220 /**< File type fields */
#define JP2_JPH 0x6A706820
#define JP2_RES 0x72657320 /**< Resolution box (super-box) */
#define JP2_CAPTURE_RES 0x72657363 /**< Capture resolution box */
#define JP2_DISPLAY_RES 0x72657364 /**< Display resolution box */
#define JP2_JP2I 0x6a703269 /**< Intellectual property box */
#define JP2_XML 0x786d6c20 /**< XML box */
#define JP2_UUID 0x75756964 /**< UUID box */
#define JP2_UINF 0x75696e66 /**< UUID info box (super-box) */
#define JP2_ULST 0x756c7374 /**< UUID list box */
#define JP2_URL 0x75726c20 /**< Data entry URL box */
#define JP2_ASOC 0x61736f63 /**< Associated data box*/
#define JP2_LBL 0x6c626c20 /**< Label box*/

#define JP2_MAX_NUM_UUIDS 128
const uint8_t IPTC_UUID[16] = {0x33, 0xC7, 0xA4, 0xD2, 0xB8, 0x1D, 0x47, 0x23,
							   0xA0, 0xBA, 0xF1, 0xA3, 0xE0, 0x97, 0xAD, 0x38};
const uint8_t XMP_UUID[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
							  0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};

#define GRK_BOX_SIZE 1024
#define GRK_RESOLUTION_BOX_SIZE (4 + 4 + 10)

enum JP2_STATE
{
   JP2_STATE_NONE = 0x0,
   JP2_STATE_SIGNATURE = 0x1,
   JP2_STATE_FILE_TYPE = 0x2,
   JP2_STATE_HEADER = 0x4,
   JP2_STATE_CODESTREAM = 0x8,
   JP2_STATE_END_CODESTREAM = 0x10,
   JP2_STATE_UNKNOWN = 0x7fffffff /* ISO C restricts enumerator values to range of 'int' */
};

struct FileFormatBox
{
   FileFormatBox() : length(0), type(0) {}
   uint64_t length;
   uint32_t type;
};

struct ComponentInfo
{
   ComponentInfo() : bpc(0) {}
   uint8_t bpc;
};

/**
  Association box (defined in ITU 15444-2 Annex M 11.1 )
*/
struct AsocBox : FileFormatBox, grk_buf8
{
   ~AsocBox() override
   {
	  dealloc();
   }
   void dealloc() override
   {
	  grk_buf8::dealloc();
	  for(auto& as : children)
	  {
		 delete as;
	  }
	  children.clear();
   }
   std::string label;
   std::vector<AsocBox*> children;
};

struct UUIDBox : public FileFormatBox, grk_buf8
{
   UUIDBox()
   {
	  memset(uuid, 0, sizeof(uuid));
   }
   UUIDBox(const uint8_t myuuid[16], uint8_t* buf, size_t size)
	   : FileFormatBox(), grk_buf8(buf, size, false)
   {
	  memcpy(uuid, myuuid, 16);
   }
   uint8_t uuid[16];
};

/**
 JPEG 2000 file format reader/writer
 */
class FileFormat
{
 public:
   FileFormat(void);
   virtual ~FileFormat();

 protected:
   bool exec(std::vector<PROCEDURE_FUNC>* procs);
   /** list of validation procedures */
   std::vector<PROCEDURE_FUNC>* validation_list_;
   /** list of execution procedures */
   std::vector<PROCEDURE_FUNC>* procedure_list_;

   /* width of image */
   uint32_t w;
   /* height of image */
   uint32_t h;
   /* number of components in the image */
   uint16_t numcomps;
   uint8_t bpc;
   uint8_t C;
   uint8_t UnkC;
   uint8_t IPR;
   uint8_t meth;
   uint8_t approx;
   GRK_ENUM_COLOUR_SPACE enumcs;
   uint8_t precedence;
   uint32_t brand;
   uint32_t minversion;
   uint32_t numcl;
   uint32_t* cl;
   ComponentInfo* comps;

   bool has_capture_resolution;
   double capture_resolution[2];
   bool has_display_resolution;
   double display_resolution[2];

   grk_buf8 xml;

   UUIDBox uuids[JP2_MAX_NUM_UUIDS];
   uint32_t numUuids;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/*@}*/

/*@}*/

} // namespace grk
