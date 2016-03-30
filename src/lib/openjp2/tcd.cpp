/*
*    Copyright (C) 2016 Grok Image Compression Inc.
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
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
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

#include "opj_includes.h"
#include "T1Decoder.h"
#ifdef _OPENMP
#include <omp.h>
#endif


/* ----------------------------------------------------------------------- */

/* TODO MSD: */
#ifdef TODO_MSD
void tcd_dump(FILE *fd, opj_tcd_t *tcd, opj_tcd_image_t * img)
{
    int tileno, compno, resno, bandno, precno;/*, cblkno;*/

    fprintf(fd, "image {\n");
    fprintf(fd, "  tw=%d, th=%d x0=%d x1=%d y0=%d y1=%d\n",
            img->tw, img->th, tcd->image->x0, tcd->image->x1, tcd->image->y0, tcd->image->y1);

    for (tileno = 0; tileno < img->th * img->tw; tileno++) {
        opj_tcd_tile_t *tile = &tcd->tile[tileno];
        fprintf(fd, "  tile {\n");
        fprintf(fd, "    x0=%d, y0=%d, x1=%d, y1=%d, numcomps=%d\n",
                tile->x0, tile->y0, tile->x1, tile->y1, tile->numcomps);
        for (compno = 0; compno < tile->numcomps; compno++) {
            opj_tcd_tilecomp_t *tilec = &tile->comps[compno];
            fprintf(fd, "    tilec {\n");
            fprintf(fd,
                    "      x0=%d, y0=%d, x1=%d, y1=%d, numresolutions=%d\n",
                    tilec->x0, tilec->y0, tilec->x1, tilec->y1, tilec->numresolutions);
            for (resno = 0; resno < tilec->numresolutions; resno++) {
                opj_tcd_resolution_t *res = &tilec->resolutions[resno];
                fprintf(fd, "\n   res {\n");
                fprintf(fd,
                        "          x0=%d, y0=%d, x1=%d, y1=%d, pw=%d, ph=%d, numbands=%d\n",
                        res->x0, res->y0, res->x1, res->y1, res->pw, res->ph, res->numbands);
                for (bandno = 0; bandno < res->numbands; bandno++) {
                    opj_tcd_band_t *band = &res->bands[bandno];
                    fprintf(fd, "        band {\n");
                    fprintf(fd,
                            "          x0=%d, y0=%d, x1=%d, y1=%d, stepsize=%f, numbps=%d\n",
                            band->x0, band->y0, band->x1, band->y1, band->stepsize, band->numbps);
                    for (precno = 0; precno < res->pw * res->ph; precno++) {
                        opj_tcd_precinct_t *prec = &band->precincts[precno];
                        fprintf(fd, "          prec {\n");
                        fprintf(fd,
                                "            x0=%d, y0=%d, x1=%d, y1=%d, cw=%d, ch=%d\n",
                                prec->x0, prec->y0, prec->x1, prec->y1, prec->cw, prec->ch);
                        /*
                        for (cblkno = 0; cblkno < prec->cw * prec->ch; cblkno++) {
                                opj_tcd_cblk_t *cblk = &prec->cblks[cblkno];
                                fprintf(fd, "            cblk {\n");
                                fprintf(fd,
                                        "              x0=%d, y0=%d, x1=%d, y1=%d\n",
                                        cblk->x0, cblk->y0, cblk->x1, cblk->y1);
                                fprintf(fd, "            }\n");
                        }
                        */
                        fprintf(fd, "          }\n");
                    }
                    fprintf(fd, "        }\n");
                }
                fprintf(fd, "      }\n");
            }
            fprintf(fd, "    }\n");
        }
        fprintf(fd, "  }\n");
    }
    fprintf(fd, "}\n");
}
#endif

/**
 * Initializes tile coding/decoding
 */
static inline bool opj_tcd_init_tile(opj_tcd_t *p_tcd,
                                     uint32_t p_tile_no,
                                     opj_image_t* output_image,
                                     bool isEncoder,
                                     float fraction,
                                     size_t sizeof_block,
                                     opj_event_mgr_t* manager);

/**
* Allocates memory for a decoding code block (but not data)
*/
static bool opj_tcd_code_block_dec_allocate (opj_tcd_cblk_dec_t * p_code_block);

/**
 * Deallocates the decoding data of the given precinct.
 */
static void opj_tcd_code_block_dec_deallocate (opj_tcd_precinct_t * p_precinct);

/**
 * Allocates memory for an encoding code block (but not data).
 */
static bool opj_tcd_code_block_enc_allocate (opj_tcd_cblk_enc_t * p_code_block);

/**
 * Allocates data for an encoding code block
 */
static bool opj_tcd_code_block_enc_allocate_data (opj_tcd_cblk_enc_t * p_code_block);

/**
 * Deallocates the encoding data of the given precinct.
 */
static void opj_tcd_code_block_enc_deallocate (opj_tcd_precinct_t * p_precinct);


/**
Free the memory allocated for encoding
@param tcd TCD handle
*/
static void opj_tcd_free_tile(opj_tcd_t *tcd);


static bool opj_tcd_t2_decode ( opj_tcd_t *p_tcd,
								uint32_t p_tile_no,
                                opj_seg_buf_t* src_buf,
                                uint32_t * p_data_read,
                                opj_event_mgr_t *p_manager);

static bool opj_tcd_t1_decode (opj_tcd_t *p_tcd, opj_event_mgr_t *p_manager);

static bool opj_tcd_dwt_decode (opj_tcd_t *p_tcd);

static bool opj_tcd_mct_decode (opj_tcd_t *p_tcd, opj_event_mgr_t *p_manager);

static bool opj_tcd_dc_level_shift_decode (opj_tcd_t *p_tcd);


static bool opj_tcd_dc_level_shift_encode ( opj_tcd_t *p_tcd );

static bool opj_tcd_mct_encode ( opj_tcd_t *p_tcd );

static bool opj_tcd_dwt_encode ( opj_tcd_t *p_tcd );

static bool opj_tcd_t1_encode ( opj_tcd_t *p_tcd );

static bool opj_tcd_t2_encode (     opj_tcd_t *p_tcd,
                                    uint8_t * p_dest_data,
                                    uint32_t * p_data_written,
                                    uint32_t p_max_dest_size,
                                    opj_codestream_info_t *p_cstr_info );

static bool opj_tcd_rate_allocate_encode(   opj_tcd_t *p_tcd,
        uint32_t p_max_dest_size,
        opj_codestream_info_t *p_cstr_info );

/* ----------------------------------------------------------------------- */

/**
Create a new TCD handle
*/
opj_tcd_t* opj_tcd_create(bool p_is_decoder)
{
    opj_tcd_t *l_tcd = 00;

    /* create the tcd structure */
    l_tcd = (opj_tcd_t*) opj_calloc(1,sizeof(opj_tcd_t));
    if (!l_tcd) {
        return 00;
    }

    l_tcd->m_is_decoder = p_is_decoder ? 1 : 0;

    return l_tcd;
}


/* ----------------------------------------------------------------------- */

void opj_tcd_rateallocate_fixed(opj_tcd_t *tcd)
{
    uint32_t layno;

    for (layno = 0; layno < tcd->tcp->numlayers; layno++) {
        opj_tcd_makelayer_fixed(tcd, layno, 1);
    }
}


void opj_tcd_makelayer( opj_tcd_t *tcd,
                        uint32_t layno,
                        double thresh,
                        uint32_t final)
{
    uint32_t compno, resno, bandno, precno, cblkno;
    uint32_t passno;

    opj_tcd_tile_t *tcd_tile = tcd->tile;

    tcd_tile->distolayer[layno] = 0;        /* fixed_quality */

    for (compno = 0; compno < tcd_tile->numcomps; compno++) {
        opj_tcd_tilecomp_t *tilec = tcd_tile->comps+compno;

        for (resno = 0; resno < tilec->numresolutions; resno++) {
            opj_tcd_resolution_t *res = tilec->resolutions+resno;

            for (bandno = 0; bandno < res->numbands; bandno++) {
                opj_tcd_band_t *band = res->bands+bandno;

                for (precno = 0; precno < res->pw * res->ph; precno++) {
                    opj_tcd_precinct_t *prc = band->precincts+precno;

                    for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
                        opj_tcd_cblk_enc_t *cblk = prc->cblks.enc+cblkno;
                        opj_tcd_layer_t *layer = cblk->layers+layno;
                        uint32_t n;

                        if (layno == 0) {
                            cblk->numpassesinlayers = 0;
                        }

                        n = cblk->numpassesinlayers;

                        for (passno = cblk->numpassesinlayers; passno < cblk->totalpasses; passno++) {
                            uint32_t dr;
                            double dd;
                            opj_tcd_pass_t *pass = &cblk->passes[passno];

                            if (n == 0) {
                                dr = pass->rate;
                                dd = pass->distortiondec;
                            } else {
                                dr = pass->rate - cblk->passes[n - 1].rate;
                                dd = pass->distortiondec - cblk->passes[n - 1].distortiondec;
                            }

                            if (!dr) {
                                if (dd != 0)
                                    n = passno + 1;
                                continue;
                            }
                            if (thresh - (dd / dr) < DBL_EPSILON) /* do not rely on float equality, check with DBL_EPSILON margin */
                                n = passno + 1;
                        }

                        layer->numpasses = n - cblk->numpassesinlayers;

                        if (!layer->numpasses) {
                            layer->disto = 0;
                            continue;
                        }

                        if (cblk->numpassesinlayers == 0) {
                            layer->len = cblk->passes[n - 1].rate;
                            layer->data = cblk->data;
                            layer->disto = cblk->passes[n - 1].distortiondec;
                        } else {
                            layer->len = cblk->passes[n - 1].rate - cblk->passes[cblk->numpassesinlayers - 1].rate;
                            layer->data = cblk->data + cblk->passes[cblk->numpassesinlayers - 1].rate;
                            layer->disto = cblk->passes[n - 1].distortiondec - cblk->passes[cblk->numpassesinlayers - 1].distortiondec;
                        }

                        tcd_tile->distolayer[layno] += layer->disto;    /* fixed_quality */

                        if (final)
                            cblk->numpassesinlayers = n;
                    }
                }
            }
        }
    }
}

