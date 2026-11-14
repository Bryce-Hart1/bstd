#pragma once
/**
* bstd::net::json @author Bryce Hart @date Nov 2026
* Requires C++17 or newer. Header only, no OS specific code.
*
* @details A JSON value type, a strict RFC 8259 parser, a printer, and conversions to and from your own types.
*
*   using namespace bstd::net;
*   auto parsed = json::parse(R"({"name":"bob","tags":["a","b"]})");   // Result<json::Value, json::Error>
*   if(!parsed){ std::cout << parsed.unwrapErr().message(); }         // "line 1, column 9: ..."
*   json::Value v = parsed.unwrap();
*   std::string name = v["name"].getOr<std::string>("anon");          // const [] never throws
*   v["tags"].pushBack("c");
*   std::cout << v.dump(2);
*
*   struct Point{ int x; int y; std::optional<std::string> label; };
*   BSTD_JSON_FIELDS(Point, x, y, label);                              // same namespace as Point
*   json::Value j = json::toValue(Point{1, 2, {}});                    // {"x":1,"y":2}
*   auto p = json::fromValue<Point>(j);                                // Result<Point, json::Error>
*
* Numbers: whole numbers that fit in int64 are stored as Int, everything else as Double.
* Objects keep keys in insertion order. Lookup is a linear scan, so very large objects are slow to search.
* Parse errors and decode errors come back as Result. Using a value as the wrong type through
* as*() / at() / non-const operator[] is a programmer error and throws json::TypeError or std::out_of_range.
*/
#include <algorithm>
#include <array>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <locale>
#include <map>
#include <numeric>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#if defined(__has_include)
    #if __has_include(<version>)
        #include <version>
    #endif
#endif
#include "../error/errorType.hpp"

/* std::from_chars / std::to_chars for double are the fast, locale independent path.
 * libc++ (Apple clang) has them but doesn't define __cpp_lib_to_chars (it lacks long double), and on Apple they only
 * exist on new enough deployment targets, so check its availability macros. Otherwise fall back to iostreams
 * with the classic "C" locale. Define BSTD_JSON_NO_FLOAT_CHARCONV to force the fallback.                         */
#if defined(BSTD_JSON_NO_FLOAT_CHARCONV)
    #define BSTD_JSON_FLOAT_FROM_CHARS 0
    #define BSTD_JSON_FLOAT_TO_CHARS 0
#elif defined(__cpp_lib_to_chars)
    #define BSTD_JSON_FLOAT_FROM_CHARS 1
    #define BSTD_JSON_FLOAT_TO_CHARS 1
#elif defined(_LIBCPP_VERSION)
    #if _LIBCPP_VERSION >= 200000 && defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) && _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT
        #define BSTD_JSON_FLOAT_FROM_CHARS 1
    #else
        #define BSTD_JSON_FLOAT_FROM_CHARS 0
    #endif
    #if _LIBCPP_VERSION >= 140000 && defined(_LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT) && _LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT
        #define BSTD_JSON_FLOAT_TO_CHARS 1
    #else
        #define BSTD_JSON_FLOAT_TO_CHARS 0
    #endif
#else
    #define BSTD_JSON_FLOAT_FROM_CHARS 0
    #define BSTD_JSON_FLOAT_TO_CHARS 0
#endif

namespace bstd{
namespace net{
namespace json{

class Value;
class Object;
using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;

/* The order matches Value::Storage, so static_cast<Type>(storage.index()) is the type.
 * GCC's -Wshadow flags Type::Array / Type::Object against the Array / Object types even though scoped enumerators
 * can't clash with them, so it's turned off for just this enum.                                                   */
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wshadow"
#endif
enum class Type : std::uint8_t{
    Null   = 0,
    Bool   = 1,
    Int    = 2,
    Double = 3,
    String = 4,
    Array  = 5,
    Object = 6,
};
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif

inline const char* typeName(const Type t) noexcept{
    switch(t){
        case Type::Null:   return "null";
        case Type::Bool:   return "bool";
        case Type::Int:    return "int";
        case Type::Double: return "double";
        case Type::String: return "string";
        case Type::Array:  return "array";
        case Type::Object: return "object";
    }
    return "unknown";
}

/* Thrown when a Value is used as a type it isn't (asString() on a number, pushBack() on an object, ...). */
class TypeError : public std::logic_error{
public:
    using std::logic_error::logic_error;
};

/**
* What went wrong while parsing text or decoding a Value into a C++ type.
* Parse errors carry a byte offset plus line/column (1 based, column counted in bytes).
* Decode errors carry a JSON Pointer path to the bad value, like "/users/0/age".
* message() is the full human readable text; reason() is the text without the location.
*/
class Error{
public:
    enum class Kind : std::uint8_t{
        Syntax,         // malformed JSON text
        TooDeep,        // nesting went past ParseOptions::maxDepth
        NumberRange,    // a number too large for a double
        InvalidUnicode, // bad UTF-8, or a bad / unpaired \u surrogate
        DuplicateKey,   // with DuplicateKeys::reject
        WrongType,      // decode: the value isn't the type the C++ side wants
        MissingField,   // decode: a required object field isn't there
        OutOfRange,     // decode: a number doesn't fit the C++ type, or an array has the wrong length
        Custom,         // returned by user fromJson() hooks
    };

private:
    Kind _kind{Kind::Custom};
    std::string _reason;
    std::string _path;
    std::string _message;
    bool _hasLocation{false};
    std::size_t _offset{0}, _line{0}, _column{0};

    void rebuild(){
        if(_hasLocation){
            _message = "line " + std::to_string(_line) + ", column " + std::to_string(_column) + ": " + _reason;
        } else if(!_path.empty()){
            _message = "at " + _path + ": " + _reason;
        } else{
            _message = _reason;
        }
    }

public:
    Error() = default;
    Error(std::string reason) : Error(Kind::Custom, std::move(reason)) {}
    Error(const char* reason) : Error(Kind::Custom, std::string(reason)) {}
    Error(const Kind kind, std::string reason) : _kind(kind), _reason(std::move(reason)) { rebuild(); }

    /* A parse error at byte @param offset of the input. */
    static Error at(const Kind kind, std::string reason, const std::size_t offset, const std::size_t line, const std::size_t column){
        Error e;
        e._kind = kind;
        e._reason = std::move(reason);
        e._hasLocation = true;
        e._offset = offset;
        e._line = line;
        e._column = column;
        e.rebuild();
        return e;
    }

    Kind kind() const noexcept { return _kind; }
    const std::string& reason() const noexcept { return _reason; }
    const std::string& message() const noexcept { return _message; }
    /* JSON Pointer to the value that failed to decode. Empty for parse errors and for the root value. */
    const std::string& path() const noexcept { return _path; }
    bool hasLocation() const noexcept { return _hasLocation; }
    std::size_t offset() const noexcept { return _offset; }
    std::size_t line() const noexcept { return _line; }
    std::size_t column() const noexcept { return _column; }

    /* Puts an already escaped pointer segment ("/0", "/name") in front of the path. Used while unwinding a decode. */
    Error& prefixPath(std::string_view segment){
        _path.insert(0, segment);
        rebuild();
        return *this;
    }
};

template <typename T>
using Result = ::bstd::error::Result<T, Error>;
using ::bstd::error::Ok;
using ::bstd::error::Err;

namespace detail{

template <typename T>
inline constexpr bool isCharType = std::is_same_v<T, char> || std::is_same_v<T, wchar_t>
    || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>
#if defined(__cpp_char8_t)
    || std::is_same_v<T, char8_t>
#endif
    ;

/* Integral types a Value stores as a number. bool is its own type, and chars are left out so 'a' isn't silently 97. */
template <typename T>
inline constexpr bool isJsonInteger = std::is_integral_v<T> && !std::is_same_v<T, bool> && !isCharType<T>;

[[noreturn]] inline void throwType(const char* wanted, const Type actual){
    throw TypeError(std::string("bstd::net::json: expected ") + wanted + ", value is " + typeName(actual));
}

/* The value const operator[] hands back when a key or index isn't there. */
inline const Value& nullValue() noexcept;

/* Escapes one JSON Pointer reference token (RFC 6901): ~ becomes ~0 and / becomes ~1. Returns "/token". */
inline std::string pointerSegment(std::string_view token){
    std::string out;
    out.reserve(token.size() + 1);
    out.push_back('/');
    for(const char c : token){
        if(c == '~'){
            out += "~0";
        } else if(c == '/'){
            out += "~1";
        } else{
            out.push_back(c);
        }
    }
    return out;
}

inline std::string pointerSegment(const std::size_t index){
    return "/" + std::to_string(index);
}

}  // namespace detail

/**
* A JSON object: string keys mapped to values, kept in the order they were first added.
* Keys are unique. set() and operator[] replace an existing key in place instead of adding a second one.
* Iterating gives Member (std::pair<std::string, Value>) references.
*/
class Object{
public:
    using value_type = Member;
    using iterator = std::vector<Member>::iterator;
    using const_iterator = std::vector<Member>::const_iterator;

