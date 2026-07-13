use std::iter::Map;
use std::sync::atomic::{AtomicBool, Ordering};
use std::collections::{HashMap, hash_map};
use std::vec;



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
    fn add(){
        
    }
}

struct fixed_data{
    exists_in_entry: bool,
    name: String,
}


pub struct fixed_data_vault{
    atom_is_reading: AtomicBool,
    total_size_in_bits: usize,
    where_is: HashMap<u32, fixed_data>,
    expect: Vec<fixed_data_init>,

}

impl fixed_data_vault{
    fn new(_data: Vec<fixed_data_init>) -> Self{
        let flag = AtomicBool::new(false);
        let size: usize = 0;
        let map: HashMap<u32, fixed_data> = HashMap::new();
        let entry: Vec<fixed_data_init> = Vec::new();

        return fixed_data_vault{
            atom_is_reading: flag,
            total_size_in_bits: size,
            where_is: map,
            expect: entry,
        };
    }

    /// Add a new entry to the vault
    /// Expects all with the same name, 
    pub fn add(entry: Vec<fixed_data>){


    }
}