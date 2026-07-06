#include <vector>
#include <cstdint>
#include <stdexcept>



/**
 * VarInt V1 Bryce Hart
 * Class is a vector of bytes that can be transformed into a number.
 * This is only worth it if most of the numbers needed should'nt have decode overhead 
 * 
 */

namespace bstd{
namespace bit{
class VarInt {
public:
    VarInt(uint64_t value = 0) { 
        encode(value); }

    // Decode back to a plain integer
    uint64_t value() const {
        uint64_t result = 0;
        for (int i = _bytes.size() - 1; i >= 0; --i) {
            result = (result << 8) | _bytes[i];
        }
        return result;
    }

    // How many bytes we're actually using
    size_t byte_size() const { 
        return _bytes.size(); 
    }

    VarInt& operator=(uint64_t val) { 
        encode(val); return *this; 
    }

    // result is a new VarInt sized to fit
    VarInt operator+(const VarInt& other) const { 
        return VarInt(value() + other.value()); 
    }

    VarInt operator-(const VarInt& other) const { 
        return VarInt(value() - other.value()); 
    }

    bool operator==(const VarInt& other) const {
        return value() == other.value(); 
    }

    bool operator<(const VarInt& other)  const { 
        return value() < other.value();  
    }

    // implicit conversion so it feels like a number
    operator uint64_t() const { 
        return value(); 
    }

private:
    std::vector<uint8_t> _bytes;

    void encode(uint64_t val) {
        _bytes.clear();
        if (val == 0) { _bytes.push_back(0); return; }
        while (val > 0) {
            _bytes.push_back(val & 0xFF); // store little-endian, most readable
            val >>= 8;
        }
    }
};


}
}