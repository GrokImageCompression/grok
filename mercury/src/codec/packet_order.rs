use crate::codec::params::{
    CodParams, PrecinctSize, ProgressionOrder, ProgressionVolume, SizParams,
};
use crate::codec::tile_geom::{ResolutionGeom, TileGeom};
use crate::decode::DecodeError;

// (px0, py0, npx, npy) per (comp, res), raster index (py - py0) * npx + (px - px0)
pub fn chart_precinct_grids(cod: &CodParams, geom: &TileGeom) -> Vec<Vec<(u32, u32, u32, u32)>> {
    geom.components
        .iter()
        .enumerate()
        .map(|(c, tc)| {
            let style = &cod.comps[c];
            tc.resolutions
                .iter()
                .take(style.num_levels as usize + 1)
                .enumerate()
                .map(|(r, res)| {
                    if res.dims.is_empty() {
                        return (0, 0, 0, 0);
                    }
                    let ps = style.precinct_span(r as u8);
                    let px0 = res.dims.x0 / ps.width;
                    let py0 = res.dims.y0 / ps.height;
                    let px1 = (res.dims.x1 - 1) / ps.width;
                    let py1 = (res.dims.y1 - 1) / ps.height;
                    (px0, py0, px1 - px0 + 1, py1 - py0 + 1)
                })
                .collect()
        })
        .collect()
}

/// Per-band block range of one precinct (band-global grid indices).
#[derive(Clone)]
pub struct PrecBand {
    pub bx0: u32,
    pub by0: u32,
    pub nbw: u32,
    pub nbh: u32,
}

/// Per-band block ranges of the precinct at grid position (px, py). Both the
/// precinct and code-block grids are anchored at the canvas origin, so a block
/// never straddles two precincts.
pub fn chart_prec_bands(
    res: &ResolutionGeom,
    px: u32,
    py: u32,
    ps: PrecinctSize,
    r: usize,
    block_w: u32,
    block_h: u32,
) -> Vec<PrecBand> {
    // for r > 0 the precinct halves into the band domain
    let band_scale = if r == 0 { 1 } else { 2 };
    let bpw = ps.width / band_scale;
    let bph = ps.height / band_scale;
    res.subbands
        .iter()
        .map(|sb| {
            let rx0 = (px * bpw).max(sb.dims.x0);
            let ry0 = (py * bph).max(sb.dims.y0);
            let rx1 = ((px + 1) * bpw).min(sb.dims.x1);
            let ry1 = ((py + 1) * bph).min(sb.dims.y1);
            if rx0 >= rx1 || ry0 >= ry1 {
                return PrecBand {
                    bx0: 0,
                    by0: 0,
                    nbw: 0,
                    nbh: 0,
                };
            }
            let first_bx = sb.dims.x0 / block_w;
            let first_by = sb.dims.y0 / block_h;
            let bx0 = rx0 / block_w - first_bx;
            let by0 = ry0 / block_h - first_by;
            let bx1 = (rx1 - 1) / block_w - first_bx;
            let by1 = (ry1 - 1) / block_h - first_by;
            PrecBand {
                bx0,
                by0,
                nbw: bx1 - bx0 + 1,
                nbh: by1 - by0 + 1,
            }
        })
        .collect()
}

pub fn chart_precinct_positions(
    cod: &CodParams,
    siz: &SizParams,
    geom: &TileGeom,
    grids: &[Vec<(u32, u32, u32, u32)>],
) -> Vec<Vec<Vec<(u64, u64)>>> {
    grids
        .iter()
        .enumerate()
        .map(|(c, comp_grids)| {
            let dx = siz.components[c].xr_siz as u64;
            let dy = siz.components[c].yr_siz as u64;
            let style = &cod.comps[c];
            comp_grids
                .iter()
                .enumerate()
                .map(|(r, &(px0, py0, npx, npy))| {
                    let res = &geom.components[c].resolutions[r];
                    let ps = style.precinct_span(r as u8);
                    let scale = 1u64 << (style.num_levels as u32 - r as u32);
                    let mut positions = Vec::with_capacity((npx * npy) as usize);
                    for py in py0..py0 + npy {
                        for px in px0..px0 + npx {
                            positions.push((
                                prec_position(
                                    py,
                                    py0,
                                    ps.height,
                                    res.dims.y0,
                                    geom.tile_y0,
                                    scale * dy,
                                ),
                                prec_position(
                                    px,
                                    px0,
                                    ps.width,
                                    res.dims.x0,
                                    geom.tile_x0,
                                    scale * dx,
                                ),
                            ));
                        }
                    }
                    positions
                })
                .collect()
        })
        .collect()
}

/// One packet in progression order.
#[derive(Clone, Copy)]
pub struct PacketSlot {
    pub comp: u16,
    pub res: u8,
    pub layer: u16,
    /// Precinct raster index within its (comp, res) precinct grid.
    pub prec: u32,
}

