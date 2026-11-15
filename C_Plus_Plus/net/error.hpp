#pragma once
/**
* bstd::net::Error @author Bryce Hart @date Nov 2026
* Requires C++17 or newer. Header only, no OS specific code.
*
* @details The error type every part of bstd::net returns (json has its own, json::Error, since it never touches the network).
* Works with bstd::error::Result, so `net::Result<T>` is `Result<T, net::Error>`.
*
*   auto url = net::Url::parse("http://[::1:8080/");
*   if(!url){ std::cout << url.unwrapErr().message(); }   // "invalid URL: missing ']' after IPv6 address"
*/
#include <cstdint>
#include <string>
#include <utility>
#include "../error/errorType.hpp"

namespace bstd{
namespace net{

class Error{
public:
    enum class Kind : std::uint8_t{
        InvalidUrl,     // Url::parse / Url::resolve
        InvalidMessage, // malformed HTTP message text
        Resolve,        // DNS lookup failed
        Connect,        // couldn't connect
        Timeout,        // an operation ran past its deadline
        Io,             // a read or write failed
        Closed,         // the other side closed the connection
        Protocol,       // the other side broke the HTTP rules
        TooLarge,       // a configured size limit was hit
    };

private:
    Kind _kind{Kind::Io};
    std::string _reason;
    int _systemCode{0};
    std::string _message;

public:
    Error() = default;
    /* @param systemCode is errno / WSAGetLastError() when an OS call failed, 0 otherwise. */
    Error(const Kind kind, std::string reason, const int systemCode = 0)
        : _kind(kind), _reason(std::move(reason)), _systemCode(systemCode) {
        _message = std::string(kindName(kind)) + ": " + _reason;
        if(_systemCode != 0){
            _message += " (os error " + std::to_string(_systemCode) + ")";
        }
    }

    static const char* kindName(const Kind kind) noexcept{
        switch(kind){
            case Kind::InvalidUrl:     return "invalid URL";
            case Kind::InvalidMessage: return "invalid HTTP message";
            case Kind::Resolve:        return "name resolution failed";
            case Kind::Connect:        return "connect failed";
            case Kind::Timeout:        return "timed out";
            case Kind::Io:             return "I/O error";
            case Kind::Closed:         return "connection closed";
            case Kind::Protocol:       return "protocol error";
            case Kind::TooLarge:       return "too large";
        }
        return "network error";
    }

    Kind kind() const noexcept { return _kind; }
    /* The text without the kind in front. */
    const std::string& reason() const noexcept { return _reason; }
    int systemCode() const noexcept { return _systemCode; }
    const std::string& message() const noexcept { return _message; }
};

template <typename T>
using Result = ::bstd::error::Result<T, Error>;

}  // namespace net
}  // namespace bstd
