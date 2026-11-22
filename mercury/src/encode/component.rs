use std::sync::Arc;

use crate::capi::MercuryT1EncodeFn;
use crate::codec::params::CompCodingStyle;
use crate::dwt::analysis::{AnalysisSample, BandSplit, LevelAnalysis, VerticalBand};
use crate::ffi_dwt::{
    MercuryXyzTables, mercury_hwy_forward_ict_16, mercury_hwy_forward_xyz_ict,
    mercury_xyz_tables_free, mercury_xyz_tables_new,
};

use super::band_coder::{BandCoder, BandGeometry, BandT1Params, PendingBand, QuantizedSample};
use super::stripe_queue::StripeQueue;
use super::task_pool::TaskPool;

pub(crate) trait CoefficientSample: AnalysisSample + QuantizedSample {}

impl<T: AnalysisSample + QuantizedSample> CoefficientSample for T {}

pub(crate) struct ComponentParams<'a, T: CoefficientSample> {
    pub compno: i32,
    pub bounds: [u32; 4],
    pub style: &'a CompCodingStyle,
    // classic band order: LL, then HL, LH, HH from the lowest resolution up
    pub band_stepsizes: &'a [f32],
    pub band_inverse_steps: &'a [f64],
    // finest level first
    pub level_lifting: &'a [T::LevelLifting],
    pub mct: bool,
    pub t1: MercuryT1EncodeFn,
    // None codes each stripe on the thread that feeds the rows
    pub stripe_queue: Option<Arc<StripeQueue<T>>>,
}

// one tile component: rows in, every code-block of every subband coded
pub(crate) struct ComponentEncoder<T: CoefficientSample> {
    staged_rows: Vec<T>,
    levels: Vec<LevelAnalysis<T>>,
    // details[level] holds HL, LH, HH
    details: Vec<[BandCoder<T>; 3]>,
    lowest: BandCoder<T>,
}

const ORIENTATION_LL: i32 = 0;
const ORIENTATION_HL: i32 = 1;
const ORIENTATION_LH: i32 = 2;
const ORIENTATION_HH: i32 = 3;

impl<T: CoefficientSample> ComponentEncoder<T> {
    pub fn warp(params: &ComponentParams<T>) -> Self {
        let [x0, y0, x1, y1] = params.bounds;
        let num_levels = params.style.num_levels as u32;
        let scaled = |coordinate: u32, level: u32| coordinate.div_ceil(1 << level);
        let band = |orientation: i32, level: u32, geometry: BandGeometry| {
            let resno = if orientation == ORIENTATION_LL {
                0
            } else {
                num_levels - level
            };
            let band_number = if resno == 0 {
                0
            } else {
                3 * (resno - 1) as usize + orientation as usize
            };
            let (block_width, block_height) = params.style.block_span(resno as u8);
            BandCoder::warp(
                geometry,
                BandT1Params {
                    orientation,
                    compno: params.compno,
                    level: level as i32,
                    stepsize: params.band_stepsizes[band_number],
                    mct: params.mct,
                    reversible: params.style.reversible,
                },
                params.band_inverse_steps[band_number],
                block_width,
                block_height,
                params.t1,
                params.stripe_queue.clone(),
            )
        };
        let geometry = |columns: BandSplit, rows: BandSplit, x0: u32, y0: u32, high_x, high_y| {
            let start = |origin: u32, high: bool| {
                if high {
                    origin >> 1
                } else {
                    origin.div_ceil(2)
                }
            };
            BandGeometry {
                x0: start(x0, high_x),
                y0: start(y0, high_y),
                width: if high_x { columns.high } else { columns.low },
                height: if high_y { rows.high } else { rows.low },
            }
        };

        let mut levels = Vec::new();
        let mut details = Vec::new();
        let mut lowest = None;
        for level in 0..num_levels {
            let (rx0, ry0) = (scaled(x0, level), scaled(y0, level));
            let width = (scaled(x1, level) - rx0) as usize;
            let height = (scaled(y1, level) - ry0) as usize;
            let analysis = LevelAnalysis::warp(
                rx0,
                ry0,
                width,
                height,
                params.level_lifting[level as usize],
            );
            let (columns, rows) = (analysis.columns(), analysis.rows_split());
            let band_geometry = |high_x, high_y| geometry(columns, rows, rx0, ry0, high_x, high_y);
            details.push([
                band(ORIENTATION_HL, level, band_geometry(true, false)),
                band(ORIENTATION_LH, level, band_geometry(false, true)),
                band(ORIENTATION_HH, level, band_geometry(true, true)),
            ]);
            if level + 1 == num_levels {
                lowest = Some(band(
                    ORIENTATION_LL,
                    num_levels,
                    band_geometry(false, false),
                ));
            }
            levels.push(analysis);
        }
        Self {
            staged_rows: Vec::new(),
            levels,
            details,
            lowest: lowest.expect("at least one level"),
        }
    }

