//! C API for embedding the decoder in a host codec (the grok seam).
//!
//! Three FFI surfaces, mirrored in `include/mercury_capi.h`:
//! - **read_at**: host-supplied positioned reads (an fd wrapper is provided)
//!   — never a mapping; see [`crate::decode::read_at`].
//! - **t1**: an optional substitute tier-1 block decoder with the C shape of
//!   the [`BlockCoder`](crate::decode::stripe_decoder::BlockCoder) contract
//!   (grok passes its part-1/HTJ2K coder here).
//! - **row callback**: each full-width row, in order, as per-component `i32`
//!   sample rows in the host's convention — unsigned DC-shifted back to
//!   `[0, 2^prec)` and clamped, signed raw, floats converted per the
//!   sample-conversion spec (see `dye_comp_row`).
//!
//! Flow: `mercury_warp_loom{,_fd}` (rejects anything the plan stage can't
//! stream — host falls back to its own pipeline), `mercury_loom_info` /
//! `mercury_loom_comp_info`, then `mercury_weave`, which consumes the plan
//! handle. `mercury_unwarp_loom` only for plans never decoded.
//!
//! The row callback runs on a decode worker thread (exactly one call at a
//! time, rows strictly in order); it must not unwind. One substitute T1 per
//! process (lands in a global; last `mercury_weave` wins). The host must
//! supply the T1; a null pointer is rejected (EBADARG).

use std::ffi::c_void;
use std::io;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

use crate::codec::params::{
    CodParams, CodingModes, PrecinctSize, ProgressionOrder, QcdParams, QuantStyle, SizComponent,
    SizParams,
};
use crate::decode::graph::{Rows, weave_with_coder};
use crate::decode::plan::{DecodePlan, MainHeaderIn, draft};
use crate::decode::read_at::ReadAt;
use crate::decode::stripe_decoder::{BlockCoder, MercuryStripeBlockInfo};

/// Positioned read: fill `buf[0..len]` from absolute offset `off`.
/// Return nonzero on success, 0 on failure. Must be callable from many
/// threads concurrently.
pub type MercuryReadAtFn =
    extern "C" fn(ctx: *mut c_void, buf: *mut u8, off: u64, len: u64) -> i32;

/// Substitute tier-1 block decoder; see the `BlockCoder` contract in
/// `stripe_decoder.rs`. `out` has room for `num_cols * 4*ceil(num_rows/4)`
/// samples; write row-major sign-magnitude i32. Return nonzero on success.
pub type MercuryT1Fn = extern "C" fn(
    coded_data: *const u8,
    coded_length: i32,
    num_passes: i32,
    missing_msbs: i32,
    k_max_prime: i32,
    orientation: i32,
    modes: i32,
    num_cols: i32,
    num_rows: i32,
    segment_lengths: *const i32,
    num_segments: i32,
    out: *mut i32,
) -> i32;

/// Row delivery: `comps` points at `num_comps` row pointers, each `width`
/// i32 samples. Pointers are valid only for the duration of the call.
pub type MercuryRowFn = extern "C" fn(
    ctx: *mut c_void,
    row: u32,
    comps: *const *const i32,
    num_comps: u32,
    width: u64,
);

pub const MERCURY_OK: i32 = 0;
pub const MERCURY_EBADARG: i32 = -1;
pub const MERCURY_EDECODE: i32 = -2;
pub const MERCURY_EPANIC: i32 = -3;

// ─── Main header (parsed by the host, handed in) ─────────────────────────────
//
// The host codec fills these `#[repr(C)]` structs (mirrored in
// include/mercury_capi.h) from its own parse and passes them to
// `mercury_warp_loom{,_fd}`. Every pointer is borrowed for that one call —
// mercury copies what it keeps.

/// Per-component SIZ info.
#[repr(C)]
pub struct MercurySizComp {
    /// Bit depth, 1..38.
    pub precision: u8,
    /// 0 = unsigned, nonzero = signed.
    pub is_signed: bool,
    /// Horizontal / vertical sub-sampling factors.
    pub xr_siz: u8,
    pub yr_siz: u8,
}