void opj_tcd_makelayer_fixed(opj_tcd_t *tcd, uint32_t layno, uint32_t final)
{
    uint32_t compno, resno, bandno, precno, cblkno;
    int32_t value;                        /*, matrice[tcd_tcp->numlayers][tcd_tile->comps[0].numresolutions][3]; */
    int32_t matrice[10][10][3];
    uint32_t i, j, k;

    opj_cp_t *cp = tcd->cp;
    opj_tcd_tile_t *tcd_tile = tcd->tile;
    opj_tcp_t *tcd_tcp = tcd->tcp;

    for (compno = 0; compno < tcd_tile->numcomps; compno++) {
        opj_tcd_tilecomp_t *tilec = &tcd_tile->comps[compno];

        for (i = 0; i < tcd_tcp->numlayers; i++) {
            for (j = 0; j < tilec->numresolutions; j++) {
                for (k = 0; k < 3; k++) {
                    matrice[i][j][k] =
                        (int32_t) ((float)cp->m_specific_param.m_enc.m_matrice[i * tilec->numresolutions * 3 + j * 3 + k]
                                   * (float) (tcd->image->comps[compno].prec / 16.0));
                }
            }
        }

        for (resno = 0; resno < tilec->numresolutions; resno++) {
            opj_tcd_resolution_t *res = &tilec->resolutions[resno];

            for (bandno = 0; bandno < res->numbands; bandno++) {
                opj_tcd_band_t *band = &res->bands[bandno];

                for (precno = 0; precno < res->pw * res->ph; precno++) {
                    opj_tcd_precinct_t *prc = &band->precincts[precno];

                    for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
                        opj_tcd_cblk_enc_t *cblk = &prc->cblks.enc[cblkno];
                        opj_tcd_layer_t *layer = &cblk->layers[layno];
                        uint32_t n;
                        int32_t imsb = (int32_t)(tcd->image->comps[compno].prec - cblk->numbps); /* number of bit-plan equal to zero */

                        /* Correction of the matrix of coefficient to include the IMSB information */
                        if (layno == 0) {
                            value = matrice[layno][resno][bandno];
                            if (imsb >= value) {
                                value = 0;
                            } else {
                                value -= imsb;
                            }
                        } else {
                            value = matrice[layno][resno][bandno] - matrice[layno - 1][resno][bandno];
                            if (imsb >= matrice[layno - 1][resno][bandno]) {
                                value -= (imsb - matrice[layno - 1][resno][bandno]);
                                if (value < 0) {
                                    value = 0;
                                }
                            }
                        }

                        if (layno == 0) {
                            cblk->numpassesinlayers = 0;
                        }

                        n = cblk->numpassesinlayers;
                        if (cblk->numpassesinlayers == 0) {
                            if (value != 0) {
                                n = 3 * (uint32_t)value - 2 + cblk->numpassesinlayers;
                            } else {
                                n = cblk->numpassesinlayers;
                            }
                        } else {
                            n = 3 * (uint32_t)value + cblk->numpassesinlayers;
                        }

                        layer->numpasses = n - cblk->numpassesinlayers;

                        if (!layer->numpasses)
                            continue;

                        if (cblk->numpassesinlayers == 0) {
                            layer->len = cblk->passes[n - 1].rate;
                            layer->data = cblk->data;
                        } else {
                            layer->len = cblk->passes[n - 1].rate - cblk->passes[cblk->numpassesinlayers - 1].rate;
                            layer->data = cblk->data + cblk->passes[cblk->numpassesinlayers - 1].rate;
                        }

                        if (final)
                            cblk->numpassesinlayers = n;
                    }
                }
            }
        }
    }
}

bool opj_tcd_rateallocate(  opj_tcd_t *tcd,
                            uint32_t * p_data_written,
                            uint32_t len,
                            opj_codestream_info_t *cstr_info)
{
    uint32_t compno, resno, bandno, precno, cblkno, layno;
    uint32_t passno;
    double min, max;
    double cumdisto[100];      /* fixed_quality */
    const double K = 1;                /* 1.1; fixed_quality */
    double maxSE = 0;

    opj_cp_t *cp = tcd->cp;
    opj_tcd_tile_t *tcd_tile = tcd->tile;
    opj_tcp_t *tcd_tcp = tcd->tcp;

    min = DBL_MAX;
    max = 0;

    tcd_tile->numpix = 0;           /* fixed_quality */
	uint32_t state = opj_plugin_get_debug_state();

    for (compno = 0; compno < tcd_tile->numcomps; compno++) {
        opj_tcd_tilecomp_t *tilec = &tcd_tile->comps[compno];
        tilec->numpix = 0;

        for (resno = 0; resno < tilec->numresolutions; resno++) {
            opj_tcd_resolution_t *res = &tilec->resolutions[resno];

            for (bandno = 0; bandno < res->numbands; bandno++) {
                opj_tcd_band_t *band = &res->bands[bandno];

                for (precno = 0; precno < res->pw * res->ph; precno++) {
                    opj_tcd_precinct_t *prc = &band->precincts[precno];

                    for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
                        opj_tcd_cblk_enc_t *cblk = &prc->cblks.enc[cblkno];

						OPJ_INT32 numPix = ((cblk->x1 - cblk->x0) * (cblk->y1 - cblk->y0));
						if (!(state &OPJ_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(tcd,
								compno,
								resno,
								bandno,
								precno,
								cblkno,
								band,
								cblk,
								&numPix);
						}

                        for (passno = 0; passno < cblk->totalpasses; passno++) {
                            opj_tcd_pass_t *pass = &cblk->passes[passno];
                            int32_t dr;
                            double dd, rdslope;

                            if (passno == 0) {
                                dr = (int32_t)pass->rate;
                                dd = pass->distortiondec;
                            } else {
                                dr = (int32_t)(pass->rate - cblk->passes[passno - 1].rate);
                                dd = pass->distortiondec - cblk->passes[passno - 1].distortiondec;
                            }

                            if (dr == 0) {
                                continue;
                            }

                            rdslope = dd / dr;
                            if (rdslope < min) {
                                min = rdslope;
                            }

                            if (rdslope > max) {
                                max = rdslope;
                            }
                        } /* passno */

                        /* fixed_quality */
						tcd_tile->numpix += numPix;
						tilec->numpix += numPix;
                    } /* cbklno */
                } /* precno */
            } /* bandno */
        } /* resno */

        maxSE += (((double)(1 << tcd->image->comps[compno].prec) - 1.0)
                  * ((double)(1 << tcd->image->comps[compno].prec) -1.0))
                 * ((double)(tilec->numpix));
    } /* compno */

    /* index file */
    if(cstr_info) {
        opj_tile_info_t *tile_info = &cstr_info->tile[tcd->tcd_tileno];
        tile_info->numpix = tcd_tile->numpix;
        tile_info->distotile = tcd_tile->distotile;
        tile_info->thresh = (double *) opj_malloc(tcd_tcp->numlayers * sizeof(double));
        if (!tile_info->thresh) {
            /* FIXME event manager error callback */
            return false;
        }
    }

    for (layno = 0; layno < tcd_tcp->numlayers; layno++) {
        double lo = min;
        double hi = max;
        bool success = false;
        uint32_t maxlen = tcd_tcp->rates[layno] ? opj_uint_min(((uint32_t) ceil(tcd_tcp->rates[layno])), len) : len;
        double goodthresh = 0;
        double stable_thresh = 0;
        double old_thresh = -1;
        uint32_t i;
        double distotarget;                /* fixed_quality */

        /* fixed_quality */
        distotarget = tcd_tile->distotile - ((K * maxSE) / pow((float)10, tcd_tcp->distoratio[layno] / 10));

        /* Don't try to find an optimal threshold but rather take everything not included yet, if
          -r xx,yy,zz,0   (disto_alloc == 1 and rates == 0)
          -q xx,yy,zz,0   (fixed_quality == 1 and distoratio == 0)
          ==> possible to have some lossy layers and the last layer for sure lossless */
        if ( ((cp->m_specific_param.m_enc.m_disto_alloc==1) && (tcd_tcp->rates[layno]>0)) || ((cp->m_specific_param.m_enc.m_fixed_quality==1) && (tcd_tcp->distoratio[layno]>0))) {
            opj_t2_t*t2 = opj_t2_create(tcd->image, cp);
            double thresh = 0;

            if (t2 == 00) {
                return false;
            }

            for     (i = 0; i < 128; ++i) {
                double distoachieved = 0;  /* fixed_quality */

                thresh = (lo + hi) / 2;

                opj_tcd_makelayer(tcd, layno, thresh, 0);
                if ((fabs(old_thresh - thresh)) < 0.001)
                    break;
                old_thresh = thresh;

                if (cp->m_specific_param.m_enc.m_fixed_quality) {       /* fixed_quality */
                    if(OPJ_IS_CINEMA(cp->rsiz)) {
                        if (! opj_t2_encode_packets_thresh(t2,tcd->tcd_tileno, tcd_tile, layno + 1, p_data_written, maxlen, tcd->tp_pos)) {

                            lo = thresh;
                            continue;
                        } else {
                            distoachieved = layno == 0 ?
                                            tcd_tile->distolayer[0] : cumdisto[layno - 1] + tcd_tile->distolayer[layno];

                            if (distoachieved < distotarget) {
                                hi=thresh;
                                stable_thresh = thresh;
                                continue;
                            } else {
                                lo=thresh;
                            }
                        }
                    } else {
                        distoachieved = (layno == 0) ?
                                        tcd_tile->distolayer[0] : (cumdisto[layno - 1] + tcd_tile->distolayer[layno]);

                        if (distoachieved < distotarget) {
                            hi = thresh;
                            stable_thresh = thresh;
                            continue;
                        }
                        lo = thresh;
                    }
                } else {
                    if (! opj_t2_encode_packets_thresh(t2, tcd->tcd_tileno, tcd_tile, layno + 1, p_data_written, maxlen, tcd->tp_pos)) {
                        /* TODO: what to do with l ??? seek / tell ??? */
                        /* opj_event_msg(tcd->cinfo, EVT_INFO, "rate alloc: len=%d, max=%d\n", l, maxlen); */
                        lo = thresh;
                        continue;
                    }

                    hi = thresh;
                    stable_thresh = thresh;
                }
            }

            success = true;
            goodthresh = stable_thresh == 0? thresh : stable_thresh;

            opj_t2_destroy(t2);
        } else {
            success = true;
            goodthresh = min;
        }

        if (!success) {
            return false;
        }

        if(cstr_info) { /* Threshold for Marcela Index */
            cstr_info->tile[tcd->tcd_tileno].thresh[layno] = goodthresh;
        }

        opj_tcd_makelayer(tcd, layno, goodthresh, 1);

        /* fixed_quality */
        cumdisto[layno] = (layno == 0) ? tcd_tile->distolayer[0] : (cumdisto[layno - 1] + tcd_tile->distolayer[layno]);
    }

    return true;
}

