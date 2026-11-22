use std::sync::Arc;

use crate::capi::MercuryT1EncodeFn;
use crate::codec::packet_order::PacketSlot;
use crate::codec::params::{MAX_LEVELS, QcdParams, step_ranging};
use crate::codec::tile_geom::{TileGeom, chart_tile_geom};
use crate::decode::plan::MainHeaderIn;

use super::band_coder::{CodedBand, PendingBand};
use super::component::{
    CoefficientSample, ComponentEncoder, ComponentParams, Int16RowFeed, Int32RowFeed, XyzTables,
};
use super::rate_control::{ImageRateAllocation, RateTargets, allocate_image_rate, allocate_rate};
use super::stripe_queue::{StripeQueue, StripeWorkers};
use super::tile_packets::{TileBand, TileBands, TilePacketWriter, run_tasks};

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

fn tile_part_header(tile: u16, index: u8, count: u8, psot: u32) -> [u8; SOT_SOD_BYTES] {
    let mut header = [0u8; SOT_SOD_BYTES];
    header[0..2].copy_from_slice(&SOT.to_be_bytes());
    header[2..4].copy_from_slice(&SOT_SEGMENT_LENGTH.to_be_bytes());
    header[4..6].copy_from_slice(&tile.to_be_bytes());
    header[6..10].copy_from_slice(&psot.to_be_bytes());
    header[10] = index;
    header[11] = count;
    header[12..14].copy_from_slice(&SOD.to_be_bytes());
    header
}

struct ComponentCoding<T: CoefficientSample> {
    band_stepsizes: Vec<f32>,
    band_inverse_steps: Vec<f64>,
    level_lifting: Vec<T::LevelLifting>,
}

struct TileCoder<T: CoefficientSample> {
    components: Vec<ComponentCoding<T>>,
    t1: MercuryT1EncodeFn,
    stripe_queue: Option<Arc<StripeQueue<T>>>,
    workers: Option<StripeWorkers<T>>,
    // [tile of the current tile row][component]
    row_encoders: Vec<Vec<ComponentEncoder<T>>>,
    // [tile][component][resolution][band]
    pending: Vec<Vec<Vec<Vec<PendingBand>>>>,
}

impl<T: CoefficientSample> TileCoder<T> {
    fn warp(
        header: &MainHeaderIn,
        components: Vec<ComponentCoding<T>>,
        threads: usize,
        t1: MercuryT1EncodeFn,
    ) -> Self {
        // the thread pushing rows codes stripes too whenever the queue is full
        let workers = threads.saturating_sub(1);
        let stripe_queue =
            (workers > 0).then(|| Arc::new(StripeQueue::warp(STRIPES_QUEUED_PER_WORKER * workers)));
        let mut coder = Self {
            components,
            t1,
            workers: stripe_queue
                .clone()
                .map(|queue| StripeWorkers::spawn(queue, workers)),
            stripe_queue,
            row_encoders: Vec::new(),
            pending: Vec::new(),
        };
        coder.start_row(header, 0);
        coder
    }

    fn start_row(&mut self, header: &MainHeaderIn, tile_row: u32) {
        let siz = &header.siz;
        let across = siz.tiles_across();
        self.row_encoders = (tile_row * across..(tile_row + 1) * across)
            .map(|tile| {
                let rect = siz.tile_rect(tile);
                header
                    .cod
                    .comps
                    .iter()
                    .zip(&self.components)
                    .enumerate()
                    .map(|(c, (style, coding))| {
                        ComponentEncoder::warp(&ComponentParams {
                            compno: c as i32,
                            bounds: [rect.x0, rect.y0, rect.x1, rect.y1],
                            style,
                            band_stepsizes: &coding.band_stepsizes,
                            band_inverse_steps: &coding.band_inverse_steps,
                            level_lifting: &coding.level_lifting,
                            mct: header.cod.use_ycc,
                            t1: self.t1,
                            stripe_queue: self.stripe_queue.clone(),
                        })
                    })
                    .collect()
            })
            .collect();
    }

