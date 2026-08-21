//! Windowed-decode geometry: padded resolution and band windows, mirroring
//! grok's ResWindow::getPaddedBandWindow (T.800 equation B-15 per axis plus
//! filter padding), so mercury's precinct-skip set and synthesis margins are
//! at least as wide as the classic pipeline's proven-sufficient ones.
//!
//! All rectangles are canvas-anchored, like [`crate::codec::tile_geom::Dims`]:
//! a band window lives on that band's coordinate plane.
//!
//! Exactness argument: a synthesis engine built on a padded window treats the
//! window edges as band boundaries (symmetric extension), which fabricates
//! values there — but a fabricated edge value influences outputs no further
//! than the lifting support, and the padding exceeds the support accumulated
//! down the level chain, so samples inside the unpadded window come out
//! bit-exact. At true band edges the clip makes the window edge the real
//! boundary and the extension is correct, not fabricated.

use crate::codec::tile_geom::Dims;

/// Classic's per-side padding of a resolution window: 4 × getFilterPad,
/// where getFilterPad is 1 (reversible 5/3) or 2 (irreversible 9/7).
pub fn resolution_padding(reversible: bool) -> u32 {
    4 * if reversible { 1 } else { 2 }
}

/// One coordinate of equation B-15: the band plane position of a canvas
/// coordinate after `num_decomps` decompositions (`high` = 1 for the high
/// half of the split along this axis).
fn band_coordinate(coordinate: u32, num_decomps: u8, high: u32) -> u32 {
    if num_decomps == 0 {
        return coordinate;
    }
    let offset = (1u32 << (num_decomps - 1)) * high;
    if coordinate <= offset {
        0
    } else {
        (coordinate - offset).div_ceil(1u32 << num_decomps)
    }
}

/// Map a canvas rectangle onto the plane of band `orientation`
/// (0=LL, 1=HL, 2=LH, 3=HH) after `num_decomps` decompositions.
pub fn band_window(rect: Dims, num_decomps: u8, orientation: u8) -> Dims {
    let high_x = (orientation & 1) as u32;
    let high_y = (orientation >> 1) as u32;
    Dims {
        x0: band_coordinate(rect.x0, num_decomps, high_x),
        y0: band_coordinate(rect.y0, num_decomps, high_y),
        x1: band_coordinate(rect.x1, num_decomps, high_x),
        y1: band_coordinate(rect.y1, num_decomps, high_y),
    }
}

fn grow_and_clip(mut rect: Dims, pad: u32, bounds: Dims) -> Dims {
    rect.x0 = rect.x0.saturating_sub(pad).max(bounds.x0);
    rect.y0 = rect.y0.saturating_sub(pad).max(bounds.y0);
    rect.x1 = rect.x1.saturating_add(pad).min(bounds.x1);
    rect.y1 = rect.y1.saturating_add(pad).min(bounds.y1);
    rect
}

/// The padded window on resolution `res`'s plane: the canvas window mapped
/// down, grown by the filter padding, clipped to the resolution's dims, and
/// parity-aligned to them (the interleaved band indexing of the partial
/// synthesis only lines up when window and resolution share x0/y0 parity).
pub fn padded_res_window(
    canvas_window: Dims,
    res_dims: Dims,
    num_levels: u8,
    res: u8,
    reversible: bool,
) -> Dims {
    let decomps = num_levels - res;
    let mut w = band_window(canvas_window, decomps, 0);
    w = grow_and_clip(w, resolution_padding(reversible), res_dims);
    if w.x0 > res_dims.x0 && ((w.x0 ^ res_dims.x0) & 1) != 0 {
        w.x0 -= 1;
    }
    if w.y0 > res_dims.y0 && ((w.y0 ^ res_dims.y0) & 1) != 0 {
        w.y0 -= 1;
    }
    w
}

/// Intersection, empty results collapsing to a zero-area rect at the corner.
pub fn intersect(a: Dims, b: Dims) -> Dims {
    let x0 = a.x0.max(b.x0);
    let y0 = a.y0.max(b.y0);
    Dims {
        x0,
        y0,
        x1: a.x1.min(b.x1).max(x0),
        y1: a.y1.min(b.y1).max(y0),
    }
}

pub fn intersects(a: &Dims, b: &Dims) -> bool {
    !a.is_empty() && !b.is_empty() && a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1
}

/// Padded decode windows of one tile component, on each plane the plan and
/// graph consume.
#[derive(Debug, Clone)]
pub struct TileCompWindow {
    /// Per resolution `r`: the padded window on that resolution's plane,
    /// clipped to the resolution dims. Empty when the window misses the tile.
    pub res: Vec<Dims>,
    /// Per resolution `r`, per subband (same order as the geometry's
    /// `subbands`): the padded window on that band's plane. These drive the
    /// precinct skip and the T1 block ranges.
    pub bands: Vec<Vec<Dims>>,
}