bool opj_tcd_init( opj_tcd_t *p_tcd,
                   opj_image_t * p_image,
                   opj_cp_t * p_cp )
{
    p_tcd->image = p_image;
    p_tcd->cp = p_cp;

    p_tcd->tile = (opj_tcd_tile_t *) opj_calloc(1,sizeof(opj_tcd_tile_t));
    if (! p_tcd->tile) {
        return false;
    }

    p_tcd->tile->comps = (opj_tcd_tilecomp_t *) opj_calloc(p_image->numcomps,sizeof(opj_tcd_tilecomp_t));
    if (! p_tcd->tile->comps ) {
        return false;
    }

    p_tcd->tile->numcomps = p_image->numcomps;
    p_tcd->tp_pos = p_cp->m_specific_param.m_enc.m_tp_pos;

    return true;
}

/**
Destroy a previously created TCD handle
*/
void opj_tcd_destroy(opj_tcd_t *tcd)
{
    if (tcd) {
        opj_tcd_free_tile(tcd);
        opj_free(tcd);
    }
}

/* ----------------------------------------------------------------------- */

static inline bool opj_tcd_init_tile(opj_tcd_t *p_tcd,
                                     uint32_t p_tile_no,
                                     opj_image_t* output_image,
                                     bool isEncoder,
                                     float fraction,
                                     size_t sizeof_block,
                                     opj_event_mgr_t* manager)
{
    uint32_t (*l_gain_ptr)(uint32_t) = 00;
    uint32_t compno, resno, bandno, precno, cblkno;
    opj_tcp_t * l_tcp = 00;
    opj_cp_t * l_cp = 00;
    opj_tcd_tile_t * l_tile = 00;
    opj_tccp_t *l_tccp = 00;
    opj_tcd_tilecomp_t *l_tilec = 00;
    opj_image_comp_t * l_image_comp = 00;
    opj_tcd_resolution_t *l_res = 00;
    opj_tcd_band_t *l_band = 00;
    opj_stepsize_t * l_step_size = 00;
    opj_tcd_precinct_t *l_current_precinct = 00;
    opj_image_t *l_image = 00;
    uint32_t p,q;
    uint32_t l_level_no;
    uint32_t l_pdx, l_pdy;
    uint32_t l_gain;
    int32_t l_x0b, l_y0b;
    uint32_t l_tx0, l_ty0;
    /* extent of precincts , top left, bottom right**/
    int32_t l_tl_prc_x_start, l_tl_prc_y_start, l_br_prc_x_end, l_br_prc_y_end;
    /* number of precinct for a resolution */
    uint32_t l_nb_precincts;
    /* room needed to store l_nb_precinct precinct for a resolution */
    uint32_t l_nb_precinct_size;
    /* number of code blocks for a precinct*/
    uint32_t l_nb_code_blocks;
    /* room needed to store l_nb_code_blocks code blocks for a precinct*/
    uint32_t l_nb_code_blocks_size;

	uint32_t state = opj_plugin_get_debug_state();

    l_cp = p_tcd->cp;
    l_tcp = &(l_cp->tcps[p_tile_no]);
    l_tile = p_tcd->tile;
    l_tccp = l_tcp->tccps;
    l_tilec = l_tile->comps;
    l_image = p_tcd->image;
    l_image_comp = p_tcd->image->comps;

    opj_seg_buf_rewind(l_tcp->m_data);

    p = p_tile_no % l_cp->tw;       /* tile coordinates */
    q = p_tile_no / l_cp->tw;
    /*fprintf(stderr, "Tile coordinate = %d,%d\n", p, q);*/

    /* 4 borders of the tile rescale on the image if necessary */
    l_tx0 = l_cp->tx0 + p * l_cp->tdx; /* can't be greater than l_image->x1 so won't overflow */
    l_tile->x0 = (int32_t)opj_uint_max(l_tx0, l_image->x0);
    l_tile->x1 = (int32_t)opj_uint_min(opj_uint_adds(l_tx0, l_cp->tdx), l_image->x1);
    l_ty0 = l_cp->ty0 + q * l_cp->tdy; /* can't be greater than l_image->y1 so won't overflow */
    l_tile->y0 = (int32_t)opj_uint_max(l_ty0, l_image->y0);
    l_tile->y1 = (int32_t)opj_uint_min(opj_uint_adds(l_ty0, l_cp->tdy), l_image->y1);

    /* testcase 1888.pdf.asan.35.988 */
    if (l_tccp->numresolutions == 0) {
        opj_event_msg(manager, EVT_ERROR, "tiles require at least one resolution\n");
        return false;
    }
    /*fprintf(stderr, "Tile border = %d,%d,%d,%d\n", l_tile->x0, l_tile->y0,l_tile->x1,l_tile->y1);*/

    /*tile->numcomps = image->numcomps; */
    for (compno = 0; compno < l_tile->numcomps; ++compno) {
        uint64_t l_tile_data_size=0;
        uint32_t l_res_data_size=0;
        /*fprintf(stderr, "compno = %d/%d\n", compno, l_tile->numcomps);*/
        l_image_comp->resno_decoded = 0;
        /* border of each l_tile component (global) */
        l_tilec->x0 = opj_int_ceildiv(l_tile->x0, (int32_t)l_image_comp->dx);
        l_tilec->y0 = opj_int_ceildiv(l_tile->y0, (int32_t)l_image_comp->dy);
        l_tilec->x1 = opj_int_ceildiv(l_tile->x1, (int32_t)l_image_comp->dx);
        l_tilec->y1 = opj_int_ceildiv(l_tile->y1, (int32_t)l_image_comp->dy);
        /*fprintf(stderr, "\tTile compo border = %d,%d,%d,%d\n", l_tilec->x0, l_tilec->y0,l_tilec->x1,l_tilec->y1);*/

        /* compute l_data_size with overflow check */
        l_tile_data_size = (uint64_t)(l_tilec->x1 - l_tilec->x0) * (uint64_t)(l_tilec->y1 - l_tilec->y0) * sizeof(uint32_t);
        l_tilec->numresolutions = l_tccp->numresolutions;
        if (l_tccp->numresolutions < l_cp->m_specific_param.m_dec.m_reduce) {
            l_tilec->minimum_num_resolutions = 1;
        } else {
            l_tilec->minimum_num_resolutions = l_tccp->numresolutions - l_cp->m_specific_param.m_dec.m_reduce;
        }

        l_res_data_size = l_tilec->numresolutions * (uint32_t)sizeof(opj_tcd_resolution_t);

        if (l_tilec->resolutions == 00) {
            l_tilec->resolutions = (opj_tcd_resolution_t *) opj_malloc(l_res_data_size);
            if (! l_tilec->resolutions ) {
                return false;
            }
            /*fprintf(stderr, "\tAllocate resolutions of tilec (opj_tcd_resolution_t): %d\n",l_data_size);*/
            l_tilec->resolutions_size = l_res_data_size;
            memset(l_tilec->resolutions,0, l_res_data_size);
        } else if (l_res_data_size > l_tilec->resolutions_size) {
            opj_tcd_resolution_t* new_resolutions = (opj_tcd_resolution_t *) opj_realloc(l_tilec->resolutions, l_res_data_size);
            if (! new_resolutions) {
                opj_event_msg(manager, EVT_ERROR, "Not enough memory for tile resolutions\n");
                opj_free(l_tilec->resolutions);
                l_tilec->resolutions = NULL;
                l_tilec->resolutions_size = 0;
                return false;
            }
            l_tilec->resolutions = new_resolutions;
            /*fprintf(stderr, "\tReallocate data of tilec (int): from %d to %d x uint32_t\n", l_tilec->resolutions_size, l_data_size);*/
            memset(((uint8_t*) l_tilec->resolutions)+l_tilec->resolutions_size,0, l_res_data_size - l_tilec->resolutions_size);
            l_tilec->resolutions_size = l_res_data_size;
        }

        l_level_no = l_tilec->numresolutions - 1;
        l_res = l_tilec->resolutions;
        l_step_size = l_tccp->stepsizes;
        if (l_tccp->qmfbid == 0) {
            l_gain_ptr = &opj_dwt_getgain_real;
        } else {
            l_gain_ptr  = &opj_dwt_getgain;
        }
        /*fprintf(stderr, "\tlevel_no=%d\n",l_level_no);*/

        for (resno = 0; resno < l_tilec->numresolutions; ++resno) {
            /*fprintf(stderr, "\t\tresno = %d/%d\n", resno, l_tilec->numresolutions);*/
            int32_t tlcbgxstart, tlcbgystart /*, brcbgxend, brcbgyend*/;
            uint32_t cbgwidthexpn, cbgheightexpn;
            uint32_t cblkwidthexpn, cblkheightexpn;

            /* border for each resolution level (global) */
            l_res->x0 = opj_int_ceildivpow2(l_tilec->x0, (int32_t)l_level_no);
            l_res->y0 = opj_int_ceildivpow2(l_tilec->y0, (int32_t)l_level_no);
            l_res->x1 = opj_int_ceildivpow2(l_tilec->x1, (int32_t)l_level_no);
            l_res->y1 = opj_int_ceildivpow2(l_tilec->y1, (int32_t)l_level_no);
            /*fprintf(stderr, "\t\t\tres_x0= %d, res_y0 =%d, res_x1=%d, res_y1=%d\n", l_res->x0, l_res->y0, l_res->x1, l_res->y1);*/
            /* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
            l_pdx = l_tccp->prcw[resno];
            l_pdy = l_tccp->prch[resno];
            /*fprintf(stderr, "\t\t\tpdx=%d, pdy=%d\n", l_pdx, l_pdy);*/
            /* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
            l_tl_prc_x_start = opj_int_floordivpow2(l_res->x0, (int32_t)l_pdx) << l_pdx;
            l_tl_prc_y_start = opj_int_floordivpow2(l_res->y0, (int32_t)l_pdy) << l_pdy;
            l_br_prc_x_end = opj_int_ceildivpow2(l_res->x1, (int32_t)l_pdx) << l_pdx;
            l_br_prc_y_end = opj_int_ceildivpow2(l_res->y1, (int32_t)l_pdy) << l_pdy;
            /*fprintf(stderr, "\t\t\tprc_x_start=%d, prc_y_start=%d, br_prc_x_end=%d, br_prc_y_end=%d \n", l_tl_prc_x_start, l_tl_prc_y_start, l_br_prc_x_end ,l_br_prc_y_end );*/

            l_res->pw = (l_res->x0 == l_res->x1) ? 0 : (uint32_t)((l_br_prc_x_end - l_tl_prc_x_start) >> l_pdx);
            l_res->ph = (l_res->y0 == l_res->y1) ? 0 : (uint32_t)((l_br_prc_y_end - l_tl_prc_y_start) >> l_pdy);
            /*fprintf(stderr, "\t\t\tres_pw=%d, res_ph=%d\n", l_res->pw, l_res->ph );*/

            l_nb_precincts = l_res->pw * l_res->ph;
            l_nb_precinct_size = l_nb_precincts * (uint32_t)sizeof(opj_tcd_precinct_t);
            if (resno == 0) {
                tlcbgxstart = l_tl_prc_x_start;
                tlcbgystart = l_tl_prc_y_start;
                /*brcbgxend = l_br_prc_x_end;*/
                /* brcbgyend = l_br_prc_y_end;*/
                cbgwidthexpn = l_pdx;
                cbgheightexpn = l_pdy;
                l_res->numbands = 1;
            } else {
                tlcbgxstart = opj_int_ceildivpow2(l_tl_prc_x_start, 1);
                tlcbgystart = opj_int_ceildivpow2(l_tl_prc_y_start, 1);
                /*brcbgxend = opj_int_ceildivpow2(l_br_prc_x_end, 1);*/
                /*brcbgyend = opj_int_ceildivpow2(l_br_prc_y_end, 1);*/
                cbgwidthexpn = l_pdx - 1;
                cbgheightexpn = l_pdy - 1;
                l_res->numbands = 3;
            }

            cblkwidthexpn = opj_uint_min(l_tccp->cblkw, cbgwidthexpn);
            cblkheightexpn = opj_uint_min(l_tccp->cblkh, cbgheightexpn);
            l_band = l_res->bands;

            for (bandno = 0; bandno < l_res->numbands; ++bandno) {
                int32_t numbps;
                /*fprintf(stderr, "\t\t\tband_no=%d/%d\n", bandno, l_res->numbands );*/

                if (resno == 0) {
                    l_band->bandno = 0 ;
                    l_band->x0 = opj_int_ceildivpow2(l_tilec->x0, (int32_t)l_level_no);
                    l_band->y0 = opj_int_ceildivpow2(l_tilec->y0, (int32_t)l_level_no);
                    l_band->x1 = opj_int_ceildivpow2(l_tilec->x1, (int32_t)l_level_no);
                    l_band->y1 = opj_int_ceildivpow2(l_tilec->y1, (int32_t)l_level_no);
                } else {
                    l_band->bandno = bandno + 1;
                    /* x0b = 1 if bandno = 1 or 3 */
                    l_x0b = l_band->bandno&1;
                    /* y0b = 1 if bandno = 2 or 3 */
                    l_y0b = (int32_t)((l_band->bandno)>>1);
                    /* l_band border (global) */
                    l_band->x0 = opj_int64_ceildivpow2(l_tilec->x0 - ((int64_t)l_x0b << l_level_no), (int32_t)(l_level_no + 1));
                    l_band->y0 = opj_int64_ceildivpow2(l_tilec->y0 - ((int64_t)l_y0b << l_level_no), (int32_t)(l_level_no + 1));
                    l_band->x1 = opj_int64_ceildivpow2(l_tilec->x1 - ((int64_t)l_x0b << l_level_no), (int32_t)(l_level_no + 1));
                    l_band->y1 = opj_int64_ceildivpow2(l_tilec->y1 - ((int64_t)l_y0b << l_level_no), (int32_t)(l_level_no + 1));
                }

                /** avoid an if with storing function pointer */
                l_gain = (*l_gain_ptr) (l_band->bandno);
                numbps = (int32_t)(l_image_comp->prec + l_gain);
                l_band->stepsize = (float)(((1.0 + l_step_size->mant / 2048.0) * pow(2.0, (int32_t) (numbps - l_step_size->expn)))) * fraction;
                l_band->numbps = l_step_size->expn + (int32_t)l_tccp->numgbits - 1;      /* WHY -1 ? */

                if (!l_band->precincts && (l_nb_precincts > 0U)) {
                    l_band->precincts = (opj_tcd_precinct_t *) opj_malloc( /*3 * */ l_nb_precinct_size);
                    if (! l_band->precincts) {
                        return false;
                    }
                    /*fprintf(stderr, "\t\t\t\tAllocate precincts of a band (opj_tcd_precinct_t): %d\n",l_nb_precinct_size);     */
                    memset(l_band->precincts,0,l_nb_precinct_size);
                    l_band->precincts_data_size = l_nb_precinct_size;
                } else if (l_band->precincts_data_size < l_nb_precinct_size) {

                    opj_tcd_precinct_t * new_precincts = (opj_tcd_precinct_t *) opj_realloc(l_band->precincts,/*3 * */ l_nb_precinct_size);
                    if (! new_precincts) {
                        opj_event_msg(manager, EVT_ERROR, "Not enough memory to handle band precints\n");
                        opj_free(l_band->precincts);
                        l_band->precincts = NULL;
                        l_band->precincts_data_size = 0;
                        return false;
                    }
                    l_band->precincts = new_precincts;
                    /*fprintf(stderr, "\t\t\t\tReallocate precincts of a band (opj_tcd_precinct_t): from %d to %d\n",l_band->precincts_data_size, l_nb_precinct_size);*/
                    memset(((uint8_t *) l_band->precincts) + l_band->precincts_data_size,0,l_nb_precinct_size - l_band->precincts_data_size);
                    l_band->precincts_data_size = l_nb_precinct_size;
                }

                l_current_precinct = l_band->precincts;
                for (precno = 0; precno < l_nb_precincts; ++precno) {
                    int32_t tlcblkxstart, tlcblkystart, brcblkxend, brcblkyend;
                    int32_t cbgxstart = tlcbgxstart + (int32_t)(precno % l_res->pw) * (1 << cbgwidthexpn);
                    int32_t cbgystart = tlcbgystart + (int32_t)(precno / l_res->pw) * (1 << cbgheightexpn);
                    int32_t cbgxend = cbgxstart + (1 << cbgwidthexpn);
                    int32_t cbgyend = cbgystart + (1 << cbgheightexpn);
                    /*fprintf(stderr, "\t precno=%d; bandno=%d, resno=%d; compno=%d\n", precno, bandno , resno, compno);*/
                    /*fprintf(stderr, "\t tlcbgxstart(=%d) + (precno(=%d) percent res->pw(=%d)) * (1 << cbgwidthexpn(=%d)) \n",tlcbgxstart,precno,l_res->pw,cbgwidthexpn);*/

                    /* precinct size (global) */
                    /*fprintf(stderr, "\t cbgxstart=%d, l_band->x0 = %d \n",cbgxstart, l_band->x0);*/

                    l_current_precinct->x0 = opj_int_max(cbgxstart, l_band->x0);
                    l_current_precinct->y0 = opj_int_max(cbgystart, l_band->y0);
                    l_current_precinct->x1 = opj_int_min(cbgxend, l_band->x1);
                    l_current_precinct->y1 = opj_int_min(cbgyend, l_band->y1);
                    /*fprintf(stderr, "\t prc_x0=%d; prc_y0=%d, prc_x1=%d; prc_y1=%d\n",l_current_precinct->x0, l_current_precinct->y0 ,l_current_precinct->x1, l_current_precinct->y1);*/

                    tlcblkxstart = opj_int_floordivpow2(l_current_precinct->x0, (int32_t)cblkwidthexpn) << cblkwidthexpn;
                    /*fprintf(stderr, "\t tlcblkxstart =%d\n",tlcblkxstart );*/
                    tlcblkystart = opj_int_floordivpow2(l_current_precinct->y0, (int32_t)cblkheightexpn) << cblkheightexpn;
                    /*fprintf(stderr, "\t tlcblkystart =%d\n",tlcblkystart );*/
                    brcblkxend = opj_int_ceildivpow2(l_current_precinct->x1, (int32_t)cblkwidthexpn) << cblkwidthexpn;
                    /*fprintf(stderr, "\t brcblkxend =%d\n",brcblkxend );*/
                    brcblkyend = opj_int_ceildivpow2(l_current_precinct->y1, (int32_t)cblkheightexpn) << cblkheightexpn;
                    /*fprintf(stderr, "\t brcblkyend =%d\n",brcblkyend );*/
                    l_current_precinct->cw = (uint32_t)((brcblkxend - tlcblkxstart) >> cblkwidthexpn);
                    l_current_precinct->ch = (uint32_t)((brcblkyend - tlcblkystart) >> cblkheightexpn);

                    l_nb_code_blocks = l_current_precinct->cw * l_current_precinct->ch;
                    /*fprintf(stderr, "\t\t\t\t precinct_cw = %d x recinct_ch = %d\n",l_current_precinct->cw, l_current_precinct->ch);      */
                    l_nb_code_blocks_size = l_nb_code_blocks * (uint32_t)sizeof_block;

                    if (!l_current_precinct->cblks.blocks && (l_nb_code_blocks > 0U)) {
                        l_current_precinct->cblks.blocks = opj_malloc(l_nb_code_blocks_size);
                        if (! l_current_precinct->cblks.blocks ) {
                            return false;
                        }
                        /*fprintf(stderr, "\t\t\t\tAllocate cblks of a precinct (opj_tcd_cblk_dec_t): %d\n",l_nb_code_blocks_size);*/

                        memset(l_current_precinct->cblks.blocks,0,l_nb_code_blocks_size);

                        l_current_precinct->block_size = l_nb_code_blocks_size;
                    } else if (l_nb_code_blocks_size > l_current_precinct->block_size) {
                        void *new_blocks = opj_realloc(l_current_precinct->cblks.blocks, l_nb_code_blocks_size);
                        if (! new_blocks) {
                            opj_free(l_current_precinct->cblks.blocks);
                            l_current_precinct->cblks.blocks = NULL;
                            l_current_precinct->block_size = 0;
                            opj_event_msg(manager, EVT_ERROR, "Not enough memory for current precinct codeblock element\n");
                            return false;
                        }
                        l_current_precinct->cblks.blocks = new_blocks;
                        /*fprintf(stderr, "\t\t\t\tReallocate cblks of a precinct (opj_tcd_cblk_dec_t): from %d to %d\n",l_current_precinct->block_size, l_nb_code_blocks_size);     */

                        memset(((uint8_t *) l_current_precinct->cblks.blocks) + l_current_precinct->block_size
                               ,0
                               ,l_nb_code_blocks_size - l_current_precinct->block_size);

                        l_current_precinct->block_size = l_nb_code_blocks_size;
                    }

                    if (! l_current_precinct->incltree) {
                        l_current_precinct->incltree = opj_tgt_create(l_current_precinct->cw, l_current_precinct->ch, manager);
                    } else {
                        l_current_precinct->incltree = opj_tgt_init(l_current_precinct->incltree, l_current_precinct->cw, l_current_precinct->ch, manager);
                    }

                    if (! l_current_precinct->incltree)     {
                        opj_event_msg(manager, EVT_WARNING, "No incltree created.\n");
                        /*return false;*/
                    }

                    if (! l_current_precinct->imsbtree) {
                        l_current_precinct->imsbtree = opj_tgt_create(l_current_precinct->cw, l_current_precinct->ch, manager);
                    } else {
                        l_current_precinct->imsbtree = opj_tgt_init(l_current_precinct->imsbtree, l_current_precinct->cw, l_current_precinct->ch, manager);
                    }

                    if (! l_current_precinct->imsbtree) {
                        opj_event_msg(manager, EVT_WARNING, "No imsbtree created.\n");
                        /*return false;*/
                    }

                    for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
                        int32_t cblkxstart = tlcblkxstart + (int32_t)(cblkno % l_current_precinct->cw) * (1 << cblkwidthexpn);
                        int32_t cblkystart = tlcblkystart + (int32_t)(cblkno / l_current_precinct->cw) * (1 << cblkheightexpn);
                        int32_t cblkxend = cblkxstart + (1 << cblkwidthexpn);
                        int32_t cblkyend = cblkystart + (1 << cblkheightexpn);

                        if (isEncoder) {
                            opj_tcd_cblk_enc_t* l_code_block = l_current_precinct->cblks.enc + cblkno;

                            if (! opj_tcd_code_block_enc_allocate(l_code_block)) {
                                return false;
                            }
                            /* code-block size (global) */
                            l_code_block->x0 = opj_int_max(cblkxstart, l_current_precinct->x0);
                            l_code_block->y0 = opj_int_max(cblkystart, l_current_precinct->y0);
                            l_code_block->x1 = opj_int_min(cblkxend, l_current_precinct->x1);
                            l_code_block->y1 = opj_int_min(cblkyend, l_current_precinct->y1);

							if (!p_tcd->current_plugin_tile || (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE)) {
								if (!opj_tcd_code_block_enc_allocate_data(l_code_block)) {
									return false;
								}
							}
                        } else {
                            opj_tcd_cblk_dec_t* l_code_block = l_current_precinct->cblks.dec + cblkno;

                            if (! opj_tcd_code_block_dec_allocate(l_code_block)) {
                                return false;
                            }
                            /* code-block size (global) */
                            l_code_block->x0 = opj_int_max(cblkxstart, l_current_precinct->x0);
                            l_code_block->y0 = opj_int_max(cblkystart, l_current_precinct->y0);
                            l_code_block->x1 = opj_int_min(cblkxend, l_current_precinct->x1);
                            l_code_block->y1 = opj_int_min(cblkyend, l_current_precinct->y1);
                        }
                    }
                    ++l_current_precinct;
                } /* precno */
                ++l_band;
                ++l_step_size;
            } /* bandno */
            ++l_res;
            --l_level_no;
        } /* resno */
        if (!opj_tile_buf_create_component(l_tilec,
                                           l_tccp->qmfbid ? false : true,
                                           1 << l_tccp->cblkw,
                                           1 << l_tccp->cblkh,
                                           output_image,
                                           l_image_comp->dx,
                                           l_image_comp->dy)) {
            return false;
        }
        l_tilec->buf->data_size_needed = l_tile_data_size;

        ++l_tccp;
        ++l_tilec;
        ++l_image_comp;
    } /* compno */


	  // decoder sanity check for tile struct
	if (!isEncoder) {
		if (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) {
			if (!tile_equals(p_tcd->current_plugin_tile, l_tile)) {
				manager->warning_handler("plugin tile differs from opj tile", NULL);
			}
		}
	}



    return true;
}