    Object();
    /* Object{{"name", "bob"}, {"age", 3}}. A repeated key keeps its first position and its last value. */
    Object(std::initializer_list<Member> members);
    Object(const Object& other);
    Object(Object&& other) noexcept;
    Object& operator=(const Object& other);
    Object& operator=(Object&& other) noexcept;
    ~Object();

    std::size_t size() const noexcept;
    bool empty() const noexcept;
    void reserve(std::size_t count);
    void clear() noexcept;

    iterator begin() noexcept;
    iterator end() noexcept;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;

    /* @returns the value at @param key, or nullptr. */
    Value* find(std::string_view key) noexcept;
    const Value* find(std::string_view key) const noexcept;
    bool contains(std::string_view key) const noexcept;

    /* @throws `std::out_of_range` if @param key isn't there. */
    Value& at(std::string_view key);
    const Value& at(std::string_view key) const;

    /* The value at @param key, adding a null one at the end if it isn't there. */
    Value& operator[](std::string_view key);

    /* Replaces the value at @param key, or appends it. @returns the stored value. */
    Value& set(std::string key, Value value);

    /* @returns true if @param key was there. Keeps the order of the rest. */
    bool erase(std::string_view key);
    iterator erase(const_iterator position);

    /* Same keys with equal values, in any order. */
    friend bool operator==(const Object& a, const Object& b);
    friend bool operator!=(const Object& a, const Object& b) { return !(a == b); }

private:
    friend class Parser;
    std::vector<Member> _members;
};

/**
* Any JSON value: null, bool, int (int64), double, string, Array or Object.
* Built implicitly from those C++ types:
*   json::Value v = json::Object{{"id", 7}, {"tags", json::Array{"a", "b"}}, {"ok", true}, {"next", nullptr}};
*/
class Value{
public:
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

private:
    Storage _data;

    template <typename T>
    static Storage fromInteger(const T v) noexcept{
        if constexpr(std::is_unsigned_v<T> && sizeof(T) >= sizeof(std::int64_t)){
            if(v > static_cast<T>(std::numeric_limits<std::int64_t>::max())){
                return Storage(std::in_place_index<3>, static_cast<double>(v));
            }
        }
        return Storage(std::in_place_index<2>, static_cast<std::int64_t>(v));
    }

    template <typename Self>
    static auto pointerImpl(Self& self, std::string_view ptr) -> decltype(&self);

public:
    Value() noexcept : _data(nullptr) {}
    Value(std::nullptr_t) noexcept : _data(nullptr) {}

    /* A template so pointers don't silently turn into bools. */
    template <typename T, std::enable_if_t<std::is_same_v<T, bool>, int> = 0>
    Value(const T b) noexcept : _data(std::in_place_index<1>, b) {}

    /* Unsigned values above INT64_MAX become a Double (and may lose precision). */
    template <typename T, std::enable_if_t<detail::isJsonInteger<T>, int> = 0>
    Value(const T v) noexcept : _data(fromInteger(v)) {}

    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    Value(const T v) noexcept : _data(std::in_place_index<3>, static_cast<double>(v)) {}

    /* A null pointer gives a null value. */
    Value(const char* s) : _data(nullptr) {
        if(s){
            _data.emplace<4>(s);
        }
    }
    Value(std::string s) noexcept : _data(std::in_place_index<4>, std::move(s)) {}
    Value(std::string_view s) : _data(std::in_place_index<4>, s) {}
    Value(Array a) noexcept : _data(std::in_place_index<5>, std::move(a)) {}
    Value(Object o) noexcept : _data(std::in_place_index<6>, std::move(o)) {}

    /* ---------- type checks ---------- */

    Type type() const noexcept { return static_cast<Type>(_data.index()); }
    bool isNull() const noexcept { return type() == Type::Null; }
    bool isBool() const noexcept { return type() == Type::Bool; }
    bool isInt() const noexcept { return type() == Type::Int; }
    bool isDouble() const noexcept { return type() == Type::Double; }
    /* Int or Double. */
    bool isNumber() const noexcept { return isInt() || isDouble(); }
    bool isString() const noexcept { return type() == Type::String; }
    bool isArray() const noexcept { return type() == Type::Array; }
    bool isObject() const noexcept { return type() == Type::Object; }

    const Storage& storage() const noexcept { return _data; }
    Storage& storage() noexcept { return _data; }

    /* ---------- typed access ---------- */

    /* Pointer to the stored alternative (std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object) or nullptr. */
    template <typename T>
    T* getIf() noexcept { return std::get_if<T>(&_data); }
    template <typename T>
    const T* getIf() const noexcept { return std::get_if<T>(&_data); }

    /* Strict accessors. @throws `json::TypeError` on the wrong type. asDouble() also accepts an Int. */
    bool asBool() const{
        if(const auto* p = getIf<bool>()){ return *p; }
        detail::throwType("bool", type());
    }
    std::int64_t asInt() const{
        if(const auto* p = getIf<std::int64_t>()){ return *p; }
        detail::throwType("int", type());
    }
    double asDouble() const{
        if(const auto* p = getIf<double>()){ return *p; }
        if(const auto* p = getIf<std::int64_t>()){ return static_cast<double>(*p); }
        detail::throwType("number", type());
    }
    /* On a temporary (json::parse(text).unwrap().asString()) these return by value, so nothing dangles. */
    const std::string& asString() const&{
        if(const auto* p = getIf<std::string>()){ return *p; }
        detail::throwType("string", type());
    }
    std::string& asString() &{
        if(auto* p = getIf<std::string>()){ return *p; }
        detail::throwType("string", type());
    }
    std::string asString() &&{ return std::move(asString()); }
    const Array& asArray() const&{
        if(const auto* p = getIf<Array>()){ return *p; }
        detail::throwType("array", type());
    }
    Array& asArray() &{
        if(auto* p = getIf<Array>()){ return *p; }
        detail::throwType("array", type());
    }
    Array asArray() &&{ return std::move(asArray()); }
    const Object& asObject() const&{
        if(const auto* p = getIf<Object>()){ return *p; }
        detail::throwType("object", type());
    }
    Object& asObject() &{
        if(auto* p = getIf<Object>()){ return *p; }
        detail::throwType("object", type());
    }
    Object asObject() &&{ return std::move(asObject()); }

    /**
    * Converts to T with the same rules as json::fromValue<T>(): numbers are range checked,
    * Int converts to floating types, and a Double with a whole value converts to integers.
    * @returns nullopt if it doesn't convert.
    */
    template <typename T>
    std::optional<T> get() const;

    /* get<T>(), or @param fallback if it doesn't convert. */
    template <typename T>
    T getOr(T fallback) const;

    /* ---------- objects and arrays ---------- */

    /* Never throws: a missing key, an index past the end, or a value that isn't an object/array gives a null. */
    const Value& operator[](std::string_view key) const noexcept{
        const Value* v = find(key);
        return v ? *v : detail::nullValue();
    }
    const Value& operator[](const std::size_t index) const noexcept{
        const auto* a = getIf<Array>();
        return (a && index < a->size()) ? (*a)[index] : detail::nullValue();
    }

    /**
    * A null value becomes an empty object first, then a missing key is added as null.
    * @throws `json::TypeError` if this is anything other than an object or null.
    * @attention on a non-const Value, reading with [] adds the key. Use find() or a const reference to only look.
    */
    Value& operator[](std::string_view key){
        if(isNull()){
            _data.emplace<Object>();
        }
        return asObject()[key];
    }
    /* @throws `json::TypeError` if this isn't an array, `std::out_of_range` if @param index is past the end. */
    Value& operator[](const std::size_t index){ return at(index); }

