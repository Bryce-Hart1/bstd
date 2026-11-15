// Tests for net/json.hpp. Build and run from C_Plus_Plus/:
//   c++ -std=c++17 -Wall -Wextra -pedantic tests/net/jsonTest.cpp -o /tmp/jsonTest && /tmp/jsonTest
// or run every test with tests/run.sh
#include "../../net/json.hpp"
#include "../check.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace json = bstd::net::json;
using Kind = json::Error::Kind;

static json::Value ok(std::string_view text, const json::ParseOptions& opts = {}){
    auto r = json::parse(text, opts);
    if(r.isErr()){
        failures++;
        std::cerr << "unexpected parse failure for " << text << ": " << r.unwrapErr().message() << "\n";
        return {};
    }
    return std::move(r).unwrap();
}

/* Checks that @param text fails to parse with @param kind. */
static void bad(std::string_view text, Kind kind, const json::ParseOptions& opts = {}){
    checks++;
    auto r = json::parse(text, opts);
    if(r.isOk()){
        failures++;
        std::cerr << "expected a parse failure for: " << text << "\n";
    } else if(r.unwrapErr().kind() != kind){
        failures++;
        std::cerr << "wrong error kind for: " << text << " (" << r.unwrapErr().message() << ")\n";
    }
}

/* ---------- parsing ---------- */

static void testScalars(){
    CHECK(ok("null").isNull());
    CHECK_EQ(ok("true").asBool(), true);
    CHECK_EQ(ok("false").asBool(), false);
    CHECK_EQ(ok("0").asInt(), 0);
    CHECK_EQ(ok("123").asInt(), 123);
    CHECK_EQ(ok("-45").asInt(), -45);
    CHECK_EQ(ok("9223372036854775807").asInt(), INT64_MAX);
    CHECK_EQ(ok("-9223372036854775808").asInt(), INT64_MIN);

    const json::Value big = ok("9223372036854775808");
    CHECK(big.isDouble());
    CHECK_EQ(big.asDouble(), 9223372036854775808.0);
    CHECK(ok("123456789012345678901234567890").isDouble());

    const json::Value negZero = ok("-0");
    CHECK(negZero.isDouble());
    CHECK(std::signbit(negZero.asDouble()));

    CHECK_EQ(ok("1.5").asDouble(), 1.5);
    CHECK(ok("1e3").isDouble());
    CHECK_EQ(ok("1e3").asDouble(), 1000.0);
    CHECK_EQ(ok("1E-2").asDouble(), 0.01);
    CHECK_EQ(ok("-2.5e+2").asDouble(), -250.0);
    CHECK_EQ(ok("1.7976931348623157e308").asDouble(), 1.7976931348623157e308);
    CHECK(ok("4.9e-324").asDouble() > 0.0);
    CHECK_EQ(ok("1e-400").asDouble(), 0.0);
    CHECK(std::signbit(ok("-1e-400").asDouble()));
    CHECK_EQ(ok("0.000000000000000000000000000000000000000000000000001e-400").asDouble(), 0.0);
    bad("1e400", Kind::NumberRange);
    bad("-1e400", Kind::NumberRange);
    bad("123456789e999999999999999999", Kind::NumberRange);

    CHECK_EQ(ok(" \t\r\n 7 \n").asInt(), 7);
    CHECK_EQ(ok("\xEF\xBB\xBF[1]").size(), 1u);
}

