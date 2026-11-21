use super::tile_packets::{TileBands, TilePacketWriter};

const SLOPE_BUCKETS: usize = u16::MAX as usize + 1;
const MAX_SLOPE: u32 = u16::MAX as u32;
// two octaves either side of the hint
const HINT_BRACKET_HALF_WIDTH: u32 = 512;
const MAX_MEASUREMENTS: u32 = 12;
const UNTESTED: u32 = u32::MAX;
const NO_BUDGET: u32 = u32::MAX;

pub(crate) struct RateTargets {
    pub max_layer_length: u32,
    // 0 for no per-component cap
    pub component_cap: u64,
    pub tolerance: f64,
    pub slope_hint: u16,
}

pub(crate) struct RateAllocation {
    // passes included per block, per band of the tile
    pub included: Vec<Vec<u8>>,
    // 0 when every pass fit
    pub slope_threshold: u16,
}

// classic's rateAllocateWithFallback over pcrdBisectFeasible, one layer
pub(crate) fn allocate_rate(
    writer: &mut TilePacketWriter,
    bands: &TileBands,
    targets: &RateTargets,
) -> Option<RateAllocation> {
    let mut search = Search::warp(writer, bands, targets);
    if let Some(allocation) = search.allocate(false, false) {
        return Some(allocation);
    }
    if targets.slope_hint != 0 {
        if let Some(allocation) = search.allocate(false, true) {
            return Some(allocation);
        }
    }
    search.allocate(true, true)
}

struct Search<'w, 'a> {
    writer: &'w mut TilePacketWriter,
    bands: &'w TileBands<'a>,
    targets: &'w RateTargets,
    num_comps: usize,
    included: Vec<Vec<u8>>,
    scratch: Vec<u8>,
    component_bytes: Vec<u64>,
    min_slope: u32,
    // [comp][slope + 1]: body bytes of the passes a threshold at slope keeps
    body_bytes_above: Vec<u32>,
    passes_above: Vec<u32>,
    max_layer_length: u32,
    measured_headers: Vec<u64>,
    measured_passes: u64,
    have_estimate: bool,
    last_measured_thresh: u32,
    last_measured_bytes: u64,
    last_measured_fits: bool,
}

impl<'w, 'a> Search<'w, 'a> {
    fn warp(
        writer: &'w mut TilePacketWriter,
        bands: &'w TileBands<'a>,
        targets: &'w RateTargets,
    ) -> Self {
        let num_comps = bands.first_band.len();
        let mut min_slope = MAX_SLOPE;
        let mut body_bytes_above = vec![0u32; num_comps * (SLOPE_BUCKETS + 1)];
        let mut passes_above = vec![0u32; SLOPE_BUCKETS + 1];
        for band in &bands.bands {
            let bytes_above = &mut body_bytes_above[band.compno * (SLOPE_BUCKETS + 1)..];
            for block in &band.coded.blocks {
                let mut previous_rate = 0u32;
                let mut effective_slope = MAX_SLOPE;
                for pass in band.coded.block_passes(block) {
                    if pass.slope == 0 {
                        continue;
                    }
                    min_slope = min_slope.min(pass.slope as u32);
                    // a threshold stops at the first hull pass at or under it
                    effective_slope = effective_slope.min(pass.slope as u32);
                    bytes_above[effective_slope as usize] += pass.rate as u32 - previous_rate;
                    passes_above[effective_slope as usize] += 1;
                    previous_rate = pass.rate as u32;
                }
            }
        }
        for slope in (0..SLOPE_BUCKETS).rev() {
            passes_above[slope] += passes_above[slope + 1];
            for compno in 0..num_comps {
                let base = compno * (SLOPE_BUCKETS + 1);
                body_bytes_above[base + slope] += body_bytes_above[base + slope + 1];
            }
        }
        Self {
            writer,
            bands,
            targets,
            num_comps,
            included: bands
                .bands
                .iter()
                .map(|band| vec![0; band.coded.blocks.len()])
                .collect(),
            scratch: Vec::new(),
            component_bytes: vec![0; num_comps],
            min_slope,
            body_bytes_above,
            passes_above,
            max_layer_length: NO_BUDGET,
            measured_headers: vec![0; num_comps],
            measured_passes: 0,
            have_estimate: false,
            last_measured_thresh: UNTESTED,
            last_measured_bytes: 0,
            last_measured_fits: false,
        }
    }

