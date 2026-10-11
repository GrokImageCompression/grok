//! JPEG 2000 tag trees for packet-header decoding (ITU-T T.800 §B.10.2).
//!
//! A tag tree hierarchically codes integer values per code-block in a precinct.
//! Two per precinct-band: one for inclusion layer, one for missing MSBs.

use std::collections::{BTreeSet, HashMap};

use super::PacketError;
use super::bitread::PacketBitReader;

/// One node's persistent state.
#[derive(Debug, Clone, Copy, Default)]
struct TagNode {
    /// Known minimum value decoded so far.
    w: u16,
    /// Threshold being tested.
    wbar: u16,
}

/// One tree level.
#[derive(Debug, Clone, Copy)]
struct LevelDesc {
    /// Columns.
    width: u32,
    /// Rows.
    height: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct NodeId {
    level: u32,
    x: u32,
    y: u32,
}

#[derive(Debug, Clone, Default)]
struct NodeStore {
    nodes: HashMap<NodeId, TagNode>,
}

impl NodeStore {
    fn get(&self, level: usize, x: u32, y: u32) -> TagNode {
        self.nodes
            .get(&NodeId {
                level: level as u32,
                x,
                y,
            })
            .copied()
            .unwrap_or_default()
    }

    fn get_mut(&mut self, level: usize, x: u32, y: u32) -> &mut TagNode {
        self.nodes
            .entry(NodeId {
                level: level as u32,
                x,
                y,
            })
            .or_default()
    }
}

/// Tag tree holding persistent state across quality layers; one instance per
/// precinct-band carries both the inclusion and MSBs trees.
#[derive(Debug, Clone)]
pub struct TagTree {
    inclusion: NodeStore,
    msbs: NodeStore,
    /// Level descriptors (level 0 = leaves).
    levels: Vec<LevelDesc>,
    /// Leaf-level width.
    leaf_width: u32,
}

impl TagTree {
    /// Build a tag tree for a block grid of the given dimensions.
    pub fn warp(width: u32, height: u32) -> Self {
        if width == 0 || height == 0 {
            return Self {
                inclusion: NodeStore::default(),
                msbs: NodeStore::default(),
                levels: Vec::new(),
                leaf_width: 0,
            };
        }

        let mut levels = Vec::new();
        let mut lw = width;
        let mut lh = height;

        loop {
            levels.push(LevelDesc {
                width: lw,
                height: lh,
            });
            if lw == 1 && lh == 1 {
                break;
            }
            lw = (lw + 1) >> 1;
            lh = (lh + 1) >> 1;
        }

        Self {
            inclusion: NodeStore::default(),
            msbs: NodeStore::default(),
            levels,
            leaf_width: width,
        }
    }

    fn coords_on_level(&self, blk_idx: usize, level: usize) -> (u32, u32) {
        let (mut x, mut y) = self.leaf_coords(blk_idx);
        for _ in 0..level {
            x >>= 1;
            y >>= 1;
        }
        (x, y)
    }

    /// (x, y) of a leaf from its linear index.
    #[inline]
    fn leaf_coords(&self, blk_idx: usize) -> (u32, u32) {
        let x = (blk_idx as u32) % self.leaf_width;
        let y = (blk_idx as u32) / self.leaf_width;
        (x, y)
    }

    // `w < wbar` means a 1 bit has fixed the value.
    fn inclusion_known(&self, level: usize, x: u32, y: u32) -> bool {
        let node = self.inclusion.get(level, x, y);
        node.w < node.wbar
    }

    fn first_leaf(&self, level: usize, row: u32, column: u32) -> usize {
        (((row as u64) << level) * self.leaf_width as u64 + ((column as u64) << level)) as usize
    }

    pub(super) fn comb_live_leaves(
        &mut self,
        leaf_count: usize,
        mut visit: impl FnMut(&mut Self, usize) -> Result<(), PacketError>,
    ) -> Result<(), PacketError> {
        if leaf_count == 0 {
            return Ok(());
        }
        if self.levels.is_empty() || self.leaf_width == 0 {
            return Err(PacketError::TagTreeError);
        }
        let mut pending = BTreeSet::new();
        pending.insert(0usize);
        while let Some(leaf) = pending.pop_first() {
            if leaf >= leaf_count {
                continue;
            }
            visit(self, leaf)?;
            self.enqueue_sibling_leaves(leaf, &mut pending);
        }
        Ok(())
    }

