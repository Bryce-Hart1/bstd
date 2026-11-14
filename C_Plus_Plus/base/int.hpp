#pragma once
#include <atomic>
#include <climits>
#include <cstdint>

class Int{
    constexpr static int MAX = INT_MAX;
    constexpr static int MIN = INT_MIN;

    private:
    std::atomic_int32_t _val;

    explicit Int(int value){

    }

    //flip bit number `bit_ind` from left to right
    void flip(const unsigned char bit_ind) noexcept{
        int 
        _val.exchange(__int_type i)
    }

    void operator=(int value){
        _val.exchange(value, std::memory_order_seq_cst);
    }
    

};