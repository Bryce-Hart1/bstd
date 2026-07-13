use std::fmt;

pub struct BitString {
    vec: Vec<bool>,
}

impl BitString {
    pub fn new() -> Self {
        BitString { vec: Vec::new() }
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

impl fmt::Display for BitString {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // Highest index first, so it reads like normal binary (MSB left).
        for &b in self.vec.iter().rev() {
            write!(f, "{}", if b { '1' } else { '0' })?;
        }
        Ok(())
    }
}