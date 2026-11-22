use std::collections::VecDeque;

use crate::ffi_dwt::{
    mercury_hwy_analysis_deinterleave_16, mercury_hwy_analysis_deinterleave_32,
    mercury_hwy_analysis_lift_97, mercury_hwy_analysis_lift_97_16,
    mercury_hwy_analysis_lift_97_16_unhalved,
};

// SAFETY for the unsafe fns: each pointer spans `samples` elements and `target`
// aliases neither neighbour
pub(crate) trait AnalysisSample: Copy + Default {
    const LIFTING_STEPS: u8;
    type LevelLifting: Copy;

    unsafe fn lift_vertical(
        level: Self::LevelLifting,
        step: u8,
        target: *mut Self,
        left: *const Self,
        right: *const Self,
        samples: usize,
    );

    unsafe fn lift_horizontal(
        row_band: VerticalBand,
        step: u8,
        target: *mut Self,
        left: *const Self,
        right: *const Self,
        samples: usize,
    );

    fn lone_vertical(level: Self::LevelLifting, sample: Self, row_band: VerticalBand) -> Self;

    fn lone_horizontal(sample: Self, column_band: VerticalBand, row_band: VerticalBand) -> Self;

    // SAFETY: `row` spans 2 * `pairs` samples, `even` and `odd` `pairs` each
    unsafe fn deinterleave(row: *const Self, even: *mut Self, odd: *mut Self, pairs: usize);
}

impl AnalysisSample for f32 {
    const LIFTING_STEPS: u8 = 4;
    type LevelLifting = ();

    unsafe fn lift_vertical(
        _: (),
        step: u8,
        target: *mut f32,
        left: *const f32,
        right: *const f32,
        samples: usize,
    ) {
        unsafe { mercury_hwy_analysis_lift_97(step as i32, target, left, right, samples as i32) }
    }

    unsafe fn lift_horizontal(
        _: VerticalBand,
        step: u8,
        target: *mut f32,
        left: *const f32,
        right: *const f32,
        samples: usize,
    ) {
        unsafe { Self::lift_vertical((), step, target, left, right, samples) }
    }

    fn lone_vertical(_: (), sample: f32, row_band: VerticalBand) -> f32 {
        match row_band {
            VerticalBand::Low => sample,
            VerticalBand::High => sample + sample,
        }
    }

    fn lone_horizontal(sample: f32, column_band: VerticalBand, _: VerticalBand) -> f32 {
        Self::lone_vertical((), sample, column_band)
    }

    unsafe fn deinterleave(row: *const f32, even: *mut f32, odd: *mut f32, pairs: usize) {
        unsafe {
            mercury_hwy_analysis_deinterleave_32(row.cast(), even.cast(), odd.cast(), pairs as i32)
        }
    }
}

impl AnalysisSample for i32 {
    const LIFTING_STEPS: u8 = 2;
    type LevelLifting = ();

    unsafe fn lift_vertical(
        _: (),
        step: u8,
        target: *mut i32,
        left: *const i32,
        right: *const i32,
        samples: usize,
    ) {
        let (target, left, right) = unsafe {
            (
                std::slice::from_raw_parts_mut(target, samples),
                std::slice::from_raw_parts(left, samples),
                std::slice::from_raw_parts(right, samples),
            )
        };
        let neighbours = left.iter().zip(right);
        if step == 0 {
            for (t, (l, r)) in target.iter_mut().zip(neighbours) {
                *t -= (l + r) >> 1;
            }
        } else {
            for (t, (l, r)) in target.iter_mut().zip(neighbours) {
                *t += (l + r + 2) >> 2;
            }
        }
    }

    unsafe fn lift_horizontal(
        _: VerticalBand,
        step: u8,
        target: *mut i32,
        left: *const i32,
        right: *const i32,
        samples: usize,
    ) {
        unsafe { Self::lift_vertical((), step, target, left, right, samples) }
    }

    fn lone_vertical(_: (), sample: i32, row_band: VerticalBand) -> i32 {
        match row_band {
            VerticalBand::Low => sample,
            VerticalBand::High => sample << 1,
        }
    }

    fn lone_horizontal(sample: i32, column_band: VerticalBand, _: VerticalBand) -> i32 {
        Self::lone_vertical((), sample, column_band)
    }

    unsafe fn deinterleave(row: *const i32, even: *mut i32, odd: *mut i32, pairs: usize) {
        unsafe {
            mercury_hwy_analysis_deinterleave_32(row.cast(), even.cast(), odd.cast(), pairs as i32)
        }
    }
}

// grok's forward 9/7 K as a float
const K: f64 = 1.230_174_1_f32 as f64;
pub(crate) const UNIT_FACTOR_Q15: i32 = 32768;

fn scale_lone_sample(sample: i16, factor: f64) -> i16 {
    // grok's scaleLoneSample
    ((sample as f64 * factor).round() as i64) as i16
}

impl AnalysisSample for i16 {
    const LIFTING_STEPS: u8 = 4;
    type LevelLifting = i32;