    fn enqueue_sibling_leaves(&self, leaf: usize, pending: &mut BTreeSet<usize>) {
        let mut row = (leaf as u32) / self.leaf_width;
        let mut column = (leaf as u32) % self.leaf_width;
        for level in 0..self.levels.len() {
            if self.first_leaf(level, row, column) != leaf {
                break;
            }
            if level != 0 && self.inclusion_known(level, column, row) {
                let child = &self.levels[level - 1];
                let row_end = (row * 2 + 2).min(child.height);
                let col_end = (column * 2 + 2).min(child.width);
                for child_row in (row * 2)..row_end {
                    for child_col in (column * 2)..col_end {
                        if child_row == row * 2 && child_col == column * 2 {
                            continue;
                        }
                        pending.insert(self.first_leaf(level - 1, child_row, child_col));
                    }
                }
            }
            row >>= 1;
            column >>= 1;
        }
    }

    /// Decode inclusion; true if the block is included in the current packet
    /// (inclusion value equals layer_idx). `threshold` = layer_idx + 1.
    pub fn comb_inclusion(
        &mut self,
        reader: &mut PacketBitReader,
        blk_idx: usize,
        threshold: u16,
    ) -> Result<bool, PacketError> {
        let n = self.levels.len();
        if n == 0 {
            return Err(PacketError::TagTreeError);
        }

        if n == 1 {
            // Single-node tree (1x1 grid): leaf is root.
            let (x, y) = self.leaf_coords(blk_idx);
            let node = self.inclusion.get_mut(0, x, y);
            while node.w == node.wbar && node.wbar < threshold {
                node.wbar += 1;
                if reader.pluck_bit()? == 0 {
                    node.w += 1;
                }
            }
            return Ok(node.w < threshold && node.w == threshold - 1);
        }

        // Multi-level: root toward leaf. Per level, raise wbar to at least
        // parent's w, then decode bits until w != wbar or wbar >= threshold.
        let mut wbar_min: u16 = 0;

        // Root down to (but not including) the leaf.
        for level in (1..n).rev() {
            let (x, y) = self.coords_on_level(blk_idx, level);
            let node = self.inclusion.get_mut(level, x, y);
            if node.wbar < wbar_min {
                node.wbar = wbar_min;
                node.w = wbar_min;
            }
            while node.w == node.wbar && node.wbar < threshold {
                node.wbar += 1;
                let bit = reader.pluck_bit_soft()?;
                if bit < 0 {
                    return Err(PacketError::Truncated);
                }
                if bit == 0 {
                    node.w += 1;
                }
            }
            wbar_min = node.w;
        }

        if wbar_min >= threshold {
            return Ok(false); // nothing can be included
        }

        // The leaf itself.
        let (x, y) = self.leaf_coords(blk_idx);
        let node = self.inclusion.get_mut(0, x, y);
        if node.wbar < wbar_min {
            node.wbar = wbar_min;
            node.w = wbar_min;
        }

        // Decode until included or threshold reached.
        loop {
            if node.w == node.wbar && node.wbar < threshold {
                node.wbar += 1;
                if reader.pluck_bit()? == 0 {
                    node.w += 1;
                } else {
                    return Ok(true); // included
                }
            } else if node.wbar >= threshold {
                return Ok(false);
            } else {
                return Ok(false); // w < wbar: not included at this layer
            }
        }
    }

