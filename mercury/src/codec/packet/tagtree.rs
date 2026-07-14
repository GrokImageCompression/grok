//! JPEG 2000 tag trees for packet-header decoding (ITU-T T.800 §B.10.2).
//!
//! A tag tree hierarchically codes integer values per code-block in a precinct.
//! Two per precinct-band: one for inclusion layer, one for missing MSBs.
//!
//! Layout: flat array, leaves first, then internal levels bottom-up to root.
//! Parent index = `(y/2) * parent_width + (x/2)` relative to each level's start.

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
    /// Offset into the nodes array where this level starts.
    start: usize,
    /// Columns.
    width: u32,
    /// Rows.
    #[allow(dead_code)]
    height: u32,
}

/// Tag tree holding persistent state across quality layers; one instance per
/// precinct-band carries both the inclusion and MSBs trees.
#[derive(Debug, Clone)]
pub struct TagTree {
    /// Nodes: leaves first, then internal levels bottom-up to root.
    inclusion_nodes: Vec<TagNode>,
    msbs_nodes: Vec<TagNode>,
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
                inclusion_nodes: Vec::new(),
                msbs_nodes: Vec::new(),
                levels: Vec::new(),
                leaf_width: 0,
            };
        }

        let mut levels = Vec::new();
        let mut total_nodes: usize = 0;
        let mut lw = width;
        let mut lh = height;

        loop {
            let count = (lw as usize) * (lh as usize);
            levels.push(LevelDesc {
                start: total_nodes,
                width: lw,
                height: lh,
            });
            total_nodes += count;
            if lw == 1 && lh == 1 {
                break;
            }
            lw = (lw + 1) >> 1;
            lh = (lh + 1) >> 1;
        }

        Self {
            inclusion_nodes: vec![TagNode::default(); total_nodes],
            msbs_nodes: vec![TagNode::default(); total_nodes],
            levels,
            leaf_width: width,
        }
    }

    /// Node index of a leaf (leaves are in raster order at the array start).
    #[inline]
    fn leaf_at(&self, blk_idx: usize) -> usize {
        blk_idx
    }

    /// (x, y) of a leaf from its linear index.
    #[inline]
    fn leaf_coords(&self, blk_idx: usize) -> (u32, u32) {
        let x = (blk_idx as u32) % self.leaf_width;
        let y = (blk_idx as u32) / self.leaf_width;
        (x, y)
    }

    /// Parent index of a node at (x, y) in a given level.
    #[inline]
    fn parent_at(&self, level: usize, x: u32, y: u32) -> usize {
        let parent_level = &self.levels[level + 1];
        parent_level.start + (y / 2) as usize * parent_level.width as usize + (x / 2) as usize
    }

    /// Node indices from root to leaf (inclusive).
    fn climb_to_root(&self, blk_idx: usize) -> Vec<usize> {
        let mut path = Vec::with_capacity(self.levels.len());
        let (mut x, mut y) = self.leaf_coords(blk_idx);
        let leaf_idx = self.leaf_at(blk_idx);
        path.push(leaf_idx);

        for level in 0..self.levels.len() - 1 {
            let parent_idx = self.parent_at(level, x, y);
            path.push(parent_idx);
            x >>= 1;
            y >>= 1;
        }

        path.reverse(); // root first
        path
    }

    /// Decode inclusion; true if the block is included in the current packet
    /// (inclusion value equals layer_idx). `threshold` = layer_idx + 1.
    pub fn comb_inclusion(
        &mut self,
        reader: &mut PacketBitReader,
        blk_idx: usize,
        threshold: u16,
    ) -> Result<bool, PacketError> {
        if self.levels.is_empty() {
            return Err(PacketError::TagTreeError);
        }

        let path = self.climb_to_root(blk_idx);

        if path.len() == 1 {
            // Single-node tree (1x1 grid): leaf is root.
            let node = &mut self.inclusion_nodes[path[0]];
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
        for &node_idx in &path[..path.len() - 1] {
            let node = &mut self.inclusion_nodes[node_idx];
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
        let leaf_idx = *path.last().unwrap();
        let node = &mut self.inclusion_nodes[leaf_idx];
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
        if self.levels.is_empty() {
            return Err(PacketError::TagTreeError);
        }

        let path = self.climb_to_root(blk_idx);

        if path.len() == 1 {
            // Single-node tree.
            let node = &mut self.msbs_nodes[path[0]];
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
        let leaf_idx = *path.last().unwrap();

        loop {
            // Read leaf's wbar for the threshold BEFORE mutating internal
            // nodes (threshold = msbs_wbar + 1).
            let threshold = self.msbs_nodes[leaf_idx].wbar + 1;
            let mut wbar_min: u16 = 0;

            // Internal nodes only.
            for &node_idx in &path[..path.len() - 1] {
                let node = &mut self.msbs_nodes[node_idx];
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
            let leaf = &mut self.msbs_nodes[leaf_idx];
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
        assert_eq!(tree.inclusion_nodes.len(), 1);
        assert_eq!(tree.levels.len(), 1);
    }

    #[test]
    fn two_by_two_grid() {
        let tree = TagTree::warp(2, 2);
        // 4 leaves + 1 root
        assert_eq!(tree.inclusion_nodes.len(), 5);
        assert_eq!(tree.levels.len(), 2);
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
    fn msbs_lone_block() {
        // '0','0','1' → msbs = 2.
        let mut tree = TagTree::warp(1, 1);
        let data = [0b00100000]; // bits 0,0,1,…
        let mut reader = PacketBitReader::warp(&data);
        let msbs = tree.comb_msbs(&mut reader, 0).unwrap();
        assert_eq!(msbs, 2);
    }
}
