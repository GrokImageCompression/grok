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
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2010-2011, Kaori Hagihara
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "grok_includes.h"

namespace grk {

void Quantizer::setBandStepSizeAndBps(grk_tcp *tcp,
							grk_tcd_band *band,
		                   uint32_t resno,
						   uint8_t bandno,
							grk_tccp *tccp,
							uint32_t image_precision,
							float fraction){

	uint32_t gain = 0;
	if (tccp->qmfbid == 1) {
		if (band->bandno == 0)
			gain = 0;
		else if (band->bandno < 3)
			gain = 1;
		else
			gain = 2;
	}
	uint32_t numbps = image_precision + gain;
	auto offset = (resno == 0) ? 0 : 3*resno - 2;
	auto step_size = tccp->stepsizes + offset + bandno;
	band->stepsize = (float) (((1.0 + step_size->mant / 2048.0)
			* pow(2.0, (int32_t) (numbps - step_size->expn))))
			* fraction;

	// see Taubman + Marcellin - Equation 10.22
	band->numbps = tccp->roishift
			+ std::max<int32_t>(0,
					(int32_t) (step_size->expn + tccp->numgbits)
							- 1);
	band->inv_step = 8192 * 8192
					/ ((int32_t) floor(band->stepsize * 8192));

	if (tcp->isHT){
		 // decode
		 if (fraction == 0.5 && tccp->qmfbid == 0) {
			 // 31 - K_max == 30 - band->numbps
			 band->stepsize /=(float)(1u << (30 - band->numbps));
		 }
	}
}

void Quantizer::calc_explicit_stepsizes(grk_tccp *tccp, uint32_t prec) {
	uint32_t numbands, bandno;
	numbands = 3 * tccp->numresolutions - 2;
	for (bandno = 0; bandno < numbands; bandno++) {
		uint32_t resno = (bandno == 0) ? 0 : ((bandno - 1) / 3 + 1);
		uint8_t orient = (bandno == 0) ? 0 : (uint8_t)((bandno - 1) % 3 + 1);
		uint32_t level = tccp->numresolutions - 1 - resno;
		uint32_t gain =	(tccp->qmfbid == 0) ? 	0 :
				((orient == 0) ? 	0 : (((orient == 1) || (orient == 2)) ? 1 : 2));
		double stepsize = 1.0;
		if (tccp->qntsty != J2K_CCP_QNTSTY_NOQNT) {
			double norm = dwt_utils::getnorm_real(level,orient);
			stepsize = (double) ((uint64_t) 1 << gain) / norm;
		}
		int32_t stepsize_fp = (int32_t) floor(stepsize * 8192.0);
		int32_t numbps = (int32_t) (prec + gain);
		int32_t p, n;
		p = int_floorlog2(stepsize_fp) - 13;
		n = 11 - int_floorlog2(stepsize_fp);
		grk_stepsize *bandno_stepsize = tccp->stepsizes + bandno;
		bandno_stepsize->mant = (uint16_t)((n < 0 ? stepsize_fp >> -n : stepsize_fp << n) & 0x7ff);
		bandno_stepsize->expn = (uint8_t)(numbps - p);
	}
}

void Quantizer::apply_quant(grk_tccp *src, grk_tccp *dest){
	if (!src || !dest)
		return;

	// respect the QCD/QCC scoping rules
	bool ignore = false;
	if (dest->fromQCC) {
		if (!src->fromTileHeader
				|| (src->fromTileHeader
						&& dest->fromTileHeader))
			ignore = true;
	}
	if (!ignore) {
		dest->qntsty = src->qntsty;
		dest->numgbits = src->numgbits;
		auto l_size = GRK_J2K_MAXBANDS * sizeof(grk_stepsize);
		memcpy(dest->stepsizes, src->stepsizes, l_size);
	}
}


bool Quantizer::write_SQcd_SQcc(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t comp_no, BufferedStream *p_stream) {
	assert(p_j2k != nullptr);

	auto l_cp = &(p_j2k->m_cp);
	auto l_tcp = &l_cp->tcps[tile_no];
	auto l_tccp = &l_tcp->tccps[comp_no];

	assert(tile_no < l_cp->tw * l_cp->th);
	assert(comp_no < p_j2k->m_private_image->numcomps);

	uint32_t l_num_bands =
			(l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ?
					1 : (l_tccp->numresolutions * 3 - 2);

	/* Sqcx */
	if (!p_stream->write_byte(
			(uint8_t) (l_tccp->qntsty + (l_tccp->numgbits << 5)))) {
		return false;
	}
	/* SPqcx_i */
	for (uint32_t l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
		uint32_t expn = l_tccp->stepsizes[l_band_no].expn;
		uint32_t mant = l_tccp->stepsizes[l_band_no].mant;
		if (l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) {
			if (!p_stream->write_byte((uint8_t) (expn << 3))) {
				return false;
			}
		} else {
			if (!p_stream->write_short((uint16_t) ((expn << 11) + mant))) {
				return false;
			}
		}
	}
	return true;
}


uint32_t Quantizer::get_SQcd_SQcc_size(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t comp_no) {
	assert(p_j2k != nullptr);

	auto l_cp = &(p_j2k->m_cp);
	auto l_tcp = &l_cp->tcps[tile_no];
	auto l_tccp = &l_tcp->tccps[comp_no];

	assert(tile_no < l_cp->tw * l_cp->th);
	assert(comp_no < p_j2k->m_private_image->numcomps);

	uint32_t l_num_bands =
			(l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ?
					1 : (l_tccp->numresolutions * 3 - 2);

	if (l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) {
		return 1 + l_num_bands;
	} else {
		return 1 + 2 * l_num_bands;
	}
}

bool Quantizer::compare_SQcd_SQcc(grk_j2k *p_j2k, uint16_t tile_no,
		uint32_t first_comp_no, uint32_t second_comp_no) {
	assert(p_j2k != nullptr);

	auto l_cp = &(p_j2k->m_cp);
	auto l_tcp = &l_cp->tcps[tile_no];
	auto l_tccp0 = &l_tcp->tccps[first_comp_no];
	auto l_tccp1 = &l_tcp->tccps[second_comp_no];

	if (l_tccp0->qntsty != l_tccp1->qntsty) {
		return false;
	}
	if (l_tccp0->numgbits != l_tccp1->numgbits) {
		return false;
	}
	uint32_t l_band_no, l_num_bands;
	if (l_tccp0->qntsty == J2K_CCP_QNTSTY_SIQNT) {
		l_num_bands = 1U;
	} else {
		l_num_bands = l_tccp0->numresolutions * 3U - 2U;
		if (l_num_bands != (l_tccp1->numresolutions * 3U - 2U)) {
			return false;
		}
	}
	for (l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
		if (l_tccp0->stepsizes[l_band_no].expn
				!= l_tccp1->stepsizes[l_band_no].expn) {
			return false;
		}
	}
	if (l_tccp0->qntsty != J2K_CCP_QNTSTY_NOQNT) {
		for (l_band_no = 0; l_band_no < l_num_bands; ++l_band_no) {
			if (l_tccp0->stepsizes[l_band_no].mant
					!= l_tccp1->stepsizes[l_band_no].mant) {
				return false;
			}
		}
	}
	return true;
}

bool Quantizer::read_SQcd_SQcc(bool fromQCC, grk_j2k *p_j2k, uint32_t comp_no,
		uint8_t *p_header_data, uint16_t *header_size) {
	assert(p_j2k != nullptr);
	assert(p_header_data != nullptr);
	assert(comp_no < p_j2k->m_private_image->numcomps);
	if (*header_size < 1) {
		GROK_ERROR( "Error reading SQcd or SQcc element");
		return false;
	}
	*header_size = (uint16_t)(*header_size - 1);
	/* Sqcx */
	uint32_t l_tmp = 0;
	auto l_current_ptr = p_header_data;
	grk_read_bytes(l_current_ptr, &l_tmp, 1);
	++l_current_ptr;
	uint8_t qntsty = l_tmp & 0x1f;
	// scoping rules
	auto l_tcp = p_j2k->get_current_decode_tcp();
	auto l_tccp = l_tcp->tccps + comp_no;

	bool ignore = false;
	bool fromTileHeader = p_j2k->decodingTilePartHeader();
	bool mainQCD = !fromQCC && !fromTileHeader;
	if ((!fromTileHeader && !fromQCC) && l_tccp->fromQCC)
		ignore = true;
	if ((fromTileHeader && !fromQCC)
			&& (l_tccp->fromTileHeader && l_tccp->fromQCC))
		ignore = true;
	if (!ignore) {
		l_tccp->fromQCC = fromQCC;
		l_tccp->fromTileHeader = fromTileHeader;
		l_tccp->qntsty = qntsty;
		if (mainQCD)
			l_tcp->main_qcd_qntsty = l_tccp->qntsty;
		l_tccp->numgbits = (uint8_t)(l_tmp >> 5);
		if (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) {
			l_tccp->numStepSizes = 1;
		} else {
			l_tccp->numStepSizes =
					(l_tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) ?
							(uint8_t)(*header_size) : (uint8_t)((*header_size) / 2);
			if (l_tccp->numStepSizes > GRK_J2K_MAXBANDS) {
				GROK_WARN(
						"While reading QCD or QCC marker segment, "
								"number of step sizes (%d) is greater"
								" than GRK_J2K_MAXBANDS (%d). "
								"So, we limit the number of elements stored to "
								"GRK_J2K_MAXBANDS (%d) and skip the rest.\n",
						l_tccp->numStepSizes, GRK_J2K_MAXBANDS,
						GRK_J2K_MAXBANDS);
			}
		}
		if (mainQCD)
			l_tcp->main_qcd_numStepSizes = l_tccp->numStepSizes;
	}
	if (qntsty == J2K_CCP_QNTSTY_NOQNT) {
		for (uint32_t l_band_no = 0; l_band_no < l_tccp->numStepSizes;
				l_band_no++) {
			/* SPqcx_i */
			grk_read_bytes(l_current_ptr++, &l_tmp, 1);
			if (!ignore) {
				if (l_band_no < GRK_J2K_MAXBANDS) {
					//top 5 bits for exponent
					l_tccp->stepsizes[l_band_no].expn = (uint8_t)(l_tmp >> 3);
					// mantissa = 0
					l_tccp->stepsizes[l_band_no].mant = 0;
				}
			}
		}
		if (*header_size < l_tccp->numStepSizes) {
			GROK_ERROR( "Error reading SQcd_SQcc marker");
			return false;
		}
		*header_size = (uint16_t)(*header_size - l_tccp->numStepSizes);
	} else {
		for (uint32_t l_band_no = 0; l_band_no < l_tccp->numStepSizes;
				l_band_no++) {
			/* SPqcx_i */
			grk_read_bytes(l_current_ptr, &l_tmp, 2);
			l_current_ptr += 2;
			if (!ignore) {
				if (l_band_no < GRK_J2K_MAXBANDS) {
					// top 5 bits for exponent
					l_tccp->stepsizes[l_band_no].expn = (uint8_t)(l_tmp >> 11);
					// bottom 11 bits for mantissa
					l_tccp->stepsizes[l_band_no].mant = (uint16_t)(l_tmp & 0x7ff);
				}
			}
		}
		if (*header_size < 2 * l_tccp->numStepSizes) {
			GROK_ERROR( "Error reading SQcd_SQcc marker");
			return false;
		}
		*header_size = (uint16_t)(*header_size - 2 * l_tccp->numStepSizes);
	}
	if (!ignore) {
		/* if scalar derived, then compute other stepsizes */
		if (l_tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) {
			for (uint32_t l_band_no = 1; l_band_no < GRK_J2K_MAXBANDS;
					l_band_no++) {
				uint8_t bandDividedBy3 = (uint8_t)((l_band_no - 1) / 3);
				l_tccp->stepsizes[l_band_no].expn = 0;
				if (l_tccp->stepsizes[0].expn > bandDividedBy3)
					l_tccp->stepsizes[l_band_no].expn =
							(uint8_t)(l_tccp->stepsizes[0].expn - bandDividedBy3);
				l_tccp->stepsizes[l_band_no].mant = l_tccp->stepsizes[0].mant;
			}
		}
		if (p_j2k->m_cp.ccap){
			l_tcp->isHT = true;
			l_tcp->qcd.generate(l_tccp->numgbits,
							l_tccp->numresolutions-1,
							l_tccp->qmfbid == 1,
							p_j2k->m_private_image->comps[0].prec,
							l_tcp->mct > 0,
							p_j2k->m_private_image->comps[0].sgnd);
			l_tcp->qcd.push(l_tccp->stepsizes, l_tccp->qmfbid == 1);
		}
	}
	return true;
}

}