    unsafe fn lift_vertical(
        factor_q15: i32,
        step: u8,
        target: *mut i16,
        left: *const i16,
        right: *const i16,
        samples: usize,
    ) {
        unsafe {
            mercury_hwy_analysis_lift_97_16(
                step as i32,
                factor_q15,
                target,
                left,
                right,
                samples as i32,
            )
        }
    }

    unsafe fn lift_horizontal(
        row_band: VerticalBand,
        step: u8,
        target: *mut i16,
        left: *const i16,
        right: *const i16,
        samples: usize,
    ) {
        match row_band {
            VerticalBand::Low => unsafe {
                Self::lift_vertical(UNIT_FACTOR_Q15, step, target, left, right, samples)
            },
            VerticalBand::High => unsafe {
                mercury_hwy_analysis_lift_97_16_unhalved(
                    step as i32,
                    target,
                    left,
                    right,
                    samples as i32,
                )
            },
        }
    }

    fn lone_vertical(factor_q15: i32, sample: i16, row_band: VerticalBand) -> i16 {
        let factor = factor_q15 as f64 / UNIT_FACTOR_Q15 as f64;
        let lone_factor = match row_band {
            VerticalBand::Low => factor * K,
            VerticalBand::High => factor / K,
        };
        scale_lone_sample(sample, lone_factor)
    }

    fn lone_horizontal(sample: i16, column_band: VerticalBand, row_band: VerticalBand) -> i16 {
        let lone_factor = match (column_band, row_band) {
            (VerticalBand::Low, _) => K,
            (VerticalBand::High, VerticalBand::Low) => 1.0 / K,
            (VerticalBand::High, VerticalBand::High) => 2.0 / K,
        };
        scale_lone_sample(sample, lone_factor)
    }

