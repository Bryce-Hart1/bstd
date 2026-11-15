#pragma once
/**
* bstd::net::Url / Query @author Bryce Hart @date Nov 2026
* Requires C++17 or newer. Header only, no OS specific code.
*
* @details URLs as RFC 3986 defines them, plus query string helpers.
*
*   auto url = net::Url::parse("https://api.example.com/v1/users?page=2").unwrap();
*   url.host();          // "api.example.com"
*   url.portOrDefault(); // 443
*   url.target();        // "/v1/users?page=2"   (what goes on an HTTP request line)
*   url.queryParams().get("page");               // "2"
*   url.resolve("../v2/me?x=1").unwrap().toString(); // "https://api.example.com/v2/me?x=1"
*
*   net::Query q{{"q", "a b&c"}, {"lang", "en"}};
*   url.setQuery(q);     // ?q=a%20b%26c&lang=en
*
* Parsing rules:
*  - Scheme and host are lowercased. The host must be a valid name, IPv4 address, or [IPv6] address.
*    Non-ASCII host names (IDN) aren't supported, pass them already punycode encoded.
*  - http, https, ws and wss URLs must have a host.
*  - In the path, query and fragment, characters RFC 3986 doesn't allow there (spaces, non-ASCII, "<>{}|\^`) are
*    percent-encoded for you. Control characters and a '%' not followed by two hex digits are errors.
*  - "." and ".." path segments are removed.
* Every component is stored in its encoded (wire) form. Use percentDecode() or Query to get decoded text.
*/
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "error.hpp"

