use crate::capi::MercuryT1EncodeFn;
use crate::codec::params::{
    CodParams, CodingModes, CompCodingStyle, PrecinctSize, ProgressionOrder, ProgressionVolume,
    SizComponent, SizParams,
};
use crate::codec::tile_geom::chart_tile_geom;
use crate::ffi_dwt::mercury_t1_band_step;

use super::component::{ComponentEncoder, ComponentParams, XyzTables, feed_tile_rows};
use super::rate_control::{RateTargets, allocate_rate};
use super::tile_packets::{TileBand, TileBands, TilePacketWriter};

const COMPONENTS: usize = 3;
const PRECISION: u8 = 12;
const BLOCK_SIDE: u32 = 32;
const PRECINCT_SIDE: u32 = 256;
// classic's cinema setup signals 128 at resolution 0
const LOWEST_PRECINCT_SIDE: u32 = 128;
const RSIZ_CINEMA_2K: u16 = 3;
const RSIZ_CINEMA_4K: u16 = 4;
const LEVELS_2K: u8 = 5;
const LEVELS_4K: u8 = 6;
const GUARD_BITS_2K: u8 = 1;
const GUARD_BITS_4K: u8 = 2;
const STEP_SCALE: f64 = 1.0;
const SCALAR_EXPOUNDED: u8 = 2;
const ORDER_CPRL: u8 = 4;
const SOT_SOD_BYTES: u64 = 14;
const LAST_LAYER_EOC_BYTES: f64 = 2.0;
const STLM_TILE16_PSOT32: u8 = 0x60;
const SCOD_PRECINCTS_DECLARED: u8 = 0x01;
const MCT_ON: u8 = 1;
const CBLK_STYLE_NONE: u8 = 0;
const TRANSFORM_9_7: u8 = 0;
const RCOM_LATIN: u16 = 1;

const SOC: u16 = 0xFF4F;
const SIZ: u16 = 0xFF51;
const COD: u16 = 0xFF52;
const QCD: u16 = 0xFF5C;
const TLM: u16 = 0xFF55;
const POC: u16 = 0xFF5F;
const COM: u16 = 0xFF64;
const SOT: u16 = 0xFF90;
const SOD: u16 = 0xFF93;
const EOC: u16 = 0xFFD9;

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum CinemaProfile {
    TwoK,
    FourK,
}

pub(crate) struct DciParams<'a> {
    pub profile: CinemaProfile,
    pub width: u32,
    pub height: u32,
    pub max_cs_size: u64,
    pub max_comp_size: u64,
    pub slope_hint: u16,
    pub tolerance: f64,
    // RGB at PRECISION + precision_shift bits, converted like classic's applyXYZTransform
    pub apply_xyz: bool,
    pub write_tlm: bool,
    pub comment: &'a [u8],
}

pub(crate) struct EncodedFrame {
    pub codestream: Vec<u8>,
    pub slope_threshold: u16,
}

struct BandQuantization {
    exponent: u8,
    mantissa: u16,
    stepsize: f32,
    max_bit_planes: u8,
}

// must match classic's Quantizer::generate and TileProcessor band setup
fn quantize_bands(levels: u8, guard_bits: u8) -> Vec<BandQuantization> {
    let num_resolutions = levels + 1;
    (0..3 * levels + 1)
        .map(|bandno| {
            let (mut exponent, mut mantissa) = (0u8, 0u16);
            unsafe {
                mercury_t1_band_step(
                    num_resolutions,
                    PRECISION,
                    bandno,
                    STEP_SCALE,
                    &mut exponent,
                    &mut mantissa,
                );
            }
            let orientation = if bandno == 0 { 0 } else { (bandno - 1) % 3 + 1 };
            let log2_gain = match orientation {
                0 => 0,
                3 => 2,
                _ => 1,
            };
            let numbps = (PRECISION + log2_gain) as i32;
            let stepsize =
                ((1.0 + mantissa as f64 / 2048.0) * 2f64.powi(numbps - exponent as i32)) as f32;
            BandQuantization {
                exponent,
                mantissa,
                stepsize,
                max_bit_planes: (exponent as i32 + guard_bits as i32 - 1).max(0) as u8,
            }
        })
        .collect()
}

