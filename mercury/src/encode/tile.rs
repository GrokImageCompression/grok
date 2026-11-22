use std::sync::Arc;

use crate::capi::MercuryT1EncodeFn;
use crate::codec::packet_order::PacketSlot;
use crate::codec::params::{QcdParams, step_ranging};
use crate::codec::tile_geom::chart_tile_geom;
use crate::decode::plan::MainHeaderIn;

use super::component::{ComponentEncoder, ComponentParams, XyzTables, feed_tile_rows};
use super::rate_control::{RateTargets, allocate_rate};
use super::stripe_queue::{StripeQueue, run_stripe_workers};
use super::tile_packets::{TileBand, TileBands, TilePacketWriter};

const STRIPES_QUEUED_PER_WORKER: usize = 2;
const SOT: u16 = 0xFF90;
const SOD: u16 = 0xFF93;
const SOT_SEGMENT_LENGTH: u16 = 10;
const SOT_SOD_BYTES: usize = 14;

pub(crate) struct TileEncodeParams {
    pub rate: RateTargets,
    pub split_by_component: bool,
    // RGB at this precision, converted like classic's applyXYZTransform
    pub xyz_source_precision: Option<u32>,
    pub threads: usize,
}

struct BandQuantization {
    stepsize: f32,
    max_bit_planes: u8,
}

fn quantize_bands(quant: &QcdParams, precision: u8, levels: u8) -> Vec<BandQuantization> {
    (0..3 * levels as usize + 1)
        .map(|bandno| {
            let step = quant.steps[bandno];
            let orientation = if bandno == 0 { 0 } else { (bandno - 1) % 3 + 1 };
            let log2_gain = match orientation {
                0 => 0,
                3 => 2,
                _ => 1,
            };
            BandQuantization {
                // T.800 equation E-3 codes steps relative to 2^(precision + gain)
                stepsize: (step as f64 * 2f64.powi(precision as i32 + log2_gain)) as f32,
                max_bit_planes: (step_ranging(step) + quant.guard_bits as i32 - 1).max(0) as u8,
            }
        })
        .collect()
}

fn tile_part_header(index: u8, count: u8, psot: u32) -> [u8; SOT_SOD_BYTES] {
    let mut header = [0u8; SOT_SOD_BYTES];
    header[0..2].copy_from_slice(&SOT.to_be_bytes());
    header[2..4].copy_from_slice(&SOT_SEGMENT_LENGTH.to_be_bytes());
    // Isot, the only tile is tile 0
    header[4..6].copy_from_slice(&0u16.to_be_bytes());
    header[6..10].copy_from_slice(&psot.to_be_bytes());
    header[10] = index;
    header[11] = count;
    header[12..14].copy_from_slice(&SOD.to_be_bytes());
    header
}

pub(crate) fn encode_tile(
    planes: &[&[i32]],
    stride: usize,
    header: &MainHeaderIn,
    params: &TileEncodeParams,
    t1: MercuryT1EncodeFn,
    mut write_tile_part: impl FnMut(&[u8]),
) -> u16 {
    let (siz, cod) = (&header.siz, &header.cod);
    let geom = chart_tile_geom(siz, cod, 0);
    let rect = siz.tile_rect(0);
    let quantization: Vec<Vec<BandQuantization>> = header
        .component_quant()
        .iter()
        .zip(&siz.components)
        .zip(&cod.comps)
        .map(|((quant, comp), style)| quantize_bands(quant, comp.precision, style.num_levels))
        .collect();

    // the thread feeding rows codes stripes too whenever the queue is full
    let workers = params.threads.saturating_sub(1);
    let stripe_queue =
        (workers > 0).then(|| Arc::new(StripeQueue::warp(STRIPES_QUEUED_PER_WORKER * workers)));
    let mut encoders: Vec<ComponentEncoder> = cod
        .comps
        .iter()
        .zip(&quantization)
        .enumerate()
        .map(|(c, (style, bands))| {
            let band_stepsizes: Vec<f32> = bands.iter().map(|band| band.stepsize).collect();
            ComponentEncoder::warp(&ComponentParams {
                compno: c as i32,
                bounds: [rect.x0, rect.y0, rect.x1, rect.y1],
                style,
                band_stepsizes: &band_stepsizes,
                mct: cod.use_ycc,
                t1,
                stripe_queue: stripe_queue.clone(),
            })
        })
        .collect();
    let precision = siz.components[0].precision as u32;
    let xyz = params
        .xyz_source_precision
        .map(|source_precision| XyzTables::warp(source_precision, precision));
    let feed = |encoders: &mut [ComponentEncoder]| {
        feed_tile_rows(
            encoders,
            planes,
            stride,
            (rect.x1 - rect.x0) as usize,
            (rect.y1 - rect.y0) as usize,
            precision,
            cod.use_ycc,
            xyz.as_ref(),
        )
    };
    match &stripe_queue {
        Some(queue) => run_stripe_workers(queue, workers, || feed(&mut encoders)),
        None => feed(&mut encoders),
    }
    for encoder in &mut encoders {
        encoder.collect_stripes();
    }

    let mut bands = Vec::new();
    let mut first_band = Vec::with_capacity(encoders.len());
    for (c, encoder) in encoders.iter().enumerate() {
        let resolutions = &geom.components[c].resolutions;
        let mut comp_first = Vec::with_capacity(resolutions.len());
        for (r, res) in resolutions.iter().enumerate() {
            comp_first.push(bands.len());
            for (b, subband) in res.subbands.iter().enumerate() {
                let bandno = if r == 0 { 0 } else { 3 * (r - 1) + b + 1 };
                bands.push(TileBand {
                    compno: c,
                    coded: encoder.coded_band(r, b),
                    blocks_across: subband.blocks_wide,
                    max_bit_planes: quantization[c][bandno].max_bit_planes,
                });
            }
        }
        first_band.push(comp_first);
    }
    let bands = TileBands { bands, first_band };
    let mut packets = TilePacketWriter::warp(cod, siz, &geom);
    let allocation =
        allocate_rate(&mut packets, &bands, &params.rate).expect("rate control finds a fit");

    let mut packet_bytes = Vec::new();
    // classic starts a tile-part at every POC volume
    let mut part_lengths: Vec<(PacketSlot, usize)> = Vec::new();
    packets.write_packets(
        &bands,
        &allocation.included,
        &mut packet_bytes,
        true,
        |slot, length| match part_lengths.last_mut() {
            Some((first, total))
                if first.progression_volume == slot.progression_volume
                    && (!params.split_by_component || first.comp == slot.comp) =>
            {
                *total += length
            }
            _ => part_lengths.push((slot, length)),
        },
    );
    let count = u8::try_from(part_lengths.len()).expect("classic allows 255 tile-parts");
    let mut packet_start = 0;
    let mut tile_part = Vec::new();
    for (index, &(_, length)) in part_lengths.iter().enumerate() {
        let psot = (SOT_SOD_BYTES + length) as u32;
        tile_part.clear();
        tile_part.extend_from_slice(&tile_part_header(index as u8, count, psot));
        tile_part.extend_from_slice(&packet_bytes[packet_start..packet_start + length]);
        packet_start += length;
        write_tile_part(&tile_part);
    }
    allocation.slope_threshold
}
