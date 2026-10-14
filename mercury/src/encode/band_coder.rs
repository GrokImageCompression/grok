use crate::capi::MercuryT1EncodeFn;
use crate::ffi_dwt::{mercury_hwy_quantize_97, mercury_rate_convex_hull};

// classic's maxCodePassesPerBlock
const MAX_CODING_PASSES: usize = 94;

#[derive(Default, Clone, Copy)]
pub(crate) struct CodingStats {
    pub coded_bytes: u64,
    pub coding_passes: u64,
    pub blocks: u64,
}

impl CodingStats {
    pub fn add(&mut self, other: CodingStats) {
        self.coded_bytes += other.coded_bytes;
        self.coding_passes += other.coding_passes;
        self.blocks += other.blocks;
    }
}

#[derive(Clone, Copy)]
pub(crate) struct CodedPass {
    // cumulative bytes through this pass
    pub rate: u16,
    // 0 when the pass is off the convex hull
    pub slope: u16,
}

pub(crate) struct CodedBlock {
    pub numbps: u8,
    pub first_pass: u32,
    pub passes: u8,
    pub first_byte: u32,
}

// a band's code-blocks in raster order, with their passes and bytes packed end to end
#[derive(Default)]
pub(crate) struct CodedBand {
    pub blocks: Vec<CodedBlock>,
    pub passes: Vec<CodedPass>,
    pub bytes: Vec<u8>,
}

impl CodedBand {
    pub fn block_passes(&self, block: &CodedBlock) -> &[CodedPass] {
        let first = block.first_pass as usize;
        &self.passes[first..first + block.passes as usize]
    }

    pub fn block_bytes(&self, block: &CodedBlock) -> &[u8] {
        let length = self.block_passes(block).last().map_or(0, |pass| pass.rate);
        let first = block.first_byte as usize;
        &self.bytes[first..first + length as usize]
    }

    pub fn stats(&self) -> CodingStats {
        CodingStats {
            coded_bytes: self.bytes.len() as u64,
            coding_passes: self.passes.len() as u64,
            blocks: self.blocks.len() as u64,
        }
    }
}

pub(crate) struct BandGeometry {
    pub x0: u32,
    pub y0: u32,
    pub width: usize,
    pub height: usize,
}

pub(crate) struct BandT1Params {
    pub orientation: i32,
    pub compno: i32,
    pub level: i32,
    pub stepsize: f32,
    pub mct: bool,
}

// one irreversible subband: collects a block row of rows, then quantizes and codes its blocks
pub(crate) struct BandCoder {
    geometry: BandGeometry,
    t1_params: BandT1Params,
    block_width: u32,
    block_height: u32,
    t1: MercuryT1EncodeFn,
    stripe: Vec<f32>,
    stripe_first_row: usize,
    rows_in_stripe: usize,
    block: Vec<i32>,
    pub coded: CodedBand,
}

impl BandCoder {
    pub fn warp(
        geometry: BandGeometry,
        t1_params: BandT1Params,
        block_width: u32,
        block_height: u32,
        t1: MercuryT1EncodeFn,
    ) -> Self {
        Self {
            stripe: vec![0.0; geometry.width * block_height as usize],
            block: vec![0; (block_width * block_height) as usize],
            geometry,
            t1_params,
            block_width,
            block_height,
            t1,
            stripe_first_row: 0,
            rows_in_stripe: 0,
            coded: CodedBand::default(),
        }
    }

    pub fn next_row(&mut self) -> &mut [f32] {
        let width = self.geometry.width;
        let start = self.rows_in_stripe * width;
        &mut self.stripe[start..start + width]
    }

    pub fn row_done(&mut self) {
        self.rows_in_stripe += 1;
        if self.rows_in_stripe == self.stripe_height() {
            self.code_stripe();
            self.stripe_first_row += self.rows_in_stripe;
            self.rows_in_stripe = 0;
        }
    }

    // rows from the stripe's first row to the next block boundary or the band end
    fn stripe_height(&self) -> usize {
        let band_end = self.geometry.y0 as usize + self.geometry.height;
        let first = self.geometry.y0 as usize + self.stripe_first_row;
        let boundary = (first / self.block_height as usize + 1) * self.block_height as usize;
        boundary.min(band_end) - first
    }

    fn code_stripe(&mut self) {
        let width = self.geometry.width;
        let block_width = self.block_width as usize;
        let quant = 1.0 / self.t1_params.stepsize as f64;
        let rows = self.rows_in_stripe;
        let band_x0 = self.geometry.x0 as usize;
        let mut column = 0;
        while column < width {
            let boundary = ((band_x0 + column) / block_width + 1) * block_width;
            let columns = boundary.min(band_x0 + width) - (band_x0 + column);
            let max_magnitude = unsafe {
                mercury_hwy_quantize_97(
                    self.stripe[column..].as_ptr(),
                    width as i32,
                    columns as i32,
                    rows as i32,
                    quant,
                    self.block.as_mut_ptr(),
                )
            };
            self.code_block(columns, rows, max_magnitude);
            column += columns;
        }
    }

    fn code_block(&mut self, columns: usize, rows: usize, max_magnitude: u32) {
        let mut coded: *const u8 = std::ptr::null();
        let mut rates = [0u16; MAX_CODING_PASSES];
        let mut distortions = [0f64; MAX_CODING_PASSES];
        let mut terminated = [0u8; MAX_CODING_PASSES];
        let mut numbps = 0i32;
        let params = &self.t1_params;
        let passes = unsafe {
            (self.t1)(
                self.block.as_ptr(),
                columns as i32,
                rows as i32,
                max_magnitude,
                params.orientation,
                params.compno,
                params.level,
                params.stepsize as f64,
                params.mct as i32,
                &mut coded,
                rates.as_mut_ptr(),
                distortions.as_mut_ptr(),
                terminated.as_mut_ptr(),
                &mut numbps,
            )
        };
        assert!(passes >= 0, "T1 encode failed on a {columns}x{rows} block");
        let passes = passes as usize;
        let mut lengths = [0u16; MAX_CODING_PASSES];
        let mut previous_rate = 0;
        for (length, &rate) in lengths.iter_mut().zip(&rates[..passes]) {
            *length = rate - previous_rate;
            previous_rate = rate;
        }
        let mut slopes = [0u16; MAX_CODING_PASSES];
        unsafe {
            mercury_rate_convex_hull(
                lengths.as_ptr(),
                distortions.as_ptr(),
                slopes.as_mut_ptr(),
                passes as u32,
            );
        }
        let coded_band = &mut self.coded;
        coded_band.blocks.push(CodedBlock {
            numbps: numbps as u8,
            first_pass: coded_band.passes.len() as u32,
            passes: passes as u8,
            first_byte: coded_band.bytes.len() as u32,
        });
        coded_band.passes.extend((0..passes).map(|p| CodedPass {
            rate: rates[p],
            slope: slopes[p],
        }));
        if passes > 0 {
            let length = rates[passes - 1] as usize;
            coded_band
                .bytes
                .extend_from_slice(unsafe { std::slice::from_raw_parts(coded, length) });
        }
    }
}