fn coding_params(params: &DciParams, levels: u8) -> (SizParams, CodParams) {
    let siz = SizParams {
        x_siz: params.width,
        y_siz: params.height,
        x_o_siz: 0,
        y_o_siz: 0,
        xt_siz: params.width,
        yt_siz: params.height,
        xt_o_siz: 0,
        yt_o_siz: 0,
        components: vec![
            SizComponent {
                precision: PRECISION,
                is_signed: false,
                xr_siz: 1,
                yr_siz: 1,
            };
            COMPONENTS
        ],
    };
    let mut precincts = vec![
        PrecinctSize {
            width: PRECINCT_SIDE,
            height: PRECINCT_SIDE,
        };
        levels as usize + 1
    ];
    precincts[0] = PrecinctSize {
        width: LOWEST_PRECINCT_SIDE,
        height: LOWEST_PRECINCT_SIDE,
    };
    let style = CompCodingStyle {
        num_levels: levels,
        block_width: BLOCK_SIDE,
        block_height: BLOCK_SIDE,
        modes: CodingModes(0),
        reversible: false,
        precincts,
    };
    let num_resolutions = levels + 1;
    let pocs = match params.profile {
        CinemaProfile::TwoK => Vec::new(),
        // classic's init4kPoc splits off the top resolution
        CinemaProfile::FourK => vec![
            ProgressionVolume {
                res_s: 0,
                comp_s: 0,
                lay_e: 1,
                res_e: num_resolutions - 1,
                comp_e: COMPONENTS as u16,
                order: ProgressionOrder::Cprl,
            },
            ProgressionVolume {
                res_s: num_resolutions - 1,
                comp_s: 0,
                lay_e: 1,
                res_e: num_resolutions,
                comp_e: COMPONENTS as u16,
                order: ProgressionOrder::Cprl,
            },
        ],
    };
    let cod = CodParams {
        order: ProgressionOrder::Cprl,
        pocs,
        num_layers: 1,
        use_ycc: true,
        use_sop: false,
        use_eph: false,
        comps: vec![style; COMPONENTS],
    };
    (siz, cod)
}

struct MarkerWriter {
    out: Vec<u8>,
}

impl MarkerWriter {
    fn u8(&mut self, value: u8) {
        self.out.push(value);
    }

    fn u16(&mut self, value: u16) {
        self.out.extend_from_slice(&value.to_be_bytes());
    }

    fn u32(&mut self, value: u32) {
        self.out.extend_from_slice(&value.to_be_bytes());
    }

    fn patch_u32(&mut self, at: usize, value: u32) {
        self.out[at..at + 4].copy_from_slice(&value.to_be_bytes());
    }
}

// classic's CodeStreamCompress::updateRates for one tile and one layer
fn max_layer_length(params: &DciParams, tile_parts: usize, header_bytes: usize) -> u32 {
    let (width, height) = (params.width as f64, params.height as f64);
    let samples_bits = COMPONENTS as f64 * width * height * PRECISION as f64;
    let image_bytes = samples_bits / 8.0;
    let mut layer_rate = 0.0;
    let min_rate = image_bytes / params.max_cs_size as f64;
    if layer_rate < min_rate {
        layer_rate = min_rate;
    }
    let cap_rate = samples_bits / (params.max_cs_size as f64 * 8.0);
    if layer_rate < cap_rate {
        layer_rate = cap_rate;
    }
    let size_pixel = (COMPONENTS as u32 * PRECISION as u32) as f64;
    let bits_empty = 8.0;
    let tile_pixels = (params.width as u64 * params.height as u64) as f64;
    let raw_budget = (size_pixel * tile_pixels) / (layer_rate * bits_empty);
    let mut rate = raw_budget - (tile_parts as u64 * SOT_SOD_BYTES) as f64;
    let header_share = (tile_pixels * header_bytes as f64) / (width * height);
    rate -= header_share + LAST_LAYER_EOC_BYTES;
    rate.ceil() as u32
}

