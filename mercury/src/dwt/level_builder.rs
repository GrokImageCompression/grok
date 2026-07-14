//! Direct construction of [`Synthesis`] engines from band dimensions —
//! computes per-level synthesis geometry directly in Rust.
//!
//! Used by the decode pipeline ([`crate::decode::graph`]) and golden tests.

use super::synthesis::{SamplePrec, StepSpec, Synthesis, SynthesisParams};
use super::{ALIGN_SAMPLES16, ALIGN_SAMPLES32, KERNEL_W5X3, KERNEL_W9X7};

/// A band's position and size (JPEG 2000 canvas coordinates, `pos + size`).
#[derive(Clone, Copy, Debug)]
pub struct BandDims {
    pub x0: i32,
    pub y0: i32,
    pub w: i32,
    pub h: i32,
}

/// The five geometry inputs of one resolution level.
#[derive(Clone, Copy, Debug)]
pub struct LevelSpec {
    /// This level's output region (the resolution's dims).
    pub node: BandDims,
    pub ll: BandDims,
    pub hl: BandDims,
    pub lh: BandDims,
}

/// Compute `SynthesisParams` for a level with both transforms present
/// (`vert_xform_exists && hor_xform_exists`).
pub fn draft_params(
    spec: &LevelSpec,
    sample_prec: SamplePrec,
    reversible: bool,
    num_steps: i32,
    symmetric_extension: bool,
) -> SynthesisParams {
    let node = &spec.node;
    let y_min_out = node.y0;
    let y_max_out = node.y0 + node.h - 1;
    let x_min_out = node.x0;
    let x_max_out = node.x0 + node.w - 1;
    let pull_offset = 0;
    let x_min_pull = x_min_out - pull_offset;
    let mut x_min_buf = x_min_pull;

    let empty = node.w <= 0 || node.h <= 0;
    if empty {
        let mut p = bare_params(sample_prec, reversible);
        p.empty = true;
        p.unit_height = y_min_out == y_max_out;
        p.unit_width = x_min_out == x_max_out;
        return p;
    }

    // --- vertical geometry ---
    let mut unit_height = false;
    let mut y_min_in = [0i32; 2];
    let mut y_max_in = [0i32; 2];
    y_min_in[0] = spec.ll.y0 << 1;
    y_max_in[0] = (spec.ll.y0 + spec.ll.h - 1) << 1;
    y_min_in[1] = (spec.lh.y0 << 1) + 1;
    y_max_in[1] = ((spec.lh.y0 + spec.lh.h - 1) << 1) + 1;
    if spec.lh.h <= 0 {
        unit_height = true;
        assert_eq!(y_min_in[0], y_max_in[0]);
        y_min_in[1] = y_min_in[0] + 1;
        y_max_in[1] = y_min_in[1] - 2;
    }
    if spec.ll.h <= 0 {
        unit_height = true;
        assert_eq!(y_min_in[1], y_max_in[1]);
        y_min_in[0] = y_min_in[1] + 1;
        y_max_in[0] = y_min_in[0] - 2;
    }

    // --- horizontal geometry ---
    let mut unit_width = false;
    let mut min_in = [spec.ll.x0 << 1, (spec.hl.x0 << 1) + 1];
    let mut max_in = [
        (spec.ll.x0 + spec.ll.w - 1) << 1,
        ((spec.hl.x0 + spec.hl.w - 1) << 1) + 1,
    ];
    if spec.hl.w <= 0 {
        unit_width = true;
        assert_eq!(min_in[0], max_in[0]);
        min_in[1] = min_in[0] + 1;
        max_in[1] = min_in[1] - 2;
    }
    if spec.ll.w <= 0 {
        unit_width = true;
        assert_eq!(min_in[1], max_in[1]);
        min_in[0] = min_in[1] + 1;
        max_in[0] = min_in[0] - 2;
    }
    let x_min_in = min_in[0].min(min_in[1]);
    let x_max_in = max_in[0].max(max_in[1]);

    let alignment = match sample_prec {
        SamplePrec::I16 => ALIGN_SAMPLES16,
        _ => ALIGN_SAMPLES32,
    };
    while x_min_in < x_min_buf {
        x_min_buf -= alignment;
    }

    let phase_off_in = [
        ((x_min_in + 1) >> 1) - ((x_min_buf + 1) >> 1),
        (x_min_in >> 1) - (x_min_buf >> 1),
    ];
    let phase_width_in = [
        1 + (x_max_in >> 1) - ((x_min_in + 1) >> 1),
        1 + ((x_max_in - 1) >> 1) - (x_min_in >> 1),
    ];
    let request_offset = [
        spec.ll.x0 - ((x_min_buf + 1) >> 1),
        spec.hl.x0 - (x_min_buf >> 1),
    ];
    let request_width = [spec.ll.w, spec.hl.w];
    let left_fill = [
        request_offset[0] - phase_off_in[0],
        request_offset[1] - phase_off_in[1],
    ];
    let right_fill = [
        (phase_off_in[0] + phase_width_in[0]) - (request_offset[0] + request_width[0]),
        (phase_off_in[1] + phase_width_in[1]) - (request_offset[1] + request_width[1]),
    ];
    assert!(
        left_fill.iter().chain(right_fill.iter()).all(|&f| (0..256).contains(&f)),
        "fill out of range: {left_fill:?} {right_fill:?}"
    );

    let phase_off_out = [
        ((x_min_out + 1) >> 1) - ((x_min_buf + 1) >> 1),
        (x_min_out >> 1) - (x_min_buf >> 1),
    ];
    let phase_width_out = [
        1 + (x_max_out >> 1) - ((x_min_out + 1) >> 1),
        1 + ((x_max_out - 1) >> 1) - (x_min_out >> 1),
    ];

    SynthesisParams {
        y_min_out,
        y_max_out,
        x_min_out,
        x_max_out,
        x_min_pull,
        x_min_buf,
        y_min_in,
        y_max_in,
        x_min_in,
        x_max_in,
        phase_off_in,
        phase_width_in,
        request_offset,
        request_width,
        left_fill,
        right_fill,
        phase_off_out,
        phase_width_out,
        num_vert_steps: num_steps,
        num_hor_steps: num_steps,
        vert_symmetric_extension: symmetric_extension,
        hor_symmetric_extension: symmetric_extension,
        reversible,
        vertical_first: false,
        unit_height,
        unit_width,
        empty: false,
        sample_prec,
    }
}

