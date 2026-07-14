//! Weft — the deadlock-free pull dataflow engine (docs/weft-design.md).
//!
//! The decode pipeline is a small static graph of nodes joined by bounded
//! SPSC rings; a node runs as a *non-blocking slice* and is rescheduled by
//! peer notifications. Rings are the demand ("pull") and the memory bound;
//! no-blocking plus sufficiency-sized rings make deadlock impossible by
//! construction — hence no shed/wwid/working-wait machinery.
//!
//! Design target: decode ESP_028011_2055_RED.JP2 (28260×52834, single tile)
//! bit-exact in ≤ 10 s wall, ≤ 47 MB max RSS.
//!
//! Three primitives:
//! - [`ring::ring`] — SPSC slot ring, zero-copy hand-off, capacity =
//!   backpressure
//! - [`node::Node`] + [`node::NodeState`] — non-blocking slices and the
//!   SCHEDULED/DIRTY wakeup word (model-checked in tests/weft_loom.rs)
//! - [`rt::Runtime`] — minimal pool with tail-chaining

pub mod node;
pub mod ring;
pub mod rt;

pub use node::Node;
