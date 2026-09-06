#pragma once
#include <bit>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <string>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

namespace bstd{
namespace store {





#if !defined(__APPLE__) && !defined(__linux__) && !defined(__unix__) && !defined(__unix)
    #error "This library (bstd::storage::binType) and (bstd::storage::safeList) are not designed for non UNIX/APPLE systems."
#endif

/**
* The type discriminant written as the first byte of every serialized binType.
* @attention These values land on disk. Never renumber them, and only ever append.
* The order matches the alternative order of binType::supportedTypes, so
* static_cast<Tag>(variant.index()) is always the same as the stored tag.
* @attention Renumbered once, when std::string was dropped so that every payload
* is fixed width. Anything written with the older numbering (where STRING was 0
* and every other tag sat one higher) will not read back correctly.
*/
enum class Tag : std::uint8_t {
    CHAR   = 0,
    U8     = 1,
    U16    = 2,
    U32    = 3,
    U64    = 4,
    I8     = 5,
    I16    = 6,
    I32    = 7,
    I64    = 8,
    FLOAT  = 9,
    DOUBLE = 10,
};


//detail namespace for private members. 
namespace _details {

    using u8  = std::uint8_t;
    using i8  = std::int8_t;
    using u16 = std::uint16_t;
    using i16 = std::int16_t;
    using u32 = std::uint32_t;
    using i32 = std::int32_t;
    using u64 = std::uint64_t;
    using i64 = std::int64_t;

    template<typename T> struct TypeTag;  //leave this one undefined to catch types we dont want
    template<> struct TypeTag<char> {static constexpr Tag value = Tag::CHAR;};
    template<> struct TypeTag<u8> {static constexpr Tag value = Tag::U8;};
    template<> struct TypeTag<u16> {static constexpr Tag value = Tag::U16;}; //same as unsigned short
    template<> struct TypeTag<u32> {static constexpr Tag value = Tag::U32;}; //same as unsigned int
    template<> struct TypeTag<u64> {static constexpr Tag value = Tag::U64;};
    template<> struct TypeTag<i8> {static constexpr Tag value = Tag::I8;};
    template<> struct TypeTag<i16> {static constexpr Tag value = Tag::I16;};
    template<> struct TypeTag<i32> {static constexpr Tag value = Tag::I32;};
    template<> struct TypeTag<i64> {static constexpr Tag value = Tag::I64;};
    template<> struct TypeTag<float> {static constexpr Tag value = Tag::FLOAT;};
    template<> struct TypeTag<double> {static constexpr Tag value = Tag::DOUBLE;};

    template<typename T>
    inline constexpr Tag TypeTag_v = TypeTag<T>::value; //type

    /**
    * Folds the platform-dependent spellings of a type onto the one alternative we
    * actually store. `long` is 64 bit on both Mac and Linux but is a *distinct type*
    * from `long long`, and which of the two int64_t names differs between them --
    * normalizing here is what keeps a file written on one readable on the other.
    */
    template<typename T>
    struct normalize {
        private:
        using D = std::decay_t<T>;
        public:
        using type =
            std::conditional_t<std::is_same_v<D, long> || std::is_same_v<D, long long>, i64,
            std::conditional_t<std::is_same_v<D, unsigned long> || std::is_same_v<D, unsigned long long>, u64,
            D>>;
    };
    template<typename T> using normalize_t = typename normalize<T>::type;

    // Satisfied only by types that have a TypeTag, so an unsupported type gives
    // "no matching constructor" instead of an error deep inside the variant.
    template<typename T>
    concept supported = requires { TypeTag<normalize_t<T>>::value; };

    inline const char* tagName(Tag t) noexcept {
        switch(t){
            case Tag::CHAR:   return "char";
            case Tag::U8:     return "u8";
            case Tag::U16:    return "u16";
            case Tag::U32:    return "u32";
            case Tag::U64:    return "u64";
            case Tag::I8:     return "i8";
            case Tag::I16:    return "i16";
            case Tag::I32:    return "i32";
            case Tag::I64:    return "i64";
            case Tag::FLOAT:  return "float";
            case Tag::DOUBLE: return "double";
        }
        return "?";
    }

}


/**
* @author Bryce Hart @date Aug 26
* converts most types into its binary representation, safely.
* can also be converted to other types safely 
* @param Type - pass in type to be converted to binary
* @attention Fixed width types only. std::string (and so string literals) are
* deliberately unsupported: every payload is exactly payloadWidth(tag) bytes, which
* is what lets a flat buffer of one type be sliced without a length prefix.
* @attention For Linux/Mac specifically because of the type discrepancies for
* long/long long conversions (for now, may update in later versions)
* @attention updated Sept 5th to no longer need write bin dependency.
*/
class binType{
    public:
    using u8 = std::uint8_t;
    using i8 = std::int8_t;
    using u16 = std::uint16_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i64 = std::int64_t;
    using Tag = bstd::store::Tag;
    using supportedTypes = std::variant<char, u8, u16, u32, u64,
    i8, i16, i32, i64, float, double>;

    private:
    supportedTypes data;
    Tag tag;