pub fn order_packets(
    cod: &CodParams,
    grids: &[Vec<(u32, u32, u32, u32)>],
    positions: &[Vec<Vec<(u64, u64)>>],
) -> Result<Vec<PacketSlot>, DecodeError> {
    let num_comps = grids.len();
    let max_res = grids.iter().map(Vec::len).max().unwrap_or(0);
    let volumes = chart_volumes(cod, num_comps, max_res)?;
    // Volumes overlap, so a packet an earlier one already emitted is skipped
    // (T.800 A.6.6); with a single volume nothing can repeat.
    let mut emitted: Vec<Vec<Vec<bool>>> = if volumes.len() > 1 {
        grids
            .iter()
            .map(|comp_grids| {
                comp_grids
                    .iter()
                    .map(|&(_, _, npx, npy)| {
                        vec![false; (npx * npy) as usize * cod.num_layers as usize]
                    })
                    .collect()
            })
            .collect()
    } else {
        Vec::new()
    };
    let mut pkts: Vec<PacketSlot> = Vec::new();
    let mut vol_pkts: Vec<([u64; 5], PacketSlot)> = Vec::new();
    for vol in &volumes {
        vol_pkts.clear();
        for c in vol.comp_s as usize..vol.comp_e as usize {
            for r in vol.res_s as usize..(vol.res_e as usize).min(grids[c].len()) {
                let (px0, py0, npx, _) = grids[c][r];
                if npx == 0 {
                    continue;
                }
                for (prec, &(ypos, xpos)) in positions[c][r].iter().enumerate() {
                    let py = py0 + prec as u32 / npx;
                    let px = px0 + prec as u32 % npx;
                    for l in 0..vol.lay_e {
                        let key = progression_key(
                            vol.order, c as u64, r as u64, l as u64, py as u64, px as u64, ypos,
                            xpos,
                        );
                        vol_pkts.push((
                            key,
                            PacketSlot {
                                comp: c as u16,
                                res: r as u8,
                                layer: l,
                                prec: prec as u32,
                            },
                        ));
                    }
                }
            }
        }
        vol_pkts.sort_by_key(|p| p.0);
        for (_, pkt) in vol_pkts.drain(..) {
            if !emitted.is_empty() {
                let slot = &mut emitted[pkt.comp as usize][pkt.res as usize]
                    [pkt.prec as usize * cod.num_layers as usize + pkt.layer as usize];
                if *slot {
                    continue;
                }
                *slot = true;
            }
            pkts.push(pkt);
        }
    }
    Ok(pkts)
}

/// The tile's progression volume list, clamped and rejected the way classic
/// grok's `finalizePocs` does at the end of the main header. No POC is one
/// volume covering every layer, resolution and component in the COD order.
fn chart_volumes(
    cod: &CodParams,
    num_comps: usize,
    max_res: usize,
) -> Result<Vec<ProgressionVolume>, DecodeError> {
    if cod.pocs.is_empty() {
        return Ok(vec![ProgressionVolume {
            res_s: 0,
            comp_s: 0,
            lay_e: cod.num_layers,
            res_e: max_res as u8,
            comp_e: num_comps as u16,
            order: cod.order,
        }]);
    }
    let mut volumes = Vec::with_capacity(cod.pocs.len());
    for vol in &cod.pocs {
        let mut vol = *vol;
        vol.lay_e = vol.lay_e.min(cod.num_layers);
        vol.res_e = vol.res_e.min(max_res as u8);
        vol.comp_e = vol.comp_e.min(num_comps as u16);
        if vol.res_s >= vol.res_e || vol.comp_s >= vol.comp_e || vol.lay_e == 0 {
            return Err(DecodeError::Logic(format!(
                "plan: empty POC volume, res {}..{} comp {}..{} layers {}",
                vol.res_s, vol.res_e, vol.comp_s, vol.comp_e, vol.lay_e
            )));
        }
        volumes.push(vol);
    }
    Ok(volumes)
}

/// Sort key of one packet under a progression order (T.800 B.12.1.1-5).
#[allow(clippy::too_many_arguments)]
fn progression_key(
    order: ProgressionOrder,
    comp: u64,
    res: u64,
    layer: u64,
    py: u64,
    px: u64,
    ypos: u64,
    xpos: u64,
) -> [u64; 5] {
    match order {
        ProgressionOrder::Lrcp => [layer, res, comp, py, px],
        ProgressionOrder::Rlcp => [res, layer, comp, py, px],
        ProgressionOrder::Rpcl => [res, ypos, xpos, comp, layer],
        ProgressionOrder::Pcrl => [ypos, xpos, comp, res, layer],
        ProgressionOrder::Cprl => [comp, ypos, xpos, res, layer],
    }
}

/// Reference-grid position of one precinct along an axis, the sort key of the
/// position-ordered progressions. A first row or column the tile edge clips
/// keys on the tile origin (T.800 B.12, the "y = ty0" case), which is what puts
/// every resolution's first precinct on one key; the resolution origin misses
/// the precinct grid exactly when it is clipped.
pub(crate) fn prec_position(
    index: u32,
    first_index: u32,
    prec_span: u32,
    res_origin: u32,
    tile_origin: u32,
    projection: u64,
) -> u64 {
    if index == first_index && !res_origin.is_multiple_of(prec_span) {
        tile_origin as u64
    } else {
        index as u64 * prec_span as u64 * projection
    }
}
