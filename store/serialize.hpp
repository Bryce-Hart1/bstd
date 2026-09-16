#pragma once
#include <bit>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <string_view>
#include "../bit/bitBuffer.hpp"

namespace bstd{
namespace store{

/**
* The type code of every bin object, and of every line in a binFile.
* Written to disk as 3 bits.
* @attention These values land on disk. Never renumber them, and only ever append (none must stay 7).
* The order matches the alternative order of the leaf, item and binFile::allowedTypes variants,
* so static_cast<rowType>(variant.index()) is always the stored code.
*/
enum class rowType : std::uint8_t{
    B8     = 0,
    B32    = 1,
    B64    = 2,
    STRING = 3,
    PAIR   = 4,
    VEC    = 5,
    MAP    = 6,
    none   = 7, //nothing exists, or end of row zero.
};

namespace _details{

inline constexpr std::size_t TYPE_BITS = 3;   // a rowType on disk
inline constexpr std::size_t COUNT_BITS = 32; // a string length, or an element / line count

/* Appends the low @param bits bits of @param value to @param out, lowest bit first. */
inline void writeUint(bit::BitBuffer& out, const std::uint64_t value, const std::size_t bits){
    for(std::size_t i{0}; i < bits; i++){
        out.push(static_cast<bool>((value >> i) & 1u));
    }
}

/**
* Reads @param bits bits written by writeUint, starting at @param cursor.
* @returns nullopt if that runs past the end of @param in. The cursor only moves on success,
* and the bounds check comes first so BitBuffer::operator[] never throws.
*/
inline std::optional<std::uint64_t> readUint(const bit::BitBuffer& in, std::size_t& cursor, const std::size_t bits) noexcept{
    if(cursor > in.size() || in.size() - cursor < bits){
        return std::nullopt;
    }
    std::uint64_t value{0};
    for(std::size_t i{0}; i < bits; i++){
        if(in[cursor + i]){
            value |= std::uint64_t{1} << i;
        }
    }
    cursor += bits;
    return value;
}

/**
* Writes a 32 bit length / element count.
* @throws `std::length_error` if @param count doesn't fit in 32 bits (@param what names the object in the message)
*/
inline void writeCount(bit::BitBuffer& out, const std::size_t count, const char* what){
    if(count > std::numeric_limits<std::uint32_t>::max()){
        throw std::length_error(std::string(what) + " is too big to write (" + std::to_string(count) + " elements).");
    }
    writeUint(out, count, COUNT_BITS);
}

/**
* true if @param count elements of at least @param minBitsEach bits each can fit in what is left of @param in.
* Checked before reserving anything, so a corrupt count can't allocate gigabytes.
*/
inline bool fitsCount(const bit::BitBuffer& in, const std::size_t cursor, const std::uint64_t count, const std::size_t minBitsEach) noexcept{
    return cursor <= in.size() && count <= (in.size() - cursor) / minBitsEach;
}

/* Reads a 3 bit rowType. */
inline std::optional<rowType> readType(const bit::BitBuffer& in, std::size_t& cursor) noexcept{
    const std::optional<std::uint64_t> raw = readUint(in, cursor, TYPE_BITS);
    if(!raw){
        return std::nullopt;
    }
    return static_cast<rowType>(*raw);
}

/* Writes a scalar's displayed_as_bitset(), bit 0 first. */
template <std::size_t N>
void writeBitset(bit::BitBuffer& out, const std::bitset<N>& bits){
    for(std::size_t i{0}; i < N; i++){
        out.push(bits.test(i));
    }
}

/* Reads N bits back into a bitset. @returns nullopt (cursor untouched) if fewer than N bits are left. */
template <std::size_t N>
std::optional<std::bitset<N>> readBitset(const bit::BitBuffer& in, std::size_t& cursor) noexcept{
    if(cursor > in.size() || in.size() - cursor < N){
        return std::nullopt;
    }
    std::bitset<N> bits{};
    for(std::size_t i{0}; i < N; i++){
        bits.set(i, in[cursor + i]);
    }
    cursor += N;
    return bits;
}

}

/**
* @class bin8
* An 8 bit value that remembers which of its four types it was built from.
* Serialized as 10 bits: [header: bits 0-1][payload: bits 2-9]
* header: 00 = u8, 01 = i8, 10 = bool, 11 = char
*/
class bin8{
    using _HEADER = std::bitset<2>;
    using _PAYLOAD = std::bitset<8>;
    enum class dataType{
        U8,
        I8,
        BOOL,
        CHAR,
    };

    public:
    using all_types = std::variant<bool, u_int8_t, int8_t, char>;

    private:
    std::uint8_t _data;
    dataType _type;

    const _HEADER getHeaderValue() const noexcept{
        _HEADER h{};
        switch (this->_type){
            case dataType::U8:
            break;
            case dataType::I8:
            h.flip(0);
            break;
            case dataType::BOOL:
            h.flip(1);
            break;
            case dataType::CHAR:
            h.flip(0);
            h.flip(1);
            break;
            }
        return h;
    }

    const _PAYLOAD getPayloadValue() const noexcept{
        return std::bitset<8>(_data);
    }



    public:
    /*convert from bool */
    explicit bin8(bool v) noexcept : _data(std::bit_cast<std::uint8_t>(v)) {
        _type = dataType::BOOL;
    }

    /* convert from char*/
    explicit bin8(char v)noexcept : _data(std::bit_cast<std::uint8_t>(v)) {
        _type = dataType::CHAR;
    }

    /*Convert from u8. */
    explicit bin8(std::uint8_t v)  noexcept : _data(v) {
        _type = dataType::U8;
    }

    /*convert from int8 */
    explicit bin8(std::int8_t v)   noexcept : _data(std::bit_cast<std::uint8_t>(v)) {
        _type = dataType::I8;
    }

    // Anything that is NOT exactly one of the four types above.
    // explicit, or overload resolution counts it as a conversion from anything and
    // std::variant<bin8, ..., binString> can no longer pick binString for a string literal.
    template <typename T>
    explicit bin8(T) = delete;

    /**
    * @returns header and payload together, header in bits 0-1 and payload in bits 2-9.
    * from_bitset() turns this back into a bin8.
    */
    const std::bitset<10> displayed_as_bitset() const noexcept{
        const _HEADER h = getHeaderValue();
        const _PAYLOAD body = getPayloadValue();
        std::bitset<10> both{};
        for(std::size_t i{0}; i < 10; i++){
            if(i < 2){
                both.set(i, h.test(i));
            }else{
                both.set(i, body.test(i-2));
            }
        }
        return both;
    }

