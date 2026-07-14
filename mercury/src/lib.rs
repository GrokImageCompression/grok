//! Mercury — pure-Rust JPEG 2000 Part 1 streaming decode engine.
//!
//! Built only as the library embedded in Grok (libgrokj2k): the host codec
//! supplies tier-1 block decoding through the [`capi::mercury_weave`]
//! fn-pointer seam and links the vendored DWT kernels it compiled itself.
//!
//! The decode engine is the weft dataflow graph:
//! - [`codec`] — codestream/JP2 parsing
//! - [`decode`] — T2 plan ([`decode::plan`]) and weft decode graph
//!   ([`decode::graph`]), plus the code-block stripe decoder
//! - [`dwt`] — incremental inverse-DWT synthesis engines and trees
//! - [`weft`] — the non-blocking dataflow runtime (rings, nodes, pool)
//! - [`ffi_dwt`] — bindings to the Highway SIMD lifting kernels, the only
//!   FFI boundary (native SIMD kernels vendored in `native/`)
//! - [`capi`] — the embedding C API (plan/decode handles, read_at + T1 +
//!   row-callback FFI surfaces; header in `include/mercury_capi.h`)
pub mod capi;
pub(crate) mod codec;
pub(crate) mod decode;
pub(crate) mod dwt;
pub(crate) mod ffi_dwt;
pub(crate) mod weft;