bool opj_tcd_init_encode_tile (opj_tcd_t *p_tcd, uint32_t p_tile_no, opj_event_mgr_t* p_manager)
{
    return opj_tcd_init_tile(p_tcd, p_tile_no, NULL, true, 1.0F, sizeof(opj_tcd_cblk_enc_t), p_manager);
}

bool opj_tcd_init_decode_tile (opj_tcd_t *p_tcd,
                               uint32_t qmfbid,
                               opj_image_t* output_image,
                               uint32_t p_tile_no,
                               opj_event_mgr_t* p_manager)
{
    return  opj_tcd_init_tile(p_tcd,
                              p_tile_no,
                              output_image,
                              false,
                              0.5F,
                              sizeof(opj_tcd_cblk_dec_t),
                              p_manager);

}

/**
 * Allocates memory for an encoding code block (but not data memory).
 */
static bool opj_tcd_code_block_enc_allocate (opj_tcd_cblk_enc_t * p_code_block)
{
    if (! p_code_block->layers) {
        /* no memset since data */
        p_code_block->layers = (opj_tcd_layer_t*) opj_calloc(100, sizeof(opj_tcd_layer_t));
        if (! p_code_block->layers) {
            return false;
        }
    }
    if (! p_code_block->passes) {
        p_code_block->passes = (opj_tcd_pass_t*) opj_calloc(100, sizeof(opj_tcd_pass_t));
        if (! p_code_block->passes) {
            return false;
        }
    }
    return true;
}

