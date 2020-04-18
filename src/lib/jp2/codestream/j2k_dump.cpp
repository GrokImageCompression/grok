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


static void j2k_dump_MH_info(grk_j2k *p_j2k, FILE *out_stream);

static void j2k_dump_MH_index(grk_j2k *p_j2k, FILE *out_stream);

static void j2k_dump_tile_info(grk_tcp *default_tile, uint32_t numcomps,
		FILE *out_stream) {
	if (default_tile) {
		uint32_t compno;

		fprintf(out_stream, "\t default tile {\n");
		fprintf(out_stream, "\t\t csty=%#x\n", default_tile->csty);
		fprintf(out_stream, "\t\t prg=%#x\n", default_tile->prg);
		fprintf(out_stream, "\t\t numlayers=%d\n", default_tile->numlayers);
		fprintf(out_stream, "\t\t mct=%x\n", default_tile->mct);

		for (compno = 0; compno < numcomps; compno++) {
			grk_tccp *tccp = &(default_tile->tccps[compno]);
			uint32_t resno;
			uint32_t bandno, numbands;

			assert(tccp->numresolutions > 0);

			/* coding style*/
			fprintf(out_stream, "\t\t comp %d {\n", compno);
			fprintf(out_stream, "\t\t\t csty=%#x\n", tccp->csty);
			fprintf(out_stream, "\t\t\t numresolutions=%d\n",
				 tccp->numresolutions);
			fprintf(out_stream, "\t\t\t cblkw=2^%d\n", tccp->cblkw);
			fprintf(out_stream, "\t\t\t cblkh=2^%d\n", tccp->cblkh);
			fprintf(out_stream, "\t\t\t cblksty=%#x\n", tccp->cblk_sty);
			fprintf(out_stream, "\t\t\t qmfbid=%d\n", tccp->qmfbid);

			fprintf(out_stream, "\t\t\t preccintsize (w,h)=");
			for (resno = 0; resno < tccp->numresolutions; resno++) {
				fprintf(out_stream, "(%d,%d) ", tccp->prcw[resno],
					 tccp->prch[resno]);
			}
			fprintf(out_stream, "\n");

			/* quantization style*/
			fprintf(out_stream, "\t\t\t qntsty=%d\n", tccp->qntsty);
			fprintf(out_stream, "\t\t\t numgbits=%d\n", tccp->numgbits);
			fprintf(out_stream, "\t\t\t stepsizes (m,e)=");
			numbands =
					(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ?
							1 : (uint32_t) (tccp->numresolutions * 3 - 2);
			for (bandno = 0; bandno < numbands; bandno++) {
				fprintf(out_stream, "(%d,%d) ", tccp->stepsizes[bandno].mant,
					 tccp->stepsizes[bandno].expn);
			}
			fprintf(out_stream, "\n");

			/* RGN value*/
			fprintf(out_stream, "\t\t\t roishift=%d\n", tccp->roishift);

			fprintf(out_stream, "\t\t }\n");
		} /*end of component of default tile*/
		fprintf(out_stream, "\t }\n"); /*end of default tile*/
	}
}

void j2k_dump(grk_j2k *p_j2k, int32_t flag, FILE *out_stream) {
	/* Check if the flag is compatible with j2k file*/
	if ((flag & GRK_JP2_INFO) || (flag & GRK_JP2_IND)) {
		fprintf(out_stream, "Wrong flag\n");
		return;
	}

	/* Dump the image_header */
	if (flag & GRK_IMG_INFO) {
		if (p_j2k->m_private_image)
			j2k_dump_image_header(p_j2k->m_private_image, 0, out_stream);
	}

	/* Dump the code stream info from main header */
	if (flag & GRK_J2K_MH_INFO) {
		if (p_j2k->m_private_image)
			j2k_dump_MH_info(p_j2k, out_stream);
	}
	/* Dump all tile/code stream info */
	if (flag & GRK_J2K_TCH_INFO) {
		uint32_t nb_tiles = p_j2k->m_cp.t_grid_height * p_j2k->m_cp.t_grid_width;
		uint32_t i;
		grk_tcp *tcp = p_j2k->m_cp.tcps;
		if (p_j2k->m_private_image) {
			for (i = 0; i < nb_tiles; ++i) {
				j2k_dump_tile_info(tcp, p_j2k->m_private_image->numcomps,
						out_stream);
				++tcp;
			}
		}
	}

	/* Dump the code stream info of the current tile */
	if (flag & GRK_J2K_TH_INFO) {

	}

	/* Dump the code stream index from main header */
	if (flag & GRK_J2K_MH_IND) {
		j2k_dump_MH_index(p_j2k, out_stream);
	}

	/* Dump the code stream index of the current tile */
	if (flag & GRK_J2K_TH_IND) {

	}

}

static void j2k_dump_MH_index(grk_j2k *p_j2k, FILE *out_stream) {
	 grk_codestream_index  *cstr_index = p_j2k->cstr_index;
	uint32_t it_marker, it_tile, it_tile_part;

	fprintf(out_stream, "Codestream index from main header: {\n");

	std::stringstream ss;
	ss << "\t Main header start position=" << cstr_index->main_head_start
			<< std::endl << "\t Main header end position="
			<< cstr_index->main_head_end << std::endl;

	fprintf(out_stream, "%s", ss.str().c_str());
	fprintf(out_stream, "\t Marker list: {\n");

	if (cstr_index->marker) {
		for (it_marker = 0; it_marker < cstr_index->marknum; it_marker++) {
			fprintf(out_stream, "\t\t type=%#x, pos=%" PRIu64", len=%d\n",
					cstr_index->marker[it_marker].type,
					cstr_index->marker[it_marker].pos,
					cstr_index->marker[it_marker].len);

		}
	}

	fprintf(out_stream, "\t }\n");

	if (cstr_index->tile_index) {

		/* Simple test to avoid to write empty information*/
		uint32_t acc_nb_of_tile_part = 0;
		for (it_tile = 0; it_tile < cstr_index->nb_of_tiles; it_tile++) {
		 acc_nb_of_tile_part += cstr_index->tile_index[it_tile].nb_tps;
		}

		if (acc_nb_of_tile_part) {
			fprintf(out_stream, "\t Tile index: {\n");

			for (it_tile = 0; it_tile < cstr_index->nb_of_tiles; it_tile++) {
				uint32_t nb_of_tile_part =
						cstr_index->tile_index[it_tile].nb_tps;

				fprintf(out_stream, "\t\t nb of tile-part in tile [%d]=%d\n",
						it_tile, nb_of_tile_part);

				if (cstr_index->tile_index[it_tile].tp_index) {
					for (it_tile_part = 0; it_tile_part < nb_of_tile_part;
							it_tile_part++) {
						ss.clear();
						ss << "\t\t\t tile-part[" << it_tile_part << "]:"
								<< " star_pos="
								<< cstr_index->tile_index[it_tile].tp_index[it_tile_part].start_pos
								<< "," << " end_header="
								<< cstr_index->tile_index[it_tile].tp_index[it_tile_part].end_header
								<< "," << " end_pos="
								<< cstr_index->tile_index[it_tile].tp_index[it_tile_part].end_pos
								<< std::endl;
						fprintf(out_stream, "%s", ss.str().c_str());
					}
				}

				if (cstr_index->tile_index[it_tile].marker) {
					for (it_marker = 0;
							it_marker < cstr_index->tile_index[it_tile].marknum;
							it_marker++) {
						ss.clear();
						ss << "\t\t type="
								<< cstr_index->tile_index[it_tile].marker[it_marker].type
								<< "," << " pos="
								<< cstr_index->tile_index[it_tile].marker[it_marker].pos
								<< "," << " len="
								<< cstr_index->tile_index[it_tile].marker[it_marker].len
								<< std::endl;
						fprintf(out_stream, "%s", ss.str().c_str());
					}
				}
			}
			fprintf(out_stream, "\t }\n");
		}
	}
	fprintf(out_stream, "}\n");
}

static void j2k_dump_MH_info(grk_j2k *p_j2k, FILE *out_stream) {

	fprintf(out_stream, "Codestream info from main header: {\n");

	fprintf(out_stream, "\t tx0=%d, ty0=%d\n", p_j2k->m_cp.tx0,
			p_j2k->m_cp.ty0);
	fprintf(out_stream, "\t tdx=%d, tdy=%d\n", p_j2k->m_cp.t_width,
			p_j2k->m_cp.t_height);
	fprintf(out_stream, "\t tw=%d, th=%d\n", p_j2k->m_cp.t_grid_width, p_j2k->m_cp.t_grid_height);
	j2k_dump_tile_info(p_j2k->m_specific_param.m_decoder.m_default_tcp,
			p_j2k->m_private_image->numcomps, out_stream);
	fprintf(out_stream, "}\n");
}

void j2k_dump_image_header(grk_image *img_header, bool dev_dump_flag,
		FILE *out_stream) {
	char tab[2];

	if (dev_dump_flag) {
		fprintf(stdout, "[DEV] Dump an image_header struct {\n");
		tab[0] = '\0';
	} else {
		fprintf(out_stream, "Image info {\n");
		tab[0] = '\t';
		tab[1] = '\0';
	}

	fprintf(out_stream, "%s x0=%d, y0=%d\n", tab, img_header->x0,
			img_header->y0);
	fprintf(out_stream, "%s x1=%d, y1=%d\n", tab, img_header->x1,
			img_header->y1);
	fprintf(out_stream, "%s numcomps=%d\n", tab, img_header->numcomps);

	if (img_header->comps) {
		uint32_t compno;
		for (compno = 0; compno < img_header->numcomps; compno++) {
			fprintf(out_stream, "%s\t component %d {\n", tab, compno);
			j2k_dump_image_comp_header(&(img_header->comps[compno]),
					dev_dump_flag, out_stream);
			fprintf(out_stream, "%s}\n", tab);
		}
	}

	fprintf(out_stream, "}\n");
}

void j2k_dump_image_comp_header( grk_image_comp  *comp_header,
		bool dev_dump_flag, FILE *out_stream) {
	char tab[3];

	if (dev_dump_flag) {
		fprintf(stdout, "[DEV] Dump an image_comp_header struct {\n");
		tab[0] = '\0';
	} else {
		tab[0] = '\t';
		tab[1] = '\t';
		tab[2] = '\0';
	}

	fprintf(out_stream, "%s dx=%d, dy=%d\n", tab, comp_header->dx,
			comp_header->dy);
	fprintf(out_stream, "%s prec=%d\n", tab, comp_header->prec);
	fprintf(out_stream, "%s sgnd=%d\n", tab, comp_header->sgnd);

	if (dev_dump_flag)
		fprintf(out_stream, "}\n");
}

 grk_codestream_info_v2  *  j2k_get_cstr_info(grk_j2k *p_j2k) {
	uint32_t compno;
	uint32_t numcomps = p_j2k->m_private_image->numcomps;
	grk_tcp *default_tile;
	 grk_codestream_info_v2  *cstr_info =
			( grk_codestream_info_v2  * ) grk_calloc(1,
					sizeof( grk_codestream_info_v2) );
	if (!cstr_info)
		return nullptr;

	cstr_info->nbcomps = p_j2k->m_private_image->numcomps;

	cstr_info->tx0 = p_j2k->m_cp.tx0;
	cstr_info->ty0 = p_j2k->m_cp.ty0;
	cstr_info->t_width = p_j2k->m_cp.t_width;
	cstr_info->t_height = p_j2k->m_cp.t_height;
	cstr_info->t_grid_width = p_j2k->m_cp.t_grid_width;
	cstr_info->t_grid_height = p_j2k->m_cp.t_grid_height;

	cstr_info->tile_info = nullptr; /* Not fill from the main header*/

 default_tile = p_j2k->m_specific_param.m_decoder.m_default_tcp;

	cstr_info->m_default_tile_info.csty = default_tile->csty;
	cstr_info->m_default_tile_info.prg = default_tile->prg;
	cstr_info->m_default_tile_info.numlayers = default_tile->numlayers;
	cstr_info->m_default_tile_info.mct = default_tile->mct;

	cstr_info->m_default_tile_info.tccp_info = ( grk_tccp_info  * ) grk_calloc(
			cstr_info->nbcomps, sizeof( grk_tccp_info) );
	if (!cstr_info->m_default_tile_info.tccp_info) {
		grk_destroy_cstr_info(&cstr_info);
		return nullptr;
	}

	for (compno = 0; compno < numcomps; compno++) {
		grk_tccp *tccp = &(default_tile->tccps[compno]);
		 grk_tccp_info  *tccp_info =
				&(cstr_info->m_default_tile_info.tccp_info[compno]);
		uint32_t bandno, numbands;

		/* coding style*/
	 tccp_info->csty = tccp->csty;
	 tccp_info->numresolutions = tccp->numresolutions;
	 tccp_info->cblkw = tccp->cblkw;
	 tccp_info->cblkh = tccp->cblkh;
	 tccp_info->cblk_sty = tccp->cblk_sty;
	 tccp_info->qmfbid = tccp->qmfbid;
		if (tccp->numresolutions < GRK_J2K_MAXRLVLS) {
			memcpy(tccp_info->prch, tccp->prch, tccp->numresolutions);
			memcpy(tccp_info->prcw, tccp->prcw, tccp->numresolutions);
		}

		/* quantization style*/
	 tccp_info->qntsty = tccp->qntsty;
	 tccp_info->numgbits = tccp->numgbits;

		numbands =
				(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ?
						1 : (tccp->numresolutions * 3 - 2);
		if (numbands < GRK_J2K_MAXBANDS) {
			for (bandno = 0; bandno < numbands; bandno++) {
			 tccp_info->stepsizes_mant[bandno] =
					 tccp->stepsizes[bandno].mant;
			 tccp_info->stepsizes_expn[bandno] =
					 tccp->stepsizes[bandno].expn;
			}
		}

		/* RGN value*/
	 tccp_info->roishift = tccp->roishift;
	}

	return cstr_info;
}

 grk_codestream_index  *  j2k_get_cstr_index(grk_j2k *p_j2k) {
	 grk_codestream_index  *cstr_index =
			( grk_codestream_index  * ) grk_calloc(1,
					sizeof( grk_codestream_index) );
	if (!cstr_index)
		return nullptr;

 cstr_index->main_head_start = p_j2k->cstr_index->main_head_start;
 cstr_index->main_head_end = p_j2k->cstr_index->main_head_end;
 cstr_index->codestream_size = p_j2k->cstr_index->codestream_size;

 cstr_index->marknum = p_j2k->cstr_index->marknum;
 cstr_index->marker = ( grk_marker_info  * ) grk_malloc(
		 cstr_index->marknum * sizeof( grk_marker_info) );
	if (!cstr_index->marker) {
		grok_free(cstr_index);
		return nullptr;
	}

	if (p_j2k->cstr_index->marker)
		memcpy(cstr_index->marker, p_j2k->cstr_index->marker,
			 cstr_index->marknum * sizeof( grk_marker_info) );
	else {
		grok_free(cstr_index->marker);
	 cstr_index->marker = nullptr;
	}

 cstr_index->nb_of_tiles = p_j2k->cstr_index->nb_of_tiles;
 cstr_index->tile_index = ( grk_tile_index  * ) grk_calloc(
		 cstr_index->nb_of_tiles, sizeof( grk_tile_index) );
	if (!cstr_index->tile_index) {
		grok_free(cstr_index->marker);
		grok_free(cstr_index);
		return nullptr;
	}

	if (!p_j2k->cstr_index->tile_index) {
		grok_free(cstr_index->tile_index);
	 cstr_index->tile_index = nullptr;
	} else {
		uint32_t it_tile = 0;
		for (it_tile = 0; it_tile < cstr_index->nb_of_tiles; it_tile++) {

			/* Tile Marker*/
		 cstr_index->tile_index[it_tile].marknum =
					p_j2k->cstr_index->tile_index[it_tile].marknum;

		 cstr_index->tile_index[it_tile].marker =
					( grk_marker_info  * ) grk_malloc(
						 cstr_index->tile_index[it_tile].marknum
									* sizeof( grk_marker_info) );

			if (!cstr_index->tile_index[it_tile].marker) {
				uint32_t it_tile_free;

				for (it_tile_free = 0; it_tile_free < it_tile; it_tile_free++) {
					grok_free(cstr_index->tile_index[it_tile_free].marker);
				}

				grok_free(cstr_index->tile_index);
				grok_free(cstr_index->marker);
				grok_free(cstr_index);
				return nullptr;
			}

			if (p_j2k->cstr_index->tile_index[it_tile].marker)
				memcpy(cstr_index->tile_index[it_tile].marker,
						p_j2k->cstr_index->tile_index[it_tile].marker,
					 cstr_index->tile_index[it_tile].marknum
								* sizeof( grk_marker_info) );
			else {
				grok_free(cstr_index->tile_index[it_tile].marker);
			 cstr_index->tile_index[it_tile].marker = nullptr;
			}

			/* Tile part index*/
		 cstr_index->tile_index[it_tile].nb_tps =
					p_j2k->cstr_index->tile_index[it_tile].nb_tps;

		 cstr_index->tile_index[it_tile].tp_index =
					( grk_tp_index  * ) grk_malloc(
						 cstr_index->tile_index[it_tile].nb_tps
									* sizeof( grk_tp_index) );

			if (!cstr_index->tile_index[it_tile].tp_index) {
				uint32_t it_tile_free;

				for (it_tile_free = 0; it_tile_free < it_tile; it_tile_free++) {
					grok_free(cstr_index->tile_index[it_tile_free].marker);
					grok_free(cstr_index->tile_index[it_tile_free].tp_index);
				}

				grok_free(cstr_index->tile_index);
				grok_free(cstr_index->marker);
				grok_free(cstr_index);
				return nullptr;
			}

			if (p_j2k->cstr_index->tile_index[it_tile].tp_index) {
				memcpy(cstr_index->tile_index[it_tile].tp_index,
						p_j2k->cstr_index->tile_index[it_tile].tp_index,
					 cstr_index->tile_index[it_tile].nb_tps
								* sizeof( grk_tp_index) );
			} else {
				grok_free(cstr_index->tile_index[it_tile].tp_index);
			 cstr_index->tile_index[it_tile].tp_index = nullptr;
			}

			/* Packet index (NOT USED)*/
		 cstr_index->tile_index[it_tile].nb_packet = 0;
		 cstr_index->tile_index[it_tile].packet_index = nullptr;

		}
	}
	return cstr_index;
}

bool j2k_allocate_tile_element_cstr_index(grk_j2k *p_j2k) {
	uint32_t it_tile = 0;

	p_j2k->cstr_index->nb_of_tiles = p_j2k->m_cp.t_grid_width * p_j2k->m_cp.t_grid_height;
	p_j2k->cstr_index->tile_index = ( grk_tile_index  * ) grk_calloc(
			p_j2k->cstr_index->nb_of_tiles, sizeof( grk_tile_index) );
	if (!p_j2k->cstr_index->tile_index)
		return false;

	for (it_tile = 0; it_tile < p_j2k->cstr_index->nb_of_tiles; it_tile++) {
		p_j2k->cstr_index->tile_index[it_tile].maxmarknum = 100;
		p_j2k->cstr_index->tile_index[it_tile].marknum = 0;
		p_j2k->cstr_index->tile_index[it_tile].marker =
				( grk_marker_info  * ) grk_calloc(p_j2k->cstr_index->tile_index[it_tile].maxmarknum,
						sizeof( grk_marker_info) );
		if (!p_j2k->cstr_index->tile_index[it_tile].marker)
			return false;
	}
	return true;
}


grk_codestream_index  *  j2k_create_cstr_index(void) {
	 grk_codestream_index  *cstr_index = ( grk_codestream_index  * ) grk_calloc(
			1, sizeof( grk_codestream_index) );
	if (!cstr_index)
		return nullptr;

	cstr_index->maxmarknum = 100;
	cstr_index->marknum = 0;
	cstr_index->marker = ( grk_marker_info  * ) grk_calloc(
			cstr_index->maxmarknum, sizeof( grk_marker_info) );
	if (!cstr_index->marker) {
		grok_free(cstr_index);
		return nullptr;
	}

	cstr_index->tile_index = nullptr;

	return cstr_index;
}

void j2k_destroy_cstr_index( grk_codestream_index  *p_cstr_ind) {
	if (p_cstr_ind) {

		if (p_cstr_ind->marker) {
			grok_free(p_cstr_ind->marker);
			p_cstr_ind->marker = nullptr;
		}

		if (p_cstr_ind->tile_index) {
			uint32_t it_tile = 0;

			for (it_tile = 0; it_tile < p_cstr_ind->nb_of_tiles; it_tile++) {

				if (p_cstr_ind->tile_index[it_tile].packet_index) {
					grok_free(p_cstr_ind->tile_index[it_tile].packet_index);
					p_cstr_ind->tile_index[it_tile].packet_index = nullptr;
				}

				if (p_cstr_ind->tile_index[it_tile].tp_index) {
					grok_free(p_cstr_ind->tile_index[it_tile].tp_index);
					p_cstr_ind->tile_index[it_tile].tp_index = nullptr;
				}

				if (p_cstr_ind->tile_index[it_tile].marker) {
					grok_free(p_cstr_ind->tile_index[it_tile].marker);
					p_cstr_ind->tile_index[it_tile].marker = nullptr;

				}
			}

			grok_free(p_cstr_ind->tile_index);
			p_cstr_ind->tile_index = nullptr;
		}

		grok_free(p_cstr_ind);
	}
}


}
