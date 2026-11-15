// Tests for net/url.hpp. Run with tests/run.sh, or build this file on its own.
#include "../../net/url.hpp"
#include "../check.hpp"

#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using bstd::net::Error;
using bstd::net::Query;
using bstd::net::Url;
using HostType = Url::HostType;

static Url ok(std::string_view text){
    auto r = Url::parse(text);
    if(r.isErr()){
        failures++;
        std::cerr << "unexpected URL parse failure for " << text << ": " << r.unwrapErr().message() << "\n";
        return {};
    }
    return std::move(r).unwrap();
}

static void bad(std::string_view text){
    checks++;
    auto r = Url::parse(text);
    if(r.isOk()){
        failures++;
        std::cerr << "expected URL parse failure for: " << text << " (got " << r.unwrap() << ")\n";
    } else if(r.unwrapErr().kind() != Error::Kind::InvalidUrl){
        failures++;
        std::cerr << "wrong error kind for: " << text << "\n";
    }
}

static std::string optionalPort(const Url& u){
    return u.port() ? std::to_string(*u.port()) : "none";
}

static void testParts(){
    const Url u = ok("https://User:Pa%20ss@API.Example.COM:8443/a/b?x=1&y=2#frag");
    CHECK_EQ(u.scheme(), "https");
    CHECK(u.hasAuthority());
    CHECK_EQ(u.userinfo(), "User:Pa%20ss");
    CHECK_EQ(u.host(), "api.example.com");
    CHECK(u.hostType() == HostType::Name);
    CHECK_EQ(optionalPort(u), "8443");
    CHECK_EQ(u.path(), "/a/b");
    CHECK_EQ(u.query(), "x=1&y=2");
    CHECK_EQ(u.fragment(), "frag");
    CHECK_EQ(u.target(), "/a/b?x=1&y=2");
    CHECK_EQ(u.hostHeader(), "api.example.com:8443");
    CHECK_EQ(u.toString(), "https://User:Pa%20ss@api.example.com:8443/a/b?x=1&y=2#frag");
    CHECK(Url::parse(u.toString()).unwrap() == u);

    const Url plain = ok("HTTP://example.com");
    CHECK_EQ(plain.scheme(), "http");
    CHECK_EQ(optionalPort(plain), "none");
    CHECK_EQ(plain.portOrDefault(), 80);
    CHECK_EQ(plain.path(), "");
    CHECK_EQ(plain.target(), "/");
    CHECK(!plain.hasQuery() && !plain.hasFragment());
    CHECK_EQ(plain.hostHeader(), "example.com");

    const Url explicitDefault = ok("http://example.com:80/");
    CHECK_EQ(explicitDefault.hostHeader(), "example.com");
    CHECK_EQ(explicitDefault.toString(), "http://example.com:80/");
    CHECK_EQ(ok("https://h/").portOrDefault(), 443);
    CHECK_EQ(ok("wss://h/").portOrDefault(), 443);
    CHECK_EQ(ok("http://h:/").toString(), "http://h/");  // empty port means none
    CHECK_EQ(optionalPort(ok("http://h:0/")), "0");
    CHECK_EQ(optionalPort(ok("http://h:65535/")), "65535");
    CHECK_EQ(ok("foo://h").portOrDefault(), 0);

    const Url emptyQuery = ok("http://h/?");
    CHECK(emptyQuery.hasQuery());
    CHECK_EQ(emptyQuery.query(), "");
    CHECK_EQ(emptyQuery.toString(), "http://h/?");
    CHECK_EQ(ok("http://h/?a#").toString(), "http://h/?a#");
    CHECK_EQ(ok("http://h/p#f?not-query").fragment(), "f?not-query");
    CHECK(!ok("http://h/p#f?not-query").hasQuery());

    CHECK_EQ(ok("  http://h/x \n").toString(), "http://h/x");
    CHECK_EQ(ok("http://user@h/").userinfo(), "user");
    CHECK_EQ(ok("http://a@b@h/").host(), "h");  // the last @ ends the userinfo
}

