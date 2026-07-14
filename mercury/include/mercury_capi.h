/* C API for embedding the mercury JPEG 2000 decoder. src/capi.rs is the
 * source of truth for these signatures and semantics.
 *
 * Flow: mercury_warp_loom{,_fd} -> (info queries) -> mercury_weave.
 * warp_loom returns NULL when the plan stage can't handle the stream (host
 * falls back to its own pipeline). mercury_weave consumes and frees the
 * plan; mercury_unwarp_loom is only for plans never decoded.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MercuryPlan MercuryPlan;

enum
{
  MERCURY_OK = 0,
  MERCURY_EBADARG = -1,
  MERCURY_EDECODE = -2,
  MERCURY_EPANIC = -3,
};

/* Positioned read: fill buf[0..len) from absolute offset off; nonzero on
 * success. Called concurrently from many threads. */
typedef int32_t (*mercury_read_at_fn)(void* ctx, uint8_t* buf, uint64_t off, uint64_t len);

/* Substitute tier-1 block decoder (BlockCoder contract, see mercury
 * src/decode/stripe_decoder.rs): write row-major sign-magnitude i32
 * (bit 31 sign, magnitude left-aligned so the reversible sample is
 * mag >> (31 - k_max_prime)) into out, which has room for
 * num_cols * 4*ceil(num_rows/4) samples. Nonzero on success. */
typedef int32_t (*mercury_t1_fn)(const uint8_t* coded_data, int32_t coded_length,
                                 int32_t num_passes, int32_t missing_msbs, int32_t k_max_prime,
                                 int32_t orientation, int32_t modes, int32_t num_cols,
                                 int32_t num_rows, const int32_t* segment_lengths,
                                 int32_t num_segments, int32_t* out);

/* Row delivery: comps[c] points at width i32 samples of component c, valid
 * only during the call. Rows arrive in order, one call at a time, on a decode
 * worker thread; must not throw. Unsigned components are DC-shifted to
 * [0, 2^prec) and clamped; signed are raw; the 9/7 float path follows the
 * sample-conversion spec. */
typedef void (*mercury_row_fn)(void* ctx, uint32_t row, const int32_t* const* comps,
                               uint32_t num_comps, uint64_t width);

/* Main header, parsed by the host codec and handed in; mercury does not parse
 * it. Every pointer is borrowed for the mercury_warp_loom{,_fd} call only;
 * mercury copies what it keeps. */

typedef struct MercurySizComp
{
  uint8_t precision;
  bool is_signed;
  uint8_t xr_siz;
  uint8_t yr_siz;
} MercurySizComp;

typedef struct MercuryPrecinct
{
  uint32_t width;
  uint32_t height;
} MercuryPrecinct;

typedef struct MercuryQuant
{
  uint8_t guard_bits;
  uint8_t style; /* 0 = reversible, 1 = derived, 2 = expounded */
  const uint8_t* ranges; /* reversible: one ranging exponent per band, otherwise nullptr */
  uint32_t num_ranges;
  const float* steps; /* irreversible: one step size per band, otherwise nullptr */
  uint32_t num_steps;
} MercuryQuant;

typedef struct MercuryQccOverride
{
  uint32_t comp;
  MercuryQuant quant;
} MercuryQccOverride;

typedef struct MercuryMainHeader
{
  uint32_t x_siz, y_siz, x_o_siz, y_o_siz;
  uint32_t xt_siz, yt_siz, xt_o_siz, yt_o_siz;
  const MercurySizComp* comps;
  uint32_t num_comps;
  uint8_t order; /* 0=LRCP 1=RLCP 2=RPCL 3=PCRL 4=CPRL */
  uint16_t num_layers;
  bool use_ycc;
  uint8_t num_levels;
  uint32_t block_width, block_height;
  uint32_t modes;
  bool reversible;
  bool use_sop, use_eph;
  const MercuryPrecinct* precincts; /* nullptr => default 2^15 x 2^15 */
  uint32_t num_precincts;
  MercuryQuant qcd;
  const MercuryQccOverride* qcc;
  uint32_t num_qcc;
  /* Codestream layout in the file the read_at/fd addresses. */
  uint64_t codestream_off; /* absolute file offset of the SOC marker */
  uint64_t first_sot_off; /* absolute file offset of the first SOT marker */
} MercuryMainHeader;

/* hdr describes the (host-parsed) main header; mercury copies what it keeps.
 * err_buf (optional): NUL-terminated rejection reason on nullptr return. */
MercuryPlan* mercury_warp_loom(const MercuryMainHeader* hdr, mercury_read_at_fn read_at, void* ctx,
                               uint64_t len, uint8_t* err_buf, size_t err_cap);

/* fd is duplicated internally (caller keeps ownership); reads use pread.
 * Unix only (POSIX fd); on Windows use mercury_warp_loom with a callback. */
#if !defined(_WIN32)
MercuryPlan* mercury_warp_loom_fd(const MercuryMainHeader* hdr, int32_t fd, uint8_t* err_buf,
                                  size_t err_cap);
#endif

typedef struct MercuryImageInfo
{
  uint32_t width;
  uint32_t height;
  uint32_t num_comps;
  bool reversible;
} MercuryImageInfo;

int32_t mercury_loom_info(const MercuryPlan* plan, MercuryImageInfo* out);
int32_t mercury_loom_comp_info(const MercuryPlan* plan, uint32_t comp, uint32_t* prec,
                               int32_t* is_signed);
void mercury_unwarp_loom(MercuryPlan* plan);

/* t1 NULL = built-in coder in the standalone build; the extern-kernels
 * (embedding) build has no built-in T1, so NULL returns MERCURY_EBADARG and a
 * substitute MUST be supplied. A non-NULL substitute is process-global (last
 * mercury_weave wins). threads 0 = one per core. */
int32_t mercury_weave(MercuryPlan* plan, mercury_t1_fn t1, mercury_row_fn row_fn, void* row_ctx,
                      uint32_t threads);

#ifdef __cplusplus
}
#endif
