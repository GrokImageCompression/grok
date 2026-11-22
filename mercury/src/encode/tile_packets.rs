use crate::codec::packet_order::{
    PacketSlot, chart_prec_bands, chart_precinct_grids, chart_precinct_positions, order_packets,
};
use crate::codec::params::{CodParams, SizParams};
use crate::codec::tile_geom::TileGeom;

use std::ops::Range;
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
    // [precinct] indices in `order` of its packets
    precinct_packets: Vec<Vec<usize>>,
    // [order index] precinct, and the packet's index among the precinct's packets
    packet_positions: Vec<(usize, usize)>,
    threads: usize,
}

struct PacketExtent {
    header: Range<usize>,
    bodies: Range<usize>,
    length: usize,
}

struct PrecinctPackets<'a> {
    headers: Vec<u8>,
    bodies: Vec<&'a [u8]>,
    packets: Vec<PacketExtent>,
}

pub(crate) struct WrittenPacket<'p> {
    pub slot: PacketSlot,
    // counts the body even when its slices were not collected
    pub length: usize,
    pub header: &'p [u8],
    pub bodies: &'p [&'p [u8]],
}

pub(crate) struct WrittenPackets<'a, 'w> {
    order: &'w [PacketSlot],
    packet_positions: &'w [(usize, usize)],
    precincts: Vec<PrecinctPackets<'a>>,
}

impl WrittenPackets<'_, '_> {
    pub fn iter(&self) -> impl Iterator<Item = WrittenPacket<'_>> {
        self.order
            .iter()
            .zip(self.packet_positions)
            .map(|(&slot, &(precinct, packet))| {
                let written = &self.precincts[precinct];
                let extent = &written.packets[packet];
                WrittenPacket {
                    slot,
                    length: extent.length,
                    header: &written.headers[extent.header.clone()],
                    bodies: &written.bodies[extent.bodies.clone()],
                }
            })
    }
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
        let mut packet_positions = Vec::with_capacity(order.len());
        for (index, slot) in order.iter().enumerate() {
            let precinct =
                first_precinct[slot.comp as usize][slot.res as usize] + slot.prec as usize;
            packet_positions.push((precinct, precinct_packets[precinct].len()));
            precinct_packets[precinct].push(index);
        }
        Self {
            order,
            precincts,
            precinct_packets,
            packet_positions,
            threads,
        }
    }

    // `included` holds the passes each block includes, per band of `bands`
    pub fn write_packets<'a>(
        &mut self,
        bands: &TileBands<'a>,
        included: &'a [Vec<u8>],
        with_body: bool,
    ) -> WrittenPackets<'a, '_> {
        let order = &self.order;
        let precinct_count = self.precincts.len();
        let write_precinct = |coder: &mut PrecinctCoder, packets: &[usize]| {
            let mut written = PrecinctPackets {
                headers: Vec::new(),
                bodies: Vec::new(),
                packets: Vec::with_capacity(packets.len()),
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
                let (header_start, bodies_start) = (written.headers.len(), written.bodies.len());
                let length = coder.write_packet(
                    slot.layer,
                    &sources,
                    &mut written.headers,
                    with_body.then_some(&mut written.bodies),
                );
                written.packets.push(PacketExtent {
                    header: header_start..written.headers.len(),
                    bodies: bodies_start..written.bodies.len(),
                    length,
                });
            }
            written
        };
        let mut tasks: Vec<(usize, &mut PrecinctCoder)> =
            self.precincts.iter_mut().enumerate().collect();
        tasks.sort_by_key(|(_, coder)| coder.block_count());
        let precinct_packets = &self.precinct_packets;
        let finished = run_tasks(tasks, self.threads, |(precinct, coder)| {
            (precinct, write_precinct(coder, &precinct_packets[precinct]))
        });
        let mut written: Vec<Option<PrecinctPackets>> = std::iter::repeat_with(|| None)
            .take(precinct_count)
            .collect();
        for (precinct, packets) in finished {
            written[precinct] = Some(packets);
        }
        WrittenPackets {
            order,
            packet_positions: &self.packet_positions,
            precincts: written
                .into_iter()
                .map(|packets| packets.expect("every precinct is written"))
                .collect(),
        }
    }
}

