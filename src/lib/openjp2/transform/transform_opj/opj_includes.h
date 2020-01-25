#pragma once

#include <math.h>
#include <assert.h>
#include <string.h>

typedef int OPJ_BOOL;
#define OPJ_TRUE 1
#define OPJ_FALSE 0

typedef char          OPJ_CHAR;
typedef float         OPJ_FLOAT32;
typedef double        OPJ_FLOAT64;
typedef unsigned char OPJ_BYTE;

#include <cstdint>

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


/* Type to use for bit-fields in internal headers */
typedef unsigned int OPJ_BITFIELD;

/* Type to use for bit-fields in internal headers */
typedef unsigned int OPJ_BITFIELD;

#define OPJ_UNUSED(x) (void)x

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


/* Not a C99 compiler */
#if defined(__GNUC__)
#define OPJ_RESTRICT __restrict__

/*
  vc14 (2015) outputs wrong results.
  Need to check OPJ_RESTRICT usage (or a bug in vc14)
    #elif defined(_MSC_VER) && (_MSC_VER >= 1400)
        #define OPJ_RESTRICT __restrict
*/
#else
#define OPJ_RESTRICT /* restrict */
#endif
#include "opj_intmath.h"


