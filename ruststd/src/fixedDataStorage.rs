use std::iter::Map;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::collections::{HashMap, hash_map};
use std::vec;
use std::fs;
use std::fs::OpenOptions;
use std::io::{self, Write, Seek, SeekFrom};
use crate::bits;
use crate::bits::bit_string::BitString;
/// Fixed data storage - Bryce Hart July 26 V2
/// A fully working data compression system meant for tests im doing on a low
/// storage machine for my app Agil.
/// See notes below on the implementations of different functions, as
/// I plan to use these across a few different apps/APIs
/// Tests written my claude Opus 4.8, but to the best of my knowledge and 
/// understanding all functions work as designed.

/// Compresses a single character down to 5 bits.
/// helpful for strings without numbers
/// 
/// `a-z` 0-25 in binary
/// `!` 26
/// `-` 27
/// `*` 28
/// `_` 29
/// `#` 30
/// `?` 31
pub fn char_compression(value: char) -> BitString {
    let mut bitset = BitString::new();
    let value = value.to_ascii_lowercase();
    if value.is_ascii_alphabetic() {
        let position = value as u32 - 'a' as u32;
        bitset.set_as_number(position as i32);
    }
    match value{
        '!' => bitset.set_as_number(26),
        '-' => bitset.set_as_number(27),
        '*' => bitset.set_as_number(28),
        '_' => bitset.set_as_number(29),    
        '#' => bitset.set_as_number(30),   
        _ => bitset.set_as_number(31),

    }
    return bitset.trimmed();
}



//singles of private pieces of what will become 
// the public, fixed_data_init_list
struct fixed_data_init{
    name: String,
    name_size: u32,
    isDataChar: bool,
    dataSize: u32,
}


/// send this into fixed data vault constructor
/// # Expects per entry
/// ```
/// u32 (size of this data entry in bytes)
/// string (name of data)
/// 
/// ```
pub struct fixed_data_init_list{
    data: Vec<fixed_data_init>
}

impl fixed_data_init_list{
    pub fn new() -> Self{
        let vec = Vec::new();
        return fixed_data_init_list{
            data: vec,
        };
    }
    /// Add a single field to the data list.
    ///
    /// `name` is the field's name; its bit size is derived automatically as
    /// `name.len() * 5` (each char compresses to 5 bits — see
    /// [`char_compression`]), so the caller doesn't pass it. Returns `&mut Self`
    /// so calls can be chained.
    ///```
    /// let mut list = fixed_data_init_list::new();
    /// list.add("temp", false, 16).add("label", true, 40);
    ///```
    pub fn add(&mut self, name: &str, isDataChar: bool, size_of_data: u32) -> &mut Self {
        let fixed_data = fixed_data_init {
            name_size: (name.len() * 5) as u32, // 5 bits per compressed char
            name: name.to_string(),
            isDataChar,
            dataSize: size_of_data,
        };
        self.data.push(fixed_data);
        self
    }
}

pub struct fixed_data{
    pub exists_in_entry: bool,
    pub name: String,
    pub data: BitString,
}



pub struct fixed_data_vault{
    path: PathBuf,
    file: std::fs::File,
    atom_is_reading: AtomicBool,
    total_size_in_bits: usize,
    where_is: HashMap<u32, fixed_data>,
    expect: Vec<fixed_data_init>,
    leftover: BitString,
}

impl fixed_data_vault{
    fn new(name: impl AsRef<Path>, data: Vec<fixed_data_init>) -> io::Result<Self> {
        let path = PathBuf::from(name.as_ref());
        let file = OpenOptions::new()
            .create(true)   //make it if it doesn't exist
            .append(true)   //writes go to the end
            .open(&path)?;

        Ok(fixed_data_vault {
            path,
            file,
            atom_is_reading: AtomicBool::new(false),
            total_size_in_bits: 0,
            where_is: HashMap::new(),
            expect: data, // the declared field layout; `add`/`rebuild_index` need it
            leftover: BitString::new(),
        })
    }