/// One resolution level's precinct size (powers of two).
#[repr(C)]
pub struct MercuryPrecinct {
    pub width: u32,
    pub height: u32,
}

/// Quantization parameters (QCD or one QCC override).
#[repr(C)]
pub struct MercuryQuant {
    pub guard_bits: u8,
    /// 0 = reversible, 1 = derived, 2 = expounded.
    pub style: u8,
    /// Reversible: one ranging exponent per band (else NULL/0).
    pub ranges: *const u8,
    pub num_ranges: u32,
    /// Irreversible: one step size per band (else NULL/0).
    pub steps: *const f32,
    pub num_steps: u32,
}

/// A per-component quantization override (QCC).
#[repr(C)]
pub struct MercuryQccOverride {
    pub comp: u32,
    pub quant: MercuryQuant,
}

/// The full main header. Field order matches `MercuryMainHeader` in
/// include/mercury_capi.h exactly.
#[repr(C)]
pub struct MercuryMainHeader {
    // SIZ
    pub x_siz: u32,
    pub y_siz: u32,
    pub x_o_siz: u32,
    pub y_o_siz: u32,
    pub xt_siz: u32,
    pub yt_siz: u32,
    pub xt_o_siz: u32,
    pub yt_o_siz: u32,
    pub comps: *const MercurySizComp,
    pub num_comps: u32,
    // COD
    pub order: u8,
    pub num_layers: u16,
    pub use_ycc: bool,
    pub num_levels: u8,
    pub block_width: u32,
    pub block_height: u32,
    pub modes: u32,
    pub reversible: bool,
    pub use_sop: bool,
    pub use_eph: bool,
    pub precincts: *const MercuryPrecinct,
    pub num_precincts: u32,
    // Quantization
    pub qcd: MercuryQuant,
    pub qcc: *const MercuryQccOverride,
    pub num_qcc: u32,
    // Codestream layout in the file the read_at/fd addresses.
    /// Absolute file offset of the SOC marker (informational).
    pub codestream_off: u64,
    /// Absolute file offset of the first SOT marker.
    pub first_sot_off: u64,
}

/// Convert a `MercuryQuant` into mercury's owned `QcdParams`.
unsafe fn quant_from_host(q: &MercuryQuant) -> Result<QcdParams, &'static str> {
    let style = match q.style {
        0 => QuantStyle::Reversible,
        1 => QuantStyle::Derived,
        2 => QuantStyle::Expounded,
        _ => return Err("invalid quantization style"),
    };
    let ranges = if q.ranges.is_null() || q.num_ranges == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(q.ranges, q.num_ranges as usize) }.to_vec()
    };
    let steps = if q.steps.is_null() || q.num_steps == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(q.steps, q.num_steps as usize) }.to_vec()
    };
    Ok(QcdParams {
        guard_bits: q.guard_bits,
        style,
        ranges,
        steps,
    })
}

