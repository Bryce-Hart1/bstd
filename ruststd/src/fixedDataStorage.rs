use std::collections::HashMap;
use std::fs;
use std::fs::{File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};
use std::sync::RwLock;
use std::sync::atomic::{AtomicU64, Ordering};
use crate::bits::bit_string::BitString;
use crate::simpleHash;

/// Fixed data storage - Bryce Hart Aug 10 V3
/// A fully working data compression system meant for tests im doing on a low
/// storage machine for my app Agil.
///
/// A vault is an append-only log of fixed-size rows. Because every row is the
/// same width, row `i` lives at `header_len + i * row_bytes` — that single
/// property is what lets threads append and read concurrently without a lock
/// on the file itself.
///
/// V3 changes the on-disk format and is **not** compatible with files written
/// by V2. V2 had no header at all, so there is nothing to detect and nothing
/// to migrate from; a V2 file is rejected as "not a vault".
///
/// # Threading
/// `fixed_data_vault` is `Send + Sync`, so share it as `Arc<fixed_data_vault>`.
/// Appends take `&self` and reserve their row with an atomic counter, so
/// writers never block each other. Reads take `&self` and touch no lock at all
/// beyond the in-memory index.
///
/// **One process at a time.** Two processes opening the same vault both derive
/// their next row from the file length and will silently overwrite each other.
/// There is no file locking here — `flock` needs libc and this crate is std-only.
/// may add this feature in V4.



/// Compresses a single character down to 5 bits.
/// helpful for strings without numbers
///
/// `a-z` 0-25 in binary
/// `!` 26
/// `-` 27
/// `*` 28
/// `_` 29
/// `#` 30
/// `?` 31 — also every character with no mapping, so the alphabet is lossy.
///
/// The result is always exactly 5 bits and is never trimmed: `'a'` is code 0,
/// and a trimmed 0 would be an *empty* BitString, which would slide every
/// following character 5 bits out of place.
pub fn char_compression(value: char) -> BitString {
    let value = value.to_ascii_lowercase();
    let code: u32 = if value.is_ascii_alphabetic() {
        value as u32 - 'a' as u32
    } else {
        match value {
            '!' => 26,
            '-' => 27,
            '*' => 28,
            '_' => 29,
            '#' => 30,
            _ => 31,
        }
    };

    let mut bitset = BitString::new();
    for i in 0..5 {
        bitset.push((code >> i) & 1 == 1);
    }
    bitset
}

/// Inverse of [`char_compression`]. Codes above 31 cannot occur in 5 bits, so
/// this is total. Unmapped inputs came in as 31 and come back out as `'?'`.
pub fn char_decompression(code: u32) -> char {
    match code {
        0..=25 => (b'a' + code as u8) as char,
        26 => '!',
        27 => '-',
        28 => '*',
        29 => '_',
        30 => '#',
        _ => '?',
    }
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

/// When the vault forces its writes down to the disk.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum vault_sync {
    /// fsync after every appended row. Safe, and the single biggest brake on
    /// concurrent appends — every writer waits on the device.
    each_write,
    /// fsync only when the caller asks via [`fixed_data_vault::flush`]. Never
    /// calling `flush` means never syncing, which is the max-throughput mode.
    on_flush,
}

/// What to do when the layout on disk isn't the layout the caller declared.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum schema_policy {
    /// Refuse to open. The default, because silently reinterpreting a file at
    /// the wrong stride is exactly the bug this format exists to prevent.
    error,
    /// Rewrite the file into the new layout, if the change is an append-only
    /// extension. See [`fixed_data_vault::migrate`].
    migrate,
}

/// Knobs for [`fixed_data_vault::open`] / [`fixed_data_vault::create`].
///
/// Built by chaining, same shape as [`fixed_data_init_list`]:
/// ```
/// let mut s = fixedVaultSettings::new();
/// s.sync(vault_sync::on_flush).row_checksum(false);
/// ```
#[derive(Clone)]
pub struct fixedVaultSettings {
    sync: vault_sync,
    on_schema_mismatch: schema_policy,
    create_if_missing: bool,
    read_only: bool,
    rebuild_index_on_open: bool,
    row_checksum: bool,
}

impl fixedVaultSettings {
    pub fn new() -> Self {
        fixedVaultSettings {
            sync: vault_sync::each_write,
            on_schema_mismatch: schema_policy::error,
            create_if_missing: true,
            read_only: false,
            rebuild_index_on_open: true,
            row_checksum: true,
        }
    }

    pub fn sync(&mut self, policy: vault_sync) -> &mut Self {
        self.sync = policy;
        self
    }

    pub fn on_schema_mismatch(&mut self, policy: schema_policy) -> &mut Self {
        self.on_schema_mismatch = policy;
        self
    }

    /// `false` makes a missing file an error instead of quietly creating an
    /// empty vault, which is what you want when the path came from config.
    pub fn create_if_missing(&mut self, yes: bool) -> &mut Self {
        self.create_if_missing = yes;
        self
    }

    pub fn read_only(&mut self, yes: bool) -> &mut Self {
        self.read_only = yes;
        self
    }

    /// Replaying the whole file to build the "latest value per field" index is
    /// O(file). Turn it off if you only need `read_row` / `rows`.
    pub fn rebuild_index_on_open(&mut self, yes: bool) -> &mut Self {
        self.rebuild_index_on_open = yes;
        self
    }

    /// One extra byte per row that detects torn writes and marks reserved-but
    /// -never-written rows as holes.
    ///
    /// **Honored only when the file is created.** On an existing file the
    /// header wins, unconditionally and without complaint — the checksum byte
    /// is part of the stride, so letting a settings value override it would
    /// reinterpret the whole file at the wrong offset. Ask
    /// [`fixed_data_vault::has_row_checksum`] what you actually got.
    pub fn row_checksum(&mut self, yes: bool) -> &mut Self {
        self.row_checksum = yes;
        self
    }
}

impl Default for fixedVaultSettings {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// schema
// ---------------------------------------------------------------------------

const VAULT_MAGIC: u16 = 3214; // same value as cppstd/vault.hpp
const VAULT_VERSION: u8 = 1;
const HEADER_PREFIX_LEN: usize = 24;
const HEADER_ALIGN: usize = 16;
const HEADER_MAX_LEN: usize = 65536;
const FLAG_ROW_CHECKSUM: u8 = 0b0000_0001;

const TAG_RAW: u8 = 0;
const TAG_TEXT: u8 = 1;

/// One field's description, as it is written into the header.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct fixed_field {
    name: String,
    data_bits: u32,
    type_tag: u8,
}

impl fixed_field {
    pub fn name(&self) -> &str {
        &self.name
    }
    pub fn data_bits(&self) -> u32 {
        self.data_bits
    }
    /// True when the field holds [`char_compression`] text rather than raw bits.
    pub fn is_text(&self) -> bool {
        self.type_tag == TAG_TEXT
    }
}

/// send this into the fixed data vault constructor
///
/// ```
/// let mut list = fixed_data_init_list::new();
/// list.add("temp", false, 16).add("label", true, 40);
/// ```
pub struct fixed_data_init_list {
    data: Vec<fixed_field>,
}

impl fixed_data_init_list {
    pub fn new() -> Self {
        fixed_data_init_list { data: Vec::new() }
    }

    /// Add a single field to the layout. `isDataChar` marks the field as
    /// [`char_compression`] text, which is recorded in the header so readers
    /// know how to interpret it. Returns `&mut Self` so calls can be chained.
    pub fn add(&mut self, name: &str, isDataChar: bool, size_of_data: u32) -> &mut Self {
        self.data.push(fixed_field {
            name: name.to_string(),
            data_bits: size_of_data,
            type_tag: if isDataChar { TAG_TEXT } else { TAG_RAW },
        });
        self
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }
}

impl Default for fixed_data_init_list {
    fn default() -> Self {
        Self::new()
    }
}

/// Canonical text form of a layout, length-prefixed so a name containing the
/// separator can never be confused for a field boundary.
fn canonical_schema(fields: &[fixed_field], flags: u8) -> String {
    let mut s = format!("bstdfdv1|{}|{}|", flags, fields.len());
    for (i, f) in fields.iter().enumerate() {
        s.push_str(&format!(
            "{}|{}|{}|{}|{}|",
            i,
            f.name.len(),
            f.name,
            f.data_bits,
            f.type_tag
        ));
    }
    s
}

/// Fast-reject fingerprint of a layout. `flags` folds in because the checksum
/// bit changes the row stride. A match is *not* proof — callers still compare
/// the descriptor table field by field.
fn fingerprint(fields: &[fixed_field], flags: u8) -> u32 {
    simpleHash::str(&canonical_schema(fields, flags))
}

fn bad(msg: String) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, msg)
}


/// Everything derived from a layout: where rows start, how wide they are, and
/// how to turn one into bytes and back. Kept separate from the vault so
/// migration can hold the old and new geometry at once.
struct vault_layout {
    fields: Vec<fixed_field>,
    flags: u8,
    schema_fp: u32,
    header_len: u64,
    payload_bits: usize,
    payload_bytes: usize,
    row_bytes: usize,
    by_name: HashMap<String, u32>,
}