    fn finish_row(&mut self) {
        for encoders in self.row_encoders.drain(..) {
            self.pending.push(
                encoders
                    .into_iter()
                    .map(ComponentEncoder::into_pending)
                    .collect(),
            );
        }
    }

    fn finish(mut self) -> Vec<Vec<Vec<Vec<CodedBand>>>> {
        if let Some(workers) = self.workers.take() {
            workers.finish();
        }
        self.pending
            .into_iter()
            .map(|components| {
                components
                    .into_iter()
                    .map(|resolutions| {
                        resolutions
                            .into_iter()
                            .map(|bands| bands.into_iter().map(PendingBand::collect).collect())
                            .collect()
                    })
                    .collect()
            })
            .collect()
    }
}

fn push_tile_row<T: CoefficientSample, S>(
    coder: &mut TileCoder<T>,
    tile_columns: &[(usize, usize)],
    planes: &[&[S]],
    first_sample: usize,
    feed: impl Fn(&mut [ComponentEncoder<T>], &[&[S]], usize),
) {
    for (encoders, &(x0, width)) in coder.row_encoders.iter_mut().zip(tile_columns) {
        let tile_planes: Vec<&[S]> = planes
            .iter()
            .map(|plane| &plane[first_sample + x0..])
            .collect();
        feed(encoders, &tile_planes, width);
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

fn tile_bands<'a>(
    coded: &'a [Vec<Vec<CodedBand>>],
    geom: &TileGeom,
    max_bit_planes: &[Vec<u8>],
) -> TileBands<'a> {
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
                    max_bit_planes: max_bit_planes[c][bandno],
                });
            }
        }
        first_band.push(comp_first);
    }
    TileBands { bands, first_band }
}

pub(crate) struct ImageEncoder {
    header: MainHeaderIn,
    rate: RateTargets,
    split_by_component: bool,
    threads: usize,
    // [component][band number]
    max_bit_planes: Vec<Vec<u8>>,
    samples: TileSamples,
    // first column and width of each tile in a tile row
    tile_columns: Vec<(usize, usize)>,
    width: usize,
    height: usize,
    rows_pushed: usize,
}

