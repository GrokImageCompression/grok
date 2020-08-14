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

#include <vector>
#include <map>

namespace grk {

struct FileFormat;


/**
 * Dump some elements from the J2K decompression structure .
 *
 *@param codeStream				JPEG 2000 code stream
 *@param flag				flag to describe what elements are dumped.
 *@param out_stream			output stream where dump the elements.
 *
 */
void j2k_dump(CodeStream *codeStream, int32_t flag, FILE *out_stream);

/**
 * Dump an image header structure.
 *
 *@param image			the image header to dump.
 *@param dev_dump_flag		flag to describe if we are in the case of this function is use outside j2k_dump function
 *@param out_stream			output stream where dump the elements.
 */
void j2k_dump_image_header(grk_image *image, bool dev_dump_flag,
		FILE *out_stream);

/**
 * Dump a component image header structure.
 *
 *@param comp		the component image header to dump.
 *@param dev_dump_flag		flag to describe if we are in the case of this function is use outside j2k_dump function
 *@param out_stream			output stream where dump the elements.
 */
void j2k_dump_image_comp_header( grk_image_comp  *comp, bool dev_dump_flag,
		FILE *out_stream);

/**
 * Get the code stream info from a JPEG2000 codec.
 *
 *@param	codeStream				the component image header to dump.
 *
 *@return	the code stream information extract from the jpg2000 codec
 */
 grk_codestream_info_v2  *  j2k_get_cstr_info(CodeStream *codeStream);

/**
 * Get the code stream index from a JPEG2000 codec.
 *
 *@param	codeStream				the component image header to dump.
 *
 *@return	the code stream index extract from the jpg2000 codec
 */
 grk_codestream_index  *  j2k_get_cstr_index(CodeStream *codeStream);

 grk_codestream_index  *  j2k_create_cstr_index(void);

 bool j2k_allocate_tile_element_cstr_index(CodeStream *codeStream);

 /**
  * Destroys a code stream index structure.
  *
  * @param	p_cstr_ind	the code stream index parameter to destroy.
  */
 void j2k_destroy_cstr_index( grk_codestream_index  *p_cstr_ind);

 /**
  * Dump some elements from the JP2 decompression structure .
  *
  *@param fileFormat        the jp2 codec.
  *@param flag        flag to describe what elements are dump.
  *@param out_stream      output stream where dump the elements.
  *
  */
 void jp2_dump(FileFormat *fileFormat, int32_t flag, FILE *out_stream);

 /**
  * Get the code stream info from a JPEG2000 codec.
  *
  *@param  fileFormat        jp2 codec.
  *
  *@return  the code stream information extract from the jpg2000 codec
  */
  grk_codestream_info_v2  *  jp2_get_cstr_info(FileFormat *fileFormat);

 /**
  * Get the code stream index from a JPEG2000 codec.
  *
  *@param  fileFormat        jp2 codec.
  *
  *@return  the code stream index extract from the jpg2000 codec
  */
  grk_codestream_index  *  jp2_get_cstr_index(FileFormat *fileFormat);


}