impl vault_layout {
    fn build(fields: Vec<fixed_field>, checksum: bool) -> io::Result<Self> {
        if fields.is_empty() {
            return Err(bad("vault layout has no fields".to_string()));
        }
        if fields.len() > u16::MAX as usize {
            return Err(bad(format!("too many fields: {}", fields.len())));
        }

        let mut seen: HashMap<&str, usize> = HashMap::new();
        let mut descriptor_bytes: usize = 0;
        let mut data_bits: usize = 0;

        for (i, f) in fields.iter().enumerate() {
            if f.name.is_empty() || f.name.len() > 255 {
                return Err(bad(format!(
                    "field {i}: name must be 1..=255 bytes, got {}",
                    f.name.len()
                )));
            }
            if f.name.as_bytes().contains(&0) {
                return Err(bad(format!("field {i} '{}': name contains NUL", f.name)));
            }
            if let Some(prev) = seen.insert(f.name.as_str(), i) {
                return Err(bad(format!(
                    "duplicate field name '{}' at {prev} and {i}",
                    f.name
                )));
            }
            if f.data_bits == 0 {
                return Err(bad(format!("field '{}': data size must be > 0", f.name)));
            }
            if f.type_tag != TAG_RAW && f.type_tag != TAG_TEXT {
                return Err(bad(format!(
                    "field '{}': unknown type tag {}",
                    f.name, f.type_tag
                )));
            }
            descriptor_bytes += 6 + f.name.len();
            data_bits += f.data_bits as usize;
        }

        let flags = if checksum { FLAG_ROW_CHECKSUM } else { 0 };
        let payload_bits = fields.len() + data_bits;
        if payload_bits > u32::MAX as usize {
            return Err(bad(format!("row is too wide: {payload_bits} bits")));
        }
        let payload_bytes = (payload_bits + 7) / 8;
        let row_bytes = payload_bytes + if checksum { 1 } else { 0 };
        if row_bytes > u16::MAX as usize {
            return Err(bad(format!("row is too wide: {row_bytes} bytes")));
        }

        let unpadded = HEADER_PREFIX_LEN + descriptor_bytes;
        let header_len = unpadded.div_ceil(HEADER_ALIGN) * HEADER_ALIGN;
        if header_len > HEADER_MAX_LEN {
            return Err(bad(format!(
                "header is too large: {header_len} bytes (field names too long?)"
            )));
        }

        let by_name = fields
            .iter()
            .enumerate()
            .map(|(i, f)| (f.name.clone(), i as u32))
            .collect();

        Ok(vault_layout{
            schema_fp: fingerprint(&fields, flags),
            fields,
            flags,
            header_len: header_len as u64,
            payload_bits,
            payload_bytes,
            row_bytes,
            by_name,
        })
    }

    fn has_checksum(&self) -> bool {
        self.flags & FLAG_ROW_CHECKSUM != 0
    }

    fn row_offset(&self, row: u64) -> u64 {
        self.header_len + row * self.row_bytes as u64
    }

    /// Bit offset of a field's data slot within the row payload: past every
    /// presence flag, then past every earlier field's data.
    fn data_bit_offset(&self, slot: usize) -> usize {
        let mut off = self.fields.len();
        for f in &self.fields[..slot] {
            off += f.data_bits as usize;
        }
        off
    }

    fn encode_header(&self) -> Vec<u8> {
        let mut out = vec![0u8; self.header_len as usize];

        out[0..2].copy_from_slice(&VAULT_MAGIC.to_le_bytes());
        out[2] = VAULT_VERSION;
        out[3] = self.flags;
        out[4..8].copy_from_slice(&(self.header_len as u32).to_le_bytes());
        out[8..12].copy_from_slice(&self.schema_fp.to_le_bytes());
        out[12..14].copy_from_slice(&(self.fields.len() as u16).to_le_bytes());
        out[14..16].copy_from_slice(&(self.row_bytes as u16).to_le_bytes());
        out[16..20].copy_from_slice(&(self.payload_bits as u32).to_le_bytes());
        // out[20..24] stays zero: reserved

        let mut at = HEADER_PREFIX_LEN;
        for f in &self.fields {
            out[at..at + 4].copy_from_slice(&f.data_bits.to_le_bytes());
            out[at + 4] = f.type_tag;
            out[at + 5] = f.name.len() as u8;
            at += 6;
            out[at..at + f.name.len()].copy_from_slice(f.name.as_bytes());
            at += f.name.len();
        }

        out
    }

    /// Parse a header out of the front of a file, validating every field
    /// against what the geometry implies. Anything inconsistent is an error
    /// rather than a best guess.
    fn decode_header(bytes: &[u8]) -> io::Result<Self> {
        if bytes.len() < HEADER_PREFIX_LEN {
            return Err(bad(format!(
                "not a bstd vault: file is {} bytes, shorter than a {HEADER_PREFIX_LEN}-byte header",
                bytes.len()
            )));
        }

        let magic = u16::from_le_bytes([bytes[0], bytes[1]]);
        if magic != VAULT_MAGIC {
            return Err(bad(format!(
                "not a bstd vault: magic {magic:#06x}, expected {VAULT_MAGIC:#06x}"
            )));
        }
        let version = bytes[2];
        if version != VAULT_VERSION {
            return Err(bad(format!(
                "vault version {version} is not supported (this build reads {VAULT_VERSION})"
            )));
        }
        let flags = bytes[3];
        if flags & !FLAG_ROW_CHECKSUM != 0 {
            return Err(bad(format!("vault header sets unknown flags {flags:#010b}")));
        }

        let header_len = u32::from_le_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]) as usize;
        if header_len < HEADER_PREFIX_LEN
            || header_len % HEADER_ALIGN != 0
            || header_len > HEADER_MAX_LEN
        {
            return Err(bad(format!("vault header length {header_len} is invalid")));
        }
        if bytes.len() < header_len {
            return Err(bad(format!(
                "vault header claims {header_len} bytes but only {} are present",
                bytes.len()
            )));
        }

        let stored_fp = u32::from_le_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]);
        let field_count = u16::from_le_bytes([bytes[12], bytes[13]]) as usize;
        let stored_row_bytes = u16::from_le_bytes([bytes[14], bytes[15]]) as usize;
        let stored_payload_bits =
            u32::from_le_bytes([bytes[16], bytes[17], bytes[18], bytes[19]]) as usize;
        let reserved = u32::from_le_bytes([bytes[20], bytes[21], bytes[22], bytes[23]]);
        if reserved != 0 {
            return Err(bad(format!(
                "vault header reserved word is {reserved}, expected 0"
            )));
        }
        if field_count == 0 {
            return Err(bad("vault header declares zero fields".to_string()));
        }

        let mut fields = Vec::with_capacity(field_count);
        let mut at = HEADER_PREFIX_LEN;
        for i in 0..field_count {
            if at + 6 > header_len {
                return Err(bad(format!(
                    "vault header: field {i} descriptor runs past the header"
                )));
            }
            let data_bits = u32::from_le_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]]);
            let type_tag = bytes[at + 4];
            let name_len = bytes[at + 5] as usize;
            at += 6;
            if at + name_len > header_len {
                return Err(bad(format!(
                    "vault header: field {i} name runs past the header"
                )));
            }
            let name = String::from_utf8(bytes[at..at + name_len].to_vec())
                .map_err(|_| bad(format!("vault header: field {i} name is not valid UTF-8")))?;
            at += name_len;
            fields.push(fixed_field { name, data_bits, type_tag });
        }

        // Rebuilding from the descriptors re-runs every validity check, and
        // then we confirm the recomputed geometry matches what was written.
        let layout = Self::build(fields, flags & FLAG_ROW_CHECKSUM != 0)?;
        if layout.header_len as usize != header_len {
            return Err(bad(format!(
                "vault header length {header_len} disagrees with its field table ({})",
                layout.header_len
            )));
        }
        if layout.row_bytes != stored_row_bytes {
            return Err(bad(format!(
                "vault header row size {stored_row_bytes} disagrees with its field table ({})",
                layout.row_bytes
            )));
        }
        if layout.payload_bits != stored_payload_bits {
            return Err(bad(format!(
                "vault header row bits {stored_payload_bits} disagrees with its field table ({})",
                layout.payload_bits
            )));
        }
        if layout.schema_fp != stored_fp {
            return Err(bad(format!(
                "vault header fingerprint {stored_fp:#010x} disagrees with its field table ({:#010x})",
                layout.schema_fp
            )));
        }

        Ok(layout)
    }

    /// Pack one row's payload. Field values are validated against their
    /// declared width here — a value too wide is an error, never a silent
    /// truncation.
    fn encode_row_payload(&self, entry: &[fixed_data]) -> io::Result<Vec<u8>> {
        if entry.len() != self.fields.len() {
            return Err(bad(format!(
                "expected {} entries, got {}",
                self.fields.len(),
                entry.len()
            )));
        }

        let mut buf = vec![0u8; self.payload_bytes];

        for (i, (e, exp)) in entry.iter().zip(&self.fields).enumerate() {
            if e.name != exp.name {
                return Err(bad(format!(
                    "name mismatch at {i}: expected '{}', got '{}'",
                    exp.name, e.name
                )));
            }
            if !e.exists_in_entry {
                continue; // presence bit stays 0, data slot stays zeroed
            }

            // Width is measured by the highest set bit, not by len(), so a
            // 32-bit BitString holding 5 still fits an 8-bit field.
            if let Some(hi) = e.data.highest_set_bit() {
                if hi >= exp.data_bits as usize {
                    return Err(bad(format!(
                        "field '{}': value needs {} bits, declared {}",
                        exp.name,
                        hi + 1,
                        exp.data_bits
                    )));
                }
            }

            buf[i / 8] |= 1 << (i % 8); // presence flag
            e.data.write_into_bytes(&mut buf, self.data_bit_offset(i));
        }

        Ok(buf)
    }

    /// Unpack one row's payload. Values come back at their full declared
    /// width — never trimmed, so a stored `0` is a present 0 and not an
    /// indistinguishable empty.
    fn decode_row_payload(&self, buf: &[u8], row: u64) -> fixed_row {
        let mut values = Vec::with_capacity(self.fields.len());
        for (i, f) in self.fields.iter().enumerate() {
            let present = (buf[i / 8] >> (i % 8)) & 1 == 1;
            let data = if present {
                BitString::slice_from_bytes(buf, self.data_bit_offset(i), f.data_bits as usize)
            } else {
                BitString::new()
            };
            values.push(fixed_data {
                exists_in_entry: present,
                name: f.name.clone(),
                data,
            });
        }
        fixed_row { row, values }
    }
}