    /**
    * Rebuilds a bin8 from the output of displayed_as_bitset().
    * @returns `std::nullopt` if @param bits can't be a bin8 (a bool payload that isn't 0 or 1)
    */
    static std::optional<bin8> from_bitset(const std::bitset<10>& bits) noexcept{
        _HEADER h{};
        h.set(0, bits.test(0));
        h.set(1, bits.test(1));
        std::uint8_t payload{0};
        for(std::size_t i{0}; i < 8; i++){
            if(bits.test(i+2)){
                payload |= static_cast<std::uint8_t>(1u << i);
            }
        }
        switch(h.to_ulong()){
            case 0: return bin8(payload);
            case 1: return bin8(std::bit_cast<std::int8_t>(payload));
            case 2:
                if(payload > 1){
                    return std::nullopt;
                }
                return bin8(payload == 1);
            case 3: return bin8(std::bit_cast<char>(payload));
        }
        return std::nullopt;
    }

    std::bitset<8> value_to_bitset() const noexcept {
        return getPayloadValue();
    }

    /* Bits one bin8 takes on disk. */
    static constexpr std::size_t bit_width = 10;

    /* Appends displayed_as_bitset() to @param out. */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeBitset(out, displayed_as_bitset());
    }

    /**
    * Reads a bin8 written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out or aren't a bin8. The cursor only moves on success.
    */
    static std::optional<bin8> read_bits(const bit::BitBuffer& in, std::size_t& cursor) noexcept{
        std::size_t at = cursor;
        const std::optional<std::bitset<10>> bits = _details::readBitset<10>(in, at);
        if(!bits){
            return std::nullopt;
        }
        std::optional<bin8> out = from_bitset(*bits);
        if(out){
            cursor = at;
        }
        return out;
    }


    all_types value() const noexcept{
        switch (_type) {
        case dataType::U8:
            return std::bit_cast<uint8_t>(_data);
        break;
        case dataType::I8:
            return std::bit_cast<int8_t>(_data);
        break;
        case dataType::BOOL:
            return std::bit_cast<bool>(_data);
        break;
        case dataType::CHAR:
            return std::bit_cast<char>(_data);
        break;
        }
        return _data; // unreachable, every dataType is handled above
    }

    /* Equal only if both the type and the bits match, so bin8(u8 65) != bin8('A'). */
    bool operator==(const bin8& other) const noexcept = default;

};

/**
* @class bin32
* A 32 bit value that remembers which of its four types it was built from.
* Serialized as 34 bits: [header: bits 0-1][payload: bits 2-33]
* header: 00 = u32, 01 = i32, 10 = float, 11 = char32_t
*/
class bin32{
    using _HEADER = std::bitset<2>;
    using _PAYLOAD = std::bitset<32>;
    enum class dataType{
        U32,
        I32,
        FLOAT,
        CHAR32,
    };

    public:
    using all_types = std::variant<std::uint32_t, std::int32_t, float, char32_t>;

    private:
    std::uint32_t _data;
    dataType _type;

    const _HEADER getHeaderValue() const noexcept{
        _HEADER h{};
        switch (this->_type){
            case dataType::U32:
            break;
            case dataType::I32:
            h.flip(0);
            break;
            case dataType::FLOAT:
            h.flip(1);
            break;
            case dataType::CHAR32:
            h.flip(0);
            h.flip(1);
            break;
            }
        return h;
    }

    const _PAYLOAD getPayloadValue() const noexcept{
        return std::bitset<32>(_data);
    }



    public:
    /* Convert from u32. */
    explicit bin32(std::uint32_t v) noexcept : _data(v) {
        _type = dataType::U32;
    }

    /* convert from int32 */
    explicit bin32(std::int32_t v) noexcept : _data(std::bit_cast<std::uint32_t>(v)) {
        _type = dataType::I32;
    }

    /* convert from float, bit for bit so NaN and -0.0 come back exactly */
    explicit bin32(float v) noexcept : _data(std::bit_cast<std::uint32_t>(v)) {
        _type = dataType::FLOAT;
    }

    /* convert from char32_t */
    explicit bin32(char32_t v) noexcept : _data(std::bit_cast<std::uint32_t>(v)) {
        _type = dataType::CHAR32;
    }

    // Anything that is NOT exactly one of the four types above
    template <typename T>
    explicit bin32(T) = delete;

    /**
    * @returns header and payload together, header in bits 0-1 and payload in bits 2-33.
    * from_bitset() turns this back into a bin32.
    */
    const std::bitset<34> displayed_as_bitset() const noexcept{
        const _HEADER h = getHeaderValue();
        const _PAYLOAD body = getPayloadValue();
        std::bitset<34> both{};
        for(std::size_t i{0}; i < 34; i++){
            if(i < 2){
                both.set(i, h.test(i));
            }else{
                both.set(i, body.test(i-2));
            }
        }
        return both;
    }

    /**
    * Rebuilds a bin32 from the output of displayed_as_bitset().
    * Every 34 bit pattern is a valid bin32, the optional is kept to match bin8 and bin64.
    */
    static std::optional<bin32> from_bitset(const std::bitset<34>& bits) noexcept{
        _HEADER h{};
        h.set(0, bits.test(0));
        h.set(1, bits.test(1));
        std::uint32_t payload{0};
        for(std::size_t i{0}; i < 32; i++){
            if(bits.test(i+2)){
                payload |= std::uint32_t{1} << i;
            }
        }
        switch(h.to_ulong()){
            case 0: return bin32(payload);
            case 1: return bin32(std::bit_cast<std::int32_t>(payload));
            case 2: return bin32(std::bit_cast<float>(payload));
            case 3: return bin32(std::bit_cast<char32_t>(payload));
        }
        return std::nullopt;
    }

    std::bitset<32> value_to_bitset() const noexcept {
        return getPayloadValue();
    }

    /* Bits one bin32 takes on disk. */
    static constexpr std::size_t bit_width = 34;

    /* Appends displayed_as_bitset() to @param out. */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeBitset(out, displayed_as_bitset());
    }

    /**
    * Reads a bin32 written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out. The cursor only moves on success.
    */
    static std::optional<bin32> read_bits(const bit::BitBuffer& in, std::size_t& cursor) noexcept{
        std::size_t at = cursor;
        const std::optional<std::bitset<34>> bits = _details::readBitset<34>(in, at);
        if(!bits){
            return std::nullopt;
        }
        std::optional<bin32> out = from_bitset(*bits);
        if(out){
            cursor = at;
        }
        return out;
    }


    all_types value() const noexcept{
        switch (_type) {
        case dataType::U32:
            return _data;
        case dataType::I32:
            return std::bit_cast<std::int32_t>(_data);
        case dataType::FLOAT:
            return std::bit_cast<float>(_data);
        case dataType::CHAR32:
            return std::bit_cast<char32_t>(_data);
        }
        return _data; // unreachable, every dataType is handled above
    }

    /* Compares the stored bits, not the float value: NaN == NaN here, and 0.0f != -0.0f. */
    bool operator==(const bin32& other) const noexcept = default;

};

/**
* @class bin64
* A 64 bit value that remembers which of its types it was built from.
* Serialized as 66 bits: [header: bits 0-1][payload: bits 2-65]
* header: 00 = u64, 01 = i64, 10 = double, 11 = unused (from_bitset() rejects it)
*/
class bin64{
    using _HEADER = std::bitset<2>;
    using _PAYLOAD = std::bitset<64>;
    enum class dataType{
        U64,
        I64,
        DOUBLE,
    };

