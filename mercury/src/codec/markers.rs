//! COD/COC/QCD/QCC marker parsing (T.800 A.6.1, A.6.2, A.6.4, A.6.5).
//!
//! The main header's copies are parsed by the host and handed in across the C
//! API; these readers exist for the tile-part header, which mercury walks
//! itself.

use crate::codec::params::{
    CodParams, CodingModes, CompCodingStyle, MAX_LEVELS, PrecinctSize, ProgressionOrder, QcdParams,
    QuantStyle,
};

/// Scod/Scoc bit 0: the segment carries one precinct size per resolution.
const CSTY_PRECINCT: u8 = 0x01;
/// Scod bit 1: SOP markers may precede packets.
const CSTY_SOP: u8 = 0x02;
/// Scod bit 2: an EPH marker terminates every packet header.
const CSTY_EPH: u8 = 0x04;
/// Decomposition levels T.800 allows.
/// Sub-bands of a component at `MAX_LEVELS`, the length a derived
/// quantization segment expands to.
const MAX_BANDS: usize = 3 * MAX_LEVELS as usize + 1;
/// Code-block exponents are stored with 2 subtracted (Table A.18).
const BLOCK_EXPONENT_OFFSET: u8 = 2;

/// Big-endian cursor over one marker segment's payload (the bytes after Lmar).
struct SegReader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> SegReader<'a> {
    fn warp(data: &'a [u8]) -> Self {
        SegReader { data, pos: 0 }
    }

    fn byte(&mut self) -> Result<u8, String> {
        let b = *self.data.get(self.pos).ok_or("segment too short")?;
        self.pos += 1;
        Ok(b)
    }

    fn word(&mut self) -> Result<u16, String> {
        Ok(u16::from_be_bytes([self.byte()?, self.byte()?]))
    }

    fn left(&self) -> usize {
        self.data.len() - self.pos
    }

    /// Ccoc/Cqcc: one byte for fewer than 257 components, two above that.
    fn comp_index(&mut self, num_comps: usize) -> Result<usize, String> {
        let idx = if num_comps <= 256 {
            self.byte()? as usize
        } else {
            self.word()? as usize
        };
        if idx >= num_comps {
            return Err(format!("component {idx} of {num_comps}"));
        }
        Ok(idx)
    }
}

/// SPcod/SPcoc (Table A.15): the per-component fields COD and COC share.
fn comb_sp_cod(r: &mut SegReader, custom_precincts: bool) -> Result<CompCodingStyle, String> {
    let num_levels = r.byte()?;
    if num_levels > MAX_LEVELS {
        return Err(format!("{num_levels} decomposition levels"));
    }
    // an exponent past u32 becomes 0, which check() rejects
    let block_side = |exponent: u8| {
        1u32.checked_shl(exponent as u32 + BLOCK_EXPONENT_OFFSET as u32)
            .unwrap_or(0)
    };
    let block_width = block_side(r.byte()?);
    let block_height = block_side(r.byte()?);
    let modes = CodingModes(r.byte()? as u32);
    let transform = r.byte()?;
    // above 1 the byte indexes a Part 2 ATK marker, whose kernel mercury has no
    // wavelet for
    if transform > 1 {
        return Err(format!("transform {transform} references an ATK marker"));
    }
    let mut precincts = Vec::new();
    if custom_precincts {
        for _ in 0..=num_levels {
            let packed = r.byte()?;
            let (width_exponent, height_exponent) = (packed & 0xF, packed >> 4);
            precincts.push(PrecinctSize {
                width: 1 << width_exponent,
                height: 1 << height_exponent,
            });
        }
    }
    let style = CompCodingStyle {
        num_levels,
        block_width,
        block_height,
        modes,
        reversible: transform == 1,
        precincts,
    };
    style.check()?;
    Ok(style)
}

/// Apply a COD segment to `cod`: the tile-wide fields plus one coding style
/// that replaces every component's, a COC in the same header excepted.
pub fn comb_cod(payload: &[u8], cod: &mut CodParams) -> Result<(), String> {
    let mut r = SegReader::warp(payload);
    let scod = r.byte()?;
    if scod & !(CSTY_PRECINCT | CSTY_SOP | CSTY_EPH) != 0 {
        return Err(format!("Scod {scod:#x}"));
    }
    let order = ProgressionOrder::from_host(r.byte()?).map_err(|_| "progression order")?;
    let num_layers = r.word()?;
    if num_layers == 0 {
        return Err("zero layers".into());
    }
    let mct = r.byte()?;
    if mct > 1 {
        return Err(format!("MCT {mct}"));
    }
    let style = comb_sp_cod(&mut r, scod & CSTY_PRECINCT != 0)?;
    if r.left() != 0 {
        return Err(format!("{} bytes past the COD fields", r.left()));
    }
    cod.order = order;
    cod.num_layers = num_layers;
    cod.use_ycc = mct == 1;
    cod.use_sop = scod & CSTY_SOP != 0;
    cod.use_eph = scod & CSTY_EPH != 0;
    for comp in cod.comps.iter_mut() {
        *comp = style.clone();
    }
    Ok(())
}

