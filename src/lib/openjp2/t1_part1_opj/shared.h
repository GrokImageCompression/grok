/*
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
* Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
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
#pragma once
/*
The inline keyword is supported by C99 but not by C90.
Most compilers implement their own version of this keyword ...
*/
#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE __forceinline
#elif defined(__GNUC__)
#define INLINE __inline__
#elif defined(__MWERKS__)
#define INLINE inline
#else
/* add other compilers here ... */
#define INLINE
#endif /* defined(<Compiler>) */
#endif /* INLINE */


#define OPJ_TRUE 1
#define OPJ_FALSE 0

typedef char          OPJ_CHAR;
typedef float         OPJ_FLOAT32;
typedef double        OPJ_FLOAT64;
typedef unsigned char OPJ_BYTE;

#include <stdint.h>

typedef int8_t   OPJ_INT8;
typedef uint8_t  OPJ_UINT8;
typedef int16_t  OPJ_INT16;
typedef uint16_t OPJ_UINT16;
typedef int32_t  OPJ_INT32;
typedef uint32_t OPJ_UINT32;
typedef int64_t  OPJ_INT64;
typedef uint64_t OPJ_UINT64;

typedef int64_t  OPJ_OFF_T; /* 64-bit file offset type */

#include <stdio.h>
typedef size_t   OPJ_SIZE_T;

/* Avoid compile-time warning because parameter is not used */
#define OPJ_ARG_NOT_USED(x) (void)(x)



/*
 * Callback function prototype for events
 * @param msg               Event message
 * @param client_data       Client object where will be return the event message
 * */
typedef void (*opj_msg_callback)(const char *msg, void *client_data);



