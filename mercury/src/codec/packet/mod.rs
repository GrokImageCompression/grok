//! Packet header parsing (ITU-T T.800 §B.10).
//!
//! Reads bit-stuffed packet headers and returns per-block descriptors
//! (inclusion layer, missing MSBs, pass count, byte lengths). Uses a tag-tree
//! state machine, per-block state persistent across quality layers, and a
//! byte-stuffed bit reader (after 0xFF, only 7 bits used).

use std::collections::HashMap;

use crate::codec::params::CodingModes;

mod bitread;
mod tagtree;

pub use bitread::{HeaderBytes, PacketBitReader};
pub use tagtree::TagTree;

/// Descriptor for a single code-block's contribution in one packet.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BlockContribution {
    /// New coding passes in this packet (0 = not included).
    pub new_passes: u8,
    /// Missing MSBs (only valid on first inclusion).
    pub missing_msbs: u8,
    segment_lengths: [u32; 4],
    extra_segment_lengths: Vec<u32>,
    num_segments: u8,
    pub first_segment_continues: bool,
}

impl BlockContribution {
    fn push_segment_length(&mut self, length: u32) {
        let index = self.num_segments as usize;
        if index < self.segment_lengths.len() {
            self.segment_lengths[index] = length;
        } else {
            self.extra_segment_lengths.push(length);
        }
        self.num_segments += 1;
    }

    pub fn segment_lengths(&self) -> impl Iterator<Item = &u32> {
        let inline = usize::from(self.num_segments).min(self.segment_lengths.len());
        self.segment_lengths[..inline]
            .iter()
            .chain(self.extra_segment_lengths.iter())
    }

    pub fn num_segments(&self) -> u8 {
        self.num_segments
    }

    #[cfg(test)]
    pub(crate) fn with_segment_lengths(
        new_passes: u8,
        missing_msbs: u8,
        lengths: &[u32],
        first_segment_continues: bool,
    ) -> Self {
        let mut contribution = Self {
            new_passes,
            missing_msbs,
            first_segment_continues,
            ..Self::default()
        };
        for length in lengths {
            contribution.push_segment_length(*length);
        }
        contribution
    }
}

/// Persistent state for a code-block across quality layers.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BlockState {
    /// Total accumulated coding passes.
    pub num_passes: u16,
    /// Beta (length bits = beta + ceil_log2(passes)).
    pub beta: u8,
    /// Missing MSB planes (set on first inclusion).
    pub missing_msbs: u8,
    /// True once included in at least one packet.
    pub included: bool,
    pub segment_passes: u8,
    pub segment_max_passes: u8,
}

