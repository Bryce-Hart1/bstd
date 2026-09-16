#pragma once
#include <bit>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <string_view>

namespace bstd{
namespace store{

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

    // Anything that is NOT exactly one of the four types above
    template <typename T>
    bin8(T) = delete;

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
    bin32(T) = delete;

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
    bin64(T) = delete;

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
