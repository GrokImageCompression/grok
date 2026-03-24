## Rust Bindings for Grok

FFI bindings generated with [bindgen](https://github.com/rust-lang/rust-bindgen).
The crate wraps the `libgrokj2k` C API and provides raw `unsafe` access to
all public functions and types.

### Prerequisites

- Rust toolchain (stable)
- `pkg-config`
- Grok installed (so that `pkg-config --libs libgrokj2k` works)

If Grok is installed to a non-standard prefix:
```bash
# Linux
export PKG_CONFIG_PATH=/path/to/grok/lib/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/path/to/grok/lib:$LD_LIBRARY_PATH

# macOS
export PKG_CONFIG_PATH=/path/to/grok/lib/pkgconfig:$PKG_CONFIG_PATH
export DYLD_LIBRARY_PATH=/path/to/grok/lib:$DYLD_LIBRARY_PATH
```

**Windows**: The build script currently requires `pkg-config`. On Windows you
can use [pkg-config-lite](https://sourceforge.net/projects/pkgconfiglite/)
or [vcpkg](https://vcpkg.io/) to provide it, with `PKG_CONFIG_PATH` pointing
to your Grok install.

### Build

```
cargo build
```

### Run Tests

```
cargo test --lib --tests
```

### Run Example

```
cargo run --example initialize_grok
```

### Usage

Add the crate as a dependency (local path or publish to a registry):

```rust
use grokj2k_sys::*;
use std::ffi::CStr;

fn main() {
    unsafe {
        grk_initialize(std::ptr::null(), 0, std::ptr::null_mut());
        let version = CStr::from_ptr(grk_version());
        println!("Grok version: {}", version.to_str().unwrap());
        grk_deinitialize();
    }
}
```
