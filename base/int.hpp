#pragma once
#include <atomic>
#include <climits>
#include <cstdint>

namespace bstd{
/**
* simple wrapper around int. use cases is when adding bit types. 
* not yet completed.
 */
class Int{
    constexpr static int MAX = INT_MAX;
    constexpr static int MIN = INT_MIN;

    private:
    std::atomic_int32_t _val;

    public:
    explicit Int(int value){}

    //flip bit number `bit_ind` from left to right
    void flip(const unsigned char bit_ind) noexcept{
        
    }

    void operator=(int value){
        _val.exchange(value, std::memory_order_seq_cst);
    }
    

};
};