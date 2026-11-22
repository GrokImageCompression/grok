use crate::codec::packet_order::{
    PacketSlot, chart_prec_bands, chart_precinct_grids, chart_precinct_positions, order_packets,
};
use crate::codec::params::{CodParams, SizParams};
use crate::codec::tile_geom::TileGeom;

use std::sync::Mutex;

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
    precincts: Vec<PrecinctCoder>,
    // [comp][res] index in `precincts` of the resolution's first precinct
    first_precinct: Vec<Vec<usize>>,
    // [precinct] indices in `order` of its packets
    precinct_packets: Vec<Vec<usize>>,
    threads: usize,
}

struct PrecinctPackets {
    bytes: Vec<u8>,
    // (bytes written, packet length) per packet, in layer order
    lengths: Vec<(usize, usize)>,
}

impl TilePacketWriter {
    pub fn warp(cod: &CodParams, siz: &SizParams, geom: &TileGeom, threads: usize) -> Self {
        let grids = chart_precinct_grids(cod, geom);
        let positions = chart_precinct_positions(cod, siz, geom, &grids);
        let order = order_packets(cod, &grids, &positions).expect("encoder POCs are valid");
        let mut precincts = Vec::new();
        let mut first_precinct = Vec::with_capacity(grids.len());
        for (c, comp_grids) in grids.iter().enumerate() {
            let style = &cod.comps[c];
            let mut comp_first = Vec::with_capacity(comp_grids.len());
            for (r, &(px0, py0, npx, npy)) in comp_grids.iter().enumerate() {
                comp_first.push(precincts.len());
                let res = &geom.components[c].resolutions[r];
                let ps = style.precinct_span(r as u8);
                let (block_w, block_h) = style.block_span(r as u8);
                for py in py0..py0 + npy {
                    for px in px0..px0 + npx {
                        precincts.push(PrecinctCoder::warp(chart_prec_bands(
                            res, px, py, ps, r, block_w, block_h,
                        )));
                    }
                }
            }
            first_precinct.push(comp_first);
        }
        let mut precinct_packets = vec![Vec::new(); precincts.len()];
        for (index, slot) in order.iter().enumerate() {
            let precinct =
                first_precinct[slot.comp as usize][slot.res as usize] + slot.prec as usize;
            precinct_packets[precinct].push(index);
        }
        Self {
            order,
            precincts,
            first_precinct,
            precinct_packets,
            threads,
        }
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
        let order = &self.order;
        let precinct_count = self.precincts.len();
        let write_precinct = |coder: &mut PrecinctCoder, packets: &[usize]| {
            let mut written = PrecinctPackets {
                bytes: Vec::new(),
                lengths: Vec::with_capacity(packets.len()),
            };
            let mut sources: Vec<BandPacketSource> = Vec::with_capacity(3);
            for &index in packets {
                let slot = order[index];
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
                let start = written.bytes.len();
                let length =
                    coder.write_packet(slot.layer, &sources, &mut written.bytes, with_body);
                written.lengths.push((written.bytes.len() - start, length));
            }
            written
        };
        let mut tasks: Vec<(usize, &mut PrecinctCoder)> =
            self.precincts.iter_mut().enumerate().collect();
        tasks.sort_by_key(|(_, coder)| coder.block_count());
        let tasks = Mutex::new(tasks);
        let precinct_packets = &self.precinct_packets;
        let run_tasks = || {
            let mut done = Vec::new();
            loop {
                let Some((precinct, coder)) = tasks.lock().unwrap().pop() else {
                    return done;
                };
                done.push((precinct, write_precinct(coder, &precinct_packets[precinct])));
            }
        };
        let helpers = self.threads.min(precinct_count).saturating_sub(1);
        let mut written: Vec<Option<PrecinctPackets>> = std::iter::repeat_with(|| None)
            .take(precinct_count)
            .collect();
        std::thread::scope(|scope| {
            let handles: Vec<_> = (0..helpers).map(|_| scope.spawn(run_tasks)).collect();
            let mut finished = run_tasks();
            for handle in handles {
                finished.extend(handle.join().expect("packet thread panicked"));
            }
            for (precinct, packets) in finished {
                written[precinct] = Some(packets);
            }
        });

        let mut cursors = vec![(0usize, 0usize); written.len()];
        for slot in order {
            let precinct =
                self.first_precinct[slot.comp as usize][slot.res as usize] + slot.prec as usize;
            let packets = written[precinct]
                .as_ref()
                .expect("every precinct is written");
            let (byte, packet) = &mut cursors[precinct];
            let (bytes_written, length) = packets.lengths[*packet];
            out.extend_from_slice(&packets.bytes[*byte..*byte + bytes_written]);
            *byte += bytes_written;
            *packet += 1;
            packet_written(*slot, length);
        }
    }
}
