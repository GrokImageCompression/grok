use crate::codec::packet_order::{
    PacketSlot, chart_prec_bands, chart_precinct_grids, chart_precinct_positions, order_packets,
};
use crate::codec::params::{CodParams, SizParams};
use crate::codec::tile_geom::TileGeom;

use std::cmp::Reverse;
use std::ops::Range;
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};

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
    precincts_by_block_count: Vec<PrecinctCoder>,
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
        let mut precinct_coordinates = Vec::new();
        let mut first_precinct = Vec::with_capacity(grids.len());
        for (c, comp_grids) in grids.iter().enumerate() {
            let mut comp_first = Vec::with_capacity(comp_grids.len());
            for (r, &(px0, py0, npx, npy)) in comp_grids.iter().enumerate() {
                comp_first.push(precinct_coordinates.len());
                for py in py0..py0 + npy {
                    for px in px0..px0 + npx {
                        precinct_coordinates.push((c, r, px, py));
                    }
                }
            }
            first_precinct.push(comp_first);
        }
        let precincts = run_tasks(precinct_coordinates, threads, |(c, r, px, py)| {
            let style = &cod.comps[c];
            let (block_w, block_h) = style.block_span(r as u8);
            PrecinctCoder::warp(chart_prec_bands(
                &geom.components[c].resolutions[r],
                px,
                py,
                style.precinct_span(r as u8),
                r,
                block_w,
                block_h,
            ))
        });
        let mut by_block_count: Vec<(usize, PrecinctCoder)> =
            precincts.into_iter().enumerate().collect();
        by_block_count.sort_by_cached_key(|(_, coder)| Reverse(coder.block_count()));
        let mut sorted_position = vec![0; by_block_count.len()];
        for (position, &(precinct, _)) in by_block_count.iter().enumerate() {
            sorted_position[precinct] = position;
        }
        let precincts_by_block_count: Vec<PrecinctCoder> =
            by_block_count.into_iter().map(|(_, coder)| coder).collect();
        let mut precinct_packets = vec![Vec::new(); precincts_by_block_count.len()];
        let mut packet_positions = Vec::with_capacity(order.len());
        for (index, slot) in order.iter().enumerate() {
            let precinct = sorted_position
                [first_precinct[slot.comp as usize][slot.res as usize] + slot.prec as usize];
            packet_positions.push((precinct, precinct_packets[precinct].len()));
            precinct_packets[precinct].push(index);
        }
        Self {
            order,
            precincts_by_block_count,
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
        let tasks: Vec<_> = self
            .precincts_by_block_count
            .iter_mut()
            .zip(&self.precinct_packets)
            .collect();
        let precincts = run_tasks(tasks, self.threads, |(coder, packets)| {
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
        });
        WrittenPackets {
            order,
            packet_positions: &self.packet_positions,
            precincts,
        }
    }
}

pub(crate) fn run_tasks<T: Send, R: Send>(
    tasks: Vec<T>,
    threads: usize,
    work: impl Fn(T) -> R + Sync,
) -> Vec<R> {
    let count = tasks.len();
    let slots: Vec<Mutex<Option<T>>> = tasks
        .into_iter()
        .map(|task| Mutex::new(Some(task)))
        .collect();
    let next = AtomicUsize::new(0);
    let run = || {
        let mut done = Vec::new();
        loop {
            let index = next.fetch_add(1, Ordering::Relaxed);
            if index >= count {
                return done;
            }
            let task = slots[index]
                .lock()
                .unwrap()
                .take()
                .expect("each task is taken once");
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