    /// Decode missing MSBs for a block (after first inclusion). Repeatedly
    /// walks root→leaf with threshold = leaf.wbar + 1, decoding internal nodes
    /// then one leaf bit; leaf '1' bit ends it. Returns missing-MSB plane count.
    pub fn comb_msbs(
        &mut self,
        reader: &mut PacketBitReader,
        blk_idx: usize,
    ) -> Result<u8, PacketError> {
        let n = self.levels.len();
        if n == 0 {
            return Err(PacketError::TagTreeError);
        }

        if n == 1 {
            // Single-node tree.
            let (x, y) = self.leaf_coords(blk_idx);
            let node = self.msbs.get_mut(0, x, y);
            loop {
                node.wbar += 1;
                if reader.pluck_bit()? != 0 {
                    break;
                }
                node.w += 1;
                if node.w > 74 {
                    return Err(PacketError::IllegalMissingMsbs);
                }
            }
            return Ok(node.w as u8);
        }

        // Multi-level: root toward leaf.
        let (leaf_x, leaf_y) = self.leaf_coords(blk_idx);
        loop {
            // Read leaf's wbar for the threshold BEFORE mutating internal
            // nodes (threshold = msbs_wbar + 1).
            let threshold = self.msbs.get(0, leaf_x, leaf_y).wbar + 1;
            let mut wbar_min: u16 = 0;

            // Internal nodes only.
            for level in (1..n).rev() {
                let (x, y) = self.coords_on_level(blk_idx, level);
                let node = self.msbs.get_mut(level, x, y);
                if node.wbar < wbar_min {
                    node.wbar = wbar_min;
                    node.w = wbar_min;
                }
                while node.w == node.wbar && node.wbar < threshold {
                    node.wbar += 1;
                    let bit = reader.pluck_bit_soft()?;
                    if bit < 0 {
                        return Err(PacketError::Truncated);
                    }
                    if bit == 0 {
                        node.w += 1;
                        if node.w > 74 {
                            return Err(PacketError::IllegalMissingMsbs);
                        }
                    }
                }
                wbar_min = node.w;
            }

            // The leaf node.
            let leaf = self.msbs.get_mut(0, leaf_x, leaf_y);
            if leaf.wbar < wbar_min {
                // Parent's minimum already covers this: update and loop back.
                leaf.wbar = wbar_min;
                leaf.w = wbar_min;
                continue;
            }
            leaf.wbar += 1;
            if reader.pluck_bit()? != 0 {
                return Ok(leaf.w as u8);
            }
            leaf.w += 1;
            if leaf.w > 74 {
                return Err(PacketError::IllegalMissingMsbs);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn one_leaf_tree() {
        let tree = TagTree::warp(1, 1);
        assert!(tree.inclusion.nodes.is_empty());
        assert_eq!(tree.levels.len(), 1);
    }

    #[test]
    fn two_by_two_grid() {
        let tree = TagTree::warp(2, 2);
        assert!(tree.inclusion.nodes.is_empty());
        assert_eq!(tree.levels.len(), 2);
    }

    #[test]
    fn a_wide_grid_allocates_no_nodes_until_a_decode() {
        let tree = TagTree::warp(8192, 8192);
        assert!(tree.inclusion.nodes.is_empty());
        assert!(tree.msbs.nodes.is_empty());
        assert!(tree.levels.len() > 1);
    }

    #[test]
    fn one_excluded_leaf_allocates_only_the_path() {
        let mut tree = TagTree::warp(64, 64);
        let mut reader = PacketBitReader::warp(&[0x00]);
        assert!(!tree.comb_inclusion(&mut reader, 0, 1).unwrap());
        assert!(tree.inclusion.nodes.len() <= tree.levels.len());
        assert!(tree.inclusion.nodes.len() < 64 * 64);
        assert!(tree.msbs.nodes.is_empty());
    }

    #[test]
    fn inclusion_lone_block_layer0() {
        // Single block, layer 0: '1' bit → included.
        let mut tree = TagTree::warp(1, 1);
        let data = [0x80u8]; // first bit 1
        let mut reader = PacketBitReader::warp(&data);
        let result = tree.comb_inclusion(&mut reader, 0, 1).unwrap();
        assert!(result);
    }

    #[test]
    fn inclusion_lone_block_excluded() {
        // Single block, layer 0: '0' then threshold reached → not included.
        let mut tree = TagTree::warp(1, 1);
        let data = [0x00u8]; // first bit 0 → w++, wbar=1=threshold
        let mut reader = PacketBitReader::warp(&data);
        let result = tree.comb_inclusion(&mut reader, 0, 1).unwrap();
        assert!(!result);
    }

    #[test]
    fn an_unknown_root_visits_only_the_first_leaf() {
        let mut tree = TagTree::warp(32, 17);
        let mut seen = Vec::new();
        tree.comb_live_leaves(32 * 17, |_, leaf| {
            seen.push(leaf);
            Ok(())
        })
        .unwrap();
        assert_eq!(seen, vec![0]);
    }

    #[test]
    fn a_raised_root_bound_still_skips_other_leaves() {
        let mut tree = TagTree::warp(8, 8);
        let data = [0x00u8];
        let mut reader = PacketBitReader::warp(&data);
        assert!(!tree.comb_inclusion(&mut reader, 0, 1).unwrap());
        let mut seen = Vec::new();
        tree.comb_live_leaves(64, |_, leaf| {
            seen.push(leaf);
            Ok(())
        })
        .unwrap();
        assert_eq!(seen, vec![0]);
    }

    #[test]
    fn a_known_root_visits_each_child_first_leaf_only() {
        for (width, height, expect) in [
            (4u32, 4u32, vec![0usize, 2, 8, 10]),
            (3, 3, vec![0, 2, 6, 8]),
        ] {
            let mut tree = TagTree::warp(width, height);
            let root = tree.levels.len() - 1;
            *tree.inclusion.get_mut(root, 0, 0) = TagNode { w: 0, wbar: 1 };
            let mut seen = Vec::new();
            tree.comb_live_leaves((width * height) as usize, |_, leaf| {
                seen.push(leaf);
                Ok(())
            })
            .unwrap();
            assert_eq!(seen, expect, "{width}x{height}");
        }
    }

    #[test]
    fn msbs_lone_block() {
        // '0','0','1' → msbs = 2.
        let mut tree = TagTree::warp(1, 1);
        let data = [0b00100000]; // bits 0,0,1,…
        let mut reader = PacketBitReader::warp(&data);
        let msbs = tree.comb_msbs(&mut reader, 0).unwrap();
        assert_eq!(msbs, 2);
    }
}
