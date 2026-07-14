//! JPEG 2000 Part 1 T2 (codestream/packet) parsing, decode-only.
//!
//! Main header (SIZ/COD/QCD/QCC) is parsed by the host and handed in via the
//! C API; this layer parses only the per-tile SOT/tile-part chain and packet
//! headers.

pub mod packet;
pub mod params;
pub mod tile_geom;