impl ImageEncoder {
    pub fn warp(header: MainHeaderIn, params: TileEncodeParams, t1: MercuryT1EncodeFn) -> Self {
        let (siz, cod) = (&header.siz, &header.cod);
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
                let components: Vec<ComponentCoding<f32>> = quantization
                    .iter()
                    .map(|bands| ComponentCoding {
                        band_stepsizes: stepsizes(bands),
                        band_inverse_steps: bands
                            .iter()
                            .map(|band| 1.0 / band.stepsize as f64)
                            .collect(),
                        level_lifting: vec![(); MAX_LEVELS as usize],
                    })
                    .collect();
                TileSamples::Int32 {
                    coder: TileCoder::warp(&header, components, params.threads, t1),
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
                        level_lifting: wavelet.level_factors.to_vec(),
                    })
                    .collect();
                TileSamples::Int16 {
                    coder: TileCoder::warp(&header, components, params.threads, t1),
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
        let tile_columns = (0..siz.tiles_across())
            .map(|tile| {
                let rect = siz.tile_rect(tile);
                (rect.x0 as usize, (rect.x1 - rect.x0) as usize)
            })
            .collect();
        Self {
            rate: params.rate,
            split_by_component: params.split_by_component,
            threads: params.threads,
            max_bit_planes,
            samples,
            tile_columns,
            width: siz.x_siz as usize,
            height: siz.y_siz as usize,
            rows_pushed: 0,
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
        self.height - self.rows_pushed
    }

    pub fn push_rows(&mut self, planes: &TileRows, stride: usize, rows: usize) {
        assert!(
            rows <= self.rows_left(),
            "{rows} rows pushed, {} left",
            self.rows_left()
        );
        let tile_height = self.header.siz.yt_siz as usize;
        let mut done = 0;
        while done < rows {
            let tile_row = self.rows_pushed / tile_height;
            let row_end = ((tile_row + 1) * tile_height).min(self.height);
            let count = (rows - done).min(row_end - self.rows_pushed);
            let first_sample = done * stride;
            let columns = &self.tile_columns;
            match (&mut self.samples, planes) {
                (TileSamples::Int32 { coder, feed }, TileRows::Int32(planes)) => push_tile_row(
                    coder,
                    columns,
                    planes,
                    first_sample,
                    |encoders, tile, width| feed.feed(encoders, tile, stride, width, count),
                ),
                (TileSamples::Int16 { coder, feed }, TileRows::Int16(planes)) => push_tile_row(
                    coder,
                    columns,
                    planes,
                    first_sample,
                    |encoders, tile, width| feed.feed(encoders, tile, stride, width, count),
                ),
                _ => panic!("plane type does not match the encoder"),
            }
            done += count;
            self.rows_pushed += count;
            if self.rows_pushed == row_end {
                self.finish_tile_row(tile_row as u32 + 1);
            }
        }
    }

    fn finish_tile_row(&mut self, next_tile_row: u32) {
        let more = next_tile_row < self.header.siz.tiles_down();
        match &mut self.samples {
            TileSamples::Int32 { coder, .. } => {
                coder.finish_row();
                if more {
                    coder.start_row(&self.header, next_tile_row);
                }
            }
            TileSamples::Int16 { coder, .. } => {
                coder.finish_row();
                if more {
                    coder.start_row(&self.header, next_tile_row);
                }
            }
        }
    }

    pub fn finish(
        self,
        mut start_tile_part: impl FnMut(u16, usize),
        mut write: impl FnMut(&[u8]),
    ) -> u16 {
        assert_eq!(self.rows_left(), 0, "rows still to push");
        let (siz, cod) = (&self.header.siz, &self.header.cod);
        let coded = match self.samples {
            TileSamples::Int32 { coder, .. } => coder.finish(),
            TileSamples::Int16 { coder, .. } => coder.finish(),
        };
        let geoms: Vec<TileGeom> = (0..coded.len())
            .map(|tile| chart_tile_geom(siz, cod, tile as u16))
            .collect();
        let bands: Vec<TileBands> = coded
            .iter()
            .zip(&geoms)
            .map(|(tile, geom)| tile_bands(tile, geom, &self.max_bit_planes))
            .collect();
        let single_tile = coded.len() == 1;
        let packet_threads = if single_tile { self.threads } else { 1 };
        let mut writers: Vec<TilePacketWriter> = geoms
            .iter()
            .map(|geom| TilePacketWriter::warp(cod, siz, geom, packet_threads))
            .collect();
        let allocation = if single_tile {
            let allocation = allocate_rate(&mut writers[0], &bands[0], &self.rate)
                .expect("rate control finds a fit");
            ImageRateAllocation {
                included: vec![allocation.included],
                slope_threshold: allocation.slope_threshold,
            }
        } else {
            allocate_image_rate(&mut writers, &bands, &self.rate, self.threads)
        };
        let tasks: Vec<_> = writers
            .iter_mut()
            .zip(&bands)
            .zip(&allocation.included)
            .collect();
        let written = run_tasks(tasks, self.threads, |((writer, bands), included)| {
            writer.write_packets(bands, included, true)
        });

        for (tile, written) in written.iter().enumerate() {
            // classic starts a tile-part at every POC volume
            let mut parts: Vec<TilePart> = Vec::new();
            for packet in written.iter() {
                match parts.last_mut() {
                    Some(part)
                        if part.first.progression_volume == packet.slot.progression_volume
                            && (!self.split_by_component
                                || part.first.comp == packet.slot.comp) =>
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
                start_tile_part(tile as u16, psot);
                write(&tile_part_header(
                    tile as u16,
                    index as u8,
                    count,
                    psot as u32,
                ));
                for packet in packets.by_ref().take(part.packets) {
                    write(packet.header);
                    for body in packet.bodies {
                        write(body);
                    }
                }
            }
        }
        allocation.slope_threshold
    }
}
