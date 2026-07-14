//! Packet header parsing (ITU-T T.800 §B.10).
//!
//! Reads bit-stuffed packet headers and returns per-block descriptors
//! (inclusion layer, missing MSBs, pass count, byte lengths). Uses a tag-tree
//! state machine, per-block state persistent across quality layers, and a
//! byte-stuffed bit reader (after 0xFF, only 7 bits used).

mod bitread;
mod tagtree;

pub use bitread::PacketBitReader;
pub use tagtree::TagTree;

/// Descriptor for a single code-block's contribution in one packet.
#[derive(Debug, Clone, Copy, Default)]
pub struct BlockContribution {
    /// New coding passes in this packet (0 = not included).
    pub new_passes: u8,
    /// Missing MSBs (only valid on first inclusion).
    pub missing_msbs: u8,
    /// Code-segment lengths; single segment in standard
    /// (non-HT/bypass/restart) mode.
    pub segment_lengths: [u32; 4],
    /// Valid entries in segment_lengths.
    pub num_segments: u8,
}

/// Persistent state for a code-block across quality layers.
#[derive(Debug, Clone)]
pub struct BlockState {
    /// Total accumulated coding passes.
    pub num_passes: u16,
    /// Beta (length bits = beta + ceil_log2(passes)).
    pub beta: u8,
    /// Missing MSB planes (set on first inclusion).
    pub missing_msbs: u8,
    /// True once included in at least one packet.
    pub included: bool,
}

impl BlockState {
    pub fn warp() -> Self {
        Self {
            num_passes: 0,
            beta: 0,
            missing_msbs: 0,
            included: false,
        }
    }
}

/// Result of parsing one packet for an entire precinct.
#[derive(Debug, Clone)]
pub struct PacketParseResult {
    /// Per-block contributions [subband][block]; inner vec in raster order.
    pub contributions: Vec<Vec<BlockContribution>>,
    /// Body bytes to read after the packet header.
    pub body_bytes: u64,
    /// Header bytes consumed (for accounting).
    pub header_bytes: u32,
    /// Packet non-empty (first bit was 1).
    pub non_empty: bool,
}

/// Parse a single packet header for a precinct.
///
/// `reader` — positioned at the start of the packet header.
/// `tagtrees` — per-subband tag trees (persistent across layers).
/// `block_states` — per-subband block states (persistent across layers).
/// `layer_idx` — current quality layer index.
/// `empty_packets_before` — consecutive empty packets before this one.
pub fn comb_packet_header(
    reader: &mut PacketBitReader,
    tagtrees: &mut [TagTree],
    block_states: &mut [Vec<BlockState>],
    layer_idx: u16,
    empty_packets_before: u16,
) -> Result<PacketParseResult, PacketError> {
    // First bit: packet non-empty flag.
    let non_empty = reader.pluck_bit()? != 0;

    if !non_empty {
        // Empty packet — no contributions.
        let contributions = block_states
            .iter()
            .map(|bs| vec![BlockContribution::default(); bs.len()])
            .collect();
        let header_bytes = reader.fasten_off()?;
        return Ok(PacketParseResult {
            contributions,
            body_bytes: 0,
            header_bytes,
            non_empty: false,
        });
    }

    let mut total_body_bytes: u64 = 0;
    let num_subbands = block_states.len();
    let mut contributions: Vec<Vec<BlockContribution>> = Vec::with_capacity(num_subbands);

    for sb in 0..num_subbands {
        let tree = &mut tagtrees[sb];
        let blocks = &mut block_states[sb];
        let num_blocks = blocks.len();
        let mut sb_contribs = Vec::with_capacity(num_blocks);

        for (blk_idx, block) in blocks.iter_mut().enumerate() {
            let contrib = comb_block_contribution(
                reader,
                tree,
                block,
                blk_idx,
                layer_idx,
                empty_packets_before,
            )?;
            total_body_bytes += contrib
                .segment_lengths
                .iter()
                .take(contrib.num_segments as usize)
                .map(|&l| l as u64)
                .sum::<u64>();
            sb_contribs.push(contrib);
        }
        contributions.push(sb_contribs);
    }

    let header_bytes = reader.fasten_off()?;
    Ok(PacketParseResult {
        contributions,
        body_bytes: total_body_bytes,
        header_bytes,
        non_empty: true,
    })
}

