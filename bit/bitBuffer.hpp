#include <vector>
#include <stdexcept>
#include <string>
#include <bitset>

namespace bstd {
/**
* Bitbuffer @version 2 @author Bryce Hart
* @details an alternative to `std::vector<bool>` where you are given complete access to individual bits, 
* compared to vector where your are not given complete access and are given a proxy object. 
* 
*/
namespace bit{
class BitBuffer {
    using u8 = u_int8_t;
    using size_t = std::size_t;

private:
    std::vector<u8> _bytes; 
    size_t _bitCount = 0;   // total bits written



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


    explicit BitBuffer(std::string_view constString){
        std::bitset<8> byte{}; 
        unsigned char itr = 0;
        std::vector<unsigned char> send{};
        for(const char c: constString){
            if(itr == 8){
                u8 current = static_cast<unsigned char>(byte.to_ulong());
                send.push_back(current);
                itr = 0;
            }
            if(c == '0'){
                byte.set(itr) = false;
            }else{
                byte.set(itr) = true;
            }
            itr++;
        }
        _bytes = (std::move(send));
        _bitCount = (_bytes.size() * 8);
    }

    explicit BitBuffer(std::string String){
        std::string_view view(String);
        BitBuffer _x(view);
        
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

    void push(u8 byte, int nBits = 8) {
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
    void operator+=(const bool b){
        _bytes.push_back(b);
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
        size_t           idx;

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
};

}
} // namespace bstd