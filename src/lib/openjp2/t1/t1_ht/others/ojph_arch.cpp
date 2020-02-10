//***************************************************************************/
// This software is released under the 2-Clause BSD license, included
// below.
//
// Copyright (c) 2019, Aous Naman 
// Copyright (c) 2019, Kakadu Software Pty Ltd, Australia
// Copyright (c) 2019, The University of New South Wales, Australia
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//***************************************************************************/
// This file is part of the OpenJPH software implementation.
// File: ojph_arch.cpp
// Author: Aous Naman
// Date: 28 August 2019
//***************************************************************************/

#include <cassert>

#include "ojph_arch.h"

namespace ojph {

#ifndef OJPH_DISABLE_INTEL_SIMD

  ////////////////////////////////////////////////////////////////////////////
  // This snippet is borrowed from Intel; see for example
  // https://software.intel.com/en-us/articles/
  // how-to-detect-knl-instruction-support
  bool run_cpuid(uint32_t eax, uint32_t ecx, uint32_t* abcd)
  {
  #ifdef OJPH_COMPILER_MSVC
    __cpuidex((int *)abcd, eax, ecx);
  #else
    uint32_t ebx = 0, edx = 0;
  #if defined( __i386__ ) && defined ( __PIC__ )
    /* in case of PIC under 32-bit EBX cannot be clobbered */
    __asm__ ( "movl %%ebx, %%edi \n\t cpuid \n\t xchgl %%ebx, %%edi"
              : "=D" (ebx), "+a" (eax), "+c" (ecx), "=d" (edx) );
  #else
    __asm__ ( "cpuid" : "+b" (ebx), "+a" (eax), "+c" (ecx), "=d" (edx) );
  #endif
    abcd[0] = eax; abcd[1] = ebx; abcd[2] = ecx; abcd[3] = edx;
  #endif
    return true;
  }

  ////////////////////////////////////////////////////////////////////////////
  uint64_t read_xcr(uint32_t index)
  {
  #ifdef OJPH_COMPILER_MSVC
    return _xgetbv(index);
  #else
    uint32_t eax = 0, edx = 0;
    __asm__ ( "xgetbv" : "=a" (eax), "=d" (edx) : "c" (index) );
    return ((uint64_t)edx << 32) | eax;
  #endif
  }

  /////////////////////////////////////////////////////////////////////////////
  bool init_cpu_ext_level(int& level)
  {
    uint32_t mmx_abcd[4];
    run_cpuid(1, 0, mmx_abcd);
    bool mmx_avail = ((mmx_abcd[3] & 0x00800000) == 0x00800000);

    level = 0;
    if (mmx_avail)
    {
      level = 1;
      bool sse_avail = ((mmx_abcd[3] & 0x02000000) == 0x02000000);
      if (sse_avail)
      {
        level = 2;
        bool sse2_avail = ((mmx_abcd[3] & 0x04000000) == 0x04000000);
        if (sse2_avail)
        {
          level = 3;
          bool sse3_avail = ((mmx_abcd[2] & 0x00000001) == 0x00000001);
          if (sse3_avail)
          {
            level = 4;
            bool ssse3_avail = ((mmx_abcd[2] & 0x00000200) == 0x00000200);
            if (ssse3_avail)
            {
              level = 5;
              bool sse41_avail = ((mmx_abcd[2] & 0x00080000) == 0x00080000);
              bool sse42_avail = ((mmx_abcd[2] & 0x00100000) == 0x00100000);
              if (sse41_avail && sse42_avail)
              {
                level = 6;

                uint64_t xcr_val = 0;
                bool osxsave_avail, ymm_avail, avx_avail = false;
                osxsave_avail = ((mmx_abcd[2] & 0x08000000) == 0x08000000);
                if (osxsave_avail)
                {
                  xcr_val = read_xcr(0);
                  ymm_avail = osxsave_avail && ((xcr_val & 0x6) == 0x6);
                  avx_avail = ymm_avail && (mmx_abcd[2] & 0x10000000);
                }
                if (avx_avail)
                {
                  level = 7;

                  uint32_t avx2_abcd[4];
                  run_cpuid(7, 0, avx2_abcd);
                  bool avx2_avail = (avx2_abcd[1] & 0x20) != 0;
                  if (avx2_avail)
                  {
                    level = 8;
                    bool avx2fma_avail =
                      avx2_avail && ((mmx_abcd[2] & 0x1000) == 0x1000);
                    if (avx2fma_avail)
                    {
                      level = 9;

                      bool zmm_avail =
                        osxsave_avail && ((xcr_val & 0xE) == 0xE);
                      bool avx512vl_avail = (avx2_abcd[1] & 0x80000000) != 0;
                      bool avx512_avail = zmm_avail && avx512vl_avail;
                      if (avx512_avail)
                        level = 10;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    return true;
  }

  ////////////////////////////////////////////////////////////////////////////
  static int cpu_level;
  static bool cpu_level_initialized = init_cpu_ext_level(cpu_level);
  
#else

  ////////////////////////////////////////////////////////////////////////////
  static int cpu_level = 0;
  static bool cpu_level_initialized = true;

#endif // !OJPH_DISABLE_INTEL_SIMD


  ////////////////////////////////////////////////////////////////////////////
  int cpu_ext_level()
  {
    assert(cpu_level_initialized);
    return cpu_level;
  }

}