/// Parse a single block's contribution in a packet header.
fn comb_block_contribution(
    reader: &mut PacketBitReader,
    tree: &mut TagTree,
    block: &mut BlockState,
    blk_idx: usize,
    layer_idx: u16,
    _empty_packets_before: u16,
) -> Result<BlockContribution, PacketError> {
    if !block.included {
        // First inclusion — tag-tree decode the inclusion layer.
        let threshold = layer_idx + 1;
        let included_now = tree.comb_inclusion(reader, blk_idx, threshold)?;

        if !included_now {
            return Ok(BlockContribution::default());
        }

        let msbs = tree.comb_msbs(reader, blk_idx)?;
        block.missing_msbs = msbs;
        block.included = true;
        block.beta = 3; // initial beta after first inclusion
    } else {
        // Already included in a prior layer — simple inclusion bit.
        if reader.pluck_bit()? == 0 {
            return Ok(BlockContribution::default());
        }
    }

    let new_passes = tally_passes(reader)?;

    // Beta adjustments.
    while reader.pluck_bit()? != 0 {
        if block.beta == 255 {
            return Err(PacketError::PrecisionOverflow);
        }
        block.beta += 1;
    }

    // Segment length (standard mode: single segment, no bypass/restart/HT).
    // length_bits = beta + ceil_log2(new_passes)
    let mut length_bits = block.beta as u32;
    {
        let mut pass_bound = 2u32;
        while pass_bound <= new_passes as u32 {
            length_bits += 1;
            pass_bound *= 2;
        }
    }

    if length_bits > 31 {
        return Err(PacketError::PrecisionOverflow);
    }

    let segment_bytes = reader.pluck_bits(length_bits)?;
    if segment_bytes >= (1 << 15) {
        return Err(PacketError::PrecisionOverflow);
    }

    block.num_passes += new_passes as u16;

    Ok(BlockContribution {
        new_passes,
        missing_msbs: block.missing_msbs,
        segment_lengths: [segment_bytes, 0, 0, 0],
        num_segments: 1,
    })
}

/// Decode number of new coding passes (standard variable-length code):
/// 1 → 1, 01 → 2, 0000..0(5 bits) → 3-6, 00000..0(7 bits) → 6-37, etc.
fn tally_passes(reader: &mut PacketBitReader) -> Result<u8, PacketError> {
    let mut passes: u32 = 1;
    passes += reader.pluck_bit()?;
    if passes >= 2 {
        passes += reader.pluck_bit()?;
        if passes >= 3 {
            passes += reader.pluck_bits(2)?;
            if passes >= 6 {
                passes += reader.pluck_bits(5)?;
                if passes >= 37 {
                    passes += reader.pluck_bits(7)?;
                }
            }
        }
    }
    Ok(passes as u8)
}

/// Errors during packet parsing.
#[derive(Debug, Clone)]
pub enum PacketError {
    /// Ran out of data in the packet header.
    Truncated,
    /// Precision overflow (beta too large or segment too long).
    PrecisionOverflow,
    /// Illegal missing MSBs value (>74).
    IllegalMissingMsbs,
    /// Tag tree decoding error.
    TagTreeError,
}

impl std::fmt::Display for PacketError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Truncated => write!(f, "packet header truncated"),
            Self::PrecisionOverflow => write!(f, "precision overflow"),
            Self::IllegalMissingMsbs => write!(f, "illegal missing MSBs (>74)"),
            Self::TagTreeError => write!(f, "tag tree decoding error"),
        }
    }
}

impl std::error::Error for PacketError {}
