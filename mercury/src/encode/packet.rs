use crate::codec::packet_order::PrecBand;

use super::band_coder::CodedBand;

// classic's IMSB tag tree holds u8 values and encodes up to their max
const IMSB_THRESHOLD: u16 = u8::MAX as u16;
const INITIAL_LENGTH_BITS: u8 = 3;
// 16 bit leaf dimensions halve at most 16 times
const MAX_TREE_LEVELS: usize = 17;

// MSB first, and a byte after 0xFF carries only 7 bits
pub(crate) struct HeaderBitWriter<'a> {
    out: &'a mut Vec<u8>,
    pending: u8,
    free_bits: u8,
}

impl<'a> HeaderBitWriter<'a> {
    pub fn warp(out: &'a mut Vec<u8>) -> Self {
        Self {
            out,
            pending: 0,
            free_bits: 8,
        }
    }

    fn emit(&mut self) {
        self.out.push(self.pending);
        self.free_bits = if self.pending == 0xFF { 7 } else { 8 };
        self.pending = 0;
    }

    pub fn write_bits(&mut self, value: u32, mut count: u8) {
        while count > 0 {
            if self.free_bits == 0 {
                self.emit();
            }
            let take = count.min(self.free_bits);
            count -= take;
            self.free_bits -= take;
            let bits = (value >> count) & ((1u32 << take) - 1);
            self.pending |= (bits << self.free_bits) as u8;
        }
    }

    pub fn write_bit(&mut self, bit: bool) {
        self.write_bits(bit as u32, 1);
    }

    fn write_comma_code(&mut self, ones: u8) {
        self.write_bits(((1u32 << ones) - 1) << 1, ones + 1);
    }

    fn write_pass_count(&mut self, passes: u8) {
        match passes {
            1 => self.write_bits(0, 1),
            2 => self.write_bits(2, 2),
            3..=5 => self.write_bits(0xc | (passes as u32 - 3), 4),
            6..=36 => self.write_bits(0x1e0 | (passes as u32 - 6), 9),
            _ => self.write_bits(0xff80 | (passes as u32 - 37), 16),
        }
    }

    pub fn flush(mut self) {
        self.emit();
        if self.free_bits == 7 {
            self.emit();
        }
    }
}

#[derive(Clone, Copy)]
struct TagNode {
    value: u16,
    low: u16,
    known: bool,
}

const UNSET_NODE: TagNode = TagNode {
    value: u16::MAX,
    low: 0,
    known: false,
};

struct TagTreeEncoder {
    leaves_width: u32,
    level_widths: Vec<u32>,
    level_bases: Vec<u32>,
    nodes: Vec<TagNode>,
}

impl TagTreeEncoder {
    fn warp(width: u32, height: u32) -> Self {
        let (mut level_width, mut level_height) = (width, height);
        let mut level_widths = Vec::new();
        let mut level_bases = Vec::new();
        let mut total = 0;
        loop {
            let nodes_in_level = level_width * level_height;
            level_widths.push(level_width);
            level_bases.push(total);
            total += nodes_in_level;
            if nodes_in_level <= 1 {
                break;
            }
            level_width = level_width.div_ceil(2);
            level_height = level_height.div_ceil(2);
        }
        Self {
            leaves_width: width,
            level_widths,
            level_bases,
            nodes: vec![UNSET_NODE; total as usize],
        }
    }

    fn reset(&mut self) {
        self.nodes.fill(UNSET_NODE);
    }

    fn path_to_root(&self, leaf: u32) -> [usize; MAX_TREE_LEVELS] {
        let mut path = [0; MAX_TREE_LEVELS];
        let (mut row, mut column) = (leaf / self.leaves_width, leaf % self.leaves_width);
        for (level, node) in path.iter_mut().take(self.level_widths.len()).enumerate() {
            *node = (self.level_bases[level] + row * self.level_widths[level] + column) as usize;
            row >>= 1;
            column >>= 1;
        }
        path
    }

    fn set(&mut self, leaf: u32, value: u16) {
        let path = self.path_to_root(leaf);
        for &index in &path[..self.level_widths.len()] {
            let node = &mut self.nodes[index];
            if node.value <= value {
                break;
            }
            node.value = value;
        }
    }