static void testStrings(){
    CHECK_EQ(ok(R"("hello")").asString(), "hello");
    CHECK_EQ(ok(R"("")").asString(), "");
    CHECK_EQ(ok(R"("\"\\\/\b\f\n\r\t")").asString(), "\"\\/\b\f\n\r\t");
    CHECK_EQ(ok(R"("A\u00e9\u20AC")").asString(), "A\xC3\xA9\xE2\x82\xAC");
    CHECK_EQ(ok(R"("\ud83d\ude00")").asString(), "\xF0\x9F\x98\x80");
    CHECK_EQ(ok("\"caf\xC3\xA9 \xF0\x9F\x98\x80\"").asString(), "caf\xC3\xA9 \xF0\x9F\x98\x80");
    CHECK_EQ(ok(R"("a\u0000b")").asString(), std::string("a\0b", 3));

    bad("\"abc", Kind::Syntax);
    bad("\"a\tb\"", Kind::Syntax);          // raw control character
    bad("\"a\nb\"", Kind::Syntax);
    bad(R"("\x")", Kind::Syntax);
    bad(R"("\u12")", Kind::Syntax);
    bad(R"("\u12G4")", Kind::Syntax);
    bad(R"("\ud800")", Kind::InvalidUnicode);
    bad(R"("\ud800x")", Kind::InvalidUnicode);
    bad(R"("\ud800A")", Kind::InvalidUnicode);
    bad(R"("\udc00")", Kind::InvalidUnicode);
    bad("\"\xC3\x28\"", Kind::InvalidUnicode);           // bad continuation byte
    bad("\"\xC0\xAF\"", Kind::InvalidUnicode);           // overlong
    bad("\"\xE0\x80\xAF\"", Kind::InvalidUnicode);       // overlong
    bad("\"\xED\xA0\x80\"", Kind::InvalidUnicode);       // UTF-8 encoded surrogate
    bad("\"\xF4\x90\x80\x80\"", Kind::InvalidUnicode);   // above U+10FFFF
    bad("\"\xFF\"", Kind::InvalidUnicode);
    bad("\"\xE2\x82\"", Kind::InvalidUnicode);           // truncated
}

static void testStructure(){
    const json::Value v = ok(R"({"b": [1, 2, {"c": null}], "a": {}, "z": []})");
    CHECK(v.isObject());
    CHECK_EQ(v.size(), 3u);
    std::vector<std::string> keys;
    for(const auto& m : v.asObject()){
        keys.push_back(m.first);
    }
    CHECK(keys == (std::vector<std::string>{"b", "a", "z"}));  // insertion order kept
    CHECK_EQ(v["b"][1].asInt(), 2);
    CHECK(v["b"][2]["c"].isNull());
    CHECK(v["a"].isObject() && v["a"].empty());
    CHECK(v["z"].isArray() && v["z"].empty());

    bad("", Kind::Syntax);
    bad("   ", Kind::Syntax);
    bad("[", Kind::Syntax);
    bad("{", Kind::Syntax);
    bad("[1,]", Kind::Syntax);
    bad("[,1]", Kind::Syntax);
    bad(R"({"a":1,})", Kind::Syntax);
    bad("[1 2]", Kind::Syntax);
    bad(R"({"a" 1})", Kind::Syntax);
    bad("{1:2}", Kind::Syntax);
    bad("{'a':1}", Kind::Syntax);
    bad("[1] x", Kind::Syntax);
    bad("[1]]", Kind::Syntax);
    bad("tru", Kind::Syntax);
    bad("nul", Kind::Syntax);
    bad("True", Kind::Syntax);
    bad("[NaN]", Kind::Syntax);
    bad("[Infinity]", Kind::Syntax);
    bad("[01]", Kind::Syntax);
    bad("[-01]", Kind::Syntax);
    bad("[1.]", Kind::Syntax);
    bad("[.5]", Kind::Syntax);
    bad("[-]", Kind::Syntax);
    bad("[1e]", Kind::Syntax);
    bad("[1e+]", Kind::Syntax);
    bad("[+1]", Kind::Syntax);
    bad("[0x10]", Kind::Syntax);
    bad("// c\n1", Kind::Syntax);
    bad("\xEF\xBB\xBF", Kind::Syntax);
}

static void testDepth(){
    const auto nested = [](std::size_t n){ return std::string(n, '[') + std::string(n, ']'); };
    CHECK(json::parse(nested(512)).isOk());
    bad(nested(513), Kind::TooDeep);
    bad(std::string(100000, '['), Kind::TooDeep);  // must not overflow the stack

    json::ParseOptions shallow;
    shallow.maxDepth = 2;
    CHECK(json::parse(R"({"a":[1]})", shallow).isOk());
    bad(R"({"a":[[1]]})", Kind::TooDeep, shallow);
}

static void testErrorLocation(){
    auto r = json::parse("{\n  \"a\": x\n}");
    CHECK(r.isErr());
    const json::Error& e = r.unwrapErr();
    CHECK(e.hasLocation());
    CHECK_EQ(e.line(), 2u);
    CHECK_EQ(e.column(), 8u);
    CHECK_EQ(e.offset(), 9u);
    CHECK_EQ(e.message(), "line 2, column 8: unexpected 'x', expected a value");

    auto unterminated = json::parse("[1, 2");
    CHECK_EQ(unterminated.unwrapErr().message(), "line 1, column 6: unexpected end of input, expected ',' or ']' in array");

    CHECK_THROWS(json::parse("[").unwrap(), bstd::error::BadResultAccess);
}

