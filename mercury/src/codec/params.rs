//! JPEG 2000 main-header parameters (SIZ/COD/QCD/QCC), decode-only.
//!
//! Coding parameters the host (grok) parsed from the main header and handed to
//! mercury across the C API; mercury no longer parses the main header itself.

// ─── Progression orders ──────────────────────────────────────────────────────

/// Progression order (Corder).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ProgressionOrder {
    Lrcp = 0,
    Rlcp = 1,
    Rpcl = 2,
    Pcrl = 3,
    Cprl = 4,
}

impl ProgressionOrder {
    /// From host-supplied order (0=LRCP..4=CPRL).
    pub fn from_host(v: u8) -> Result<Self, ()> {
        match v {
            0 => Ok(Self::Lrcp),
            1 => Ok(Self::Rlcp),
            2 => Ok(Self::Rpcl),
            3 => Ok(Self::Pcrl),
            4 => Ok(Self::Cprl),
            _ => Err(()),
        }
    }
}

// ─── Block coding modes ──────────────────────────────────────────────────────

/// Block coder mode flags (Cmodes).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CodingModes(pub u32);

impl CodingModes {
    // draft whitelists exactly these four; others (BYPASS/RESTART, HT)
    // would misparse and are rejected as leftover bits.
    pub const RESET: u32 = 2;
    pub const CAUSAL: u32 = 8;
    pub const ERTERM: u32 = 16;
    pub const SEGMARK: u32 = 32;
}

// ─── SIZ parameters ─────────────────────────────────────────────────────────

/// Per-component information from the SIZ marker.
#[derive(Debug, Clone)]
pub struct SizComponent {
    pub precision: u8,
    pub is_signed: bool,
    pub xr_siz: u8,
    pub yr_siz: u8,
}

/// Parsed SIZ marker segment.
#[derive(Debug, Clone)]
pub struct SizParams {
    /// Canvas width (Xsiz).
    pub x_siz: u32,
    /// Canvas height (Ysiz).
    pub y_siz: u32,
    /// Image horizontal origin (XOsiz).
    pub x_o_siz: u32,
    /// Image vertical origin (YOsiz).
    pub y_o_siz: u32,
    /// Tile width (XTsiz).
    pub xt_siz: u32,
    /// Tile height (YTsiz).
    pub yt_siz: u32,
    /// Tile horizontal origin (XTOsiz).
    pub xt_o_siz: u32,
    /// Tile vertical origin (YTOsiz).
    pub yt_o_siz: u32,
    /// Per-component information.
    pub components: Vec<SizComponent>,
}

impl SizParams {
    /// Number of components.
    pub fn comp_count(&self) -> usize {
        self.components.len()
    }

    /// Number of tiles in X direction.
    pub fn tiles_across(&self) -> u32 {
        if self.xt_siz == 0 {
            return 1;
        }
        (self.x_siz - self.xt_o_siz).div_ceil(self.xt_siz)
    }

    /// Number of tiles in Y direction.
    pub fn tiles_down(&self) -> u32 {
        if self.yt_siz == 0 {
            return 1;
        }
        (self.y_siz - self.yt_o_siz).div_ceil(self.yt_siz)
    }

    /// Image width (accounting for origin).
    pub fn width(&self) -> u32 {
        self.x_siz - self.x_o_siz
    }

    /// Image height (accounting for origin).
    pub fn height(&self) -> u32 {
        self.y_siz - self.y_o_siz
    }
}

// ─── COD parameters ─────────────────────────────────────────────────────────

/// Precinct size for one resolution level.
#[derive(Debug, Clone, Copy)]
pub struct PrecinctSize {
    /// Precinct width (power of 2).
    pub width: u32,
    /// Precinct height (power of 2).
    pub height: u32,
}

/// Parsed COD marker segment (coding style default).
#[derive(Debug, Clone)]
pub struct CodParams {
    /// Progression order.
    pub order: ProgressionOrder,
    /// Number of quality layers.
    pub num_layers: u16,
    /// Multiple component transform (YCC).
    pub use_ycc: bool,
    /// Number of DWT decomposition levels.
    pub num_levels: u8,
    /// Code-block width (power of 2, e.g. 64).
    pub block_width: u32,
    /// Code-block height (power of 2, e.g. 64).
    pub block_height: u32,
    /// Block coding mode flags.
    pub modes: CodingModes,
    /// Reversible transform.
    pub reversible: bool,
    /// Use SOP markers.
    pub use_sop: bool,
    /// Use EPH markers.
    pub use_eph: bool,
    /// Custom precinct sizes per resolution (level 0=LL up to NL).
    /// Empty ⇒ default 2^15 × 2^15.
    pub precincts: Vec<PrecinctSize>,
}

impl CodParams {
    /// Precinct size for a resolution level (0=full res); default 2^15×2^15
    /// if no custom precincts.
    pub fn precinct_span(&self, res_level: u8) -> PrecinctSize {
        if self.precincts.is_empty() {
            PrecinctSize {
                width: 1 << 15,
                height: 1 << 15,
            }
        } else {
            let idx = res_level as usize;
            if idx < self.precincts.len() {
                self.precincts[idx]
            } else {
                *self.precincts.last().unwrap()
            }
        }
    }
}


// ─── QCD parameters ─────────────────────────────────────────────────────────

/// Quantization style.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QuantStyle {
    /// Reversible: no quantization, just bit-shift.
    Reversible,
    /// Irreversible, derived from LL band.
    Derived,
    /// Irreversible, expounded: one step per band.
    Expounded,
}

/// Parsed QCD marker segment (quantization default).
#[derive(Debug, Clone)]
pub struct QcdParams {
    /// Number of guard bits.
    pub guard_bits: u8,
    /// Quantization style.
    pub style: QuantStyle,
    /// For reversible: ranging exponents (epsilon values), per band.
    pub ranges: Vec<u8>,
    /// For irreversible: step sizes, per band.
    pub steps: Vec<f32>,
}

