/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

#include "grk_includes.h"

namespace grk
{
bool Quantizer::setBandStepSizeAndBps(TileCodingParams* tcp, Subband* band, uint32_t resno,
									  uint8_t bandIndex, TileComponentCodingParams* tccp,
									  uint8_t image_precision, bool compress)
{
	/* Table E-1 - Sub-band gains */
	/* BUG_WEIRD_TWO_INVK (look for this identifier in dwt.c): */
	/* the test (!isEncoder && l_tccp->qmfbid == 0) is strongly */
	/* linked to the use of two_invK instead of invK */
	const uint32_t log2_gain = (!compress && tccp->qmfbid == 0) ? 0
							   : (band->orientation == 0)		? 0
							   : (band->orientation == 3)		? 2
																: 1;
	uint32_t numbps = image_precision + log2_gain;
	auto offset = (resno == 0) ? 0 : 3 * resno - 2;
	auto step_size = tccp->stepsizes + offset + bandIndex;
	band->stepsize =
		(float)(((1.0 + step_size->mant / 2048.0) * pow(2.0, (int32_t)(numbps - step_size->expn))));
	// printf("res=%d, band=%d, mant=%d,expn=%d, numbps=%d, step size=
	// %f\n",resno,band->orientation,step_size->mant,step_size->expn,numbps, band->stepsize);

	// see Taubman + Marcellin - Equation 10.22
	band->numbps = tccp->roishift +
				   (uint8_t)std::max<int8_t>(0, int8_t(step_size->expn + tccp->numgbits - 1U));
	// assert(band->numbps <= maxBitPlanesGRK);

	if(tcp->getIsHT())
	{
		// lossy decompress
		if(!compress && tccp->qmfbid == 0)
		{
			if(band->numbps > 31)
			{
				GRK_ERROR("Unsupported number of band bps %u", band->numbps);
				return false;
			}
			band->stepsize /= (float)(1u << (31 - band->numbps));
		}
	}

	return true;
}

void Quantizer::apply_quant(TileComponentCodingParams* src, TileComponentCodingParams* dest)
{
	if(!src || !dest)
		return;

	// respect the QCD/QCC scoping rules
	bool ignore = false;
	if(dest->fromQCC)
	{
		if(!src->fromTileHeader || dest->fromTileHeader)
			ignore = true;
	}
	if(!ignore)
	{
		dest->qntsty = src->qntsty;
		dest->numgbits = src->numgbits;
		auto size = GRK_J2K_MAXBANDS * sizeof(grk_stepsize);
		memcpy(dest->stepsizes, src->stepsizes, size);
	}
}

bool Quantizer::write_SQcd_SQcc(CodeStream* codeStream, uint32_t comp_no, IBufferedStream* stream)
{
	assert(codeStream != nullptr);

	auto cp = codeStream->getCodingParams();
	auto tcp = &cp->tcps[0];
	auto tccp = &tcp->tccps[comp_no];

	assert(comp_no < codeStream->getHeaderImage()->numcomps);

	uint32_t num_bands =
		(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions * 3U - 2);

	/* Sqcx */
	if(!stream->writeByte((uint8_t)(tccp->qntsty + (tccp->numgbits << 5))))
		return false;

	/* SPqcx_i */
	for(uint32_t band_no = 0; band_no < num_bands; ++band_no)
	{
		uint32_t expn = tccp->stepsizes[band_no].expn;
		uint32_t mant = tccp->stepsizes[band_no].mant;
		if(tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
		{
			if(!stream->writeByte((uint8_t)(expn << 3)))
				return false;
		}
		else
		{
			if(!stream->writeShort((uint16_t)((expn << 11) + mant)))
				return false;
		}
	}
	return true;
}

uint32_t Quantizer::get_SQcd_SQcc_size(CodeStream* codeStream, uint32_t comp_no)
{
	assert(codeStream != nullptr);

	auto cp = codeStream->getCodingParams();
	auto tcp = &cp->tcps[0];
	auto tccp = &tcp->tccps[comp_no];

	assert(comp_no < codeStream->getHeaderImage()->numcomps);

	uint32_t num_bands =
		(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions * 3U - 2);

	if(tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
		return 1 + num_bands;
	else
		return 1 + 2 * num_bands;
}

bool Quantizer::compare_SQcd_SQcc(CodeStream* codeStream, uint32_t first_comp_no,
								  uint32_t second_comp_no)
{
	assert(codeStream != nullptr);

	auto cp = codeStream->getCodingParams();
	auto tcp = &cp->tcps[0];
	auto tccp0 = &tcp->tccps[first_comp_no];
	auto tccp1 = &tcp->tccps[second_comp_no];

	if(tccp0->qntsty != tccp1->qntsty)
		return false;
	if(tccp0->numgbits != tccp1->numgbits)
		return false;
	uint32_t band_no, num_bands;
	if(tccp0->qntsty == J2K_CCP_QNTSTY_SIQNT)
	{
		num_bands = 1U;
	}
	else
	{
		num_bands = tccp0->numresolutions * 3U - 2U;
		if(num_bands != (tccp1->numresolutions * 3U - 2U))
			return false;
	}
	for(band_no = 0; band_no < num_bands; ++band_no)
	{
		if(tccp0->stepsizes[band_no].expn != tccp1->stepsizes[band_no].expn)
			return false;
	}
	if(tccp0->qntsty != J2K_CCP_QNTSTY_NOQNT)
	{
		for(band_no = 0; band_no < num_bands; ++band_no)
		{
			if(tccp0->stepsizes[band_no].mant != tccp1->stepsizes[band_no].mant)
				return false;
		}
	}
	return true;
}

bool Quantizer::read_SQcd_SQcc(CodeStreamDecompress* codeStream, bool fromQCC, uint32_t comp_no,
							   uint8_t* headerData, uint16_t* header_size)
{
	assert(codeStream != nullptr);
	assert(headerData != nullptr);
	assert(comp_no < codeStream->getHeaderImage()->numcomps);
	if(*header_size < 1)
	{
		GRK_ERROR("Error reading SQcd or SQcc element");
		return false;
	}
	/* Sqcx */
	uint32_t tmp = 0;
	auto current_ptr = headerData;
	grk_read<uint32_t>(current_ptr++, &tmp, 1);
	uint8_t qntsty = tmp & 0x1f;
	*header_size = (uint16_t)(*header_size - 1);
	if(qntsty > J2K_CCP_QNTSTY_SEQNT)
	{
		GRK_ERROR("Undefined quantization style %d", qntsty);
		return false;
	}

	// scoping rules
	auto tcp = codeStream->get_current_decode_tcp();
	auto tccp = tcp->tccps + comp_no;
	bool ignore = false;
	bool fromTileHeader = codeStream->isDecodingTilePartHeader();
	bool mainQCD = !fromQCC && !fromTileHeader;

	if(tccp->quantizationMarkerSet)
	{
		bool tileHeaderQCC = fromQCC && fromTileHeader;
		bool setMainQCD = !tccp->fromQCC && !tccp->fromTileHeader;
		bool setMainQCC = tccp->fromQCC && !tccp->fromTileHeader;
		bool setTileHeaderQCD = !tccp->fromQCC && tccp->fromTileHeader;
		bool setTileHeaderQCC = tccp->fromQCC && tccp->fromTileHeader;

		if(!fromTileHeader)
		{
			if(setMainQCC || (mainQCD && setMainQCD))
				ignore = true;
		}
		else
		{
			if(setTileHeaderQCC)
				ignore = true;
			else if(setTileHeaderQCD && !tileHeaderQCC)
				ignore = true;
		}
	}

	if(!ignore)
	{
		tccp->quantizationMarkerSet = true;
		tccp->fromQCC = fromQCC;
		tccp->fromTileHeader = fromTileHeader;
		tccp->qntsty = qntsty;
		if(mainQCD)
			tcp->main_qcd_qntsty = tccp->qntsty;
		tccp->numgbits = (uint8_t)(tmp >> 5);
		if(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT)
		{
			tccp->numStepSizes = 1;
		}
		else
		{
			tccp->numStepSizes = (tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
									 ? (uint8_t)(*header_size)
									 : (uint8_t)((*header_size) / 2);
			if(tccp->numStepSizes > GRK_J2K_MAXBANDS)
			{
				GRK_WARN("While reading QCD or QCC marker segment, "
						 "number of step sizes (%u) is greater"
						 " than GRK_J2K_MAXBANDS (%u).\n"
						 "So, number of elements stored is limited to "
						 "GRK_J2K_MAXBANDS (%u) and the rest are skipped.",
						 tccp->numStepSizes, GRK_J2K_MAXBANDS, GRK_J2K_MAXBANDS);
			}
		}
		if(mainQCD)
			tcp->main_qcd_numStepSizes = tccp->numStepSizes;
	}
	if(qntsty == J2K_CCP_QNTSTY_NOQNT)
	{
		if(*header_size < tccp->numStepSizes)
		{
			GRK_ERROR("Error reading SQcd_SQcc marker");
			return false;
		}
		for(uint32_t band_no = 0; band_no < tccp->numStepSizes; band_no++)
		{
			/* SPqcx_i */
			grk_read<uint32_t>(current_ptr++, &tmp, 1);
			if(!ignore)
			{
				if(band_no < GRK_J2K_MAXBANDS)
				{
					// top 5 bits for exponent
					tccp->stepsizes[band_no].expn = (uint8_t)(tmp >> 3);
					// mantissa = 0
					tccp->stepsizes[band_no].mant = 0;
				}
			}
		}
		*header_size = (uint16_t)(*header_size - tccp->numStepSizes);
	}
	else
	{
		if(*header_size < 2 * tccp->numStepSizes)
		{
			GRK_ERROR("Error reading SQcd_SQcc marker");
			return false;
		}
		for(uint32_t band_no = 0; band_no < tccp->numStepSizes; band_no++)
		{
			/* SPqcx_i */
			grk_read<uint32_t>(current_ptr, &tmp, 2);
			current_ptr += 2;
			if(!ignore)
			{
				if(band_no < GRK_J2K_MAXBANDS)
				{
					// top 5 bits for exponent
					tccp->stepsizes[band_no].expn = (uint8_t)(tmp >> 11);
					// bottom 11 bits for mantissa
					tccp->stepsizes[band_no].mant = (uint16_t)(tmp & 0x7ff);
				}
			}
		}
		*header_size = (uint16_t)(*header_size - 2 * tccp->numStepSizes);
	}
	if(!ignore)
	{
		/* if scalar derived, then compute other stepsizes */
		if(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT)
		{
			for(uint32_t band_no = 1; band_no < GRK_J2K_MAXBANDS; band_no++)
			{
				uint8_t bandDividedBy3 = (uint8_t)((band_no - 1) / 3);
				tccp->stepsizes[band_no].expn = 0;
				if(tccp->stepsizes[0].expn > bandDividedBy3)
					tccp->stepsizes[band_no].expn =
						(uint8_t)(tccp->stepsizes[0].expn - bandDividedBy3);
				tccp->stepsizes[band_no].mant = tccp->stepsizes[0].mant;
			}
		}
	}
	return true;
}

} // namespace grk
