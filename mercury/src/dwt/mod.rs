//! DWT lifting engines — Rust, dispatching to Highway SIMD kernels via FFI,
//! with scalar fallbacks.

pub mod hlift;
pub mod hsyn;
pub mod level_builder;
pub mod synthesis;
pub mod vlift;

/// Kernel IDs for the two lifting kernels (passed to the Highway bridge).
pub const KERNEL_W9X7: u8 = 0;
pub const KERNEL_W5X3: u8 = 1;

/// SIMD sample-alignment (elements) for the lifting buffers.
pub const ALIGN_SAMPLES16: i32 = 16;
pub const ALIGN_SAMPLES32: i32 = 8;
