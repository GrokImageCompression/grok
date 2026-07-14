//! Mercury builds only as Grok's embedded decode engine: the host compiles the
//! vendored native/ kernels against its own Highway tree (provenance in
//! native/README.md) and resolves the symbols at the final C++ link. This crate
//! emits no C++ — build.rs just tracks native/ so a kernel edit re-triggers cargo.

fn main() {
    println!("cargo:rerun-if-changed=native");
}