    pub fn push_row(&mut self, fill: impl FnOnce(&mut [T])) {
        self.levels[0].feed(fill);
        self.drain(0);
    }

    fn drain(&mut self, level: usize) {
        while let Some(band) = self.levels[level].ready() {
            let deeper_level = level + 1;
            let [hl, lh, hh] = &mut self.details[level];
            match band {
                VerticalBand::High => {
                    self.levels[level].emit(lh.next_row(), hh.next_row());
                    lh.row_done();
                    hh.row_done();
                }
                VerticalBand::Low if deeper_level == self.levels.len() => {
                    self.levels[level].emit(self.lowest.next_row(), hl.next_row());
                    self.lowest.row_done();
                    hl.row_done();
                }
                VerticalBand::Low => {
                    let (current, deeper) = self.levels.split_at_mut(deeper_level);
                    deeper[0].feed(|target| current[level].emit(target, hl.next_row()));
                    hl.row_done();
                    self.drain(deeper_level);
                }
            }
        }
    }

    // [resolution][band], LL alone at resolution 0, then HL, LH, HH
    pub fn into_pending(self) -> Vec<Vec<PendingBand>> {
        let mut resolutions = vec![vec![self.lowest.into_pending()]];
        for [hl, lh, hh] in self.details.into_iter().rev() {
            resolutions.push(vec![
                hl.into_pending(),
                lh.into_pending(),
                hh.into_pending(),
            ]);
        }
        resolutions
    }
}

// grok's RGB to X'Y'Z' lookup tables for one source precision
pub(crate) struct XyzTables(*mut MercuryXyzTables);

// mercury_hwy_forward_xyz_ict only reads the tables mercury_xyz_tables_new filled
unsafe impl Sync for XyzTables {}

impl XyzTables {
    pub fn warp(source_precision: u32, coded_precision: u32) -> Self {
        Self(unsafe { mercury_xyz_tables_new(source_precision, coded_precision) })
    }
}

impl Drop for XyzTables {
    fn drop(&mut self) {
        unsafe { mercury_xyz_tables_free(self.0) };
    }
}

pub(crate) struct Int32RowFeed {
    pub precision: u32,
    pub mct: bool,
    // samples arrive at the source precision with xyz
    pub xyz: Option<XyzTables>,
}

fn staged_batches<T: CoefficientSample>(
    encoders: &mut [ComponentEncoder<T>],
    samples: usize,
) -> [&mut [T]; 3] {
    let [luma, chroma_blue, chroma_red] = encoders else {
        panic!("the colour transform needs three components");
    };
    [luma, chroma_blue, chroma_red].map(|encoder| {
        encoder.staged_rows.resize(samples, T::default());
        encoder.staged_rows.as_mut_slice()
    })
}

fn push_staged_rows<T: CoefficientSample + Sync>(
    encoders: &mut [ComponentEncoder<T>],
    width: usize,
    pool: &TaskPool,
) {
    pool.run(encoders.iter_mut().collect(), |encoder| {
        let staged = std::mem::take(&mut encoder.staged_rows);
        for row in staged.chunks_exact(width) {
            encoder.push_row(|target| target.copy_from_slice(row));
        }
        encoder.staged_rows = staged;
    });
}

impl Int32RowFeed {
    pub fn feed(
        &self,
        encoders: &mut [ComponentEncoder<f32>],
        planes: &[&[i32]],
        stride: usize,
        width: usize,
        rows: usize,
        pool: &TaskPool,
    ) {
        // classic adds a negative DC shift
        let dc_shift = -(1i32 << (self.precision - 1));
        if !self.mct {
            pool.run(
                encoders.iter_mut().zip(planes).collect(),
                |(encoder, plane)| {
                    for y in 0..rows {
                        let source = &plane[y * stride..y * stride + width];
                        encoder.push_row(|target| {
                            for (sample, &value) in target.iter_mut().zip(source) {
                                *sample = (value + dc_shift) as f32;
                            }
                        });
                    }
                },
            );
            return;
        }
        let [luma, chroma_blue, chroma_red] = staged_batches(encoders, width * rows);
        let row_outputs: Vec<_> = luma
            .chunks_exact_mut(width)
            .zip(chroma_blue.chunks_exact_mut(width))
            .zip(chroma_red.chunks_exact_mut(width))
            .enumerate()
            .collect();
        pool.run(row_outputs, |(y, ((luma, chroma_blue), chroma_red))| {
            let row = |c: usize| planes[c][y * stride..y * stride + width].as_ptr();
            match &self.xyz {
                Some(tables) => unsafe {
                    mercury_hwy_forward_xyz_ict(
                        tables.0,
                        row(0),
                        row(1),
                        row(2),
                        dc_shift,
                        luma.as_mut_ptr(),
                        chroma_blue.as_mut_ptr(),
                        chroma_red.as_mut_ptr(),
                        width as i32,
                    );
                },
                None => unsafe {
                    crate::ffi_dwt::mercury_hwy_forward_ict(
                        row(0),
                        row(1),
                        row(2),
                        dc_shift,
                        luma.as_mut_ptr(),
                        chroma_blue.as_mut_ptr(),
                        chroma_red.as_mut_ptr(),
                        width as i32,
                    );
                },
            }
        });
        push_staged_rows(encoders, width, pool);
    }
}