static void testDuplicateKeys(){
    const std::string text = R"({"a":1,"b":2,"a":3})";
    const json::Value last = ok(text);
    CHECK_EQ(last.size(), 2u);
    CHECK_EQ(last["a"].asInt(), 3);
    CHECK_EQ(last.asObject().begin()->first, "a");  // keeps the first position

    json::ParseOptions opts;
    opts.duplicateKeys = json::DuplicateKeys::firstWins;
    CHECK_EQ(ok(text, opts)["a"].asInt(), 1);

    opts.duplicateKeys = json::DuplicateKeys::reject;
    auto r = json::parse(text, opts);
    CHECK(r.isErr());
    CHECK(r.unwrapErr().kind() == Kind::DuplicateKey);
    CHECK_EQ(r.unwrapErr().offset(), 13u);  // the second "a"

    // more than 16 keys takes the sorting path
    std::string many = "{";
    for(int i = 0; i < 40; i++){
        many += "\"k" + std::to_string(i) + "\":" + std::to_string(i) + ",";
    }
    many += R"("k5":"again","k39":"x","k5":"final"})";
    const json::Value m = ok(many);
    CHECK_EQ(m.size(), 40u);
    CHECK_EQ(m["k5"].asString(), "final");
    CHECK_EQ(m["k39"].asString(), "x");
    CHECK_EQ(std::next(m.asObject().begin(), 5)->first, "k5");
    opts.duplicateKeys = json::DuplicateKeys::reject;
    bad(many, Kind::DuplicateKey, opts);

    // nested objects don't confuse each other's key offsets
    opts.duplicateKeys = json::DuplicateKeys::reject;
    auto nested = json::parse(R"({"x":{"a":1,"b":2},"y":{"a":1,"a":2}})", opts);
    CHECK(nested.isErr());
    CHECK_EQ(nested.unwrapErr().offset(), 30u);
}

/* ---------- printing ---------- */

static void testDump(){
    const json::Value v = json::Object{{"a", json::Array{1, 2.5, "x"}}, {"b", nullptr}, {"c", json::Object{}}, {"d", true}};
    CHECK_EQ(v.dump(), R"({"a":[1,2.5,"x"],"b":null,"c":{},"d":true})");
    CHECK_EQ(v.dump(2),
        "{\n"
        "  \"a\": [\n"
        "    1,\n"
        "    2.5,\n"
        "    \"x\"\n"
        "  ],\n"
        "  \"b\": null,\n"
        "  \"c\": {},\n"
        "  \"d\": true\n"
        "}");
    json::DumpOptions tabs;
    tabs.indent = 1;
    tabs.indentChar = '\t';
    CHECK_EQ(json::dump(json::Array{1}, tabs), "[\n\t1\n]");

    CHECK_EQ(json::Value(0.1).dump(), "0.1");
    CHECK_EQ(json::Value(3.0).dump(), "3.0");
    CHECK_EQ(json::Value(-0.0).dump(), "-0.0");
    CHECK_EQ(json::Value(1e20).dump(), "1e+20");
    CHECK_EQ(json::Value(std::nan("")).dump(), "null");
    CHECK_EQ(json::Value(INFINITY).dump(), "null");
    CHECK_EQ(json::Value(INT64_MIN).dump(), "-9223372036854775808");
    CHECK(ok("3.0").isDouble());

    CHECK_EQ(json::Value(std::string("q\"b\\s/\n\t\x01\x1f")).dump(), R"("q\"b\\s/\n\t\u0001\u001f")");
    CHECK_EQ(json::Value("caf\xC3\xA9").dump(), "\"caf\xC3\xA9\"");

    json::DumpOptions ascii;
    ascii.ensureAscii = true;
    CHECK_EQ(json::dump(json::Value("caf\xC3\xA9 \xF0\x9F\x98\x80"), ascii), R"("caf\u00e9 \ud83d\ude00")");
    CHECK_EQ(json::dump(json::Value("a\xFF" "b"), ascii), R"("a\ufffdb")");

    std::ostringstream os;
    os << json::Value(json::Array{1, "two"});
    CHECK_EQ(os.str(), R"([1,"two"])");
}