fn bare_params(sample_prec: SamplePrec, reversible: bool) -> SynthesisParams {
    SynthesisParams {
        y_min_out: 0,
        y_max_out: 0,
        x_min_out: 0,
        x_max_out: 0,
        x_min_pull: 0,
        x_min_buf: 0,
        y_min_in: [0; 2],
        y_max_in: [0; 2],
        x_min_in: 0,
        x_max_in: 0,
        phase_off_in: [0; 2],
        phase_width_in: [0; 2],
        request_offset: [0; 2],
        request_width: [0; 2],
        left_fill: [0; 2],
        right_fill: [0; 2],
        phase_off_out: [0; 2],
        phase_width_out: [0; 2],
        num_vert_steps: 0,
        num_hor_steps: 0,
        vert_symmetric_extension: false,
        hor_symmetric_extension: false,
        reversible,
        vertical_first: false,
        unit_height: false,
        unit_width: false,
        empty: false,
        sample_prec,
    }
}

fn treadle_step(
    support_min: i16,
    downshift: u8,
    rounding_offset: i16,
    coeffs: [f32; 2],
    icoeffs: [i32; 2],
    kernel_id: u8,
) -> StepSpec {
    StepSpec {
        support_min,
        support_length: 2,
        downshift,
        rounding_offset,
        coeffs: coeffs.to_vec(),
        icoeffs: icoeffs.to_vec(),
        kernel_id,
    }
}

/// W5X3 reversible lifting-step table: step 0 = predict {-1/2,-1/2} >>1
/// round 1, step 1 = update {1/4,1/4} >>2 round 2.
/// `icoeffs = coeff * (1<<downshift)`.
pub fn w5x3_draft() -> Vec<StepSpec> {
    vec![
        treadle_step(0, 1, 1, [-0.5, -0.5], [-1, -1], KERNEL_W5X3),
        treadle_step(-1, 2, 2, [0.25, 0.25], [1, 1], KERNEL_W5X3),
    ]
}

/// Build one W5X3 reversible engine at the given sample precision (I16 when
/// every band's coefficients fit 15 bits + sign; I32 for higher precision).
pub fn warp_w5x3_prec(spec: &LevelSpec, prec: SamplePrec) -> Synthesis {
    let params = draft_params(spec, prec, true, 2, true);
    let steps = w5x3_draft();
    Synthesis::warp(params, &steps, &steps)
}