    public:
    using all_types = std::variant<std::uint64_t, std::int64_t, double>;

    private:
    std::uint64_t _data;
    dataType _type;

    const _HEADER getHeaderValue() const noexcept{
        _HEADER h{};
        switch (this->_type){
            case dataType::U64:
            break;
            case dataType::I64:
            h.flip(0);
            break;
            case dataType::DOUBLE:
            h.flip(1);
            break;
            }
        return h;
    }

    const _PAYLOAD getPayloadValue() const noexcept{
        return std::bitset<64>(_data);
    }



    public:
    /**
    * Convert from u64.
    * A template so both `unsigned long` and `unsigned long long` are taken: std::uint64_t is
    * a different one of the two on Mac and Linux, and std::size_t is the other one on Mac.
    */
    template <typename T>
    requires std::unsigned_integral<T> && (sizeof(T) == sizeof(std::uint64_t))
    explicit bin64(T v) noexcept : _data(static_cast<std::uint64_t>(v)) {
        _type = dataType::U64;
    }

    /* convert from int64, both `long` and `long long` for the same reason as u64 */
    template <typename T>
    requires std::signed_integral<T> && (sizeof(T) == sizeof(std::int64_t))
    explicit bin64(T v) noexcept : _data(std::bit_cast<std::uint64_t>(static_cast<std::int64_t>(v))) {
        _type = dataType::I64;
    }

    /* convert from double, bit for bit so NaN and -0.0 come back exactly */
    explicit bin64(double v) noexcept : _data(std::bit_cast<std::uint64_t>(v)) {
        _type = dataType::DOUBLE;
    }

    // Anything that is NOT a 64 bit integer or a double
    template <typename T>
    explicit bin64(T) = delete;

    /**
    * @returns header and payload together, header in bits 0-1 and payload in bits 2-65.
    * from_bitset() turns this back into a bin64.
    */
    const std::bitset<66> displayed_as_bitset() const noexcept{
        const _HEADER h = getHeaderValue();
        const _PAYLOAD body = getPayloadValue();
        std::bitset<66> both{};
        for(std::size_t i{0}; i < 66; i++){
            if(i < 2){
                both.set(i, h.test(i));
            }else{
                both.set(i, body.test(i-2));
            }
        }
        return both;
    }

    /**
    * Rebuilds a bin64 from the output of displayed_as_bitset().
    * @returns `std::nullopt` if the header in @param bits is the unused 11
    */
    static std::optional<bin64> from_bitset(const std::bitset<66>& bits) noexcept{
        _HEADER h{};
        h.set(0, bits.test(0));
        h.set(1, bits.test(1));
        std::uint64_t payload{0};
        for(std::size_t i{0}; i < 64; i++){
            if(bits.test(i+2)){
                payload |= std::uint64_t{1} << i;
            }
        }
        switch(h.to_ulong()){
            case 0: return bin64(payload);
            case 1: return bin64(std::bit_cast<std::int64_t>(payload));
            case 2: return bin64(std::bit_cast<double>(payload));
        }
        return std::nullopt;
    }

    std::bitset<64> value_to_bitset() const noexcept {
        return getPayloadValue();
    }

    /* Bits one bin64 takes on disk. */
    static constexpr std::size_t bit_width = 66;

    /* Appends displayed_as_bitset() to @param out. */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeBitset(out, displayed_as_bitset());
    }

    /**
    * Reads a bin64 written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out or use the unused header. The cursor only moves on success.
    */
    static std::optional<bin64> read_bits(const bit::BitBuffer& in, std::size_t& cursor) noexcept{
        std::size_t at = cursor;
        const std::optional<std::bitset<66>> bits = _details::readBitset<66>(in, at);
        if(!bits){
            return std::nullopt;
        }
        std::optional<bin64> out = from_bitset(*bits);
        if(out){
            cursor = at;
        }
        return out;
    }


    all_types value() const noexcept{
        switch (_type) {
        case dataType::U64:
            return _data;
        case dataType::I64:
            return std::bit_cast<std::int64_t>(_data);
        case dataType::DOUBLE:
            return std::bit_cast<double>(_data);
        }
        return _data; // unreachable, every dataType is handled above
    }

    /* Compares the stored bits, not the double value: NaN == NaN here, and 0.0 != -0.0. */
    bool operator==(const bin64& other) const noexcept = default;

};


namespace _details{

/**
* Anything that implicitly converts to a std::string_view:
* std::string, std::string_view, const char*, and string literals / char arrays.
* A bare `nullptr` is rejected; C++20 would otherwise let it through as a null const char*.
*/
template <typename T>
concept string_equivalents = std::convertible_to<const T&, std::string_view> && !std::is_null_pointer_v<T>;

/* @returns @param s as a view. A null `const char*` is read as an empty string instead of crashing. */
template <string_equivalents T>
constexpr std::string_view as_view(const T& s) noexcept{
    if constexpr (std::is_pointer_v<T>){
        if(s == nullptr){
            return {};
        }
    }
    return std::string_view(s);
}

}

/**
* @class binString
* A string stored as raw bytes. Anything that satisfies `_details::string_equivalents`
* (std::string, std::string_view, const char*, string literals) can build, append to,
* search, assign, and compare against it.
*/
class binString{
    private:
    using u8 = u_int8_t;
    using iterator = std::vector<uint8_t>::iterator;
    using const_iterator = std::vector<uint8_t>::const_iterator;
    std::vector<u8> _data;
    friend class bstd::store::bin8;

    /* true if @param v points into this string's own bytes, e.g. push_back(view()) */
    bool overlaps(const std::string_view v) const noexcept{
        const char* first = reinterpret_cast<const char*>(_data.data());
        const char* last = first + _data.size();
        return !v.empty() && std::less_equal<const char*>{}(first, v.data()) && std::less<const char*>{}(v.data(), last);
    }


    public:

    /* Create an empty binString. */
    binString(){}

    /* Create binString from a string, string_view, const char*, or string literal. */
    template <_details::string_equivalents S>
    binString(const S& string){
        push_back(string);
    }


    /**
    * return element at `position`
    * @param `position`: the position that you want to retrieve from
    * @returns `char` value if found, `std::nullopt` if not (wrapped in option)
    */
    const std::optional<char> at(const std::size_t position) const noexcept{
        if(position >= _data.size()){
            return std::nullopt;
        }
        return std::bit_cast<char>(_data[position]);
    }



    /**
    * @returns amount of elements
    */
    std::size_t size() const noexcept{
        return _data.size();
    }

    /**
    * @returns the bytes as a `std::string_view`
    * @attention the view is invalidated by anything that changes this string (push_back, remove, =, +=)
    */
    std::string_view view() const noexcept{
        return std::string_view(reinterpret_cast<const char*>(_data.data()), _data.size());
    }

    /**  Push a single @param other (char) to the back of the string */
    void push_back(const char& other){
        _data.emplace_back(std::bit_cast<u_int8_t>(other));
    }