static void testOtherSchemes(){
    const Url tel = ok("tel:5551234");
    CHECK_EQ(tel.scheme(), "tel");
    CHECK(!tel.hasAuthority());
    CHECK_EQ(tel.path(), "5551234");

    const Url mail = ok("mailto:bob@example.com");
    CHECK_EQ(mail.path(), "bob@example.com");
    CHECK(mail.hostType() == HostType::None);
    CHECK_EQ(mail.toString(), "mailto:bob@example.com");

    const Url file = ok("file:///tmp/x.txt");
    CHECK(file.hasAuthority());
    CHECK_EQ(file.host(), "");
    CHECK_EQ(file.path(), "/tmp/x.txt");
    CHECK_EQ(file.toString(), "file:///tmp/x.txt");

    CHECK_EQ(ok("urn:isbn:0451450523").path(), "isbn:0451450523");
    CHECK_EQ(ok("a.b-c+d://h").scheme(), "a.b-c+d");
}

static void testHosts(){
    const Url v6 = ok("http://[2001:DB8::1]:8080/x");
    CHECK_EQ(v6.host(), "2001:db8::1");
    CHECK(v6.hostType() == HostType::IPv6);
    CHECK_EQ(v6.hostHeader(), "[2001:db8::1]:8080");
    CHECK_EQ(v6.toString(), "http://[2001:db8::1]:8080/x");

    for(const char* good : {"http://[::]/", "http://[::1]/", "http://[1::]/", "http://[1:2:3:4:5:6:7:8]/",
                            "http://[1:2:3:4:5:6:7::]/", "http://[::2:3:4:5:6:7:8]/", "http://[::ffff:1.2.3.4]/",
                            "http://[1:2:3:4:5:6:1.2.3.4]/", "http://[fe80::abcd:ef01]/"}){
        checks++;
        if(Url::parse(good).isErr()){
            failures++;
            std::cerr << "rejected valid IPv6 URL " << good << "\n";
        }
    }
    for(const char* wrong : {"http://[::1/", "http://[1:2:3:4:5:6:7:8:9]/", "http://[1::2::3]/", "http://[::1]x/",
                             "http://[12345::]/", "http://[:1]/", "http://[1:]/", "http://[1.2.3.4]/", "http://[::1.2.3]/",
                             "http://[1:2:3:4:5:6:7:1.2.3.4]/", "http://[]/", "http://[g::1]/", "http://[fe80::1%25eth0]/"}){
        bad(wrong);
    }

    CHECK(ok("http://127.0.0.1/").hostType() == HostType::IPv4);
    CHECK(ok("http://256.1.1.1/").hostType() == HostType::Name);   // not an address, but a legal name
    CHECK(ok("http://01.2.3.4/").hostType() == HostType::Name);
    CHECK(ok("http://1.2.3/").hostType() == HostType::Name);
    CHECK_EQ(ok("http://xn--mnchen-3ya.de/").host(), "xn--mnchen-3ya.de");
    CHECK_EQ(ok("http://a%2Db.com/").host(), "a%2db.com");
}

static void testErrors(){
    bad("");
    bad("   ");
    bad("example.com/path");
    bad("/just/a/path");
    bad("1http://x/");
    bad("http://");
    bad("http:///x");
    bad("http://:80/");
    bad("http:foo");
    bad("http://h:99999/");
    bad("http://h:65536/");
    bad("http://h:12a/");
    bad("http://h:-1/");
    bad("http://ho st/");
    bad("http://h\x01/");
    bad("http://h<x>/");
    bad("http://m\xC3\xBCnchen.de/");
    bad("http://h/%zz");
    bad("http://h/a%2");
    bad("http://h/?q=%");
    bad("http://h/a\nb");
    bad("http://h/a\x7F");

    auto hint = Url::parse("localhost:8080");
    CHECK(hint.isErr());
    CHECK(hint.unwrapErr().message().find("add http://") != std::string::npos);
    CHECK(Url::parse("example.com:443").isErr());
    CHECK_EQ(Url::parse("http://h:99999/").unwrapErr().message(), "invalid URL: port above 65535");
}

