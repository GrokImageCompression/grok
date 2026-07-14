//! Tile geometry computation (ITU-T T.800 §B canvas/tiling arithmetic).
//!
//! Resolution/subband/block-grid dimensions from SIZ and COD, needed to size
//! tag trees for packet parsing.

use crate::codec::params::{CodParams, SizParams};

/// Dimensions of a rectangle on the canvas.
#[derive(Debug, Clone, Copy, Default)]
pub struct Dims {
    pub x0: u32,
    pub y0: u32,
    pub x1: u32,
    pub y1: u32,
}

impl Dims {
    pub fn width(&self) -> u32 {
        self.x1.saturating_sub(self.x0)
    }
    pub fn height(&self) -> u32 {
        self.y1.saturating_sub(self.y0)
    }
    pub fn is_empty(&self) -> bool {
        self.x1 <= self.x0 || self.y1 <= self.y0
    }
}

/// A subband within a resolution level.
#[derive(Debug, Clone)]
pub struct SubbandGeom {
    /// 0=LL (only at res 0), 1=HL, 2=LH, 3=HH.
    pub band_type: u8,
    /// Subband dimensions on the canvas.
    pub dims: Dims,
    /// Block-grid dimensions (blocks in x and y).
    pub blocks_wide: u32,
    pub blocks_high: u32,
}

/// A resolution level within a tile-component.
#[derive(Debug, Clone)]
pub struct ResolutionGeom {
    /// Canvas dimensions at this resolution.
    pub dims: Dims,
    /// 1 subband at res 0, 3 at higher resolutions.
    pub subbands: Vec<SubbandGeom>,
}

/// Complete tile-component geometry for packet parsing.
#[derive(Debug, Clone)]
pub struct TileCompGeom {
    /// Indexed 0 (lowest) to num_levels (highest, full res).
    pub resolutions: Vec<ResolutionGeom>,
}

/// Complete tile geometry.
#[derive(Debug, Clone)]
pub struct TileGeom {
    /// Per-component geometry.
    pub components: Vec<TileCompGeom>,
}

