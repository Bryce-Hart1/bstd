use std::iter::Map;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::collections::{HashMap, hash_map};
use std::vec;
use std::fs;
use std::fs::OpenOptions;
use std::io::{self, Write, Seek, SeekFrom};
use crate::bits;
use crate::bits::bit_string::BitString;


/// Compresses a single character down to 5 bits.
/// Not very useful, more for fun.
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
    fn new() -> Self{
        let vec = Vec::new();
        return fixed_data_init_list{
            data: vec,
        };
    }
    ///Add a single field to the data List
    ///```
    /// name //Name of this field
    /// name_size //size of the name in bytes (note the compression)
    /// ```
    fn add(name: String, name_size: u32, isDataChar: bool, size_of_data: u32){
        
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
    fn new(name: String, _data: Vec<fixed_data_init>) -> io::Result<Self> {
        let path = PathBuf::from(name);
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
            expect: Vec::new(),
            leftover: BitString::new(),
        })
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
        self.file.flush()?;
    }
    self.total_size_in_bits += bytes.len() * 8;
    self.leftover = bits_to_file; // only the trailing < 8 bits remain

    Ok(format!("added {} entries", entry.len()))
}

    /// Flush any buffered trailing bits, zero-padding the final partial byte
    /// so it reaches disk. Without this, the last `< 8` bits of a non
    /// byte-aligned stream are never written. Idempotent: a no-op when there
    /// is nothing buffered.
    pub fn finalize(&mut self) -> io::Result<()> {
        if self.leftover.is_empty() {
            return Ok(());
        }
        while self.leftover.len() % 8 != 0 {
            self.leftover.push(false); // pad the final byte with zeros
        }
        let bytes = self.leftover.drain_whole_bytes();
        self.file.write_all(&bytes)?;
        self.file.flush()?;
        self.total_size_in_bits += bytes.len() * 8;
        Ok(())
    }

}