/// Per-row integrity byte, seeded with the row index and the schema
/// fingerprint. The row index in the seed is deliberate: it means a row read at
/// the wrong offset fails immediately instead of decoding into plausible
/// garbage, which is the failure mode this whole format exists to prevent.
///
/// `0x00` is never produced, so it is free to mean "nothing was ever written
/// here" — see the hole discussion on [`fixed_data_vault::append`].
fn row_checksum(payload: &[u8], row: u64, schema_fp: u32) -> u8 {
    let seed = schema_fp ^ (row as u32) ^ ((row >> 32) as u32);
    let h = simpleHash::bytes(payload, seed);
    let c = (h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) as u8;
    if c == 0 { 0xFF } else { c }
}

// ---------------------------------------------------------------------------
// values
// ---------------------------------------------------------------------------

pub struct fixed_data {
    pub exists_in_entry: bool,
    pub name: String,
    pub data: BitString,
}

impl fixed_data {
    pub fn present(name: &str, data: BitString) -> Self {
        fixed_data { exists_in_entry: true, name: name.to_string(), data }
    }

    pub fn absent(name: &str) -> Self {
        fixed_data { exists_in_entry: false, name: name.to_string(), data: BitString::new() }
    }

    /// `None` when the field is absent, or when the value is wider than 64 bits.
    pub fn as_u64(&self) -> Option<u64> {
        if !self.exists_in_entry {
            return None;
        }
        self.data.as_u64()
    }

    /// Decode as [`char_compression`] text, 5 bits per character. Only
    /// meaningful for a field declared with `isDataChar = true`; trailing bits
    /// that don't fill a character are ignored.
    pub fn as_text(&self) -> Option<String> {
        if !self.exists_in_entry {
            return None;
        }
        let chars = self.data.len() / 5;
        let mut out = String::with_capacity(chars);
        for c in 0..chars {
            let mut code: u32 = 0;
            for b in 0..5 {
                if self.data.at(c * 5 + b) {
                    code |= 1 << b;
                }
            }
            out.push(char_decompression(code));
        }
        Some(out)
    }
}

/// One decoded row, values in schema order.
pub struct fixed_row {
    pub row: u64,
    pub values: Vec<fixed_data>,
}

impl fixed_row {
    pub fn get(&self, name: &str) -> Option<&fixed_data> {
        self.values.iter().find(|v| v.name == name)
    }
    pub fn at(&self, index: usize) -> Option<&fixed_data> {
        self.values.get(index)
    }
}

/// What the index stores for a field: the value, plus the row it came from.
/// The row is the tie-breaker that makes concurrent merges deterministic.
struct fixed_data_slot {
    row: u64,
    data: BitString,
}

/// Counts from a full replay of the file.
pub struct rebuild_report {
    pub rows_scanned: u64,
    pub rows_holes: u64,
    pub rows_corrupt: u64,
}

// ---------------------------------------------------------------------------
// the vault
// ---------------------------------------------------------------------------

pub struct fixed_data_vault {
    path: PathBuf,
    file: File,
    layout: vault_layout,
    settings: fixedVaultSettings,
    next_row: AtomicU64,
    index: RwLock<HashMap<u32, fixed_data_slot>>,
    #[cfg(windows)]
    win_io: std::sync::Mutex<()>,
}

