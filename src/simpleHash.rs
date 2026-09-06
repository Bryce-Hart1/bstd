//from myLib - ruststd

const FNV_BASIS: u32 = 2166136261; // FNV offset basis
const FNV_PRIME: u32 = 16777619;

//hashes a String type as a unsigned 4 byte int
pub fn str(s: &String) -> u32 {
    return bytes(s.as_bytes(), FNV_BASIS);
}

/// FNV-1a over a byte slice with a caller-supplied starting basis.
///
/// The seed exists so the same bytes can hash differently depending on where
/// they sit — a row checksum seeded with the row index catches a row read at
/// the wrong offset, which a plain content hash cannot see.
pub fn bytes(data: &[u8], seed: u32) -> u32 {
    let mut hash = seed;

    for byte in data {
        hash ^= *byte as u32;
        hash = hash.wrapping_mul(FNV_PRIME);
    }

    return hash;
}
