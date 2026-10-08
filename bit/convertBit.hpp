#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>


namespace bstd{
namespace bit{



/**
* allows for numeric value to be constructed from the underlying bytes of an object.
* @param `value` - the object that is being inspected - `must be trivially copyable!`
* @param `at` - the location of byte being inspected - `must fall between range of 0 and the max byte position of the object`
*
*
* @author - `Bryce Hart` @date `October 8th 2026`
*/
template<typename Type>
std::optional<unsigned char> value_at_byte(const Type& value, std::size_t at){
    static_assert(std::is_trivially_copy_constructible_v<Type>, "Object must be trivially copyable.");
    if(sizeof(Type) <= at){
        return std::nullopt;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(std::addressof(value));
    return bytes[at];
}






}

}