pub(crate) fn encode_dci_frame(
    planes: &[&[i32]],
    stride: usize,
    precision_shift: u32,
    params: &DciParams,
    t1: MercuryT1EncodeFn,
) -> EncodedFrame {
    let (levels, guard_bits, rsiz) = match params.profile {
        CinemaProfile::TwoK => (LEVELS_2K, GUARD_BITS_2K, RSIZ_CINEMA_2K),
        CinemaProfile::FourK => (LEVELS_4K, GUARD_BITS_4K, RSIZ_CINEMA_4K),
    };
    let (siz, cod) = coding_params(params, levels);
    let geom = chart_tile_geom(&siz, &cod, 0);
    let quantization = quantize_bands(levels, guard_bits);
    let band_stepsizes: Vec<f32> = quantization.iter().map(|band| band.stepsize).collect();

    let bounds = [0, 0, params.width, params.height];
    let mut encoders: Vec<ComponentEncoder> = (0..COMPONENTS)
        .map(|c| {
            ComponentEncoder::warp(&ComponentParams {
                compno: c as i32,
                bounds,
                levels: levels as u32,
                block_width: BLOCK_SIDE,
                block_height: BLOCK_SIDE,
                band_stepsizes: &band_stepsizes,
                mct: true,
                t1,
            })
        })
        .collect();
    let xyz = params
        .apply_xyz
        .then(|| XyzTables::warp(PRECISION as u32 + precision_shift, PRECISION as u32));
    feed_tile_rows(
        &mut encoders,
        planes,
        stride,
        params.width as usize,
        params.height as usize,
        PRECISION as u32,
        precision_shift,
        xyz.as_ref(),
    );

    let mut bands = Vec::new();
    let mut first_band = Vec::with_capacity(COMPONENTS);
    for (c, encoder) in encoders.iter().enumerate() {
        let mut comp_first = Vec::with_capacity(levels as usize + 1);
        for (r, res) in geom.components[c].resolutions.iter().enumerate() {
            comp_first.push(bands.len());
            for (b, subband) in res.subbands.iter().enumerate() {
                let bandno = if r == 0 { 0 } else { 3 * (r - 1) + b + 1 };
                bands.push(TileBand {
                    compno: c,
                    coded: encoder.coded_band(r, b),
                    blocks_across: subband.blocks_wide,
                    max_bit_planes: quantization[bandno].max_bit_planes,
                });
            }
        }
        first_band.push(comp_first);
    }
    let bands = TileBands { bands, first_band };
    let mut packets = TilePacketWriter::warp(&cod, &siz, &geom);
    let tile_parts = packets.component_runs();

    let mut writer = MarkerWriter { out: Vec::new() };
    writer.u16(SOC);
    writer.u16(SIZ);
    writer.u16(38 + 3 * COMPONENTS as u16);
    writer.u16(rsiz);
    for value in [
        params.width,
        params.height,
        0,
        0,
        params.width,
        params.height,
        0,
        0,
    ] {
        writer.u32(value);
    }
    writer.u16(COMPONENTS as u16);
    for _ in 0..COMPONENTS {
        writer.u8(PRECISION - 1);
        writer.u8(1);
        writer.u8(1);
    }
    let num_resolutions = levels as usize + 1;
    writer.u16(COD);
    writer.u16(12 + num_resolutions as u16);
    writer.u8(SCOD_PRECINCTS_DECLARED);
    writer.u8(ORDER_CPRL);
    writer.u16(cod.num_layers);
    writer.u8(MCT_ON);
    writer.u8(levels);
    writer.u8((BLOCK_SIDE.trailing_zeros() - 2) as u8);
    writer.u8((BLOCK_SIDE.trailing_zeros() - 2) as u8);
    writer.u8(CBLK_STYLE_NONE);
    writer.u8(TRANSFORM_9_7);
    for precinct in &cod.comps[0].precincts {
        let exponent = precinct.width.trailing_zeros() as u8;
        writer.u8((precinct.height.trailing_zeros() as u8) << 4 | exponent);
    }
    writer.u16(QCD);
    writer.u16(3 + 2 * quantization.len() as u16);
    writer.u8(guard_bits << 5 | SCALAR_EXPOUNDED);
    for band in &quantization {
        writer.u16(((band.exponent as u16) << 11) + band.mantissa);
    }
    let mut tlm_entries_at = None;
    if params.write_tlm {
        writer.u16(TLM);
        writer.u16(4 + 6 * tile_parts as u16);
        writer.u8(0);
        writer.u8(STLM_TILE16_PSOT32);
        tlm_entries_at = Some(writer.out.len());
        for _ in 0..tile_parts {
            writer.u16(0);
            writer.u32(0);
        }
    }
    if !cod.pocs.is_empty() {
        writer.u16(POC);
        writer.u16(2 + 7 * cod.pocs.len() as u16);
        for volume in &cod.pocs {
            writer.u8(volume.res_s);
            writer.u8(volume.comp_s as u8);
            writer.u16(volume.lay_e);
            writer.u8(volume.res_e);
            writer.u8(volume.comp_e as u8);
            writer.u8(ORDER_CPRL);
        }
    }
    writer.u16(COM);
    writer.u16(4 + params.comment.len() as u16);
    writer.u16(RCOM_LATIN);
    writer.out.extend_from_slice(params.comment);

    let targets = RateTargets {
        max_layer_length: max_layer_length(params, tile_parts, writer.out.len()),
        component_cap: params.max_comp_size,
        tolerance: params.tolerance,
        slope_hint: params.slope_hint,
    };
    let allocation =
        allocate_rate(&mut packets, &bands, &targets).expect("rate control finds a fit");

    let mut packet_bytes = Vec::new();
    let mut part_lengths: Vec<(u16, usize)> = Vec::new();
    packets.write_packets(
        &bands,
        &allocation.included,
        &mut packet_bytes,
        true,
        |slot, length| match part_lengths.last_mut() {
            Some((comp, total)) if *comp == slot.comp => *total += length,
            _ => part_lengths.push((slot.comp, length)),
        },
    );
    let mut packet_start = 0;
    for (part, &(_, length)) in part_lengths.iter().enumerate() {
        let psot = (SOT_SOD_BYTES as usize + length) as u32;
        writer.u16(SOT);
        writer.u16(10);
        writer.u16(0);
        writer.u32(psot);
        writer.u8(part as u8);
        writer.u8(tile_parts as u8);
        writer.u16(SOD);
        writer
            .out
            .extend_from_slice(&packet_bytes[packet_start..packet_start + length]);
        packet_start += length;
        if let Some(at) = tlm_entries_at {
            writer.patch_u32(at + 6 * part + 2, psot);
        }
    }
    writer.u16(EOC);
    EncodedFrame {
        codestream: writer.out,
        slope_threshold: allocation.slope_threshold,
    }
}