static void testRoundTrip(){
    const std::string text = R"({"name":"bstd","version":1,"ratio":0.30000000000000004,"tags":["a","b\n",""],)"
                             R"("nested":{"deep":[[[]],{}],"u":"\u00e9\ud83d\ude00"},"neg":-17,"big":1.5e300,"t":true,"f":false,"n":null})";
    const json::Value v = ok(text);
    CHECK_EQ(v.dump(), ok(v.dump()).dump());
    CHECK(ok(v.dump()) == v);
    CHECK(ok(v.dump(4)) == v);
    json::DumpOptions ascii;
    ascii.ensureAscii = true;
    CHECK(ok(json::dump(v, ascii)) == v);

    // every finite double prints as text that reads back bit for bit
    std::mt19937_64 rng(12345);
    int mismatches = 0;
    for(int i = 0; i < 20000; i++){
        std::uint64_t bits = rng();
        double d;
        std::memcpy(&d, &bits, sizeof d);
        if(!std::isfinite(d)){
            continue;
        }
        const std::string s = json::Value(d).dump();
        auto back = json::parse(s);
        const double got = back.isOk() && back.unwrap().isDouble() ? back.unwrap().asDouble() : std::nan("");
        if(std::memcmp(&got, &d, sizeof d) != 0){
            if(++mismatches < 5){
                std::cerr << "double round trip failed: " << s << "\n";
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

/* ---------- Value API ---------- */

static void testValueApi(){
    json::Value v = json::Object{{"name", "bob"}, {"age", 30}, {"pets", json::Array{"cat"}}};

    const json::Value& cv = v;
    CHECK(cv["missing"].isNull());
    CHECK(cv["name"]["deeper"].isNull());
    CHECK(cv["pets"][7].isNull());
    CHECK_EQ(v.size(), 3u);  // const [] didn't add anything

    v["city"] = "Paris";
    CHECK_EQ(v.size(), 4u);
    v["pets"].pushBack("dog");
    CHECK_EQ(v["pets"].size(), 2u);
    CHECK_EQ(v["pets"][1].asString(), "dog");

    json::Value built;
    built["a"]["b"] = 1;            // null -> object, twice
    built["list"].pushBack(1);      // null -> array
    CHECK_EQ(built.dump(), R"({"a":{"b":1},"list":[1]})");

    CHECK_THROWS(v.at("nope"), std::out_of_range);
    CHECK_THROWS(v["pets"].at(5), std::out_of_range);
    CHECK_THROWS(v["pets"][5], std::out_of_range);
    CHECK_THROWS(v["age"].asString(), json::TypeError);
    CHECK_THROWS(v["name"]["x"], json::TypeError);
    CHECK_THROWS(v["name"].pushBack(1), json::TypeError);
    CHECK_THROWS(cv.at(0), json::TypeError);

    CHECK(v.find("name") != nullptr);
    CHECK(v.find("nope") == nullptr);
    CHECK(v.contains("age"));
    CHECK(json::Value(5).find("x") == nullptr);

    CHECK_EQ(v["age"].get<int>().value_or(-1), 30);
    CHECK_EQ(v["age"].get<double>().value_or(-1), 30.0);
    CHECK(!v["age"].get<std::string>());
    CHECK(!json::Value(300).get<std::int8_t>());
    CHECK(!json::Value(-1).get<unsigned>());
    CHECK_EQ(json::Value(3.0).get<int>().value_or(-1), 3);
    CHECK(!json::Value(3.5).get<int>());
    CHECK(!json::Value(1e19).get<std::int64_t>());
    CHECK_EQ(json::Value(1e19).get<std::uint64_t>().value_or(0), 10000000000000000000ull);
    CHECK(!json::Value(1e39).get<float>());
    CHECK_EQ(v["name"].getOr<std::string>("anon"), "bob");
    CHECK_EQ(v["nope"].getOr<std::string>("anon"), "anon");
    CHECK_EQ(v["nope"].getOr(5), 5);

    CHECK(json::Value(UINT64_MAX).isDouble());
    CHECK(json::Value(std::uint64_t{5}).isInt());
    CHECK(json::Value(static_cast<const char*>(nullptr)).isNull());
    CHECK(json::Value(std::string_view("sv")).isString());
    CHECK(json::Value(1.5f).isDouble());

    CHECK(json::Value(1) == json::Value(1.0));
    CHECK(json::Value(1.0) == json::Value(1));
    CHECK(json::Value(1) != json::Value(1.5));
    CHECK(json::Value(INT64_MAX) != json::Value(9223372036854775808.0));
    CHECK(json::Value("1") != json::Value(1));
    CHECK(json::Value() == json::Value(nullptr));
    CHECK(ok(R"({"a":1,"b":[2]})") == ok(R"({"b":[2],"a":1.0})"));
    CHECK(ok(R"({"a":1})") != ok(R"({"a":1,"b":2})"));
    CHECK(ok("[1,2]") != ok("[2,1]"));

    json::Object o{{"x", 1}, {"y", 2}, {"x", 3}};
    CHECK_EQ(o.size(), 2u);
    CHECK_EQ(o.at("x").asInt(), 3);
    o.set("x", "replaced");
    CHECK_EQ(o.begin()->first, "x");
    CHECK(o.erase("x"));
    CHECK(!o.erase("x"));
    CHECK_EQ(o.begin()->first, "y");
}

static void testPointer(){
    const json::Value v = ok(R"({"a":{"b":[10,20,{"c":"deep"}]},"a/b":1,"m~n":2,"":3})");
    CHECK(v.pointer("") == &v);
    CHECK_EQ(v.pointer("/a/b/1")->asInt(), 20);
    CHECK_EQ(v.pointer("/a/b/2/c")->asString(), "deep");
    CHECK_EQ(v.pointer("/a~1b")->asInt(), 1);
    CHECK_EQ(v.pointer("/m~0n")->asInt(), 2);
    CHECK_EQ(v.pointer("/")->asInt(), 3);
    CHECK(v.pointer("/missing") == nullptr);
    CHECK(v.pointer("/a/b/3") == nullptr);
    CHECK(v.pointer("/a/b/01") == nullptr);
    CHECK(v.pointer("/a/b/-") == nullptr);
    CHECK(v.pointer("/a/b/1/x") == nullptr);
    CHECK(v.pointer("/a~2") == nullptr);
    CHECK(v.pointer("a") == nullptr);

    json::Value m = v;
    *m.pointer("/a/b/0") = "changed";
    CHECK_EQ(m["a"]["b"][0].asString(), "changed");
}

/* ---------- typed conversions ---------- */

namespace app{

struct Point{
    int x{};
    int y{};
};
BSTD_JSON_FIELDS(Point, x, y);

struct Shape{
    std::string name;
    std::vector<Point> points;
    std::optional<std::string> color;
    std::map<std::string, double> weights;
    std::array<int, 2> size{};
    bool closed{};
    json::Value extra;
};
BSTD_JSON_FIELDS(Shape, name, points, color, weights, size, closed, extra);

class Secret{
    std::string _id;
    unsigned _level{};

public:
    Secret() = default;
    Secret(std::string id, unsigned level) : _id(std::move(id)), _level(level) {}
    const std::string& id() const { return _id; }
    unsigned level() const { return _level; }
    BSTD_JSON_FIELDS_IN(Secret, _id, _level);
};

/* Hand written hooks: stored as "r,g,b". */
struct Rgb{
    int r{}, g{}, b{};
};
inline json::Value toJson(const Rgb& c){
    return std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
}
inline json::Result<void> fromJson(const json::Value& j, Rgb& out){
    const auto* s = j.getIf<std::string>();
    if(!s || std::sscanf(s->c_str(), "%d,%d,%d", &out.r, &out.g, &out.b) != 3){
        return json::Err("expected \"r,g,b\"");
    }
    return json::Ok();
}

}  // namespace app

static void testTyped(){
    app::Shape s;
    s.name = "tri";
    s.points = {{0, 0}, {1, 0}, {0, 1}};
    s.weights = {{"a", 0.5}};
    s.size = {3, 4};
    s.closed = true;
    s.extra = json::Array{1, "x"};

    const json::Value j = json::toValue(s);
    CHECK_EQ(j.dump(), R"({"name":"tri","points":[{"x":0,"y":0},{"x":1,"y":0},{"x":0,"y":1}],"weights":{"a":0.5},"size":[3,4],"closed":true,"extra":[1,"x"]})");
    CHECK(!j.contains("color"));  // empty optional left out

    auto back = json::fromValue<app::Shape>(j);
    CHECK(back.isOk());
    const app::Shape& b = back.unwrap();
    CHECK_EQ(b.name, "tri");
    CHECK_EQ(b.points.size(), 3u);
    CHECK_EQ(b.points[2].y, 1);
    CHECK(!b.color);
    CHECK_EQ(b.weights.at("a"), 0.5);
    CHECK_EQ(b.size[1], 4);
    CHECK(b.closed);
    CHECK(b.extra == s.extra);

    auto parsed = json::parseAs<app::Shape>(
        R"({"name":"sq","points":[],"color":"red","weights":{},"size":[1,1],"closed":false,"extra":null,"unknown":1})");
    CHECK(parsed.isOk());
    CHECK_EQ(parsed.unwrap().color.value_or(""), "red");

    auto wrong = json::parseAs<app::Shape>(
        R"({"name":"x","points":[{"x":1,"y":2},{"x":"one","y":2}],"weights":{},"size":[1,1],"closed":false,"extra":1})");
    CHECK(wrong.isErr());
    CHECK(wrong.unwrapErr().kind() == Kind::WrongType);
    CHECK_EQ(wrong.unwrapErr().path(), "/points/1/x");
    CHECK_EQ(wrong.unwrapErr().message(), "at /points/1/x: expected integer, got string");

    auto missing = json::parseAs<app::Shape>(R"({"name":"x","points":[{"x":1}]})");
    CHECK(missing.isErr());
    CHECK(missing.unwrapErr().kind() == Kind::MissingField);
    CHECK_EQ(missing.unwrapErr().message(), "at /points/0: missing field \"y\"");

    auto size = json::parseAs<app::Shape>(R"({"name":"x","points":[],"weights":{},"size":[1],"closed":true,"extra":1})");
    CHECK(size.isErr() && size.unwrapErr().kind() == Kind::OutOfRange);

    auto weight = json::parseAs<app::Shape>(R"({"name":"x","points":[],"weights":{"a/b":"heavy"},"size":[1,2],"closed":true,"extra":1})");
    CHECK(weight.isErr());
    CHECK_EQ(weight.unwrapErr().path(), "/weights/a~1b");

    CHECK(json::parseAs<app::Point>("[1,2]").unwrapErr().kind() == Kind::WrongType);
    CHECK(json::parseAs<app::Point>("{").unwrapErr().kind() == Kind::Syntax);

    const app::Secret secret("abc", 7);
    const json::Value sj = json::toValue(secret);
    CHECK_EQ(sj.dump(), R"({"_id":"abc","_level":7})");
    auto secretBack = json::fromValue<app::Secret>(sj);
    CHECK(secretBack.isOk() && secretBack.unwrap().level() == 7u);
    CHECK(json::parseAs<app::Secret>(R"({"_id":"a","_level":-1})").unwrapErr().kind() == Kind::OutOfRange);

    CHECK_EQ(json::toValue(app::Rgb{1, 2, 3}).asString(), "1,2,3");
    auto rgbs = json::parseAs<std::vector<app::Rgb>>(R"(["1,2,3","nope"])");
    CHECK(rgbs.isErr());
    CHECK(rgbs.unwrapErr().kind() == Kind::Custom);
    CHECK_EQ(rgbs.unwrapErr().message(), "at /1: expected \"r,g,b\"");

    CHECK_EQ(json::toValue(std::vector<bool>{true, false}).dump(), "[true,false]");
    CHECK_EQ(json::toValue(std::optional<int>{}).dump(), "null");
    CHECK_EQ(json::toValue("literal").asString(), "literal");
    auto opt = json::parseAs<std::vector<std::optional<int>>>("[1,null,3]");
    CHECK(opt.isOk() && !opt.unwrap()[1] && *opt.unwrap()[2] == 3);
    auto um = json::parseAs<std::unordered_map<std::string, std::vector<int>>>(R"({"a":[1,2],"b":[]})");
    CHECK(um.isOk() && um.unwrap().at("a").size() == 2u);
}

int main(){
    testScalars();
    testStrings();
    testStructure();
    testDepth();
    testErrorLocation();
    testDuplicateKeys();
    testDump();
    testRoundTrip();
    testValueApi();
    testPointer();
    testTyped();
    return finishTests();
}