    /* @throws `json::TypeError` on the wrong type, `std::out_of_range` if missing. */
    Value& at(std::string_view key) { return asObject().at(key); }
    const Value& at(std::string_view key) const { return asObject().at(key); }
    Value& at(const std::size_t index){
        Array& a = asArray();
        if(index >= a.size()){
            throw std::out_of_range("bstd::net::json: index " + std::to_string(index) + " past the end of an array of " + std::to_string(a.size()));
        }
        return a[index];
    }
    const Value& at(const std::size_t index) const{
        const Array& a = asArray();
        if(index >= a.size()){
            throw std::out_of_range("bstd::net::json: index " + std::to_string(index) + " past the end of an array of " + std::to_string(a.size()));
        }
        return a[index];
    }

    /* @returns the member at @param key, or nullptr when missing or this isn't an object. */
    Value* find(std::string_view key) noexcept{
        auto* o = getIf<Object>();
        return o ? o->find(key) : nullptr;
    }
    const Value* find(std::string_view key) const noexcept{
        const auto* o = getIf<Object>();
        return o ? o->find(key) : nullptr;
    }
    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    /**
    * Looks up a JSON Pointer (RFC 6901) such as "/users/0/name". "" is this value itself.
    * @returns nullptr if any step is missing or the pointer is malformed.
    */
    Value* pointer(std::string_view ptr) { return pointerImpl(*this, ptr); }
    const Value* pointer(std::string_view ptr) const { return pointerImpl(*this, ptr); }

    /* Element count of an array or object, 0 for anything else. */
    std::size_t size() const noexcept{
        if(const auto* a = getIf<Array>()){ return a->size(); }
        if(const auto* o = getIf<Object>()){ return o->size(); }
        return 0;
    }
    bool empty() const noexcept { return size() == 0; }

    /* A null value becomes an empty array first. @throws `json::TypeError` if this is anything else. @returns the new element. */
    Value& pushBack(Value v){
        if(isNull()){
            _data.emplace<Array>();
        }
        Array& a = asArray();
        a.push_back(std::move(v));
        return a.back();
    }

    /* Compact text when @param indent is negative, otherwise one element per line indented by @param indent spaces. */
    std::string dump(int indent = -1) const;

    /* Deep equality. Int and Double compare by numeric value, objects ignore key order. */
    friend bool operator==(const Value& a, const Value& b);
    friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }
};

/* ---------- Object (needs a complete Value) ---------- */

inline Object::Object() = default;
inline Object::Object(const Object&) = default;
inline Object::Object(Object&&) noexcept = default;
inline Object& Object::operator=(const Object&) = default;
inline Object& Object::operator=(Object&&) noexcept = default;
inline Object::~Object() = default;

inline Object::Object(std::initializer_list<Member> members){
    _members.reserve(members.size());
    for(const Member& m : members){
        set(m.first, m.second);
    }
}

inline std::size_t Object::size() const noexcept { return _members.size(); }
inline bool Object::empty() const noexcept { return _members.empty(); }
inline void Object::reserve(const std::size_t count) { _members.reserve(count); }
inline void Object::clear() noexcept { _members.clear(); }
inline Object::iterator Object::begin() noexcept { return _members.begin(); }
inline Object::iterator Object::end() noexcept { return _members.end(); }
inline Object::const_iterator Object::begin() const noexcept { return _members.begin(); }
inline Object::const_iterator Object::end() const noexcept { return _members.end(); }

inline Value* Object::find(std::string_view key) noexcept{
    for(Member& m : _members){
        if(m.first == key){
            return &m.second;
        }
    }
    return nullptr;
}
inline const Value* Object::find(std::string_view key) const noexcept{
    for(const Member& m : _members){
        if(m.first == key){
            return &m.second;
        }
    }
    return nullptr;
}
inline bool Object::contains(std::string_view key) const noexcept { return find(key) != nullptr; }

inline Value& Object::at(std::string_view key){
    if(Value* v = find(key)){
        return *v;
    }
    throw std::out_of_range("bstd::net::json: no key \"" + std::string(key) + "\" in object");
}
inline const Value& Object::at(std::string_view key) const{
    if(const Value* v = find(key)){
        return *v;
    }
    throw std::out_of_range("bstd::net::json: no key \"" + std::string(key) + "\" in object");
}

inline Value& Object::operator[](std::string_view key){
    if(Value* v = find(key)){
        return *v;
    }
    _members.emplace_back(std::string(key), Value());
    return _members.back().second;
}

inline Value& Object::set(std::string key, Value value){
    if(Value* v = find(key)){
        *v = std::move(value);
        return *v;
    }
    _members.emplace_back(std::move(key), std::move(value));
    return _members.back().second;
}

inline bool Object::erase(std::string_view key){
    for(auto it = _members.begin(); it != _members.end(); ++it){
        if(it->first == key){
            _members.erase(it);
            return true;
        }
    }
    return false;
}
inline Object::iterator Object::erase(const_iterator position) { return _members.erase(position); }

inline bool operator==(const Object& a, const Object& b){
    if(a.size() != b.size()){
        return false;
    }
    if(a.size() <= 16){
        for(const Member& m : a){
            const Value* other = b.find(m.first);
            if(!other || !(m.second == *other)){
                return false;
            }
        }
        return true;
    }
    std::unordered_map<std::string_view, const Value*> index;
    index.reserve(b.size());
    for(const Member& m : b){
        index.emplace(m.first, &m.second);
    }
    for(const Member& m : a){
        const auto it = index.find(m.first);
        if(it == index.end() || !(m.second == *it->second)){
            return false;
        }
    }
    return true;
}

/* ---------- Value members that need Object ---------- */

namespace detail{

inline const Value& nullValue() noexcept{
    static const Value null;
    return null;
}

/* Int vs Double: equal only when the double is exactly that whole number. */
inline bool intEqualsDouble(const std::int64_t i, const double d) noexcept{
    if(!std::isfinite(d) || std::trunc(d) != d || d < -9223372036854775808.0 || d >= 9223372036854775808.0){
        return false;
    }
    return static_cast<std::int64_t>(d) == i;
}

}  // namespace detail

inline bool operator==(const Value& a, const Value& b){
    if(a.type() != b.type()){
        if(const auto* ai = a.getIf<std::int64_t>()){
            if(const auto* bd = b.getIf<double>()){ return detail::intEqualsDouble(*ai, *bd); }
        } else if(const auto* ad = a.getIf<double>()){
            if(const auto* bi = b.getIf<std::int64_t>()){ return detail::intEqualsDouble(*bi, *ad); }
        }
        return false;
    }
    return a.storage() == b.storage();
}

template <typename Self>
auto Value::pointerImpl(Self& self, std::string_view ptr) -> decltype(&self){
    decltype(&self) current = &self;
    if(ptr.empty()){
        return current;
    }
    if(ptr.front() != '/'){
        return nullptr;
    }
    std::string token;
    std::size_t pos = 1;
    for(;;){
        const std::size_t slash = ptr.find('/', pos);
        const std::string_view raw = ptr.substr(pos, slash == std::string_view::npos ? std::string_view::npos : slash - pos);
        token.clear();
        for(std::size_t i = 0; i < raw.size(); i++){
            if(raw[i] != '~'){
                token.push_back(raw[i]);
                continue;
            }
            if(i + 1 >= raw.size() || (raw[i + 1] != '0' && raw[i + 1] != '1')){
                return nullptr;  // "~" must be "~0" or "~1"
            }
            token.push_back(raw[i + 1] == '0' ? '~' : '/');
            i++;
        }
        if(auto* obj = current->template getIf<Object>()){
            current = obj->find(token);
        } else if(auto* arr = current->template getIf<Array>()){
            // array indexes are plain decimal with no leading zeros ("-", the past-the-end index, never resolves)
            if(token.empty() || (token.size() > 1 && token[0] == '0')){
                return nullptr;
            }
            std::size_t index = 0;
            const auto r = std::from_chars(token.data(), token.data() + token.size(), index);
            if(r.ec != std::errc{} || r.ptr != token.data() + token.size() || index >= arr->size()){
                return nullptr;
            }
            current = &(*arr)[index];
        } else{
            return nullptr;
        }
        if(!current){
            return nullptr;
        }
        if(slash == std::string_view::npos){
            return current;
        }
        pos = slash + 1;
    }
}

/* ---------- number text ---------- */

namespace detail{

/* Parses text that already matched the JSON number grammar. nullopt means out of range for a double. */
inline std::optional<double> parseDouble(std::string_view text){
#if BSTD_JSON_FLOAT_FROM_CHARS
    double d = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), d);
    if(r.ec == std::errc{} && r.ptr == text.data() + text.size()){
        return d;
    }
    return std::nullopt;
#else
    // strtod reads subnormals correctly (iostreams reject them), but it uses the C locale's decimal point,
    // so swap '.' for whatever setlocale() made it.
    std::string copy(text);
    const char* point = std::localeconv()->decimal_point;
    if(point && point[0] != '.' && point[0] != '\0'){
        const std::size_t dot = copy.find('.');
        if(dot != std::string::npos){
            copy.replace(dot, 1, point);
        }
    }
    char* end = nullptr;
    const double d = std::strtod(copy.c_str(), &end);
    if(end != copy.c_str() + copy.size()){
        return std::nullopt;
    }
    return d;  // overflow gives +-HUGE_VAL, which the caller rejects as not finite
#endif
}

/* For number text that failed to convert: true if its magnitude is above 1 (overflow), false if it's tiny (underflow). */
inline bool magnitudeAboveOne(std::string_view t) noexcept{
    std::size_t i = (!t.empty() && t[0] == '-') ? 1 : 0;
    long long leadExp = 0;
    bool found = false;
    const std::size_t intStart = i;
    while(i < t.size() && t[i] >= '0' && t[i] <= '9'){ i++; }
    const std::size_t intLen = i - intStart;
    for(std::size_t k = 0; k < intLen; k++){
        if(t[intStart + k] != '0'){
            leadExp = static_cast<long long>(intLen - k) - 1;
            found = true;
            break;
        }
    }
    if(i < t.size() && t[i] == '.'){
        i++;
        const std::size_t fracStart = i;
        while(i < t.size() && t[i] >= '0' && t[i] <= '9'){ i++; }
        for(std::size_t k = fracStart; !found && k < i; k++){
            if(t[k] != '0'){
                leadExp = -static_cast<long long>(k - fracStart) - 1;
                found = true;
            }
        }
    }
    long long exp = 0;
    if(i < t.size() && (t[i] == 'e' || t[i] == 'E')){
        i++;
        bool negative = false;
        if(i < t.size() && (t[i] == '+' || t[i] == '-')){
            negative = t[i] == '-';
            i++;
        }
        for(; i < t.size(); i++){
            if(exp < 100000000){
                exp = exp * 10 + (t[i] - '0');
            }
        }
        if(negative){
            exp = -exp;
        }
    }
    return found && leadExp + exp > 0;
}

/* Shortest text that reads back as exactly @param d. Always has a '.' or exponent, so it re-parses as a Double. */
inline void appendDouble(std::string& out, const double d){
    const std::size_t start = out.size();
#if BSTD_JSON_FLOAT_TO_CHARS
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), d);
    out.append(buf, r.ptr);