    fn encode(&mut self, writer: &mut HeaderBitWriter, leaf: u32, threshold: u16) {
        let path = self.path_to_root(leaf);
        let mut low = 0;
        for &index in path[..self.level_widths.len()].iter().rev() {
            let node = &mut self.nodes[index];
            if node.low < low {
                node.low = low;
            } else {
                low = node.low;
            }
            while low < threshold {
                if low >= node.value {
                    if !node.known {
                        writer.write_bit(true);
                        node.known = true;
                    }
                    break;
                }
                writer.write_bit(false);
                low += 1;
            }
            node.low = low;
        }
    }
}

#[derive(Clone, Copy, Default)]
struct BlockHeaderState {
    passes_written: u8,
    length_bits: u8,
}

struct PrecinctBandCoder {
    range: PrecBand,
    inclusion: TagTreeEncoder,
    imsb: TagTreeEncoder,
    blocks: Vec<BlockHeaderState>,
}

pub(crate) struct BandPacketSource<'a> {
    pub coded: &'a CodedBand,
    pub blocks_across: u32,
    pub max_bit_planes: u8,
    // passes included through this packet's layer, per block in band raster order
    pub included_passes: &'a [u8],
}

// header state of one precinct, kept across its layers
pub(crate) struct PrecinctCoder {
    bands: Vec<PrecinctBandCoder>,
}

impl PrecinctCoder {
    pub fn warp(ranges: Vec<PrecBand>) -> Self {
        let bands = ranges
            .into_iter()
            .map(|range| PrecinctBandCoder {
                inclusion: TagTreeEncoder::warp(range.nbw, range.nbh),
                imsb: TagTreeEncoder::warp(range.nbw, range.nbh),
                blocks: vec![BlockHeaderState::default(); (range.nbw * range.nbh) as usize],
                range,
            })
            .collect();
        Self { bands }
    }

    fn band_block_index(range: &PrecBand, blocks_across: u32, precinct_block: u32) -> usize {
        let x = range.bx0 + precinct_block % range.nbw;
        let y = range.by0 + precinct_block / range.nbw;
        (y * blocks_across + x) as usize
    }

    // the returned length counts the body even when it is not written
    pub fn write_packet(
        &mut self,
        layer: u16,
        sources: &[BandPacketSource],
        out: &mut Vec<u8>,
        with_body: bool,
    ) -> usize {
        let start = out.len();
        let mut body_length = 0;
        let mut writer = HeaderBitWriter::warp(out);
        for (band, source) in self.bands.iter_mut().zip(sources) {
            if band.blocks.is_empty() {
                continue;
            }
            if layer == 0 {
                band.inclusion.reset();
                band.imsb.reset();
                for (precinct_block, state) in band.blocks.iter_mut().enumerate() {
                    *state = BlockHeaderState::default();
                    let index = Self::band_block_index(
                        &band.range,
                        source.blocks_across,
                        precinct_block as u32,
                    );
                    let numbps = source.coded.blocks[index].numbps;
                    assert!(
                        numbps <= source.max_bit_planes,
                        "code-block has {numbps} bit planes, band maximum {}",
                        source.max_bit_planes
                    );
                    band.imsb.set(
                        precinct_block as u32,
                        (source.max_bit_planes - numbps) as u16,
                    );
                }
            }
        }

        // classic always signals a non-empty packet
        writer.write_bit(true);
        for (band, source) in self.bands.iter_mut().zip(sources) {
            for (precinct_block, state) in band.blocks.iter().enumerate() {
                let index = Self::band_block_index(
                    &band.range,
                    source.blocks_across,
                    precinct_block as u32,
                );
                if state.passes_written == 0 && source.included_passes[index] > 0 {
                    band.inclusion.set(precinct_block as u32, layer);
                }
            }
            for (precinct_block, state) in band.blocks.iter_mut().enumerate() {
                let index = Self::band_block_index(
                    &band.range,
                    source.blocks_across,
                    precinct_block as u32,
                );
                let new_passes = source.included_passes[index] - state.passes_written;
                if state.passes_written == 0 {
                    band.inclusion
                        .encode(&mut writer, precinct_block as u32, layer + 1);
                } else {
                    writer.write_bit(new_passes > 0);
                }
                if new_passes == 0 {
                    continue;
                }
                if state.passes_written == 0 {
                    state.length_bits = INITIAL_LENGTH_BITS;
                    band.imsb
                        .encode(&mut writer, precinct_block as u32, IMSB_THRESHOLD);
                }
                writer.write_pass_count(new_passes);
                // only a block's final pass is terminated, so the new passes are one segment
                let block = &source.coded.blocks[index];
                let passes = source.coded.block_passes(block);
                let previous_rate = match state.passes_written {
                    0 => 0,
                    written => passes[written as usize - 1].rate,
                };
                let length = (passes[source.included_passes[index] as usize - 1].rate
                    - previous_rate) as u32;
                let pass_bits = floor_log2(new_passes as u32);
                let increment = (floor_log2(length) as i32 + 1
                    - (state.length_bits as i32 + pass_bits as i32))
                    .max(0) as u8;
                writer.write_comma_code(increment);
                state.length_bits += increment;
                writer.write_bits(length, state.length_bits + pass_bits);
                body_length += length as usize;
            }
        }
        writer.flush();
        let header_length = out.len() - start;

        for (band, source) in self.bands.iter_mut().zip(sources) {
            for (precinct_block, state) in band.blocks.iter_mut().enumerate() {
                let index = Self::band_block_index(
                    &band.range,
                    source.blocks_across,
                    precinct_block as u32,
                );
                let included = source.included_passes[index];
                if included == state.passes_written {
                    continue;
                }
                if with_body {
                    let block = &source.coded.blocks[index];
                    let passes = source.coded.block_passes(block);
                    let from = match state.passes_written {
                        0 => 0,
                        written => passes[written as usize - 1].rate as usize,
                    };
                    let to = passes[included as usize - 1].rate as usize;
                    out.extend_from_slice(&source.coded.block_bytes(block)[from..to]);
                }
                state.passes_written = included;
            }
        }
        header_length + body_length
    }
}