/**
 * Allocates data memory for an encoding code block.
 */
static bool opj_tcd_code_block_enc_allocate_data (opj_tcd_cblk_enc_t * p_code_block)
{
    uint32_t l_data_size;

    l_data_size = (uint32_t)((p_code_block->x1 - p_code_block->x0) * (p_code_block->y1 - p_code_block->y0) * (int32_t)sizeof(uint32_t));

    if (l_data_size > p_code_block->data_size) {
        if (p_code_block->data) {
            opj_free(p_code_block->data - 1); /* again, why -1 */
        }
        p_code_block->data = (uint8_t*) opj_malloc(l_data_size+1);
        if(! p_code_block->data) {
            p_code_block->data_size = 0U;
            return false;
        }
        p_code_block->data_size = l_data_size;

        p_code_block->data[0] = 0;
        p_code_block->data+=1;   /*why +1 ?*/
		p_code_block->owns_data = true;
    }
    return true;
}

/**
 * Allocates memory for a decoding code block (but not data)
 */
static bool opj_tcd_code_block_dec_allocate (opj_tcd_cblk_dec_t * p_code_block)
{
    if (!p_code_block->segs) {
        p_code_block->segs = (opj_tcd_seg_t *)opj_calloc(OPJ_J2K_DEFAULT_NB_SEGS, sizeof(opj_tcd_seg_t));
        if (!p_code_block->segs) {
            return false;
        }
        /*fprintf(stderr, "Allocate %d elements of code_block->data\n", OPJ_J2K_DEFAULT_NB_SEGS * sizeof(opj_tcd_seg_t));*/

        p_code_block->m_current_max_segs = OPJ_J2K_DEFAULT_NB_SEGS;

        /*fprintf(stderr, "Allocate 8192 elements of code_block->data\n");*/
        /*fprintf(stderr, "m_current_max_segs of code_block->data = %d\n", p_code_block->m_current_max_segs);*/
    } else {
        /* sanitize */
        opj_tcd_seg_t * l_segs = p_code_block->segs;
        uint32_t l_current_max_segs = p_code_block->m_current_max_segs;

        /* Note: since seg_buffers simply holds references to another data buffer,
        we do not need to copy it  to the sanitized block  */
        opj_vec_cleanup(&p_code_block->seg_buffers);

        memset(p_code_block, 0, sizeof(opj_tcd_cblk_dec_t));
        p_code_block->segs = l_segs;
        p_code_block->m_current_max_segs = l_current_max_segs;
    }
    return true;
}
/*
Get size of tile data, summed over all components, reflecting actual precision of data.
opj_image_t always stores data in 32 bit format.
*/
uint32_t opj_tcd_get_decoded_tile_size ( opj_tcd_t *p_tcd )
{
    uint32_t i;
    uint32_t l_data_size = 0;
    opj_image_comp_t * l_img_comp = 00;
    opj_tcd_tilecomp_t * l_tile_comp = 00;
    opj_tcd_resolution_t * l_res = 00;
    uint32_t l_size_comp;

    l_tile_comp = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;

    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = (l_img_comp->prec + 7) >> 3;

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        l_res = l_tile_comp->resolutions + l_tile_comp->minimum_num_resolutions - 1;
        l_data_size += l_size_comp * (uint32_t)((l_res->x1 - l_res->x0) * (l_res->y1 - l_res->y0));
        ++l_img_comp;
        ++l_tile_comp;
    }

    return l_data_size;
}

