use crate::capi::MercuryT1EncodeFn;
use crate::dwt::analysis::{BandSplit, LevelAnalysis, VerticalBand};
use crate::ffi_dwt::{
    MercuryXyzTables, mercury_hwy_forward_xyz_ict, mercury_xyz_tables_free, mercury_xyz_tables_new,
};

use super::band_coder::{BandCoder, BandGeometry, BandT1Params, CodedBand, CodingStats};

pub(crate) struct ComponentParams<'a> {
    pub compno: i32,
    pub bounds: [u32; 4],
    pub levels: u32,
    pub block_width: u32,
    pub block_height: u32,
    // classic band order: LL, then HL, LH, HH from the lowest resolution up
    pub band_stepsizes: &'a [f32],
    pub mct: bool,
    pub t1: MercuryT1EncodeFn,
}

// one tile component: rows in, every code-block of every subband coded
pub(crate) struct ComponentEncoder {
    levels: Vec<LevelAnalysis<f32>>,
    // details[level] holds HL, LH, HH
    details: Vec<[BandCoder; 3]>,
    lowest: BandCoder,
}

const ORIENTATION_LL: i32 = 0;
const ORIENTATION_HL: i32 = 1;
const ORIENTATION_LH: i32 = 2;
const ORIENTATION_HH: i32 = 3;

impl ComponentEncoder {
    pub fn warp(params: &ComponentParams) -> Self {
        let [x0, y0, x1, y1] = params.bounds;
        let scaled = |coordinate: u32, level: u32| coordinate.div_ceil(1 << level);
        let band = |orientation: i32, level: u32, geometry: BandGeometry| {
            let resno = if orientation == ORIENTATION_LL {
                0
            } else {
                params.levels - level
            };
            let band_number = if resno == 0 {
                0
            } else {
                3 * (resno - 1) as usize + orientation as usize
            };
            BandCoder::warp(
                geometry,
                BandT1Params {
                    orientation,
                    compno: params.compno,
                    level: level as i32,
                    stepsize: params.band_stepsizes[band_number],
                    mct: params.mct,
                },
                params.block_width,
                params.block_height,
                params.t1,
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
        for level in 0..params.levels {
            let (rx0, ry0) = (scaled(x0, level), scaled(y0, level));
            let width = (scaled(x1, level) - rx0) as usize;
            let height = (scaled(y1, level) - ry0) as usize;
            let analysis = LevelAnalysis::warp(rx0, ry0, width, height);
            let (columns, rows) = (analysis.columns(), analysis.rows_split());
            let band_geometry = |high_x, high_y| geometry(columns, rows, rx0, ry0, high_x, high_y);
            details.push([
                band(ORIENTATION_HL, level, band_geometry(true, false)),
                band(ORIENTATION_LH, level, band_geometry(false, true)),
                band(ORIENTATION_HH, level, band_geometry(true, true)),
            ]);
            if level + 1 == params.levels {
                lowest = Some(band(
                    ORIENTATION_LL,
                    params.levels,
                    band_geometry(false, false),
                ));
            }
            levels.push(analysis);
        }
        Self {
            levels,
            details,
            lowest: lowest.expect("at least one level"),
        }
    }

    pub fn push_row(&mut self, fill: impl FnOnce(&mut [f32])) {
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

    // band 0 is LL at resolution 0, then HL, LH, HH
    pub fn coded_band(&self, res: usize, band: usize) -> &CodedBand {
        if res == 0 {
            &self.lowest.coded
        } else {
            &self.details[self.levels.len() - res][band].coded
        }
    }

    pub fn stats(&self) -> CodingStats {
        let mut total = self.lowest.coded.stats();
        for band in self.details.iter().flatten() {
            total.add(band.coded.stats());
        }
        total
    }
}

// grok's RGB to X'Y'Z' lookup tables for one source precision
pub(crate) struct XyzTables(*mut MercuryXyzTables);

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

// with `xyz`, samples are at the source precision
#[allow(clippy::too_many_arguments)]
pub(crate) fn feed_tile_rows(
    encoders: &mut [ComponentEncoder],
    planes: &[&[i32]],
    stride: usize,
    width: usize,
    height: usize,
    precision: u32,
    precision_shift: u32,
    xyz: Option<&XyzTables>,
) {
    let comps = planes.len();
    // classic adds a negative DC shift
    let dc_shift = -(1i32 << (precision - 1));
    let mut cut = vec![vec![0i32; width]; comps];
    let mut shifted = vec![vec![0f32; width]; comps];
    for y in 0..height {
        if let Some(tables) = xyz {
            let row = |c: usize| planes[c][y * stride..].as_ptr();
            let [luma, chroma_blue, chroma_red] = &mut shifted[..] else {
                unreachable!("three components");
            };
            unsafe {
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
            }
            for (encoder, row) in encoders.iter_mut().zip(&shifted) {
                encoder.push_row(|target| target.copy_from_slice(row));
            }
            continue;
        }
        for (row, plane) in cut.iter_mut().zip(planes) {
            let source = &plane[y * stride..y * stride + width];
            for (sample, &value) in row.iter_mut().zip(source) {
                *sample = value >> precision_shift;
            }
        }
        if comps == 3 {
            let [luma, chroma_blue, chroma_red] = &mut shifted[..] else {
                unreachable!("three components");
            };
            unsafe {
                crate::ffi_dwt::mercury_hwy_forward_ict(
                    cut[0].as_ptr(),
                    cut[1].as_ptr(),
                    cut[2].as_ptr(),
                    dc_shift,
                    luma.as_mut_ptr(),
                    chroma_blue.as_mut_ptr(),
                    chroma_red.as_mut_ptr(),
                    width as i32,
                );
            }
        } else {
            for (sample, &value) in shifted[0].iter_mut().zip(&cut[0]) {
                *sample = (value + dc_shift) as f32;
            }
        }
        for (encoder, row) in encoders.iter_mut().zip(&shifted) {
            encoder.push_row(|target| target.copy_from_slice(row));
        }
    }
}