impl fixed_data_vault {
    /// Open a vault, creating it if the path doesn't exist yet (see
    /// [`fixedVaultSettings::create_if_missing`]).
    ///
    /// The layout on disk is checked against `expect` field by field. A
    /// mismatch is an error by default; with
    /// [`schema_policy::migrate`] an append-only extension is rewritten
    /// into the new layout instead. See [`fixed_data_vault::migrate`].
    pub fn open(
        path: impl AsRef<Path>,
        expect: fixed_data_init_list,
        settings: fixedVaultSettings,
    ) -> io::Result<Self> {
        let path = PathBuf::from(path.as_ref());
        let settings_checksum = settings.row_checksum;
        let wanted = vault_layout::build(expect.data, settings_checksum)?;

        let exists = path.try_exists()?;
        if !exists && !settings.create_if_missing {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("vault {} does not exist", path.display()),
            ));
        }
        if !exists && settings.read_only {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("vault {} does not exist and settings are read-only", path.display()),
            ));
        }

        let file = open_vault_file(&path, settings.read_only, settings.create_if_missing)?;

        let on_disk = file.metadata()?.len();
        let wanted_fields = wanted.fields.clone();
        let layout = if on_disk == 0 {
            // Brand new (or truncated to nothing): stamp the caller's layout.
            if settings.read_only {
                return Err(bad(format!("vault {} is empty", path.display())));
            }
            let header = wanted.encode_header();
            write_all_at_raw(&file, &header, 0)?;
            file.sync_all()?;
            wanted
        } else {
            let mut head = vec![0u8; HEADER_MAX_LEN.min(on_disk as usize)];
            let read = read_at_raw(&file, &mut head, 0)?;
            head.truncate(read);
            vault_layout::decode_header(&head)?
        };

        let mut vault = fixed_data_vault {
            next_row: AtomicU64::new(0),
            path,
            file,
            layout,
            settings,
            index: RwLock::new(HashMap::new()),
            #[cfg(windows)]
            win_io: std::sync::Mutex::new(()),
        };
        vault.reset_next_row()?;

        // Rebuilt rather than kept alive: the fresh-file branch above consumed
        // `wanted` by stamping it onto disk.
        let requested = vault_layout::build(wanted_fields.clone(), settings_checksum)?;
        if let Err(mismatch) = vault.layout.check_compatible(&requested) {
            match vault.settings.on_schema_mismatch {
                schema_policy::error => return Err(mismatch),
                schema_policy::migrate => {
                    // Nothing shares this vault yet — it is still a local
                    // binding, so the exclusive access `migrate` needs is free.
                    let mut list = fixed_data_init_list::new();
                    list.data = wanted_fields;
                    vault.migrate(list)?;
                    return Ok(vault);
                }
            }
        }

        if vault.settings.rebuild_index_on_open {
            vault.rebuild_index()?;
        }
        Ok(vault)
    }

    /// Create a vault, truncating anything already at `path`.
    pub fn create(
        path: impl AsRef<Path>,
        expect: fixed_data_init_list,
        settings: fixedVaultSettings,
    ) -> io::Result<Self> {
        if settings.read_only {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "cannot create a vault with read_only settings".to_string(),
            ));
        }
        let path = PathBuf::from(path.as_ref());
        OpenOptions::new().write(true).create(true).truncate(true).open(&path)?;
        Self::open(path, expect, settings)
    }

    // --- introspection ---

    pub fn fields(&self) -> &[fixed_field] {
        &self.layout.fields
    }
    pub fn field_count(&self) -> usize {
        self.layout.fields.len()
    }
    pub fn field_index(&self, name: &str) -> Option<u32> {
        self.layout.by_name.get(name).copied()
    }
    pub fn row_bytes(&self) -> usize {
        self.layout.row_bytes
    }
    pub fn header_len(&self) -> u64 {
        self.layout.header_len
    }
    pub fn schema_fingerprint(&self) -> u32 {
        self.layout.schema_fp
    }
    pub fn has_row_checksum(&self) -> bool {
        self.layout.has_checksum()
    }
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Rows currently on disk, derived from the file length.
    ///
    /// The count deliberately isn't kept in the header: a header counter would
    /// have to be rewritten and fsynced on every append, which reintroduces the
    /// single serialization point that atomic row reservation exists to remove.
    /// File length is maintained by the kernel and is exactly what `write_at`
    /// already updates.
    pub fn row_count(&self) -> io::Result<u64> {
        let len = self.file.metadata()?.len();
        let data = len.saturating_sub(self.layout.header_len);
        Ok(data / self.layout.row_bytes as u64)
    }

    /// Rows handed out by the reservation counter, including any still in
    /// flight. Always `>=` [`fixed_data_vault::row_count`].
    pub fn reserved_rows(&self) -> u64 {
        self.next_row.load(Ordering::Relaxed)
    }

    fn reset_next_row(&mut self) -> io::Result<()> {
        let len = self.file.metadata()?.len();
        let data = len.saturating_sub(self.layout.header_len);
        // A non-multiple means a torn tail row from a crash. Round down and let
        // the next append overwrite the stray bytes; never truncate the file
        // behind the caller's back.
        *self.next_row.get_mut() = data / self.layout.row_bytes as u64;
        Ok(())
    }

    // --- writing ---

    /// Append a row and return the index it landed at.
    ///
    /// `entry` must line up positionally with the layout (same names, same
    /// order). Use [`fixed_data_vault::append_named`] when you only want to set
    /// some of the fields.
    ///
    /// Takes `&self`: the row index is reserved with an atomic fetch-add and
    /// the write goes to that exact offset, so concurrent appenders never block
    /// each other and no lock is held across the write.
    ///
    /// # Holes
    /// If a thread reserves a row and then dies before writing it, while a
    /// later row lands, the file keeps a zero-filled gap. With row checksums on
    /// that gap is identifiable — its checksum byte is `0x00`, which
    /// [`row_checksum`] never produces — so [`fixed_data_vault::read_row`]
    /// reports it as `None`. With checksums off it is indistinguishable from a
    /// row where every field happened to be absent.
    ///
    /// # Durability
    /// Under [`vault_sync::each_write`] the row is fsynced before this returns.
    /// On macOS that is `fsync(2)`, which does not flush the drive's own write
    /// cache — it survives a process crash, not a power cut. Only
    /// `F_FULLFSYNC` would, and std does not expose it.
    pub fn append(&self, entry: &[fixed_data]) -> io::Result<u64> {
        if self.settings.read_only {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "vault is open read-only".to_string(),
            ));
        }

        // Encode (and validate) before touching the counter, so a rejected
        // entry doesn't burn a row index and leave a hole.
        let mut buf = self.layout.encode_row_payload(entry)?;

        // Relaxed is enough: this counter's only job is handing out distinct
        // u64s. The write syscall and the index lock supply the ordering that
        // anything else depends on.
        let row = self.next_row.fetch_add(1, Ordering::Relaxed);

        if self.layout.has_checksum() {
            buf.push(row_checksum(&buf, row, self.layout.schema_fp));
        }

        self.write_all_at(&buf, self.layout.row_offset(row))?;
        if self.settings.sync == vault_sync::each_write {
            self.file.sync_data()?;
        }

        self.merge_into_index(row, entry);
        Ok(row)
    }

    /// Append a row setting only the named fields; everything else is absent.
    pub fn append_named(&self, values: &[(&str, BitString)]) -> io::Result<u64> {
        let mut entry: Vec<fixed_data> = self
            .layout
            .fields
            .iter()
            .map(|f| fixed_data::absent(&f.name))
            .collect();

        for (name, data) in values {
            match self.layout.by_name.get(*name) {
                Some(&slot) => {
                    entry[slot as usize].exists_in_entry = true;
                    entry[slot as usize].data = data.clone();
                }
                None => return Err(bad(format!("no field named '{name}' in this vault"))),
            }
        }
        self.append(&entry)
    }

    /// Force everything written so far down to the disk. Under
    /// [`vault_sync::each_write`] this is redundant; under
    /// [`vault_sync::on_flush`] it is the only thing that makes writes durable.
    pub fn flush(&self) -> io::Result<()> {
        if self.settings.read_only {
            return Ok(());
        }
        self.file.sync_data()
    }

    /// Fold a freshly written row into the "latest value per field" index.
    ///
    /// The comparison is on the row index, never on who reached the lock first:
    /// if thread B writes row 6 and merges before thread A merges row 5, A
    /// loses, because `5 > 6` is false. Absent fields are skipped entirely, so
    /// a later row leaving a field unset does not clear it.
    fn merge_into_index(&self, row: u64, entry: &[fixed_data]) {
        // A poisoned index is recoverable — at worst it is stale, and
        // rebuild_index repairs it — so one panicking thread must not brick
        // every subsequent read.
        let mut ix = self.index.write().unwrap_or_else(|e| e.into_inner());
        for (slot, e) in entry.iter().enumerate() {
            if !e.exists_in_entry {
                continue;
            }
            let slot = slot as u32;
            let wins = match ix.get(&slot) {
                Some(existing) => row > existing.row,
                None => true,
            };
            if wins {
                ix.insert(slot, fixed_data_slot { row, data: e.data.clone() });
            }
        }
    }

    // --- reading ---

    /// Read one row. `Ok(None)` means the row is past the end of the file, or
    /// is a hole (see [`fixed_data_vault::append`]). A row whose checksum
    /// doesn't match is an error, not a `None` — it means real bytes are wrong.
    pub fn read_row(&self, row: u64) -> io::Result<Option<fixed_row>> {
        let mut buf = vec![0u8; self.layout.row_bytes];
        let got = self.read_at(&mut buf, self.layout.row_offset(row))?;
        if got < self.layout.row_bytes {
            return Ok(None); // past the end, or a torn tail row
        }

        if self.layout.has_checksum() {
            let payload = &buf[..self.layout.payload_bytes];
            let stored = buf[self.layout.payload_bytes];
            if stored == 0 {
                return Ok(None); // reserved but never written
            }
            let want = row_checksum(payload, row, self.layout.schema_fp);
            if stored != want {
                return Err(bad(format!(
                    "row {row}: checksum {stored:#04x}, expected {want:#04x}"
                )));
            }
        }

        Ok(Some(self.layout.decode_row_payload(&buf[..self.layout.payload_bytes], row)))
    }

    /// Every row in the file, in index order, skipping holes. Corrupt rows are
    /// yielded as `Err` and do not stop the iteration.
    pub fn rows(&self) -> io::Result<fixed_row_iter<'_>> {
        Ok(fixed_row_iter { vault: self, next: 0, end: self.row_count()? })
    }

    /// Latest value written for a field, at its full declared width.
    /// `Err` means there is no such field; `Ok(None)` means it was never set.
    ///
    /// This reads the in-memory index, which lags the file by the width of one
    /// `merge_into_index` call: under concurrent appends a row can be durable
    /// before it is indexed. Call [`fixed_data_vault::rebuild_index`] first if
    /// you need a strictly current answer.
    pub fn latest(&self, field: &str) -> io::Result<Option<BitString>> {
        let slot = self
            .field_index(field)
            .ok_or_else(|| bad(format!("no field named '{field}' in this vault")))?;
        self.latest_at(slot)
    }

    pub fn latest_at(&self, slot: u32) -> io::Result<Option<BitString>> {
        if slot as usize >= self.layout.fields.len() {
            return Err(bad(format!(
                "field slot {slot} is out of range (vault has {} fields)",
                self.layout.fields.len()
            )));
        }
        let ix = self.index.read().unwrap_or_else(|e| e.into_inner());
        Ok(ix.get(&slot).map(|s| s.data.clone()))
    }

    pub fn latest_u64(&self, field: &str) -> io::Result<Option<u64>> {
        Ok(self.latest(field)?.and_then(|b| b.as_u64()))
    }

    /// Latest value of a text field, decoded through [`char_decompression`].
    /// Errors if the field wasn't declared with `isDataChar = true`.
    pub fn latest_text(&self, field: &str) -> io::Result<Option<String>> {
        let slot = self
            .field_index(field)
            .ok_or_else(|| bad(format!("no field named '{field}' in this vault")))?;
        if !self.layout.fields[slot as usize].is_text() {
            return Err(bad(format!("field '{field}' is not a text field")));
        }
        Ok(self
            .latest_at(slot)?
            .map(|b| fixed_data::present(field, b).as_text().unwrap_or_default()))
    }

    /// Which row supplied the current value of a field.
    pub fn latest_row_of(&self, field: &str) -> io::Result<Option<u64>> {
        let slot = self
            .field_index(field)
            .ok_or_else(|| bad(format!("no field named '{field}' in this vault")))?;
        let ix = self.index.read().unwrap_or_else(|e| e.into_inner());
        Ok(ix.get(&slot).map(|s| s.row))
    }

    /// Every field's current value as one row-shaped snapshot. Fields never
    /// written come back absent.
    pub fn snapshot(&self) -> Vec<fixed_data> {
        let ix = self.index.read().unwrap_or_else(|e| e.into_inner());
        self.layout
            .fields
            .iter()
            .enumerate()
            .map(|(i, f)| match ix.get(&(i as u32)) {
                Some(slot) => fixed_data::present(&f.name, slot.data.clone()),
                None => fixed_data::absent(&f.name),
            })
            .collect()
    }

    /// Rebuild the index by replaying every row on disk.
    ///
    /// Recovers the current state from durable bytes alone, so it survives a
    /// crash, and it is also the strict read for callers who can't tolerate the
    /// index lagging concurrent appends. Uses the same "highest row wins,
    /// absent never overwrites" rule as the incremental merge, so both paths
    /// converge on the same answer.
    ///
    /// Corrupt rows are counted and skipped rather than aborting the replay —
    /// one bad row shouldn't cost you the other million.
    pub fn rebuild_index(&self) -> io::Result<rebuild_report> {
        let rows = self.row_count()?;
        let mut report = rebuild_report { rows_scanned: rows, rows_holes: 0, rows_corrupt: 0 };
        let mut fresh: HashMap<u32, fixed_data_slot> = HashMap::new();

        // Read in batches so a large vault doesn't come through one syscall
        // per row, and doesn't land in memory all at once either.
        let batch_rows: u64 = (1 << 20) / self.layout.row_bytes.max(1) as u64;
        let batch_rows = batch_rows.max(1);
        let mut buf = vec![0u8; (batch_rows as usize) * self.layout.row_bytes];

        let mut row = 0u64;
        while row < rows {
            let want = batch_rows.min(rows - row) as usize;
            let span = want * self.layout.row_bytes;
            let got = self.read_at(&mut buf[..span], self.layout.row_offset(row))?;
            let usable = got / self.layout.row_bytes;

            for r in 0..usable {
                let at = r * self.layout.row_bytes;
                let raw = &buf[at..at + self.layout.row_bytes];
                let payload = &raw[..self.layout.payload_bytes];
                let this_row = row + r as u64;

                if self.layout.has_checksum() {
                    let stored = raw[self.layout.payload_bytes];
                    if stored == 0 {
                        report.rows_holes += 1;
                        continue;
                    }
                    if stored != row_checksum(payload, this_row, self.layout.schema_fp) {
                        report.rows_corrupt += 1;
                        continue;
                    }
                }

                let decoded = self.layout.decode_row_payload(payload, this_row);
                for (slot, value) in decoded.values.into_iter().enumerate() {
                    if !value.exists_in_entry {
                        continue;
                    }
                    let slot = slot as u32;
                    let wins = match fresh.get(&slot) {
                        Some(existing) => this_row > existing.row,
                        None => true,
                    };
                    if wins {
                        fresh.insert(slot, fixed_data_slot { row: this_row, data: value.data });
                    }
                }
            }

            if usable == 0 {
                break; // short read: nothing further to replay
            }
            row += usable as u64;
        }

        let mut ix = self.index.write().unwrap_or_else(|e| e.into_inner());
        *ix = fresh;
        Ok(report)
    }

    // --- migration ---

    /// Rewrite the vault into `new_layout`.
    ///
    /// Only an **append-only extension** is accepted: every existing field must
    /// still be there, at the same position, with the same name, width and
    /// type, and new fields come after. Renames, reorders, removals and width
    /// changes are refused — the format identifies a field by position and
    /// name and nothing else, so a rename is byte-for-byte identical to a
    /// delete-plus-add, and migrating one would mean guessing.
    ///
    /// Row indices are preserved. New fields read back absent on every existing
    /// row. Holes become real all-absent rows, so migration also cleans them up.
    ///
    /// # Why `&mut self`?
    /// This renames a new file over the old one. Concurrent appenders hold
    /// `&self` plus a *cached* header offset and stride; swapping the inode
    /// under them would send their in-flight writes into the old, unlinked file
    /// where they vanish without an error. `&mut self` makes that impossible at
    /// compile time — through an `Arc` you can only reach it via
    /// `Arc::get_mut`, which requires you to be the sole owner. Do not relax
    /// this to `&self`.
    pub fn migrate(&mut self, new_layout: fixed_data_init_list) -> io::Result<()> {
        if self.settings.read_only {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "vault is open read-only".to_string(),
            ));
        }

        let target = vault_layout::build(new_layout.data, self.layout.has_checksum())?;
        if self.layout.check_compatible(&target).is_ok() {
            return Ok(()); // already there
        }
        self.layout.check_extendable_to(&target)?;

        // Same directory: rename is only atomic within one filesystem.
        let tmp = self.path.with_extension(format!("migrate.{}.tmp", std::process::id()));
        let result = self.write_migrated(&target, &tmp);
        if result.is_err() {
            let _ = fs::remove_file(&tmp);
            return result;
        }

        fs::rename(&tmp, &self.path)?;
        // fsync the directory too, or the rename itself can be lost even
        // though the file it points at is safely on disk.
        if let Some(dir) = self.path.parent() {
            let dir = if dir.as_os_str().is_empty() { Path::new(".") } else { dir };
            if let Ok(handle) = File::open(dir) {
                let _ = handle.sync_all();
            }
        }

        self.file = open_vault_file(&self.path, false, false)?;
        self.layout = target;
        self.reset_next_row()?;
        self.rebuild_index()?;
        Ok(())
    }

    /// Stream every row into a fresh file laid out by `target` (in settings).
    fn write_migrated(&self, target: &vault_layout, tmp: &Path) -> io::Result<()> {
        // create_new: a leftover tmp from a crashed migration is an error to
        // look at, not something to silently clobber.
        let out = OpenOptions::new().read(true).write(true).create_new(true).open(tmp)?;
        write_all_at_raw(&out, &target.encode_header(), 0)?;

        let rows = self.row_count()?;
        let batch: u64 = ((1 << 20) / target.row_bytes.max(1) as u64).max(1);
        let mut staged = Vec::with_capacity((batch as usize) * target.row_bytes);

        let mut row = 0u64;
        while row < rows {
            let want = batch.min(rows - row);
            staged.clear();

            for r in 0..want {
                let this_row = row + r;
                // A hole or a corrupt row migrates to a clean all-absent row.
                let decoded = self.read_row(this_row).unwrap_or(None);
                let mut entry: Vec<fixed_data> = target
                    .fields
                    .iter()
                    .map(|f| fixed_data::absent(&f.name))
                    .collect();
                if let Some(old) = decoded {
                    for (slot, value) in old.values.into_iter().enumerate() {
                        if value.exists_in_entry {
                            entry[slot] = value;
                        }
                    }
                }

                let mut buf = target.encode_row_payload(&entry)?;
                if target.has_checksum() {
                    buf.push(row_checksum(&buf, this_row, target.schema_fp));
                }
                staged.extend_from_slice(&buf);
            }

            write_all_at_raw(&out, &staged, target.row_offset(row))?;
            row += want;
        }

        out.sync_all()
    }

    // --- positional io ---

    #[cfg(unix)]
    fn write_all_at(&self, buf: &[u8], off: u64) -> io::Result<()> {
        write_all_at_raw(&self.file, buf, off)
    }

    #[cfg(unix)]
    fn read_at(&self, buf: &mut [u8], off: u64) -> io::Result<usize> {
        read_at_raw(&self.file, buf, off)
    }

    // seek_read / seek_write move the handle's shared cursor, so one handle is
    // not concurrency-safe on Windows. Serialize them; darwin is the target.
    #[cfg(windows)]
    fn write_all_at(&self, buf: &[u8], off: u64) -> io::Result<()> {
        let _guard = self.win_io.lock().unwrap_or_else(|e| e.into_inner());
        write_all_at_raw(&self.file, buf, off)
    }

    #[cfg(windows)]
    fn read_at(&self, buf: &mut [u8], off: u64) -> io::Result<usize> {
        let _guard = self.win_io.lock().unwrap_or_else(|e| e.into_inner());
        read_at_raw(&self.file, buf, off)
    }
}

