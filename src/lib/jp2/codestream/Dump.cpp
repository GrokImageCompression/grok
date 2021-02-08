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

namespace grk {

static void j2k_dump_MH_info(CodeStream *codeStream, FILE *out_stream);

static void j2k_dump_MH_index(CodeStream *codeStream, FILE *out_stream);

static void j2k_dump_tile_info(TileCodingParams *default_tile,
		uint32_t numcomps, FILE *out_stream) {
	if (default_tile) {
		uint32_t compno;

		fprintf(out_stream, "\t default tile {\n");
		fprintf(out_stream, "\t\t csty=%#x\n", default_tile->csty);
		fprintf(out_stream, "\t\t prg=%#x\n", default_tile->prg);
		fprintf(out_stream, "\t\t numlayers=%d\n", default_tile->numlayers);
		fprintf(out_stream, "\t\t mct=%x\n", default_tile->mct);

		for (compno = 0; compno < numcomps; compno++) {
			auto tccp = &(default_tile->tccps[compno]);
			uint32_t resno;
			uint32_t bandIndex, numBandWindows;

			assert(tccp->numresolutions > 0);

			/* coding style*/
			fprintf(out_stream, "\t\t comp %u {\n", compno);
			fprintf(out_stream, "\t\t\t csty=%#x\n", tccp->csty);
			fprintf(out_stream, "\t\t\t numresolutions=%d\n",
					tccp->numresolutions);
			fprintf(out_stream, "\t\t\t cblkw=2^%d\n", tccp->cblkw);
			fprintf(out_stream, "\t\t\t cblkh=2^%d\n", tccp->cblkh);
			fprintf(out_stream, "\t\t\t cblksty=%#x\n", tccp->cblk_sty);
			fprintf(out_stream, "\t\t\t qmfbid=%d\n", tccp->qmfbid);

			fprintf(out_stream, "\t\t\t preccintsize (w,h)=");
			for (resno = 0; resno < tccp->numresolutions; resno++) {
				fprintf(out_stream, "(%d,%d) ", tccp->prcw_exp[resno],
						tccp->prch_exp[resno]);
			}
			fprintf(out_stream, "\n");

			/* quantization style*/
			fprintf(out_stream, "\t\t\t qntsty=%d\n", tccp->qntsty);
			fprintf(out_stream, "\t\t\t numgbits=%d\n", tccp->numgbits);
			fprintf(out_stream, "\t\t\t stepsizes (m,e)=");
			numBandWindows =
					(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ?
							1 : (uint32_t) (tccp->numresolutions * 3 - 2);
			for (bandIndex = 0; bandIndex < numBandWindows; bandIndex++) {
				fprintf(out_stream, "(%d,%d) ", tccp->stepsizes[bandIndex].mant,
						tccp->stepsizes[bandIndex].expn);
			}
			fprintf(out_stream, "\n");

			/* RGN value*/
			fprintf(out_stream, "\t\t\t roishift=%d\n", tccp->roishift);

			fprintf(out_stream, "\t\t }\n");
		} /*end of component of default tile*/
		fprintf(out_stream, "\t }\n"); /*end of default tile*/
	}
}

void j2k_dump(CodeStream *codeStream, uint32_t flag, FILE *out_stream) {
	/* Check if the flag is compatible with j2k file*/
	if ((flag & GRK_JP2_INFO) || (flag & GRK_JP2_IND)) {
		fprintf(out_stream, "Wrong flag\n");
		return;
	}

	/* Dump the image_header */
	if (flag & GRK_IMG_INFO) {
		if (codeStream->getHeaderImage())
			j2k_dump_image_header(codeStream->getHeaderImage(), 0, out_stream);
	}

	/* Dump the code stream info from main header */
	if (flag & GRK_J2K_MH_INFO) {
		if (codeStream->getHeaderImage())
			j2k_dump_MH_info(codeStream, out_stream);
	}
	/* Dump all tile/code stream info */
	if (flag & GRK_J2K_TCH_INFO) {
		uint32_t nb_tiles = codeStream->m_cp.t_grid_height
				* codeStream->m_cp.t_grid_width;
		if (codeStream->getHeaderImage()) {
			for (uint32_t i = 0; i < nb_tiles; ++i) {
				auto tcp = codeStream->m_cp.tcps + i;
				j2k_dump_tile_info(tcp, codeStream->getHeaderImage()->numcomps,
						out_stream);
			}
		}
	}

	/* Dump the code stream info of the current tile */
	if (flag & GRK_J2K_TH_INFO) {

	}

	/* Dump the code stream index from main header */
	if (flag & GRK_J2K_MH_IND)
		j2k_dump_MH_index(codeStream, out_stream);

	/* Dump the code stream index of the current tile */
	if (flag & GRK_J2K_TH_IND) {

	}

}

static void j2k_dump_MH_index(CodeStream *codeStream, FILE *out_stream) {
	auto cstr_index = codeStream->cstr_index;

	fprintf(out_stream, "Codestream index from main header: {\n");

	std::stringstream ss;
	ss << "\t Main header start position=" << cstr_index->main_head_start
			<< std::endl << "\t Main header end position="
			<< cstr_index->main_head_end << std::endl;

	fprintf(out_stream, "%s", ss.str().c_str());
	fprintf(out_stream, "\t Marker list: {\n");

	if (cstr_index->marker) {
		for (uint32_t it_marker = 0; it_marker < cstr_index->marknum; it_marker++) {
			fprintf(out_stream, "\t\t type=%#x, pos=%" PRIu64", len=%d\n",
					cstr_index->marker[it_marker].id,
					cstr_index->marker[it_marker].pos,
					cstr_index->marker[it_marker].len);

		}
	}

	fprintf(out_stream, "\t }\n");

	if (cstr_index->tile_index) {

		/* Simple test to avoid to write empty information*/
		uint32_t acc_nb_of_tile_part = 0;
		for (uint32_t i = 0; i < cstr_index->nb_of_tiles; i++)
			acc_nb_of_tile_part += cstr_index->tile_index[i].nb_tps;

		if (acc_nb_of_tile_part) {
			fprintf(out_stream, "\t Tile index: {\n");

			for (uint32_t i = 0; i < cstr_index->nb_of_tiles; i++) {
				uint32_t nb_of_tile_part =
						cstr_index->tile_index[i].nb_tps;

				fprintf(out_stream, "\t\t nb of tile-part in tile [%u]=%u\n",
						i, nb_of_tile_part);

				if (cstr_index->tile_index[i].tp_index) {
					for (uint32_t it_tile_part = 0; it_tile_part < nb_of_tile_part;
							it_tile_part++) {
						ss.clear();
						ss << "\t\t\t tile-part[" << it_tile_part << "]:"
								<< " star_pos="
								<< cstr_index->tile_index[i].tp_index[it_tile_part].start_pos
								<< "," << " end_header="
								<< cstr_index->tile_index[i].tp_index[it_tile_part].end_header
								<< "," << " end_pos="
								<< cstr_index->tile_index[i].tp_index[it_tile_part].end_pos
								<< std::endl;
						fprintf(out_stream, "%s", ss.str().c_str());
					}
				}

				if (cstr_index->tile_index[i].marker) {
					for (uint32_t it_marker = 0;
							it_marker < cstr_index->tile_index[i].marknum;
							it_marker++) {
						ss.clear();
						ss << "\t\t type="
								<< cstr_index->tile_index[i].marker[it_marker].id
								<< "," << " pos="
								<< cstr_index->tile_index[i].marker[it_marker].pos
								<< "," << " len="
								<< cstr_index->tile_index[i].marker[it_marker].len
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

static void j2k_dump_MH_info(CodeStream *codeStream, FILE *out_stream) {
	fprintf(out_stream, "Codestream info from main header: {\n");

	fprintf(out_stream, "\t tx0=%d, ty0=%d\n", codeStream->m_cp.tx0,
			codeStream->m_cp.ty0);
	fprintf(out_stream, "\t tdx=%d, tdy=%d\n", codeStream->m_cp.t_width,
			codeStream->m_cp.t_height);
	fprintf(out_stream, "\t tw=%d, th=%d\n", codeStream->m_cp.t_grid_width,
			codeStream->m_cp.t_grid_height);
	j2k_dump_tile_info(codeStream->m_decompressor.m_default_tcp,
			codeStream->getHeaderImage()->numcomps, out_stream);
	fprintf(out_stream, "}\n");
}

void j2k_dump_image_header(GrkImage *img_header, bool dev_dump_flag,
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
			fprintf(out_stream, "%s\t component %u {\n", tab, compno);
			j2k_dump_image_comp_header(&(img_header->comps[compno]),
					dev_dump_flag, out_stream);
			fprintf(out_stream, "%s}\n", tab);
		}
	}

	fprintf(out_stream, "}\n");
}

void j2k_dump_image_comp_header(grk_image_comp *comp_header, bool dev_dump_flag,
		FILE *out_stream) {
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
	fprintf(out_stream, "%s sgnd=%d\n", tab, comp_header->sgnd ? 1 : 0);

	if (dev_dump_flag)
		fprintf(out_stream, "}\n");
}

bool j2k_allocate_tile_element_cstr_index(CodeStream *codeStream) {
	if (!codeStream->cstr_index->tile_index){
		codeStream->cstr_index->nb_of_tiles = codeStream->m_cp.t_grid_width	* codeStream->m_cp.t_grid_height;
		codeStream->cstr_index->tile_index = (grk_tile_index*) grk_calloc(codeStream->cstr_index->nb_of_tiles, sizeof(grk_tile_index));
		if (!codeStream->cstr_index->tile_index)
			return false;

		for (uint32_t i = 0; i < codeStream->cstr_index->nb_of_tiles;i++) {
			codeStream->cstr_index->tile_index[i].maxmarknum = 100;
			codeStream->cstr_index->tile_index[i].marknum = 0;
			codeStream->cstr_index->tile_index[i].marker =	(grk_marker_info*) grk_calloc(codeStream->cstr_index->tile_index[i].maxmarknum,	sizeof(grk_marker_info));
			if (!codeStream->cstr_index->tile_index[i].marker)
				return false;
		}
	}
	return true;
}

grk_codestream_index* j2k_create_cstr_index(void) {
	auto cstr_index = (grk_codestream_index*) grk_calloc(1,	sizeof(grk_codestream_index));
	if (!cstr_index)
		return nullptr;

	cstr_index->maxmarknum = 100;
	cstr_index->marknum = 0;
	cstr_index->marker = (grk_marker_info*) grk_calloc(cstr_index->maxmarknum,
			sizeof(grk_marker_info));
	if (!cstr_index->marker) {
		grk_free(cstr_index);
		return nullptr;
	}
	cstr_index->tile_index = nullptr;

	return cstr_index;
}

void j2k_destroy_cstr_index(grk_codestream_index *p_cstr_ind) {
	if (p_cstr_ind) {
		grk_free(p_cstr_ind->marker);
		if (p_cstr_ind->tile_index) {
			for (uint32_t i = 0; i < p_cstr_ind->nb_of_tiles; i++) {
				grk_free(p_cstr_ind->tile_index[i].tp_index);
				grk_free(p_cstr_ind->tile_index[i].marker);
			}
			grk_free(p_cstr_ind->tile_index);
		}
		grk_free(p_cstr_ind);
	}
}

void jp2_dump(FileFormat *fileFormat, uint32_t flag, FILE *out_stream) {
	assert(fileFormat != nullptr);
	j2k_dump(fileFormat->codeStream, flag, out_stream);
}

}
