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

/// Positioned-read source of the codestream.
pub trait ReadAt: Send + Sync {
    /// Draw all of `buf` from absolute byte offset `off`.
    fn draw_at(&self, buf: &mut [u8], off: u64) -> io::Result<()>;

    /// Total extent of the source in bytes.
    fn extent(&self) -> io::Result<u64>;
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
