use super::tile_packets::{TileBands, TilePacketWriter, plt_markers, run_tasks};

const SLOPE_BUCKETS: usize = u16::MAX as usize + 1;
const MAX_SLOPE: u32 = u16::MAX as u32;
// two octaves either side of the hint
const HINT_BRACKET_HALF_WIDTH: u32 = 512;
const MAX_MEASUREMENTS: u32 = 12;
// classic's kMaxLayerMeasurements
const IMAGE_MAX_MEASUREMENTS: u32 = 16;
const UNTESTED: u32 = u32::MAX;
const NO_BUDGET: u64 = u64::MAX;

pub(crate) struct RateTargets {
    // the sum over the tiles with several tiles
    pub max_layer_length: u64,
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
    threads: usize,
) -> Option<RateAllocation> {
    if targets.max_layer_length == 0 {
        let mut included = no_passes(bands);
        every_pass(bands, &mut included);
        return Some(RateAllocation {
            included,
            slope_threshold: 0,
        });
    }
    let mut search = Search::warp(writer, bands, targets, threads);
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

fn for_each_hull_pass(bands: &TileBands, mut visit: impl FnMut(usize, u32, u32)) {
    for band in &bands.bands {
        for block in &band.coded.blocks {
            let mut previous_rate = 0u32;
            let mut effective_slope = MAX_SLOPE;
            for pass in band.coded.block_passes(block) {
                if pass.slope == 0 {
                    continue;
                }
                // a threshold stops at the first hull pass at or under it
                effective_slope = effective_slope.min(pass.slope as u32);
                visit(
                    band.compno,
                    effective_slope,
                    pass.rate as u32 - previous_rate,
                );
                previous_rate = pass.rate as u32;
            }
        }
    }
}

fn feasible_passes(bands: &TileBands, thresh: u32, included: &mut [Vec<u8>], threads: usize) {
    let tasks: Vec<_> = bands.bands.iter().zip(included).collect();
    run_tasks(tasks, threads, |(band, included)| {
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
    });
}

fn every_pass(bands: &TileBands, included: &mut [Vec<u8>]) {
    for (band, included) in bands.bands.iter().zip(included) {
        for (block, slot) in band.coded.blocks.iter().zip(included.iter_mut()) {
            *slot = block.passes;
        }
    }
}

fn no_passes(bands: &TileBands) -> Vec<Vec<u8>> {
    bands
        .bands
        .iter()
        .map(|band| vec![0; band.coded.blocks.len()])
        .collect()
}

pub(crate) struct ImageRateAllocation {
    // [tile][band] passes included per block
    pub included: Vec<Vec<Vec<u8>>>,
    pub slope_threshold: u16,
}

pub(crate) fn allocate_image_rate(
    writers: &mut [TilePacketWriter],
    tiles: &[TileBands],
    targets: &RateTargets,
    write_plt: bool,
    threads: usize,
) -> ImageRateAllocation {
    let mut included: Vec<Vec<Vec<u8>>> = tiles.iter().map(no_passes).collect();
    let mut simulate = |included: &mut Vec<Vec<Vec<u8>>>,
                        layout: &(dyn Fn(&TileBands, &mut [Vec<u8>]) + Sync)|
     -> u64 {
        let tasks: Vec<_> = writers
            .iter_mut()
            .zip(tiles)
            .zip(included.iter_mut())
            .collect();
        run_tasks(tasks, threads, |((writer, bands), included)| {
            layout(bands, included);
            let lengths = writer.packet_lengths(bands, included);
            let packets: u64 = lengths.iter().map(|&length| length as u64).sum();
            // classic counts PLT bytes only in its image-wide rate measurements
            let plt = if write_plt {
                plt_markers(lengths.iter().copied()).len() as u64
            } else {
                0
            };
            packets + plt
        })
        .into_iter()
        .sum()
    };
    let every_pass_everywhere = |mut included: Vec<Vec<Vec<u8>>>| {
        for (bands, included) in tiles.iter().zip(included.iter_mut()) {
            every_pass(bands, included);
        }
        ImageRateAllocation {
            included,
            slope_threshold: 0,
        }
    };
    let budget = targets.max_layer_length;
    if budget == 0 {
        return every_pass_everywhere(included);
    }

    let mut min_slope = MAX_SLOPE;
    let mut bytes_above = vec![0u64; SLOPE_BUCKETS + 1];
    let mut passes_above = vec![0u64; SLOPE_BUCKETS + 1];
    for bands in tiles {
        for_each_hull_pass(bands, |_, slope, bytes| {
            min_slope = min_slope.min(slope);
            bytes_above[slope as usize] += bytes as u64;
            passes_above[slope as usize] += 1;
        });
    }
    // bytes_above[t] counts what a threshold of t - 1 includes
    for slope in (0..SLOPE_BUCKETS).rev() {
        bytes_above[slope] += bytes_above[slope + 1];
        passes_above[slope] += passes_above[slope + 1];
    }

    // packet headers grow with the number of included passes
    let mut measured_headers = 0u64;
    let mut measured_passes = 0u64;
    // every pass at or under the threshold is left out, so one below the minimum keeps every
    // hull pass
    let every_hull_pass = min_slope.saturating_sub(1);
    let mut failing = every_hull_pass as i64 - 1;
    let mut fitting = MAX_SLOPE;
    let mut have_fit = false;
    for _ in 0..IMAGE_MAX_MEASUREMENTS {
        let lower = (failing + 1) as u32;
        let upper = if have_fit { fitting } else { MAX_SLOPE };
        if have_fit && lower >= upper {
            break;
        }
        let estimate = |thresh: u32| {
            let headers = (measured_headers * passes_above[thresh as usize + 1])
                .checked_div(measured_passes)
                .unwrap_or(measured_headers);
            bytes_above[thresh as usize + 1] + headers
        };
        // smallest threshold the estimate says fits
        let (mut low, mut high) = (lower, upper);
        while low < high {
            let mid = (low + high) / 2;
            if estimate(mid) <= budget {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        let thresh = low;
        if have_fit && thresh == fitting {
            break;
        }
        let total = simulate(&mut included, &|bands, included| {
            feasible_passes(bands, thresh, included, 1)
        });
        measured_passes = passes_above[thresh as usize + 1];
        measured_headers = total - bytes_above[thresh as usize + 1];
        if total <= budget {
            fitting = thresh;
            have_fit = true;
            if targets.tolerance > 0.0 && total as f64 >= budget as f64 * (1.0 - targets.tolerance)
            {
                break;
            }
        } else {
            failing = thresh as i64;
            if thresh == MAX_SLOPE {
                break;
            }
        }
    }
    if !have_fit {
        return every_pass_everywhere(included);
    }
    // passes after a block's last hull pass need every pass taken to be included
    let takes_every_pass = fitting == every_hull_pass
        && simulate(&mut included, &|bands, included| {
            every_pass(bands, included)
        }) <= budget;
    if !takes_every_pass {
        let tasks: Vec<_> = tiles.iter().zip(included.iter_mut()).collect();
        run_tasks(tasks, threads, |(bands, included)| {
            feasible_passes(bands, fitting, included, 1)
        });
    }
    ImageRateAllocation {
        included,
        slope_threshold: fitting as u16,
    }
}

struct Search<'w, 'a> {
    writer: &'w mut TilePacketWriter,
    bands: &'w TileBands<'a>,
    targets: &'w RateTargets,
    threads: usize,
    num_comps: usize,
    included: Vec<Vec<u8>>,
    component_bytes: Vec<u64>,
    min_slope: u32,
    // [comp][slope + 1]: body bytes of the passes a threshold at slope keeps
    body_bytes_above: Vec<u32>,
    passes_above: Vec<u32>,
    max_layer_length: u64,
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
        threads: usize,
    ) -> Self {
        let num_comps = bands.first_band.len();
        let mut min_slope = MAX_SLOPE;
        let mut body_bytes_above = vec![0u32; num_comps * (SLOPE_BUCKETS + 1)];
        let mut passes_above = vec![0u32; SLOPE_BUCKETS + 1];
        for_each_hull_pass(bands, |compno, slope, bytes| {
            min_slope = min_slope.min(slope);
            body_bytes_above[compno * (SLOPE_BUCKETS + 1) + slope as usize] += bytes;
            passes_above[slope as usize] += 1;
        });
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
            threads,
            num_comps,
            included: no_passes(bands),
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
        feasible_passes(self.bands, thresh, &mut self.included, self.threads);
    }

    fn make_layer_final(&mut self) {
        every_pass(self.bands, &mut self.included);
    }

    fn simulate(&mut self) -> u64 {
        self.component_bytes.fill(0);
        let lengths = self.writer.packet_lengths(self.bands, &self.included);
        for (slot, length) in self.writer.order().iter().zip(lengths) {
            self.component_bytes[slot.comp as usize] += length as u64;
        }
        self.component_bytes.iter().sum()
    }

    fn components_within_cap(&self) -> bool {
        let cap = self.targets.component_cap;
        cap == 0 || self.component_bytes.iter().all(|&bytes| bytes <= cap)
    }

    fn simulate_within_budget(&mut self) -> bool {
        let total = self.simulate();
        total <= self.max_layer_length && self.components_within_cap()
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
        total <= self.max_layer_length
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
        if total > self.max_layer_length || !self.components_within_cap() {
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