static void testNormalizing(){
    const Url u = ok("http://h/a b/\xC3\xBC?q=a b&r=<x>#f g");
    CHECK_EQ(u.path(), "/a%20b/%C3%BC");
    CHECK_EQ(u.query(), "q=a%20b&r=%3Cx%3E");
    CHECK_EQ(u.fragment(), "f%20g");
    CHECK_EQ(u.queryParams().get("q").value_or("?"), "a b");

    CHECK_EQ(ok("http://h/a%2fb").path(), "/a%2fb");  // existing escapes are left as written
    CHECK_EQ(ok("http://h/a/b/../c/./d").path(), "/a/c/d");
    CHECK_EQ(ok("http://h/../x").path(), "/x");
    CHECK_EQ(ok("http://h/a/..").path(), "/");
    CHECK_EQ(ok("http://h/a/b/").path(), "/a/b/");
    CHECK_EQ(ok("http://h/a?b/../c").query(), "b/../c");  // only the path loses dot segments
}

static void testResolve(){
    // RFC 3986 section 5.4
    const Url base = ok("http://a/b/c/d;p?q");
    const std::vector<std::pair<const char*, const char*>> cases = {
        {"g:h", "g:h"},
        {"g", "http://a/b/c/g"},
        {"./g", "http://a/b/c/g"},
        {"g/", "http://a/b/c/g/"},
        {"/g", "http://a/g"},
        {"//g", "http://g"},
        {"?y", "http://a/b/c/d;p?y"},
        {"g?y", "http://a/b/c/g?y"},
        {"#s", "http://a/b/c/d;p?q#s"},
        {"g#s", "http://a/b/c/g#s"},
        {"g?y#s", "http://a/b/c/g?y#s"},
        {";x", "http://a/b/c/;x"},
        {"g;x", "http://a/b/c/g;x"},
        {"g;x?y#s", "http://a/b/c/g;x?y#s"},
        {"", "http://a/b/c/d;p?q"},
        {".", "http://a/b/c/"},
        {"./", "http://a/b/c/"},
        {"..", "http://a/b/"},
        {"../", "http://a/b/"},
        {"../g", "http://a/b/g"},
        {"../..", "http://a/"},
        {"../../", "http://a/"},
        {"../../g", "http://a/g"},
        {"../../../g", "http://a/g"},
        {"../../../../g", "http://a/g"},
        {"/./g", "http://a/g"},
        {"/../g", "http://a/g"},
        {"g.", "http://a/b/c/g."},
        {".g", "http://a/b/c/.g"},
        {"g..", "http://a/b/c/g.."},
        {"..g", "http://a/b/c/..g"},
        {"./../g", "http://a/b/g"},
        {"./g/.", "http://a/b/c/g/"},
        {"g/./h", "http://a/b/c/g/h"},
        {"g/../h", "http://a/b/c/h"},
        {"g;x=1/./y", "http://a/b/c/g;x=1/y"},
        {"g;x=1/../y", "http://a/b/c/y"},
        {"g?y/./x", "http://a/b/c/g?y/./x"},
        {"g?y/../x", "http://a/b/c/g?y/../x"},
        {"g#s/./x", "http://a/b/c/g#s/./x"},
        {"g#s/../x", "http://a/b/c/g#s/../x"},
        {"https://other.com/x", "https://other.com/x"},
    };
    for(const auto& c : cases){
        checks++;
        auto r = base.resolve(c.first);
        const std::string got = r.isOk() ? r.unwrap().toString() : "error: " + r.unwrapErr().message();
        if(got != c.second){
            failures++;
            std::cerr << "resolve(\"" << c.first << "\") gave " << got << ", expected " << c.second << "\n";
        }
    }
    CHECK(base.resolve("http:g").isErr());  // strict parsers read this as http:g, which has no host
    CHECK_EQ(ok("http://h").resolve("x").unwrap().toString(), "http://h/x");
    CHECK_EQ(ok("http://h/dir/").resolve("a b").unwrap().path(), "/dir/a%20b");
    CHECK(ok("http://h/").resolve("/%zz").isErr());
}