    /**  Push a full @param other (string, string_view, const char*) to the back of the string */
    template <_details::string_equivalents S>
    void push_back(const S& other){
        const std::string_view v = _details::as_view(other);
        if(overlaps(v)){
            // growing _data can free the bytes v points at, so copy them out first
            const std::string copy(v);
            push_back(copy);
            return;
        }
        _data.insert(_data.end(), v.begin(), v.end());
    }

    /** Remove char from @param index.
    * @throws `runtime_error` if index is out of bounds
    *
    * example:
    *
    * data.remove(1);
    *
    * d o g becomes:
    * d g
    */
    void remove(const std::size_t& index){
        if(index >= _data.size()){
            throw std::runtime_error("Element " + std::to_string(index) + " is out of bounds.");
        }
        _data.erase(_data.begin() + static_cast<std::ptrdiff_t>(index));
    }

    /**
    * Finds all indexes where @param element exists in the list
    */
    std::vector<std::size_t> find(const char& element)const noexcept{
        u8 Find = std::bit_cast<u8>(element);
        std::vector<std::size_t> instances;
        for(std::size_t i{0}; i < _data.size(); i++){
            if(_data.at(i) == Find){
                instances.push_back(i);
            }
        }
        return instances;
    }

    /**
    * finds all start and end indexes where @param other appears, overlapping matches included.
    * Both indexes are inclusive: finding "aa" in "aaa" gives {0, 1} and {1, 2}.
    * An empty @param other matches nothing.
    */
    template <_details::string_equivalents S>
    const std::vector<std::pair<std::size_t, std::size_t>> find(const S& other) const noexcept{
        const std::string_view needle = _details::as_view(other);
        const std::string_view haystack = view();
        std::vector<std::pair<std::size_t, std::size_t>> instances;
        if(needle.empty()){
            return instances;
        }
        for(std::size_t i = haystack.find(needle); i != std::string_view::npos; i = haystack.find(needle, i + 1)){
            instances.emplace_back(i, i + needle.size() - 1);
        }
        return instances;
    }



    /**
    * Appends [length: 32 bits][8 bits per char] to @param out.
    * @throws `std::length_error` if the string is longer than a 32 bit length can hold
    */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeCount(out, _data.size(), "binString");
        for(const u8& byte : _data){
            _details::writeUint(out, byte, 8);
        }
    }

    /**
    * Reads a binString written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out before the length says they should. The cursor only moves on success.
    */
    static std::optional<binString> read_bits(const bit::BitBuffer& in, std::size_t& cursor){
        std::size_t at = cursor;
        const std::optional<std::uint64_t> length = _details::readUint(in, at, _details::COUNT_BITS);
        if(!length || !_details::fitsCount(in, at, *length, 8)){
            return std::nullopt;
        }
        binString out;
        out._data.reserve(static_cast<std::size_t>(*length));
        for(std::uint64_t i{0}; i < *length; i++){
            out._data.push_back(static_cast<u8>(*_details::readUint(in, at, 8)));
        }
        cursor = at;
        return out;
    }

    /* Iterators        */
    iterator begin() noexcept { return _data.begin(); }
    iterator end() noexcept { return _data.end(); }
    const_iterator begin() const noexcept { return _data.begin(); }
    const_iterator end() const noexcept { return _data.end(); }
    const_iterator cbegin() const noexcept { return _data.cbegin(); }
    const_iterator cend() const noexcept { return _data.cend(); }



    /**
    * True if @param other holds exactly the same chars.
    * `!=` and the reversed `"abc" == str` are generated from this one (C++20). There is
    * deliberately no operator!= -- declaring one turns both of those off.
    */
    template <_details::string_equivalents S>
    bool operator==(const S& other) const noexcept{
        return view() == _details::as_view(other);
    }

    bool operator==(const binString& other) const noexcept = default;

    /* Replace the contents with @param other. */
    template <_details::string_equivalents S>
    binString& operator=(const S& other){
        const std::string_view v = _details::as_view(other);
        std::vector<u8> next(v.begin(), v.end()); // built before _data changes, so `str = str.view()` is safe
        _data.swap(next);
        return *this;
    }

    binString& operator+=(const char& other) noexcept{
        push_back(other);
        return *this;
    }

    template <_details::string_equivalents S>
    binString& operator+=(const S& other){
        push_back(other);
        return *this;
    }


};

}//store
}//bstd

/* Hashes of the stored bits, so bin types can be keys in std::unordered_map / std::unordered_set (and binMap). */
namespace std{

template <>
struct hash<bstd::store::bin8>{
    std::size_t operator()(const bstd::store::bin8& b) const noexcept{
        return std::hash<std::bitset<10>>{}(b.displayed_as_bitset());
    }
};

template <>
struct hash<bstd::store::bin32>{
    std::size_t operator()(const bstd::store::bin32& b) const noexcept{
        return std::hash<std::bitset<34>>{}(b.displayed_as_bitset());
    }
};

template <>
struct hash<bstd::store::bin64>{
    std::size_t operator()(const bstd::store::bin64& b) const noexcept{
        return std::hash<std::bitset<66>>{}(b.displayed_as_bitset());
    }
};

template <>
struct hash<bstd::store::binString>{
    std::size_t operator()(const bstd::store::binString& s) const noexcept{
        return std::hash<std::string_view>{}(s.view());
    }
};

}//std

