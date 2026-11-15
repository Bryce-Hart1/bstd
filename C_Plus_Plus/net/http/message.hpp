#pragma once
/**
* bstd::net::http messages @author Bryce Hart @date Nov 2026
* Requires C++17 or newer. Header only, no OS specific code, no I/O.
*
* @details HTTP/1.1 requests and responses as plain data, and how to write them out as bytes (RFC 9110 / 9112).
*
*   auto url = net::Url::parse("http://api.example.com/users?page=2").unwrap();
*   http::Request req = http::Request::forUrl(http::Method::Post, url);
*   req.setJson(json::Object{{"name", "bob"}});
*   std::string wire = req.serialize();
*   // POST /users?page=2 HTTP/1.1\r\nHost: api.example.com\r\nContent-Type: application/json\r\nContent-Length: 14\r\n\r\n{"name":"bob"}
*
*   http::Response res = http::Response::makeJson(json::Object{{"id", 7}}, http::Status::Created);
*   res.headers.add("Cache-Control", "no-store");
*
* Header names and values are checked when added, and the request target and reason phrase when serialized, so
* text from outside (with \r\n in it) can't inject extra headers. Breaking those rules throws std::invalid_argument.
* Content-Length is filled in by serialize() unless you set Content-Length or Transfer-Encoding yourself.
*/
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "../json.hpp"
#include "../url.hpp"

