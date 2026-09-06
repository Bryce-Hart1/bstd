#include <cmath>
#include <concepts>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>


namespace bstd{
namespace form{

template<std::integral Integral>
class formattedNumber{
    public:
    /**
    * @author Bryce Hart @date Aug 9 26
    * Immutable by construction: all data members are `const`, so the object
    * cannot be mutated or reassigned after construction, regardless of
    * whether the declaring variable itself is marked `const`.
    * @param x - number to round/format 
    * @param leading - used by Set(): max leading digits allowed
    * @param trailing - used by Set(): max trailing digits allowed
    * @attention `leading` or `trailing` cannot exceed 15 digits of precision.
    * @throws std::runtime_error for digit precision over 15 places in each direction.
    */
    explicit formattedNumber(const Integral x) : number(x){}

    [[nodiscard]] std::pair<double, signed char> Auto() const{
        if(number == 0){
            return std::make_pair(0.0, static_cast<signed char>(0));
        }

        const bool isNegative = (number < 0);
        double absNumber = std::fabs(static_cast<double>(number));
        signed char shift = 0;

        while(std::fmod(absNumber, 10.0) == 0.0){
            absNumber /= 10.0;
            ++shift;
        }
        while(absNumber >= 10.0){
            absNumber /= 10.0;
            ++shift;
        }

        return std::make_pair(isNegative ? -absNumber : absNumber, shift);
    }

    [[nodiscard]] std::pair<double, signed char> Set(const unsigned char leading, const unsigned char trailing) const{
        try{
            if(leading > 15 || trailing > 15){
                throw std::runtime_error("formattedNumber: leading or trailing digits cannot exceed 15 digits of precision");
            }
        }catch(const std::exception& e){
            std::cerr << e.what() << std::endl;
            return std::make_pair(0.0, static_cast<signed char>(0));
        }

        if(number == 0){
            return std::make_pair(0.0, static_cast<signed char>(0));
        }
        const bool isNegative = (number < 0);
        const double absOfNumber = std::fabs(static_cast<double>(number));
        const int expo = static_cast<int>(std::floor(std::log10(absOfNumber)));
        int shift = expo - static_cast<int>(leading) + 1;
        const double scale = std::pow(10.0, shift);
        double mantissa = absOfNumber / scale;

        double roundingFactor = std::pow(10.0, trailing);
        mantissa = std::round(mantissa * roundingFactor) / roundingFactor;

        if(mantissa >= std::pow(10.0, leading)){
            mantissa /= 10.0;
            shift += 1;
        }

        return std::make_pair(isNegative ? -mantissa : mantissa,
         static_cast<signed char>(shift));
    }

    private:
    const Integral number;

};



/** How many @param item in @param x*/
inline const std::size_t howManyOf(std::string_view item, std::string_view x){
    std::size_t count = 0;
    if(x.size() < item.size()){
        return 0;
    }
    std::size_t findItr = 0;
    for(std::size_t itr = 0; itr < x.size(); itr++){
        if(x.at(itr) == item.at(findItr)){
            findItr++;
            if(findItr == item.size()){
                findItr = 0;
                count++;
            }
        }else{
            findItr = 0;
        }
       
    }
    return count;
}

/** How many @param this in @param x*/
inline const std::size_t howManyOf(std::size_t This, std::size_t x){
    return howManyOf(static_cast<std::string_view>(std::to_string(This)),
    static_cast<std::string_view>(std::to_string(x)));
}

}
}