impl BlockState {
    pub fn warp() -> Self {
        Self {
            num_passes: 0,
            beta: 0,
            missing_msbs: 0,
            included: false,
            segment_passes: 0,
            segment_max_passes: 0,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BandBlocks {
    count: usize,
    states: HashMap<usize, BlockState>,
}

impl BandBlocks {
    pub fn warp(count: usize) -> Self {
        Self {
            count,
            states: HashMap::new(),
        }
    }

    pub fn len(&self) -> usize {
        self.count
    }

    fn update<R>(&mut self, idx: usize, f: impl FnOnce(&mut BlockState) -> R) -> R {
        if let Some(block) = self.states.get_mut(&idx) {
            return f(block);
        }
        let mut block = BlockState::warp();
        let result = f(&mut block);
        if block.included {
            self.states.insert(idx, block);
        }
        result
    }
}

/// Result of parsing one packet for an entire precinct.
#[derive(Debug, Clone)]
pub struct PacketParseResult {
    /// Included blocks per subband, `(raster index, contribution)`, raster order.
    pub contributions: Vec<Vec<(u32, BlockContribution)>>,
    /// Body bytes to read after the packet header.
    pub body_bytes: u64,
    /// Header bytes consumed (for accounting).
    pub header_bytes: u32,
}

/// Parse a single packet header for a precinct.
///
/// `reader` — positioned at the start of the packet header.
/// `tagtrees` — per-subband tag trees (persistent across layers).
/// `block_states` — per-subband block states (persistent across layers).
/// `layer_idx` — current quality layer index.
/// `empty_packets_before` — consecutive empty packets before this one.
pub fn comb_packet_header<S: HeaderBytes>(
    reader: &mut PacketBitReader<S>,
    tagtrees: &mut [TagTree],
    block_states: &mut [BandBlocks],
    layer_idx: u16,
    empty_packets_before: u16,
    modes: CodingModes,
) -> Result<PacketParseResult, PacketError> {
    // First bit: packet non-empty flag.
    let non_empty = reader.pluck_bit()? != 0;

    if !non_empty {
        // Empty packet — no contributions.
        let contributions = (0..block_states.len()).map(|_| Vec::new()).collect();
        let header_bytes = reader.fasten_off()?;
        return Ok(PacketParseResult {
            contributions,
            body_bytes: 0,
            header_bytes,
        });
    }

    let mut total_body_bytes: u64 = 0;
    let num_subbands = block_states.len();
    let mut contributions: Vec<Vec<(u32, BlockContribution)>> = Vec::with_capacity(num_subbands);

    for sb in 0..num_subbands {
        let tree = &mut tagtrees[sb];
        let blocks = &mut block_states[sb];
        let num_blocks = blocks.len();
        let mut sb_contribs = Vec::new();

        // A node reads bits only at its first leaf under a known parent.
        tree.comb_live_leaves(num_blocks, |tree, blk_idx| {
            let contrib = blocks.update(blk_idx, |block| {
                comb_block_contribution(
                    reader,
                    tree,
                    block,
                    blk_idx,
                    layer_idx,
                    empty_packets_before,
                    modes,
                )
            })?;
            total_body_bytes += contrib.segment_lengths().map(|&l| l as u64).sum::<u64>();
            if contrib.new_passes != 0 {
                sb_contribs.push((blk_idx as u32, contrib));
            }
            Ok(())
        })?;
        contributions.push(sb_contribs);
    }

    let header_bytes = reader.fasten_off()?;
    Ok(PacketParseResult {
        contributions,
        body_bytes: total_body_bytes,
        header_bytes,
    })
}

/// Parse a single block's contribution in a packet header.
fn comb_block_contribution<S: HeaderBytes>(
    reader: &mut PacketBitReader<S>,
    tree: &mut TagTree,
    block: &mut BlockState,
    blk_idx: usize,
    layer_idx: u16,
    _empty_packets_before: u16,
    modes: CodingModes,
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

    let segmented = modes.0 & (CodingModes::BYPASS | CodingModes::TERMALL) != 0;
    if !segmented {
        let segment_bytes = read_segment_length(reader, block.beta, new_passes)?;
        let mut contribution = BlockContribution {
            new_passes,
            missing_msbs: block.missing_msbs,
            first_segment_continues: block.num_passes != 0,
            ..BlockContribution::default()
        };
        contribution.push_segment_length(segment_bytes);
        block.num_passes += new_passes as u16;
        return Ok(contribution);
    }

    let first_segment_continues =
        block.segment_passes != 0 && block.segment_passes < block.segment_max_passes;
    let mut remaining_passes = new_passes;
    let mut contribution = BlockContribution {
        new_passes,
        missing_msbs: block.missing_msbs,
        first_segment_continues,
        ..BlockContribution::default()
    };
    while remaining_passes != 0 {
        if block.segment_max_passes == 0 || block.segment_passes == block.segment_max_passes {
            block.segment_max_passes = next_segment_max_passes(modes, block.segment_max_passes);
            block.segment_passes = 0;
        }
        let segment_passes = remaining_passes.min(block.segment_max_passes - block.segment_passes);
        let segment_bytes = read_segment_length(reader, block.beta, segment_passes)?;
        contribution.push_segment_length(segment_bytes);
        block.segment_passes += segment_passes;
        remaining_passes -= segment_passes;
    }

    block.num_passes += new_passes as u16;

    Ok(contribution)
}

fn next_segment_max_passes(modes: CodingModes, previous_max_passes: u8) -> u8 {
    if modes.0 & CodingModes::TERMALL != 0 {
        return 1;
    }
    if previous_max_passes == 0 {
        return 10;
    }
    if previous_max_passes == 1 || previous_max_passes == 10 {
        2
    } else {
        1
    }
}

fn floor_log2(value: u32) -> u32 {
    u32::BITS - 1 - value.leading_zeros()
}

fn read_segment_length<S: HeaderBytes>(
    reader: &mut PacketBitReader<S>,
    beta: u8,
    passes: u8,
) -> Result<u32, PacketError> {
    let length_bits = beta as u32 + floor_log2(passes as u32);
    if length_bits > 31 {
        return Err(PacketError::PrecisionOverflow);
    }
    let segment_bytes = reader.pluck_bits(length_bits)?;
    if segment_bytes >= (1 << 15) {
        return Err(PacketError::PrecisionOverflow);
    }
    Ok(segment_bytes)
}

/// Decode number of new coding passes (standard variable-length code):
/// 1 → 1, 01 → 2, 0000..0(5 bits) → 3-6, 00000..0(7 bits) → 6-37, etc.
fn tally_passes<S: HeaderBytes>(reader: &mut PacketBitReader<S>) -> Result<u8, PacketError> {
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
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PacketError {
    /// Ran out of data in the packet header.
    Truncated,
    InvalidMarker,
    /// Precision overflow (beta too large or segment too long).
    PrecisionOverflow,
    /// Illegal missing MSBs value (>74).
    IllegalMissingMsbs,
    /// Tag tree decoding error.
    TagTreeError,
    Read(String),
}

impl std::fmt::Display for PacketError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Truncated => write!(f, "packet header truncated"),
            Self::InvalidMarker => write!(f, "invalid marker in packet header"),
            Self::PrecisionOverflow => write!(f, "precision overflow"),
            Self::IllegalMissingMsbs => write!(f, "illegal missing MSBs (>74)"),
            Self::TagTreeError => write!(f, "tag tree decoding error"),
            Self::Read(msg) => write!(f, "reading packet header: {msg}"),
        }
    }
}

impl std::error::Error for PacketError {}

#[cfg(test)]
mod tests {
    use super::*;