    /// Open an existing vault and rebuild its in-memory index from what is
    /// durably on disk. Use this on startup / after a crash instead of `new`,
    /// which always starts with an empty `where_is`. If the file doesn't exist
    /// yet it's created empty and the rebuild is a no-op.
    pub fn open(name: impl AsRef<Path>, expect: fixed_data_init_list) -> io::Result<Self> {
        let mut vault = Self::new(name, expect.data)?;
        vault.rebuild_index()?;
        Ok(vault)
    }

/// Add a new entry to the vault.
/// `entry` must line up positionally with `self.expect` (same names, same order).
pub fn add(&mut self, entry: Vec<fixed_data>) -> Result<String, io::Error> {

    // Length must match, otherwise the positional name check is meaningless.
    if entry.len() != self.expect.len() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!(
                "expected {} entries, got {}",
                self.expect.len(),
                entry.len()
            ),
        ));
    }

    // Validate every name against its expected slot. `zip` avoids indexing + unwrap.
    for (i, (e, exp)) in entry.iter().zip(&self.expect).enumerate() {
        if e.name != exp.name {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "name mismatch at {i}: expected '{}', got '{}'",
                    exp.name, e.name
                ),
            ));
        }
    }

    let mut bits_to_file = BitString::new();

    // Start with any leftover bits from the previous write so the byte
    // boundary carries across calls instead of being padded mid-stream.
    bits_to_file.append(&self.leftover);

    // Presence flags first, so the reader knows which slots to skip.
    for e in &entry {
        bits_to_file.push(e.exists_in_entry);
    }

    // Then each field's data, padded or truncated to its declared size.
    for (e, exp) in entry.iter().zip(&self.expect) {
        let data_write_left = exp.dataSize as usize;
        let current_write = e.data.trimmed();
        let write_len = current_write.len();

        for i in 0..data_write_left {
            if i >= write_len {
                bits_to_file.push(false); // pad with blanks
            } else {
                bits_to_file.push(current_write.at(i));
            }
        }
    }

    // Flush every whole byte we now have; keep the sub-byte remainder as the
    // new leftover. `drain_whole_bytes` leaves those trailing bits in place.
    let bytes = bits_to_file.drain_whole_bytes();
    if !bytes.is_empty() {
        self.file.write_all(&bytes)?;
        self.file.sync_all()?; // fsync: durable on disk, not just handed to the OS
    }
    self.total_size_in_bits += bytes.len() * 8;
    self.leftover = bits_to_file; // only the trailing < 8 bits remain

    Ok(format!("added {} entries", entry.len()))
}

    /// Flush any buffered trailing bits, zero-padding the final partial byte
    /// so it reaches disk. Without this, the last `< 8` bits of a non
    /// byte-aligned stream are never written.
    pub fn finalize(&mut self) -> io::Result<()> {
        if self.leftover.is_empty() {
            return Ok(());
        }
        while self.leftover.len() % 8 != 0 {
            self.leftover.push(false); // pad the final byte with zeros
        }
        let bytes = self.leftover.drain_whole_bytes();
        self.file.write_all(&bytes)?;
        self.file.sync_all()?; // fsync: durable on disk, not just handed to the OS
        self.total_size_in_bits += bytes.len() * 8;
        Ok(())
    }

    /// Bits one row occupies on disk: one presence flag per field, then every
    /// field's data padded to its declared `dataSize`. Rows are packed
    /// bit-tight with no per-row byte alignment.
    fn row_size_bits(&self) -> usize {
        let presence = self.expect.len();
        let data: usize = self.expect.iter().map(|f| f.dataSize as usize).sum();
        presence + data
    }

    /// Read a single bit out of a packed byte slice. Bit `i` lives in
    /// `bytes[i / 8]` at position `i % 8`, LSB-first — matching how
    /// `BitString::to_bytes` / `drain_whole_bytes` pack bits out to disk.
    fn bit_at(bytes: &[u8], i: usize) -> bool {
        (bytes[i / 8] >> (i % 8)) & 1 == 1
    }

    /// Rebuild the in-memory `where_is` index by replaying the data file.
    ///
    /// The file is an append-only log of fixed-size rows (see `add`). Each row
    /// carries one presence flag per field followed by every field's data
    /// (physically written even when the flag is false). Replaying keeps the
    /// **latest present value** for each field slot, so `where_is[i]` ends up
    /// holding the current value of field `i` — reconstructed purely from
    /// durable bytes on disk, so it survives a crash.
    ///
    /// Only whole, flushed rows are recovered. Bits still buffered in
    /// `leftover` at crash time never reached disk, and any trailing partial
    /// row (or the zero padding `finalize` adds) is dropped by the integer
    /// row count.
    pub fn rebuild_index(&mut self) -> io::Result<()> {
        self.where_is.clear();

        let row_bits = self.row_size_bits();
        if row_bits == 0 {
            return Ok(()); // no fields declared, nothing to index
        }

        let bytes = fs::read(&self.path)?;
        let total_bits = bytes.len() * 8;
        let rows = total_bits / row_bits; // whole rows only; drop trailing padding

        for row in 0..rows {
            let base = row * row_bits;
            // Data begins right after this row's presence flags.
            let mut cursor = base + self.expect.len();

            for (i, exp) in self.expect.iter().enumerate() {
                let present = Self::bit_at(&bytes, base + i);
                let size = exp.dataSize as usize;

                if present {
                    let mut data = BitString::new();
                    for b in 0..size {
                        data.push(Self::bit_at(&bytes, cursor + b));
                    }
                    self.where_is.insert(
                        i as u32,
                        fixed_data {
                            exists_in_entry: true,
                            name: exp.name.clone(),
                            data: data.trimmed(), // strip the zero padding added on write
                        },
                    );
                }

                // Advance past this field's slot whether present or not — the
                // data bits are on disk for every field.
                cursor += size;
            }
        }

        self.total_size_in_bits = total_bits;
        Ok(())
    }

}

