#pragma once
#include <bitset>
#include <cmath>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace bstd{
namespace store{
namespace details{

inline std::vector<unsigned char> convertToUCvec(const std::vector<std::bitset<8>>& bytes){
    std::vector<unsigned char> r;
    for(const std::bitset<8> byte : bytes){
        r.push_back(static_cast<unsigned char>(byte.to_ulong()));
    }
    return r;
}
}


using signedTypes = std::variant<int8_t, int16_t, int32_t, int64_t>;

/**
* Reads `bytesLE` as a little endian two's complement integer.
* The result type is the smallest of int8/16/32/64 that holds the byte count
* (1, 2, 3-4 or 5-8 bytes); shorter inputs are sign extended from their top byte.
* @returns nullopt for an empty vector or more than 8 bytes.
*/
inline std::optional<signedTypes> convertToSignedIntegral(const std::vector<unsigned char>& bytesLE){
    const std::size_t size = bytesLE.size();
    if(size == 0 || size > 8){
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for(std::size_t i = 0; i < size; i++){
        value |= static_cast<std::uint64_t>(bytesLE[i]) << (8 * i);
    }
    //sign extend from the top bit of the last supplied byte
    const unsigned bits = static_cast<unsigned>(size * 8);
    if(bits < 64 && (value & (std::uint64_t(1) << (bits - 1)))){
        value |= ~std::uint64_t(0) << bits;
    }
    const std::int64_t wide = static_cast<std::int64_t>(value);
    if(size == 1){
        return static_cast<int8_t>(wide);
    }
    if(size == 2){
        return static_cast<int16_t>(wide);
    }
    if(size <= 4){
        return static_cast<int32_t>(wide);
    }
    return wide;
}

}
}