#else
    for(int precision = 15; precision <= 17; precision++){
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s.precision(precision);
        s << d;
        const std::string text = s.str();
        const auto back = parseDouble(text);
        if(precision == 17 || (back && *back == d)){
            out += text;
            break;
        }
    }
#endif
    if(out.find_first_of(".eE", start) == std::string::npos){
        out += ".0";
    }
}

inline void appendInt(std::string& out, const std::int64_t i){
    char buf[24];
    const auto r = std::to_chars(buf, buf + sizeof(buf), i);
    out.append(buf, r.ptr);
}

/* ---------- UTF-8 ---------- */

inline void appendUtf8(std::string& out, const std::uint32_t cp){
    if(cp < 0x80){
        out.push_back(static_cast<char>(cp));
    } else if(cp < 0x800){
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if(cp < 0x10000){
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else{
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/**
* Length of the valid UTF-8 sequence at s[i], or 0 if it's invalid
* (bad lead byte, truncated, overlong, a UTF-16 surrogate, or above U+10FFFF). Stores the code point in @param cp.
*/
inline std::size_t utf8Sequence(std::string_view s, const std::size_t i, std::uint32_t& cp) noexcept{
    const auto b0 = static_cast<unsigned char>(s[i]);
    std::size_t len = 0;
    std::uint32_t min = 0;
    if(b0 < 0x80){
        cp = b0;
        return 1;
    } else if((b0 & 0xE0) == 0xC0){
        len = 2; cp = b0 & 0x1F; min = 0x80;
    } else if((b0 & 0xF0) == 0xE0){
        len = 3; cp = b0 & 0x0F; min = 0x800;
    } else if((b0 & 0xF8) == 0xF0){
        len = 4; cp = b0 & 0x07; min = 0x10000;
    } else{
        return 0;
    }
    if(s.size() - i < len){
        return 0;
    }
    for(std::size_t k = 1; k < len; k++){
        const auto b = static_cast<unsigned char>(s[i + k]);
        if((b & 0xC0) != 0x80){
            return 0;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    if(cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)){
        return 0;
    }
    return len;
}

}  // namespace detail

/* ---------- parsing ---------- */

/* What parse() does when an object repeats a key. */
enum class DuplicateKeys : std::uint8_t{
    lastWins,  // keep the first position, take the last value (what JavaScript and Python do)
    firstWins, // ignore later copies
    reject,    // Error::Kind::DuplicateKey
};

struct ParseOptions{
    /* Arrays/objects nested deeper than this are an error, which keeps hostile input from overflowing the stack. */
    std::size_t maxDepth = 512;
    DuplicateKeys duplicateKeys = DuplicateKeys::lastWins;
};

/* Recursive descent parser for RFC 8259. Use json::parse(). */
class Parser{
private:
    std::string_view _in;
    ParseOptions _opts;
    std::size_t _pos{0};
    std::optional<Error> _error;
    std::vector<std::size_t> _keyOffsets;  // where each pending object key started, a stack shared by nested objects

    bool fail(const Error::Kind kind, std::string reason, const std::size_t at){
        std::size_t line = 1, column = 1;
        for(std::size_t i = 0; i < at && i < _in.size(); i++){
            if(_in[i] == '\n'){
                line++;
                column = 1;
            } else{
                column++;
            }
        }
        _error = Error::at(kind, std::move(reason), at, line, column);
        return false;
    }

    std::string describeHere() const{
        if(_pos >= _in.size()){
            return "end of input";
        }
        const auto c = static_cast<unsigned char>(_in[_pos]);
        if(c >= 0x20 && c < 0x7F){
            return std::string("'") + static_cast<char>(c) + "'";
        }
        static const char hex[] = "0123456789abcdef";
        return std::string("byte 0x") + hex[c >> 4] + hex[c & 0xF];
    }

    bool unexpected(std::string_view context = {}){
        std::string reason = "unexpected " + describeHere();
        if(!context.empty()){
            reason += ", ";
            reason += context;
        }
        return fail(Error::Kind::Syntax, std::move(reason), _pos);
    }

    void skipWhitespace() noexcept{
        while(_pos < _in.size()){
            const char c = _in[_pos];
            if(c != ' ' && c != '\t' && c != '\n' && c != '\r'){
                return;
            }
            _pos++;
        }
    }

    bool isDigitAt(const std::size_t i) const noexcept { return i < _in.size() && _in[i] >= '0' && _in[i] <= '9'; }

    bool parseValue(Value& out, const std::size_t depth){
        if(_pos >= _in.size()){
            return unexpected("expected a value");
        }
        switch(_in[_pos]){
            case '{': return parseObject(out, depth + 1);
            case '[': return parseArray(out, depth + 1);
            case '"':{
                std::string s;
                if(!parseString(s)){
                    return false;
                }
                out = Value(std::move(s));
                return true;
            }
            case 't': return parseLiteral("true", out, Value(true));
            case 'f': return parseLiteral("false", out, Value(false));
            case 'n': return parseLiteral("null", out, Value());
            case '-': case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return parseNumber(out);
            default:
                return unexpected("expected a value");
        }
    }

    bool parseLiteral(std::string_view word, Value& out, Value value){
        if(_in.substr(_pos, word.size()) != word){
            return fail(Error::Kind::Syntax, "invalid literal, expected '" + std::string(word) + "'", _pos);
        }
        _pos += word.size();
        out = std::move(value);
        return true;
    }

    bool checkDepth(const std::size_t depth){
        if(depth > _opts.maxDepth){
            return fail(Error::Kind::TooDeep, "nested deeper than maxDepth (" + std::to_string(_opts.maxDepth) + ")", _pos);
        }
        return true;
    }

    bool parseArray(Value& out, const std::size_t depth){
        if(!checkDepth(depth)){
            return false;
        }
        _pos++;  // [
        Array arr;
        skipWhitespace();
        if(_pos < _in.size() && _in[_pos] == ']'){
            _pos++;
            out = Value(std::move(arr));
            return true;
        }
        for(;;){
            skipWhitespace();
            arr.emplace_back();
            if(!parseValue(arr.back(), depth)){
                return false;
            }
            skipWhitespace();
            if(_pos < _in.size() && _in[_pos] == ','){
                _pos++;
                skipWhitespace();
                if(_pos < _in.size() && _in[_pos] == ']'){
                    return fail(Error::Kind::Syntax, "trailing comma in array", _pos);
                }
                continue;
            }
            if(_pos < _in.size() && _in[_pos] == ']'){
                _pos++;
                break;
            }
            return unexpected("expected ',' or ']' in array");
        }
        out = Value(std::move(arr));
        return true;
    }

    bool parseObject(Value& out, const std::size_t depth){
        if(!checkDepth(depth)){
            return false;
        }
        _pos++;  // {
        Object obj;
        const std::size_t keyBase = _keyOffsets.size();
        skipWhitespace();
        if(_pos < _in.size() && _in[_pos] == '}'){
            _pos++;
            out = Value(std::move(obj));
            return true;
        }
        for(;;){
            skipWhitespace();
            if(_pos >= _in.size() || _in[_pos] != '"'){
                return unexpected("expected a string key in object");
            }
            const std::size_t keyAt = _pos;
            std::string key;
            if(!parseString(key)){
                return false;
            }
            skipWhitespace();
            if(_pos >= _in.size() || _in[_pos] != ':'){
                return unexpected("expected ':' after object key");
            }
            _pos++;
            skipWhitespace();
            obj._members.emplace_back(std::move(key), Value());
            _keyOffsets.push_back(keyAt);
            if(!parseValue(obj._members.back().second, depth)){
                return false;
            }
            skipWhitespace();
            if(_pos < _in.size() && _in[_pos] == ','){
                _pos++;
                skipWhitespace();
                if(_pos < _in.size() && _in[_pos] == '}'){
                    return fail(Error::Kind::Syntax, "trailing comma in object", _pos);
                }
                continue;
            }
            if(_pos < _in.size() && _in[_pos] == '}'){
                _pos++;
                break;
            }
            return unexpected("expected ',' or '}' in object");
        }
        if(!resolveDuplicates(obj._members, keyBase)){
            return false;
        }
        _keyOffsets.resize(keyBase);
        out = Value(std::move(obj));
        return true;
    }

    /* Applies ParseOptions::duplicateKeys to a just parsed object. Small objects compare pairwise, big ones sort. */
    bool resolveDuplicates(std::vector<Member>& members, const std::size_t keyBase){
        const std::size_t n = members.size();
        if(n < 2){
            return true;
        }
        std::vector<char> removed;
        auto handle = [&](const std::size_t first, const std::size_t later) -> bool{
            if(_opts.duplicateKeys == DuplicateKeys::reject){
                return fail(Error::Kind::DuplicateKey, "duplicate key \"" + members[later].first + "\"", _keyOffsets[keyBase + later]);
            }
            if(removed.empty()){
                removed.assign(n, 0);
            }
            if(_opts.duplicateKeys == DuplicateKeys::lastWins){
                members[first].second = std::move(members[later].second);
            }
            removed[later] = 1;
            return true;
        };
        if(n <= 16){
            for(std::size_t j = 1; j < n; j++){
                for(std::size_t i = 0; i < j; i++){
                    if((removed.empty() || !removed[i]) && members[i].first == members[j].first){
                        if(!handle(i, j)){
                            return false;
                        }
                        break;
                    }
                }
            }
        } else{
            std::vector<std::size_t> order(n);
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b){
                return members[a].first < members[b].first;
            });
            for(std::size_t a = 0; a < n;){
                std::size_t b = a + 1;
                while(b < n && members[order[b]].first == members[order[a]].first){
                    // stable_sort keeps equal keys in input order, so order[a] is the first copy
                    if(!handle(order[a], order[b])){
                        return false;
                    }
                    b++;
                }
                a = b;
            }
        }
        if(removed.empty()){
            return true;
        }
        std::size_t write = 0;
        for(std::size_t read = 0; read < n; read++){
            if(!removed[read]){
                if(write != read){
                    members[write] = std::move(members[read]);
                }
                write++;
            }
        }
        members.erase(members.begin() + static_cast<std::ptrdiff_t>(write), members.end());
        return true;
    }

    bool readHex4(std::uint32_t& out){
        if(_in.size() - _pos < 4){
            return fail(Error::Kind::Syntax, "\\u escape needs 4 hex digits", _pos);
        }
        out = 0;
        for(std::size_t k = 0; k < 4; k++){
            const char c = _in[_pos + k];
            std::uint32_t digit = 0;
            if(c >= '0' && c <= '9'){ digit = static_cast<std::uint32_t>(c - '0'); }
            else if(c >= 'a' && c <= 'f'){ digit = static_cast<std::uint32_t>(c - 'a' + 10); }
            else if(c >= 'A' && c <= 'F'){ digit = static_cast<std::uint32_t>(c - 'A' + 10); }
            else{ return fail(Error::Kind::Syntax, "\\u escape needs 4 hex digits", _pos + k); }
            out = (out << 4) | digit;
        }
        _pos += 4;
        return true;
    }

    bool parseString(std::string& out){
        const std::size_t open = _pos;
        _pos++;  // "
        for(;;){
            const std::size_t runStart = _pos;
            while(_pos < _in.size()){
                const auto c = static_cast<unsigned char>(_in[_pos]);
                if(c == '"' || c == '\\' || c < 0x20 || c >= 0x80){
                    break;
                }
                _pos++;
            }
            out.append(_in.data() + runStart, _pos - runStart);
            if(_pos >= _in.size()){
                return fail(Error::Kind::Syntax, "unterminated string", open);
            }
            const auto c = static_cast<unsigned char>(_in[_pos]);
            if(c == '"'){
                _pos++;
                return true;
            }
            if(c < 0x20){
                return fail(Error::Kind::Syntax, "control characters must be escaped in strings", _pos);
            }
            if(c >= 0x80){
                std::uint32_t cp = 0;
                const std::size_t len = detail::utf8Sequence(_in, _pos, cp);
                if(len == 0){
                    return fail(Error::Kind::InvalidUnicode, "invalid UTF-8 in string", _pos);
                }
                out.append(_in.data() + _pos, len);
                _pos += len;
                continue;
            }
            // backslash
            const std::size_t escapeAt = _pos;
            _pos++;
            if(_pos >= _in.size()){
                return fail(Error::Kind::Syntax, "unterminated string", open);
            }
            const char e = _in[_pos++];
            switch(e){
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':{
                    std::uint32_t cp = 0;
                    if(!readHex4(cp)){
                        return false;
                    }
                    if(cp >= 0xD800 && cp <= 0xDBFF){
                        if(_in.size() - _pos < 2 || _in[_pos] != '\\' || _in[_pos + 1] != 'u'){
                            return fail(Error::Kind::InvalidUnicode, "high surrogate \\u escape without a low surrogate after it", escapeAt);
                        }
                        _pos += 2;
                        std::uint32_t low = 0;
                        if(!readHex4(low)){
                            return false;
                        }
                        if(low < 0xDC00 || low > 0xDFFF){
                            return fail(Error::Kind::InvalidUnicode, "high surrogate \\u escape without a low surrogate after it", escapeAt);
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if(cp >= 0xDC00 && cp <= 0xDFFF){
                        return fail(Error::Kind::InvalidUnicode, "low surrogate \\u escape without a high surrogate before it", escapeAt);
                    }
                    detail::appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail(Error::Kind::Syntax, "invalid escape in string", escapeAt);
            }
        }
    }

    bool parseNumber(Value& out){
        const std::size_t start = _pos;
        if(_in[_pos] == '-'){
            _pos++;
        }
        if(!isDigitAt(_pos)){
            return unexpected("expected a digit in number");
        }
        if(_in[_pos] == '0'){
            _pos++;
            if(isDigitAt(_pos)){
                return fail(Error::Kind::Syntax, "leading zeros are not allowed in numbers", start);
            }
        } else{
            while(isDigitAt(_pos)){ _pos++; }
        }
        bool whole = true;
        if(_pos < _in.size() && _in[_pos] == '.'){
            whole = false;
            _pos++;
            if(!isDigitAt(_pos)){
                return unexpected("expected a digit after '.'");
            }
            while(isDigitAt(_pos)){ _pos++; }
        }
        if(_pos < _in.size() && (_in[_pos] == 'e' || _in[_pos] == 'E')){
            whole = false;
            _pos++;
            if(_pos < _in.size() && (_in[_pos] == '+' || _in[_pos] == '-')){
                _pos++;
            }
            if(!isDigitAt(_pos)){
                return unexpected("expected a digit in exponent");
            }
            while(isDigitAt(_pos)){ _pos++; }
        }
        const std::string_view text = _in.substr(start, _pos - start);
        const bool negative = text[0] == '-';
        if(whole){
            if(text == "-0"){
                out = Value(-0.0);  // an Int can't keep the sign
                return true;
            }
            std::int64_t i = 0;
            const auto r = std::from_chars(text.data(), text.data() + text.size(), i);
            if(r.ec == std::errc{}){
                out = Value(i);
                return true;
            }
            // too big for int64, store as a double
        }
        const std::optional<double> d = detail::parseDouble(text);
        if(!d || !std::isfinite(*d)){
            if(detail::magnitudeAboveOne(text)){
                return fail(Error::Kind::NumberRange, "number is too large for a double", start);
            }
            out = Value(negative ? -0.0 : 0.0);  // underflow rounds to zero
            return true;
        }
        out = Value(*d);
        return true;
    }

public:
    Parser(std::string_view in, const ParseOptions& opts) : _in(in), _opts(opts) {}

    Result<Value> run(){
        if(_in.substr(0, 3) == "\xEF\xBB\xBF"){
            _pos = 3;  // a UTF-8 byte order mark is allowed and skipped
        }
        Value root;
        skipWhitespace();
        if(!parseValue(root, 0)){
            return Err(std::move(*_error));
        }
        skipWhitespace();
        if(_pos != _in.size()){
            unexpected("after the end of the JSON value");
            return Err(std::move(*_error));
        }
        return Ok(std::move(root));
    }
};

/**
* Parses one JSON value from @param text. Strict RFC 8259: no comments, no trailing commas, no single quotes,
* no NaN/Infinity, strings must be valid UTF-8. Whitespace and a UTF-8 BOM around the value are allowed.
* A number too large for a double is an error, one too small rounds to 0.
*/
inline Result<Value> parse(std::string_view text, const ParseOptions& options = {}){
    return Parser(text, options).run();
}

/* ---------- printing ---------- */

struct DumpOptions{
    /* Negative is compact. 0 or more puts each element on its own line, indented by this many indentChars per level. */
    int indent = -1;
    char indentChar = ' ';
    /* Write every non-ASCII character as a \u escape. Invalid UTF-8 in a string is then written as \ufffd. */
    bool ensureAscii = false;
};

namespace detail{

class Writer{
private:
    std::string& _out;
    const DumpOptions& _opts;

    void newline(const std::size_t depth){
        if(_opts.indent < 0){
            return;
        }
        _out.push_back('\n');
        _out.append(depth * static_cast<std::size_t>(_opts.indent), _opts.indentChar);
    }

    void escapeU(const std::uint32_t unit){
        static const char hex[] = "0123456789abcdef";
        _out += "\\u";
        _out.push_back(hex[(unit >> 12) & 0xF]);
        _out.push_back(hex[(unit >> 8) & 0xF]);
        _out.push_back(hex[(unit >> 4) & 0xF]);
        _out.push_back(hex[unit & 0xF]);
    }

public:
    Writer(std::string& out, const DumpOptions& opts) : _out(out), _opts(opts) {}

    void string(std::string_view s){
        _out.push_back('"');
        std::size_t run = 0;
        for(std::size_t i = 0; i < s.size();){
            const auto c = static_cast<unsigned char>(s[i]);
            const bool plain = c >= 0x20 && c != '"' && c != '\\' && (c < 0x80 || !_opts.ensureAscii);
            if(plain){
                i++;
                continue;
            }
            _out.append(s.data() + run, i - run);
            if(c >= 0x80){
                std::uint32_t cp = 0;
                const std::size_t len = utf8Sequence(s, i, cp);
                if(len == 0){
                    escapeU(0xFFFD);
                    i++;
                } else{
                    if(cp >= 0x10000){
                        escapeU(0xD800 + ((cp - 0x10000) >> 10));
                        escapeU(0xDC00 + ((cp - 0x10000) & 0x3FF));
                    } else{
                        escapeU(cp);
                    }
                    i += len;
                }
            } else{
                switch(c){
                    case '"':  _out += "\\\""; break;
                    case '\\': _out += "\\\\"; break;
                    case '\b': _out += "\\b"; break;
                    case '\f': _out += "\\f"; break;
                    case '\n': _out += "\\n"; break;
                    case '\r': _out += "\\r"; break;
                    case '\t': _out += "\\t"; break;
                    default:   escapeU(c); break;
                }
                i++;
            }
            run = i;
        }
        _out.append(s.data() + run, s.size() - run);
        _out.push_back('"');
    }

    void value(const Value& v, const std::size_t depth){
        switch(v.type()){
            case Type::Null:   _out += "null"; break;
            case Type::Bool:   _out += *v.getIf<bool>() ? "true" : "false"; break;
            case Type::Int:    appendInt(_out, *v.getIf<std::int64_t>()); break;
            case Type::Double:{
                const double d = *v.getIf<double>();
                if(std::isfinite(d)){
                    appendDouble(_out, d);
                } else{
                    _out += "null";  // JSON has no NaN or Infinity
                }
                break;
            }
            case Type::String: string(*v.getIf<std::string>()); break;
            case Type::Array:{
                const Array& a = *v.getIf<Array>();
                if(a.empty()){
                    _out += "[]";
                    break;
                }
                _out.push_back('[');
                for(std::size_t i = 0; i < a.size(); i++){
                    if(i){
                        _out.push_back(',');
                    }
                    newline(depth + 1);
                    value(a[i], depth + 1);
                }
                newline(depth);
                _out.push_back(']');
                break;
            }
            case Type::Object:{
                const Object& o = *v.getIf<Object>();
                if(o.empty()){
                    _out += "{}";
                    break;
                }
                _out.push_back('{');
                bool first = true;
                for(const Member& m : o){
                    if(!first){
                        _out.push_back(',');
                    }
                    first = false;
                    newline(depth + 1);
                    string(m.first);
                    _out += _opts.indent < 0 ? ":" : ": ";
                    value(m.second, depth + 1);
                }
                newline(depth);
                _out.push_back('}');
                break;
            }
        }
    }
};

}  // namespace detail

/* Appends the JSON text of @param v to @param out. NaN and Infinity are written as null. */
inline void dump(const Value& v, std::string& out, const DumpOptions& options = {}){
    detail::Writer(out, options).value(v, 0);
}

inline std::string dump(const Value& v, const DumpOptions& options = {}){
    std::string out;
    dump(v, out, options);
    return out;
}

inline std::string Value::dump(const int indent) const{
    DumpOptions options;
    options.indent = indent;
    return ::bstd::net::json::dump(*this, options);
}

/* Writes compact JSON. */
inline std::ostream& operator<<(std::ostream& os, const Value& v){
    return os << v.dump();
}

/* ---------- conversions to and from C++ types ---------- */

/**
* How a C++ type T turns into a Value and back:
*   static Value encode(const T&);
*   static Result<void> decode(const Value&, T& out);
* Built in for bool, integers, floating types, std::string, Value/Array/Object, std::optional, std::vector,
* std::array, and std::map / std::unordered_map with std::string keys.
* For your own types either write these two functions next to the type (found by argument dependent lookup):
*   json::Value toJson(const T&);
*   json::Result<void> fromJson(const json::Value&, T&);
* or use BSTD_JSON_FIELDS, or specialize json::Codec<T>.
*/
template <typename T, typename Enable = void>
struct Codec;

template <typename T>
Value toValue(const T& v);

/* Decodes @param j into a new T. T must be default constructible. */
template <typename T>
Result<T> fromValue(const Value& j);

/* json::parse() then json::fromValue<T>(). */
template <typename T>
Result<T> parseAs(std::string_view text, const ParseOptions& options = {});

namespace detail{

template <typename T, typename = void>
struct HasToJson : std::false_type{};
template <typename T>
struct HasToJson<T, std::void_t<decltype(toJson(std::declval<const T&>()))>> : std::true_type{};

template <typename T, typename = void>
struct HasFromJson : std::false_type{};
template <typename T>
struct HasFromJson<T, std::void_t<decltype(fromJson(std::declval<const Value&>(), std::declval<T&>()))>> : std::true_type{};

template <typename T>
struct IsOptional : std::false_type{};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type{};

inline Error wrongType(const char* wanted, const Value& got){
    return Error(Error::Kind::WrongType, std::string("expected ") + wanted + ", got " + typeName(got.type()));
}

/* Decodes one element and puts @param segment in front of the error path if it fails. */
template <typename T>
Result<void> decodeAt(const Value& j, T& out, std::string_view segment){
    auto r = Codec<T>::decode(j, out);
    if(r.isErr()){
        Error e = std::move(r).unwrapErr();
        e.prefixPath(segment);
        return Err(std::move(e));
    }
    return Ok();
}

/* Used by BSTD_JSON_FIELDS. An empty std::optional field is left out of the object. */
template <typename T>
void writeField(Object& o, std::string_view name, const T& v){
    if constexpr(IsOptional<T>::value){
        if(!v.has_value()){
            return;
        }
    }
    o.set(std::string(name), toValue(v));
}

/* Used by BSTD_JSON_FIELDS. A missing std::optional field becomes nullopt, any other missing field is an error. */
template <typename T>
Result<void> readField(const Object& o, std::string_view name, T& out){
    const Value* f = o.find(name);
    if(!f){
        if constexpr(IsOptional<T>::value){
            out.reset();
            return Ok();
        } else{
            return Err(Error(Error::Kind::MissingField, "missing field \"" + std::string(name) + "\""));
        }
    }
    return decodeAt(*f, out, pointerSegment(name));
}

}  // namespace detail

/* Anything without a built in Codec uses the user's toJson / fromJson. */
template <typename T, typename Enable>
struct Codec{
    static Value encode(const T& v){
        static_assert(detail::HasToJson<T>::value,
            "bstd::net::json: no conversion to json for this type. Declare `json::Value toJson(const T&)` in T's namespace, "
            "use BSTD_JSON_FIELDS(T, ...), or specialize bstd::net::json::Codec<T>.");
        return toJson(v);
    }
    static Result<void> decode(const Value& j, T& out){
        static_assert(detail::HasFromJson<T>::value,
            "bstd::net::json: no conversion from json for this type. Declare `json::Result<void> fromJson(const json::Value&, T&)` "
            "in T's namespace, use BSTD_JSON_FIELDS(T, ...), or specialize bstd::net::json::Codec<T>.");
        return fromJson(j, out);
    }
};

template <>
struct Codec<Value>{
    static Value encode(const Value& v) { return v; }
    static Result<void> decode(const Value& j, Value& out){
        out = j;
        return Ok();
    }
};

template <>
struct Codec<Array>{
    static Value encode(const Array& v) { return v; }
    static Result<void> decode(const Value& j, Array& out){
        if(const auto* a = j.getIf<Array>()){
            out = *a;
            return Ok();
        }
        return Err(detail::wrongType("array", j));
    }
};

template <>
struct Codec<Object>{
    static Value encode(const Object& v) { return v; }
    static Result<void> decode(const Value& j, Object& out){
        if(const auto* o = j.getIf<Object>()){
            out = *o;
            return Ok();
        }
        return Err(detail::wrongType("object", j));
    }
};

template <>
struct Codec<bool>{
    static Value encode(const bool v) { return Value(v); }
    static Result<void> decode(const Value& j, bool& out){
        if(const auto* b = j.getIf<bool>()){
            out = *b;
            return Ok();
        }
        return Err(detail::wrongType("bool", j));
    }
};

template <>
struct Codec<std::string>{
    static Value encode(const std::string& v) { return Value(v); }
    static Result<void> decode(const Value& j, std::string& out){
        if(const auto* s = j.getIf<std::string>()){
            out = *s;
            return Ok();
        }
        return Err(detail::wrongType("string", j));
    }
};

/* Integers are range checked. A Double converts only if it's a whole number that fits. */
template <typename T>
struct Codec<T, std::enable_if_t<detail::isJsonInteger<T>>>{
    static Value encode(const T v) { return Value(v); }
    static Result<void> decode(const Value& j, T& out){
        using limits = std::numeric_limits<T>;
        const auto tooBig = [](const std::string& shown){
            return Error(Error::Kind::OutOfRange, shown + " does not fit in a " + std::to_string(sizeof(T) * 8)
                + "-bit " + (limits::is_signed ? "signed" : "unsigned") + " integer");
        };
        if(const auto* i = j.getIf<std::int64_t>()){
            bool fits = false;
            if constexpr(limits::is_signed){
                fits = *i >= static_cast<std::int64_t>(limits::min()) && *i <= static_cast<std::int64_t>(limits::max());
            } else{
                fits = *i >= 0 && static_cast<std::uint64_t>(*i) <= static_cast<std::uint64_t>(limits::max());
            }
            if(!fits){
                return Err(tooBig(std::to_string(*i)));
            }
            out = static_cast<T>(*i);
            return Ok();
        }
        if(const auto* d = j.getIf<double>()){
            if(!std::isfinite(*d) || std::trunc(*d) != *d){
                return Err(detail::wrongType("integer", j));
            }
            const double upper = std::ldexp(1.0, limits::digits);  // max + 1, exact as a double
            const double lower = limits::is_signed ? -upper : 0.0;
            if(*d < lower || *d >= upper){
                std::string shown;
                detail::appendDouble(shown, *d);
                return Err(tooBig(shown));
            }
            out = static_cast<T>(*d);
            return Ok();
        }
        return Err(detail::wrongType("integer", j));
    }
};

template <typename T>
struct Codec<T, std::enable_if_t<std::is_floating_point_v<T>>>{
    static Value encode(const T v) { return Value(v); }
    static Result<void> decode(const Value& j, T& out){
        if(const auto* i = j.getIf<std::int64_t>()){
            out = static_cast<T>(*i);
            return Ok();
        }
        if(const auto* d = j.getIf<double>()){
            if(std::isfinite(*d) && std::fabs(*d) > static_cast<double>(std::numeric_limits<T>::max())){
                return Err(Error(Error::Kind::OutOfRange, "number does not fit in a float"));
            }
            out = static_cast<T>(*d);
            return Ok();
        }
        return Err(detail::wrongType("number", j));
    }
};

/* nullopt is null, and null decodes to nullopt. */
template <typename T>
struct Codec<std::optional<T>>{
    static Value encode(const std::optional<T>& v) { return v ? toValue(*v) : Value(); }
    static Result<void> decode(const Value& j, std::optional<T>& out){
        if(j.isNull()){
            out.reset();
            return Ok();
        }
        T inner{};
        auto r = Codec<T>::decode(j, inner);
        if(r.isErr()){
            return r;
        }
        out = std::move(inner);
        return Ok();
    }
};

template <typename T, typename A>
struct Codec<std::vector<T, A>>{
    static Value encode(const std::vector<T, A>& v){
        Array a;
        a.reserve(v.size());
        for(const auto& e : v){
            a.push_back(toValue(static_cast<const T&>(e)));
        }
        return a;
    }
    static Result<void> decode(const Value& j, std::vector<T, A>& out){
        const auto* a = j.getIf<Array>();
        if(!a){
            return Err(detail::wrongType("array", j));
        }
        out.clear();
        out.reserve(a->size());
        for(std::size_t i = 0; i < a->size(); i++){
            T e{};
            auto r = detail::decodeAt((*a)[i], e, detail::pointerSegment(i));
            if(r.isErr()){
                return r;
            }
            out.push_back(std::move(e));
        }
        return Ok();
    }
};

/* The array must have exactly N elements. */
template <typename T, std::size_t N>
struct Codec<std::array<T, N>>{
    static Value encode(const std::array<T, N>& v){
        Array a;
        a.reserve(N);
        for(const auto& e : v){
            a.push_back(toValue(e));
        }
        return a;
    }
    static Result<void> decode(const Value& j, std::array<T, N>& out){
        const auto* a = j.getIf<Array>();
        if(!a){
            return Err(detail::wrongType("array", j));
        }
        if(a->size() != N){
            return Err(Error(Error::Kind::OutOfRange, "expected " + std::to_string(N) + " elements, got " + std::to_string(a->size())));
        }
        for(std::size_t i = 0; i < N; i++){
            auto r = detail::decodeAt((*a)[i], out[i], detail::pointerSegment(i));
            if(r.isErr()){
                return r;
            }
        }
        return Ok();
    }
};

namespace detail{

template <typename Map>
struct MapCodec{
    using Mapped = typename Map::mapped_type;
    static Value encode(const Map& m){
        Object o;
        o.reserve(m.size());
        for(const auto& kv : m){
            o.set(kv.first, toValue(kv.second));
        }
        return o;
    }
    static Result<void> decode(const Value& j, Map& out){
        const auto* o = j.getIf<Object>();
        if(!o){
            return Err(wrongType("object", j));
        }
        out.clear();
        for(const Member& m : *o){
            Mapped e{};
            auto r = decodeAt(m.second, e, pointerSegment(m.first));
            if(r.isErr()){
                return r;
            }
            out.emplace(m.first, std::move(e));
        }
        return Ok();
    }
};

}  // namespace detail

template <typename T, typename C, typename A>
struct Codec<std::map<std::string, T, C, A>> : detail::MapCodec<std::map<std::string, T, C, A>>{};

template <typename T, typename H, typename E, typename A>
struct Codec<std::unordered_map<std::string, T, H, E, A>> : detail::MapCodec<std::unordered_map<std::string, T, H, E, A>>{};

/* Strings and anything a Value is directly built from skip the Codec, so toValue("text") works. */
template <typename T>
Value toValue(const T& v){
    if constexpr(std::is_constructible_v<Value, const T&>){
        return Value(v);
    } else{
        return Codec<T>::encode(v);
    }
}

template <typename T>
Result<T> fromValue(const Value& j){
    static_assert(std::is_default_constructible_v<T>, "bstd::net::json::fromValue<T> needs a default constructible T");
    T out{};
    auto r = Codec<T>::decode(j, out);
    if(r.isErr()){
        return Err(std::move(r).unwrapErr());
    }
    return Ok(std::move(out));
}

template <typename T>
Result<T> parseAs(std::string_view text, const ParseOptions& options){
    auto parsed = parse(text, options);
    if(parsed.isErr()){
        return Err(std::move(parsed).unwrapErr());
    }
    return fromValue<T>(parsed.unwrap());
}

template <typename T>
std::optional<T> Value::get() const{
    auto r = fromValue<T>(*this);
    if(r.isErr()){
        return std::nullopt;
    }
    return std::move(r).unwrap();
}

template <typename T>
T Value::getOr(T fallback) const{
    auto r = fromValue<T>(*this);
    if(r.isErr()){
        return fallback;
    }
    return std::move(r).unwrap();
}

}  // namespace json
}  // namespace net
}  // namespace bstd

/* ---------- BSTD_JSON_FIELDS ---------- */

/**
* Writes toJson / fromJson for a struct from a list of its members (1 to 32), using the member names as keys.
*
*   namespace app{
*   struct User{ std::string name; int age{}; std::optional<std::string> email; std::vector<std::string> tags; };
*   BSTD_JSON_FIELDS(User, name, age, email, tags);
*   }
*
* Use it at namespace scope, in the same namespace as the type. For private members use BSTD_JSON_FIELDS_IN
* inside the class instead (it writes friend functions).
* Decoding: a missing std::optional member becomes nullopt, any other missing member is Error::Kind::MissingField.
* Unknown keys are ignored. Encoding: an empty std::optional member is left out.
*/
#define BSTD_JSON_FIELDS(Type, ...) BSTD_JSON_DEFINE_(inline, Type, __VA_ARGS__)
#define BSTD_JSON_FIELDS_IN(Type, ...) BSTD_JSON_DEFINE_(friend, Type, __VA_ARGS__)

#define BSTD_JSON_DEFINE_(Spec, Type, ...)                                                                          \
    Spec ::bstd::net::json::Value toJson(const Type& bstdJsonIn){                                                   \
        ::bstd::net::json::Object bstdJsonOut;                                                                      \
        BSTD_JSON_FOR_EACH(BSTD_JSON_WRITE_FIELD_, __VA_ARGS__)                                                     \
        return bstdJsonOut;                                                                                         \
    }                                                                                                               \
    Spec ::bstd::net::json::Result<void> fromJson(const ::bstd::net::json::Value& bstdJsonIn, Type& bstdJsonOut){   \
        const auto* bstdJsonObj = bstdJsonIn.getIf<::bstd::net::json::Object>();                                   \
        if(!bstdJsonObj){                                                                                           \
            return ::bstd::error::Err(::bstd::net::json::detail::wrongType("object", bstdJsonIn));                  \
        }                                                                                                           \
        BSTD_JSON_FOR_EACH(BSTD_JSON_READ_FIELD_, __VA_ARGS__)                                                      \
        return ::bstd::error::Ok();                                                                                 \
    }                                                                                                               \
    static_assert(true, "")

#define BSTD_JSON_WRITE_FIELD_(field) ::bstd::net::json::detail::writeField(bstdJsonOut, #field, bstdJsonIn.field);
#define BSTD_JSON_READ_FIELD_(field)                                                                                \
    if(auto bstdJsonR = ::bstd::net::json::detail::readField(*bstdJsonObj, #field, bstdJsonOut.field); bstdJsonR.isErr()){ \
        return bstdJsonR;                                                                                           \
    }

/* BSTD_JSON_FOR_EACH(m, a, b, c) is m(a) m(b) m(c). The EXPAND wrappers make MSVC's old preprocessor split __VA_ARGS__. */
#define BSTD_JSON_EXPAND(x) x
#define BSTD_JSON_CAT_(a, b) a##b
#define BSTD_JSON_CAT(a, b) BSTD_JSON_CAT_(a, b)
#define BSTD_JSON_FOR_EACH(m, ...) BSTD_JSON_EXPAND(BSTD_JSON_CAT(BSTD_JSON_FE_, BSTD_JSON_NARGS(__VA_ARGS__))(m, __VA_ARGS__))
#define BSTD_JSON_NARGS_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, N, ...) N
#define BSTD_JSON_NARGS(...) BSTD_JSON_EXPAND(BSTD_JSON_NARGS_(__VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))
#define BSTD_JSON_FE_1(m, a) m(a)
#define BSTD_JSON_FE_2(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_1(m, __VA_ARGS__))
#define BSTD_JSON_FE_3(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_2(m, __VA_ARGS__))
#define BSTD_JSON_FE_4(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_3(m, __VA_ARGS__))
#define BSTD_JSON_FE_5(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_4(m, __VA_ARGS__))
#define BSTD_JSON_FE_6(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_5(m, __VA_ARGS__))
#define BSTD_JSON_FE_7(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_6(m, __VA_ARGS__))
#define BSTD_JSON_FE_8(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_7(m, __VA_ARGS__))
#define BSTD_JSON_FE_9(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_8(m, __VA_ARGS__))
#define BSTD_JSON_FE_10(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_9(m, __VA_ARGS__))
#define BSTD_JSON_FE_11(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_10(m, __VA_ARGS__))
#define BSTD_JSON_FE_12(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_11(m, __VA_ARGS__))
#define BSTD_JSON_FE_13(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_12(m, __VA_ARGS__))
#define BSTD_JSON_FE_14(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_13(m, __VA_ARGS__))
#define BSTD_JSON_FE_15(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_14(m, __VA_ARGS__))
#define BSTD_JSON_FE_16(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_15(m, __VA_ARGS__))
#define BSTD_JSON_FE_17(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_16(m, __VA_ARGS__))
#define BSTD_JSON_FE_18(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_17(m, __VA_ARGS__))
#define BSTD_JSON_FE_19(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_18(m, __VA_ARGS__))
#define BSTD_JSON_FE_20(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_19(m, __VA_ARGS__))
#define BSTD_JSON_FE_21(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_20(m, __VA_ARGS__))
#define BSTD_JSON_FE_22(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_21(m, __VA_ARGS__))
#define BSTD_JSON_FE_23(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_22(m, __VA_ARGS__))
#define BSTD_JSON_FE_24(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_23(m, __VA_ARGS__))
#define BSTD_JSON_FE_25(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_24(m, __VA_ARGS__))
#define BSTD_JSON_FE_26(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_25(m, __VA_ARGS__))
#define BSTD_JSON_FE_27(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_26(m, __VA_ARGS__))
#define BSTD_JSON_FE_28(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_27(m, __VA_ARGS__))
#define BSTD_JSON_FE_29(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_28(m, __VA_ARGS__))
#define BSTD_JSON_FE_30(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_29(m, __VA_ARGS__))
#define BSTD_JSON_FE_31(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_30(m, __VA_ARGS__))
#define BSTD_JSON_FE_32(m, a, ...) m(a) BSTD_JSON_EXPAND(BSTD_JSON_FE_31(m, __VA_ARGS__))