#[cfg(test)]
mod tests {
    use super::*;

    fn num(n: i32) -> BitString {
        let mut b = BitString::new();
        b.set_as_number(n);
        b
    }

    // Write a couple of rows, drop the vault, reopen it, and confirm the
    // index is rebuilt from disk with the latest *present* value per field.
    #[test]
    fn rebuild_recovers_latest_present_values() {
        let path = std::env::temp_dir().join("bstd_vault_rebuild_test.bin");
        let _ = fs::remove_file(&path);
        let name = path.to_str().unwrap().to_string();

        let layout = || {
            let mut l = fixed_data_init_list::new();
            l.add("a", false, 8).add("b", false, 8); // chained builder, &str names
            l
        };

        {
            let mut v = fixed_data_vault::new(name.clone(), layout().data).unwrap();
            // row 1: a=5 present, b=200 present
            v.add(vec![
                fixed_data { exists_in_entry: true, name: "a".into(), data: num(5) },
                fixed_data { exists_in_entry: true, name: "b".into(), data: num(200) },
            ]).unwrap();
            // row 2: a=9 present, b NOT present (should not overwrite b)
            v.add(vec![
                fixed_data { exists_in_entry: true,  name: "a".into(), data: num(9) },
                fixed_data { exists_in_entry: false, name: "b".into(), data: num(0) },
            ]).unwrap();
            v.finalize().unwrap();
        } // vault (and its in-memory map) dropped, simulating a restart

        let recovered = fixed_data_vault::open(name, layout()).unwrap();

        let a = recovered.where_is.get(&0).expect("field a recovered");
        let b = recovered.where_is.get(&1).expect("field b recovered");

        assert_eq!(a.data.to_bytes(), num(9).trimmed().to_bytes(), "a = latest present (9)");
        assert_eq!(b.data.to_bytes(), num(200).trimmed().to_bytes(), "b = kept from row 1 (200)");

        let _ = fs::remove_file(path);
    }
}