pub(crate) fn run_tasks<T: Send, R: Send>(
    tasks: Vec<T>,
    threads: usize,
    work: impl Fn(T) -> R + Sync,
) -> Vec<R> {
    let count = tasks.len();
    let queue = Mutex::new(tasks.into_iter().enumerate().collect::<Vec<_>>());
    let run = || {
        let mut done = Vec::new();
        loop {
            let Some((index, task)) = queue.lock().unwrap().pop() else {
                return done;
            };
            done.push((index, work(task)));
        }
    };
    let helpers = threads.min(count).saturating_sub(1);
    let mut results: Vec<Option<R>> = std::iter::repeat_with(|| None).take(count).collect();
    std::thread::scope(|scope| {
        let handles: Vec<_> = (0..helpers).map(|_| scope.spawn(run)).collect();
        let mut finished = run();
        for handle in handles {
            finished.extend(handle.join().expect("task thread panicked"));
        }
        for (index, result) in finished {
            results[index] = Some(result);
        }
    });
    results
        .into_iter()
        .map(|result| result.expect("every task ran"))
        .collect()
}

const PLT: u16 = 0xFF58;
// classic's plWriteBufferLen, the Zplt byte and the packet lengths of one PLT segment
const PLT_SEGMENT_PAYLOAD: usize = u16::MAX as usize - 4;

pub(crate) fn plt_markers(lengths: impl Iterator<Item = u32>) -> Vec<u8> {
    let mut markers = Vec::new();
    let mut lplt_positions: Vec<usize> = Vec::new();
    for length in lengths {
        let digits = (u32::BITS - length.leading_zeros()).div_ceil(7);
        let fits = lplt_positions
            .last()
            .is_some_and(|&at| markers.len() - at - 2 + digits as usize <= PLT_SEGMENT_PAYLOAD);
        if !fits {
            markers.extend_from_slice(&PLT.to_be_bytes());
            lplt_positions.push(markers.len());
            let zplt = (lplt_positions.len() - 1) as u8;
            markers.extend_from_slice(&[0, 0, zplt]);
        }
        for digit in (0..digits).rev() {
            let group = (length >> (7 * digit)) as u8 & 0x7F;
            markers.push(if digit == 0 { group } else { group | 0x80 });
        }
    }
    let segment_ends = lplt_positions
        .iter()
        .skip(1)
        .map(|&at| at - 2)
        .chain([markers.len()]);
    for (&at, end) in lplt_positions.iter().zip(segment_ends) {
        markers[at..at + 2].copy_from_slice(&((end - at) as u16).to_be_bytes());
    }
    markers
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::decode::plan::plt_packet_lengths;

    #[test]
    fn plt_markers_split_segments_like_classic_and_decode() {
        // two-byte lengths fill the first segment to exactly 65531 bytes after 32765 of them
        let lengths: Vec<u32> = (0..40_000u32)
            .map(|i| 128 + i % 1000)
            .chain([1, 300_000])
            .collect();
        let markers = plt_markers(lengths.iter().copied());
        let mut segments = Vec::new();
        let mut at = 0;
        while at < markers.len() {
            assert_eq!(&markers[at..at + 2], &PLT.to_be_bytes());
            let lplt = u16::from_be_bytes([markers[at + 2], markers[at + 3]]) as usize;
            segments.push((markers[at + 4], markers[at + 5..at + 2 + lplt].to_vec()));
            at += 2 + lplt;
        }
        assert_eq!(at, markers.len());
        let zplts: Vec<u8> = segments.iter().map(|(zplt, _)| *zplt).collect();
        assert_eq!(zplts, [0, 1]);
        assert_eq!(1 + segments[0].1.len(), PLT_SEGMENT_PAYLOAD);
        assert_eq!(plt_packet_lengths(segments), Some(lengths));
    }
}