namespace bstd{
namespace net{

namespace detail{

constexpr bool isAlpha(const char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool isDigit(const char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool isHex(const char c) noexcept { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
constexpr int hexValue(const char c) noexcept{
    return isDigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
}
constexpr char toLowerAscii(const char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

/* RFC 3986 unreserved: ALPHA / DIGIT / "-" / "." / "_" / "~" */
constexpr bool isUnreserved(const char c) noexcept { return isAlpha(c) || isDigit(c) || c == '-' || c == '.' || c == '_' || c == '~'; }
/* RFC 3986 sub-delims */
constexpr bool isSubDelim(const char c) noexcept{
    switch(c){
        case '!': case '$': case '&': case '\'': case '(': case ')':
        case '*': case '+': case ',': case ';': case '=':
            return true;
        default:
            return false;
    }
}
constexpr bool isPchar(const char c) noexcept { return isUnreserved(c) || isSubDelim(c) || c == ':' || c == '@'; }

inline bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept{
    if(a.size() != b.size()){
        return false;
    }
    for(std::size_t i = 0; i < a.size(); i++){
        if(toLowerAscii(a[i]) != toLowerAscii(b[i])){
            return false;
        }
    }
    return true;
}

inline void appendPercent(std::string& out, const unsigned char c){
    static const char hex[] = "0123456789ABCDEF";
    out.push_back('%');
    out.push_back(hex[c >> 4]);
    out.push_back(hex[c & 0xF]);
}

/* Decodes %XX and optionally '+'. @param strict: a bad escape fails instead of being kept as is. */
inline std::optional<std::string> decode(std::string_view s, const bool plusAsSpace, const bool strict){
    std::string out;
    out.reserve(s.size());
    for(std::size_t i = 0; i < s.size(); i++){
        const char c = s[i];
        if(c == '%'){
            if(i + 2 < s.size() && isHex(s[i + 1]) && isHex(s[i + 2])){
                out.push_back(static_cast<char>(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2])));
                i += 2;
                continue;
            }
            if(strict){
                return std::nullopt;
            }
        } else if(c == '+' && plusAsSpace){
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

enum class Part : std::uint8_t { userinfo, path, query, fragment };

constexpr bool allowedIn(const Part part, const char c) noexcept{
    switch(part){
        case Part::userinfo: return isUnreserved(c) || isSubDelim(c) || c == ':';
        case Part::path:     return isPchar(c) || c == '/';
        case Part::query:
        case Part::fragment: return isPchar(c) || c == '/' || c == '?';
    }
    return false;
}

/**
* Copies @param raw to @param out, percent-encoding characters @param part doesn't allow.
* Existing %XX escapes are kept. @returns an error reason, or nullptr on success.
*/
inline const char* normalizePart(std::string_view raw, const Part part, std::string& out){
    out.clear();
    out.reserve(raw.size());
    for(std::size_t i = 0; i < raw.size(); i++){
        const auto c = static_cast<unsigned char>(raw[i]);
        if(c == '%'){
            if(i + 2 >= raw.size() || !isHex(raw[i + 1]) || !isHex(raw[i + 2])){
                return "'%' must be followed by two hex digits";
            }
            out.append(raw.data() + i, 3);
            i += 2;
        } else if(c < 0x20 || c == 0x7F){
            return "control character in URL";
        } else if(allowedIn(part, static_cast<char>(c))){
            out.push_back(static_cast<char>(c));
        } else{
            appendPercent(out, c);
        }
    }
    return nullptr;
}

/* dec-octet "." dec-octet "." dec-octet "." dec-octet, no leading zeros. */
inline bool isIpv4(std::string_view s) noexcept{
    int parts = 0;
    std::size_t i = 0;
    while(true){
        const std::size_t start = i;
        int value = 0;
        while(i < s.size() && isDigit(s[i]) && i - start < 4){
            value = value * 10 + (s[i] - '0');
            i++;
        }
        const std::size_t len = i - start;
        if(len == 0 || len > 3 || value > 255 || (len > 1 && s[start] == '0')){
            return false;
        }
        parts++;
        if(i == s.size()){
            return parts == 4;
        }
        if(s[i] != '.' || parts == 4){
            return false;
        }
        i++;
    }
}

/* RFC 4291 text form: 8 groups of 1-4 hex digits, one "::" for a run of zero groups, optional IPv4 tail. */
inline bool isIpv6(std::string_view s) noexcept{
    if(s.empty()){
        return false;
    }
    std::size_t groups = 0;
    bool compressed = false;
    std::size_t i = 0;
    if(s.size() >= 2 && s[0] == ':' && s[1] == ':'){
        compressed = true;
        i = 2;
        if(i == s.size()){
            return true;  // "::"
        }
    } else if(s[0] == ':'){
        return false;
    }
    while(i < s.size()){
        const std::size_t start = i;
        while(i < s.size() && isHex(s[i]) && i - start < 5){
            i++;
        }
        if(i < s.size() && s[i] == '.'){
            if(!isIpv4(s.substr(start))){
                return false;
            }
            groups += 2;
            break;
        }
        const std::size_t len = i - start;
        if(len == 0 || len > 4){
            return false;
        }
        groups++;
        if(i == s.size()){
            break;
        }
        if(s[i] != ':'){
            return false;
        }
        i++;
        if(i < s.size() && s[i] == ':'){
            if(compressed){
                return false;  // only one "::"
            }
            compressed = true;
            i++;
        } else if(i == s.size()){
            return false;  // ends in a single ':'
        }
    }
    return compressed ? groups <= 7 : groups == 8;
}

/* RFC 3986 section 5.2.4. */
inline std::string removeDotSegments(std::string_view in){
    std::string out;
    out.reserve(in.size());
    const auto popSegment = [&out]{
        const std::size_t slash = out.rfind('/');
        out.erase(slash == std::string::npos ? 0 : slash);
    };
    const auto startsWith = [&in](std::string_view p){ return in.substr(0, p.size()) == p; };
    while(!in.empty()){
        if(startsWith("../")){
            in.remove_prefix(3);
        } else if(startsWith("./")){
            in.remove_prefix(2);
        } else if(startsWith("/./")){
            in.remove_prefix(2);
        } else if(in == "/."){
            in = "/";
        } else if(startsWith("/../")){
            in.remove_prefix(3);
            popSegment();
        } else if(in == "/.."){
            in = "/";
            popSegment();
        } else if(in == "." || in == ".."){
            in = {};
        } else{
            const std::size_t next = in.find('/', in[0] == '/' ? 1 : 0);
            const std::size_t len = next == std::string_view::npos ? in.size() : next;
            out.append(in.substr(0, len));
            in.remove_prefix(len);
        }
    }
    return out;
}

/* Schemes whose URLs must have a host. */
inline bool needsHost(std::string_view scheme) noexcept{
    return scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss";
}

}  // namespace detail

/* Percent-encodes every byte except RFC 3986 unreserved characters (A-Z a-z 0-9 - . _ ~). Safe for any component. */
inline std::string percentEncode(std::string_view s){
    std::string out;
    out.reserve(s.size());
    for(const char c : s){
        if(detail::isUnreserved(c)){
            out.push_back(c);
        } else{
            detail::appendPercent(out, static_cast<unsigned char>(c));
        }
    }
    return out;
}

/* Decodes %XX escapes (and '+' as a space when @param plusAsSpace). @returns nullopt if a '%' isn't followed by two hex digits. */
inline std::optional<std::string> percentDecode(std::string_view s, const bool plusAsSpace = false){
    return detail::decode(s, plusAsSpace, true);
}

/**
* Decoded key/value pairs of a query string, in order. Keys can repeat.
* parse() reads both "a+b" and "a%20b" as "a b" and keeps malformed escapes as they are, like browsers do.
* toString() encodes everything but unreserved characters, so it's also valid application/x-www-form-urlencoded.
*/
class Query{
public:
    using Param = std::pair<std::string, std::string>;
    using const_iterator = std::vector<Param>::const_iterator;

private:
    std::vector<Param> _params;

public:
    Query() = default;
    Query(std::initializer_list<Param> params) : _params(params) {}

    /* @param raw is the text after '?', a leading '?' is skipped. "a" with no '=' gives the value "". */
    static Query parse(std::string_view raw){
        Query q;
        if(!raw.empty() && raw[0] == '?'){
            raw.remove_prefix(1);
        }
        while(!raw.empty()){
            const std::size_t amp = raw.find('&');
            const std::string_view piece = raw.substr(0, amp);
            raw = amp == std::string_view::npos ? std::string_view{} : raw.substr(amp + 1);
            if(piece.empty()){
                continue;
            }
            const std::size_t eq = piece.find('=');
            const std::string_view key = piece.substr(0, eq);
            const std::string_view value = eq == std::string_view::npos ? std::string_view{} : piece.substr(eq + 1);
            q._params.emplace_back(*detail::decode(key, true, false), *detail::decode(value, true, false));
        }
        return q;
    }

    std::string toString() const{
        std::string out;
        for(const Param& p : _params){
            if(!out.empty()){
                out.push_back('&');
            }
            out += percentEncode(p.first);
            out.push_back('=');
            out += percentEncode(p.second);
        }
        return out;
    }

    void add(std::string key, std::string value) { _params.emplace_back(std::move(key), std::move(value)); }

    /* Replaces the first @param key and removes any others, or appends it. */
    void set(std::string_view key, std::string value){
        bool found = false;
        for(auto it = _params.begin(); it != _params.end();){
            if(it->first != key){
                ++it;
            } else if(!found){
                it->second = std::move(value);
                found = true;
                ++it;
            } else{
                it = _params.erase(it);
            }
        }
        if(!found){
            _params.emplace_back(std::string(key), std::move(value));
        }
    }

    /* The first value for @param key. On a temporary (url.queryParams().get("x")) the value is copied out, so nothing dangles. */
    std::optional<std::string_view> get(std::string_view key) const& noexcept{
        for(const Param& p : _params){
            if(p.first == key){
                return std::string_view(p.second);
            }
        }
        return std::nullopt;
    }
    std::optional<std::string> get(std::string_view key) &&{
        for(Param& p : _params){
            if(p.first == key){
                return std::move(p.second);
            }
        }
        return std::nullopt;
    }
    std::vector<std::string_view> getAll(std::string_view key) const&{
        std::vector<std::string_view> out;
        for(const Param& p : _params){
            if(p.first == key){
                out.emplace_back(p.second);
            }
        }
        return out;
    }
    std::vector<std::string> getAll(std::string_view key) &&{
        std::vector<std::string> out;
        for(Param& p : _params){
            if(p.first == key){
                out.push_back(std::move(p.second));
            }
        }
        return out;
    }
    bool has(std::string_view key) const noexcept{
        for(const Param& p : _params){
            if(p.first == key){
                return true;
            }
        }
        return false;
    }

    /* @returns how many were removed. */
    std::size_t remove(std::string_view key){
        const std::size_t before = _params.size();
        for(auto it = _params.begin(); it != _params.end();){
            it = it->first == key ? _params.erase(it) : it + 1;
        }
        return before - _params.size();
    }

    std::size_t size() const noexcept { return _params.size(); }
    bool empty() const noexcept { return _params.empty(); }
    void clear() noexcept { _params.clear(); }
    const_iterator begin() const noexcept { return _params.begin(); }
    const_iterator end() const noexcept { return _params.end(); }

    friend bool operator==(const Query& a, const Query& b) { return a._params == b._params; }
    friend bool operator!=(const Query& a, const Query& b) { return !(a == b); }
};

/**
* A parsed URL: scheme://userinfo@host:port/path?query#fragment.
* Built with Url::parse(), then changed with the setters or turned into another URL with resolve().
*/
class Url{
public:
    enum class HostType : std::uint8_t{
        None,  // no host (mailto:, file:///)
        Name,  // a registered name like example.com
        IPv4,
        IPv6,  // host() has no brackets
    };

private:
    std::string _scheme;
    bool _hasAuthority{false};
    std::string _userinfo;
    std::string _host;
    HostType _hostType{HostType::None};
    std::optional<std::uint16_t> _port;
    std::string _path;
    std::optional<std::string> _query;
    std::optional<std::string> _fragment;

    static Error invalid(std::string reason) { return Error(Error::Kind::InvalidUrl, std::move(reason)); }

    /* Parses [scheme:][//authority]path[?query][#fragment]. Dot segments are left alone, the caller handles them. */
    static Result<Url> parseReference(std::string_view s, const bool requireScheme){
        const auto isSpace = [](const char c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
        while(!s.empty() && isSpace(s.front())){ s.remove_prefix(1); }
        while(!s.empty() && isSpace(s.back())){ s.remove_suffix(1); }

        Url url;
        std::size_t i = 0;
        while(i < s.size() && (detail::isAlpha(s[i]) || detail::isDigit(s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.')){
            i++;
        }
        if(i > 0 && i < s.size() && s[i] == ':' && detail::isAlpha(s[0])){
            const std::string_view rest = s.substr(i + 1);
            const std::string_view maybeHost = s.substr(0, i);
            const bool hostLike = maybeHost.find('.') != std::string_view::npos || detail::equalsIgnoreCase(maybeHost, "localhost");
            if(requireScheme && hostLike && !rest.empty() && rest.find_first_not_of("0123456789") == std::string_view::npos){
                return ::bstd::error::Err(invalid("missing scheme (\"" + std::string(s) + "\" looks like host:port, add http:// in front)"));
            }
            url._scheme.reserve(i);
            for(std::size_t k = 0; k < i; k++){
                url._scheme.push_back(detail::toLowerAscii(s[k]));
            }
            s = rest;
        } else if(requireScheme){
            return ::bstd::error::Err(invalid(s.empty() ? "empty URL" : "missing scheme (like http://)"));
        }

        std::string scratch;
        const std::size_t hash = s.find('#');
        if(hash != std::string_view::npos){
            if(const char* why = detail::normalizePart(s.substr(hash + 1), detail::Part::fragment, scratch)){
                return ::bstd::error::Err(invalid(why));
            }
            url._fragment = scratch;
            s = s.substr(0, hash);
        }
        const std::size_t question = s.find('?');
        if(question != std::string_view::npos){
            if(const char* why = detail::normalizePart(s.substr(question + 1), detail::Part::query, scratch)){
                return ::bstd::error::Err(invalid(why));
            }
            url._query = scratch;
            s = s.substr(0, question);
        }
        if(s.substr(0, 2) == "//"){
            s.remove_prefix(2);
            const std::size_t slash = s.find('/');
            auto authority = url.parseAuthority(s.substr(0, slash));
            if(authority.isErr()){
                return ::bstd::error::Err(std::move(authority).unwrapErr());
            }
            s = slash == std::string_view::npos ? std::string_view{} : s.substr(slash);
        }
        if(const char* why = detail::normalizePart(s, detail::Part::path, url._path)){
            return ::bstd::error::Err(invalid(why));
        }
        return ::bstd::error::Ok(std::move(url));
    }

    Result<void> parseAuthority(std::string_view a){
        _hasAuthority = true;
        const std::size_t at = a.rfind('@');
        if(at != std::string_view::npos){
            if(const char* why = detail::normalizePart(a.substr(0, at), detail::Part::userinfo, _userinfo)){
                return ::bstd::error::Err(invalid(why));
            }
            a = a.substr(at + 1);
        }
        std::string_view portText;
        bool hasPort = false;
        if(!a.empty() && a[0] == '['){
            const std::size_t close = a.find(']');
            if(close == std::string_view::npos){
                return ::bstd::error::Err(invalid("missing ']' after IPv6 address"));
            }
            const std::string_view address = a.substr(1, close - 1);
            if(!detail::isIpv6(address)){
                return ::bstd::error::Err(invalid("invalid IPv6 address \"" + std::string(address) + "\""));
            }
            for(const char c : address){
                _host.push_back(detail::toLowerAscii(c));
            }
            _hostType = HostType::IPv6;
            const std::string_view after = a.substr(close + 1);
            if(!after.empty()){
                if(after[0] != ':'){
                    return ::bstd::error::Err(invalid("unexpected text after IPv6 address"));
                }
                hasPort = true;
                portText = after.substr(1);
            }
        } else{
            const std::size_t colon = a.rfind(':');
            const std::string_view name = a.substr(0, colon);
            if(colon != std::string_view::npos){
                hasPort = true;
                portText = a.substr(colon + 1);
            }
            for(std::size_t i = 0; i < name.size(); i++){
                const char c = name[i];
                if(c == '%'){
                    if(i + 2 >= name.size() || !detail::isHex(name[i + 1]) || !detail::isHex(name[i + 2])){
                        return ::bstd::error::Err(invalid("'%' must be followed by two hex digits"));
                    }
                } else if(!detail::isUnreserved(c) && !detail::isSubDelim(c)){
                    const auto u = static_cast<unsigned char>(c);
                    if(u >= 0x80){
                        return ::bstd::error::Err(invalid("non-ASCII host names aren't supported, use the punycode (xn--) form"));
                    }
                    return ::bstd::error::Err(invalid(u < 0x20 || u == 0x7F ? std::string("control character in host")
                                                                             : "invalid character '" + std::string(1, c) + "' in host"));
                }
                _host.push_back(detail::toLowerAscii(c));
            }
            _hostType = _host.empty() ? HostType::None : detail::isIpv4(_host) ? HostType::IPv4 : HostType::Name;
        }
        if(hasPort && !portText.empty()){
            if(portText.find_first_not_of("0123456789") != std::string_view::npos){
                return ::bstd::error::Err(invalid("port must be a number"));
            }
            unsigned long value = 0;
            for(const char c : portText){
                value = value * 10 + static_cast<unsigned long>(c - '0');
                if(value > 65535){
                    return ::bstd::error::Err(invalid("port above 65535"));
                }
            }
            _port = static_cast<std::uint16_t>(value);
        }
        return ::bstd::error::Ok();
    }

    /* Rules for a finished absolute URL. */
    Result<Url> finish(){
        if(detail::needsHost(_scheme) && _host.empty()){
            return ::bstd::error::Err(invalid(_scheme + " URLs need a host (" + _scheme + "://host/...)"));
        }
        if(!_path.empty() && _path[0] == '/'){
            _path = detail::removeDotSegments(_path);
        }
        return ::bstd::error::Ok(std::move(*this));
    }

public:
    Url() = default;

    /* Parses an absolute URL (it must have a scheme). Whitespace at either end is ignored. */
    static Result<Url> parse(std::string_view text){
        auto r = parseReference(text, true);
        if(r.isErr()){
            return r;
        }
        return r.unwrap().finish();
    }

    /* Default port of a scheme: http/ws 80, https/wss 443, ftp 21. */
    static std::optional<std::uint16_t> defaultPort(std::string_view scheme) noexcept{
        if(scheme == "http" || scheme == "ws"){ return 80; }
        if(scheme == "https" || scheme == "wss"){ return 443; }
        if(scheme == "ftp"){ return 21; }
        return std::nullopt;
    }

    /**
    * Resolves @param reference against this URL (RFC 3986 section 5.2), the way a browser follows a link or a
    * Location header: "other", "../up", "/root", "?q=1", "#frag", "//host/path" or a full URL.
    */
    Result<Url> resolve(std::string_view reference) const{
        auto parsed = parseReference(reference, false);
        if(parsed.isErr()){
            return parsed;
        }
        Url r = std::move(parsed).unwrap();
        Url t;
        if(!r._scheme.empty()){
            t = std::move(r);
        } else{
            if(r._hasAuthority){
                t._hasAuthority = true;
                t._userinfo = std::move(r._userinfo);
                t._host = std::move(r._host);
                t._hostType = r._hostType;
                t._port = r._port;
                t._path = std::move(r._path);
                t._query = std::move(r._query);
            } else{
                if(r._path.empty()){
                    t._path = _path;
                    t._query = r._query ? std::move(r._query) : _query;
                } else{
                    if(r._path[0] == '/'){
                        t._path = std::move(r._path);
                    } else if(_hasAuthority && _path.empty()){
                        t._path = "/" + r._path;
                    } else{
                        const std::size_t slash = _path.rfind('/');
                        t._path = (slash == std::string::npos ? std::string() : _path.substr(0, slash + 1)) + r._path;
                    }
                    t._query = std::move(r._query);
                }
                t._hasAuthority = _hasAuthority;
                t._userinfo = _userinfo;
                t._host = _host;
                t._hostType = _hostType;
                t._port = _port;
            }
            t._scheme = _scheme;
            t._fragment = std::move(r._fragment);
        }
        if(t._hasAuthority || !t._path.empty()){
            t._path = detail::removeDotSegments(t._path);
        }
        return t.finish();
    }

    /* ---------- parts ---------- */

    /* Lowercase, without the ':'. */
    const std::string& scheme() const noexcept { return _scheme; }
    /* True when the URL had "//", even if the host is empty (file:///tmp). */
    bool hasAuthority() const noexcept { return _hasAuthority; }
    /* "user:pass" in encoded form, empty if none. */
    const std::string& userinfo() const noexcept { return _userinfo; }
    /* Lowercase. IPv6 addresses have no brackets. */
    const std::string& host() const noexcept { return _host; }
    HostType hostType() const noexcept { return _hostType; }
    /* The port written in the URL, if any. */
    std::optional<std::uint16_t> port() const noexcept { return _port; }
    /* The written port, else the scheme's default, else 0. */
    std::uint16_t portOrDefault() const noexcept { return _port ? *_port : defaultPort(_scheme).value_or(0); }
    /* Encoded. Can be empty ("http://example.com"). */
    const std::string& path() const noexcept { return _path; }
    bool hasQuery() const noexcept { return _query.has_value(); }
    /* Encoded, without the '?'. Empty if there is none. Copied out on a temporary Url, so nothing dangles. */
    std::string_view query() const& noexcept { return _query ? std::string_view(*_query) : std::string_view{}; }
    std::string query() && { return _query ? std::move(*_query) : std::string(); }
    Query queryParams() const { return Query::parse(query()); }
    bool hasFragment() const noexcept { return _fragment.has_value(); }
    /* Encoded, without the '#'. Copied out on a temporary Url. */
    std::string_view fragment() const& noexcept { return _fragment ? std::string_view(*_fragment) : std::string_view{}; }
    std::string fragment() && { return _fragment ? std::move(*_fragment) : std::string(); }

    /* The host with brackets around IPv6, plus ":port" when the port isn't the scheme's default. What an HTTP Host header holds. */
    std::string hostHeader() const{
        std::string out = _hostType == HostType::IPv6 ? "[" + _host + "]" : _host;
        if(_port && _port != defaultPort(_scheme)){
            out += ":" + std::to_string(*_port);
        }
        return out;
    }

    /* Path and query for an HTTP request line ("/a/b?x=1"). An empty path becomes "/". The fragment is never sent. */
    std::string target() const{
        std::string out = _path.empty() ? "/" : _path;
        if(_query){
            out += "?" + *_query;
        }
        return out;
    }

    std::string toString() const{
        std::string out = _scheme;
        if(!out.empty()){
            out.push_back(':');
        }
        if(_hasAuthority){
            out += "//";
            if(!_userinfo.empty()){
                out += _userinfo + "@";
            }
            out += _hostType == HostType::IPv6 ? "[" + _host + "]" : _host;
            if(_port){
                out += ":" + std::to_string(*_port);
            }
        }
        out += _path;
        if(_query){
            out += "?" + *_query;
        }
        if(_fragment){
            out += "#" + *_fragment;
        }
        return out;
    }

    /* ---------- setters ---------- */
    /* Path, query and fragment setters take encoded text and encode anything not allowed, like parse() does.
     * @throws `std::invalid_argument` on a control character or a bad '%' escape. */

    Url& setPath(std::string_view path){
        std::string out;
        if(const char* why = detail::normalizePart(path, detail::Part::path, out)){
            throw std::invalid_argument(std::string("bstd::net::Url::setPath: ") + why);
        }
        if(_hasAuthority && !out.empty() && out[0] != '/'){
            out.insert(out.begin(), '/');
        }
        _path = out.empty() || out[0] != '/' ? std::move(out) : detail::removeDotSegments(out);
        return *this;
    }
    Url& setQuery(std::string_view encoded){
        std::string out;
        if(const char* why = detail::normalizePart(encoded, detail::Part::query, out)){
            throw std::invalid_argument(std::string("bstd::net::Url::setQuery: ") + why);
        }
        _query = std::move(out);
        return *this;
    }
    Url& setQuery(const Query& q){
        _query = q.toString();
        return *this;
    }
    Url& clearQuery() noexcept{
        _query.reset();
        return *this;
    }
    Url& setFragment(std::string_view encoded){
        std::string out;
        if(const char* why = detail::normalizePart(encoded, detail::Part::fragment, out)){
            throw std::invalid_argument(std::string("bstd::net::Url::setFragment: ") + why);
        }
        _fragment = std::move(out);
        return *this;
    }
    Url& clearFragment() noexcept{
        _fragment.reset();
        return *this;
    }
    Url& setPort(const std::optional<std::uint16_t> port) noexcept{
        _port = port;
        return *this;
    }

    /* Same text. Doesn't treat an explicit default port as equal to no port. */
    friend bool operator==(const Url& a, const Url& b) { return a.toString() == b.toString(); }
    friend bool operator!=(const Url& a, const Url& b) { return !(a == b); }
};

inline std::ostream& operator<<(std::ostream& os, const Url& url) { return os << url.toString(); }

}  // namespace net
}  // namespace bstd