static void testSetters(){
    Url u = ok("http://h/old?x=1#top");
    u.setQuery(Query{{"q", "a b&c"}, {"lang", "en"}});
    CHECK_EQ(u.query(), "q=a%20b%26c&lang=en");
    u.setPath("x y");
    CHECK_EQ(u.path(), "/x%20y");
    u.setPath("/a/../b");
    CHECK_EQ(u.path(), "/b");
    u.setFragment("sec 2");
    CHECK_EQ(u.fragment(), "sec%202");
    u.setPort(9000);
    CHECK_EQ(u.hostHeader(), "h:9000");
    u.setQuery("a=<b>");
    CHECK_EQ(u.query(), "a=%3Cb%3E");
    u.clearQuery().clearFragment().setPort(std::nullopt);
    CHECK_EQ(u.toString(), "http://h/b");
    CHECK_THROWS(u.setPath("/\x01"), std::invalid_argument);
    CHECK_THROWS(u.setQuery("%g0"), std::invalid_argument);

    std::ostringstream os;
    os << u;
    CHECK_EQ(os.str(), "http://h/b");
    CHECK(ok("http://H/b") == u);
    CHECK(ok("http://h/c") != u);
}

static void testPercentCoding(){
    CHECK_EQ(bstd::net::percentEncode("a b/\xC3\xBC~-._!"), "a%20b%2F%C3%BC~-._%21");
    CHECK_EQ(bstd::net::percentDecode("a%20b+c").value_or("?"), "a b+c");
    CHECK_EQ(bstd::net::percentDecode("a%20b+c", true).value_or("?"), "a b c");
    CHECK_EQ(bstd::net::percentDecode("%41%62").value_or("?"), "Ab");
    CHECK(!bstd::net::percentDecode("%zz"));
    CHECK(!bstd::net::percentDecode("%4"));
    CHECK(!bstd::net::percentDecode("%"));
    CHECK_EQ(bstd::net::percentDecode(bstd::net::percentEncode("\x01\xFF" " any\"thing")).value_or("?"), "\x01\xFF" " any\"thing");
}

static void testQuery(){
    Query q = Query::parse("?a=1&b=two+words&a=3&empty=&flag&&c=%E2%82%AC&bad=%zz&eq=x=y");
    CHECK_EQ(q.size(), 8u);
    CHECK_EQ(q.get("a").value_or("?"), "1");
    CHECK_EQ(q.getAll("a").size(), 2u);
    CHECK_EQ(q.getAll("a")[1], "3");
    CHECK_EQ(q.get("b").value_or("?"), "two words");
    CHECK_EQ(q.get("empty").value_or("?"), "");
    CHECK_EQ(q.get("flag").value_or("?"), "");
    CHECK_EQ(q.get("c").value_or("?"), "\xE2\x82\xAC");
    CHECK_EQ(q.get("bad").value_or("?"), "%zz");
    CHECK_EQ(q.get("eq").value_or("?"), "x=y");
    CHECK(!q.get("nope"));
    CHECK(q.has("flag"));

    CHECK(Query::parse(q.toString()) == q);

    q.set("a", "x");
    CHECK_EQ(q.getAll("a").size(), 1u);
    CHECK_EQ(q.begin()->first, "a");
    CHECK_EQ(q.begin()->second, "x");
    q.set("new", "v");
    CHECK_EQ(std::prev(q.end())->first, "new");
    CHECK_EQ(q.remove("b"), 1u);
    CHECK_EQ(q.remove("b"), 0u);

    const Query built{{"k", "v w"}, {"a&b", "c=d"}, {"plus", "1+1"}};
    CHECK_EQ(built.toString(), "k=v%20w&a%26b=c%3Dd&plus=1%2B1");
    CHECK(Query::parse("").empty());
    CHECK(Query::parse("&&&").empty());
}

int main(){
    testParts();
    testOtherSchemes();
    testHosts();
    testErrors();
    testNormalizing();
    testResolve();
    testSetters();
    testPercentCoding();
    testQuery();
    return finishTests();
}
