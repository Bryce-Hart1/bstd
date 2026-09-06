#include <exception>
#include <string>

namespace bstd{
namespace err{

class outOfBoundsException : public std::exception{
    std::string errorMsg;
    public:
    outOfBoundsException(std::string message) : errorMsg(std::move(message)){}
    const char* what() const noexcept override {return errorMsg.c_str(); }
};




}
}