namespace bstd{
namespace store{

class binPair;
class binVec;
class binMap;

namespace _details{

/* bin8, bin32, bin64, or binString: what a binPair holds and what a binMap uses as keys. */
template <typename T>
concept bin_leaf = std::same_as<T, bin8> || std::same_as<T, bin32> || std::same_as<T, bin64> || std::same_as<T, binString>;

/* A leaf or a binPair: what binVec and binMap values hold. */
template <typename T>
concept bin_item = bin_leaf<T> || std::same_as<T, binPair>;

/* Anything a binFile line can hold. */
template <typename T>
concept bin_object = bin_item<T> || std::same_as<T, binVec> || std::same_as<T, binMap>;

using leaf = std::variant<bin8, bin32, bin64, binString>;
using item = std::variant<bin8, bin32, bin64, binString, binPair>;

/* The rowType code of each bin type. */
template <typename T>
inline constexpr rowType rowType_v = rowType::none;
template <> inline constexpr rowType rowType_v<bin8> = rowType::B8;
template <> inline constexpr rowType rowType_v<bin32> = rowType::B32;
template <> inline constexpr rowType rowType_v<bin64> = rowType::B64;
template <> inline constexpr rowType rowType_v<binString> = rowType::STRING;
template <> inline constexpr rowType rowType_v<binPair> = rowType::PAIR;
template <> inline constexpr rowType rowType_v<binVec> = rowType::VEC;
template <> inline constexpr rowType rowType_v<binMap> = rowType::MAP;

/* true if every alternative of the variant sits at the index of its own rowType code. */
template <typename... Ts>
constexpr bool indexesMatchRowType(const std::variant<Ts...>*) noexcept{
    std::size_t i{0};
    for(const rowType t : {rowType_v<Ts>...}){
        if(static_cast<std::size_t>(t) != i++){
            return false;
        }
    }
    return true;
}

/* The rowType of whatever @param v holds. Relies on indexesMatchRowType (checked below each variant). */
template <typename... Ts>
constexpr rowType typeOf(const std::variant<Ts...>& v) noexcept{
    return static_cast<rowType>(v.index());
}

/* The fewest bits an object of @param type can take on disk. Used to reject impossible counts. */
inline constexpr std::size_t minBits(const rowType type) noexcept{
    switch(type){
        case rowType::B8:     return 10;
        case rowType::B32:    return 34;
        case rowType::B64:    return 66;
        case rowType::STRING: return COUNT_BITS;                      // empty string
        case rowType::PAIR:   return 2 * (TYPE_BITS + 10);             // two bin8s
        case rowType::VEC:    return TYPE_BITS + COUNT_BITS;           // empty vec
        case rowType::MAP:    return 2 * TYPE_BITS + COUNT_BITS;       // empty map
        case rowType::none:   return 1;
    }
    return 1;
}

/* Appends whichever alternative @param v holds, without its type code. */
template <typename V>
void writeAlternative(bit::BitBuffer& out, const V& v){
    std::visit([&out](const auto& x){ x.write_bits(out); }, v);
}

/**
* Reads the alternative at @param index of Variant (the object's rowType code) at @param cursor.
* @returns nullopt if the index isn't one of Variant's alternatives or the object's bits are bad.
* The cursor only moves on success.
*/
template <typename Variant, std::size_t I = 0>
std::optional<Variant> readAlternative(const bit::BitBuffer& in, std::size_t& cursor, const std::size_t index){
    if constexpr (I == std::variant_size_v<Variant>){
        return std::nullopt;
    }else{
        if(index != I){
            return readAlternative<Variant, I + 1>(in, cursor, index);
        }
        using T = std::variant_alternative_t<I, Variant>;
        std::optional<T> value = T::read_bits(in, cursor);
        if(!value){
            return std::nullopt;
        }
        return Variant(std::in_place_index<I>, std::move(*value));
    }
}

}

static_assert(_details::indexesMatchRowType(static_cast<_details::leaf*>(nullptr)));


/**
* @class binPair
* Two bin8 / bin32 / bin64 / binString values, which can be different types.
* On disk: [first's rowType: 3 bits][first][second's rowType: 3 bits][second]
*/
class binPair{
    public:
    using leaf = _details::leaf;

    private:
    leaf _first;
    leaf _second;

    public:
    /* Pair two bin types, e.g. binPair(bin8('a'), binString("apple")). */
    template <_details::bin_leaf A, _details::bin_leaf B>
    binPair(const A& first, const B& second) : _first(std::in_place_type<A>, first), _second(std::in_place_type<B>, second) {}

    /* Pair two values you already hold as leaves, e.g. from binVec::at(). A string literal becomes a binString. */
    binPair(leaf first, leaf second) : _first(std::move(first)), _second(std::move(second)) {}

    const leaf& first() const noexcept{
        return _first;
    }

    const leaf& second() const noexcept{
        return _second;
    }

    rowType first_type() const noexcept{
        return _details::typeOf(_first);
    }

    rowType second_type() const noexcept{
        return _details::typeOf(_second);
    }

    /* Appends [first's type][first][second's type][second] to @param out. */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeUint(out, static_cast<std::uint64_t>(first_type()), _details::TYPE_BITS);
        _details::writeAlternative(out, _first);
        _details::writeUint(out, static_cast<std::uint64_t>(second_type()), _details::TYPE_BITS);
        _details::writeAlternative(out, _second);
    }

    /**
    * Reads a binPair written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out or either half isn't a leaf type. The cursor only moves on success.
    */
    static std::optional<binPair> read_bits(const bit::BitBuffer& in, std::size_t& cursor){
        std::size_t at = cursor;
        std::optional<rowType> firstType = _details::readType(in, at);
        if(!firstType){
            return std::nullopt;
        }
        std::optional<leaf> first = _details::readAlternative<leaf>(in, at, static_cast<std::size_t>(*firstType));
        if(!first){
            return std::nullopt;
        }
        std::optional<rowType> secondType = _details::readType(in, at);
        if(!secondType){
            return std::nullopt;
        }
        std::optional<leaf> second = _details::readAlternative<leaf>(in, at, static_cast<std::size_t>(*secondType));
        if(!second){
            return std::nullopt;
        }
        cursor = at;
        return binPair(std::move(*first), std::move(*second));
    }

    bool operator==(const binPair& other) const = default;

};

static_assert(_details::indexesMatchRowType(static_cast<_details::item*>(nullptr)));


/**
* @class binVec
* A list of bin objects that are all the same rowType, locked in by the first push_back.
* Subtypes can still differ: bin32 u32 and bin32 float can share a binVec, and so can pairs of different types.
* On disk: [rowType: 3 bits][count: 32 bits][elements], the type is written once, not per element.
*/
class binVec{
    public:
    using item = _details::item;
    using const_iterator = std::vector<item>::const_iterator;

    private:
    rowType _type = rowType::none;
    std::vector<item> _data;

    public:

    /* Create an empty binVec, its type is decided by the first push_back. */
    binVec(){}

    /**
    * Push @param value to the back.
    * @returns false (and adds nothing) if it isn't the same rowType as what is already in the vec.
    */
    bool push_back(const item& value){
        const rowType type = _details::typeOf(value);
        if(_type != rowType::none && _type != type){
            return false;
        }
        _data.push_back(value);
        _type = type;
        return true;
    }

    /**
    * return element at @param index
    * @returns the element if found, `std::nullopt` if not
    */
    std::optional<item> at(const std::size_t index) const{
        if(index >= _data.size()){
            return std::nullopt;
        }
        return _data[index];
    }

    /**
    * Typed version of at(), e.g. vec.at<bin32>(3).
    * @returns `std::nullopt` if @param index is out of bounds or the element isn't a T
    */
    template <_details::bin_item T>
    std::optional<T> at(const std::size_t index) const{
        if(index >= _data.size()){
            return std::nullopt;
        }
        if(const T* value = std::get_if<T>(&_data[index])){
            return *value;
        }
        return std::nullopt;
    }

    /**
    * @returns amount of elements
    */
    std::size_t size() const noexcept{
        return _data.size();
    }

    /* The rowType every element has, `rowType::none` until the first push_back (or after clear()). */
    rowType type() const noexcept{
        return _type;
    }

    /** Remove element at @param index.
    * @throws `runtime_error` if index is out of bounds
    * The vec keeps its type even if this empties it, use clear() to reset it.
    */
    void remove(const std::size_t& index){
        if(index >= _data.size()){
            throw std::runtime_error("Element " + std::to_string(index) + " is out of bounds.");
        }
        _data.erase(_data.begin() + static_cast<std::ptrdiff_t>(index));
    }

    /* Removes every element and unlocks the type. */
    void clear() noexcept{
        _data.clear();
        _type = rowType::none;
    }