    fn allocate(
        &mut self,
        disable_rate_control: bool,
        ignore_hint: bool,
    ) -> Option<RateAllocation> {
        self.max_layer_length = if disable_rate_control {
            NO_BUDGET
        } else {
            self.targets.max_layer_length
        };
        self.measured_headers.fill(0);
        self.measured_passes = 0;
        self.have_estimate = false;
        self.last_measured_thresh = UNTESTED;
        self.last_measured_bytes = 0;
        self.last_measured_fits = false;

        let mut lower_bound = self.min_slope;
        let mut upper_bound = MAX_SLOPE;
        let hint = if ignore_hint {
            0
        } else {
            self.targets.slope_hint as u32
        };
        let use_hint = hint > 1
            && hint > self.min_slope + HINT_BRACKET_HALF_WIDTH
            && hint + HINT_BRACKET_HALF_WIDTH < upper_bound;
        if use_hint {
            let narrow_lower = hint - HINT_BRACKET_HALF_WIDTH;
            let narrow_upper = hint + HINT_BRACKET_HALF_WIDTH;
            let (mut bracket_lower, mut bracket_upper) = (narrow_lower, narrow_upper);
            let accepted_on_tolerance = self.run_bisection(&mut bracket_lower, &mut bracket_upper);
            if !accepted_on_tolerance && bracket_upper == narrow_upper {
                // nothing in the bracket fit, so the answer is above it
                self.run_bisection(&mut lower_bound, &mut upper_bound);
            } else if !accepted_on_tolerance && bracket_upper == narrow_lower {
                // everything in the bracket fit, so the answer may be below it
                upper_bound = narrow_lower;
                self.run_bisection(&mut lower_bound, &mut upper_bound);
            } else {
                upper_bound = bracket_upper;
            }
        } else {
            self.run_bisection(&mut lower_bound, &mut upper_bound);
        }

        let good_thresh = upper_bound;
        let mut took_every_pass = false;
        if good_thresh <= self.min_slope {
            self.make_layer_final();
            took_every_pass = self.simulate_within_budget();
        }
        if !took_every_pass {
            self.make_layer_feasible(good_thresh);
        }
        let final_layout_measured =
            !took_every_pass && self.last_measured_fits && self.last_measured_thresh == good_thresh;
        if !final_layout_measured && !self.simulate_within_budget() {
            return None;
        }
        Some(RateAllocation {
            included: self.included.clone(),
            slope_threshold: if took_every_pass {
                0
            } else {
                good_thresh as u16
            },
        })
    }

    fn make_layer_feasible(&mut self, thresh: u32) {
        for (band, included) in self.bands.bands.iter().zip(&mut self.included) {
            for (block, slot) in band.coded.blocks.iter().zip(included.iter_mut()) {
                let mut passes = 0;
                for (passno, pass) in band.coded.block_passes(block).iter().enumerate() {
                    if pass.slope == 0 {
                        continue;
                    }
                    if pass.slope as u32 <= thresh {
                        break;
                    }
                    passes = passno as u8 + 1;
                }
                *slot = passes;
            }
        }
    }

    fn make_layer_final(&mut self) {
        for (band, included) in self.bands.bands.iter().zip(&mut self.included) {
            for (block, slot) in band.coded.blocks.iter().zip(included.iter_mut()) {
                *slot = block.passes;
            }
        }
    }

    fn simulate(&mut self) -> u64 {
        self.component_bytes.fill(0);
        let component_bytes = &mut self.component_bytes;
        let scratch = &mut self.scratch;
        scratch.clear();
        self.writer.write_packets(
            self.bands,
            &self.included,
            scratch,
            false,
            |slot, length| {
                component_bytes[slot.comp as usize] += length as u64;
            },
        );
        component_bytes.iter().sum()
    }

    fn components_within_cap(&self) -> bool {
        let cap = self.targets.component_cap;
        cap == 0 || self.component_bytes.iter().all(|&bytes| bytes <= cap)
    }

    fn simulate_within_budget(&mut self) -> bool {
        let total = self.simulate();
        total <= self.max_layer_length as u64 && self.components_within_cap()
    }

    fn body_bytes_at(&self, compno: usize, thresh: u32) -> u64 {
        self.body_bytes_above[compno * (SLOPE_BUCKETS + 1) + thresh as usize + 1] as u64
    }