    unsafe fn deinterleave(row: *const i16, even: *mut i16, odd: *mut i16, pairs: usize) {
        unsafe {
            mercury_hwy_analysis_deinterleave_16(row.cast(), even.cast(), odd.cast(), pairs as i32)
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum VerticalBand {
    Low,
    High,
}

// split of `length` samples starting at an absolute coordinate of parity `parity`
#[derive(Debug, Clone, Copy)]
pub(crate) struct BandSplit {
    pub parity: usize,
    pub low: usize,
    pub high: usize,
}

impl BandSplit {
    pub fn warp(start: u32, length: usize) -> Self {
        let parity = (start & 1) as usize;
        let low = (length + 1 - parity) >> 1;
        Self {
            parity,
            low,
            high: length - low,
        }
    }

    fn band_at(&self, position: usize) -> VerticalBand {
        if (position + self.parity) & 1 == 0 {
            VerticalBand::Low
        } else {
            VerticalBand::High
        }
    }
}

// lifting state of one row: the value equals the row after global steps 0..state
const FINAL_STATE: u8 = u8::MAX;

// a row lifts once both neighbours have reached the previous step
pub(crate) struct LevelAnalysis<T: AnalysisSample> {
    width: usize,
    height: usize,
    columns: BandSplit,
    rows_split: BandSplit,
    rows: VecDeque<Vec<T>>,
    states: VecDeque<u8>,
    first_held_row: usize,
    received: usize,
    emitted: usize,
    next_target: [usize; 4],
    spare_rows: Vec<Vec<T>>,
    lifting: T::LevelLifting,
}

impl<T: AnalysisSample> LevelAnalysis<T> {
    pub fn warp(x0: u32, y0: u32, width: usize, height: usize, lifting: T::LevelLifting) -> Self {
        let rows_split = BandSplit::warp(y0, height);
        let mut next_target = [0usize; 4];
        for (step, target) in next_target.iter_mut().enumerate() {
            let target_band = if step % 2 == 0 {
                VerticalBand::High
            } else {
                VerticalBand::Low
            };
            *target = (0..2)
                .find(|&position| rows_split.band_at(position) == target_band)
                .unwrap_or(0);
        }
        Self {
            width,
            height,
            columns: BandSplit::warp(x0, width),
            rows_split,
            rows: VecDeque::new(),
            states: VecDeque::new(),
            first_held_row: 0,
            received: 0,
            emitted: 0,
            next_target,
            spare_rows: Vec::new(),
            lifting,
        }
    }

    pub fn columns(&self) -> BandSplit {
        self.columns
    }

    pub fn rows_split(&self) -> BandSplit {
        self.rows_split
    }

    pub fn wants_row(&self) -> bool {
        self.received < self.height && self.ready().is_none()
    }

    pub fn feed(&mut self, fill: impl FnOnce(&mut [T])) {
        assert!(
            self.wants_row(),
            "fed a row while another is waiting to be emitted"
        );
        let mut row = self.spare_rows.pop().unwrap_or_default();
        row.resize(self.width, T::default());
        fill(&mut row);
        let position = self.received;
        let band = self.rows_split.band_at(position);
        let state = if self.height == 1 {
            for sample in row.iter_mut() {
                *sample = T::lone_vertical(self.lifting, *sample, band);
            }
            FINAL_STATE
        } else if band == VerticalBand::Low {
            1
        } else {
            0
        };
        self.rows.push_back(row);
        self.states.push_back(state);
        self.received += 1;
        self.advance();
    }

    pub fn ready(&self) -> Option<VerticalBand> {
        if self.emitted >= self.received {
            return None;
        }
        let state = self.states[self.emitted - self.first_held_row];
        (state >= T::LIFTING_STEPS).then(|| self.rows_split.band_at(self.emitted))
    }

    // `low` and `high` take the columns() split of the next ready row
    pub fn emit(&mut self, low: &mut [T], high: &mut [T]) {
        assert!(self.ready().is_some(), "no finished row to emit");
        let row = &self.rows[self.emitted - self.first_held_row];
        split_row(
            row,
            self.columns,
            self.rows_split.band_at(self.emitted),
            low,
            high,
        );
        self.emitted += 1;
        let keep_from = if self.emitted == self.height {
            self.height
        } else {
            self.emitted.saturating_sub(1)
        };
        while self.first_held_row < keep_from {
            let row = self.rows.pop_front().expect("held row");
            self.states.pop_front();
            self.spare_rows.push(row);
            self.first_held_row += 1;
        }
    }

    fn state(&self, position: usize) -> u8 {
        self.states[position - self.first_held_row]
    }

    fn advance(&mut self) {
        if self.height < 2 {
            return;
        }
        loop {
            let mut progressed = false;
            for step in 0..T::LIFTING_STEPS {
                while self.try_lift(step) {
                    progressed = true;
                }
            }
            if !progressed {
                return;
            }
        }
    }

    fn try_lift(&mut self, step: u8) -> bool {
        let target = self.next_target[step as usize];
        if target >= self.received || self.state(target) != step {
            return false;
        }
        let left = if target == 0 { 1 } else { target - 1 };
        let right = if target + 1 >= self.height {
            target - 1
        } else {
            target + 1
        };
        let neighbours_ready = [left, right]
            .iter()
            .all(|&n| n < self.received && self.state(n) >= step);
        if !neighbours_ready {
            return false;
        }
        let base = self.first_held_row;
        let target_row = self.rows[target - base].as_mut_ptr();
        let left_row = self.rows[left - base].as_ptr();
        let right_row = self.rows[right - base].as_ptr();
        unsafe {
            T::lift_vertical(
                self.lifting,
                step,
                target_row,
                left_row,
                right_row,
                self.width,
            )
        };
        let next_state = step + 2;
        self.states[target - base] = if next_state >= T::LIFTING_STEPS {
            FINAL_STATE
        } else {
            next_state
        };
        self.next_target[step as usize] += 2;
        true
    }
}

fn split_row<T: AnalysisSample>(
    row: &[T],
    columns: BandSplit,
    row_band: VerticalBand,
    low: &mut [T],
    high: &mut [T],
) {
    let width = row.len();
    if width == 0 {
        return;
    }
    if width == 1 {
        if columns.high == 1 {
            high[0] = T::lone_horizontal(row[0], VerticalBand::High, row_band);
        } else {
            low[0] = T::lone_horizontal(row[0], VerticalBand::Low, row_band);
        }
        return;
    }
    let parity = columns.parity;
    if parity == 0 {
        deinterleave(row, low, high);
    } else {
        deinterleave(row, high, low);
    }
    for step in 0..T::LIFTING_STEPS {
        if step % 2 == 0 {
            lift_band(step, row_band, high, low, parity == 1);
        } else {
            lift_band(step, row_band, low, high, parity == 0);
        }
    }
}

fn deinterleave<T: AnalysisSample>(row: &[T], even: &mut [T], odd: &mut [T]) {
    let pairs = row.len() / 2;
    assert!(even.len() >= row.len() - pairs && odd.len() >= pairs);
    unsafe { T::deinterleave(row.as_ptr(), even.as_mut_ptr(), odd.as_mut_ptr(), pairs) };
    if row.len() % 2 == 1 {
        even[pairs] = row[2 * pairs];
    }
}

// target[j] lifts from neighbours[j - lead] and the next one, repeating the edge sample past either end
fn lift_band<T: AnalysisSample>(
    step: u8,
    row_band: VerticalBand,
    target: &mut [T],
    neighbours: &[T],
    target_leads: bool,
) {
    let lead = target_leads as usize;
    let last = neighbours.len() - 1;
    let interior_end = (last + lead).min(target.len());
    let interior_start = lead.min(interior_end);
    let target_samples = target.as_mut_ptr();
    let edge = |index: usize| &neighbours[index] as *const T;
    unsafe {
        if lead == 1 {
            T::lift_horizontal(row_band, step, target_samples, edge(0), edge(0), 1);
        }
        let left = neighbours.as_ptr().add(interior_start - lead);
        T::lift_horizontal(
            row_band,
            step,
            target_samples.add(interior_start),
            left,
            left.add(1),
            interior_end - interior_start,
        );
        for j in interior_end..target.len() {
            T::lift_horizontal(
                row_band,
                step,
                target_samples.add(j),
                edge(last),
                edge(last),
                1,
            );
        }
    }
}