bool opj_tcd_encode_tile(   opj_tcd_t *p_tcd,
                            uint32_t p_tile_no,
                            uint8_t *p_dest,
                            uint32_t * p_data_written,
                            uint32_t p_max_length,
                            opj_codestream_info_t *p_cstr_info)
{
	uint32_t state = opj_plugin_get_debug_state();
    if (p_tcd->cur_tp_num == 0) {

        p_tcd->tcd_tileno = p_tile_no;
        p_tcd->tcp = &p_tcd->cp->tcps[p_tile_no];

        /* INDEX >> "Precinct_nb_X et Precinct_nb_Y" */
        if(p_cstr_info)  {
            uint32_t l_num_packs = 0;
            uint32_t i;
            opj_tcd_tilecomp_t *l_tilec_idx = &p_tcd->tile->comps[0];        /* based on component 0 */
            opj_tccp_t *l_tccp = p_tcd->tcp->tccps; /* based on component 0 */

            for (i = 0; i < l_tilec_idx->numresolutions; i++) {
                opj_tcd_resolution_t *l_res_idx = &l_tilec_idx->resolutions[i];

                p_cstr_info->tile[p_tile_no].pw[i] = (int)l_res_idx->pw;
                p_cstr_info->tile[p_tile_no].ph[i] = (int)l_res_idx->ph;

                l_num_packs += l_res_idx->pw * l_res_idx->ph;
                p_cstr_info->tile[p_tile_no].pdx[i] = (int)l_tccp->prcw[i];
                p_cstr_info->tile[p_tile_no].pdy[i] = (int)l_tccp->prch[i];
            }
            p_cstr_info->tile[p_tile_no].packet = (opj_packet_info_t*) opj_calloc((size_t)p_cstr_info->numcomps * (size_t)p_cstr_info->numlayers * l_num_packs, sizeof(opj_packet_info_t));
            if (!p_cstr_info->tile[p_tile_no].packet) {
                /* FIXME event manager error callback */
                return false;
            }
        }
		/* << INDEX */
		if ((state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) &&
			!(state & OPJ_PLUGIN_STATE_CPU_ONLY)) {
			set_context_stream(p_tcd);
		}

		// When debugging the encoder, we do all of T1 up to and including DWT in the plugin, and pass this in as image data.
		// This way, both OPJ and plugin start with same inputs for context formation and MQ coding.
		bool debugEncode = state & OPJ_PLUGIN_STATE_DEBUG_ENCODE;
		bool debugMCT = (state & OPJ_PLUGIN_STATE_MCT_ONLY) ? true : false ;

		if (!p_tcd->current_plugin_tile || debugEncode) {

			if (!debugEncode) {
				/* FIXME _ProfStart(PGROUP_DC_SHIFT); */
				/*---------------TILE-------------------*/
				if (!opj_tcd_dc_level_shift_encode(p_tcd)) {
					return false;
				}
				/* FIXME _ProfStop(PGROUP_DC_SHIFT); */

				/* FIXME _ProfStart(PGROUP_MCT); */
				if (!opj_tcd_mct_encode(p_tcd)) {
					return false;
				}
				/* FIXME _ProfStop(PGROUP_MCT); */
			}

			if (!debugEncode || debugMCT) {
				/* FIXME _ProfStart(PGROUP_DWT); */
				if (!opj_tcd_dwt_encode(p_tcd)) {
					return false;
				}
				/* FIXME  _ProfStop(PGROUP_DWT); */
			}


			/* FIXME  _ProfStart(PGROUP_T1); */
			if (!opj_tcd_t1_encode(p_tcd)) {
				return false;
			}
			/* FIXME _ProfStop(PGROUP_T1); */

		}

		/* FIXME _ProfStart(PGROUP_RATE); */
		if (!opj_tcd_rate_allocate_encode(p_tcd, p_max_length, p_cstr_info)) {
			return false;
		}
		/* FIXME _ProfStop(PGROUP_RATE); */

    }
    /*--------------TIER2------------------*/

    /* INDEX */
    if (p_cstr_info) {
        p_cstr_info->index_write = 1;
    }
    /* FIXME _ProfStart(PGROUP_T2); */

    if (! opj_tcd_t2_encode(p_tcd,p_dest,p_data_written,p_max_length,p_cstr_info)) {
        return false;
    }
    /* FIXME _ProfStop(PGROUP_T2); */

    /*---------------CLEAN-------------------*/

    return true;
}

bool opj_tcd_decode_tile(   opj_tcd_t *p_tcd,
                            opj_seg_buf_t* src_buf,
                            uint32_t p_tile_no,
                            opj_event_mgr_t *p_manager
                        )
{
    uint32_t l_data_read;
    p_tcd->tcp = p_tcd->cp->tcps + p_tile_no;

    l_data_read = 0;
    if (! opj_tcd_t2_decode(p_tcd, p_tile_no, src_buf, &l_data_read,p_manager)) {
        return false;
    }

    if  (! opj_tcd_t1_decode(p_tcd, p_manager)) {
        return false;
    }

    if  (! opj_tcd_dwt_decode(p_tcd)) {
        return false;
    }

    if   (! opj_tcd_mct_decode(p_tcd, p_manager)) {
        return false;
    }

    if  (! opj_tcd_dc_level_shift_decode(p_tcd)) {
        return false;
    }

    return true;
}

/*

For each component, copy decoded resolutions from the tile data buffer
into p_dest buffer.

So, p_dest stores a sub-region of the tcd data, based on the number
of resolutions decoded. (why doesn't tile data buffer also match number of resolutions decoded ?)

Note: p_dest stores data in the actual precision of the decompressed image,
vs. tile data buffer which is always 32 bits.

If we are decoding all resolutions, then this step is not necessary ??

*/
bool opj_tcd_update_tile_data ( opj_tcd_t *p_tcd,
                                uint8_t * p_dest,
                                uint32_t p_dest_length
                              )
{
    uint32_t i,j,k,l_data_size = 0;
    opj_image_comp_t * l_img_comp = 00;
    opj_tcd_tilecomp_t * l_tilec = 00;
    opj_tcd_resolution_t * l_res;
    uint32_t l_size_comp;
    uint32_t l_stride, l_width,l_height;

    l_data_size = opj_tcd_get_decoded_tile_size(p_tcd);
    if (l_data_size > p_dest_length) {
        return false;
    }

    l_tilec = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;

    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = (l_img_comp->prec + 7) >> 3;
        l_res = l_tilec->resolutions + l_img_comp->resno_decoded;
        l_width = (uint32_t)(l_res->x1 - l_res->x0);
        l_height = (uint32_t)(l_res->y1 - l_res->y0);
        l_stride = (uint32_t)(l_tilec->x1 - l_tilec->x0) - l_width;

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        switch (l_size_comp) {
        case 1: {
            char * l_dest_ptr = (char *) p_dest;
            const int32_t * l_src_ptr = opj_tile_buf_get_ptr(l_tilec->buf, 0, 0, 0, 0);

            if (l_img_comp->sgnd) {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (char) (*(l_src_ptr++));
                    }
                    l_src_ptr += l_stride;
                }
            } else {
                for (j=0; j<l_height; ++j) {
                    for     (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (char) ((*(l_src_ptr++))&0xff);
                    }
                    l_src_ptr += l_stride;
                }
            }

            p_dest = (uint8_t *)l_dest_ptr;
        }
        break;
        case 2: {
            const int32_t * l_src_ptr = opj_tile_buf_get_ptr(l_tilec->buf, 0, 0, 0, 0);
            int16_t * l_dest_ptr = (int16_t *) p_dest;

            if (l_img_comp->sgnd) {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (int16_t) (*(l_src_ptr++));
                    }
                    l_src_ptr += l_stride;
                }
            } else {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (int16_t) ((*(l_src_ptr++))&0xffff);
                    }
                    l_src_ptr += l_stride;
                }
            }

            p_dest = (uint8_t*) l_dest_ptr;
        }
        break;
        case 4: {
            int32_t * l_dest_ptr = (int32_t *) p_dest;
            int32_t * l_src_ptr = opj_tile_buf_get_ptr(l_tilec->buf, 0, 0, 0, 0);

            for (j=0; j<l_height; ++j) {
                for (k=0; k<l_width; ++k) {
                    *(l_dest_ptr++) = (*(l_src_ptr++));
                }
                l_src_ptr += l_stride;
            }

            p_dest = (uint8_t*) l_dest_ptr;
        }
        break;
        }

        ++l_img_comp;
        ++l_tilec;
    }

    return true;
}




static void opj_tcd_free_tile(opj_tcd_t *p_tcd)
{
    uint32_t compno, resno, bandno, precno;
    opj_tcd_tile_t *l_tile = 00;
    opj_tcd_tilecomp_t *l_tile_comp = 00;
    opj_tcd_resolution_t *l_res = 00;
    opj_tcd_band_t *l_band = 00;
    opj_tcd_precinct_t *l_precinct = 00;
    uint32_t l_nb_resolutions, l_nb_precincts;
    void (* l_tcd_code_block_deallocate) (opj_tcd_precinct_t *) = 00;

    if (! p_tcd) {
        return;
    }

    if (! p_tcd->tile) {
        return;
    }

    if (p_tcd->m_is_decoder) {
        l_tcd_code_block_deallocate = opj_tcd_code_block_dec_deallocate;
    } else {
        l_tcd_code_block_deallocate = opj_tcd_code_block_enc_deallocate;
    }

    l_tile = p_tcd->tile;
    if (! l_tile) {
        return;
    }

    l_tile_comp = l_tile->comps;

    for (compno = 0; compno < l_tile->numcomps; ++compno) {
        l_res = l_tile_comp->resolutions;
        if (l_res) {

            l_nb_resolutions = l_tile_comp->resolutions_size / sizeof(opj_tcd_resolution_t);
            for (resno = 0; resno < l_nb_resolutions; ++resno) {
                l_band = l_res->bands;
                for     (bandno = 0; bandno < 3; ++bandno) {
                    l_precinct = l_band->precincts;
                    if (l_precinct) {

                        l_nb_precincts = l_band->precincts_data_size / sizeof(opj_tcd_precinct_t);
                        for (precno = 0; precno < l_nb_precincts; ++precno) {
                            opj_tgt_destroy(l_precinct->incltree);
                            l_precinct->incltree = 00;
                            opj_tgt_destroy(l_precinct->imsbtree);
                            l_precinct->imsbtree = 00;
                            (*l_tcd_code_block_deallocate) (l_precinct);
                            ++l_precinct;
                        }

                        opj_free(l_band->precincts);
                        l_band->precincts = 00;
                    }
                    ++l_band;
                } /* for (resno */
                ++l_res;
            }

            opj_free(l_tile_comp->resolutions);
            l_tile_comp->resolutions = 00;
        }

        opj_tile_buf_destroy_component(l_tile_comp->buf);
        l_tile_comp->buf = NULL;
        ++l_tile_comp;
    }

    opj_free(l_tile->comps);
    l_tile->comps = 00;
    opj_free(p_tcd->tile);
    p_tcd->tile = 00;
}


