# native/ — vendored C++ (the only non-Rust code)

The Highway SIMD DWT lifting kernels and their `extern "C"` bridge. The Rust
side of the boundary is [src/ffi_dwt.rs](../src/ffi_dwt.rs).

These sources are **not compiled by this crate** — the embedding codec (Grok)
compiles them into `libgrokj2k` against its own Highway tree and resolves the
kernel symbols at the final C++ link, so [build.rs](../build.rs) emits no C++.

| Path | What |
|------|------|
| `transform/mercury_dwt_bridge.cpp` | `extern "C"` bridge forwarding the `mercury_*` FFI surface to the kernels |
| `transform/hwy_dwt.cpp` | The vlift/hlift/interleave SIMD kernels (Highway `foreach_target` dynamic dispatch) |
| `transform/dwt_kernels.h` | The `merc_lifting_step` struct + `extern` decls of the dispatched kernels, used by the bridge |
| `mercury_dwt.h` | C prototypes matching `ffi_dwt.rs` |

All native C++ lives in one namespace, `mercury_native`.

The `#include "transform/..."` paths resolve against `-Inative` (Grok's CMake
adds `native/` to these sources' include dirs, alongside its own `highway/`).

The kernels must be compiled with the documented flags
(`-O2 -std=gnu++23 -msse2 -DHWY_STATIC_DEFINE`, `NDEBUG`)
— the f32 9/7 path is sensitive to FP contraction, so re-run the decode gates
after any change here.