/// W9X7 irreversible lifting-step table: four float steps, no downshift;
/// synthesis subtracts these analysis-direction factors.
pub fn w9x7_draft() -> Vec<StepSpec> {
    const A: f32 = -1.586134342;
    const B: f32 = -0.052980118;
    const C: f32 = 0.882911075;
    const D: f32 = 0.443506852;
    vec![
        treadle_step(0, 0, 0, [A, A], [0, 0], KERNEL_W9X7),
        treadle_step(-1, 0, 0, [B, B], [0, 0], KERNEL_W9X7),
        treadle_step(0, 0, 0, [C, C], [0, 0], KERNEL_W9X7),
        treadle_step(-1, 0, 0, [D, D], [0, 0], KERNEL_W9X7),
    ]
}

/// Build one W9X7 irreversible engine (f32 samples in i32 lines).
pub fn warp_w9x7(spec: &LevelSpec) -> Synthesis {
    let params = draft_params(spec, SamplePrec::I32, false, 4, true);
    let steps = w9x7_draft();
    Synthesis::warp(params, &steps, &steps)
}

/// W9X7 `low_scale`/`high_scale` (1/DC gain of the derived low analysis
/// filter, 1/Nyquist gain of the high), in f32. Numerically (1/K, K/2)
/// with K = 1.2301741.
pub fn w9x7_gains() -> (f32, f32) {
    let support_min = [0i32, -1, 0, -1];
    let factors: [f32; 4] = [-1.586134342, -0.052980118, 0.882911075, 0.443506852];
    let num_steps = 4usize;
    const L: i32 = 16; // ample for the 9/7 support
    let idx = |i: i32| (i + L) as usize;

    let mut low_analysis: Vec<f32> = Vec::new();
    let mut high_analysis: Vec<f32> = Vec::new();
    for which in 0..2i32 {
        // Inverse-lift a coefficient impulse to get the synthesis branch.
        let mut buf = [[0.0f32; (2 * L + 1) as usize]; 2];
        let mut bmin = [1i32, 1];
        let mut bmax = [-1i32, -1];
        buf[which as usize][idx(0)] = 1.0;
        bmin[which as usize] = 0;
        bmax[which as usize] = 0;
        for s in (0..num_steps).rev() {
            let sp = s & 1;
            if bmax[sp] < bmin[sp] {
                continue;
            }
            let ns = support_min[s];
            let ps = 2 - 1 + ns;
            let other = 1 - sp;
            let lo = bmin[sp] - ps;
            let hi = bmax[sp] - ns;
            if bmax[other] < bmin[other] {
                bmin[other] = lo;
                bmax[other] = hi;
            } else {
                bmin[other] = bmin[other].min(lo);
                bmax[other] = bmax[other].max(hi);
            }
            for i in bmin[sp]..=bmax[sp] {
                let val = buf[sp][idx(i)];
                for n in ns..=ps {
                    // Both taps of each step share one factor.
                    buf[other][idx(i - n)] -= val * factors[s];
                }
            }
        }
        // Interleave phases into the synthesis filter, derive analysis by
        // sign alternation (which=0 -> high analysis; which=1 -> low).
        let mut syn = vec![0.0f32; (4 * L + 1) as usize];
        let sidx = |i: i32| (i + 2 * L) as usize;
        for s in 0..2i32 {
            if bmax[s as usize] < bmin[s as usize] {
                continue;
            }
            for nn in bmin[s as usize]..=bmax[s as usize] {
                syn[sidx(2 * nn + s - which)] = buf[s as usize][idx(nn)];
            }
        }
        let ana: Vec<f32> = (-2 * L..=2 * L)
            .map(|n| if n & 1 != 0 { -syn[sidx(n)] } else { syn[sidx(n)] })
            .collect();
        if which == 0 {
            high_analysis = ana;
        } else {
            low_analysis = ana;
        }
    }
    // Gains summed in ascending tap order.
    let mut low_gain = 0.0f32;
    for &t in &low_analysis {
        low_gain += t;
    }
    let mut high_gain = 0.0f32;
    for (n, &t) in high_analysis.iter().enumerate() {
        let signed_n = n as i32 - 2 * L;
        high_gain += if signed_n & 1 != 0 { -t } else { t };
    }
    (1.0 / low_gain, 1.0 / high_gain)
}