    /**
    * Finds all indexes where @param element exists in the list
    */
    std::vector<std::size_t> find(const item& element) const{
        std::vector<std::size_t> instances;
        for(std::size_t i{0}; i < _data.size(); i++){
            if(_data[i] == element){
                instances.push_back(i);
            }
        }
        return instances;
    }

    /* Appends [type][count][elements] to @param out. @throws `std::length_error` past 2^32 - 1 elements */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeUint(out, static_cast<std::uint64_t>(_type), _details::TYPE_BITS);
        _details::writeCount(out, _data.size(), "binVec");
        for(const item& element : _data){
            _details::writeAlternative(out, element);
        }
    }

    /**
    * Reads a binVec written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out, the type can't go in a binVec, or an element is bad.
    * The cursor only moves on success.
    */
    static std::optional<binVec> read_bits(const bit::BitBuffer& in, std::size_t& cursor){
        std::size_t at = cursor;
        const std::optional<rowType> type = _details::readType(in, at);
        const std::optional<std::uint64_t> count = type ? _details::readUint(in, at, _details::COUNT_BITS) : std::nullopt;
        if(!count){
            return std::nullopt;
        }
        binVec out;
        if(*type == rowType::none){
            if(*count != 0){
                return std::nullopt;
            }
            cursor = at;
            return out;
        }
        const std::size_t index = static_cast<std::size_t>(*type);
        if(index >= std::variant_size_v<item> || !_details::fitsCount(in, at, *count, _details::minBits(*type))){
            return std::nullopt;
        }
        out._type = *type;
        out._data.reserve(static_cast<std::size_t>(*count));
        for(std::uint64_t i{0}; i < *count; i++){
            std::optional<item> element = _details::readAlternative<item>(in, at, index);
            if(!element){
                return std::nullopt;
            }
            out._data.push_back(std::move(*element));
        }
        cursor = at;
        return out;
    }

    /* Iterators, const only: writing through one could put a bin8 in a bin32 vec. */
    const_iterator begin() const noexcept { return _data.begin(); }
    const_iterator end() const noexcept { return _data.end(); }
    const_iterator cbegin() const noexcept { return _data.cbegin(); }
    const_iterator cend() const noexcept { return _data.cend(); }

    /* Equal if the type and every element match, in order. */
    bool operator==(const binVec& other) const = default;

};


/**
* @class binMap
* A hashmap from bin8 / bin32 / bin64 / binString keys to bin8 / bin32 / bin64 / binString / binPair values.
* Keys all share one rowType and values all share one rowType, both locked in by the first insert.
* On disk: [key rowType: 3 bits][value rowType: 3 bits][count: 32 bits][key][value]...
* @attention entries are written in hash order, so saving the same map twice can give different bytes.
* It always reads back to an equal map.
*/
class binMap{
    public:
    using leaf = _details::leaf;
    using item = _details::item;
    using const_iterator = std::unordered_map<leaf, item>::const_iterator;

    private:
    rowType _keyType = rowType::none;
    rowType _valueType = rowType::none;
    std::unordered_map<leaf, item> _data;

    /* true if @param key and @param value have the types this map is locked to (or it isn't locked yet). */
    bool typesMatch(const leaf& key, const item& value) const noexcept{
        return (_keyType == rowType::none || _keyType == _details::typeOf(key)) &&
               (_valueType == rowType::none || _valueType == _details::typeOf(value));
    }

    public:

    /* Create an empty binMap, its key and value types are decided by the first insert. */
    binMap(){}

    /**
    * Adds @param key -> @param value.
    * @returns false (and changes nothing) if the key already exists or either type doesn't match the map.
    */
    bool insert(const leaf& key, const item& value){
        if(!typesMatch(key, value)){
            return false;
        }
        if(!_data.try_emplace(key, value).second){
            return false;
        }
        _keyType = _details::typeOf(key);
        _valueType = _details::typeOf(value);
        return true;
    }

    /**
    * Adds @param key -> @param value, replacing the value if the key already exists.
    * @returns false (and changes nothing) only if either type doesn't match the map.
    */
    bool insert_or_assign(const leaf& key, const item& value){
        if(!typesMatch(key, value)){
            return false;
        }
        _data.insert_or_assign(key, value);
        _keyType = _details::typeOf(key);
        _valueType = _details::typeOf(value);
        return true;
    }

    /* @returns the value for @param key, `std::nullopt` if it isn't in the map. */
    std::optional<item> at(const leaf& key) const{
        const auto found = _data.find(key);
        if(found == _data.end()){
            return std::nullopt;
        }
        return found->second;
    }

    /**
    * Typed version of at(), e.g. map.at<bin32>(binString("age")).
    * @returns `std::nullopt` if @param key isn't in the map or its value isn't a T
    */
    template <_details::bin_item T>
    std::optional<T> at(const leaf& key) const{
        const auto found = _data.find(key);
        if(found == _data.end()){
            return std::nullopt;
        }
        if(const T* value = std::get_if<T>(&found->second)){
            return *value;
        }
        return std::nullopt;
    }

    bool contains(const leaf& key) const{
        return _data.contains(key);
    }

    /**
    * Removes @param key. @returns false if it wasn't in the map.
    * The map keeps its types even if this empties it, use clear() to reset them.
    */
    bool remove(const leaf& key){
        return _data.erase(key) > 0;
    }

    /* Removes every entry and unlocks both types. */
    void clear() noexcept{
        _data.clear();
        _keyType = rowType::none;
        _valueType = rowType::none;
    }

    /**
    * @returns amount of entries
    */
    std::size_t size() const noexcept{
        return _data.size();
    }

    /* The rowType every key has, `rowType::none` until the first insert (or after clear()). */
    rowType key_type() const noexcept{
        return _keyType;
    }

    /* The rowType every value has, `rowType::none` until the first insert (or after clear()). */
    rowType value_type() const noexcept{
        return _valueType;
    }

    /* Appends [key type][value type][count][key][value]... to @param out. @throws `std::length_error` past 2^32 - 1 entries */
    void write_bits(bit::BitBuffer& out) const{
        _details::writeUint(out, static_cast<std::uint64_t>(_keyType), _details::TYPE_BITS);
        _details::writeUint(out, static_cast<std::uint64_t>(_valueType), _details::TYPE_BITS);
        _details::writeCount(out, _data.size(), "binMap");
        for(const auto& [key, value] : _data){
            _details::writeAlternative(out, key);
            _details::writeAlternative(out, value);
        }
    }

