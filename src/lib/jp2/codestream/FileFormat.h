/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
namespace grk {

/**
 @file FileFormat.h
 @brief The JPEG 2000 file format Reader/Writer (JP2)

 */

/** @defgroup JP2 JP2 - JPEG 2000 file format reader/writer */
/*@{*/

#define     JP2_JP   0x6a502020    /**< JPEG 2000 signature box */
#define     JP2_FTYP 0x66747970    /**< File type box */
#define     JP2_JP2H 0x6a703268    /**< JP2 header box (super-box) */
#define     JP2_IHDR 0x69686472    /**< Image header box */
#define     JP2_COLR 0x636f6c72    /**< Colour specification box */
#define     JP2_JP2C 0x6a703263    /**< Contiguous code stream box */
#define     JP2_PCLR 0x70636c72    /**< Palette box */
#define     JP2_CMAP 0x636d6170    /**< Component Mapping box */
#define     JP2_CDEF 0x63646566    /**< Channel Definition box */
#define     JP2_DTBL 0x6474626c    /**< Data Reference box */
#define     JP2_BPCC 0x62706363    /**< Bits per component box */
#define     JP2_JP2  0x6a703220    /**< File type fields */

#define JP2_RES			0x72657320   /**< Resolution box (super-box) */
#define JP2_CAPTURE_RES 0x72657363   /**< Capture resolution box */
#define JP2_DISPLAY_RES 0x72657364   /**< Display resolution box */

#define JP2_JP2I 0x6a703269   /**< Intellectual property box */
#define JP2_XML  0x786d6c20   /**< XML box */
#define JP2_UUID 0x75756964   /**< UUID box */
#define JP2_UINF 0x75696e66   /**< UUID info box (super-box) */
#define JP2_ULST 0x756c7374   /**< UUID list box */
#define JP2_URL  0x75726c20   /**< Data entry URL box */

/* ----------------------------------------------------------------------- */

#define JP2_MAX_NUM_UUIDS	128

const uint8_t IPTC_UUID[16] = { 0x33, 0xC7, 0xA4, 0xD2, 0xB8, 0x1D, 0x47, 0x23,
		0xA0, 0xBA, 0xF1, 0xA3, 0xE0, 0x97, 0xAD, 0x38 };
const uint8_t XMP_UUID[16] = { 0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
		0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC };

enum JP2_STATE {
	JP2_STATE_NONE = 0x0,
	JP2_STATE_SIGNATURE = 0x1,
	JP2_STATE_FILE_TYPE = 0x2,
	JP2_STATE_HEADER = 0x4,
	JP2_STATE_CODESTREAM = 0x8,
	JP2_STATE_END_CODESTREAM = 0x10,
	JP2_STATE_UNKNOWN = 0x7fffffff /* ISO C restricts enumerator values to range of 'int' */
};

enum JP2_IMG_STATE {
	JP2_IMG_STATE_NONE = 0x0, JP2_IMG_STATE_UNKNOWN = 0x7fffffff
};

/**
 JP2 component
 */
struct grk_jp2_comps {
	uint32_t depth;
	uint32_t sgnd;
	uint8_t bpcc;
};

struct grk_jp2_buffer {
	grk_jp2_buffer(uint8_t *buf, size_t size, bool owns) :
			buffer(buf), len(size), ownsData(owns) {
	}
	grk_jp2_buffer() :
			grk_jp2_buffer(nullptr, 0, false) {
	}
	void alloc(size_t length) {
		dealloc();
		buffer = new uint8_t[length];
		len = length;
		ownsData = true;
	}
	void dealloc() {
		if (ownsData)
			delete[] buffer;
		buffer = nullptr;
		ownsData = false;
		len = 0;
	}
	uint8_t *buffer;
	size_t len;
	bool ownsData;
};

struct grk_jp2_uuid: public grk_jp2_buffer {
	grk_jp2_uuid() : grk_jp2_buffer() {}
	grk_jp2_uuid(const uint8_t myuuid[16], uint8_t *buf, size_t size, bool owns) :
			grk_jp2_buffer(buf, size, owns) {
		for (int i = 0; i < 16; ++i)
			uuid[i] = myuuid[i];
	}
	uint8_t uuid[16];
};

struct FileFormat;
typedef bool (*jp2_procedure)(FileFormat *fileFormat);

/**
 JPEG 2000 file format reader/writer
 */
struct FileFormat : public ICodeStream {
	FileFormat(bool isDecoder, BufferedStream *stream);
	~FileFormat();