static bool opj_tcd_t2_decode (opj_tcd_t *p_tcd,
								uint32_t p_tile_no,
                               opj_seg_buf_t* src_buf,
                               uint32_t * p_data_read,
                               opj_event_mgr_t *p_manager
                              )
{
    opj_t2_t * l_t2;

    l_t2 = opj_t2_create(p_tcd->image, p_tcd->cp);
    if (l_t2 == 00) {
        return false;
    }

    if (! opj_t2_decode_packets(
                l_t2,
				p_tile_no,
                p_tcd->tile,
                src_buf,
                p_data_read,
                p_manager)) {
        opj_t2_destroy(l_t2);
        return false;
    }

    opj_t2_destroy(l_t2);

    return true;
}

static bool opj_tcd_t1_decode ( opj_tcd_t *p_tcd, opj_event_mgr_t * p_manager)
{
    uint32_t compno;
    opj_tcd_tile_t * l_tile = p_tcd->tile;
    opj_tcd_tilecomp_t* l_tile_comp = l_tile->comps;
    opj_tccp_t * l_tccp = p_tcd->tcp->tccps;
	std::vector<decodeBlockInfo*> blocks;
	T1Decoder decoder(l_tccp->cblkw, l_tccp->cblkh);
    for (compno = 0; compno < l_tile->numcomps; ++compno) {

        /* The +3 is headroom required by the vectorized DWT */
        if (false == opj_t1_decode_cblks(l_tile_comp, l_tccp,&blocks, p_manager)) {
            return false;
        }
        ++l_tile_comp;
        ++l_tccp;
    }
	decoder.decode(&blocks, numDecodeThreads);
    return true;
}


static bool opj_tcd_dwt_decode ( opj_tcd_t *p_tcd )
{
    opj_tcd_tile_t * l_tile = p_tcd->tile;
    int64_t compno=0;
    bool rc = true;
#ifdef _OPENMP
    #pragma omp parallel default(none) private(compno) shared(p_tcd, l_tile, rc)
    {
        #pragma omp for
#endif
        for (compno = 0; compno < (int64_t)l_tile->numcomps; compno++) {
            opj_tcd_tilecomp_t * l_tile_comp = l_tile->comps + compno;
            opj_tccp_t * l_tccp = p_tcd->tcp->tccps + compno;
            opj_image_comp_t * l_img_comp = p_tcd->image->comps + compno;
            if (l_tccp->qmfbid == 1) {
                if (! opj_dwt_decode(l_tile_comp, l_img_comp->resno_decoded+1)) {
                    rc = false;
                    continue;
                }
            } else {
                if (! opj_dwt_decode_real(l_tile_comp, l_img_comp->resno_decoded+1)) {
                    rc = false;
                    continue;
                }
            }
#ifdef _OPENMP
        }
#endif

    }

    return rc;
}
static bool opj_tcd_mct_decode ( opj_tcd_t *p_tcd, opj_event_mgr_t *p_manager)
{
    opj_tcd_tile_t * l_tile = p_tcd->tile;
    opj_tcp_t * l_tcp = p_tcd->tcp;
    opj_tcd_tilecomp_t * l_tile_comp = l_tile->comps;
    uint32_t l_samples,i;

    if (! l_tcp->mct) {
        return true;
    }

    l_samples = (uint32_t)((l_tile_comp->x1 - l_tile_comp->x0) * (l_tile_comp->y1 - l_tile_comp->y0));

    if (l_tile->numcomps >= 3 ) {
        /* testcase 1336.pdf.asan.47.376 */
        if ((l_tile->comps[0].x1 - l_tile->comps[0].x0) * (l_tile->comps[0].y1 - l_tile->comps[0].y0) < (int32_t)l_samples ||
                (l_tile->comps[1].x1 - l_tile->comps[1].x0) * (l_tile->comps[1].y1 - l_tile->comps[1].y0) < (int32_t)l_samples ||
                (l_tile->comps[2].x1 - l_tile->comps[2].x0) * (l_tile->comps[2].y1 - l_tile->comps[2].y0) < (int32_t)l_samples) {
            opj_event_msg(p_manager, EVT_ERROR, "Tiles don't all have the same dimension. Skip the MCT step.\n");
            return false;
        } else if (l_tcp->mct == 2) {
            uint8_t ** l_data;

            if (! l_tcp->m_mct_decoding_matrix) {
                return true;
            }

            l_data = (uint8_t **) opj_malloc(l_tile->numcomps*sizeof(uint8_t*));
            if (! l_data) {
                return false;
            }

            for (i=0; i<l_tile->numcomps; ++i) {
                l_data[i] = (uint8_t*)opj_tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
                ++l_tile_comp;
            }

            if (! opj_mct_decode_custom(/* MCT data */
                        (uint8_t*) l_tcp->m_mct_decoding_matrix,
                        /* size of components */
                        l_samples,
                        /* components */
                        l_data,
                        /* nb of components (i.e. size of pData) */
                        l_tile->numcomps,
                        /* tells if the data is signed */
                        p_tcd->image->comps->sgnd)) {
                opj_free(l_data);
                return false;
            }

            opj_free(l_data);
        } else {
            if (l_tcp->tccps->qmfbid == 1) {
                opj_mct_decode(opj_tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0) ,
                               opj_tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                               opj_tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                               l_samples);
            } else {
                opj_mct_decode_real((float*)opj_tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0),
                                    (float*)opj_tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                                    (float*)opj_tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                                    l_samples);
            }
        }
    } else {
        opj_event_msg(p_manager, EVT_ERROR, "Number of components (%d) is inconsistent with a MCT. Skip the MCT step.\n",l_tile->numcomps);
    }

    return true;
}


static bool opj_tcd_dc_level_shift_decode ( opj_tcd_t *p_tcd )
{
    int32_t compno=0;

#ifdef _OPENMP
#pragma omp parallel default(none) private(compno) shared(p_tcd)
	{
#pragma omp for
#endif

		for (compno = 0; compno < (int32_t)p_tcd->tile->numcomps; compno++) {
			int32_t l_min = INT32_MAX, l_max = INT32_MIN;

			opj_tcd_tilecomp_t *l_tile_comp = p_tcd->tile->comps + compno;
			opj_tccp_t * l_tccp = p_tcd->tcp->tccps + compno;
			opj_image_comp_t * l_img_comp = p_tcd->image->comps + compno;

			opj_tcd_resolution_t* l_res = l_tile_comp->resolutions + l_img_comp->resno_decoded;
			uint32_t l_width = (uint32_t)(l_res->x1 - l_res->x0);
			uint32_t l_height = (uint32_t)(l_res->y1 - l_res->y0);
			uint32_t l_stride = (uint32_t)(l_tile_comp->x1 - l_tile_comp->x0) - l_width;

		//	assert(l_height == 0 || l_width + l_stride <= l_tile_comp->buf->data_size / l_height); 

			if (l_img_comp->sgnd) {
				l_min = -(1 << (l_img_comp->prec - 1));
				l_max = (1 << (l_img_comp->prec - 1)) - 1;
			}
			else {
				l_min = 0;
				l_max = (1 << l_img_comp->prec) - 1;
			}

			int32_t* l_current_ptr = opj_tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);

			if (l_tccp->qmfbid == 1) {
				for (uint32_t j = 0; j < l_height; ++j) {
					for (uint32_t i = 0; i < l_width; ++i) {
						*l_current_ptr = opj_int_clamp(*l_current_ptr + l_tccp->m_dc_level_shift, l_min, l_max);
						++l_current_ptr;
					}
					l_current_ptr += l_stride;
				}
			}
			else {
				for (uint32_t j = 0; j < l_height; ++j) {
					for (uint32_t i = 0; i < l_width; ++i) {
						float l_value = *((float *)l_current_ptr);
						*l_current_ptr = opj_int_clamp((int32_t)opj_lrintf(l_value) + l_tccp->m_dc_level_shift, l_min, l_max); ;
						++l_current_ptr;
					}
					l_current_ptr += l_stride;
				}
			}
		}

#ifdef _OPENMP
	}
#endif

    return true;
}



/**
 * Deallocates the encoding data of the given precinct.
 */
static void opj_tcd_code_block_dec_deallocate (opj_tcd_precinct_t * p_precinct)
{
    uint32_t cblkno , l_nb_code_blocks;

    opj_tcd_cblk_dec_t * l_code_block = p_precinct->cblks.dec;
    if (l_code_block) {
        /*fprintf(stderr,"deallocate codeblock:{\n");*/
        /*fprintf(stderr,"\t x0=%d, y0=%d, x1=%d, y1=%d\n",l_code_block->x0, l_code_block->y0, l_code_block->x1, l_code_block->y1);*/
        /*fprintf(stderr,"\t numbps=%d, numlenbits=%d, len=%d, numnewpasses=%d, real_num_segs=%d, m_current_max_segs=%d\n ",
                        l_code_block->numbps, l_code_block->numlenbits, l_code_block->len, l_code_block->numnewpasses, l_code_block->real_num_segs, l_code_block->m_current_max_segs );*/


        l_nb_code_blocks = p_precinct->block_size / sizeof(opj_tcd_cblk_dec_t);
        /*fprintf(stderr,"nb_code_blocks =%d\t}\n", l_nb_code_blocks);*/

        for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
            opj_vec_cleanup(&l_code_block->seg_buffers);
            if (l_code_block->segs) {
                opj_free(l_code_block->segs );
                l_code_block->segs = 00;
            }

            ++l_code_block;
        }

        opj_free(p_precinct->cblks.dec);
        p_precinct->cblks.dec = 00;
    }
}

/**
 * Deallocates the encoding data of the given precinct.
 */