/// Compute tile geometry for a single tile
pub fn chart_tile_geom(siz: &SizParams, cod: &CodParams, tile_idx: u16) -> TileGeom {
    let ntx = siz.tiles_across();
    let tx = tile_idx as u32 % ntx;
    let ty = tile_idx as u32 / ntx;

    // Tile canvas bounds
    let tile_x0 = (siz.xt_o_siz + tx * siz.xt_siz).max(siz.x_o_siz);
    let tile_y0 = (siz.yt_o_siz + ty * siz.yt_siz).max(siz.y_o_siz);
    let tile_x1 = (siz.xt_o_siz + (tx + 1) * siz.xt_siz).min(siz.x_siz);
    let tile_y1 = (siz.yt_o_siz + (ty + 1) * siz.yt_siz).min(siz.y_siz);

    let nc = siz.comp_count() as u16;
    let mut components = Vec::with_capacity(nc as usize);

    for c in 0..nc {
        let comp = &siz.components[c as usize];
        let xr = comp.xr_siz as u32;
        let yr = comp.yr_siz as u32;

        // Component tile bounds (subsampled)
        let comp_x0 = tile_x0.div_ceil(xr);
        let comp_y0 = tile_y0.div_ceil(yr);
        let comp_x1 = tile_x1.div_ceil(xr);
        let comp_y1 = tile_y1.div_ceil(yr);

        let n_levels = cod.num_levels;
        let block_w = cod.block_width;
        let block_h = cod.block_height;

        let mut resolutions = Vec::with_capacity((n_levels + 1) as usize);

        for r in 0..=n_levels {
            let n_l = n_levels - r; // levels below full resolution

            // Resolution canvas bounds
            let res_x0 = comp_x0.div_ceil(1 << n_l);
            let res_y0 = comp_y0.div_ceil(1 << n_l);
            let res_x1 = comp_x1.div_ceil(1 << n_l);
            let res_y1 = comp_y1.div_ceil(1 << n_l);

            let res_dims = Dims {
                x0: res_x0,
                y0: res_y0,
                x1: res_x1,
                y1: res_y1,
            };

            // Precinct grids are recomputed by the T2 plan (decode::plan via
            // cod.precinct_span); tile geometry carries only the canvas and
            // subband/code-block layout the plan can't derive as cheaply.

            let subbands = if r == 0 {
                // LL band only
                let sb_x0 = res_x0;
                let sb_y0 = res_y0;
                let sb_x1 = res_x1;
                let sb_y1 = res_y1;
                // Code-block grid anchored at canvas origin: first block
                // is partial when x0 isn't block-aligned.
                let bw = if sb_x1 > sb_x0 { sb_x1.div_ceil(block_w) - sb_x0 / block_w } else { 0 };
                let bh = if sb_y1 > sb_y0 { sb_y1.div_ceil(block_h) - sb_y0 / block_h } else { 0 };
                vec![SubbandGeom {
                    band_type: 0,
                    dims: Dims {
                        x0: sb_x0,
                        y0: sb_y0,
                        x1: sb_x1,
                        y1: sb_y1,
                    },
                    blocks_wide: bw,
                    blocks_high: bh,
                }]
            } else {
                let n_l_r = n_levels - r; // = NL - r
                let step = 1u64 << n_l_r;
                let denom = 1u64 << (n_l_r + 1);
                let cx0 = comp_x0 as u64;
                let cy0 = comp_y0 as u64;
                let cx1 = comp_x1 as u64;
                let cy1 = comp_y1 as u64;

                let make_subband = |xb: u64, yb: u64, band_type: u8| -> SubbandGeom {
                    let sb_x0 = (cx0.saturating_sub(xb * step)).div_ceil(denom) as u32;
                    let sb_y0 = (cy0.saturating_sub(yb * step)).div_ceil(denom) as u32;
                    let sb_x1 = (cx1.saturating_sub(xb * step)).div_ceil(denom) as u32;
                    let sb_y1 = (cy1.saturating_sub(yb * step)).div_ceil(denom) as u32;
                    let bw = if sb_x1 > sb_x0 { sb_x1.div_ceil(block_w) - sb_x0 / block_w } else { 0 };
                    let bh = if sb_y1 > sb_y0 { sb_y1.div_ceil(block_h) - sb_y0 / block_h } else { 0 };
                    SubbandGeom {
                        band_type,
                        dims: Dims {
                            x0: sb_x0,
                            y0: sb_y0,
                            x1: sb_x1,
                            y1: sb_y1,
                        },
                        blocks_wide: bw,
                        blocks_high: bh,
                    }
                };

                vec![
                    make_subband(1, 0, 1), // HL
                    make_subband(0, 1, 2), // LH
                    make_subband(1, 1, 3), // HH
                ]
            };

            resolutions.push(ResolutionGeom {
                dims: res_dims,
                subbands,
            });
        }

        components.push(TileCompGeom { resolutions });
    }

    TileGeom { components }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hirise_layout() {
        // HiRISE: 28260×52834, 1 comp, 9 levels, 64×64 blocks, single tile
        use crate::codec::params::SizComponent;

        let siz = SizParams {
            x_siz: 28260,
            y_siz: 52834,
            x_o_siz: 0,
            y_o_siz: 0,
            xt_siz: 28260,
            yt_siz: 52834,
            xt_o_siz: 0,
            yt_o_siz: 0,
            components: vec![SizComponent {
                precision: 10,
                is_signed: false,
                xr_siz: 1,
                yr_siz: 1,
            }],
        };

        let cod = CodParams {
            order: crate::codec::params::ProgressionOrder::Lrcp,
            num_layers: 1,
            use_ycc: false,
            num_levels: 9,
            block_width: 64,
            block_height: 64,
            modes: crate::codec::params::CodingModes(0),
            reversible: true,
            use_sop: false,
            use_eph: false,
            precincts: vec![],
        };

        let geom = chart_tile_geom(&siz, &cod, 0);
        assert_eq!(geom.components.len(), 1);
        let tc = &geom.components[0];
        assert_eq!(tc.resolutions.len(), 10); // 0..=9

        // Resolution 9 (full) = 28260×52834
        let full = &tc.resolutions[9];
        assert_eq!(full.dims.width(), 28260);
        assert_eq!(full.dims.height(), 52834);

        // Resolution 0 (lowest) = ceil(28260/512) × ceil(52834/512)
        let lowest = &tc.resolutions[0];
        assert_eq!(lowest.dims.width(), 56); // ceil(28260/512) = 56
        assert_eq!(lowest.dims.height(), 104); // ceil(52834/512) = 104

        // Print all resolutions for inspection
        for (r_idx, r) in tc.resolutions.iter().enumerate() {
            let total_blocks: u32 = r
                .subbands
                .iter()
                .map(|sb| sb.blocks_wide * sb.blocks_high)
                .sum();
            println!(
                "Res {}: {}×{}, {} subbands, {} total blocks",
                r_idx,
                r.dims.width(),
                r.dims.height(),
                r.subbands.len(),
                total_blocks,
            );
        }
    }
}
