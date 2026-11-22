use std::sync::Arc;

use crate::capi::MercuryT1EncodeFn;
use crate::codec::packet_order::PacketSlot;
use crate::codec::params::{MAX_LEVELS, QcdParams, step_ranging};
use crate::codec::tile_geom::chart_tile_geom;
use crate::decode::plan::MainHeaderIn;

use super::band_coder::CodedBand;
use super::component::{
    CoefficientSample, ComponentEncoder, ComponentParams, Int16RowFeed, Int32RowFeed, XyzTables,
};
use super::rate_control::{RateTargets, allocate_rate};
use super::stripe_queue::{StripeQueue, StripeWorkers};
use super::tile_packets::{TileBand, TileBands, TilePacketWriter};

const STRIPES_QUEUED_PER_WORKER: usize = 2;
const SOT: u16 = 0xFF90;
const SOD: u16 = 0xFF93;
const SOT_SEGMENT_LENGTH: u16 = 10;
const SOT_SOD_BYTES: usize = 14;
pub(crate) const ORIENTATIONS: usize = 4;

pub(crate) struct TileEncodeParams<'a> {
    pub rate: RateTargets,
    pub split_by_component: bool,
    // RGB at this precision, converted like classic's applyXYZTransform
    pub xyz_source_precision: Option<u32>,
    pub threads: usize,
    // None for int32 planes and the float 9/7
    pub int16_wavelet: Option<Int16Wavelet<'a>>,
}

pub(crate) struct Int16Wavelet<'a> {
    pub fraction_bits: u32,
    // grok's makeForward97Int16Plan level factors in Q15, finest level first
    pub level_factors: &'a [i32],
    // grok's makeForward97Int16Plan band scales, finest level first, by orientation LL, HL, LH, HH
    pub band_scales: &'a [[f64; ORIENTATIONS]],
}

