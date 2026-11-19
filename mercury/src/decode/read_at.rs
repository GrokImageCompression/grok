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
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

/// Positioned-read source of the codestream.
pub trait ReadAt: Send + Sync {
    /// Draw all of `buf` from absolute byte offset `off`.
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()>;

    /// Total extent of the source in bytes.
    fn extent(&self) -> io::Result<u64>;

    fn fetched_run_end(&self, _off: u64) -> Option<u64> {
        None
    }

    fn draw_at_if_cached(&self, _buf: &mut [u8], _off: u64) -> io::Result<bool> {
        Ok(false)
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

    fn draw_at_if_cached(&self, buf: &mut [u8], off: u64) -> io::Result<bool> {
        (**self).draw_at_if_cached(buf, off)
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

    fn draw_at_if_cached(&self, buf: &mut [u8], off: u64) -> io::Result<bool> {
        (**self).draw_at_if_cached(buf, off)
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

const FETCH_CHUNK_BYTES: usize = 1 << 20;

pub fn fetch_runs(
    source: &dyn ReadAt,
    runs: &[Range<u64>],
    threads: usize,
) -> io::Result<Vec<FetchedRun>> {
    let mut fetched: Vec<FetchedRun> = runs
        .iter()
        .map(|run| (run.start, vec![0u8; (run.end - run.start) as usize]))
        .collect();
    if threads <= 1 {
        for (start, bytes) in &mut fetched {
            source.draw_at(bytes, *start)?;
        }
        return Ok(fetched);
    }
    let chunks: Vec<(u64, &mut [u8])> = fetched
        .iter_mut()
        .flat_map(|(start, bytes)| {
            let start = *start;
            bytes
                .chunks_mut(FETCH_CHUNK_BYTES)
                .enumerate()
                .map(move |(i, chunk)| (start + (i * FETCH_CHUNK_BYTES) as u64, chunk))
        })
        .collect();
    let threads = threads.min(chunks.len());
    let chunks = std::sync::Mutex::new(chunks.into_iter());
    std::thread::scope(|scope| {
        let workers: Vec<_> = (0..threads)
            .map(|_| {
                scope.spawn(|| {
                    loop {
                        let next = chunks.lock().expect("fetch chunk queue poisoned").next();
                        let Some((off, chunk)) = next else {
                            return Ok(());
                        };
                        source.draw_at(chunk, off)?;
                    }
                })
            })
            .collect();
        workers
            .into_iter()
            .try_for_each(|worker| -> io::Result<()> {
                worker.join().expect("fetch thread panicked")
            })
    })?;
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

struct FilledChunk {
    bytes: Option<Box<[u8]>>,
    owed: u64,
    sealed: bool,
}

struct FilledRun {
    start: u64,
    len: u64,
    chunks: Box<[Mutex<FilledChunk>]>,
    filled: AtomicUsize,
}

impl FilledRun {
    fn filled_end(&self) -> u64 {
        let filled = self.filled.load(Ordering::Acquire);
        (self.start + (filled * FETCH_CHUNK_BYTES) as u64).min(self.start + self.len)
    }

    fn chunk_end(&self, chunk: usize) -> u64 {
        (self.start + ((chunk + 1) * FETCH_CHUNK_BYTES) as u64).min(self.start + self.len)
    }
}

// HiRISE full decodes took the same time holding 8, 16, 32 or 64 MB, and 50 MB less peak RSS at 8
const MAX_HELD_BYTES: u64 = 8 << 20;
// a decode whose published rows wait on unpublished ones frees nothing until the feed moves on
const FEED_STALL: Duration = Duration::from_millis(50);

#[derive(Default)]
struct HeldBytes {
    bytes: u64,
    spare: Vec<Box<[u8]>>,
}

pub struct FilledRuns<S> {
    runs: Vec<FilledRun>,
    source: S,
    held: Mutex<HeldBytes>,
    released: Condvar,
    unread_published_bytes: AtomicU64,
    closed: AtomicBool,
}

#[derive(Default)]
pub struct FillCursor {
    run: usize,
    chunk: usize,
}

impl<S: ReadAt> FilledRuns<S> {
    pub fn over(source: S, runs: &[Range<u64>]) -> Self {
        let runs = runs
            .iter()
            .map(|run| {
                let len = run.end - run.start;
                FilledRun {
                    start: run.start,
                    len,
                    chunks: (0..len.div_ceil(FETCH_CHUNK_BYTES as u64))
                        .map(|_| {
                            Mutex::new(FilledChunk {
                                bytes: None,
                                owed: 0,
                                sealed: false,
                            })
                        })
                        .collect(),
                    filled: AtomicUsize::new(0),
                }
            })
            .collect();
        Self {
            runs,
            source,
            held: Mutex::new(HeldBytes::default()),
            released: Condvar::new(),
            unread_published_bytes: AtomicU64::new(0),
            closed: AtomicBool::new(false),
        }
    }

    pub fn fill_through(
        &self,
        cursor: &mut FillCursor,
        end: u64,
        graph_failed: &dyn Fn() -> bool,
    ) -> io::Result<()> {
        while let Some(run) = self.runs.get(cursor.run) {
            if cursor.chunk == run.chunks.len() {
                cursor.run += 1;
                cursor.chunk = 0;
                continue;
            }
            let chunk_start = run.start + (cursor.chunk * FETCH_CHUNK_BYTES) as u64;
            if chunk_start >= end {
                return Ok(());
            }
            let chunk_len = FETCH_CHUNK_BYTES.min((run.start + run.len - chunk_start) as usize);
            let mut bytes = self.wait_for_room(chunk_len, graph_failed)?;
            self.source.draw_at(&mut bytes, chunk_start)?;
            run.chunks[cursor.chunk].lock().unwrap().bytes = Some(bytes);
            cursor.chunk += 1;
            run.filled.store(cursor.chunk, Ordering::Release);
        }
        Ok(())
    }

    fn wait_for_room(
        &self,
        chunk_len: usize,
        graph_failed: &dyn Fn() -> bool,
    ) -> io::Result<Box<[u8]>> {
        let mut held = self.held.lock().unwrap();
        while held.bytes >= MAX_HELD_BYTES
            && self.unread_published_bytes.load(Ordering::Acquire) > 0
        {
            if self.closed.load(Ordering::Acquire) || graph_failed() {
                return Err(io::Error::other("the decode ended before the packet feed"));
            }
            let (next, wait) = self.released.wait_timeout(held, FEED_STALL).unwrap();
            held = next;
            if wait.timed_out() {
                break;
            }
        }
        held.bytes += chunk_len as u64;
        let spare = (chunk_len == FETCH_CHUNK_BYTES)
            .then(|| held.spare.pop())
            .flatten();
        Ok(spare.unwrap_or_else(|| vec![0u8; chunk_len].into_boxed_slice()))
    }

    pub fn owe(&self, off: u64, len: u64) {
        if len == 0 {
            return;
        }
        self.visit_chunks(off, len, |chunk, take| chunk.owed += take);
    }

    pub fn seal_through(&self, cursor: &mut FillCursor, end: u64) {
        while let Some(run) = self.runs.get(cursor.run) {
            if cursor.chunk == run.chunks.len() {
                cursor.run += 1;
                cursor.chunk = 0;
                continue;
            }
            if run.chunk_end(cursor.chunk) > end {
                return;
            }
            let mut chunk = run.chunks[cursor.chunk].lock().unwrap();
            chunk.sealed = true;
            self.release_if_paid(&mut chunk);
            cursor.chunk += 1;
        }
    }

    pub fn publish(&self, bytes: u64) {
        self.unread_published_bytes
            .fetch_add(bytes, Ordering::AcqRel);
    }

    fn consume_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        self.copy_out(buf, off, true)?;
        self.unread_published_bytes
            .fetch_sub(buf.len() as u64, Ordering::AcqRel);
        Ok(())
    }

    fn close(&self) {
        self.closed.store(true, Ordering::Release);
        let _held = self.held.lock().unwrap();
        self.released.notify_all();
    }

    fn release_if_paid(&self, chunk: &mut FilledChunk) {
        if !chunk.sealed || chunk.owed != 0 {
            return;
        }
        let Some(bytes) = chunk.bytes.take() else {
            return;
        };
        let mut held = self.held.lock().unwrap();
        held.bytes -= bytes.len() as u64;
        if bytes.len() == FETCH_CHUNK_BYTES {
            held.spare.push(bytes);
        }
        self.released.notify_all();
    }

    fn visit_chunks(&self, off: u64, len: u64, mut visit: impl FnMut(&mut FilledChunk, u64)) {
        let run = self
            .run_containing(off)
            .expect("owed bytes lie in a fed run");
        let mut at = off - run.start;
        let end = at + len;
        while at < end {
            let index = (at / FETCH_CHUNK_BYTES as u64) as usize;
            let take = ((index as u64 + 1) * FETCH_CHUNK_BYTES as u64).min(end) - at;
            visit(&mut run.chunks[index].lock().unwrap(), take);
            at += take;
        }
    }

    fn copy_out(&self, buf: &mut [u8], off: u64, pay: bool) -> io::Result<()> {
        let Some(run) = self
            .run_containing(off)
            .filter(|run| off + buf.len() as u64 <= run.start + run.len)
        else {
            return self.source.draw_at(buf, off);
        };
        let mut copied = 0usize;
        while copied < buf.len() {
            let at = off + copied as u64 - run.start;
            let mut chunk = run.chunks[(at / FETCH_CHUNK_BYTES as u64) as usize]
                .lock()
                .unwrap();
            let Some(bytes) = &chunk.bytes else {
                return Err(io::Error::other(
                    "read of run bytes the feed has not filled or has released",
                ));
            };
            let from = (at % FETCH_CHUNK_BYTES as u64) as usize;
            let take = (bytes.len() - from).min(buf.len() - copied);
            buf[copied..copied + take].copy_from_slice(&bytes[from..from + take]);
            copied += take;
            if pay {
                chunk.owed -= take as u64;
                self.release_if_paid(&mut chunk);
            }
        }
        Ok(())
    }

    fn run_containing(&self, off: u64) -> Option<&FilledRun> {
        let after = self.runs.partition_point(|run| run.start <= off);
        let run = &self.runs[after.checked_sub(1)?];
        (off < run.start + run.len).then_some(run)
    }
}

impl<S: ReadAt> ReadAt for FilledRuns<S> {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        self.copy_out(buf, off, false)
    }

    fn extent(&self) -> io::Result<u64> {
        self.source.extent()
    }

    fn fetched_run_end(&self, off: u64) -> Option<u64> {
        self.run_containing(off).map(FilledRun::filled_end)
    }
}

pub struct ConsumedRuns<S: ReadAt>(pub Arc<FilledRuns<S>>);

impl<S: ReadAt> ReadAt for ConsumedRuns<S> {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        self.0.consume_at(buf, off)
    }

    fn extent(&self) -> io::Result<u64> {
        self.0.extent()
    }
}

impl<S: ReadAt> Drop for ConsumedRuns<S> {
    fn drop(&mut self) {
        self.0.close();
    }
}

impl ReadAt for File {
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()> {
        pread_exact(self, buf, off)
    }

    fn extent(&self) -> io::Result<u64> {
        Ok(self.metadata()?.len())
    }

    #[cfg(all(
        target_os = "linux",
        any(target_arch = "x86_64", target_arch = "aarch64")
    ))]
    fn draw_at_if_cached(&self, buf: &mut [u8], off: u64) -> io::Result<bool> {
        use std::os::fd::AsRawFd;
        #[repr(C)]
        struct IoVec {
            base: *mut u8,
            len: usize,
        }
        unsafe extern "C" {
            fn preadv2(fd: i32, iov: *const IoVec, iovcnt: i32, offset: i64, flags: i32) -> isize;
        }
        // linux RWF_NOWAIT fails a read with EAGAIN when the bytes are not in the page cache
        const RWF_NOWAIT: i32 = 0x8;
        let iov = IoVec {
            base: buf.as_mut_ptr(),
            len: buf.len(),
        };
        let read = unsafe { preadv2(self.as_raw_fd(), &iov, 1, off as i64, RWF_NOWAIT) };
        if read >= 0 {
            return Ok(read as usize == buf.len());
        }
        // linux errno values on x86_64 and aarch64
        const EAGAIN: i32 = 11;
        const ENOSYS: i32 = 38;
        // rust 1.87 std maps EOPNOTSUPP to ErrorKind::Uncategorized
        const EOPNOTSUPP: i32 = 95;
        let error = io::Error::last_os_error();
        match error.raw_os_error() {
            Some(EAGAIN | ENOSYS | EOPNOTSUPP) => Ok(false),
            _ => Err(error),
        }
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parallel_fetch_splits_runs_into_chunks_and_matches_the_source() {
        let source: Vec<u8> = (0..3 * FETCH_CHUNK_BYTES + 777)
            .map(|i| (i * 31 % 251) as u8)
            .collect();
        let runs = vec![
            5..FETCH_CHUNK_BYTES as u64 * 2 + 9,
            FETCH_CHUNK_BYTES as u64 * 2 + 100..FETCH_CHUNK_BYTES as u64 * 2 + 200,
            FETCH_CHUNK_BYTES as u64 * 2 + 300..source.len() as u64,
        ];
        let fetched = fetch_runs(&source, &runs, 4).expect("in-memory reads succeed");
        assert_eq!(fetched.len(), runs.len());
        for ((start, bytes), run) in fetched.iter().zip(&runs) {
            assert_eq!(*start, run.start);
            assert_eq!(bytes[..], source[run.start as usize..run.end as usize]);
        }
    }

    #[cfg(all(
        target_os = "linux",
        any(target_arch = "x86_64", target_arch = "aarch64")
    ))]
    #[test]
    fn file_draws_freshly_written_bytes_from_the_page_cache() {
        // tmpfs, the usual temp_dir, rejects RWF_NOWAIT with EOPNOTSUPP
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join(format!("mercury_cached_{}", std::process::id()));
        let bytes: Vec<u8> = (0..8192).map(|i| (i % 251) as u8).collect();
        std::fs::write(&path, &bytes).expect("temp file writes");
        let file = File::open(&path).expect("temp file opens");
        let mut drawn = [0u8; 12];
        let cached = file.draw_at_if_cached(&mut drawn, 5000);
        std::fs::remove_file(&path).expect("temp file removes");
        assert!(cached.expect("cached read succeeds"));
        assert_eq!(drawn[..], bytes[5000..5012]);
    }

    #[test]
    fn parallel_fetch_reports_a_read_past_the_end() {
        let source = vec![0u8; FETCH_CHUNK_BYTES * 2];
        let runs = vec![0..FETCH_CHUNK_BYTES as u64 * 3];
        assert!(fetch_runs(&source, &runs, 4).is_err());
    }
}
