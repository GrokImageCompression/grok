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

#include <stdint.h>
#include <stdio.h>