namespace bstd{
namespace net{
namespace http{

/* ---------- method ---------- */

/* The standard methods. Mixed case names because <windows.h> defines DELETE as a macro. */
enum class Method : std::uint8_t{
    Get,
    Head,
    Post,
    Put,
    Delete,
    Patch,
    Options,
    Trace,
    Connect,
};

constexpr std::string_view methodName(const Method m) noexcept{
    switch(m){
        case Method::Get:     return "GET";
        case Method::Head:    return "HEAD";
        case Method::Post:    return "POST";
        case Method::Put:     return "PUT";
        case Method::Delete:  return "DELETE";
        case Method::Patch:   return "PATCH";
        case Method::Options: return "OPTIONS";
        case Method::Trace:   return "TRACE";
        case Method::Connect: return "CONNECT";
    }
    return "GET";
}

/* Method names are case sensitive ("get" is not GET). @returns nullopt for anything else. */
inline std::optional<Method> parseMethod(std::string_view name) noexcept{
    for(const Method m : {Method::Get, Method::Head, Method::Post, Method::Put, Method::Delete,
                          Method::Patch, Method::Options, Method::Trace, Method::Connect}){
        if(methodName(m) == name){
            return m;
        }
    }
    return std::nullopt;
}

/* ---------- version ---------- */

enum class Version : std::uint8_t{
    Http10,
    Http11,
};

constexpr std::string_view versionName(const Version v) noexcept { return v == Version::Http10 ? "HTTP/1.0" : "HTTP/1.1"; }

/* ---------- status ---------- */

/**
* Status codes. Any code from 100 to 999 is allowed, the named ones are just the common ones:
*   http::Status custom = static_cast<http::Status>(299);
*/
enum class Status : std::uint16_t{
    Continue                    = 100,
    SwitchingProtocols          = 101,
    Ok                          = 200,
    Created                     = 201,
    Accepted                    = 202,
    NonAuthoritativeInformation = 203,
    NoContent                   = 204,
    ResetContent                = 205,
    PartialContent              = 206,
    MultipleChoices             = 300,
    MovedPermanently            = 301,
    Found                       = 302,
    SeeOther                    = 303,
    NotModified                 = 304,
    TemporaryRedirect           = 307,
    PermanentRedirect           = 308,
    BadRequest                  = 400,
    Unauthorized                = 401,
    PaymentRequired             = 402,
    Forbidden                   = 403,
    NotFound                    = 404,
    MethodNotAllowed            = 405,
    NotAcceptable               = 406,
    ProxyAuthenticationRequired = 407,
    RequestTimeout              = 408,
    Conflict                    = 409,
    Gone                        = 410,
    LengthRequired              = 411,
    PreconditionFailed          = 412,
    ContentTooLarge             = 413,
    UriTooLong                  = 414,
    UnsupportedMediaType        = 415,
    RangeNotSatisfiable         = 416,
    ExpectationFailed           = 417,
    MisdirectedRequest          = 421,
    UnprocessableContent        = 422,
    UpgradeRequired             = 426,
    PreconditionRequired        = 428,
    TooManyRequests             = 429,
    RequestHeaderFieldsTooLarge = 431,
    InternalServerError         = 500,
    NotImplemented              = 501,
    BadGateway                  = 502,
    ServiceUnavailable          = 503,
    GatewayTimeout              = 504,
    HttpVersionNotSupported     = 505,
};

constexpr std::uint16_t code(const Status s) noexcept { return static_cast<std::uint16_t>(s); }

/* The RFC 9110 reason phrase, or "" for a code without one. */
constexpr std::string_view reasonPhrase(const Status s) noexcept{
    switch(s){
        case Status::Continue:                    return "Continue";
        case Status::SwitchingProtocols:          return "Switching Protocols";
        case Status::Ok:                          return "OK";
        case Status::Created:                     return "Created";
        case Status::Accepted:                    return "Accepted";
        case Status::NonAuthoritativeInformation: return "Non-Authoritative Information";
        case Status::NoContent:                   return "No Content";
        case Status::ResetContent:                return "Reset Content";
        case Status::PartialContent:              return "Partial Content";
        case Status::MultipleChoices:             return "Multiple Choices";
        case Status::MovedPermanently:            return "Moved Permanently";
        case Status::Found:                       return "Found";
        case Status::SeeOther:                    return "See Other";
        case Status::NotModified:                 return "Not Modified";
        case Status::TemporaryRedirect:           return "Temporary Redirect";
        case Status::PermanentRedirect:           return "Permanent Redirect";
        case Status::BadRequest:                  return "Bad Request";
        case Status::Unauthorized:                return "Unauthorized";
        case Status::PaymentRequired:             return "Payment Required";
        case Status::Forbidden:                   return "Forbidden";
        case Status::NotFound:                    return "Not Found";
        case Status::MethodNotAllowed:            return "Method Not Allowed";
        case Status::NotAcceptable:               return "Not Acceptable";
        case Status::ProxyAuthenticationRequired: return "Proxy Authentication Required";
        case Status::RequestTimeout:              return "Request Timeout";
        case Status::Conflict:                    return "Conflict";
        case Status::Gone:                        return "Gone";
        case Status::LengthRequired:              return "Length Required";
        case Status::PreconditionFailed:          return "Precondition Failed";
        case Status::ContentTooLarge:             return "Content Too Large";
        case Status::UriTooLong:                  return "URI Too Long";
        case Status::UnsupportedMediaType:        return "Unsupported Media Type";
        case Status::RangeNotSatisfiable:         return "Range Not Satisfiable";
        case Status::ExpectationFailed:           return "Expectation Failed";
        case Status::MisdirectedRequest:          return "Misdirected Request";
        case Status::UnprocessableContent:        return "Unprocessable Content";
        case Status::UpgradeRequired:             return "Upgrade Required";
        case Status::PreconditionRequired:        return "Precondition Required";
        case Status::TooManyRequests:             return "Too Many Requests";
        case Status::RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
        case Status::InternalServerError:         return "Internal Server Error";
        case Status::NotImplemented:              return "Not Implemented";
        case Status::BadGateway:                  return "Bad Gateway";
        case Status::ServiceUnavailable:          return "Service Unavailable";
        case Status::GatewayTimeout:              return "Gateway Timeout";
        case Status::HttpVersionNotSupported:     return "HTTP Version Not Supported";
    }
    return "";
}

constexpr bool isInformational(const Status s) noexcept { return code(s) >= 100 && code(s) < 200; }
constexpr bool isSuccess(const Status s) noexcept { return code(s) >= 200 && code(s) < 300; }
constexpr bool isRedirect(const Status s) noexcept { return code(s) >= 300 && code(s) < 400; }
constexpr bool isClientError(const Status s) noexcept { return code(s) >= 400 && code(s) < 500; }
constexpr bool isServerError(const Status s) noexcept { return code(s) >= 500 && code(s) < 600; }
/* 1xx, 204 and 304 responses never have a body. */
constexpr bool allowsBody(const Status s) noexcept { return !isInformational(s) && s != Status::NoContent && s != Status::NotModified; }

/* ---------- headers ---------- */

namespace detail{

/* RFC 9110 tchar */
constexpr bool isTokenChar(const char c) noexcept{
    if(net::detail::isAlpha(c) || net::detail::isDigit(c)){
        return true;
    }
    switch(c){
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

constexpr std::string_view trimOws(std::string_view s) noexcept{
    while(!s.empty() && (s.front() == ' ' || s.front() == '\t')){ s.remove_prefix(1); }
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t')){ s.remove_suffix(1); }
    return s;
}

/* Visible ASCII, space, tab and bytes 0x80+ (obs-text). Rules out CR, LF, NUL and other controls. */
constexpr bool isFieldText(std::string_view s) noexcept{
    for(const char c : s){
        const auto u = static_cast<unsigned char>(c);
        if((u < 0x20 && u != '\t') || u == 0x7F){
            return false;
        }
    }
    return true;
}

}  // namespace detail

/**
* Header fields in the order they were added. Names compare case insensitively and keep the case you gave them.
* A name can appear more than once (Set-Cookie). Values have spaces and tabs at either end trimmed.
* @throws `std::invalid_argument` from add/set on a name that isn't an HTTP token or a value with control characters.
*/
class Headers{
public:
    using Field = std::pair<std::string, std::string>;
    using const_iterator = std::vector<Field>::const_iterator;

private:
    std::vector<Field> _fields;

    static void check(std::string_view name, std::string_view value){
        if(!validName(name)){
            throw std::invalid_argument("bstd::net::http::Headers: invalid header name \"" + std::string(name) + "\"");
        }
        if(!validValue(value)){
            throw std::invalid_argument("bstd::net::http::Headers: control character in the value of " + std::string(name));
        }
    }

public:
    Headers() = default;
    Headers(std::initializer_list<Field> fields){
        for(const Field& f : fields){
            add(f.first, f.second);
        }
    }

    static bool validName(std::string_view name) noexcept{
        if(name.empty()){
            return false;
        }
        for(const char c : name){
            if(!detail::isTokenChar(c)){
                return false;
            }
        }
        return true;
    }
    static bool validValue(std::string_view value) noexcept { return detail::isFieldText(value); }

    /* Adds another field, even if @param name is already there. */
    void add(std::string_view name, std::string_view value){
        check(name, value);
        _fields.emplace_back(std::string(name), std::string(detail::trimOws(value)));
    }

    /* Replaces every @param name field with one, kept where the first one was. */
    void set(std::string_view name, std::string_view value){
        check(name, value);
        bool found = false;
        for(auto it = _fields.begin(); it != _fields.end();){
            if(!net::detail::equalsIgnoreCase(it->first, name)){
                ++it;
            } else if(!found){
                it->second = std::string(detail::trimOws(value));
                found = true;
                ++it;
            } else{
                it = _fields.erase(it);
            }
        }
        if(!found){
            _fields.emplace_back(std::string(name), std::string(detail::trimOws(value)));
        }
    }

    /* The first value of @param name. */
    std::optional<std::string_view> get(std::string_view name) const noexcept{
        for(const Field& f : _fields){
            if(net::detail::equalsIgnoreCase(f.first, name)){
                return std::string_view(f.second);
            }
        }
        return std::nullopt;
    }
    std::vector<std::string_view> getAll(std::string_view name) const{
        std::vector<std::string_view> out;
        for(const Field& f : _fields){
            if(net::detail::equalsIgnoreCase(f.first, name)){
                out.emplace_back(f.second);
            }
        }
        return out;
    }
    /* Every value of @param name joined with ", ", the way repeated list headers combine. Don't use it for Set-Cookie. */
    std::string getJoined(std::string_view name) const{
        std::string out;
        for(const Field& f : _fields){
            if(net::detail::equalsIgnoreCase(f.first, name)){
                if(!out.empty()){
                    out += ", ";
                }
                out += f.second;
            }
        }
        return out;
    }
    bool has(std::string_view name) const noexcept { return get(name).has_value(); }

    /**
    * True if the comma separated list in any @param name field contains @param token, ignoring case.
    * hasToken("Connection", "close"), hasToken("Transfer-Encoding", "chunked").
    */
    bool hasToken(std::string_view name, std::string_view token) const noexcept{
        for(const Field& f : _fields){
            if(!net::detail::equalsIgnoreCase(f.first, name)){
                continue;
            }
            std::string_view rest = f.second;
            while(true){
                const std::size_t comma = rest.find(',');
                std::string_view item = detail::trimOws(rest.substr(0, comma));
                const std::size_t semicolon = item.find(';');  // drop parameters ("gzip;q=1")
                item = detail::trimOws(item.substr(0, semicolon));
                if(net::detail::equalsIgnoreCase(item, token)){
                    return true;
                }
                if(comma == std::string_view::npos){
                    break;
                }
                rest.remove_prefix(comma + 1);
            }
        }
        return false;
    }

    /* @returns how many fields were removed. */
    std::size_t remove(std::string_view name){
        const std::size_t before = _fields.size();
        for(auto it = _fields.begin(); it != _fields.end();){
            it = net::detail::equalsIgnoreCase(it->first, name) ? _fields.erase(it) : it + 1;
        }
        return before - _fields.size();
    }

    /* Content-Length as a number. nullopt if it's missing or not a plain decimal number. */
    std::optional<std::uint64_t> contentLength() const noexcept{
        const auto v = get("Content-Length");
        if(!v || v->empty() || v->size() > 19){
            return std::nullopt;
        }
        std::uint64_t n = 0;
        for(const char c : *v){
            if(!net::detail::isDigit(c)){
                return std::nullopt;
            }
            n = n * 10 + static_cast<std::uint64_t>(c - '0');
        }
        return n;
    }

    /* The media type of Content-Type, lowercased and without parameters: "application/json". "" if missing. */
    std::string mediaType() const{
        const auto v = get("Content-Type");
        if(!v){
            return {};
        }
        const std::string_view type = detail::trimOws(v->substr(0, v->find(';')));
        std::string out;
        out.reserve(type.size());
        for(const char c : type){
            out.push_back(net::detail::toLowerAscii(c));
        }
        return out;
    }

    std::size_t size() const noexcept { return _fields.size(); }
    bool empty() const noexcept { return _fields.empty(); }
    void clear() noexcept { _fields.clear(); }
    const_iterator begin() const noexcept { return _fields.begin(); }
    const_iterator end() const noexcept { return _fields.end(); }
};

/* ---------- writing messages ---------- */

namespace detail{

inline void appendHeaders(std::string& out, const Headers& headers){
    for(const Headers::Field& f : headers){
        out += f.first;
        out += ": ";
        out += f.second;
        out += "\r\n";
    }
}

/* Body bytes after the header block: as is, or as one chunk plus the last-chunk when Transfer-Encoding is chunked. */
inline void appendBody(std::string& out, const Headers& headers, std::string_view body){
    if(!headers.hasToken("Transfer-Encoding", "chunked")){
        out += body;
        return;
    }
    if(!body.empty()){
        static const char hex[] = "0123456789abcdef";
        char size[2 * sizeof(std::size_t)];
        std::size_t len = 0;
        for(std::size_t n = body.size(); n; n >>= 4){
            size[len++] = hex[n & 0xF];
        }
        for(std::size_t i = len; i > 0; i--){
            out.push_back(size[i - 1]);
        }
        out += "\r\n";
        out += body;
        out += "\r\n";
    }
    out += "0\r\n\r\n";
}

/* Content-Length goes in unless the caller already framed the body. */
inline bool needsLength(const Headers& headers) noexcept{
    return !headers.has("Content-Length") && !headers.has("Transfer-Encoding");
}

inline void checkLength(const Headers& headers, const std::size_t bodySize){
    if(headers.has("Transfer-Encoding")){
        return;
    }
    if(headers.has("Content-Length") && headers.contentLength() != std::optional<std::uint64_t>(bodySize)){
        throw std::invalid_argument("bstd::net::http: Content-Length header doesn't match the body size ("
            + std::to_string(bodySize) + " bytes)");
    }
}

inline bool keepAlive(const Version version, const Headers& headers) noexcept{
    return version == Version::Http11 ? !headers.hasToken("Connection", "close") : headers.hasToken("Connection", "keep-alive");
}

}  // namespace detail

/* ---------- request ---------- */

/**
* An HTTP request. The fields are public, the helpers fill in the usual headers.
* target is the request-target from the request line, normally path and query ("/users?page=2").
*/
struct Request{
    Method method{Method::Get};
    std::string target{"/"};
    Version version{Version::Http11};
    Headers headers;
    std::string body;

    Request() = default;
    Request(const Method m, std::string requestTarget) : method(m), target(std::move(requestTarget)) {}

    /* A request for @param url: target is url.target() and the Host header is set. */
    static Request forUrl(const Method m, const Url& url){
        Request r(m, url.target());
        r.headers.set("Host", url.hostHeader());
        return r;
    }

    /* The target before any '?', still percent-encoded. Copied out on a temporary Request. */
    std::string_view path() const& noexcept{
        const std::string_view t = target;
        return t.substr(0, t.find('?'));
    }
    std::string path() && { return std::string(static_cast<const Request&>(*this).path()); }
    /* The target after '?', still percent-encoded. "" if none. */
    std::string_view rawQuery() const& noexcept{
        const std::string_view t = target;
        const std::size_t q = t.find('?');
        return q == std::string_view::npos ? std::string_view{} : t.substr(q + 1);
    }
    std::string rawQuery() && { return std::string(static_cast<const Request&>(*this).rawQuery()); }
    Query query() const { return Query::parse(rawQuery()); }

    Request& setText(std::string text, std::string_view contentType = "text/plain; charset=utf-8"){
        body = std::move(text);
        headers.set("Content-Type", contentType);
        return *this;
    }
    Request& setJson(const json::Value& value){
        body = value.dump();
        headers.set("Content-Type", "application/json");
        return *this;
    }
    /* An HTML form post body: application/x-www-form-urlencoded. */
    Request& setForm(const Query& fields){
        body = fields.toString();
        headers.set("Content-Type", "application/x-www-form-urlencoded");
        return *this;
    }

    /* Parses the body as JSON (whatever the Content-Type says). */
    json::Result<json::Value> parseJson() const { return json::parse(body); }

    /* Whether the connection stays open after this exchange: HTTP/1.1 unless "Connection: close", HTTP/1.0 only with "keep-alive". */
    bool keepAlive() const noexcept { return detail::keepAlive(version, headers); }

    /**
    * The request as bytes. Adds Content-Length when the body isn't empty or the method is POST/PUT/PATCH,
    * unless Content-Length or Transfer-Encoding is set. With "Transfer-Encoding: chunked" the body is sent as one chunk.
    * @throws `std::invalid_argument` if target is empty or has spaces/control characters, or Content-Length is wrong.
    */
    std::string serialize() const{
        if(target.empty()){
            throw std::invalid_argument("bstd::net::http::Request: empty target");
        }
        for(const char c : target){
            const auto u = static_cast<unsigned char>(c);
            if(u <= 0x20 || u >= 0x7F){
                throw std::invalid_argument("bstd::net::http::Request: target has a space, control or non-ASCII character (percent-encode it)");
            }
        }
        detail::checkLength(headers, body.size());
        std::string out;
        out.reserve(64 + target.size() + body.size() + headers.size() * 32);
        out += methodName(method);
        out.push_back(' ');
        out += target;
        out.push_back(' ');
        out += versionName(version);
        out += "\r\n";
        detail::appendHeaders(out, headers);
        const bool bodyMethod = method == Method::Post || method == Method::Put || method == Method::Patch;
        if(detail::needsLength(headers) && (!body.empty() || bodyMethod)){
            out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        }
        out += "\r\n";
        detail::appendBody(out, headers, body);
        return out;
    }
};

/* ---------- response ---------- */

/* An HTTP response. An empty reason uses the standard phrase for the status. */
struct Response{
    Version version{Version::Http11};
    Status status{Status::Ok};
    std::string reason;
    Headers headers;
    std::string body;

    Response() = default;
    explicit Response(const Status s) : status(s) {}

    static Response makeText(std::string text, const Status s = Status::Ok, std::string_view contentType = "text/plain; charset=utf-8"){
        Response r(s);
        r.setText(std::move(text), contentType);
        return r;
    }
    static Response makeJson(const json::Value& value, const Status s = Status::Ok){
        Response r(s);
        r.setJson(value);
        return r;
    }
    /* @param location can be relative. @throws `std::invalid_argument` if it has control characters. */
    static Response makeRedirect(std::string_view location, const Status s = Status::Found){
        Response r(s);
        r.headers.set("Location", location);
        return r;
    }

    Response& setText(std::string text, std::string_view contentType = "text/plain; charset=utf-8"){
        body = std::move(text);
        headers.set("Content-Type", contentType);
        return *this;
    }
    Response& setJson(const json::Value& value){
        body = value.dump();
        headers.set("Content-Type", "application/json");
        return *this;
    }

    json::Result<json::Value> parseJson() const { return json::parse(body); }

    /* 2xx */
    bool ok() const noexcept { return isSuccess(status); }
    bool keepAlive() const noexcept { return detail::keepAlive(version, headers); }

    /**
    * The status line and headers, ending with the blank line. Adds Content-Length (even 0, so the connection can be
    * reused) unless the status never has a body or Content-Length/Transfer-Encoding is set. Answer a HEAD request with
    * just this: it gives the length of the body without sending it.
    * @throws `std::invalid_argument` if the status isn't 100-999 or the reason has control characters.
    */
    std::string serializeHeaders() const{
        const std::uint16_t c = code(status);
        if(c < 100 || c > 999){
            throw std::invalid_argument("bstd::net::http::Response: status " + std::to_string(c) + " isn't a 3 digit code");
        }
        const std::string_view phrase = reason.empty() ? reasonPhrase(status) : std::string_view(reason);
        if(!detail::isFieldText(phrase)){
            throw std::invalid_argument("bstd::net::http::Response: control character in the reason phrase");
        }
        std::string out;
        out.reserve(64 + headers.size() * 32);
        out += versionName(version);
        out.push_back(' ');
        out += std::to_string(c);
        out.push_back(' ');
        out += phrase;
        out += "\r\n";
        detail::appendHeaders(out, headers);
        if(allowsBody(status) && detail::needsLength(headers)){
            out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        }
        out += "\r\n";
        return out;
    }

    /* serializeHeaders() plus the body. A 1xx, 204 or 304 status never sends one, even if body isn't empty. */
    std::string serialize() const{
        if(!allowsBody(status)){
            return serializeHeaders();
        }
        detail::checkLength(headers, body.size());
        std::string out = serializeHeaders();
        detail::appendBody(out, headers, body);
        return out;
    }
};

}  // namespace http
}  // namespace net
}  // namespace bstd
