#![allow(unused, non_snake_case, non_ascii_idents, non_camel_case_types)]

mod simpleHash;
mod fixedDataStorage;
mod bits;

use std::{collections::HashMap};

/**
 * test functions found in src
 * 
 * 
 */

fn simplehash_test_helper(inc: &str) -> String {
    let mut n = i32::from_str_radix(inc, 2).unwrap();
    n += 1;
    format!("{:08b}", n) // Pad to 8 bits to maintain consistency
}

fn test_simplehash_str() -> bool {
    let mut map: HashMap<u32, bool> = HashMap::new();
    let mut bin_str = "00000000".to_string();
    
    for n in 0..256 {
        let encoded = simpleHash::str(&bin_str);
        
        if map.contains_key(&encoded) {
            println!("Collision at iteration {}: hash value {}", n, encoded);
            return false;
        }
        
        map.insert(encoded, true);
        
        if n < 255 { //last: 
            bin_str = simplehash_test_helper(&bin_str);
        }
    }
    
    true
}


fn main(){
    print!("simplehash_str: ");
    if !test_simplehash_str(){
        println!("failed.")
    }else{
        println!("passed. ")
    }
}