    /**
    * Reads a binMap written by write_bits() at @param cursor.
    * @returns nullopt if the bits run out, a type can't go in a binMap, an entry is bad, or a key repeats.
    * The cursor only moves on success.
    */
    static std::optional<binMap> read_bits(const bit::BitBuffer& in, std::size_t& cursor){
        std::size_t at = cursor;
        const std::optional<rowType> keyType = _details::readType(in, at);
        const std::optional<rowType> valueType = keyType ? _details::readType(in, at) : std::nullopt;
        const std::optional<std::uint64_t> count = valueType ? _details::readUint(in, at, _details::COUNT_BITS) : std::nullopt;
        if(!count){
            return std::nullopt;
        }
        binMap out;
        if(*keyType == rowType::none || *valueType == rowType::none){
            // only a map that was never locked has no types, and it has to be empty
            if(*keyType != *valueType || *count != 0){
                return std::nullopt;
            }
            cursor = at;
            return out;
        }
        const std::size_t keyIndex = static_cast<std::size_t>(*keyType);
        const std::size_t valueIndex = static_cast<std::size_t>(*valueType);
        if(keyIndex >= std::variant_size_v<leaf> || valueIndex >= std::variant_size_v<item> ||
           !_details::fitsCount(in, at, *count, _details::minBits(*keyType) + _details::minBits(*valueType))){
            return std::nullopt;
        }
        out._keyType = *keyType;
        out._valueType = *valueType;
        out._data.reserve(static_cast<std::size_t>(*count));
        for(std::uint64_t i{0}; i < *count; i++){
            std::optional<leaf> key = _details::readAlternative<leaf>(in, at, keyIndex);
            if(!key){
                return std::nullopt;
            }
            std::optional<item> value = _details::readAlternative<item>(in, at, valueIndex);
            if(!value || !out._data.try_emplace(std::move(*key), std::move(*value)).second){
                return std::nullopt;
            }
        }
        cursor = at;
        return out;
    }

    /* Iterators over {key, value}, const only: writing through one could break the type lock. */
    const_iterator begin() const noexcept { return _data.begin(); }
    const_iterator end() const noexcept { return _data.end(); }
    const_iterator cbegin() const noexcept { return _data.cbegin(); }
    const_iterator cend() const noexcept { return _data.cend(); }

    /* Equal if both have the same types and the same entries, order doesn't matter. */
    bool operator==(const binMap& other) const = default;

};




/**
*   @class binFile @author Bryce Hart @date Sept 16th 2026
*   acts as storage space for all bintypes. It is up to caller to decide how to layout,
*   create, and retrieve this data. None of it is implied.
*   @attention: each row can only contain one type, for example, bin8 cannot exist on the same line
*   as bin64. The only exception to this rule is if the row type is defined as `binPair`, in that
*   case you can pair 2 types together and write them. It is up to caller to organize and keep track
*   of elements added.
*   @attention: the whole file is read into memory when opened. Changes stay in memory until save()
*   (or the destructor) writes them.
*
*   On disk:
*   [magic: u16 little endian][version: u8][reserved: u8, always 0]
*   then bits, padded with zeros to a full byte:
*   row zero: one 3 bit rowType per line, ended by rowType::none
*   each line: [count: 32 bits][items, their type comes from row zero]
*/
class binFile{
    public:
    using allowedTypes = std::variant<bin8, bin32, bin64, binString, binPair, binVec, binMap>;

    private:
    using u8 = std::uint8_t;
    static constexpr std::uint16_t MAGIC = 0xB1F1;
    static constexpr u8 VERSION = 1;
    static constexpr std::size_t HEADER_BYTES = 4;

    std::string _path;
    std::vector<rowType> _rowZeroType;            // type of each line, row zero on disk
    std::vector<std::vector<allowedTypes>> _rows; // the items of each line
    std::size_t _objectAmount{0};
    bool _dirty{false};                           // changed since the last load or save


    rowType whatIsTypeAt(const std::size_t& index) const noexcept{
        if(index >= _rowZeroType.size()){
            return rowType::none;
        }
        return _rowZeroType[index];
    }

    std::runtime_error corrupt(const std::string& what) const{
        return std::runtime_error("binFile " + _path + " is corrupt: " + what + ".");
    }

    /* Fills the rows from a whole file's @param bytes. @throws `std::runtime_error` saying what is wrong */
    void load(const std::vector<u8>& bytes){
        if(bytes.size() < HEADER_BYTES){
            throw std::runtime_error(_path + " is not a binFile (too short for a header).");
        }
        const std::uint16_t magic = static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
        if(magic != MAGIC){
            throw std::runtime_error(_path + " is not a binFile (wrong magic number).");
        }
        if(bytes[2] != VERSION){
            throw std::runtime_error("binFile " + _path + " is version " + std::to_string(bytes[2]) +
                                     ", this build only reads version " + std::to_string(VERSION) + ".");
        }
        if(bytes[3] != 0){
            throw corrupt("reserved header byte isn't 0");
        }

        const bit::BitBuffer body(std::vector<u8>(bytes.begin() + HEADER_BYTES, bytes.end()));
        std::size_t cursor{0};

        std::vector<rowType> types;
        while(true){
            const std::optional<rowType> type = _details::readType(body, cursor);
            if(!type){
                throw corrupt("row zero never ends");
            }
            if(*type == rowType::none){
                break;
            }
            types.push_back(*type);
        }

        std::vector<std::vector<allowedTypes>> rows;
        rows.reserve(types.size());
        std::size_t objects{0};
        for(std::size_t line{0}; line < types.size(); line++){
            const std::optional<std::uint64_t> count = _details::readUint(body, cursor, _details::COUNT_BITS);
            if(!count || !_details::fitsCount(body, cursor, *count, _details::minBits(types[line]))){
                throw corrupt("line " + std::to_string(line) + " is truncated");
            }
            std::vector<allowedTypes> row;
            row.reserve(static_cast<std::size_t>(*count));
            for(std::uint64_t i{0}; i < *count; i++){
                std::optional<allowedTypes> object = _details::readAlternative<allowedTypes>(body, cursor, static_cast<std::size_t>(types[line]));
                if(!object){
                    throw corrupt("item " + std::to_string(i) + " of line " + std::to_string(line) + " can't be read");
                }
                row.push_back(std::move(*object));
            }
            objects += row.size();
            rows.push_back(std::move(row));
        }

        // all that may be left is the zero padding of the last byte
        const std::size_t left = body.size() - cursor;
        if(left >= 8 || *_details::readUint(body, cursor, left) != 0){
            throw corrupt("there is data after the last line");
        }

        _rowZeroType = std::move(types);
        _rows = std::move(rows);
        _objectAmount = objects;
    }


    public:

    /**
    * Opens @param fileName. An existing file is read into memory, a missing one starts empty
    * and isn't created until save().
    * @throws `std::invalid_argument` if the name is empty
    * @throws `std::runtime_error` if the file exists but can't be read, isn't a binFile, or is corrupt
    */
    explicit binFile(const std::string& fileName) : _path(fileName){
        if(fileName.empty()){
            throw std::invalid_argument("binFile must have a name.");
        }
        std::error_code ec;
        if(!std::filesystem::exists(_path, ec)){
            if(ec){
                throw std::runtime_error("could not check whether " + _path + " exists: " + ec.message());
            }
            return;
        }
        std::ifstream in(_path, std::ios::binary);
        if(!in){
            throw std::runtime_error("could not open " + _path + " for reading.");
        }
        const std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if(in.bad()){
            throw std::runtime_error("could not read " + _path + ".");
        }
        load(bytes);
    }