/// Copy the host-supplied main header into mercury's owned parameter structs.
/// # Safety
/// `hdr` must be a valid `MercuryMainHeader` whose array pointers each address
/// at least their stated count of elements.
unsafe fn draft_header_in(hdr: *const MercuryMainHeader) -> Result<MainHeaderIn, String> {
    if hdr.is_null() {
        return Err("null main header".into());
    }
    let h = unsafe { &*hdr };
    if h.comps.is_null() || h.num_comps == 0 {
        return Err("main header has no components".into());
    }

    let comps_c = unsafe { std::slice::from_raw_parts(h.comps, h.num_comps as usize) };
    let components = comps_c
        .iter()
        .map(|c| SizComponent {
            precision: c.precision,
            is_signed: c.is_signed,
            xr_siz: c.xr_siz,
            yr_siz: c.yr_siz,
        })
        .collect();
    let siz = SizParams {
        x_siz: h.x_siz,
        y_siz: h.y_siz,
        x_o_siz: h.x_o_siz,
        y_o_siz: h.y_o_siz,
        xt_siz: h.xt_siz,
        yt_siz: h.yt_siz,
        xt_o_siz: h.xt_o_siz,
        yt_o_siz: h.yt_o_siz,
        components,
    };

    let order = ProgressionOrder::from_host(h.order).map_err(|_| "invalid progression order")?;
    let precincts = if h.precincts.is_null() || h.num_precincts == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(h.precincts, h.num_precincts as usize) }
            .iter()
            .map(|p| PrecinctSize {
                width: p.width,
                height: p.height,
            })
            .collect()
    };
    let cod = CodParams {
        order,
        num_layers: h.num_layers,
        use_ycc: h.use_ycc,
        num_levels: h.num_levels,
        block_width: h.block_width,
        block_height: h.block_height,
        modes: CodingModes(h.modes),
        reversible: h.reversible,
        use_sop: h.use_sop,
        use_eph: h.use_eph,
        precincts,
    };

    let qcd = unsafe { quant_from_host(&h.qcd) }?;
    let qcc = if h.qcc.is_null() || h.num_qcc == 0 {
        Vec::new()
    } else {
        let qcc_c = unsafe { std::slice::from_raw_parts(h.qcc, h.num_qcc as usize) };
        let mut v = Vec::with_capacity(qcc_c.len());
        for o in qcc_c {
            v.push((o.comp as usize, unsafe { quant_from_host(&o.quant) }?));
        }
        v
    };

    Ok(MainHeaderIn {
        siz,
        cod,
        qcd,
        qcc,
        first_sot_off: h.first_sot_off,
    })
}

/// Opaque plan handle: the parsed T2 plan plus the byte source to stream from.
pub struct MercuryPlan {
    plan: DecodePlan,
    src: Arc<dyn ReadAt>,
}

struct CallbackReader {
    read_at: MercuryReadAtFn,
    ctx: *mut c_void,
    len: u64,
}

// The read_at contract requires concurrent callability; ctx travels with it.
unsafe impl Send for CallbackReader {}
unsafe impl Sync for CallbackReader {}

impl ReadAt for CallbackReader {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        if (self.read_at)(self.ctx, buf.as_mut_ptr(), off, buf.len() as u64) != 0 {
            Ok(())
        } else {
            Err(io::Error::new(io::ErrorKind::Other, "read_at callback failed"))
        }
    }

    fn extent(&self) -> io::Result<u64> {
        Ok(self.len)
    }
}

fn stamp_err(err_buf: *mut u8, err_cap: usize, msg: &str) {
    if err_buf.is_null() || err_cap == 0 {
        return;
    }
    let n = msg.len().min(err_cap - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(msg.as_ptr(), err_buf, n);
        *err_buf.add(n) = 0;
    }
}

fn assemble_plan(
    hdr: *const MercuryMainHeader,
    src: Arc<dyn ReadAt>,
    err_buf: *mut u8,
    err_cap: usize,
) -> *mut MercuryPlan {
    let header = match unsafe { draft_header_in(hdr) } {
        Ok(h) => h,
        Err(e) => {
            stamp_err(err_buf, err_cap, &e);
            return std::ptr::null_mut();
        }
    };
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| draft(&*src, &header)));
    match r {
        Ok(Ok(plan)) => Box::into_raw(Box::new(MercuryPlan { plan, src })),
        Ok(Err(e)) => {
            stamp_err(err_buf, err_cap, &format!("{e:?}"));
            std::ptr::null_mut()
        }
        Err(_) => {
            stamp_err(err_buf, err_cap, "panic in draft");
            std::ptr::null_mut()
        }
    }
}

/// Build a decode plan for the main header `hdr` (parsed by the host),
/// reading packet data through `read_at` (`len` = total stream bytes).
/// Returns null if the stream is rejected (reason in `err_buf`,
/// NUL-terminated, if provided) — the host should fall back.
#[unsafe(no_mangle)]
pub extern "C" fn mercury_warp_loom(
    hdr: *const MercuryMainHeader,
    read_at: Option<MercuryReadAtFn>,
    ctx: *mut c_void,
    len: u64,
    err_buf: *mut u8,
    err_cap: usize,
) -> *mut MercuryPlan {
    let Some(read_at) = read_at else {
        stamp_err(err_buf, err_cap, "null read_at");
        return std::ptr::null_mut();
    };
    assemble_plan(hdr, Arc::new(CallbackReader { read_at, ctx, len }), err_buf, err_cap)
}

