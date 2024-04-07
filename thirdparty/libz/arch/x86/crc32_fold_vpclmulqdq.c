/* crc32_fold_vpclmulqdq.c -- VPCMULQDQ-based CRC32 folding implementation.
 * Copyright Wangyang Guo (wangyang.guo@intel.com)
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#ifdef X86_VPCLMULQDQ_CRC
#include "../../zbuild.h"
#include "../../fallback_builtins.h"

#include <immintrin.h>

#define ONCE(op)            if (first) { first = 0; op; }
#define XOR_INITIAL(where)  ONCE(where = _mm512_xor_si512(where, zmm_initial))

#include "crc32_fold_vpclmulqdq_tpl.h"
#define COPY
#include "crc32_fold_vpclmulqdq_tpl.h"

#endif
