use crate::codec::packet_order::{
    PacketSlot, chart_prec_bands, chart_precinct_grids, chart_precinct_positions, order_packets,
};
use crate::codec::params::{CodParams, SizParams};
use crate::codec::tile_geom::TileGeom;

use super::band_coder::CodedBand;
use super::packet::{BandPacketSource, PrecinctCoder};

pub(crate) struct TileBand<'a> {
    pub compno: usize,
    pub coded: &'a CodedBand,
    pub blocks_across: u32,
    pub max_bit_planes: u8,
}

// a resolution's bands are adjacent, in geometry order
pub(crate) struct TileBands<'a> {
    pub bands: Vec<TileBand<'a>>,
    // [comp][res] index of the resolution's first band
    pub first_band: Vec<Vec<usize>>,
}

pub(crate) struct TilePacketWriter {
    order: Vec<PacketSlot>,
    // [comp][res][precinct raster index]
    precincts: Vec<Vec<Vec<PrecinctCoder>>>,
}

impl TilePacketWriter {
    pub fn warp(cod: &CodParams, siz: &SizParams, geom: &TileGeom) -> Self {
        let grids = chart_precinct_grids(cod, geom);
        let positions = chart_precinct_positions(cod, siz, geom, &grids);
        let order = order_packets(cod, &grids, &positions).expect("encoder POCs are valid");
        let precincts = grids
            .iter()
            .enumerate()
            .map(|(c, comp_grids)| {
                let style = &cod.comps[c];
                comp_grids
                    .iter()
                    .enumerate()
                    .map(|(r, &(px0, py0, npx, npy))| {
                        let res = &geom.components[c].resolutions[r];
                        let ps = style.precinct_span(r as u8);
                        let (block_w, block_h) = style.block_span(r as u8);
                        let mut coders = Vec::with_capacity((npx * npy) as usize);
                        for py in py0..py0 + npy {
                            for px in px0..px0 + npx {
                                coders.push(PrecinctCoder::warp(chart_prec_bands(
                                    res, px, py, ps, r, block_w, block_h,
                                )));
                            }
                        }
                        coders
                    })
                    .collect()
            })
            .collect();
        Self { order, precincts }
    }

    // one tile part per run of packets from one component
    pub fn component_runs(&self) -> usize {
        let mut runs = 0;
        let mut previous = None;
        for slot in &self.order {
            if previous != Some(slot.comp) {
                runs += 1;
                previous = Some(slot.comp);
            }
        }
        runs
    }

    // `included` holds the passes each block includes, per band of `bands`
    pub fn write_packets(
        &mut self,
        bands: &TileBands,
        included: &[Vec<u8>],
        out: &mut Vec<u8>,
        with_body: bool,
        mut packet_written: impl FnMut(PacketSlot, usize),
    ) {
        let mut sources: Vec<BandPacketSource> = Vec::with_capacity(3);
        for &slot in &self.order {
            let (c, r) = (slot.comp as usize, slot.res as usize);
            let first = bands.first_band[c][r];
            let count = if r == 0 { 1 } else { 3 };
            sources.clear();
            sources.extend((first..first + count).map(|b| BandPacketSource {
                coded: bands.bands[b].coded,
                blocks_across: bands.bands[b].blocks_across,
                max_bit_planes: bands.bands[b].max_bit_planes,
                included_passes: &included[b],
            }));
            let length = self.precincts[c][r][slot.prec as usize]
                .write_packet(slot.layer, &sources, out, with_body);
            packet_written(slot, length);
        }
    }
}