/// [`mercury_warp_loom`] over a POSIX file descriptor (duplicated internally;
/// the caller keeps ownership of `fd`). Reads use pread — the fd's file
/// offset is never touched. Unix only; Windows hosts use the callback entry.
#[cfg(unix)]
#[unsafe(no_mangle)]
pub extern "C" fn mercury_warp_loom_fd(
    hdr: *const MercuryMainHeader,
    fd: i32,
    err_buf: *mut u8,
    err_cap: usize,
) -> *mut MercuryPlan {
    let owned = unsafe { std::os::fd::BorrowedFd::borrow_raw(fd) }.try_clone_to_owned();
    match owned {
        Ok(o) => assemble_plan(hdr, Arc::new(std::fs::File::from(o)), err_buf, err_cap),
        Err(e) => {
            stamp_err(err_buf, err_cap, &format!("dup fd: {e}"));
            std::ptr::null_mut()
        }
    }
}

#[repr(C)]
pub struct MercuryImageInfo {
    pub width: u32,
    pub height: u32,
    pub num_comps: u32,
    /// 1 = 5/3 reversible, 0 = 9/7 irreversible.
    pub reversible: bool,
}

#[unsafe(no_mangle)]
pub extern "C" fn mercury_loom_info(plan: *const MercuryPlan, out: *mut MercuryImageInfo) -> i32 {
    if plan.is_null() || out.is_null() {
        return MERCURY_EBADARG;
    }
    let p = unsafe { &*plan };
    unsafe {
        *out = MercuryImageInfo {
            width: p.plan.width,
            height: p.plan.height,
            num_comps: p.plan.siz.comp_count() as u32,
            reversible: p.plan.cod.reversible,
        };
    }
    MERCURY_OK
}

#[unsafe(no_mangle)]
pub extern "C" fn mercury_loom_comp_info(
    plan: *const MercuryPlan,
    comp: u32,
    prec: *mut u32,
    is_signed: *mut i32,
) -> i32 {
    if plan.is_null() {
        return MERCURY_EBADARG;
    }
    let p = unsafe { &*plan };
    let Some(c) = p.plan.siz.components.get(comp as usize) else {
        return MERCURY_EBADARG;
    };
    if !prec.is_null() {
        unsafe { *prec = c.precision as u32 };
    }
    if !is_signed.is_null() {
        unsafe { *is_signed = c.is_signed as i32 };
    }
    MERCURY_OK
}

/// Free a plan that will not be decoded. (`mercury_weave` consumes and
/// frees its plan itself.)
#[unsafe(no_mangle)]
pub extern "C" fn mercury_unwarp_loom(plan: *mut MercuryPlan) {
    if !plan.is_null() {
        drop(unsafe { Box::from_raw(plan) });
    }
}

/// The substitute T1 for [`extern_t1_weave`] — process-global; set by
/// `mercury_weave` before the graph runs.
static EXTERN_T1: AtomicUsize = AtomicUsize::new(0);

unsafe fn extern_t1_weave(blk: &MercuryStripeBlockInfo) -> Option<Vec<i32>> {
    let f: MercuryT1Fn = unsafe { std::mem::transmute(EXTERN_T1.load(Ordering::Relaxed)) };
    let stripes = (blk.num_rows + 3) >> 2;
    let mut out = vec![0i32; ((stripes << 2) * blk.num_cols) as usize];
    let ok = f(
        blk.coded_data,
        blk.coded_length,
        blk.num_passes,
        blk.missing_msbs,
        blk.k_max_prime,
        blk.orientation,
        blk.modes,
        blk.num_cols,
        blk.num_rows,
        blk.segment_lengths,
        blk.num_segments,
        out.as_mut_ptr(),
    );
    (ok != 0).then_some(out)
}