    fn pack_header_bits(bits: &str) -> Vec<u8> {
        bits.as_bytes()
            .chunks(8)
            .map(|chunk| {
                let mut byte = 0u8;
                for bit in chunk {
                    byte = (byte << 1) | u8::from(*bit == b'1');
                }
                byte << (8 - chunk.len())
            })
            .collect()
    }

    fn parse_existing_block(
        bits: &str,
        block: &mut BlockState,
        modes: CodingModes,
    ) -> BlockContribution {
        let data = pack_header_bits(bits);
        assert!(!data.contains(&0xFF));
        comb_block_contribution(
            &mut PacketBitReader::warp(&data),
            &mut TagTree::warp(1, 1),
            block,
            0,
            0,
            0,
            modes,
        )
        .expect("block contribution parses")
    }

    #[test]
    fn bypass_segments_continue_and_split_across_layers() {
        let mut block = BlockState {
            num_passes: 9,
            beta: 3,
            missing_msbs: 2,
            included: true,
            segment_passes: 9,
            segment_max_passes: 10,
        };
        let modes = CodingModes(CodingModes::BYPASS);

        let crossing = parse_existing_block("1110001011001", &mut block, modes);
        assert_eq!(crossing.new_passes, 3);
        assert_eq!(
            crossing.segment_lengths().copied().collect::<Vec<_>>(),
            [5, 9]
        );
        assert!(crossing.first_segment_continues);
        assert_eq!(block.segment_passes, 2);
        assert_eq!(block.segment_max_passes, 2);

        let next = parse_existing_block("100110", &mut block, modes);
        assert_eq!(next.segment_lengths().copied().collect::<Vec<_>>(), [6]);
        assert!(!next.first_segment_continues);
        assert_eq!(block.segment_passes, 1);
        assert_eq!(block.segment_max_passes, 1);
    }

    #[test]
    fn termall_reads_one_length_per_pass() {
        let mut block = BlockState {
            num_passes: 4,
            beta: 3,
            missing_msbs: 1,
            included: true,
            segment_passes: 1,
            segment_max_passes: 1,
        };
        let modes = CodingModes(CodingModes::TERMALL);

        let contribution = parse_existing_block("111000001010011", &mut block, modes);
        assert_eq!(contribution.new_passes, 3);
        assert_eq!(
            contribution.segment_lengths().copied().collect::<Vec<_>>(),
            [1, 2, 3]
        );
        assert!(!contribution.first_segment_continues);
        assert_eq!(block.segment_passes, 1);
        assert_eq!(block.segment_max_passes, 1);
    }

