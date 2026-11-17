//! The decoder's only I/O seam: positioned reads from the codestream.
//!
//! Everything downstream of [`draft`](crate::decode::plan::draft)
//! pulls bytes exclusively through this trait — the header window, the SOT
//! walk, every worker's scattered code-block reads — so implementations must
//! be callable concurrently from many threads.
//!
//! The trait exists (rather than `Arc<File>`) so an embedding codec can supply
//! an fd or a `read_at(ctx, buf, off, len)` callback for non-file streams. It
//! is deliberately pread-shaped, not mmap-shaped: the decoder's O(window) RSS
//! bound depends on block bytes being copied into small scratch buffers and
//! released, which mapping the whole input would defeat.

use std::fs::File;
use std::io;
use std::ops::Range;
use std::sync::Arc;

/// Positioned-read source of the codestream.
pub trait ReadAt: Send + Sync {
    /// Draw all of `buf` from absolute byte offset `off`.
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()>;

    /// Total extent of the source in bytes.
    fn extent(&self) -> io::Result<u64>;

    fn fetched_run_end(&self, _off: u64) -> Option<u64> {
        None
    }
}

impl<T: ReadAt + ?Sized> ReadAt for &T {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        (**self).draw_at(buf, off)
    }

    fn extent(&self) -> io::Result<u64> {
        (**self).extent()
    }

    fn fetched_run_end(&self, off: u64) -> Option<u64> {
        (**self).fetched_run_end(off)
    }
}

impl<T: ReadAt + ?Sized> ReadAt for Arc<T> {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        (**self).draw_at(buf, off)
    }

    fn extent(&self) -> io::Result<u64> {
        (**self).extent()
    }

    fn fetched_run_end(&self, off: u64) -> Option<u64> {
        (**self).fetched_run_end(off)
    }
}

/// In-memory codestream (tests, callers that already hold the bytes).
impl ReadAt for Vec<u8> {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        let off = off as usize;
        let end = off.checked_add(buf.len()).filter(|&e| e <= self.len());
        match end {
            Some(end) => {
                buf.copy_from_slice(&self[off..end]);
                Ok(())
            }
            None => Err(io::ErrorKind::UnexpectedEof.into()),
        }
    }

    fn extent(&self) -> io::Result<u64> {
        Ok(Vec::len(self) as u64)
    }
}

// a 64 KB gap was fastest for cold HiRISE regions read through callbacks, against 256 KB and 1 MB
pub const FETCH_GAP_BYTES: u64 = 64 << 10;

pub type FetchedRun = (u64, Vec<u8>);

pub fn merge_ranges(mut ranges: Vec<Range<u64>>, max_gap: u64) -> Vec<Range<u64>> {
    ranges.retain(|range| !range.is_empty());
    ranges.sort_unstable_by_key(|range| range.start);
    let mut runs: Vec<Range<u64>> = Vec::new();
    for range in ranges {
        match runs.last_mut() {
            Some(run) if range.start <= run.end + max_gap => run.end = run.end.max(range.end),
            _ => runs.push(range),
        }
    }
    runs
}

pub fn fetch_runs(source: &dyn ReadAt, runs: &[Range<u64>]) -> io::Result<Vec<FetchedRun>> {
    let mut fetched = Vec::with_capacity(runs.len());
    for run in runs {
        let mut bytes = vec![0u8; (run.end - run.start) as usize];
        source.draw_at(&mut bytes, run.start)?;
        fetched.push((run.start, bytes));
    }
    Ok(fetched)
}

pub struct FetchedRuns<S> {
    runs: Vec<FetchedRun>,
    source: S,
}

impl<S: ReadAt> FetchedRuns<S> {
    pub fn over(source: S, mut runs: Vec<FetchedRun>) -> Self {
        runs.sort_unstable_by_key(|(start, _)| *start);
        Self { runs, source }
    }

    pub fn into_runs(self) -> Vec<FetchedRun> {
        self.runs
    }

    fn run_containing(&self, off: u64) -> Option<&FetchedRun> {
        let after = self.runs.partition_point(|(start, _)| *start <= off);
        let run = &self.runs[after.checked_sub(1)?];
        (off < run.0 + run.1.len() as u64).then_some(run)
    }
}

impl<S: ReadAt> ReadAt for FetchedRuns<S> {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        if let Some((start, bytes)) = self.run_containing(off) {
            let from = (off - start) as usize;
            if let Some(held) = bytes.get(from..from + buf.len()) {
                buf.copy_from_slice(held);
                return Ok(());
            }
        }
        self.source.draw_at(buf, off)
    }

    fn extent(&self) -> io::Result<u64> {
        self.source.extent()
    }

    fn fetched_run_end(&self, off: u64) -> Option<u64> {
        self.run_containing(off)
            .map(|(start, bytes)| start + bytes.len() as u64)
    }
}

impl ReadAt for File {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        pread_exact(self, buf, off)
    }

    fn extent(&self) -> io::Result<u64> {
        Ok(self.metadata()?.len())
    }
}

/// Positioned read that fills `buf` from `off` without touching the file
/// cursor: `pread` on unix, `ReadFile` with an explicit offset on Windows.
#[cfg(unix)]
fn pread_exact(file: &File, buf: &mut [u8], off: u64) -> io::Result<()> {
    std::os::unix::fs::FileExt::read_exact_at(file, buf, off)
}

#[cfg(windows)]
fn pread_exact(file: &File, mut buf: &mut [u8], mut off: u64) -> io::Result<()> {
    use std::os::windows::fs::FileExt;
    while !buf.is_empty() {
        match file.seek_read(buf, off) {
            Ok(0) => return Err(io::ErrorKind::UnexpectedEof.into()),
            Ok(n) => {
                buf = &mut buf[n..];
                off += n as u64;
            }
            Err(ref e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(())
}