/// Compute a tile component's padded windows from the canvas window
/// (unreduced, pre-clipped to the tile component like classic's
/// unreducedBounds_). `resolutions` pairs each resolution's dims with its
/// subbands' (orientation, dims); at least two resolutions (mercury rejects
/// zero-level images before this).
///
/// Resolution r's band windows are the splits of resolution r's own padded
/// window (they live one decomposition below it); resolution 0's single LL
/// band splits off resolution 1's, exactly as classic's resno-0 ResWindow
/// does.
pub fn chart_tile_comp_window(
    canvas_window: Dims,
    resolutions: &[(Dims, Vec<(u8, Dims)>)],
    num_levels: u8,
    reversible: bool,
) -> TileCompWindow {
    let padded = |r: usize| {
        padded_res_window(
            canvas_window,
            resolutions[r].0,
            num_levels,
            r as u8,
            reversible,
        )
    };
    let mut res = Vec::with_capacity(resolutions.len());
    let mut bands = Vec::with_capacity(resolutions.len());
    for (r, (_, band_dims)) in resolutions.iter().enumerate() {
        let w = padded(r);
        let split_source = if r == 0 { padded(1) } else { w };
        let bw = band_dims
            .iter()
            .map(|(orientation, dims)| intersect(band_window(split_source, 1, *orientation), *dims))
            .collect();
        res.push(w);
        bands.push(bw);
    }
    TileCompWindow { res, bands }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn d(x0: u32, y0: u32, x1: u32, y1: u32) -> Dims {
        Dims { x0, y0, x1, y1 }
    }

    #[test]
    fn band_coordinates_follow_b15() {
        // one decomposition: low half is ceildiv2, high half shifts by 2^0
        assert_eq!(band_coordinate(11, 1, 0), 6);
        assert_eq!(band_coordinate(11, 1, 1), 5);
        // offset at or above the coordinate clamps to 0
        assert_eq!(band_coordinate(1, 2, 1), 0);
        assert_eq!(band_coordinate(0, 3, 0), 0);
    }

    #[test]
    fn padded_window_grows_clips_and_parity_aligns() {
        // window x 100..200 at full res (decomps 0), reversible pad 4
        let res = d(1, 1, 1000, 1000);
        let w = padded_res_window(d(100, 100, 200, 200), res, 3, 3, true);
        // grown by 4 each side: 96..204, then x0 96 has different parity than
        // res x0 1, so it aligns down to 95
        assert_eq!((w.x0, w.y0, w.x1, w.y1), (95, 95, 204, 204));

        // clipped at the resolution edge: the true boundary wins
        let w = padded_res_window(d(1, 1, 10, 10), res, 3, 3, true);
        assert_eq!((w.x0, w.y0), (1, 1));
    }

    #[test]
    fn irreversible_padding_is_twice_as_wide() {
        let res = d(0, 0, 1000, 1000);
        let w = padded_res_window(d(100, 100, 200, 200), res, 3, 3, false);
        assert_eq!((w.x0, w.x1), (92, 208));
    }

    #[test]
    fn deeper_resolutions_scale_the_window_down() {
        let res = d(0, 0, 125, 125);
        // 3 levels, res 0: window /8 then padded
        let w = padded_res_window(d(64, 64, 96, 96), res, 3, 0, true);
        assert_eq!((w.x0, w.y0, w.x1, w.y1), (4, 4, 16, 16));
    }

    #[test]
    fn band_windows_split_the_next_resolution() {
        // full-res 0..100, window 40..60, 1 level
        let resolutions = vec![
            (d(0, 0, 50, 50), vec![(0u8, d(0, 0, 50, 50))]),
            (
                d(0, 0, 100, 100),
                vec![
                    (1u8, d(0, 0, 50, 50)),
                    (2u8, d(0, 0, 50, 50)),
                    (3u8, d(0, 0, 50, 50)),
                ],
            ),
        ];
        let w = chart_tile_comp_window(d(40, 40, 60, 60), &resolutions, 1, true);
        // res 1 padded: 36..64; its HL split per B-15
        assert_eq!(w.bands[1][0], d(18, 18, 32, 32));
        // res 0's LL band equals the LL split of res 1's padded window
        assert_eq!(w.bands[0][0], d(18, 18, 32, 32));
        // res 0's own padded window carries fresh margin around that band
        assert_eq!(w.res[0], d(16, 16, 34, 34));
        // and contains it, which is what lets the levels chain
        assert!(
            w.res[0].x0 <= w.bands[0][0].x0 && w.bands[0][0].x1 <= w.res[0].x1,
            "LL band window must sit inside the lower resolution's padded window"
        );
    }
}