impl std::fmt::Debug for fixed_data_vault {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.debug_struct("fixed_data_vault")
            .field("path", &self.path)
            .field("fields", &self.layout.fields.len())
            .field("row_bytes", &self.layout.row_bytes)
            .field("checksum", &self.layout.has_checksum())
            .field("reserved_rows", &self.reserved_rows())
            .finish()
    }
}

impl std::fmt::Debug for fixed_data {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if self.exists_in_entry {
            write!(f, "{}={:?}", self.name, self.data)
        } else {
            write!(f, "{}=absent", self.name)
        }
    }
}

impl std::fmt::Debug for fixed_row {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "row {} {:?}", self.row, self.values)
    }
}

impl Drop for fixed_data_vault {
    /// Best-effort durability for `vault_sync::on_flush` callers who never got
    /// around to calling `flush`. Errors are swallowed — there is nowhere to
    /// report them from a drop.
    fn drop(&mut self) {
        if !self.settings.read_only {
            let _ = self.file.sync_data();
        }
    }
}

impl vault_layout {
    /// `Ok` when two layouts declare the same fields.
    ///
    /// Deliberately ignores `flags`: the row-checksum bit is a property of the
    /// file, not of the caller's settings, and the header always wins. Erroring
    /// on that difference would make one settings value unusable across vaults.
    fn check_compatible(&self, other: &vault_layout) -> io::Result<()> {
        if self.fields.len() != other.fields.len() {
            return Err(bad(format!(
                "schema mismatch: {} fields on disk, {} requested",
                self.fields.len(),
                other.fields.len()
            )));
        }
        self.check_prefix(other, self.fields.len())
    }

    /// `Ok` when `other` is `self` plus fields on the end.
    fn check_extendable_to(&self, other: &vault_layout) -> io::Result<()> {
        if other.fields.len() < self.fields.len() {
            return Err(bad(format!(
                "cannot migrate: {} fields on disk, {} requested — fields cannot be removed",
                self.fields.len(),
                other.fields.len()
            )));
        }
        self.check_prefix(other, self.fields.len())
    }

    fn check_prefix(&self, other: &vault_layout, upto: usize) -> io::Result<()> {
        for i in 0..upto {
            let a = &self.fields[i];
            let b = &other.fields[i];
            if a == b {
                continue;
            }
            let hint = if a.name != b.name {
                "; fields are identified by position and name, so a rename cannot be migrated automatically"
            } else {
                "; field widths and types cannot change"
            };
            return Err(bad(format!(
                "schema mismatch at field {i}: on disk '{}' ({} bits, {}), requested '{}' ({} bits, {}){hint}",
                a.name,
                a.data_bits,
                if a.is_text() { "text" } else { "raw" },
                b.name,
                b.data_bits,
                if b.is_text() { "text" } else { "raw" },
            )));
        }
        Ok(())
    }
}

/// Iterates rows in index order, skipping holes.
pub struct fixed_row_iter<'a> {
    vault: &'a fixed_data_vault,
    next: u64,
    end: u64,
}

impl<'a> Iterator for fixed_row_iter<'a> {
    type Item = io::Result<fixed_row>;

    fn next(&mut self) -> Option<Self::Item> {
        while self.next < self.end {
            let row = self.next;
            self.next += 1;
            match self.vault.read_row(row) {
                Ok(Some(r)) => return Some(Ok(r)),
                Ok(None) => continue, // hole
                Err(e) => return Some(Err(e)),
            }
        }
        None
    }
}

/// The one place a vault's file handle is configured.
///
/// **Never add `.append(true)` here.** On Linux, `pwrite` on a descriptor
/// opened `O_APPEND` ignores the offset and writes at the end of the file, so
/// every reserved row would pile up at EOF, land at the wrong index, and fail
/// its checksum — with no error from the write itself. macOS happens to honor
/// the offset instead, so the damage is invisible on this machine and shows up
/// only in production on Linux. `positional_writes_ignore_file_end` guards
/// this, but only on a platform that cares.
fn open_vault_file(path: &Path, read_only: bool, create: bool) -> io::Result<File> {
    OpenOptions::new()
        .read(true)
        .write(!read_only)
        .create(create && !read_only)
        .open(path)
}

// Free functions so migration can drive a file it doesn't own a vault for.

#[cfg(unix)]
fn write_all_at_raw(file: &File, buf: &[u8], off: u64) -> io::Result<()> {
    use std::os::unix::fs::FileExt;
    file.write_all_at(buf, off)
}