/// Apply a COC segment to `cod`, returning the component it covers.
pub fn comb_coc(payload: &[u8], cod: &mut CodParams) -> Result<usize, String> {
    let mut r = SegReader::warp(payload);
    let comp = r.comp_index(cod.comps.len())?;
    let scoc = r.byte()?;
    let style = comb_sp_cod(&mut r, scoc & CSTY_PRECINCT != 0)?;
    if r.left() != 0 {
        return Err(format!("{} bytes past the COC fields", r.left()));
    }
    cod.comps[comp] = style;
    Ok(comp)
}

/// Step size from a quantization exponent and mantissa, the expression the
/// host hands mercury for the main header's.
fn step_size(exponent: u32, mantissa: u16) -> f32 {
    (1.0 + mantissa as f32 / 2048.0) / (1u32 << exponent) as f32
}

/// Sqcx plus its SPqcx values (Tables A.28 and A.30). A derived segment is
/// expanded into one step per band, so the caller only ever sees the
/// reversible-ranges or expounded-steps forms.
fn comb_quant(r: &mut SegReader) -> Result<QcdParams, String> {
    let sqcx = r.byte()?;
    let guard_bits = sqcx >> 5;
    match sqcx & 0x1F {
        0 => {
            let mut ranges = Vec::with_capacity(r.left());
            while r.left() != 0 {
                ranges.push(r.byte()? >> 3);
            }
            Ok(QcdParams {
                guard_bits,
                style: QuantStyle::Reversible,
                ranges,
                steps: Vec::new(),
            })
        }
        1 => {
            let packed = r.word()?;
            let (exponent, mantissa) = ((packed >> 11) as u32, packed & 0x7FF);
            if r.left() != 0 {
                return Err(format!("{} bytes past the derived step", r.left()));
            }
            // T.800 E.1.1: band b drops (b-1)/3 from the LL exponent, floored
            // at 0, and keeps the mantissa
            let steps = (0..MAX_BANDS)
                .map(|band| {
                    let drop = band.saturating_sub(1) as u32 / 3;
                    step_size(exponent.saturating_sub(drop), mantissa)
                })
                .collect();
            Ok(QcdParams {
                guard_bits,
                style: QuantStyle::Expounded,
                ranges: Vec::new(),
                steps,
            })
        }
        2 => {
            let mut steps = Vec::with_capacity(r.left() / 2);
            while r.left() >= 2 {
                let packed = r.word()?;
                steps.push(step_size((packed >> 11) as u32, packed & 0x7FF));
            }
            if r.left() != 0 {
                return Err("odd byte past the expounded steps".into());
            }
            Ok(QcdParams {
                guard_bits,
                style: QuantStyle::Expounded,
                ranges: Vec::new(),
                steps,
            })
        }
        style => Err(format!("quantization style {style}")),
    }
}

/// Parse a QCD segment.
pub fn comb_qcd(payload: &[u8]) -> Result<QcdParams, String> {
    comb_quant(&mut SegReader::warp(payload))
}