/// Convert one component's row to absolute i32 samples (see module doc).
/// Ports mercury_decompress's cvt spec, minus the width narrowing.
fn dye_comp_row(rows: &Rows<'_>, ci: usize, prec: u32, signed: bool, out: &mut [i32]) {
    let dc = 1i32 << (prec - 1);
    let mx = (1i64 << prec) as i32 - 1;
    match rows {
        Rows::I16(r) => {
            if signed {
                for (o, &v) in out.iter_mut().zip(r[ci]) {
                    *o = v as i32;
                }
            } else {
                for (o, &v) in out.iter_mut().zip(r[ci]) {
                    *o = (v as i32 + dc).clamp(0, mx);
                }
            }
        }
        Rows::I32(r) => {
            if signed {
                out.copy_from_slice(r[ci]);
            } else {
                for (o, &v) in out.iter_mut().zip(r[ci]) {
                    *o = (v + dc).clamp(0, mx);
                }
            }
        }
        Rows::F32(r) => {
            // normalized floats: sample / 2^prec, signed around 0.
            if signed {
                let sc = (1i64 << (prec - 1)) as f32;
                for (o, &s) in out.iter_mut().zip(r[ci]) {
                    *o = ((s * sc) as i32).clamp(-dc, mx - dc);
                }
            } else {
                let sc = (1i64 << prec) as f32;
                let hf = sc * 0.5;
                for (o, &s) in out.iter_mut().zip(r[ci]) {
                    *o = ((s * sc + hf) as i32).clamp(0, mx);
                }
            }
        }
    }
}

struct SendPtr(*mut c_void);
unsafe impl Send for SendPtr {}

/// Decode `plan` to completion, streaming rows to `row_fn`. Consumes and
/// frees `plan` (even on failure). `t1` is required (mercury ships no
/// built-in coder; null → EBADARG); `threads` 0 = one worker per core.
#[unsafe(no_mangle)]
pub extern "C" fn mercury_weave(
    plan: *mut MercuryPlan,
    t1: Option<MercuryT1Fn>,
    row_fn: Option<MercuryRowFn>,
    row_ctx: *mut c_void,
    threads: u32,
) -> i32 {
    if plan.is_null() {
        return MERCURY_EBADARG;
    }
    let handle = unsafe { Box::from_raw(plan) };
    let Some(row_fn) = row_fn else {
        return MERCURY_EBADARG;
    };

    let coder: BlockCoder = match t1 {
        Some(f) => {
            EXTERN_T1.store(f as usize, Ordering::Relaxed);
            extern_t1_weave
        }
        // mercury ships no tier-1 decoder; the host must supply one.
        None => return MERCURY_EBADARG,
    };
    let workers = if threads == 0 {
        std::thread::available_parallelism().map_or(1, |n| n.get())
    } else {
        threads as usize
    };

    let MercuryPlan { plan, src } = *handle;
    let height = plan.height as u64;
    let width = plan.width as usize;
    let comps: Vec<(u32, bool)> = plan
        .siz
        .components
        .iter()
        .map(|c| (c.precision as u32, c.is_signed))
        .collect();

    let mut scratch: Vec<Vec<i32>> = comps.iter().map(|_| vec![0i32; width]).collect();
    // Row pointers stored as usize so the closure stays Send; rebuilt (and
    // only read) inside each serial sink call.
    let mut ptrs: Vec<usize> = vec![0; comps.len()];
    let ctx = SendPtr(row_ctx);
    let mut row_no: u32 = 0;
    let nc = comps.len() as u32;
    let sink = Box::new(move |_row: u32, rows: Rows<'_>| {
        let ctx = &ctx;
        for (ci, &(prec, signed)) in comps.iter().enumerate() {
            dye_comp_row(&rows, ci, prec, signed, &mut scratch[ci]);
            ptrs[ci] = scratch[ci].as_ptr() as usize;
        }
        row_fn(ctx.0, row_no, ptrs.as_ptr().cast::<*const i32>(), nc, width as u64);
        row_no += 1;
    });

    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        weave_with_coder(src, plan, workers, sink, coder)
    }));
    match r {
        Ok(Ok(emitted)) if emitted == height => MERCURY_OK,
        Ok(Ok(_)) | Ok(Err(_)) => MERCURY_EDECODE,
        Err(_) => MERCURY_EPANIC,
    }
}
