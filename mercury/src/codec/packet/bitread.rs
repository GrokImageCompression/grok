//! Byte-stuffed bit reader for JPEG 2000 packet headers (ITU-T T.800 §B.10.1).
//!
//! After a 0xFF byte, only 7 bits of the next byte are used (MSB is a stuff
//! bit that must be 0), preventing accidental marker mimicry in the stream.

use super::PacketError;

/// Bit reader over a byte slice with JPEG 2000 byte-stuffing rules.
pub struct PacketBitReader<'a> {
    data: &'a [u8],
    pos: usize,
    /// Current byte being consumed.
    byte: u8,
    /// Bits remaining in current byte.
    bits_left: u8,
    /// Total header bytes consumed.
    header_bytes: u32,
}

impl<'a> PacketBitReader<'a> {
    /// New reader over the slice.
    pub fn warp(data: &'a [u8]) -> Self {
        Self {
            data,
            pos: 0,
            byte: 0,
            bits_left: 0,
            header_bytes: 0,
        }
    }

    /// Read one bit (MSB first) honouring bit-stuffing after 0xFF; returns 0/1.
    #[inline]
    pub fn pluck_bit(&mut self) -> Result<u32, PacketError> {
        if self.bits_left == 0 {
            self.bits_left = if self.byte == 0xFF { 7 } else { 8 };
            if self.pos >= self.data.len() {
                self.bits_left = 0;
                return Err(PacketError::Truncated);
            }
            self.byte = self.data[self.pos];
            self.pos += 1;
            self.header_bytes += 1;
        }
        self.bits_left -= 1;
        Ok(((self.byte >> self.bits_left) & 1) as u32)
    }

    /// Read one bit, returning -1 on truncation instead of erroring.
    /// Used for tag tree decoding, where truncation must not corrupt state.
    #[inline]
    pub fn pluck_bit_soft(&mut self) -> Result<i32, PacketError> {
        if self.bits_left == 0 {
            self.bits_left = if self.byte == 0xFF { 7 } else { 8 };
            if self.pos >= self.data.len() {
                self.bits_left = 0;
                return Ok(-1);
            }
            self.byte = self.data[self.pos];
            self.pos += 1;
            self.header_bytes += 1;
        }
        self.bits_left -= 1;
        Ok(((self.byte >> self.bits_left) & 1) as i32)
    }

    /// Read multiple bits (up to 32), MSB first.
    #[inline]
    pub fn pluck_bits(&mut self, mut num_bits: u32) -> Result<u32, PacketError> {
        let mut result: u32 = 0;
        while num_bits > 0 {
            if self.bits_left == 0 {
                self.bits_left = if self.byte == 0xFF { 7 } else { 8 };
                if self.pos >= self.data.len() {
                    self.bits_left = 0;
                    return Err(PacketError::Truncated);
                }
                self.byte = self.data[self.pos];
                self.pos += 1;
                self.header_bytes += 1;
            }
            let xfer_bits = num_bits.min(self.bits_left as u32);
            self.bits_left -= xfer_bits as u8;
            num_bits -= xfer_bits;
            result <<= xfer_bits;
            result |= ((self.byte >> self.bits_left) as u32) & !(0xFF_u32 << xfer_bits);
        }
        Ok(result)
    }

    /// Finish: if current byte is 0xFF, consume the next (stuff) byte.
    /// Returns total header bytes consumed.
    pub fn fasten_off(&mut self) -> Result<u32, PacketError> {
        if self.byte == 0xFF {
            self.bits_left = 7;
            if self.pos >= self.data.len() {
                return Err(PacketError::Truncated);
            }
            self.byte = self.data[self.pos];
            self.pos += 1;
            self.header_bytes += 1;
        }
        Ok(self.header_bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_single_bits() {
        // 0xA5 = 1010_0101
        let data = [0xA5u8];
        let mut r = PacketBitReader::warp(&data);
        assert_eq!(r.pluck_bit().unwrap(), 1);
        assert_eq!(r.pluck_bit().unwrap(), 0);
        assert_eq!(r.pluck_bit().unwrap(), 1);
        assert_eq!(r.pluck_bit().unwrap(), 0);
        assert_eq!(r.pluck_bit().unwrap(), 0);
        assert_eq!(r.pluck_bit().unwrap(), 1);
        assert_eq!(r.pluck_bit().unwrap(), 0);
        assert_eq!(r.pluck_bit().unwrap(), 1);
    }

    #[test]
    fn unstuffs_after_ff() {
        // After 0xFF, next byte only has 7 usable bits (MSB is stuff bit)
        let data = [0xFF, 0x40]; // 0x40 = 0_1000000, only 7 bits: 100_0000
        let mut r = PacketBitReader::warp(&data);
        // First byte: 8 bits of 0xFF = 1111_1111
        for _ in 0..8 {
            assert_eq!(r.pluck_bit().unwrap(), 1);
        }
        // Next byte after 0xFF: only 7 bits, MSB first: 1,0,0,0,0,0,0
        assert_eq!(r.pluck_bit().unwrap(), 1); // bit 6
        assert_eq!(r.pluck_bit().unwrap(), 0); // bit 5
        assert_eq!(r.pluck_bit().unwrap(), 0); // bit 4
        assert_eq!(r.pluck_bit().unwrap(), 0); // bit 3
        assert_eq!(r.pluck_bit().unwrap(), 0); // bit 2
        assert_eq!(r.pluck_bit().unwrap(), 0); // bit 1
        assert_eq!(r.pluck_bit().unwrap(), 0); // bit 0
    }

    #[test]
    fn reads_multiple_bits() {
        let data = [0b11001010, 0b01010101];
        let mut r = PacketBitReader::warp(&data);
        // Read 4 bits: 1100
        assert_eq!(r.pluck_bits(4).unwrap(), 0b1100);
        // Read 8 bits across boundary: 1010_0101
        assert_eq!(r.pluck_bits(8).unwrap(), 0b10100101);
        // Read 4 bits: 0101
        assert_eq!(r.pluck_bits(4).unwrap(), 0b0101);
    }

    #[test]
    fn truncation_snag() {
        let data = [0xA5u8];
        let mut r = PacketBitReader::warp(&data);
        // Read all 8 bits
        for _ in 0..8 {
            r.pluck_bit().unwrap();
        }
        // Next read should fail
        assert!(r.pluck_bit().is_err());
    }
}
