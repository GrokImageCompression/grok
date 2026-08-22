//! Direct construction of [`Synthesis`] engines from band dimensions —
//! computes per-level synthesis geometry directly in Rust.
//!
//! Used by the decode pipeline ([`crate::decode::graph`]) and golden tests.

use super::synthesis::{SamplePrec, StepSpec, Synthesis, SynthesisParams};
use super::{KERNEL_W5X3, KERNEL_W9X7, align_samples16, align_samples32};
use crate::decode::DecodeError;

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
///
/// Band geometry comes from the codestream, so a corrupt one can break the
/// band-splitting relations this relies on. Those are rejected as errors
/// rather than asserted, so the host gets a decode failure and falls back.
pub fn draft_params(
    spec: &LevelSpec,
    sample_prec: SamplePrec,
    reversible: bool,
    num_steps: i32,
    symmetric_extension: bool,
) -> Result<SynthesisParams, DecodeError> {
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
        return Ok(p);
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
        require_single_line(y_min_in[0], y_max_in[0], "lh", "height", "ll")?;
        y_min_in[1] = y_min_in[0] + 1;
        y_max_in[1] = y_min_in[1] - 2;
    }
    if spec.ll.h <= 0 {
        unit_height = true;
        require_single_line(y_min_in[1], y_max_in[1], "ll", "height", "lh")?;
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
        require_single_line(min_in[0], max_in[0], "hl", "width", "ll")?;
        min_in[1] = min_in[0] + 1;
        max_in[1] = min_in[1] - 2;
    }
    if spec.ll.w <= 0 {
        unit_width = true;
        require_single_line(min_in[1], max_in[1], "ll", "width", "hl")?;
        min_in[0] = min_in[1] + 1;
        max_in[0] = min_in[0] - 2;
    }
    let x_min_in = min_in[0].min(min_in[1]);
    let x_max_in = max_in[0].max(max_in[1]);

    let alignment = match sample_prec {
        SamplePrec::I16 => align_samples16(),
        _ => align_samples32(),
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
    if !left_fill
        .iter()
        .chain(right_fill.iter())
        .all(|&f| (0..256).contains(&f))
    {
        return Err(DecodeError::Logic(format!(
            "level fill out of range: left {left_fill:?} right {right_fill:?}"
        )));
    }

    let phase_off_out = [
        ((x_min_out + 1) >> 1) - ((x_min_buf + 1) >> 1),
        (x_min_out >> 1) - (x_min_buf >> 1),
    ];
    let phase_width_out = [
        1 + (x_max_out >> 1) - ((x_min_out + 1) >> 1),
        1 + ((x_max_out - 1) >> 1) - (x_min_out >> 1),
    ];

    Ok(SynthesisParams {
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
    })
}

