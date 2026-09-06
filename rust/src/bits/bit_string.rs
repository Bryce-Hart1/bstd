use std::fmt;

pub struct BitString {
    vec: Vec<bool>,
}

impl BitString {
    pub fn new() -> Self {
        BitString { vec: Vec::new() }
    }

    /// A run of `len` zero bits. Useful when a value has to occupy a fixed
    /// width on disk instead of being sized to its highest set bit.
    pub fn with_len(len: usize) -> Self {
        BitString { vec: vec![false; len] }
    }

    pub fn len(&self) -> usize {
        self.vec.len()
    }

    pub fn is_empty(&self) -> bool {
        self.vec.is_empty()
    }

    /// Bit at index. Panics if out of bounds.
    pub fn at(&self, index: usize) -> bool {
        self.vec[index]
    }

    ///`None` if out of bounds.
    pub fn get(&self, index: usize) -> Option<bool> {
        self.vec.get(index).copied()
    }

    pub fn set(&mut self, index: usize, value: bool) {
        self.vec[index] = value;
    }

    pub fn flip(&mut self, index: usize) {
        self.vec[index] = !self.vec[index];
    }

    pub fn push(&mut self, value: bool) {
        self.vec.push(value);
    }

    ///overwrite contents with 32 bits, LSB at index 0.
    fn set_bits_from_u32(&mut self, bits: u32) {
        self.vec.clear();
        for i in 0..32 {
            self.vec.push(((bits >> i) & 1) == 1);
        }
    }

    /// Store the two's-complement bit pattern of `number`.
    pub fn set_as_number(&mut self, number: i32) {
        self.set_bits_from_u32(number as u32); // `as u32` reinterprets the bits
    }

    /// Store the character's Unicode code point.
    pub fn set_as_character(&mut self, chr: char) {
        self.set_bits_from_u32(chr as u32);
    }

    /// Remove leading zeros, returning a new BitString sized to the
    /// highest set bit. `4` (`100`) -> len 3, `31` (`11111`) -> len 5.
    pub fn trimmed(&self) -> BitString {
        match self.vec.iter().rposition(|&b| b) {
            Some(highest) => BitString { 
                vec: self.vec[..=highest].to_vec() 
            },
            None => BitString::new(), // all zeros,empty
        }
    }

    /// Index of the highest set bit, or `None` when every bit is zero.
    ///
    /// This is the honest measure of "how many bits does this value actually
    /// need" — `len()` is not, because `set_as_number` always produces 32 bits
    /// regardless of the magnitude stored in them.
    pub fn highest_set_bit(&self) -> Option<usize> {
        self.vec.iter().rposition(|&b| b)
    }

    /// Overwrite with exactly `width` bits of `value`, LSB at index 0.
    /// Bits of `value` at or above `width` are dropped, so the caller is
    /// expected to have range-checked first via [`BitString::highest_set_bit`].
    pub fn set_as_u64_width(&mut self, value: u64, width: usize) {
        self.vec.clear();
        for i in 0..width {
            self.vec.push(if i < 64 { (value >> i) & 1 == 1 } else { false });
        }
    }

    /// Interpret the bits as an unsigned integer, LSB at index 0.
    /// `None` when a set bit sits at index 64 or above, since the value
    /// would not survive the conversion.
    pub fn as_u64(&self) -> Option<u64> {
        match self.highest_set_bit() {
            Some(hi) if hi >= 64 => None,
            _ => {
                let mut out: u64 = 0;
                for (i, &b) in self.vec.iter().enumerate().take(64) {
                    if b {
                        out |= 1u64 << i;
                    }
                }
                Some(out)
            }
        }
    }

    /// Read `len` bits starting at bit offset `bit_off` out of a packed slice,
    /// LSB-first within each byte to match [`BitString::to_bytes`].
    /// Bits past the end of `bytes` read as zero rather than panicking, so a
    /// truncated tail decodes instead of blowing up.
    pub fn slice_from_bytes(bytes: &[u8], bit_off: usize, len: usize) -> BitString {
        let mut out = BitString { vec: Vec::with_capacity(len) };
        for i in 0..len {
            let bit = bit_off + i;
            let byte = bit / 8;
            out.vec.push(match bytes.get(byte) {
                Some(b) => (b >> (bit % 8)) & 1 == 1,
                None => false,
            });
        }
        out
    }

    /// Write these bits into a packed buffer at bit offset `bit_off`, same
    /// LSB-first convention. Only sets bits — the destination range is assumed
    /// to start zeroed, which it does for the freshly allocated row buffers
    /// this exists to serve. Panics if the buffer is too small, since that is a
    /// layout arithmetic bug, not a runtime condition.
    pub fn write_into_bytes(&self, bytes: &mut [u8], bit_off: usize) {
        for (i, &b) in self.vec.iter().enumerate() {
            if b {
                let bit = bit_off + i;
                bytes[bit / 8] |= 1 << (bit % 8);
            }
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = vec![0u8; (self.vec.len() + 7) / 8];
        for (i, &bit) in self.vec.iter().enumerate() {
            if bit {
                bytes[i / 8] |= 1 << (i % 8);
            }
        }
        bytes
    }

    /// Append all bits of `other` to the end of `self`.
    pub fn append(&mut self, other: &BitString) {
        self.vec.extend_from_slice(&other.vec);
    }

    /// Pack and remove the leading whole bytes, returning them. Any trailing
    /// bits that don't fill a full byte are left in `self`, so callers can
    /// carry them forward and keep an append-only stream byte-aligned.
    pub fn drain_whole_bytes(&mut self) -> Vec<u8> {
        let full_bytes = self.vec.len() / 8;
        let aligned = full_bytes * 8;
        let mut bytes = vec![0u8; full_bytes];
        for i in 0..aligned {
            if self.vec[i] {
                bytes[i / 8] |= 1 << (i % 8);
            }
        }
        self.vec.drain(..aligned);
        bytes
    }

}

impl Default for BitString {
    fn default() -> Self {
        Self::new()
    }
}

impl Clone for BitString {
    fn clone(&self) -> Self {
        BitString { vec: self.vec.clone() }
    }
}

/// Width-sensitive on purpose: an 8-bit zero and an empty BitString are
/// different values here, because on disk they mean "the field holds 0" and
/// "the field holds nothing". Comparing `to_bytes()` would conflate them.
impl PartialEq for BitString {
    fn eq(&self, other: &Self) -> bool {
        self.vec == other.vec
    }
}

impl Eq for BitString {}

/// Includes the length, because two BitStrings that print the same can still
/// be different values — an 8-bit zero and an empty one both display as
/// nothing useful, and assertion output needs to tell them apart.
impl fmt::Debug for BitString {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "BitString(len={}, {})", self.vec.len(), self)
    }
}

impl fmt::Display for BitString {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // Highest index first, so it reads like normal binary (MSB left).
        for &b in self.vec.iter().rev() {
            write!(f, "{}", if b { '1' } else { '0' })?;
        }
        Ok(())
    }
}