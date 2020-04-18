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
 * Copyright (c) 2017, IntoPix SA <contact@intopix.com>
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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#endif /* _WIN32 */

#include <chrono>  // for high_resolution_clock

using namespace grk;

namespace grk {

int32_t getValue(uint32_t i)
{
    return ((int32_t)i % 511) - 256;
}

void init_tilec(TileComponent * l_tilec,
                uint32_t x0,
                uint32_t y0,
                uint32_t x1,
                uint32_t y1,
                uint32_t numresolutions)
{
    grk_tcd_resolution* l_res;
    uint32_t resno, l_level_no;
    size_t i, nValues;

    l_tilec->x0 = x0;
    l_tilec->y0 = y0;
    l_tilec->x1 = x1;
    l_tilec->y1 = y1;
    l_tilec->m_is_encoder = false;
    l_tilec->create_buffer(nullptr,1,1);

    nValues = (size_t)(l_tilec->x1 - l_tilec->x0) *
              (size_t)(l_tilec->y1 - l_tilec->y0);
    l_tilec->buf->data = (int32_t*) grk_malloc(sizeof(int32_t) * nValues);
    for (i = 0; i < nValues; i++) {
        l_tilec->buf->data[i] = getValue((uint32_t)i);
    }
    l_tilec->numresolutions = numresolutions;
    l_tilec->minimum_num_resolutions = numresolutions;
    l_tilec->resolutions = (grk_tcd_resolution*) grk_calloc(
                               l_tilec->numresolutions,
                               sizeof(grk_tcd_resolution));

    l_level_no = l_tilec->numresolutions;
    l_res = l_tilec->resolutions;

    /* Adapted from grk_tcd_init_tile() */
    for (resno = 0; resno < l_tilec->numresolutions; ++resno) {

        --l_level_no;

        /* border for each resolution level (global) */
        l_res->x0 = uint_ceildivpow2(l_tilec->x0, l_level_no);
        l_res->y0 = uint_ceildivpow2(l_tilec->y0, l_level_no);
        l_res->x1 = uint_ceildivpow2(l_tilec->x1, l_level_no);
        l_res->y1 = uint_ceildivpow2(l_tilec->y1, l_level_no);

        ++l_res;
    }
}

void free_tilec(TileComponent * l_tilec)
{
    grok_free(l_tilec->buf->data);
    grok_free(l_tilec->resolutions);
}

void usage(void)
{
    printf(
        "bench_dwt [-size value] [-check] [-display] [-num_resolutions val] [-lossy]\n");
    printf(
        "          [-offset x y] [-num_threads val]\n");
}

}



int main(int argc, char** argv)
{
    uint32_t num_threads = 0;
    TileProcessor tcd(true);
    grk_image tcd_image;
    grk_tcd_tile tcd_tile;
    TileComponent tilec;
    grk_image image;
    grk_image_comp image_comp;
    int32_t i, j, k;
    bool display = false;
    bool check = false;
    bool lossy = false;
    uint32_t size = 16384 - 1;
    uint32_t offset_x = (uint32_t)((size + 1) / 2 - 1);
    uint32_t offset_y = (uint32_t)((size + 1) / 2 - 1);
    uint32_t num_resolutions = 6;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-display") == 0) {
            display = true;
            check = true;
        } else if (strcmp(argv[i], "-check") == 0) {
            check = true;
        } else if (strcmp(argv[i], "-size") == 0 && i + 1 < argc) {
            size = (uint32_t)atoi(argv[i + 1]);
            i ++;
        } else if (strcmp(argv[i], "-lossy") == 0) {
            lossy = true;
            i ++;
        } else if (strcmp(argv[i], "-num_threads") == 0 && i + 1 < argc) {
            num_threads = (uint32_t)atoi(argv[i + 1]);
            i ++;
        } else if (strcmp(argv[i], "-num_resolutions") == 0 && i + 1 < argc) {
            num_resolutions = (uint32_t)atoi(argv[i + 1]);
            if (num_resolutions == 0 || num_resolutions > 32) {
                fprintf(stderr,
                        "Invalid value for num_resolutions. Should be >= 1 and <= 32\n");
                exit(1);
            }
            i ++;
        } else if (strcmp(argv[i], "-offset") == 0 && i + 2 < argc) {
            offset_x = (uint32_t)atoi(argv[i + 1]);
            offset_y = (uint32_t)atoi(argv[i + 2]);
            i += 2;
        } else {
            usage();
            return 1;
        }
    }

   grk_initialize(nullptr,num_threads);

   init_tilec(&tilec, offset_x, offset_y,
               offset_x + size, offset_y + size,
               num_resolutions);

    if (display) {
        printf("Before\n");
        k = 0;
        for (j = 0; j < (int32_t)(tilec.y1 - tilec.y0); j++) {
            for (i = 0; i < (int32_t)(tilec.x1 - tilec.x0); i++) {
                printf("%d ", tilec.buf->data[k]);
                k ++;
            }
            printf("\n");
        }
    }

    tcd.image = &tcd_image;
    memset(&tcd_image, 0, sizeof(tcd_image));
    memset(&tcd_tile, 0, sizeof(tcd_tile));
    tcd_tile.x0 = tilec.x0;
    tcd_tile.y0 = tilec.y0;
    tcd_tile.x1 = tilec.x1;
    tcd_tile.y1 = tilec.y1;
    tcd_tile.numcomps = 1;
    tcd_tile.comps = &tilec;
    tcd.image = &image;
    memset(&image, 0, sizeof(image));
    image.numcomps = 1;
    image.comps = &image_comp;
    memset(&image_comp, 0, sizeof(image_comp));
    image_comp.dx = 1;
    image_comp.dy = 1;


	std::chrono::time_point<std::chrono::high_resolution_clock> start, finish;
	std::chrono::duration<double> elapsed;

	start = std::chrono::high_resolution_clock::now();
	if (lossy)
		decode_97(&tcd, &tilec, tilec.numresolutions);
	else
		decode_53(&tcd, &tilec, tilec.numresolutions);
	finish = std::chrono::high_resolution_clock::now();
	elapsed = finish - start;
    printf("time for dwt_decode: %.03f ms\n", elapsed.count()*1000);

    if (display || check) {
        if (display) {
            printf("After IDWT\n");
            k = 0;
            for (j = 0; j < (int32_t)(tilec.y1 - tilec.y0); j++) {
                for (i = 0; i < (int32_t)(tilec.x1 - tilec.x0); i++) {
                    printf("%d ", tilec.buf->data[k]);
                    k ++;
                }
                printf("\n");
            }
        }

        Wavelet::compress(&tilec, 1);
        if (display) {
            printf("After FDWT\n");
            k = 0;
            for (j = 0; j < (int32_t)(tilec.y1 - tilec.y0); j++) {
                for (i = 0; i < (int32_t)(tilec.x1 - tilec.x0); i++) {
                    printf("%d ", tilec.buf->data[k]);
                    k ++;
                }
                printf("\n");
            }
        }

        if (check) {
            size_t idx;
            size_t nValues = (size_t)(tilec.x1 - tilec.x0) *
                             (size_t)(tilec.y1 - tilec.y0);
            for (idx = 0; idx < nValues; idx++) {
                if (tilec.buf->data[idx] != getValue((uint32_t)idx)) {
                    printf("Difference found at idx = %u\n", (uint32_t)idx);
                    return 1;
                }
            }
        }
    }
    free_tilec(&tilec);
    grk_deinitialize();

    return 0;
}