#[cfg(unix)]
fn read_at_raw(file: &File, buf: &mut [u8], off: u64) -> io::Result<usize> {
    use std::os::unix::fs::FileExt;
    let mut done = 0;
    while done < buf.len() {
        match file.read_at(&mut buf[done..], off + done as u64) {
            Ok(0) => break, // end of file
            Ok(n) => done += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(done)
}

#[cfg(windows)]
fn write_all_at_raw(file: &File, buf: &[u8], off: u64) -> io::Result<()> {
    use std::os::windows::fs::FileExt;
    let mut done = 0;
    while done < buf.len() {
        done += file.seek_write(&buf[done..], off + done as u64)?;
    }
    Ok(())
}

#[cfg(windows)]
fn read_at_raw(file: &File, buf: &mut [u8], off: u64) -> io::Result<usize> {
    use std::os::windows::fs::FileExt;
    let mut done = 0;
    while done < buf.len() {
        match file.seek_read(&mut buf[done..], off + done as u64) {
            Ok(0) => break,
            Ok(n) => done += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(done)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    /// A vault path that is unique per test, per process, and per run, and that
    /// cleans itself up even when an assertion panics. The old fixed filename
    /// made parallel tests collide and leaked the file on failure.
    struct temp_vault(PathBuf);

    impl temp_vault {
        fn new(tag: &str) -> Self {
            static N: AtomicU64 = AtomicU64::new(0);
            let n = N.fetch_add(1, Ordering::Relaxed);
            let ns = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            temp_vault(std::env::temp_dir().join(format!(
                "bstd_vault_{tag}_{}_{n}_{ns}.bin",
                std::process::id()
            )))
        }
        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for temp_vault {
        fn drop(&mut self) {
            let _ = fs::remove_file(&self.0);
        }
    }

    fn num(n: u64, width: usize) -> BitString {
        let mut b = BitString::new();
        b.set_as_u64_width(n, width);
        b
    }

    fn ab_layout() -> fixed_data_init_list {
        let mut l = fixed_data_init_list::new();
        l.add("a", false, 8).add("b", false, 8);
        l
    }

    fn fast() -> fixedVaultSettings {
        let mut s = fixedVaultSettings::new();
        s.sync(vault_sync::on_flush);
        s
    }

    // -- send + sync ------------------------------------------------------

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn vault_is_send_sync() {
        assert_send_sync::<fixed_data_vault>();
    }

    // -- the invariant every row offset depends on -------------------------

    // Rewriting an earlier offset must not append. This is the whole basis of
    // reserving a row index and writing to `header + i * stride`.
    //
    // The failure mode it guards is `.append(true)` creeping back into
    // open_vault_file: on Linux, pwrite on an O_APPEND descriptor ignores the
    // offset entirely. macOS honors the offset either way, so on this machine
    // the test passes regardless -- it earns its keep on Linux, and the comment
    // on open_vault_file is what carries the warning here.
    #[test]
    fn positional_writes_ignore_file_end() {
        let t = temp_vault::new("positional");
        let f = open_vault_file(t.path(), false, true).unwrap();
        write_all_at_raw(&f, b"AAAA", 0).unwrap();
        write_all_at_raw(&f, b"BBBB", 0).unwrap();

        let got = fs::read(t.path()).unwrap();
        assert_eq!(got.len(), 4, "second write appended instead of overwriting");
        assert_eq!(&got, b"BBBB");

        write_all_at_raw(&f, b"CC", 8).unwrap(); // past the end: leaves a gap
        let got = fs::read(t.path()).unwrap();
        assert_eq!(got, b"BBBB\0\0\0\0CC", "gap reads back as zeros, as holes rely on");
    }

    // -- header -----------------------------------------------------------

    // Create, drop, reopen: every header field has to survive the round trip,
    // because all row arithmetic is derived from it.
    #[test]
    fn header_round_trips() {
        let t = temp_vault::new("header");
        let (len, stride, fp) = {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            assert_eq!(v.field_count(), 2);
            assert_eq!(v.fields()[0].name(), "a");
            assert_eq!(v.fields()[0].data_bits(), 8);
            assert!(!v.fields()[0].is_text());
            assert!(v.has_row_checksum());
            // 2 presence + 16 data = 18 bits -> 3 payload bytes + 1 checksum
            assert_eq!(v.row_bytes(), 4);
            (v.header_len(), v.row_bytes(), v.schema_fingerprint())
        };

        let v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();
        assert_eq!(v.header_len(), len);
        assert_eq!(v.row_bytes(), stride);
        assert_eq!(v.schema_fingerprint(), fp);
        assert_eq!(v.field_index("b"), Some(1));
        assert_eq!(v.field_index("nope"), None);
    }

    #[test]
    fn rejects_headerless_file() {
        let t = temp_vault::new("headerless");
        fs::write(t.path(), vec![0u8; 64]).unwrap();
        let err = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
        assert!(err.to_string().contains("magic"), "got: {err}");
    }

    #[test]
    fn rejects_truncated_header() {
        let t = temp_vault::new("truncated");
        fs::write(t.path(), vec![0xAAu8; 5]).unwrap();
        let err = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    // Every shape of layout change has to be caught by name, not silently
    // reinterpreted at the wrong stride -- that was the original bug.
    #[test]
    fn rejects_schema_mismatch_by_default() {
        let t = temp_vault::new("mismatch");
        fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();

        let mut widened = fixed_data_init_list::new();
        widened.add("a", false, 16).add("b", false, 8);
        let err = fixed_data_vault::open(t.path(), widened, fast()).unwrap_err();
        assert!(err.to_string().contains("field 0"), "got: {err}");

        let mut renamed = fixed_data_init_list::new();
        renamed.add("a", false, 8).add("bb", false, 8);
        let err = fixed_data_vault::open(t.path(), renamed, fast()).unwrap_err();
        assert!(err.to_string().contains("rename"), "got: {err}");

        let mut removed = fixed_data_init_list::new();
        removed.add("a", false, 8);
        let err = fixed_data_vault::open(t.path(), removed, fast()).unwrap_err();
        assert!(err.to_string().contains("2 fields on disk"), "got: {err}");

        let mut added = fixed_data_init_list::new();
        added.add("a", false, 8).add("b", false, 8).add("c", false, 16);
        let err = fixed_data_vault::open(t.path(), added, fast()).unwrap_err();
        assert!(err.to_string().contains("3 requested"), "got: {err}");
    }

    #[test]
    fn rejects_bad_layouts() {
        let t = temp_vault::new("badlayout");
        let mut dup = fixed_data_init_list::new();
        dup.add("a", false, 8).add("a", false, 8);
        assert!(fixed_data_vault::create(t.path(), dup, fast()).is_err());

        let mut zero = fixed_data_init_list::new();
        zero.add("a", false, 0);
        assert!(fixed_data_vault::create(t.path(), zero, fast()).is_err());

        let empty = fixed_data_init_list::new();
        assert!(fixed_data_vault::create(t.path(), empty, fast()).is_err());
    }

    // -- the old defects --------------------------------------------------

    // V2 trimmed values on read, so a stored 0 came back as an empty BitString,
    // indistinguishable from a field that was never written.
    #[test]
    fn zero_is_not_absent() {
        let t = temp_vault::new("zero");
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            v.append(&[
                fixed_data::present("a", num(0, 8)),
                fixed_data::absent("b"),
            ])
            .unwrap();
        }
        let v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();

        let a = v.latest("a").unwrap().expect("a was written, even though it is 0");
        assert_eq!(a.len(), 8, "value keeps its declared width");
        assert_eq!(a.as_u64(), Some(0));
        assert_eq!(v.latest("b").unwrap(), None, "b was never written");
    }

    // V2 silently dropped the high bits of an oversized value.
    #[test]
    fn too_wide_value_errors() {
        let t = temp_vault::new("toowide");
        let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();

        let err = v
            .append(&[
                fixed_data::present("a", num(256, 9)),
                fixed_data::absent("b"),
            ])
            .unwrap_err();
        assert!(err.to_string().contains("needs 9 bits"), "got: {err}");

        // Width is measured by the highest set bit, so a wide-but-small value
        // is still fine.
        let mut wide_five = BitString::new();
        wide_five.set_as_number(5);
        assert_eq!(wide_five.len(), 32);
        v.append(&[
            fixed_data::present("a", wide_five),
            fixed_data::absent("b"),
        ])
        .unwrap();
        assert_eq!(v.latest_u64("a").unwrap(), Some(5));
    }

    #[test]
    fn rejects_wrong_arity_and_names() {
        let t = temp_vault::new("arity");
        let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();

        let err = v.append(&[fixed_data::absent("a")]).unwrap_err();
        assert!(err.to_string().contains("expected 2 entries"), "got: {err}");

        let err = v
            .append(&[fixed_data::absent("a"), fixed_data::absent("z")])
            .unwrap_err();
        assert!(err.to_string().contains("name mismatch at 1"), "got: {err}");

        assert!(v.append_named(&[("nope", num(1, 8))]).is_err());
    }

    // The V2 test, rewritten against the new API. A later row that leaves a
    // field absent must not clear the value an earlier row set.
    #[test]
    fn latest_keeps_earlier_when_absent() {
        let t = temp_vault::new("latest");
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            v.append(&[
                fixed_data::present("a", num(5, 8)),
                fixed_data::present("b", num(200, 8)),
            ])
            .unwrap();
            v.append(&[
                fixed_data::present("a", num(9, 8)),
                fixed_data::absent("b"),
            ])
            .unwrap();
        } // dropped, simulating a restart

        let v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();
        assert_eq!(v.latest("a").unwrap(), Some(num(9, 8)), "a = latest present");
        assert_eq!(v.latest("b").unwrap(), Some(num(200, 8)), "b = kept from row 0");
        assert_eq!(v.latest_row_of("a").unwrap(), Some(1));
        assert_eq!(v.latest_row_of("b").unwrap(), Some(0));

        let snap = v.snapshot();
        assert_eq!(snap[0].as_u64(), Some(9));
        assert_eq!(snap[1].as_u64(), Some(200));
    }

    // Regression for the if/match fall-through that made every letter encode
    // as 31, and for the trim that made 'a' encode as nothing at all.
    #[test]
    fn char_compression_is_five_bits_and_distinct() {
        let mut seen = HashMap::new();
        for c in 'a'..='z' {
            let bits = char_compression(c);
            assert_eq!(bits.len(), 5, "{c} must occupy exactly 5 bits");
            let code = bits.as_u64().unwrap();
            assert!(seen.insert(code, c).is_none(), "{c} collides with {:?}", seen.get(&code));
            assert_eq!(char_decompression(code as u32), c);
        }
        assert_eq!(char_compression('a').as_u64(), Some(0));
        assert_eq!(char_compression('z').as_u64(), Some(25));
        assert_eq!(char_compression('#').as_u64(), Some(30));
        assert_eq!(char_compression('~').as_u64(), Some(31), "unmapped falls back to ?");
        assert_eq!(char_decompression(31), '?');
    }

    #[test]
    fn text_field_round_trips() {
        let t = temp_vault::new("text");
        let mut l = fixed_data_init_list::new();
        l.add("label", true, 20); // 4 chars at 5 bits

        let mut bits = BitString::new();
        for c in "kite".chars() {
            bits.append(&char_compression(c));
        }

        let v = fixed_data_vault::create(t.path(), l, fast()).unwrap();
        v.append(&[fixed_data::present("label", bits)]).unwrap();
        assert_eq!(v.latest_text("label").unwrap(), Some("kite".to_string()));
    }

    // -- rows -------------------------------------------------------------

    #[test]
    fn read_row_and_iterate() {
        let t = temp_vault::new("rows");
        let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
        for i in 0..10u64 {
            let row = v
                .append(&[
                    fixed_data::present("a", num(i, 8)),
                    fixed_data::present("b", num(100 + i, 8)),
                ])
                .unwrap();
            assert_eq!(row, i, "rows are handed out in order for one thread");
        }

        assert_eq!(v.row_count().unwrap(), 10);
        assert_eq!(v.reserved_rows(), 10);

        let all: Vec<fixed_row> = v.rows().unwrap().map(|r| r.unwrap()).collect();
        assert_eq!(all.len(), 10);
        for (i, r) in all.iter().enumerate() {
            assert_eq!(r.row, i as u64);
            assert_eq!(r.get("a").unwrap().as_u64(), Some(i as u64));
            assert_eq!(r.at(1).unwrap().as_u64(), Some(100 + i as u64));
        }

        assert!(v.read_row(10).unwrap().is_none(), "past the end");
    }

    // A row reserved but never written stays zero-filled. The checksum byte
    // 0x00 is a value row_checksum can never produce, so it is identifiable.
    #[test]
    fn hole_is_skipped() {
        let t = temp_vault::new("hole");
        let (header, stride) = {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            for i in 0..3u64 {
                v.append(&[
                    fixed_data::present("a", num(i + 1, 8)),
                    fixed_data::absent("b"),
                ])
                .unwrap();
            }
            (v.header_len(), v.row_bytes())
        };

        // Zero out row 1, as if its writer had died after reserving it.
        {
            let f = OpenOptions::new().write(true).open(t.path()).unwrap();
            write_all_at_raw(&f, &vec![0u8; stride], header + stride as u64).unwrap();
            f.sync_all().unwrap();
        }

        let v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();
        assert_eq!(v.row_count().unwrap(), 3);
        assert!(v.read_row(1).unwrap().is_none(), "row 1 is a hole");
        assert!(v.read_row(0).unwrap().is_some());
        assert!(v.read_row(2).unwrap().is_some());

        let seen: Vec<u64> = v.rows().unwrap().map(|r| r.unwrap().row).collect();
        assert_eq!(seen, vec![0, 2], "iteration skips the hole");

        let report = v.rebuild_index().unwrap();
        assert_eq!(report.rows_scanned, 3);
        assert_eq!(report.rows_holes, 1);
        assert_eq!(report.rows_corrupt, 0);
        assert_eq!(v.latest_u64("a").unwrap(), Some(3), "row 2 still wins");
    }

    #[test]
    fn checksum_detects_torn_row() {
        let t = temp_vault::new("torn");
        let (header, stride) = {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            for i in 0..3u64 {
                v.append(&[
                    fixed_data::present("a", num(i + 1, 8)),
                    fixed_data::absent("b"),
                ])
                .unwrap();
            }
            (v.header_len(), v.row_bytes())
        };

        // Corrupt a payload byte of row 1 without touching its checksum.
        {
            let f = OpenOptions::new().read(true).write(true).open(t.path()).unwrap();
            let off = header + stride as u64;
            let mut byte = [0u8; 1];
            read_at_raw(&f, &mut byte, off).unwrap();
            byte[0] ^= 0b0100_0000;
            write_all_at_raw(&f, &byte, off).unwrap();
            f.sync_all().unwrap();
        }

        let mut s = fast();
        s.rebuild_index_on_open(false);
        let v = fixed_data_vault::open(t.path(), ab_layout(), s).unwrap();

        let err = v.read_row(1).unwrap_err();
        assert!(err.to_string().contains("checksum"), "got: {err}");

        let report = v.rebuild_index().unwrap();
        assert_eq!(report.rows_corrupt, 1, "counted and skipped, not fatal");
        assert_eq!(report.rows_scanned, 3);
        assert_eq!(v.latest_u64("a").unwrap(), Some(3), "replay continued past it");
    }

    #[test]
    fn checksum_can_be_turned_off_at_creation() {
        let t = temp_vault::new("nocsum");
        let mut s = fast();
        s.row_checksum(false);
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), s.clone()).unwrap();
            assert!(!v.has_row_checksum());
            assert_eq!(v.row_bytes(), 3, "no checksum byte in the stride");
            v.append(&[
                fixed_data::present("a", num(7, 8)),
                fixed_data::absent("b"),
            ])
            .unwrap();
        }

        // The header wins over the setting on an existing file: reopening with
        // checksums requested must not change the stride.
        let v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();
        assert!(!v.has_row_checksum(), "header is authoritative");
        assert_eq!(v.row_bytes(), 3);
        assert_eq!(v.latest_u64("a").unwrap(), Some(7));
    }

    // -- concurrency
    // The point of the whole rewrite: many threads appending through &self,
    // each landing on its own row, with no lock across the write.
    #[test]
    fn concurrent_appends_reserve_unique_rows() {
        let t = temp_vault::new("concurrent");
        let v = Arc::new(fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap());

        let threads = 8u64;
        let per = 500u64;
        let mut handles = Vec::new();
        for _ in 0..threads {
            let v = Arc::clone(&v);
            handles.push(std::thread::spawn(move || {
                let mut mine = Vec::with_capacity(per as usize);
                for i in 0..per {
                    mine.push(
                        v.append(&[
                            fixed_data::present("a", num(i % 256, 8)),
                            fixed_data::absent("b"),
                        ])
                        .unwrap(),
                    );
                }
                mine
            }));
        }

        let mut rows: Vec<u64> = handles.into_iter().flat_map(|h| h.join().unwrap()).collect();
        rows.sort_unstable();
        let total = threads * per;
        assert_eq!(rows.len() as u64, total);
        assert_eq!(rows, (0..total).collect::<Vec<u64>>(), "every row index handed out exactly once");

        v.flush().unwrap();
        assert_eq!(v.row_count().unwrap(), total);
        let expected_len = v.header_len() + total * v.row_bytes() as u64;
        assert_eq!(fs::metadata(t.path()).unwrap().len(), expected_len);

        // No holes and no corruption: every reserved row actually landed.
        let report = v.rebuild_index().unwrap();
        assert_eq!(report.rows_scanned, total);
        assert_eq!(report.rows_holes, 0);
        assert_eq!(report.rows_corrupt, 0);
    }

    // The ordering rule itself, driven deterministically: row 6 merges first,
    // then row 5 arrives late. Under concurrent appends this interleaving is
    // real but rare, so asserting it through actual threads is luck. Here the
    // merges are forced out of order, so a comparator that just takes the last
    // writer fails every time.
    #[test]
    fn index_merge_prefers_higher_row_regardless_of_merge_order() {
        let t = temp_vault::new("mergeorder");
        let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();

        v.merge_into_index(
            6,
            &[fixed_data::present("a", num(66, 8)), fixed_data::absent("b")],
        );
        v.merge_into_index(
            5,
            &[fixed_data::present("a", num(55, 8)), fixed_data::present("b", num(5, 8))],
        );

        assert_eq!(v.latest_row_of("a").unwrap(), Some(6), "the later row keeps the field");
        assert_eq!(v.latest_u64("a").unwrap(), Some(66));
        // Row 6 left b absent, so row 5's value for it stands -- the rule is
        // per field, not per row.
        assert_eq!(v.latest_row_of("b").unwrap(), Some(5));
        assert_eq!(v.latest_u64("b").unwrap(), Some(5));

        // An equal row index must not overwrite either, or a replay that
        // revisits a row could flip a value that is already correct.
        v.merge_into_index(
            6,
            &[fixed_data::present("a", num(99, 8)), fixed_data::absent("b")],
        );
        assert_eq!(v.latest_u64("a").unwrap(), Some(66), "strictly greater wins");
    }

    // Row 6 must beat row 5 regardless of which thread reaches the index lock
    // first. Asserted by checking the in-memory index against the last row on
    // disk, and against a full replay.
    #[test]
    fn concurrent_index_ordering_last_row_wins() {
        for attempt in 0..25u64 {
            let t = temp_vault::new("ordering");
            let v = Arc::new(fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap());

            let mut handles = Vec::new();
            for thread in 0..8u64 {
                let v = Arc::clone(&v);
                handles.push(std::thread::spawn(move || {
                    for i in 0..40u64 {
                        v.append(&[
                            fixed_data::present("a", num((thread * 40 + i) % 256, 8)),
                            fixed_data::absent("b"),
                        ])
                        .unwrap();
                    }
                }));
            }
            for h in handles {
                h.join().unwrap();
            }

            let last = v.row_count().unwrap() - 1;
            assert_eq!(
                v.latest_row_of("a").unwrap(),
                Some(last),
                "attempt {attempt}: index must point at the highest row"
            );

            let on_disk = v.read_row(last).unwrap().unwrap();
            assert_eq!(
                v.latest("a").unwrap(),
                Some(on_disk.get("a").unwrap().data.clone()),
                "attempt {attempt}: index value must match the last row on disk"
            );

            // Incremental merge and full replay must converge.
            let live = v.latest("a").unwrap();
            v.rebuild_index().unwrap();
            assert_eq!(v.latest("a").unwrap(), live, "attempt {attempt}: replay disagrees");
            assert_eq!(v.latest_row_of("a").unwrap(), Some(last));
        }
    }

    #[test]
    fn concurrent_reads_during_appends() {
        let t = temp_vault::new("readwrite");
        let v = Arc::new(fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap());

        let writer = {
            let v = Arc::clone(&v);
            std::thread::spawn(move || {
                for i in 0..2000u64 {
                    v.append(&[
                        fixed_data::present("a", num(i % 256, 8)),
                        fixed_data::absent("b"),
                    ])
                    .unwrap();
                }
            })
        };

        let reader = {
            let v = Arc::clone(&v);
            std::thread::spawn(move || {
                for _ in 0..2000 {
                    // Reads must never see a half-written row: a row is one
                    // write_at, and its checksum guards the rest.
                    let count = v.row_count().unwrap();
                    if count > 0 {
                        v.read_row(count - 1).unwrap();
                    }
                    let _ = v.latest("a").unwrap();
                }
            })
        };

        writer.join().unwrap();
        reader.join().unwrap();
        assert_eq!(v.row_count().unwrap(), 2000);
    }





    //migration and tests

    #[test]
    fn migration_round_trip() {
        let t = temp_vault::new("migrate");
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            for i in 0..50u64 {
                v.append(&[
                    fixed_data::present("a", num(i, 8)),
                    fixed_data::present("b", num(200 - i, 8)),
                ])
                .unwrap();
            }
        }

        let mut extended = fixed_data_init_list::new();
        extended.add("a", false, 8).add("b", false, 8).add("c", false, 16);

        let mut v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();
        let old_stride = v.row_bytes();
        let old_fp = v.schema_fingerprint();
        v.migrate(extended).unwrap();

        assert_ne!(v.schema_fingerprint(), old_fp, "fingerprint tracks the new layout");
        assert_ne!(v.row_bytes(), old_stride, "stride widened");
        assert_eq!(v.row_count().unwrap(), 50, "no rows gained or lost");
        assert_eq!(v.field_count(), 3);

        for i in 0..50u64 {
            let r = v.read_row(i).unwrap().expect("row survived at its original index");
            assert_eq!(r.get("a").unwrap().as_u64(), Some(i), "row {i} field a");
            assert_eq!(r.get("b").unwrap().as_u64(), Some(200 - i), "row {i} field b");
            assert!(!r.get("c").unwrap().exists_in_entry, "row {i} field c is absent");
        }
        assert_eq!(v.latest_u64("a").unwrap(), Some(49));
        assert_eq!(v.latest("c").unwrap(), None);

        // The tmp file is gone and appends now use the new stride.
        let leftovers: Vec<_> = fs::read_dir(t.path().parent().unwrap())
            .unwrap()
            .filter_map(|e| e.ok())
            .map(|e| e.file_name().to_string_lossy().to_string())
            .filter(|n| n.contains("migrate.") && n.ends_with(".tmp"))
            .collect();
        assert!(leftovers.is_empty(), "left behind: {leftovers:?}");

        let row = v
            .append(&[
                fixed_data::present("a", num(7, 8)),
                fixed_data::absent("b"),
                fixed_data::present("c", num(60000, 16)),
            ])
            .unwrap();
        assert_eq!(row, 50);
        assert_eq!(v.latest_u64("c").unwrap(), Some(60000));
    }

    // The auto-migrate policy runs the same routine from inside open().
    #[test]
    fn migrate_policy_extends_on_open() {
        let t = temp_vault::new("automigrate");
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            v.append(&[
                fixed_data::present("a", num(3, 8)),
                fixed_data::present("b", num(4, 8)),
            ])
            .unwrap();
        }

        let mut extended = fixed_data_init_list::new();
        extended.add("a", false, 8).add("b", false, 8).add("c", false, 16);
        let mut s = fast();
        s.on_schema_mismatch(schema_policy::migrate);

        let v = fixed_data_vault::open(t.path(), extended, s).unwrap();
        assert_eq!(v.field_count(), 3);
        assert_eq!(v.latest_u64("a").unwrap(), Some(3));
        assert_eq!(v.latest_u64("b").unwrap(), Some(4));
        assert_eq!(v.latest("c").unwrap(), None);
    }

    #[test]
    fn migration_rejects_non_prefix() {
        let t = temp_vault::new("badmigrate");
        fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
        let mut v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();

        let mut renamed = fixed_data_init_list::new();
        renamed.add("a", false, 8).add("bb", false, 8).add("c", false, 8);
        let err = v.migrate(renamed).unwrap_err();
        assert!(err.to_string().contains("rename"), "got: {err}");

        let mut resized = fixed_data_init_list::new();
        resized.add("a", false, 16).add("b", false, 8).add("c", false, 8);
        let err = v.migrate(resized).unwrap_err();
        assert!(err.to_string().contains("widths and types"), "got: {err}");

        let mut removed = fixed_data_init_list::new();
        removed.add("a", false, 8);
        let err = v.migrate(removed).unwrap_err();
        assert!(err.to_string().contains("cannot be removed"), "got: {err}");

        let mut reordered = fixed_data_init_list::new();
        reordered.add("b", false, 8).add("a", false, 8).add("c", false, 8);
        assert!(v.migrate(reordered).is_err());

        assert_eq!(v.field_count(), 2, "nothing changed on disk");
    }

    // migrate takes &mut self so the borrow checker enforces exclusive access:
    // renaming a new file over the old one while other threads hold a cached
    // header offset would send their writes into the unlinked inode.
    #[test]
    fn migrate_requires_exclusive_access() {
        let t = temp_vault::new("exclusive");
        let mut v = Arc::new(fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap());

        let shared = Arc::clone(&v);
        assert!(Arc::get_mut(&mut v).is_none(), "cannot migrate while shared");
        drop(shared);
        assert!(Arc::get_mut(&mut v).is_some(), "sole owner can migrate");
    }

    // -- settings ---------------------------------------------------------

    #[test]
    fn read_only_rejects_append() {
        let t = temp_vault::new("readonly");
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            v.append(&[
                fixed_data::present("a", num(11, 8)),
                fixed_data::absent("b"),
            ])
            .unwrap();
        }

        let mut s = fast();
        s.read_only(true);
        let mut v = fixed_data_vault::open(t.path(), ab_layout(), s).unwrap();
        assert_eq!(v.latest_u64("a").unwrap(), Some(11), "reads still work");

        let err = v
            .append(&[fixed_data::absent("a"), fixed_data::absent("b")])
            .unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::PermissionDenied);

        let mut extended = fixed_data_init_list::new();
        extended.add("a", false, 8).add("b", false, 8).add("c", false, 8);
        assert_eq!(
            v.migrate(extended).unwrap_err().kind(),
            io::ErrorKind::PermissionDenied
        );
    }

    #[test]
    fn create_if_missing_false_errors() {
        let t = temp_vault::new("nocreate");
        let mut s = fast();
        s.create_if_missing(false);
        let err = fixed_data_vault::open(t.path(), ab_layout(), s).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::NotFound);
        assert!(!t.path().try_exists().unwrap(), "nothing was created");
    }

    #[test]
    fn on_flush_policy_is_durable_after_flush() {
        let t = temp_vault::new("onflush");
        let mut s = fixedVaultSettings::new();
        s.sync(vault_sync::on_flush);
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), s).unwrap();
            v.append(&[
                fixed_data::present("a", num(42, 8)),
                fixed_data::absent("b"),
            ])
            .unwrap();
            v.flush().unwrap();
        }
        let v = fixed_data_vault::open(t.path(), ab_layout(), fast()).unwrap();
        assert_eq!(v.latest_u64("a").unwrap(), Some(42));
    }

    #[test]
    fn skipping_index_rebuild_leaves_it_empty() {
        let t = temp_vault::new("noindex");
        {
            let v = fixed_data_vault::create(t.path(), ab_layout(), fast()).unwrap();
            v.append(&[
                fixed_data::present("a", num(8, 8)),
                fixed_data::absent("b"),
            ])
            .unwrap();
        }

        let mut s = fast();
        s.rebuild_index_on_open(false);
        let v = fixed_data_vault::open(t.path(), ab_layout(), s).unwrap();
        assert_eq!(v.latest("a").unwrap(), None, "index not populated");
        assert_eq!(v.row_count().unwrap(), 1, "rows are still readable");
        v.rebuild_index().unwrap();
        assert_eq!(v.latest_u64("a").unwrap(), Some(8));
    }
}
