#include <bitset>
#include <cmath>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace bstd{
namespace store{
namespace details{

inline const u_int64_t valueForByteAt(const unsigned char& positionFromFirst, const unsigned char& byteValue){
    if(positionFromFirst == 0){
        return static_cast<u_int64_t>(byteValue);
    }
    const int MAX_IN_BYTE = 256; //max in byte after the first
    return (std::pow(MAX_IN_BYTE, positionFromFirst));
}

inline const std::vector<unsigned char> convertToUCvec(const std::vector<std::bitset<8>>& bytes){
    std::vector<unsigned char> r;
    for(const std::bitset<8> byte : bytes){
        r.push_back(static_cast<unsigned char>(byte.to_ulong()));
    }
    const std::vector<unsigned char> c = std::move(r);
    return c;
}
}


using signedTypes = std::variant<int8_t, int16_t, int32_t, int64_t>;
inline const std::optional<signedTypes> convertToSignedIntegral(const std::vector<unsigned char>& bytesLE){
    using namespace details;
    if(bytesLE.size() == 1){
        return static_cast<int8_t>(bytesLE.at(1));
    }
    if(bytesLE.size() == 2){
       return static_cast<int16_t>(
        (valueForByteAt(bytesLE.at(1), 1) + static_cast<int8_t>(bytesLE.at(0))));
    }
    if(bytesLE.size() <= 4){
        int16_t fourByte = 0;
        for(int i = 0; i < 4; i++){
            fourByte += valueForByteAt(i, bytesLE.at(i));
        }
        return fourByte;
    }
    if(bytesLE.size() <= 8){
        int16_t eightByte = 0;
        for(int i = 0; i < 8; i++){
            eightByte += valueForByteAt(i, bytesLE.at(i));
        }
        return eightByte;
    }
    return std::nullopt;
}






}






}