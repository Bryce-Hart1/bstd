#![allow(unused)]




pub struct parser{
    _data: Vec<String>,
}


impl parser{
    

    /**
     * Parse a new fresh input, `x`, and add it to accumulated data.
     * 
     * `x` - input string slice
     * `by` - what the string slice will be parsed by, usually ` `.
     */
    pub fn parse_new_slice(mut self, x: &str, by: char){
        let parts: Vec<String> = x.split(by)
        .map(|x| x.to_string()).collect(); //map parts to a string
        self._data.extend(parts); //consume parts
    }

    /**
     * Find all parsed pieces with this parameter, `x`.
     */
    pub fn find_all_with(self, x: char) -> Vec<String>{
        let mut r: Vec<String> = Vec::new();
        for s in self._data{
            if(s.contains(x)){
                r.push(s);
            }
        }
        return r;
    }

    fn _private_is_next(&self, next: Option<&String>, find_order: Vec<String>, nextItr: usize, findItr: usize) -> bool{
        let find: Option<&String> = find_order.get(findItr);
        if find.is_none(){
            return true;
        }
        if next.is_none(){
            return false;
        }
        if next.unwrap() == find.unwrap(){
            return self._private_is_next(
                self._data.get(nextItr+1),
                find_order, nextItr, findItr+1); //found, move on
        }else{
            return self._private_is_next(
                self._data.get(nextItr+1),
                find_order, nextItr, 0); //does not equal, restart
        }
        
        return true;
    }

    /**
     * Find out if the list of strings `find_order` exists in parser
     * 
     * `param`:`find_order` a list of strings to be found in list
     * 
     * `returns` - true if `find_order` exists, false if not.
     */
    pub fn has_order(&self, find_order: Vec<String>) -> bool{
        return self._private_is_next(self._data.get(0), find_order,0, 0);
    }
    







}