	/** Main header reading function handler */
   bool read_header(grk_header_info  *header_info, grk_image **p_image);

	/** Setup decoder function handler */
  void init_decompress(grk_dparameters  *p_param);

	/**
	 * Sets the given area to be decoded. This function should be called right after grk_read_header
	 * and before any tile header reading.
	 *
	 * @param	p_image     image
	 * @param	start_x		the left position of the rectangle to decompress (in image coordinates).
	 * @param	start_y		the up position of the rectangle to decompress (in image coordinates).
	 * @param	end_x		the right position of the rectangle to decompress (in image coordinates).
	 * @param	end_y		the bottom position of the rectangle to decompress (in image coordinates).

	 *
	 * @return	true			if the area could be set.
	 */
	bool set_decompress_area(grk_image *p_image,
						uint32_t start_x,
						uint32_t start_y,
						uint32_t end_x,
						uint32_t end_y);


	/** Decoding function */
   bool decompress( grk_plugin_tile *tile,	grk_image *p_image);

	/** Reading function used after code stream if necessary */
   bool end_decompress(void);

   bool init_compress(grk_cparameters  *p_param,grk_image *p_image);

   bool start_compress(void);

   bool compress(grk_plugin_tile* tile);

   bool compress_tile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size);

   bool end_compress(void);

	bool decompress_tile(grk_image *p_image,uint16_t tile_index);

   void dump(int32_t flag, FILE *out_stream);

   grk_codestream_info_v2* get_cstr_info(void);

   grk_codestream_index* get_cstr_index(void);


	/** handle to the J2K codec  */
	CodeStream *codeStream;
	/** list of validation procedures */
	std::vector<jp2_procedure> *m_validation_list;
	/** list of execution procedures */
	std::vector<jp2_procedure> *m_procedure_list;

	/* width of image */
	uint32_t w;
	/* height of image */
	uint32_t h;
	/* number of components in the image */
	uint32_t numcomps;
	uint32_t bpc;
	uint32_t C;
	uint32_t UnkC;
	uint32_t IPR;
	uint32_t meth;
	uint32_t approx;
	GRK_ENUM_COLOUR_SPACE enumcs;
	uint32_t precedence;
	uint32_t brand;
	uint32_t minversion;
	uint32_t numcl;
	uint32_t *cl;
	grk_jp2_comps *comps;
	uint64_t j2k_codestream_offset;
	bool needs_xl_jp2c_box_length;
	uint32_t jp2_state;
	uint32_t jp2_img_state;
	grk_jp2_color color;

	bool has_capture_resolution;
	double capture_resolution[2];

	bool has_display_resolution;
	double display_resolution[2];

	grk_jp2_buffer xml;
	grk_jp2_uuid uuids[JP2_MAX_NUM_UUIDS];
	uint32_t numUuids;

	BufferedStream *m_stream;
};

/**
 JP2 Box
 */
struct grk_jp2_box {
	uint64_t length;
	uint32_t type;
};

struct grk_jp2_header_handler {
	/* marker value */
	uint32_t id;
	/* action linked to the marker */
	bool (*handler)(FileFormat *fileFormat, uint8_t *p_header_data, uint32_t header_size);
};

struct grk_jp2_img_header_writer_handler {
	/* action to perform */
	uint8_t* (*handler)(FileFormat *fileFormat, uint32_t *data_size);
	/* result of the action : data */
	uint8_t *m_data;
	/* size of data */
	uint32_t m_size;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/*@}*/

/*@}*/

}

