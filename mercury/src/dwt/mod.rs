//! DWT lifting engines — Rust, dispatching to Highway SIMD kernels via FFI,
//! with scalar fallbacks.

pub mod fixed97;
pub mod hlift;
pub mod hsyn;
pub mod level_builder;
pub mod synthesis;
pub mod vlift;

/// Kernel IDs for the two lifting kernels (passed to the Highway bridge).
pub const KERNEL_W9X7: u8 = 0;
pub const KERNEL_W5X3: u8 = 1;

/// SIMD sample-alignment (elements) for the lifting buffers, queried from the
/// dispatched kernels so layout granularity matches their vector width.
pub fn align_samples32() -> i32 {
    static LANES: std::sync::OnceLock<i32> = std::sync::OnceLock::new();
    *LANES.get_or_init(|| unsafe { crate::ffi_dwt::mercury_hwy_lanes_32() })
}

pub fn align_samples16() -> i32 {
    2 * align_samples32()
}