    fn estimated_headers(&self, compno: usize, thresh: u32) -> u64 {
        if self.measured_passes == 0 {
            return self.measured_headers[compno];
        }
        self.measured_headers[compno] * self.passes_above[thresh as usize + 1] as u64
            / self.measured_passes
    }

    fn fits_estimated(&self, thresh: u32) -> bool {
        let mut total = 0;
        for compno in 0..self.num_comps {
            let bytes = self.body_bytes_at(compno, thresh) + self.estimated_headers(compno, thresh);
            if self.targets.component_cap != 0 && bytes > self.targets.component_cap {
                return false;
            }
            total += bytes;
        }
        total <= self.max_layer_length as u64
    }

    fn measure(&mut self, thresh: u32) -> bool {
        self.make_layer_feasible(thresh);
        let total = self.simulate();
        self.last_measured_thresh = thresh;
        self.last_measured_bytes = total;
        self.last_measured_fits = false;
        self.measured_passes = self.passes_above[thresh as usize + 1] as u64;
        if self.targets.component_cap != 0 {
            for compno in 0..self.num_comps {
                self.measured_headers[compno] =
                    self.component_bytes[compno] - self.body_bytes_at(compno, thresh);
            }
        } else {
            let body: u64 = (0..self.num_comps)
                .map(|compno| self.body_bytes_at(compno, thresh))
                .sum();
            self.measured_headers[0] = total - body;
        }
        self.have_estimate = true;
        if total > self.max_layer_length as u64 || !self.components_within_cap() {
            return false;
        }
        self.last_measured_fits = true;
        true
    }

    fn within_tolerance(&self, achieved: u64) -> bool {
        let tolerance = self.targets.tolerance;
        tolerance > 0.0
            && self.max_layer_length != NO_BUDGET
            && achieved as f64 >= self.max_layer_length as f64 * (1.0 - tolerance)
    }

    fn measure_at(&mut self, state: &mut BisectionState, thresh: u32) -> bool {
        let fits = self.measure(thresh);
        state.measurements += 1;
        if fits {
            state.verified_upper = thresh;
            state.verified_bytes = self.last_measured_bytes;
        } else if state.measured_lower == UNTESTED || thresh > state.measured_lower {
            state.measured_lower = thresh;
        }
        fits
    }

    // the answer is a measured fit with a measured failure one below it
    fn run_bisection(&mut self, search_lower: &mut u32, search_upper: &mut u32) -> bool {
        let mut state = BisectionState {
            verified_upper: UNTESTED,
            verified_bytes: 0,
            measured_lower: UNTESTED,
            measurements: 0,
        };
        let mut upper = *search_upper;
        while state.measurements < MAX_MEASUREMENTS {
            let mut lower = if state.measured_lower == UNTESTED {
                *search_lower
            } else {
                state.measured_lower
            };
            let mut previous_thresh = 0;
            loop {
                let thresh = (lower + upper) >> 1;
                if previous_thresh != 0 && previous_thresh == thresh {
                    break;
                }
                previous_thresh = thresh;
                if self.passes_above[thresh as usize + 1] == 0 {
                    upper = thresh;
                    continue;
                }
                let fits = if self.have_estimate {
                    self.fits_estimated(thresh)
                } else {
                    self.measure_at(&mut state, thresh)
                };
                if fits {
                    upper = thresh;
                } else {
                    lower = thresh;
                }
            }
            if upper == *search_upper || self.passes_above[upper as usize + 1] == 0 {
                break;
            }
            if upper != state.verified_upper && !self.measure_at(&mut state, upper) {
                upper = if state.verified_upper == UNTESTED {
                    *search_upper
                } else {
                    state.verified_upper
                };
                continue;
            }
            if self.within_tolerance(state.verified_bytes) {
                break;
            }
            if upper <= *search_lower {
                break;
            }
            if state.measured_lower != UNTESTED && upper - 1 <= state.measured_lower {
                break;
            }
            if !self.measure_at(&mut state, upper - 1) {
                break;
            }
            upper -= 1;
        }
        if state.measured_lower != UNTESTED {
            *search_lower = state.measured_lower;
        }
        if state.verified_upper != UNTESTED {
            *search_upper = state.verified_upper;
        } else if self.passes_above[upper as usize + 1] == 0 {
            *search_upper = upper;
        }
        state.verified_upper != UNTESTED && self.within_tolerance(state.verified_bytes)
    }
}

struct BisectionState {
    verified_upper: u32,
    verified_bytes: u64,
    measured_lower: u32,
    measurements: u32,
}
