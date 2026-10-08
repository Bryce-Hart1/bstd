#pragma once
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
#include <stdexcept>
#include <string>
#include <string_view>
#include <bitset>
#include "convertBit.hpp" //needs convertBit file (also in bstd)

namespace bstd {
/**
* Bitbuffer @version 2 @author Bryce Hart
* @details an alternative to `std::vector<bool>` where you are given complete access to individual bits, 
* compared to vector where your are not given complete access and are given a proxy object. 
* 
*/
namespace bit{
class BitBuffer {
    using u8 = std::uint8_t; // u_int8_t is POSIX only, it doesn't exist on Windows (MSVC)
    using size_t = std::size_t;


private:
    std::vector<u8> _bytes;
    size_t _bitCount = 0;   // total bits written


    /**
    * true if bit `position` of `u` is 1, where 0 is the lowest bit.
    * @returns false if `position` is past the last bit of `U`, instead of shifting by too much (undefined behavior)
    */
    template<std::unsigned_integral U>
    static constexpr bool is_bit_set(const U u, const std::size_t position) noexcept{
        if(position >= static_cast<std::size_t>(std::numeric_limits<U>::digits)){
            return false;
        }
        return ((u >> position) & 1u) != 0;
    }

    /* Appends all 8 bits of `element`, MSB-first. Takes the fast path when the buffer ends on a byte boundary. */
    void push_u8(const u8 element){
        if(_bitCount % 8 != 0){
            push(element, 8);
            return;
        }
        _bytes.push_back(element);
        _bitCount += 8;
    }
   



    // Which byte index does bit i live in?
    static constexpr size_t byteIndex(size_t i) { return i / 8; }
    // Which bit within that byte? (MSB-first)
    static constexpr size_t bitOffset(size_t i) { return 7 - (i % 8); }

public:
    //  Proxy — returned by operator[] for read/write access
    //nested class Bitref
    class BitRef {
        u8&    byte;
        size_t offset;   // 0–7, where 7 = MSB

    public:
        BitRef(u8& b, size_t o) : byte(b), offset(o) {}

        // Write: buf[i] = true
        BitRef& operator=(bool val) {
            if (val) byte |=  (1u << offset);
            else byte &= ~(1u << offset);
            return *this;
        }

        // Copy-assign between two proxies
        BitRef& operator=(const BitRef& other) {
            return *this = static_cast<bool>(other);
        }

        // Read: bool x = buf[i]
        operator bool() const {
            return (byte >> offset) & 1u;
        }
    };

    // Construction
    BitBuffer() = default;

    // Pre-allocate for n bits (all zero)
    explicit BitBuffer(size_t n): _bytes((n + 7) / 8, 0), _bitCount(n){}

    // Build from raw bytes (all bits counted)
    explicit BitBuffer(std::vector<u8> raw): _bytes(std::move(raw)), _bitCount(_bytes.size() * 8){}


    /**
    * writes from a string that Appears as char `1` or char `0`, first char = first bit (MSB-first, like push())
    *
    * non `0` characters will be saved as `true`
    * @details size() is exactly `bits.size()`, a partial last byte is kept and padded with 0s.
    * Also takes a `std::string` (it converts to `std::string_view`), so there is no separate std::string constructor,
    * having both made `BitBuffer("0101")` ambiguous.
    */
    explicit BitBuffer(std::string_view bits){
        reserve(bits.size());
        for(const char c : bits){
            push(c != '0');
        }
    }

    //  Bit pushing
    void push(bool bit) {
        if (_bitCount % 8 == 0)
            _bytes.push_back(0);
        if (bit){
            _bytes.back() |= (1u << bitOffset(_bitCount));
        }
        ++_bitCount;
    }

    void push(u8 byte, int nBits = 8){
        if (nBits < 1 || nBits > 8)
            throw std::out_of_range("nBits must be 1–8");
        for (int i = nBits - 1; i >= 0; --i)
            push(static_cast<bool>((byte >> i) & 1u));
    }

    // Append every bit from another BitBuffer obj
    void push(const BitBuffer& _other) {
        for (size_t i = 0; i < _other._bitCount; ++i)
            push(static_cast<bool>(_other[i]));
    }

    //  Element access
    BitRef operator[](size_t i) {
        if (i >= _bitCount) throw std::out_of_range("BitBuffer index out of range");
        return { _bytes[byteIndex(i)], bitOffset(i) };
    }