pub(crate) fn int16_finest_sample(value: i16, dc_shift: i16, fraction_bits: u32) -> i16 {
    // grok's encode_97_16_v load at the finest level, in wrapping int16 lanes
    value.wrapping_sub(dc_shift).wrapping_shl(fraction_bits)
}

pub(crate) struct Int16RowFeed {
    pub precision: u32,
    pub fraction_bits: u32,
    pub mct: bool,
}

impl Int16RowFeed {
    pub fn feed(
        &self,
        encoders: &mut [ComponentEncoder<i16>],
        planes: &[&[i16]],
        stride: usize,
        width: usize,
        rows: usize,
        pool: &TaskPool,
    ) {
        let dc_shift = 1i32 << (self.precision - 1);
        // grok's CompressIrrev16 with the ICT, else the finest level's encode_97_16_v load
        if !self.mct {
            pool.run(
                encoders.iter_mut().zip(planes).collect(),
                |(encoder, plane)| {
                    for y in 0..rows {
                        let source = &plane[y * stride..y * stride + width];
                        encoder.push_row(|target| {
                            for (sample, &value) in target.iter_mut().zip(source) {
                                *sample =
                                    int16_finest_sample(value, dc_shift as i16, self.fraction_bits);
                            }
                        });
                    }
                },
            );
            return;
        }
        let [luma, chroma_blue, chroma_red] = staged_batches(encoders, width * rows);
        let row_outputs: Vec<_> = luma
            .chunks_exact_mut(width)
            .zip(chroma_blue.chunks_exact_mut(width))
            .zip(chroma_red.chunks_exact_mut(width))
            .enumerate()
            .collect();
        pool.run(row_outputs, |(y, ((luma, chroma_blue), chroma_red))| {
            let row = |c: usize| planes[c][y * stride..y * stride + width].as_ptr();
            unsafe {
                mercury_hwy_forward_ict_16(
                    row(0),
                    row(1),
                    row(2),
                    -dc_shift,
                    self.fraction_bits,
                    luma.as_mut_ptr(),
                    chroma_blue.as_mut_ptr(),
                    chroma_red.as_mut_ptr(),
                    width as i32,
                );
            }
        });
        push_staged_rows(encoders, width, pool);
    }
}

pub(crate) struct ReversibleRowFeed {
    pub precision: u32,
    pub mct: bool,
}

impl ReversibleRowFeed {
    pub fn feed<S: Copy + Into<i32> + Sync>(
        &self,
        encoders: &mut [ComponentEncoder<i32>],
        planes: &[&[S]],
        stride: usize,
        width: usize,
        rows: usize,
        pool: &TaskPool,
    ) {
        let dc_shift = -(1i32 << (self.precision - 1));
        let shifted = |plane: &[S], x: usize| plane[x].into() + dc_shift;
        pool.run(
            encoders.iter_mut().enumerate().collect(),
            |(component, encoder)| {
                for y in 0..rows {
                    let row = |c: usize| &planes[c][y * stride..y * stride + width];
                    if !self.mct {
                        let source = row(component);
                        encoder.push_row(|target| {
                            for (sample, &value) in target.iter_mut().zip(source) {
                                *sample = value.into() + dc_shift;
                            }
                        });
                        continue;
                    }
                    let (red, green, blue) = (row(0), row(1), row(2));
                    // grok's CompressRev
                    match component {
                        0 => encoder.push_row(|target| {
                            for (x, sample) in target.iter_mut().enumerate() {
                                let green = shifted(green, x);
                                *sample = (green + green + shifted(blue, x) + shifted(red, x)) >> 2;
                            }
                        }),
                        1 => encoder.push_row(|target| {
                            for (x, sample) in target.iter_mut().enumerate() {
                                *sample = shifted(blue, x) - shifted(green, x);
                            }
                        }),
                        _ => encoder.push_row(|target| {
                            for (x, sample) in target.iter_mut().enumerate() {
                                *sample = shifted(red, x) - shifted(green, x);
                            }
                        }),
                    }
                }
            },
        );
    }
}
