/*
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

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "grok.h"

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len);

typedef struct {
    const uint8_t* pabyData;
    size_t         nCurPos;
    size_t         nLength;
} MemFile;


static void ErrorCallback(const char * msg, void *)
{
    (void)msg;
    //fprintf(stderr, "%s\n", msg);
}


static void WarningCallback(const char *, void *)
{
}

static void InfoCallback(const char *, void *)
{
}

static size_t ReadCallback(void* pBuffer, size_t nBytes,
                               void *pUserData)
{
    MemFile* memFile = (MemFile*)pUserData;
    //printf("want to read %d bytes at %d\n", (int)memFile->nCurPos, (int)nBytes);
    if (memFile->nCurPos >= memFile->nLength) {
        return 0;
    }
    if (memFile->nCurPos + nBytes >= memFile->nLength) {
        size_t nToRead = memFile->nLength - memFile->nCurPos;
        memcpy(pBuffer, memFile->pabyData + memFile->nCurPos, nToRead);
        memFile->nCurPos = memFile->nLength;
        return nToRead;
    }
    if (nBytes == 0) {
        return 0;
    }
    memcpy(pBuffer, memFile->pabyData + memFile->nCurPos, nBytes);
    memFile->nCurPos += nBytes;
    return nBytes;
}

static bool SeekCallback(size_t nBytes, void * pUserData)
{
    MemFile* memFile = (MemFile*)pUserData;
    //printf("seek to %d\n", (int)nBytes);
    memFile->nCurPos = nBytes;
    return true;
}

int LLVMFuzzerInitialize(int* /*argc*/, char*** argv)
{
    return 0;
}

static const unsigned char jpc_header[] = {0xff, 0x4f};
static const unsigned char jp2_box_jp[] = {0x6a, 0x50, 0x20, 0x20}; /* 'jP  ' */

int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len)
{
    GRK_CODEC_FORMAT eCodecFormat;
    if (len >= sizeof(jpc_header) &&
            memcmp(buf, jpc_header, sizeof(jpc_header)) == 0) {
        eCodecFormat = GRK_CODEC_J2K;
    } else if (len >= 4 + sizeof(jp2_box_jp) &&
               memcmp(buf + 4, jp2_box_jp, sizeof(jp2_box_jp)) == 0) {
        eCodecFormat = GRK_CODEC_JP2;
    } else {
        return 0;
    }
	grk_initialize(nullptr,0);
    grk_stream *pStream = grk_stream_create(1024, true);
    MemFile memFile;
    memFile.pabyData = buf;
    memFile.nLength = len;
    memFile.nCurPos = 0;
    grk_stream_set_user_data_length(pStream, len);
    grk_stream_set_read_function(pStream, ReadCallback);
    grk_stream_set_seek_function(pStream, SeekCallback);
    grk_stream_set_user_data(pStream, &memFile, NULL);


    grk_codec* pCodec = grk_create_decompress(eCodecFormat, pStream);
    grk_set_info_handler(InfoCallback, NULL);
    grk_set_warning_handler(WarningCallback, NULL);
    grk_set_error_handler(ErrorCallback, NULL);

    grk_dparameters parameters;
    grk_set_default_decoder_parameters(&parameters);

    grk_setup_decoder(pCodec, &parameters);
    grk_image * psImage = NULL;
    grk_header_info  header_info;
    uint32_t width, height,width_to_read, height_to_read;
    if (!grk_read_header(pCodec, &header_info, &psImage))
        goto cleanup;
    width = psImage->x1 - psImage->x0;
    height = psImage->y1 - psImage->y0;
    width_to_read = width;
    if (width_to_read > 1024)
        width_to_read = 1024;
    height_to_read = height;
    if (height_to_read > 1024)
        height_to_read = 1024;

    if (grk_set_decode_area(pCodec,
    						psImage,
                            psImage->x0,
							psImage->y0,
                            psImage->x0 + width_to_read,
                            psImage->y0 + height_to_read)) {
        if (!grk_decode(pCodec, nullptr, psImage))
        	goto cleanup;
    }

    grk_end_decompress(pCodec);
cleanup:
    grk_stream_destroy(pStream);
    grk_destroy_codec(pCodec);
    grk_image_destroy(psImage);
    grk_deinitialize();

    return 0;
}
