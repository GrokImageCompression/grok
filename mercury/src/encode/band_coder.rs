use std::sync::Arc;

use crate::capi::MercuryT1EncodeFn;
use crate::ffi_dwt::{mercury_hwy_quantize_16, mercury_hwy_quantize_97, mercury_rate_convex_hull};

use super::stripe_queue::{StripeJob, StripeQueue, StripeResults};

// classic's maxCodePassesPerBlock
const MAX_CODING_PASSES: usize = 94;

pub(crate) trait QuantizedSample: Copy + Default + Send + 'static {
    // SAFETY: `stripe` spans `rows` rows of `columns` samples `stride` apart, `block` columns * rows
    unsafe fn quantize(
        stripe: *const Self,
        stride: i32,
        columns: i32,
        rows: i32,
        inverse_step: f64,
        block: *mut i32,
    ) -> u32;
}

impl QuantizedSample for f32 {
    unsafe fn quantize(
        stripe: *const f32,
        stride: i32,
        columns: i32,
        rows: i32,
        inverse_step: f64,
        block: *mut i32,
    ) -> u32 {
        unsafe { mercury_hwy_quantize_97(stripe, stride, columns, rows, inverse_step, block) }
    }
}

impl QuantizedSample for i16 {
    unsafe fn quantize(
        stripe: *const i16,
        stride: i32,
        columns: i32,
        rows: i32,
        inverse_step: f64,
        block: *mut i32,
    ) -> u32 {
        unsafe { mercury_hwy_quantize_16(stripe, stride, columns, rows, inverse_step, block) }
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

    pub fn append(&mut self, mut other: CodedBand) {
        let pass_offset = self.passes.len() as u32;
        let byte_offset = self.bytes.len() as u32;
        for block in &mut other.blocks {
            block.first_pass += pass_offset;
            block.first_byte += byte_offset;
        }
        self.blocks.append(&mut other.blocks);
        self.passes.append(&mut other.passes);
        self.bytes.append(&mut other.bytes);
    }
}

pub(crate) struct BandGeometry {
    pub x0: u32,
    pub y0: u32,
    pub width: usize,
    pub height: usize,
}

#[derive(Clone, Copy)]
pub(crate) struct BandT1Params {
    pub orientation: i32,
    pub compno: i32,
    pub level: i32,
    pub stepsize: f32,
    pub mct: bool,
}

#[derive(Clone, Copy)]
pub(crate) struct StripeLayout {
    pub width: usize,
    pub x0: usize,
    pub block_width: usize,
    pub inverse_step: f64,
    pub t1_params: BandT1Params,
    pub t1: MercuryT1EncodeFn,
}

pub(crate) fn code_stripe<T: QuantizedSample>(
    layout: &StripeLayout,
    stripe: &[T],
    rows: usize,
    block: &mut Vec<i32>,
    coded: &mut CodedBand,
) {
    let width = layout.width;
    let block_width = layout.block_width;
    block.resize(block_width * rows, 0);
    let mut column = 0;
    while column < width {
        let boundary = ((layout.x0 + column) / block_width + 1) * block_width;
        let columns = boundary.min(layout.x0 + width) - (layout.x0 + column);
        let max_magnitude = unsafe {
            T::quantize(
                stripe[column..].as_ptr(),
                width as i32,
                columns as i32,
                rows as i32,
                layout.inverse_step,
                block.as_mut_ptr(),
            )
        };
        code_block(layout, block, columns, rows, max_magnitude, coded);
        column += columns;
    }
}

fn code_block(
    layout: &StripeLayout,
    block: &[i32],
    columns: usize,
    rows: usize,
    max_magnitude: u32,
    coded_band: &mut CodedBand,
) {
    let mut coded: *const u8 = std::ptr::null();
    let mut rates = [0u16; MAX_CODING_PASSES];
    let mut distortions = [0f64; MAX_CODING_PASSES];
    let mut terminated = [0u8; MAX_CODING_PASSES];
    let mut numbps = 0i32;
    let params = &layout.t1_params;
    let passes = unsafe {
        (layout.t1)(
            block.as_ptr(),
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

struct PooledStripes<T> {
    queue: Arc<StripeQueue<T>>,
    results: Arc<StripeResults>,
    stripes_sent: usize,
}

// one irreversible subband: collects a block row of rows, then quantizes and codes its blocks
pub(crate) struct BandCoder<T: QuantizedSample> {
    layout: StripeLayout,
    y0: usize,
    height: usize,
    block_height: usize,
    stripe: Vec<T>,
    stripe_first_row: usize,
    rows_in_stripe: usize,
    block: Vec<i32>,
    pooled: Option<PooledStripes<T>>,
    pub coded: CodedBand,
}

impl<T: QuantizedSample> BandCoder<T> {
    pub fn warp(
        geometry: BandGeometry,
        t1_params: BandT1Params,
        inverse_step: f64,
        block_width: u32,
        block_height: u32,
        t1: MercuryT1EncodeFn,
        stripe_queue: Option<Arc<StripeQueue<T>>>,
    ) -> Self {
        Self {
            layout: StripeLayout {
                width: geometry.width,
                x0: geometry.x0 as usize,
                block_width: block_width as usize,
                inverse_step,
                t1_params,
                t1,
            },
            y0: geometry.y0 as usize,
            height: geometry.height,
            block_height: block_height as usize,
            stripe: vec![T::default(); geometry.width * block_height as usize],
            stripe_first_row: 0,
            rows_in_stripe: 0,
            block: Vec::new(),
            pooled: stripe_queue.map(|queue| PooledStripes {
                queue,
                results: Arc::default(),
                stripes_sent: 0,
            }),
            coded: CodedBand::default(),
        }
    }

    pub fn next_row(&mut self) -> &mut [T] {
        let width = self.layout.width;
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
        let band_end = self.y0 + self.height;
        let first = self.y0 + self.stripe_first_row;
        let boundary = (first / self.block_height + 1) * self.block_height;
        boundary.min(band_end) - first
    }

    fn code_stripe(&mut self) {
        let rows = self.rows_in_stripe;
        let Some(pooled) = &mut self.pooled else {
            code_stripe(
                &self.layout,
                &self.stripe,
                rows,
                &mut self.block,
                &mut self.coded,
            );
            return;
        };
        let next = pooled.queue.spare_buffer(self.stripe.len());
        let job = StripeJob {
            samples: std::mem::replace(&mut self.stripe, next),
            rows,
            layout: self.layout,
            results: pooled.results.clone(),
            index: pooled.stripes_sent,
        };
        pooled.stripes_sent += 1;
        // a full queue means the workers are behind
        if let Err(job) = pooled.queue.try_push(job) {
            pooled.queue.recycle(job.code(&mut self.block));
        }
    }

    // after the workers have finished
    pub fn collect_stripes(&mut self) {
        let Some(pooled) = self.pooled.take() else {
            return;
        };
        for stripe in pooled.results.take() {
            self.coded.append(stripe);
        }
    }
}