    /* Two binFiles on one path would overwrite each other's saves, so no copies. */
    binFile(const binFile&) = delete;
    binFile& operator=(const binFile&) = delete;

    binFile(binFile&& other) noexcept :
        _path(std::move(other._path)),
        _rowZeroType(std::move(other._rowZeroType)),
        _rows(std::move(other._rows)),
        _objectAmount(std::exchange(other._objectAmount, 0)),
        _dirty(std::exchange(other._dirty, false)) {}

    /* Saves this file's unsaved changes (if any) before taking over @param other. */
    binFile& operator=(binFile&& other) noexcept{
        if(this != &other){
            if(_dirty){
                try{ save(); }catch(...){}
            }
            _path = std::move(other._path);
            _rowZeroType = std::move(other._rowZeroType);
            _rows = std::move(other._rows);
            _objectAmount = std::exchange(other._objectAmount, 0);
            _dirty = std::exchange(other._dirty, false);
        }
        return *this;
    }

    /* Saves unsaved changes. A destructor can't report failure, call save() yourself to check it. */
    ~binFile(){
        if(_dirty){
            try{ save(); }catch(...){}
        }
    }

    /**
    * push @param data to the end of @param line.
    * @returns false if the line doesn't exist or data isn't that line's type
    */
    bool push(const std::size_t& line, const allowedTypes& data){
        if(whatIsTypeAt(line) != _details::typeOf(data)){ // a missing line is none, which never matches
            return false;
        }
        _rows[line].push_back(data);
        _objectAmount++;
        _dirty = true;
        return true;
    }

    /* Start a new line with @param data, the line's type becomes data's type. */
    bool pushNewLine(const allowedTypes& data){
        _rowZeroType.reserve(_rowZeroType.size() + 1); // so the push_back below can't throw after _rows has grown
        _rows.push_back({data});
        _rowZeroType.push_back(_details::typeOf(data));
        _objectAmount++;
        _dirty = true;
        return true;
    }

    /**
    * read item @param item_in_row from @param line.
    * @returns `std::nullopt` if that item doesn't exist
    */
    std::optional<allowedTypes> readObject(const std::size_t line, const std::size_t item_in_row) const{
        if(line >= _rows.size() || item_in_row >= _rows[line].size()){
            return std::nullopt;
        }
        return _rows[line][item_in_row];
    }

    /**
    * Typed version of readObject(), e.g. file.readAs<bin32>(0, 2).
    * @returns `std::nullopt` if that item doesn't exist or isn't a T
    */
    template <_details::bin_object T>
    std::optional<T> readAs(const std::size_t line, const std::size_t item_in_row) const{
        if(line >= _rows.size() || item_in_row >= _rows[line].size()){
            return std::nullopt;
        }
        if(const T* value = std::get_if<T>(&_rows[line][item_in_row])){
            return *value;
        }
        return std::nullopt;
    }

    /**
    * Overwrite item @param item_in_row of @param line with @param data.
    * @returns false if that item doesn't exist or data isn't the line's type
    */
    bool replaceObject(const std::size_t line, const std::size_t item_in_row, const allowedTypes& data){
        if(whatIsTypeAt(line) != _details::typeOf(data) || item_in_row >= _rows[line].size()){
            return false;
        }
        _rows[line][item_in_row] = data;
        _dirty = true;
        return true;
    }

    /**
    * Remove item @param item_in_row from @param line. The line stays, even if it is now empty,
    * so no other line number changes.
    * @returns false if that item doesn't exist
    */
    bool removeObject(const std::size_t line, const std::size_t item_in_row){
        if(line >= _rows.size() || item_in_row >= _rows[line].size()){
            return false;
        }
        _rows[line].erase(_rows[line].begin() + static_cast<std::ptrdiff_t>(item_in_row));
        _objectAmount--;
        _dirty = true;
        return true;
    }

    /**
    * Delete @param line and everything in it. Every line after it moves down by one.
    * Line 0 is the first line the caller wrote, row zero (the type list) is never visible.
    * @returns false if the line doesn't exist
    */
    bool deleteLine(const std::size_t line){
        if(line >= _rows.size()){
            return false;
        }
        _objectAmount -= _rows[line].size();
        _rows.erase(_rows.begin() + static_cast<std::ptrdiff_t>(line));
        _rowZeroType.erase(_rowZeroType.begin() + static_cast<std::ptrdiff_t>(line));
        _dirty = true;
        return true;
    }

    /* @returns the type of @param line, `std::nullopt` if it doesn't exist */
    std::optional<rowType> lineType(const std::size_t line) const noexcept{
        if(line >= _rowZeroType.size()){
            return std::nullopt;
        }
        return _rowZeroType[line];
    }

    /* @returns how many items @param line holds, `std::nullopt` if it doesn't exist */
    std::optional<std::size_t> lineSize(const std::size_t line) const noexcept{
        if(line >= _rows.size()){
            return std::nullopt;
        }
        return _rows[line].size();
    }

    std::size_t lineAmount() const noexcept{
        return _rows.size();
    }

    /* @returns how many items there are across every line */
    std::size_t objectAmount() const noexcept{
        return _objectAmount;
    }

    /* true if there are changes save() hasn't written yet */
    bool hasUnsavedChanges() const noexcept{
        return _dirty;
    }

    /**
    * Writes everything to `<fileName>.tmp`, then renames it over the file, so a crash
    * mid-save leaves the old file intact instead of half of the new one.
    * @returns false if the file couldn't be written (the old file is untouched)
    * @throws `std::length_error` if a line, string, vec, or map has more than 2^32 - 1 elements
    */
    bool save(){
        bit::BitBuffer body;
        for(const rowType type : _rowZeroType){
            _details::writeUint(body, static_cast<std::uint64_t>(type), _details::TYPE_BITS);
        }
        _details::writeUint(body, static_cast<std::uint64_t>(rowType::none), _details::TYPE_BITS);
        for(const std::vector<allowedTypes>& row : _rows){
            _details::writeCount(body, row.size(), "binFile line");
            for(const allowedTypes& object : row){
                _details::writeAlternative(body, object);
            }
        }

        const std::string tmp = _path + ".tmp";
        std::error_code ec;
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if(!out){
                return false;
            }
            const u8 header[HEADER_BYTES] = {
                static_cast<u8>(MAGIC & 0xFF), static_cast<u8>(MAGIC >> 8), VERSION, 0
            };
            out.write(reinterpret_cast<const char*>(header), HEADER_BYTES);
            out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.byteSize()));
            out.flush();
            if(!out){
                out.close();
                std::filesystem::remove(tmp, ec);
                return false;
            }
        }
        std::filesystem::rename(tmp, _path, ec);
        if(ec){
            std::filesystem::remove(tmp, ec);
            return false;
        }
        _dirty = false;
        return true;
    }

};

static_assert(_details::indexesMatchRowType(static_cast<binFile::allowedTypes*>(nullptr)));

}//store
}//bstd