    bool operator[](size_t i) const {
        if (i >= _bitCount) throw std::out_of_range("BitBuffer index out of range");
        return (_bytes[byteIndex(i)] >> bitOffset(i)) & 1u;
    }
    // Append one bit, same as push(bool)
    BitBuffer& operator+=(const bool b){
        push(b);
        return *this;
    }

    bool at(size_t i) const { 
        return (*this)[i]; }   // explicit checked read

    //  Byte-level access (for file I/O, sockets, etc.)
    const u8* data() const {
        return _bytes.data();
    }

    const std::vector<u8>& rawBytes()const {
        return _bytes; 
    }
    size_t byteSize() const{ 
        return _bytes.size();
    }

    // Capacity / state
    std::size_t size() const {
         return _bitCount;
    }
    bool isEmpty() const{
         return (_bitCount == 0);
    }

    void reserve(size_t nBits){
        _bytes.reserve((nBits + 7) / 8);
    }

    void clear() { 
        _bytes.clear(); 
        _bitCount = 0;}

    //  Iteration (read-only)
    struct Iterator {
        const BitBuffer& buf;
        size_t idx;

        bool operator*() const {
            return buf[idx]; 
        }
        //increment
        Iterator& operator++() {
            ++idx; return *this; 
        }
        //does not equal
        bool operator!=(const Iterator& o) const {
            return idx != o.idx; }
    };

    Iterator begin() const { return { *this, 0 }; }
    Iterator end()   const { return { *this, _bitCount }; }

/**
 * Utility 
 */
    //Readable string
    std::string toString() const{
        std::string s;
        for (size_t i = 0; i < _bitCount; ++i) {
            if (i > 0 && i % 8 == 0) s += ' ';
            s += ((*this)[i] ? '1' : '0');
        }
        // show padding bits in final byte
        size_t pad = (8 - _bitCount % 8) % 8;
        if (pad) {
            s += '|';
            for (size_t i = 0; i < pad; ++i) s += '.';
        }
        return s;
    }

    // How many padding bits exist in the last byte
    size_t paddingBits() const {
        return _bitCount == 0 ? 0 : (8 - _bitCount % 8) % 8;
    }
    
    /**
    * Readable bitset utility
     */
    std::vector<std::bitset<8>> toBitset() const{
        std::vector<std::bitset<8>> r;
        for(const unsigned char b : _bytes){
            r.emplace_back(static_cast<unsigned long>(b));
        }
        return r;
    }

    /**
    * Replaces current Bitbuffer with incoming string.
    * only incodes data at each char, 8 bits per char (the raw byte, NOT `'0'`/`'1'`, see the string_view constructor for that)
    * @throws `std::bad_alloc` if out of memory
    */
    void from_string(const std::string_view string){
        this->clear();
        _bytes.reserve(string.size());
        for(const char c : string){
            this->push_u8(static_cast<u8>(c));
        }
    }


    /**
    * extracts data from each element in the vector, and adds it to the current
    * `BitBuffer` after clearing it
    * @details the bytes of each element are copied as they sit in memory, so the byte order of
    * multi-byte types is the machine's (little-endian on x86 / ARM), and padding bytes inside structs are copied too.
    * @throws `std::bad_alloc` if out of memory
    */
    template<typename T>
    void from_vector(const std::vector<T>& data){
        static_assert(!std::is_pointer_v<T>, "from_vector would encode the addresses, not what they point to.");
        this->clear();
        _bytes.reserve(data.size() * sizeof(T));
        for(const T& v : data){
            for(std::size_t i = 0; i < sizeof(T); i++){
                push_u8(*bstd::bit::value_at_byte(v, i)); // i < sizeof(T), so the optional always has a value
            }
        }
    }


    /**
    * Encodes value of an object, `data`
    * clears underlying BitBuffer
    * @details same byte order and padding rules as from_vector(), `T` must be trivially copyable.
    * @throws `std::bad_alloc` if out of memory
    */
    template<typename T>
    void from(const T& data){
        static_assert(!std::is_pointer_v<T>, "from would encode the address, not what it points to.");
        this->clear(); //clear current
        _bytes.reserve(sizeof(T));
        for(std::size_t i = 0; i < sizeof(T); i++){
            push_u8(*bstd::bit::value_at_byte(data, i)); // i < sizeof(T), so the optional always has a value
        }
    }



};

}
} // namespace bstd