    fn scan_every_block<S: HeaderBytes>(
        reader: &mut PacketBitReader<S>,
        tagtrees: &mut [TagTree],
        block_states: &mut [BandBlocks],
        layer_idx: u16,
        modes: CodingModes,
    ) -> Result<PacketParseResult, PacketError> {
        let non_empty = reader.pluck_bit()? != 0;
        if !non_empty {
            let contributions = (0..block_states.len()).map(|_| Vec::new()).collect();
            return Ok(PacketParseResult {
                contributions,
                body_bytes: 0,
                header_bytes: reader.fasten_off()?,
            });
        }
        let mut total_body_bytes = 0u64;
        let mut contributions = Vec::with_capacity(block_states.len());
        for sb in 0..block_states.len() {
            let num_blocks = block_states[sb].len();
            let mut sb_contribs = Vec::with_capacity(num_blocks);
            for blk_idx in 0..num_blocks {
                let contrib = block_states[sb].update(blk_idx, |block| {
                    comb_block_contribution(
                        reader,
                        &mut tagtrees[sb],
                        block,
                        blk_idx,
                        layer_idx,
                        0,
                        modes,
                    )
                })?;
                total_body_bytes += contrib.segment_lengths().map(|&l| l as u64).sum::<u64>();
                if contrib.new_passes != 0 {
                    sb_contribs.push((blk_idx as u32, contrib));
                }
            }
            contributions.push(sb_contribs);
        }
        Ok(PacketParseResult {
            contributions,
            body_bytes: total_body_bytes,
            header_bytes: reader.fasten_off()?,
        })
    }

    struct Rng(u64);
    impl Rng {
        fn next(&mut self) -> u32 {
            self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1);
            (self.0 >> 32) as u32
        }
    }

    #[test]
    fn live_leaves_match_a_full_block_scan() {
        let mut rng = Rng(0x5A17_7A9_u64);
        let shapes = [(1u32, 1u32), (1, 5), (5, 1), (3, 3), (3, 5), (8, 3), (7, 7)];
        for (w, h) in shapes {
            check_shapes(&mut rng, &[(w, h)]);
        }
        for _ in 0..40 {
            let bands = 1 + (rng.next() % 2) as usize;
            let shapes: Vec<(u32, u32)> = (0..bands)
                .map(|_| (1 + rng.next() % 8, 1 + rng.next() % 8))
                .collect();
            check_shapes(&mut rng, &shapes);
        }
    }

    fn check_shapes(rng: &mut Rng, shapes: &[(u32, u32)]) {
        let nbytes = 1 + (rng.next() % 64) as usize;
        let data: Vec<u8> = (0..nbytes).map(|_| rng.next() as u8).collect();
        let mut trees_a: Vec<TagTree> = shapes.iter().map(|&(w, h)| TagTree::warp(w, h)).collect();
        let mut trees_b = trees_a.clone();
        let mut states_a: Vec<BandBlocks> = shapes
            .iter()
            .map(|&(w, h)| BandBlocks::warp((w * h) as usize))
            .collect();
        let mut states_b = states_a.clone();
        for layer in 0..3u16 {
            let live = comb_packet_header(
                &mut PacketBitReader::warp(&data),
                &mut trees_a,
                &mut states_a,
                layer,
                0,
                CodingModes(0),
            );
            let full = scan_every_block(
                &mut PacketBitReader::warp(&data),
                &mut trees_b,
                &mut states_b,
                layer,
                CodingModes(0),
            );
            match (live, full) {
                (Ok(live), Ok(full)) => {
                    assert_eq!(
                        live.header_bytes, full.header_bytes,
                        "layer {layer} {shapes:?}"
                    );
                    assert_eq!(live.body_bytes, full.body_bytes, "layer {layer} {shapes:?}");
                    assert_eq!(
                        live.contributions, full.contributions,
                        "layer {layer} {shapes:?}"
                    );
                    assert_eq!(states_a, states_b, "layer {layer} {shapes:?}");
                    for band in &states_a {
                        assert!(band.states.values().all(|block| block.included));
                    }
                }
                (Err(a), Err(b)) => {
                    assert_eq!(a, b, "layer {layer} {shapes:?}");
                    break;
                }
                (live, full) => panic!("layer {layer} {shapes:?}: {live:?} vs {full:?}"),
            }
        }
    }

    #[test]
    fn root_exclusion_does_not_scan_the_grid() {
        let side = 2048u32;
        let blocks = (side * side) as usize;
        let mut trees = vec![TagTree::warp(side, side)];
        let mut states = vec![BandBlocks::warp(blocks)];
        // Non-empty packet, then a 0 bit.
        let data = [0x80u8];
        let started = std::time::Instant::now();
        for layer in 0..4u16 {
            let parsed = comb_packet_header(
                &mut PacketBitReader::warp(&data),
                &mut trees,
                &mut states,
                layer,
                0,
                CodingModes(0),
            )
            .expect("header parses");
            assert_eq!(parsed.body_bytes, 0);
            assert!(parsed.contributions[0].is_empty());
            assert!(states[0].states.is_empty());
        }
        let elapsed = started.elapsed();
        assert!(
            elapsed < std::time::Duration::from_millis(50),
            "excluded {blocks} blocks took {elapsed:?}"
        );
    }
}
