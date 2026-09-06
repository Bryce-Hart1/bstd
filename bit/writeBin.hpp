#pragma once
#include <cstdint>
#include <fstream>
#include <vector>
#include <concepts>
#include <stdexcept>
#include <bit>
#include <array>

namespace bstd{
namespace bit{

// Swap byte order of an integral value.
template<std::integral T>
T byteswap(T value){
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
    for(size_t i = 0; i < sizeof(T) / 2; ++i)
        std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
    return std::bit_cast<T>(bytes);
}

template<std::integral T>
T to_little_end(T value){
    if constexpr(std::endian::native == std::endian::big)
        return byteswap(value);
    return value;
}

template<std::integral T>
T from_little_end(T value){
    return to_little_end(value); // swap is its own inverse
}

template<std::integral T>
void writeBinary(const T& value, std::fstream& file){
    T le = to_little_end(value);
    file.write(reinterpret_cast<const char*>(&le), sizeof(T));
    if(!file){
        throw std::runtime_error("writeBinary: write failed");
    }
}

template<std::integral T>
void writeBinary(const std::vector<T>& values, std::fstream& file){
    for(const auto& v : values)
        writeBinary(v, file);
}

template<std::integral T>
T readBinary(std::fstream& file){
    T le{};
    file.read(reinterpret_cast<char*>(&le), sizeof(T));
    if(!file){
        throw std::runtime_error("readBinary: read failed (truncated file?)");
    }
    return from_little_end(le);
}




// Reads `count` values of T into a vector.
template<std::integral T>
std::vector<T> readBinary(std::fstream& file, size_t count){
    std::vector<T> values;
    values.reserve(count);
    for(size_t i = 0; i < count; ++i)
        values.push_back(readBinary<T>(file));
    return values;
}
}}