    template<std::integral T>
    static T to_little_end(T value){
        if constexpr(std::endian::native == std::endian::big)
            return byteswap(value);
        return value;
    }

    template<std::integral T>
    static T from_little_end(T value){
        return to_little_end(value); // swap is its own inverse
    }



    public:

    /**
    * A default binType holds u8{0}, so a resized vector<binType> is well defined.
    */
    binType() : data(std::in_place_type<u8>, u8{0}), tag(Tag::U8) {}

    /**
    * Stores @param v and records its type.
    * @attention Unsupported types (bool, std::string, pointers, containers...) have no TypeTag,
    * so they fail the `supported` concept and are rejected at compile time.
    */
    template<typename T>
    requires (!std::is_same_v<std::remove_cvref_t<T>, binType>) && _details::supported<T>
    explicit binType(T&& v) : data(std::in_place_type<_details::normalize_t<T>>, static_cast<_details::normalize_t<T>>(std::forward<T>(v))),
    tag(_details::TypeTag_v<_details::normalize_t<T>>) {}



    // The type this binType is holding.
    Tag type() const noexcept{ 
        return tag;
    }

    // Printable name of the held type.
    const char* typeName() const noexcept{
        return _details::tagName(tag); 
    }

    // The underlying variant, for std::visit.
    const supportedTypes& raw() const noexcept{ 
        return data; 
    }

    /** True if the held type is T. */
    template<typename T>
    bool holds() const noexcept{
        return std::holds_alternative<_details::normalize_t<T>>(data);
    }

    /**
    * Attempts to return the value of the binType.
    * @returns the value as the type if possible, if not, returns nullopt.
    */
    template<typename T>
    std::optional<_details::normalize_t<T>> value() const {
        using N = _details::normalize_t<T>;
        if(const N* p = std::get_if<N>(&data)){
            return *p;
        }
        return std::nullopt;
    }

    /**
    * Like value(), but throws instead of returning nullopt.
    * @throws std::runtime_error naming both the stored and the requested type
    */
    template<typename T>
    _details::normalize_t<T> get() const {
        auto v = value<T>();
        if(!v){
            throw std::runtime_error(
                std::string("binType::get: holds ") + typeName() + ", requested " +
                _details::tagName(_details::TypeTag_v<_details::normalize_t<T>>));
        }
        return *v;
    }

    /** 
    * Human readable form, for logging and test output. 
    */
    std::string toString() const {
        return std::visit([](const auto& v) -> std::string {
            using V = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<V, char>)        return std::string("'") + v + "'";
            else if constexpr (std::is_same_v<V, u8> || std::is_same_v<V, i8>)
                return std::to_string(static_cast<int>(v));
            else return std::to_string(v);
        }, data);
    }

    bool operator==(const binType& other) const {
        return tag == other.tag && data == other.data;
    }



    /**
    * @returns the payload size in bytes for @param t, not counting the tag byte.
    * Total for every tag, since no supported type is variable width.
    * @returns 0 for a Tag value outside the enum, which is never a valid payload size.
    */
    static constexpr std::size_t payloadWidth(Tag t) noexcept {
        switch(t){
            case Tag::CHAR:   return sizeof(char);
            case Tag::U8:     return sizeof(u8);
            case Tag::U16:    return sizeof(u16);
            case Tag::U32:    return sizeof(u32);
            case Tag::U64:    return sizeof(u64);
            case Tag::I8:     return sizeof(i8);
            case Tag::I16:    return sizeof(i16);
            case Tag::I32:    return sizeof(i32);
            case Tag::I64:    return sizeof(i64);
            case Tag::FLOAT:  return sizeof(float);
            case Tag::DOUBLE: return sizeof(double);
        }
        return 0;
    }

    /**
    * @returns the number of bytes toBytes() will produce for this value.
    */
    std::size_t byteSize() const noexcept {
        return 1 + payloadWidth(tag);
    }

    /**
    * The on-disk form: [tag: 1 byte][payload].
    * Integers are little-endian and floats are bit_cast to their integer form first,
    * so NaN, infinity, denormals and -0.0 survive exactly. The payload is always
    * payloadWidth(tag) bytes, so a reader always knows where the next record starts.
    */
    std::vector<u8> toBytes() const {
        std::vector<u8> out;
        out.reserve(byteSize());
        out.push_back(static_cast<u8>(tag));
        std::visit([&out](const auto& v){ appendPayload(out, v); }, data);
        return out;
    }

    /**
    * @returns the same bytes as toBytes(), one bitset per byte. Debug/display helper.
    */
    std::vector<std::bitset<8>> getAsBitset() const {
        const std::vector<u8> bytes = toBytes();
        std::vector<std::bitset<8>> out;
        out.reserve(bytes.size());
        for(u8 b : bytes){
            out.emplace_back(b);
        }
        return out;
    }