/// Parse a QCC segment, returning the component it covers.
pub fn comb_qcc(payload: &[u8], num_comps: usize) -> Result<(usize, QcdParams), String> {
    let mut r = SegReader::warp(payload);
    let comp = r.comp_index(num_comps)?;
    let quant = comb_quant(&mut r)?;
    Ok((comp, quant))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cod_params(num_comps: usize) -> CodParams {
        CodParams {
            order: ProgressionOrder::Lrcp,
            pocs: Vec::new(),
            num_layers: 1,
            use_ycc: false,
            use_sop: false,
            use_eph: false,
            comps: vec![
                CompCodingStyle {
                    num_levels: 5,
                    block_width: 64,
                    block_height: 64,
                    modes: CodingModes(0),
                    reversible: true,
                    precincts: Vec::new(),
                };
                num_comps
            ],
        }
    }

    #[test]
    fn cod_sets_every_component_and_the_tile_wide_fields() {
        // Scod = SOP|EPH, RLCP, 4 layers, MCT, 3 levels, 32x16 blocks,
        // SEGMARK, 5/3
        let payload = [0x06, 0x01, 0x00, 0x04, 0x01, 0x03, 0x03, 0x02, 0x20, 0x01];
        let mut cod = cod_params(3);
        comb_cod(&payload, &mut cod).expect("COD must parse");
        assert_eq!(cod.order, ProgressionOrder::Rlcp);
        assert_eq!(cod.num_layers, 4);
        assert!(cod.use_ycc && cod.use_sop && cod.use_eph);
        for style in &cod.comps {
            assert_eq!(style.num_levels, 3);
            assert_eq!((style.block_width, style.block_height), (32, 16));
            assert_eq!(style.modes.0, 0x20);
            assert!(style.reversible);
            assert!(style.precincts.is_empty());
        }
    }

    #[test]
    fn cod_reads_one_precinct_size_per_resolution() {
        // Scod = custom precincts, LRCP, 1 layer, no MCT, 2 levels, 64x64
        // blocks, 9/7, then PPx/PPy for resolutions 0..2
        let payload = [
            0x01, 0x00, 0x00, 0x01, 0x00, 0x02, 0x04, 0x04, 0x00, 0x00, 0x77, 0x88, 0x69,
        ];
        let mut cod = cod_params(1);
        comb_cod(&payload, &mut cod).expect("COD must parse");
        let style = &cod.comps[0];
        assert!(!style.reversible);
        let sizes: Vec<(u32, u32)> = style
            .precincts
            .iter()
            .map(|p| (p.width, p.height))
            .collect();
        assert_eq!(
            sizes,
            vec![(1 << 7, 1 << 7), (1 << 8, 1 << 8), (1 << 9, 1 << 6)]
        );
    }

    #[test]
    fn coc_replaces_only_its_own_component() {
        // component 1, Scoc = 0, 2 levels, 16x16 blocks, no modes, 9/7
        let payload = [0x01, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00];
        let mut cod = cod_params(3);
        let comp = comb_coc(&payload, &mut cod).expect("COC must parse");
        assert_eq!(comp, 1);
        assert_eq!(cod.comps[1].num_levels, 2);
        assert_eq!(cod.comps[1].block_width, 16);
        assert!(!cod.comps[1].reversible);
        assert_eq!(cod.comps[0].num_levels, 5);
        assert_eq!(cod.comps[2].num_levels, 5);
    }

    #[test]
    fn a_wide_component_index_takes_two_bytes() {
        let mut narrow = [0u8; 7];
        narrow[0] = 0x01;
        narrow[2] = 0x02;
        narrow[3] = 0x02;
        narrow[4] = 0x02;
        narrow[6] = 0x01;
        let mut cod = cod_params(256);
        assert_eq!(comb_coc(&narrow, &mut cod), Ok(1));

        let mut wide = [0u8; 8];
        wide[1] = 0x01;
        wide[3] = 0x02;
        wide[4] = 0x02;
        wide[5] = 0x02;
        wide[7] = 0x01;
        let mut cod = cod_params(257);
        assert_eq!(comb_coc(&wide, &mut cod), Ok(1));
        assert_eq!(cod.comps[1].num_levels, 2);
    }

    #[test]
    fn a_reversible_qcd_carries_one_exponent_per_band() {
        // 2 guard bits, style 0, exponents 8, 9, 9, 10
        let payload = [0x40, 8 << 3, 9 << 3, 9 << 3, 10 << 3];
        let quant = comb_qcd(&payload).expect("QCD must parse");
        assert_eq!(quant.guard_bits, 2);
        assert_eq!(quant.style, QuantStyle::Reversible);
        assert_eq!(quant.ranges, vec![8, 9, 9, 10]);
    }

    #[test]
    fn a_derived_qcd_expands_into_one_step_per_band() {
        // 3 guard bits, style 1, exponent 13, mantissa 1024
        let payload = [0x61, (13 << 3) | 0x04, 0x00];
        let quant = comb_qcd(&payload).expect("QCD must parse");
        assert_eq!(quant.guard_bits, 3);
        assert_eq!(quant.style, QuantStyle::Expounded);
        assert_eq!(quant.steps.len(), MAX_BANDS);
        assert_eq!(quant.steps[0], step_size(13, 1024));
        assert_eq!(quant.steps[1], step_size(13, 1024));
        assert_eq!(quant.steps[3], step_size(13, 1024));
        assert_eq!(quant.steps[4], step_size(12, 1024));
        assert_eq!(quant.steps[MAX_BANDS - 1], step_size(0, 1024));
    }

    #[test]
    fn an_expounded_qcc_carries_one_step_per_band() {
        // component 2, 1 guard bit, style 2, then two (exponent, mantissa)
        let payload = [0x02, 0x22, (10 << 3) | 0x01, 0x00, (9 << 3) | 0x00, 0x7F];
        let (comp, quant) = comb_qcc(&payload, 4).expect("QCC must parse");
        assert_eq!(comp, 2);
        assert_eq!(quant.guard_bits, 1);
        assert_eq!(quant.style, QuantStyle::Expounded);
        assert_eq!(quant.steps, vec![step_size(10, 256), step_size(9, 127)]);
    }

    #[test]
    fn a_truncated_segment_is_rejected() {
        assert!(comb_cod(&[0x00, 0x00, 0x00], &mut cod_params(1)).is_err());
        assert!(comb_coc(&[0x00], &mut cod_params(1)).is_err());
        assert!(comb_qcc(&[0x00], 1).is_err());
    }
}
