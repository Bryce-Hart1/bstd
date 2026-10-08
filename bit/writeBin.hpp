#pragma once
#include <bitset>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>
#include "bitBuffer.hpp" //needs bitBuffer file (also in bstd)

namespace bstd {
/**
* writeBin @version 2 @author Bryce Hart
* @details writes bits and bytes to a binary stream, and reads raw bytes back as a `BitBuffer`.
*
* What lands on disk (identical on Windows and unix):
* - bytes are written exactly as given, in order. Nothing is added before or after them (no size, no header).
* - bits are packed MSB-first: the first bit written is the top bit (0x80) of the first byte, the same order `BitBuffer` uses.
* - if the bit count isn't a multiple of 8, the last byte is padded with `0` bits at the low end.
*   The bit count itself is NOT written, so the reader has to already know it (or store it themselves).
* - everything is written one byte at a time, so the machine's endianness never changes the output.
*
* @attention streams must be opened with `std::ios::binary`. On Windows a text mode stream turns every
* 0x0A byte into 0x0D 0x0A when writing (and back when reading), which corrupts binary data.
* openWrite() and openRead() always set it. unix ignores the flag, so it is always safe to pass.
*/
namespace bit{

static_assert(CHAR_BIT == 8, "writeBin assumes 8 bit bytes.");


/**
* Opens a file for binary writing.
* @param `path` - the file to write. `std::filesystem::path` also handles Windows wide (unicode) paths.
* @param `append` - true writes after what is already in the file, false (default) empties the file first
* @returns the stream, check it with `if(!stream)` before writing (falsy if the file couldn't be opened)
*/
inline std::ofstream openWrite(const std::filesystem::path& path, const bool append = false){
    return std::ofstream(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
}

/**
* Opens a file for binary reading.
* @param `path` - the file to read
* @returns the stream, check it with `if(!stream)` before reading (falsy if the file couldn't be opened)
*/
inline std::ifstream openRead(const std::filesystem::path& path){
    return std::ifstream(path, std::ios::binary);
}


/**
* Catches every type without its own overload below, so they are a compile error instead of silently converting.
* Without this `writeBinary(out, 300)` would write 44 (300 wrapped into a byte) and
* `writeBinary(out, someU32)` would only write its lowest byte.
* Cast to `unsigned char` first if a single byte is what you meant.
*/
template<typename T>
bool writeBinary(std::ostream& out, const T& value) = delete;


/**
* Writes one byte.
* @param `out` - a stream opened with `std::ios::binary` (see openWrite())
* @param `byte` - the byte to write. This one overload covers both `unsigned char` and `std::uint8_t`:
* `std::uint8_t` is a typedef of `unsigned char` on every platform that has it (Windows, Linux, macOS),
* so giving each its own overload would be a redefinition error.
* @returns false if the stream has failed (never opened, disk full, ...), and it stays failed after that
*/
inline bool writeBinary(std::ostream& out, const unsigned char byte){
    out.write(reinterpret_cast<const char*>(&byte), 1);
    return static_cast<bool>(out);
}


/**
* Writes every byte of a `BitBuffer` as is.
* @param `out` - a stream opened with `std::ios::binary` (see openWrite())
* @param `bits` - written as `bits.byteSize()` bytes. The bits are already MSB-first and the padding is already `0`.
* @returns false if the stream has failed (never opened, disk full, ...), and it stays failed after that
*/
inline bool writeBinary(std::ostream& out, const BitBuffer& bits){
    if(bits.byteSize() != 0){ //data() may be null when empty
        out.write(reinterpret_cast<const char*>(bits.data()), static_cast<std::streamsize>(bits.byteSize()));
    }
    return static_cast<bool>(out);
}


/**
* Writes a `std::bitset` in the order it prints: bit `N - 1` first, bit `0` last.
* So `std::bitset<8>(0xA5)` writes the byte 0xA5, and `std::bitset<16>(0x1234)` writes 0x12 then 0x34.
* @param `out` - a stream opened with `std::ios::binary` (see openWrite())
* @param `bits` - written as `(N + 7) / 8` bytes, padded with `0` bits if `N` isn't a multiple of 8
* @returns false if the stream has failed (never opened, disk full, ...), and it stays failed after that
*/
template<std::size_t N>
bool writeBinary(std::ostream& out, const std::bitset<N>& bits){
    BitBuffer buffer;
    buffer.reserve(N);
    for(std::size_t i = N; i > 0; i--){
        buffer.push(bits.test(i - 1));
    }
    return writeBinary(out, buffer);
}


/**
* Writes a `std::vector<bool>` in index order: `bits[0]` is the top bit of the first byte.
* @param `out` - a stream opened with `std::ios::binary` (see openWrite())
* @param `bits` - written as `(bits.size() + 7) / 8` bytes, padded with `0` bits if the size isn't a multiple of 8
* @returns false if the stream has failed (never opened, disk full, ...), and it stays failed after that
*/
inline bool writeBinary(std::ostream& out, const std::vector<bool>& bits){
    BitBuffer buffer;
    buffer.reserve(bits.size());
    for(const bool b : bits){
        buffer.push(b);
    }
    return writeBinary(out, buffer);
}


/**
* Reads the next `amount` bytes into a `BitBuffer` (every bit counted, so `size()` is `amount * 8`).
* @param `in` - a stream opened with `std::ios::binary` (see openRead())
* @param `amount` - how many bytes to read
* @returns nullopt if fewer than `amount` bytes were left. Those bytes are still used up and the stream is left failed.
*/
inline std::optional<BitBuffer> readBinary(std::istream& in, const std::size_t amount){
    if(amount == 0){
        return BitBuffer{};
    }
    if(amount > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())){
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(amount);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(amount));
    if(in.gcount() != static_cast<std::streamsize>(amount)){
        return std::nullopt;
    }
    return BitBuffer(std::move(bytes));
}


}//bit
}//bstd