static void opj_tcd_code_block_enc_deallocate (opj_tcd_precinct_t * p_precinct)
{
    uint32_t cblkno , l_nb_code_blocks;

    opj_tcd_cblk_enc_t * l_code_block = p_precinct->cblks.enc;
    if (l_code_block) {
        l_nb_code_blocks = p_precinct->block_size / sizeof(opj_tcd_cblk_enc_t);

        for     (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno)  {
            if (l_code_block->owns_data && l_code_block->data) {
                opj_free(l_code_block->data - 1);
                l_code_block->data = 00;
				l_code_block->owns_data = false;
            }

            if (l_code_block->layers) {
                opj_free(l_code_block->layers );
                l_code_block->layers = 00;
            }

            if (l_code_block->passes) {
                opj_free(l_code_block->passes );
                l_code_block->passes = 00;
            }
            ++l_code_block;
        }

        opj_free(p_precinct->cblks.enc);

        p_precinct->cblks.enc = 00;
    }
}

uint32_t opj_tcd_get_encoded_tile_size ( opj_tcd_t *p_tcd )
{
    uint32_t i,l_data_size = 0;
    opj_image_comp_t * l_img_comp = 00;
    opj_tcd_tilecomp_t * l_tilec = 00;
    uint32_t l_size_comp, l_remaining;

    l_tilec = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;
    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = l_img_comp->prec >> 3; /*(/ 8)*/
        l_remaining = l_img_comp->prec & 7;  /* (%8) */

        if (l_remaining) {
            ++l_size_comp;
        }

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        l_data_size += l_size_comp * (uint32_t)((l_tilec->x1 - l_tilec->x0) * (l_tilec->y1 - l_tilec->y0));
        ++l_img_comp;
        ++l_tilec;
    }

    return l_data_size;
}

static bool opj_tcd_dc_level_shift_encode ( opj_tcd_t *p_tcd )
{
    uint32_t compno;
    opj_tcd_tilecomp_t * l_tile_comp = 00;
    opj_tccp_t * l_tccp = 00;
    opj_image_comp_t * l_img_comp = 00;
    opj_tcd_tile_t * l_tile;
    uint32_t l_nb_elem,i;
    int32_t * l_current_ptr;

    l_tile = p_tcd->tile;
    l_tile_comp = l_tile->comps;
    l_tccp = p_tcd->tcp->tccps;
    l_img_comp = p_tcd->image->comps;

    for (compno = 0; compno < l_tile->numcomps; compno++) {
        l_current_ptr = opj_tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
        l_nb_elem = (uint32_t)((l_tile_comp->x1 - l_tile_comp->x0) * (l_tile_comp->y1 - l_tile_comp->y0));

        if (l_tccp->qmfbid == 1) {
            for     (i = 0; i < l_nb_elem; ++i) {
                *l_current_ptr -= l_tccp->m_dc_level_shift ;
                ++l_current_ptr;
            }
        } else {
            for (i = 0; i < l_nb_elem; ++i) {
                *l_current_ptr = (*l_current_ptr - l_tccp->m_dc_level_shift) << 11 ;
                ++l_current_ptr;
            }
        }

        ++l_img_comp;
        ++l_tccp;
        ++l_tile_comp;
    }

    return true;
}

static bool opj_tcd_mct_encode ( opj_tcd_t *p_tcd )
{
    opj_tcd_tile_t * l_tile = p_tcd->tile;
    opj_tcd_tilecomp_t * l_tile_comp = p_tcd->tile->comps;
    uint32_t samples = (uint32_t)((l_tile_comp->x1 - l_tile_comp->x0) * (l_tile_comp->y1 - l_tile_comp->y0));
    uint32_t i;
    uint8_t ** l_data = 00;
    opj_tcp_t * l_tcp = p_tcd->tcp;

    if(!p_tcd->tcp->mct) {
        return true;
    }

    if (p_tcd->tcp->mct == 2) {
        if (! p_tcd->tcp->m_mct_coding_matrix) {
            return true;
        }

        l_data = (uint8_t **) opj_malloc(l_tile->numcomps*sizeof(uint8_t*));
        if (! l_data) {
            return false;
        }

        for (i=0; i<l_tile->numcomps; ++i) {
            l_data[i] = (uint8_t*)opj_tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
            ++l_tile_comp;
        }

        if (! opj_mct_encode_custom(/* MCT data */
                    (uint8_t*) p_tcd->tcp->m_mct_coding_matrix,
                    /* size of components */
                    samples,
                    /* components */
                    l_data,
                    /* nb of components (i.e. size of pData) */
                    l_tile->numcomps,
                    /* tells if the data is signed */
                    p_tcd->image->comps->sgnd) ) {
            opj_free(l_data);
            return false;
        }

        opj_free(l_data);
    } else if (l_tcp->tccps->qmfbid == 0) {
        opj_mct_encode_real(opj_tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0),
                            opj_tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                            opj_tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                            samples);
    } else {
        opj_mct_encode(opj_tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0),
                       opj_tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                       opj_tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                       samples);
    }

    return true;
}


bool opj_tcd_dwt_encode ( opj_tcd_t *p_tcd )
{
    opj_tcd_tile_t * l_tile = p_tcd->tile;
    int64_t compno=0;
    bool rc = true;
#ifdef _OPENMP
    #pragma omp parallel default(none) private(compno) shared(p_tcd, l_tile, rc)
    {
        #pragma omp for
#endif
        for (compno = 0; compno < (int64_t)l_tile->numcomps; ++compno) {
            opj_tcd_tilecomp_t * tile_comp = p_tcd->tile->comps + compno;
            opj_tccp_t * l_tccp = p_tcd->tcp->tccps + compno;
            if (l_tccp->qmfbid == 1) {
                if (! opj_dwt_encode(tile_comp)) {
                    rc = false;
                    continue;
                }
            } else if (l_tccp->qmfbid == 0) {
                if (! opj_dwt_encode_real(tile_comp)) {
                    rc = false;
                    continue;
                }
            }
        }
#ifdef _OPENMP
    }
#endif

    return rc;
}

static bool opj_tcd_t1_encode ( opj_tcd_t *p_tcd )
{
    const double * l_mct_norms;
    uint32_t l_mct_numcomps = 0U;
    opj_tcp_t * l_tcp = p_tcd->tcp;

    if (l_tcp->mct == 1) {
        l_mct_numcomps = 3U;
        /* irreversible encoding */
        if (l_tcp->tccps->qmfbid == 0) {
            l_mct_norms = opj_mct_get_mct_norms_real();
        } else {
            l_mct_norms = opj_mct_get_mct_norms();
        }
    } else {
        l_mct_numcomps = p_tcd->image->numcomps;
        l_mct_norms = (const double *) (l_tcp->mct_norms);
    }

    return opj_t1_encode_cblks(p_tcd->tile, l_tcp, l_mct_norms, l_mct_numcomps);
}

static bool opj_tcd_t2_encode (opj_tcd_t *p_tcd,
                               uint8_t * p_dest_data,
                               uint32_t * p_data_written,
                               uint32_t p_max_dest_size,
                               opj_codestream_info_t *p_cstr_info )
{
    opj_t2_t * l_t2;

    l_t2 = opj_t2_create(p_tcd->image, p_tcd->cp);
    if (l_t2 == 00) {
        return false;
    }

    if (! opj_t2_encode_packets(
                l_t2,
                p_tcd->tcd_tileno,
                p_tcd->tile,
                p_tcd->tcp->numlayers,
                p_dest_data,
                p_data_written,
                p_max_dest_size,
                p_cstr_info,
                p_tcd->tp_num,
                p_tcd->tp_pos,
                p_tcd->cur_pino)) {
        opj_t2_destroy(l_t2);
        return false;
    }

    opj_t2_destroy(l_t2);

    /*---------------CLEAN-------------------*/
    return true;
}


static bool opj_tcd_rate_allocate_encode(  opj_tcd_t *p_tcd,
        uint32_t p_max_dest_size,
        opj_codestream_info_t *p_cstr_info )
{
    opj_cp_t * l_cp = p_tcd->cp;
    uint32_t l_nb_written = 0;

    if (p_cstr_info)  {
        p_cstr_info->index_write = 0;
    }

    if (l_cp->m_specific_param.m_enc.m_disto_alloc|| l_cp->m_specific_param.m_enc.m_fixed_quality)  {
        /* fixed_quality */
        /* Normal Rate/distortion allocation */
        if (! opj_tcd_rateallocate(p_tcd, &l_nb_written, p_max_dest_size, p_cstr_info)) {
            return false;
        }
    } else {
        /* Fixed layer allocation */
        opj_tcd_rateallocate_fixed(p_tcd);
    }

    return true;
}


bool opj_tcd_copy_tile_data (       opj_tcd_t *p_tcd,
                                    uint8_t * p_src,
                                    uint32_t p_src_length )
{
    uint32_t i,j,l_data_size = 0;
    opj_image_comp_t * l_img_comp = 00;
    opj_tcd_tilecomp_t * l_tilec = 00;
    uint32_t l_size_comp, l_remaining;
    uint32_t l_nb_elem;

    l_data_size = opj_tcd_get_encoded_tile_size(p_tcd);
    if (l_data_size != p_src_length) {
        return false;
    }

    l_tilec = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;
    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = l_img_comp->prec >> 3; /*(/ 8)*/
        l_remaining = l_img_comp->prec & 7;  /* (%8) */
        l_nb_elem = (uint32_t)((l_tilec->x1 - l_tilec->x0) * (l_tilec->y1 - l_tilec->y0));

        if (l_remaining) {
            ++l_size_comp;
        }

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        switch (l_size_comp) {
        case 1: {
            char * l_src_ptr = (char *) p_src;
            int32_t * l_dest_ptr = l_tilec->buf->data;

            if (l_img_comp->sgnd) {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
                }
            } else {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (*(l_src_ptr++))&0xff;
                }
            }

            p_src = (uint8_t*) l_src_ptr;
        }
        break;
        case 2: {
            int32_t * l_dest_ptr = l_tilec->buf->data;
            int16_t * l_src_ptr = (int16_t *) p_src;

            if (l_img_comp->sgnd) {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
                }
            } else {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (*(l_src_ptr++))&0xffff;
                }
            }

            p_src = (uint8_t*) l_src_ptr;
        }
        break;
        case 4: {
            int32_t * l_src_ptr = (int32_t *) p_src;
            int32_t * l_dest_ptr = l_tilec->buf->data;

            for (j=0; j<l_nb_elem; ++j) {
                *(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
            }

            p_src = (uint8_t*) l_src_ptr;
        }
        break;
        }

        ++l_img_comp;
        ++l_tilec;
    }

    return true;
}
