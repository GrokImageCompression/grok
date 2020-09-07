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

#include <cstdint>


namespace grk {


struct CodeStream;

/**
 * Quantization stepsize
 */
struct grk_stepsize {
	grk_stepsize() : expn(0), mant(0)
	{}
	/** exponent - 5 bits */
	uint8_t expn;
	/** mantissa  -11 bits */
	uint16_t mant;
};



struct CodeStream;
struct TileComponentCodingParams;
struct BufferedStream;
struct grk_band;
struct TileCodingParams;
struct TileProcessor;


class Quantizer {
public:

	void setBandStepSizeAndBps( TileCodingParams *tcp,
								grk_band *band,
								uint32_t resno,
								 uint8_t bandno,
								TileComponentCodingParams *tccp,
								uint32_t image_precision,
								bool encode);


	uint32_t get_SQcd_SQcc_size(CodeStream *codeStream,	uint32_t comp_no);
	bool compare_SQcd_SQcc(CodeStream *codeStream,
			uint32_t first_comp_no, uint32_t second_comp_no);
	bool read_SQcd_SQcc(CodeStream *codeStream,bool fromQCC, uint32_t comp_no,
			uint8_t *p_header_data, uint16_t *header_size);
	bool write_SQcd_SQcc(CodeStream *codeStream,uint32_t comp_no, BufferedStream *stream);
	void apply_quant(TileComponentCodingParams *src, TileComponentCodingParams *dest);
};

}