    /**
    * Rebuilds a binType from @param in starting at @param offset, then advances
    * offset past the record it read so a buffer of concatenated records can be
    * walked in one pass.
    * @throws std::runtime_error on an unknown tag byte or a truncated payload.
    * Never reads past in.size().
    */
    static binType fromBytes(const std::vector<u8>& in, std::size_t& offset){
        if(offset >= in.size()){
            throw std::runtime_error("binType::fromBytes: buffer exhausted");
        }
        const u8 rawTag = in[offset++];
        switch(static_cast<Tag>(rawTag)){
            case Tag::CHAR:   return binType(readLE<char>(in, offset));
            case Tag::U8:     return binType(readLE<u8>(in, offset));
            case Tag::U16:    return binType(readLE<u16>(in, offset));
            case Tag::U32:    return binType(readLE<u32>(in, offset));
            case Tag::U64:    return binType(readLE<u64>(in, offset));
            case Tag::I8:     return binType(readLE<i8>(in, offset));
            case Tag::I16:    return binType(readLE<i16>(in, offset));
            case Tag::I32:    return binType(readLE<i32>(in, offset));
            case Tag::I64:    return binType(readLE<i64>(in, offset));
            case Tag::FLOAT:  return binType(std::bit_cast<float>(readLE<u32>(in, offset)));
            case Tag::DOUBLE: return binType(std::bit_cast<double>(readLE<u64>(in, offset)));
        }
        throw std::runtime_error("binType::fromBytes: unknown tag byte " + std::to_string(static_cast<int>(rawTag)));
    }

    /**
    * Single record overload. @throws if @param in holds trailing bytes.
    */
    static binType fromBytes(const std::vector<u8>& in){
        std::size_t offset = 0;
        binType out = fromBytes(in, offset);
        if(offset != in.size()){
            throw std::runtime_error("binType::fromBytes: " + std::to_string(in.size() - offset) + " trailing byte(s) after record");
        }
        return out;
    }

    /**
    * Builds a binType out of a *payload only* buffer, with @param tag supplying the
    * type from somewhere else (a record header, a column descriptor...). @param in
    * carries no tag byte, so this pairs with toBytes() minus its first byte, and
    * must be exactly payloadWidth(tag) bytes long.
    * The payload layout is otherwise identical: little-endian integers and floats
    * in their bit_cast integer form.
    * @returns nullopt instead of throwing on a tag outside the enum, a truncated
    * payload, or bytes left over after the value. Never reads past in.size().
    */
    static std::optional<binType> fromBytes(const std::vector<u8>& in, Tag tag){
        std::size_t offset = 0;
        std::optional<binType> out;
        switch(tag){
            case Tag::CHAR:   { const auto v = tryReadLE<char>(in, offset); if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::U8:     { const auto v = tryReadLE<u8>(in, offset);   if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::U16:    { const auto v = tryReadLE<u16>(in, offset);  if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::U32:    { const auto v = tryReadLE<u32>(in, offset);  if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::U64:    { const auto v = tryReadLE<u64>(in, offset);  if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::I8:     { const auto v = tryReadLE<i8>(in, offset);   if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::I16:    { const auto v = tryReadLE<i16>(in, offset);  if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::I32:    { const auto v = tryReadLE<i32>(in, offset);  if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::I64:    { const auto v = tryReadLE<i64>(in, offset);  if(!v) return std::nullopt; out = binType(*v); break; }
            case Tag::FLOAT:  { const auto v = tryReadLE<u32>(in, offset);  if(!v) return std::nullopt; out = binType(std::bit_cast<float>(*v)); break; }
            case Tag::DOUBLE: { const auto v = tryReadLE<u64>(in, offset);  if(!v) return std::nullopt; out = binType(std::bit_cast<double>(*v)); break; }
        }
        if(!out){ // tag value outside the enum fell straight past the switch
            return std::nullopt;
        }
        if(offset != in.size()){ // strict: one value, nothing after it
            return std::nullopt;
        }
        return out;
    }


    private:

    template<std::integral T>
    static void appendPayload(std::vector<u8>& out, T v){
        const T le = to_little_end(v);
        const u8* p = reinterpret_cast<const u8*>(&le);
        out.insert(out.end(), p, p + sizeof(T));
    }

    static void appendPayload(std::vector<u8>& out, float v){
        appendPayload(out, std::bit_cast<u32>(v));
    }

    static void appendPayload(std::vector<u8>& out, double v){
        appendPayload(out, std::bit_cast<u64>(v));
    }

    /**
    * Bounds-checked little-endian read that advances @param offset, and only on
    * success -- a failed read leaves offset where it was.
    * @returns nullopt rather than throwing, so the optional-returning path can use it.
    */
    template<std::integral T>
    static std::optional<T> tryReadLE(const std::vector<u8>& in, std::size_t& offset) noexcept {
        if(offset > in.size() || in.size() - offset < sizeof(T)){
            return std::nullopt;
        }
        T le{};
        std::memcpy(&le, in.data() + offset, sizeof(T));
        offset += sizeof(T);
        return from_little_end(le);
    }

    /** Like tryReadLE, but throws on a truncated payload. */
    template<std::integral T>
    static T readLE(const std::vector<u8>& in, std::size_t& offset){
        const std::optional<T> v = tryReadLE<T>(in, offset);
        if(!v){
            throw std::runtime_error("binType::fromBytes: truncated payload");
        }
        return *v;
    }

};
}
}