// classic's floorlog2: 0 for 0 and 1
fn floor_log2(value: u32) -> u8 {
    if value <= 1 {
        0
    } else {
        (31 - value.leading_zeros()) as u8
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::codec::packet::{BandBlocks, PacketBitReader, TagTree, comb_packet_header};
    use crate::encode::band_coder::{CodedBlock, CodedPass};

    const BLOCKS_ACROSS: u32 = 5;
    const BLOCKS_DOWN: u32 = 4;
    const MAX_BIT_PLANES: u8 = 13;
    const LAYERS: u16 = 3;

    struct Rng(u64);

    impl Rng {
        fn next(&mut self) -> u32 {
            self.0 = self
                .0
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            (self.0 >> 33) as u32
        }

        fn below(&mut self, bound: u32) -> u32 {
            self.next() % bound
        }
    }

    fn random_band(rng: &mut Rng) -> CodedBand {
        let mut band = CodedBand::default();
        for _ in 0..BLOCKS_ACROSS * BLOCKS_DOWN {
            let passes = rng.below(12) as u8;
            band.blocks.push(CodedBlock {
                numbps: 1 + rng.below(MAX_BIT_PLANES as u32) as u8,
                first_pass: band.passes.len() as u32,
                passes,
                first_byte: band.bytes.len() as u32,
            });
            let mut rate = 0u16;
            for _ in 0..passes {
                // zero-length passes and lengths past 2^8 both occur
                rate += [0, 1 + rng.below(40), 200 + rng.below(3000)][rng.below(3) as usize] as u16;
                band.passes.push(CodedPass { rate, slope: 0 });
            }
            band.bytes.extend((0..rate).map(|_| rng.next() as u8));
        }
        band
    }

    fn random_layers(rng: &mut Rng, band: &CodedBand) -> Vec<Vec<u8>> {
        let mut layers: Vec<Vec<u8>> = vec![Vec::new(); LAYERS as usize];
        for block in &band.blocks {
            let mut cuts: Vec<u8> = (0..LAYERS)
                .map(|_| rng.below(block.passes as u32 + 1) as u8)
                .collect();
            cuts.sort_unstable();
            for (layer, cut) in layers.iter_mut().zip(cuts) {
                layer.push(cut);
            }
        }
        layers
    }

    #[test]
    fn packets_parse_back_with_the_decoder() {
        let mut rng = Rng(0x7E57_C0DE);
        let ranges = [
            PrecBand {
                bx0: 1,
                by0: 1,
                nbw: 3,
                nbh: 2,
            },
            PrecBand {
                bx0: 0,
                by0: 0,
                nbw: BLOCKS_ACROSS,
                nbh: BLOCKS_DOWN,
            },
            PrecBand {
                bx0: 4,
                by0: 3,
                nbw: 1,
                nbh: 1,
            },
        ];
        let mut stuffed_headers = 0;
        for _ in 0..300 {
            let bands: Vec<CodedBand> = (0..ranges.len()).map(|_| random_band(&mut rng)).collect();
            let layers: Vec<Vec<Vec<u8>>> = bands
                .iter()
                .map(|band| random_layers(&mut rng, band))
                .collect();
            let mut coder = PrecinctCoder::warp(ranges.to_vec());
            let mut trees: Vec<TagTree> =
                ranges.iter().map(|r| TagTree::warp(r.nbw, r.nbh)).collect();
            let mut states: Vec<BandBlocks> = ranges
                .iter()
                .map(|r| BandBlocks::warp((r.nbw * r.nbh) as usize))
                .collect();
            let mut previous: Vec<Vec<u8>> = bands
                .iter()
                .map(|band| vec![0; band.blocks.len()])
                .collect();
            for layer in 0..LAYERS {
                let sources: Vec<BandPacketSource> = bands
                    .iter()
                    .zip(&layers)
                    .map(|(coded, band_layers)| BandPacketSource {
                        coded,
                        blocks_across: BLOCKS_ACROSS,
                        max_bit_planes: MAX_BIT_PLANES,
                        included_passes: &band_layers[layer as usize],
                    })
                    .collect();
                let mut packet = Vec::new();
                let length = coder.write_packet(layer, &sources, &mut packet, true);
                assert_eq!(length, packet.len());

                let parsed = comb_packet_header(
                    &mut PacketBitReader::warp(&packet),
                    &mut trees,
                    &mut states,
                    layer,
                    0,
                    crate::codec::params::CodingModes(0),
                )
                .expect("decoder parses the header");
                let header_length = parsed.header_bytes as usize;
                assert_eq!(header_length + parsed.body_bytes as usize, packet.len());
                if packet[..header_length].contains(&0xFF) {
                    stuffed_headers += 1;
                }

                let mut expected_body = Vec::new();
                for (b, range) in ranges.iter().enumerate() {
                    let mut expected = Vec::new();
                    for precinct_block in 0..range.nbw * range.nbh {
                        let index =
                            PrecinctCoder::band_block_index(range, BLOCKS_ACROSS, precinct_block);
                        let (before, now) = (previous[b][index], layers[b][layer as usize][index]);
                        if now == before {
                            continue;
                        }
                        let block = &bands[b].blocks[index];
                        let passes = bands[b].block_passes(block);
                        let from = if before == 0 {
                            0
                        } else {
                            passes[before as usize - 1].rate
                        };
                        let to = passes[now as usize - 1].rate;
                        expected.push((
                            precinct_block,
                            now - before,
                            MAX_BIT_PLANES - block.numbps,
                            (to - from) as u32,
                        ));
                        expected_body.extend_from_slice(
                            &bands[b].block_bytes(block)[from as usize..to as usize],
                        );
                        previous[b][index] = now;
                    }
                    let got: Vec<(u32, u8, u8, u32)> = parsed.contributions[b]
                        .iter()
                        .map(|(i, c)| {
                            (
                                *i,
                                c.new_passes,
                                c.missing_msbs,
                                *c.segment_lengths().next().unwrap(),
                            )
                        })
                        .collect();
                    assert_eq!(got, expected, "band {b} layer {layer}");
                }
                assert_eq!(&packet[header_length..], &expected_body[..]);
            }
        }
        assert!(stuffed_headers > 0, "no header hit a 0xFF byte");
    }

    #[test]
    fn a_trailing_ff_gets_a_stuffing_byte() {
        let mut out = Vec::new();
        let mut writer = HeaderBitWriter::warp(&mut out);
        writer.write_bits(0xFF, 8);
        writer.flush();
        assert_eq!(out, [0xFF, 0x00]);
    }
}