/// A subband with no extent along one axis means the level is one sample across
/// it, so the opposite subband has to be a single line. `empty` and `other` name
/// the two subbands for the error message.
fn require_single_line(
    min: i32,
    max: i32,
    empty: &str,
    axis: &str,
    other: &str,
) -> Result<(), DecodeError> {
    if min == max {
        return Ok(());
    }
    Err(DecodeError::Logic(format!(
        "{empty} subband has no {axis} but {other} spans {min}..{max}"
    )))
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
pub fn warp_w5x3_prec(spec: &LevelSpec, prec: SamplePrec) -> Result<Synthesis, DecodeError> {
    let params = draft_params(spec, prec, true, 2, true)?;
    let steps = w5x3_draft();
    Ok(Synthesis::warp(params, &steps, &steps))
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
pub fn warp_w9x7(spec: &LevelSpec) -> Result<Synthesis, DecodeError> {
    let params = draft_params(spec, SamplePrec::I32, false, 4, true)?;
    let steps = w9x7_draft();
    Ok(Synthesis::warp(params, &steps, &steps))
}

/// W9X7 int16 fixed-point lifting-step table (Q13 samples, see
/// [`super::fixed97`]). Same step geometry as [`w9x7_draft`]; the arithmetic
/// is hardcoded in kernels keyed on `step_idx`, so the coefficient fields
/// are informational.
pub fn w9x7_i16_draft() -> Vec<StepSpec> {
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

/// Build one W9X7 irreversible engine on int16 Q13 fixed-point samples.
pub fn warp_w9x7_i16(spec: &LevelSpec) -> Result<Synthesis, DecodeError> {
    let params = draft_params(spec, SamplePrec::I16, false, 4, true)?;
    let steps = w9x7_i16_draft();
    Ok(Synthesis::warp(params, &steps, &steps))
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
            .map(|n| {
                if n & 1 != 0 {
                    -syn[sidx(n)]
                } else {
                    syn[sidx(n)]
                }
            })
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

#[cfg(test)]
mod tests {
    use super::*;

    fn bd(w: i32, h: i32) -> BandDims {
        BandDims { x0: 0, y0: 0, w, h }
    }

    fn spec(node_h: i32, ll_h: i32, lh_h: i32) -> LevelSpec {
        LevelSpec {
            node: bd(4, node_h),
            ll: bd(2, ll_h),
            hl: bd(2, ll_h),
            lh: bd(2, lh_h),
        }
    }

    fn draft(spec: &LevelSpec) -> Result<SynthesisParams, DecodeError> {
        draft_params(spec, SamplePrec::I32, true, 2, true)
    }

    /// `Result::expect_err` needs `Debug` on the success type, which
    /// `SynthesisParams` does not derive.
    fn reject_reason(spec: &LevelSpec) -> String {
        match draft(spec) {
            Ok(_) => panic!("inconsistent geometry must be rejected"),
            Err(DecodeError::Logic(msg)) => msg,
        }
    }

    #[test]
    fn accepts_a_normal_level() {
        assert!(draft(&spec(4, 2, 2)).is_ok());
    }

    #[test]
    fn accepts_a_single_row_level() {
        // node one row tall: lh is legitimately empty and ll is a single line
        let params = draft(&spec(1, 1, 0)).expect("single-row level must be accepted");
        assert!(params.unit_height);
    }

    #[test]
    fn rejects_an_empty_subband_beside_a_tall_one() {
        // the v4dwt_interleave_h.gsr105.j2k geometry: lh has no height while ll
        // spans 100 rows, which used to trip an assert and panic
        let msg = reject_reason(&spec(100, 100, 0));
        assert!(msg.contains("lh subband has no height"), "{msg}");
    }

    use super::super::synthesis::{PullStatus, RowSource};

    /// Deterministic band sample at absolute band coordinates.
    fn band_sample(sb: usize, x: i32, y: i32) -> i32 {
        (x * 31 + y * 17 + sb as i32 * 101).rem_euclid(64) - 32
    }

    /// Serves band rows from `band_sample`, starting at each band's window
    /// origin — the same values a full source serves on the overlap.
    struct ArraySource {
        /// per band: (x0, y0, width) of the served window
        origin: [(i32, i32, i32); 4],
        next_row: [i32; 4],
    }

    impl ArraySource {
        fn warp(origin: [(i32, i32, i32); 4]) -> Self {
            let next_row = [origin[0].1, origin[1].1, origin[2].1, origin[3].1];
            ArraySource { origin, next_row }
        }
    }

    impl RowSource for ArraySource {
        fn subband_ready(&self, _sb: i32) -> bool {
            true
        }
        unsafe fn pack(&mut self, sb: i32, buf: *mut u8, width: i32) {
            let (x0, _, w) = self.origin[sb as usize];
            assert_eq!(w, width, "band {sb} width");
            let y = self.next_row[sb as usize];
            self.next_row[sb as usize] += 1;
            let out = buf as *mut i32;
            for i in 0..width {
                unsafe { *out.add(i as usize) = band_sample(sb as usize, x0 + i, y) };
            }
        }
    }

    /// Pull every row the spec's node describes; returns rows of node-width
    /// i32 samples.
    fn pull_all(spec: &LevelSpec) -> Vec<Vec<i32>> {
        use super::super::synthesis::AlignedVec;
        let engine = warp_w5x3_prec(spec, SamplePrec::I32).expect("engine must build");
        let mut engine = engine;
        let mut src = ArraySource::warp([
            (spec.ll.x0, spec.ll.y0, spec.ll.w),
            (spec.hl.x0, spec.hl.y0, spec.hl.w),
            (spec.lh.x0, spec.lh.y0, spec.lh.w),
            (spec.hl.x0, spec.lh.y0, spec.hl.w), // hh: hl's width, lh's height
        ]);
        let mut rows = Vec::new();
        let mut buf = AlignedVec::bare(engine.hem_row_bytes().max(64));
        for _ in 0..spec.node.h {
            match unsafe { engine.try_draw(buf.as_mut_ptr(), &mut src) } {
                PullStatus::Row => {
                    let row = unsafe {
                        std::slice::from_raw_parts(buf.as_ptr() as *const i32, spec.node.w as usize)
                    };
                    rows.push(row.to_vec());
                }
                PullStatus::Stalled => panic!("engine stalled with an always-ready source"),
            }
        }
        rows
    }

    /// A windowed spec (a padded sub-rect of the full level, with its B-15
    /// band splits) must synthesize the window's interior bit-exact against
    /// the full-level engine.
    #[test]
    fn windowed_specs_match_the_full_synthesis() {
        let full = LevelSpec {
            node: BandDims {
                x0: 0,
                y0: 0,
                w: 64,
                h: 48,
            },
            ll: BandDims {
                x0: 0,
                y0: 0,
                w: 32,
                h: 24,
            },
            hl: BandDims {
                x0: 0,
                y0: 0,
                w: 32,
                h: 24,
            },
            lh: BandDims {
                x0: 0,
                y0: 0,
                w: 32,
                h: 24,
            },
        };
        let reference = pull_all(&full);

        // vertical window rows 8..40, padded to node rows 4..44
        let vertical = LevelSpec {
            node: BandDims {
                x0: 0,
                y0: 4,
                w: 64,
                h: 40,
            },
            ll: BandDims {
                x0: 0,
                y0: 2,
                w: 32,
                h: 20,
            },
            hl: BandDims {
                x0: 0,
                y0: 2,
                w: 32,
                h: 20,
            },
            lh: BandDims {
                x0: 0,
                y0: 2,
                w: 32,
                h: 20,
            },
        };
        let got = pull_all(&vertical);
        for y in 8..40 {
            assert_eq!(
                got[(y - 4) as usize],
                reference[y as usize],
                "vertical window row {y}"
            );
        }

        // horizontal window cols 16..48, padded to node cols 12..52
        let horizontal = LevelSpec {
            node: BandDims {
                x0: 12,
                y0: 0,
                w: 40,
                h: 48,
            },
            ll: BandDims {
                x0: 6,
                y0: 0,
                w: 20,
                h: 24,
            },
            hl: BandDims {
                x0: 6,
                y0: 0,
                w: 20,
                h: 24,
            },
            lh: BandDims {
                x0: 6,
                y0: 0,
                w: 20,
                h: 24,
            },
        };
        let got = pull_all(&horizontal);
        for y in 0..48 {
            assert_eq!(
                got[y as usize][(16 - 12)..(48 - 12)],
                reference[y as usize][16..48],
                "horizontal window row {y}"
            );
        }
    }

    /// The int16 Q13 9/7 engine must track the f32 engine within a few Q13
    /// LSBs (well under one sample LSB at the path's 8-bit precision limit).
    /// Sources fold the one-level 2D band gains the way the decode graph
    /// does: into the f32 deltas for the float path, into the Q13 dequant
    /// for the fixed-point path.
    #[test]
    fn q13_engine_tracks_the_float_engine() {
        use super::super::synthesis::AlignedVec;
        struct FnSource<F: FnMut(i32, *mut u8, i32)> {
            fill: F,
        }
        impl<F: FnMut(i32, *mut u8, i32)> RowSource for FnSource<F> {
            fn subband_ready(&self, _sb: i32) -> bool {
                true
            }
            unsafe fn pack(&mut self, sb: i32, buf: *mut u8, width: i32) {
                (self.fill)(sb, buf, width)
            }
        }

        let spec = LevelSpec {
            node: BandDims {
                x0: 0,
                y0: 0,
                w: 64,
                h: 48,
            },
            ll: BandDims {
                x0: 0,
                y0: 0,
                w: 32,
                h: 24,
            },
            hl: BandDims {
                x0: 0,
                y0: 0,
                w: 32,
                h: 24,
            },
            lh: BandDims {
                x0: 0,
                y0: 0,
                w: 32,
                h: 24,
            },
        };
        let (low, high) = w9x7_gains();
        let gain = [
            1.0 / (low * low),
            1.0 / (low * high),
            1.0 / (high * low),
            1.0 / (high * high),
        ];
        // normalized band values in ±0.25 — sample scale for the BIBO budget
        let value = |sb: i32, x: i32, y: i32| band_sample(sb as usize, x, y) as f32 / 128.0;

        let mut engine_f = warp_w9x7(&spec).expect("f32 engine");
        let mut engine_q = warp_w9x7_i16(&spec).expect("q13 engine");

        let mut next_f = [0i32; 4];
        let mut src_f = FnSource {
            fill: |sb: i32, buf: *mut u8, width: i32| {
                let y = next_f[sb as usize];
                next_f[sb as usize] += 1;
                let out = buf as *mut f32;
                for i in 0..width {
                    unsafe { *out.add(i as usize) = value(sb, i, y) * gain[sb as usize] };
                }
            },
        };
        let mut next_q = [0i32; 4];
        let mut src_q = FnSource {
            fill: |sb: i32, buf: *mut u8, width: i32| {
                let y = next_q[sb as usize];
                next_q[sb as usize] += 1;
                let out = buf as *mut i16;
                for i in 0..width {
                    let q = (value(sb, i, y) * gain[sb as usize] * 8192.0).round_ties_even();
                    unsafe { *out.add(i as usize) = q as i16 };
                }
            },
        };

        let mut buf_f = AlignedVec::bare(engine_f.hem_row_bytes().max(64));
        let mut buf_q = AlignedVec::bare(engine_q.hem_row_bytes().max(64));
        let mut max_diff = 0.0f32;
        for y in 0..spec.node.h {
            let s = unsafe { engine_f.try_draw(buf_f.as_mut_ptr(), &mut src_f) };
            assert_eq!(s, PullStatus::Row, "f32 row {y}");
            let s = unsafe { engine_q.try_draw(buf_q.as_mut_ptr(), &mut src_q) };
            assert_eq!(s, PullStatus::Row, "q13 row {y}");
            let row_f = unsafe {
                std::slice::from_raw_parts(buf_f.as_ptr() as *const f32, spec.node.w as usize)
            };
            let row_q = unsafe {
                std::slice::from_raw_parts(buf_q.as_ptr() as *const i16, spec.node.w as usize)
            };
            for x in 0..spec.node.w as usize {
                let diff = (row_q[x] as f32 / 8192.0 - row_f[x]).abs();
                max_diff = max_diff.max(diff);
                assert!(
                    diff <= 8.0 / 8192.0,
                    "row {y} col {x}: q13 {} vs f32 {} (max so far {max_diff})",
                    row_q[x],
                    row_f[x]
                );
            }
        }
    }

    #[test]
    fn rejects_an_empty_subband_beside_a_wide_one() {
        let wide = LevelSpec {
            node: bd(100, 4),
            ll: bd(100, 2),
            hl: bd(0, 2),
            lh: bd(100, 2),
        };
        let msg = reject_reason(&wide);
        assert!(msg.contains("hl subband has no width"), "{msg}");
    }
}