pub(crate) enum TileRows<'a> {
    Int32(Vec<&'a [i32]>),
    Int16(Vec<&'a [i16]>),
}

struct BandQuantization {
    stepsize: f32,
    max_bit_planes: u8,
}

fn orientation(bandno: usize) -> usize {
    if bandno == 0 { 0 } else { (bandno - 1) % 3 + 1 }
}

fn quantize_bands(quant: &QcdParams, precision: u8, levels: u8) -> Vec<BandQuantization> {
    (0..3 * levels as usize + 1)
        .map(|bandno| {
            let step = quant.steps[bandno];
            let log2_gain = match orientation(bandno) {
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

fn int16_inverse_steps(bands: &[BandQuantization], levels: u8, wavelet: &Int16Wavelet) -> Vec<f64> {
    bands
        .iter()
        .enumerate()
        .map(|(bandno, band)| {
            // grok's CompressScheduler fixedPointInverseStep, resolution 0 is the LL band of the
            // coarsest level
            let resno = if bandno == 0 { 0 } else { (bandno - 1) / 3 + 1 };
            let level = levels as usize - resno.max(1);
            let scale = wavelet.band_scales[level][orientation(bandno)]
                * (1u32 << wavelet.fraction_bits) as f64;
            1.0 / (band.stepsize as f64 * scale)
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

struct ComponentCoding<'a, T: CoefficientSample> {
    band_stepsizes: Vec<f32>,
    band_inverse_steps: Vec<f64>,
    level_lifting: &'a [T::LevelLifting],
}

struct TileCoder<T: CoefficientSample> {
    encoders: Vec<ComponentEncoder<T>>,
    workers: Option<StripeWorkers<T>>,
}

impl<T: CoefficientSample> TileCoder<T> {
    fn warp(
        header: &MainHeaderIn,
        components: &[ComponentCoding<T>],
        threads: usize,
        t1: MercuryT1EncodeFn,
    ) -> Self {
        let rect = header.siz.tile_rect(0);
        // the thread pushing rows codes stripes too whenever the queue is full
        let workers = threads.saturating_sub(1);
        let stripe_queue =
            (workers > 0).then(|| Arc::new(StripeQueue::warp(STRIPES_QUEUED_PER_WORKER * workers)));
        let encoders = header
            .cod
            .comps
            .iter()
            .zip(components)
            .enumerate()
            .map(|(c, (style, coding))| {
                ComponentEncoder::warp(&ComponentParams {
                    compno: c as i32,
                    bounds: [rect.x0, rect.y0, rect.x1, rect.y1],
                    style,
                    band_stepsizes: &coding.band_stepsizes,
                    band_inverse_steps: &coding.band_inverse_steps,
                    level_lifting: coding.level_lifting,
                    mct: header.cod.use_ycc,
                    t1,
                    stripe_queue: stripe_queue.clone(),
                })
            })
            .collect();
        Self {
            encoders,
            workers: stripe_queue.map(|queue| StripeWorkers::spawn(queue, workers)),
        }
    }

    fn finish(mut self) -> Vec<Vec<Vec<CodedBand>>> {
        if let Some(workers) = self.workers.take() {
            workers.finish();
        }
        self.encoders
            .into_iter()
            .map(|mut encoder| {
                encoder.collect_stripes();
                encoder.into_coded_bands()
            })
            .collect()
    }
}

enum TileSamples {
    Int32 {
        coder: TileCoder<f32>,
        feed: Int32RowFeed,
    },
    Int16 {
        coder: TileCoder<i16>,
        feed: Int16RowFeed,
    },
}

struct TilePart {
    first: PacketSlot,
    length: usize,
    packets: usize,
}

pub(crate) struct TileEncoder {
    header: MainHeaderIn,
    rate: RateTargets,
    split_by_component: bool,
    threads: usize,
    // [component][band number]
    max_bit_planes: Vec<Vec<u8>>,
    samples: TileSamples,
    width: usize,
    rows_left: usize,
}

impl TileEncoder {
    pub fn warp(header: MainHeaderIn, params: TileEncodeParams, t1: MercuryT1EncodeFn) -> Self {
        let (siz, cod) = (&header.siz, &header.cod);
        let rect = siz.tile_rect(0);
        let precision = siz.components[0].precision as u32;
        let quantization: Vec<Vec<BandQuantization>> = header
            .component_quant()
            .iter()
            .zip(&siz.components)
            .zip(&cod.comps)
            .map(|((quant, comp), style)| quantize_bands(quant, comp.precision, style.num_levels))
            .collect();
        let stepsizes =
            |bands: &[BandQuantization]| bands.iter().map(|band| band.stepsize).collect();
        let samples = match &params.int16_wavelet {
            None => {
                let no_lifting = [(); MAX_LEVELS as usize];
                let components: Vec<ComponentCoding<f32>> = quantization
                    .iter()
                    .map(|bands| ComponentCoding {
                        band_stepsizes: stepsizes(bands),
                        band_inverse_steps: bands
                            .iter()
                            .map(|band| 1.0 / band.stepsize as f64)
                            .collect(),
                        level_lifting: &no_lifting,
                    })
                    .collect();
                TileSamples::Int32 {
                    coder: TileCoder::warp(&header, &components, params.threads, t1),
                    feed: Int32RowFeed {
                        precision,
                        mct: cod.use_ycc,
                        xyz: params
                            .xyz_source_precision
                            .map(|source_precision| XyzTables::warp(source_precision, precision)),
                    },
                }
            }
            Some(wavelet) => {
                let components: Vec<ComponentCoding<i16>> = quantization
                    .iter()
                    .zip(&cod.comps)
                    .map(|(bands, style)| ComponentCoding {
                        band_stepsizes: stepsizes(bands),
                        band_inverse_steps: int16_inverse_steps(bands, style.num_levels, wavelet),
                        level_lifting: wavelet.level_factors,
                    })
                    .collect();
                TileSamples::Int16 {
                    coder: TileCoder::warp(&header, &components, params.threads, t1),
                    feed: Int16RowFeed {
                        precision,
                        fraction_bits: wavelet.fraction_bits,
                        mct: cod.use_ycc,
                    },
                }
            }
        };
        let max_bit_planes = quantization
            .iter()
            .map(|bands| bands.iter().map(|band| band.max_bit_planes).collect())
            .collect();
        Self {
            rate: params.rate,
            split_by_component: params.split_by_component,
            threads: params.threads,
            max_bit_planes,
            samples,
            width: (rect.x1 - rect.x0) as usize,
            rows_left: (rect.y1 - rect.y0) as usize,
            header,
        }
    }

    pub fn takes_int16(&self) -> bool {
        matches!(self.samples, TileSamples::Int16 { .. })
    }

    pub fn width(&self) -> usize {
        self.width
    }

    pub fn components(&self) -> usize {
        self.header.siz.comp_count()
    }

    pub fn rows_left(&self) -> usize {
        self.rows_left
    }

    pub fn push_rows(&mut self, planes: &TileRows, stride: usize, rows: usize) {
        assert!(
            rows <= self.rows_left,
            "{rows} rows pushed, {} left",
            self.rows_left
        );
        match (&mut self.samples, planes) {
            (TileSamples::Int32 { coder, feed }, TileRows::Int32(planes)) => {
                feed.feed(&mut coder.encoders, planes, stride, self.width, rows)
            }
            (TileSamples::Int16 { coder, feed }, TileRows::Int16(planes)) => {
                feed.feed(&mut coder.encoders, planes, stride, self.width, rows)
            }
            _ => panic!("plane type does not match the encoder"),
        }
        self.rows_left -= rows;
    }

    pub fn finish(
        self,
        mut start_tile_part: impl FnMut(usize),
        mut write: impl FnMut(&[u8]),
    ) -> u16 {
        assert_eq!(self.rows_left, 0, "rows still to push");
        let (siz, cod) = (&self.header.siz, &self.header.cod);
        let coded = match self.samples {
            TileSamples::Int32 { coder, .. } => coder.finish(),
            TileSamples::Int16 { coder, .. } => coder.finish(),
        };
        let geom = chart_tile_geom(siz, cod, 0);
        let mut bands = Vec::new();
        let mut first_band = Vec::with_capacity(coded.len());
        for (c, resolutions) in coded.iter().enumerate() {
            let mut comp_first = Vec::with_capacity(resolutions.len());
            for (r, (res, coded_bands)) in geom.components[c]
                .resolutions
                .iter()
                .zip(resolutions)
                .enumerate()
            {
                comp_first.push(bands.len());
                for (b, (subband, coded_band)) in res.subbands.iter().zip(coded_bands).enumerate() {
                    let bandno = if r == 0 { 0 } else { 3 * (r - 1) + b + 1 };
                    bands.push(TileBand {
                        compno: c,
                        coded: coded_band,
                        blocks_across: subband.blocks_wide,
                        max_bit_planes: self.max_bit_planes[c][bandno],
                    });
                }
            }
            first_band.push(comp_first);
        }
        let bands = TileBands { bands, first_band };
        let mut packets = TilePacketWriter::warp(cod, siz, &geom, self.threads);
        let allocation =
            allocate_rate(&mut packets, &bands, &self.rate).expect("rate control finds a fit");

        let written = packets.write_packets(&bands, &allocation.included, true);
        // classic starts a tile-part at every POC volume
        let mut parts: Vec<TilePart> = Vec::new();
        for packet in written.iter() {
            match parts.last_mut() {
                Some(part)
                    if part.first.progression_volume == packet.slot.progression_volume
                        && (!self.split_by_component || part.first.comp == packet.slot.comp) =>
                {
                    part.length += packet.length;
                    part.packets += 1;
                }
                _ => parts.push(TilePart {
                    first: packet.slot,
                    length: packet.length,
                    packets: 1,
                }),
            }
        }
        let count = u8::try_from(parts.len()).expect("classic allows 255 tile-parts");
        let mut packets = written.iter();
        for (index, part) in parts.iter().enumerate() {
            let psot = SOT_SOD_BYTES + part.length;
            start_tile_part(psot);
            write(&tile_part_header(index as u8, count, psot as u32));
            for packet in packets.by_ref().take(part.packets) {
                write(packet.header);
                for body in packet.bodies {
                    write(body);
                }
            }
        }
        allocation.slope_threshold
    }
}
