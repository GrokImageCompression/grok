//! Decode pipeline — orchestrates the full JPEG 2000 decode path.
//!
//! Ties together: host-supplied main header → tile geometry → packet parsing
//! ([`plan`]) → T1 block decode ([`stripe_decoder`]) → DWT → output assembly,
//! all wired as the weft graph in [`graph`].

pub mod graph;
pub mod plan;
pub mod read_at;
pub mod stripe_decoder;

pub use read_at::ReadAt;

/// Errors during decoding.
///
/// The payload is read only through derived `Debug` (which the dead-code lint
/// doesn't count): the C API formats it with `{:?}` into `err_buf` — the
/// "plan rejected (reason)" string the host logs — so it carries real
/// diagnostic value despite looking unread.
#[derive(Debug)]
#[allow(dead_code)]
pub enum DecodeError {
    /// Logical error in decode pipeline.
    Logic(String),
}
