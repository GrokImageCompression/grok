//! Horizontal synthesis — full row-level DWT inverse transform.
//!
//! Boundary extension + all horizontal lifting steps in one Rust function.
//! Calls `mercury_ply_hlift_16/32` internally (no FFI hop).
#![allow(unsafe_op_in_unsafe_fn)]

use crate::ffi_dwt::MercuryLiftingStep;

/// Parameters for horizontal synthesis.
pub struct MercuryHorSynthParams {
    pub phase_off_in: [i32; 2],
    pub phase_width_in: [i32; 2],
    pub num_hor_steps: i32,
    pub hor_symmetric_extension: bool,
    pub x_min_in: i32,
    pub x_max_in: i32,
    pub unit_width: bool,
    pub reversible: bool,
}

/// Full horizontal synthesis on a 16-bit line.
///
/// # Safety
/// - `phase0`/`phase1` point to valid phase buffers with enough negative and
///   positive extent for boundary extension.
/// - `params` valid.
/// - `steps` points to `params.num_hor_steps` valid `MercuryLiftingStep`.
pub unsafe fn mercury_hweave_16(
    phase0: *mut i16,
    phase1: *mut i16,
    params: *const MercuryHorSynthParams,
    steps: *mut MercuryLiftingStep,
) {
    let p = &*params;

    if p.num_hor_steps == 0 {
        return;
    }

    if p.unit_width {
        if p.reversible && (p.x_min_in & 1) != 0 {
            let off = p.phase_off_in[1] as usize;
            *phase1.add(off) >>= 1;
        }
        return;
    }

    let phases: [*mut i16; 2] = [phase0, phase1];

    for s in (0..p.num_hor_steps).rev() {
        let step = &mut *steps.add(s as usize);
        if step.support_length == 0 {
            continue;
        }

        let c = 1 - (s & 1);
        let width = p.phase_width_in[c as usize];
        let x_off = p.phase_off_in[c as usize];

        let sp_base = phases[(1 - c) as usize].add(p.phase_off_in[(1 - c) as usize] as usize);
        let dp = phases[c as usize].add(x_off as usize);
        let esp = sp_base.add(p.phase_width_in[(1 - c) as usize] as usize - 1);

        // Boundary extension
        let extend = step.extend as i32;
        if !p.hor_symmetric_extension {
            for k in 1..=extend {
                *sp_base.offset(-(k as isize)) = *sp_base;
                *esp.add(k as usize) = *esp;
            }
        } else {
            for k in 1..=extend {
                *sp_base.offset(-(k as isize)) =
                    *sp_base.add((k - ((p.x_min_in ^ s) & 1)) as usize);
                *esp.add(k as usize) = *esp.offset(-((k - ((p.x_max_in ^ s) & 1)) as isize));
            }
        }

        let mut sp = sp_base;
        if (p.x_min_in & 1) != 0 {
            sp = sp.offset(-((c + c - 1) as isize));
        }
        sp = sp.offset(step.support_min as isize);

        // Rust hlift engine directly (no FFI boundary).
        super::hlift::mercury_ply_hlift_16(
            sp.offset(-(x_off as isize)),
            dp.offset(-(x_off as isize)),
            width + x_off,
            step as *mut MercuryLiftingStep,
        );
    }
}

/// Full horizontal synthesis on a 32-bit line.
///
/// # Safety
/// Same as the 16-bit version, for 32-bit buffers.
pub unsafe fn mercury_hweave_32(
    phase0: *mut i32,
    phase1: *mut i32,
    params: *const MercuryHorSynthParams,
    steps: *mut MercuryLiftingStep,
) {
    let p = &*params;

    if p.num_hor_steps == 0 {
        return;
    }

    if p.unit_width {
        if p.reversible && (p.x_min_in & 1) != 0 {
            let off = p.phase_off_in[1] as usize;
            *phase1.add(off) >>= 1;
        }
        return;
    }

    let phases: [*mut i32; 2] = [phase0, phase1];

    for s in (0..p.num_hor_steps).rev() {
        let step = &mut *steps.add(s as usize);
        if step.support_length == 0 {
            continue;
        }

        let c = 1 - (s & 1);
        let width = p.phase_width_in[c as usize];
        let x_off = p.phase_off_in[c as usize];

        let sp_base = phases[(1 - c) as usize].add(p.phase_off_in[(1 - c) as usize] as usize);
        let dp = phases[c as usize].add(x_off as usize);
        let esp = sp_base.add(p.phase_width_in[(1 - c) as usize] as usize - 1);

        // Boundary extension
        let extend = step.extend as i32;
        if !p.hor_symmetric_extension {
            for k in 1..=extend {
                *sp_base.offset(-(k as isize)) = *sp_base;
                *esp.add(k as usize) = *esp;
            }
        } else {
            for k in 1..=extend {
                *sp_base.offset(-(k as isize)) =
                    *sp_base.add((k - ((p.x_min_in ^ s) & 1)) as usize);
                *esp.add(k as usize) = *esp.offset(-((k - ((p.x_max_in ^ s) & 1)) as isize));
            }
        }

        let mut sp = sp_base;
        if (p.x_min_in & 1) != 0 {
            sp = sp.offset(-((c + c - 1) as isize));
        }
        sp = sp.offset(step.support_min as isize);

        // Rust hlift engine directly (no FFI boundary).
        super::hlift::mercury_ply_hlift_32(
            sp.offset(-(x_off as isize)),
            dp.offset(-(x_off as isize)),
            width + x_off,
            step as *mut MercuryLiftingStep,
        );
    }
}
