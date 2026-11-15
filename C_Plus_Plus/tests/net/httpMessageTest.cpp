// Tests for net/http/message.hpp. Run with tests/run.sh, or build this file on its own.
#include "../../net/http/message.hpp"
#include "../check.hpp"

#include <string>

namespace net = bstd::net;
namespace http = bstd::net::http;
namespace json = bstd::net::json;
using http::Headers;
using http::Method;
using http::Request;
using http::Response;
using http::Status;

static std::string value(const Headers& h, std::string_view name){
    return std::string(h.get(name).value_or("<missing>"));
}

static void testMethodsAndStatus(){
    CHECK_EQ(http::methodName(Method::Delete), "DELETE");
    CHECK(http::parseMethod("DELETE") == Method::Delete);
    CHECK(http::parseMethod("OPTIONS") == Method::Options);
    CHECK(!http::parseMethod("get"));
    CHECK(!http::parseMethod("PROPFIND"));
    CHECK(!http::parseMethod(""));
    CHECK_EQ(http::versionName(http::Version::Http10), "HTTP/1.0");

    CHECK_EQ(http::reasonPhrase(Status::NotFound), "Not Found");
    CHECK_EQ(http::reasonPhrase(Status::ContentTooLarge), "Content Too Large");
    CHECK_EQ(http::reasonPhrase(static_cast<Status>(299)), "");
    CHECK_EQ(http::code(Status::TooManyRequests), 429);
    CHECK(http::isSuccess(Status::NoContent));
    CHECK(http::isRedirect(Status::PermanentRedirect));
    CHECK(http::isClientError(Status::Gone));
    CHECK(http::isServerError(Status::BadGateway));
    CHECK(http::isInformational(Status::Continue));
    CHECK(!http::isSuccess(Status::MultipleChoices));
    CHECK(!http::allowsBody(Status::NoContent));
    CHECK(!http::allowsBody(Status::NotModified));
    CHECK(!http::allowsBody(Status::SwitchingProtocols));
    CHECK(http::allowsBody(Status::NotFound));
}

static void testHeaders(){
    Headers h;
    h.add("Content-Type", "  text/html \t");
    CHECK_EQ(value(h, "content-type"), "text/html");
    CHECK_EQ(value(h, "CONTENT-TYPE"), "text/html");
    CHECK_EQ(h.begin()->first, "Content-Type");  // keeps the case it was given

    h.add("Set-Cookie", "a=1");
    h.add("set-cookie", "b=2");
    CHECK_EQ(h.getAll("Set-Cookie").size(), 2u);
    h.set("SET-COOKIE", "c=3");
    CHECK_EQ(h.getAll("set-cookie").size(), 1u);
    CHECK_EQ(value(h, "Set-Cookie"), "c=3");
    CHECK_EQ(std::next(h.begin())->first, "Set-Cookie");  // the first one's place and spelling
    CHECK(h.has("set-cookie"));
    CHECK_EQ(h.remove("set-cookie"), 1u);
    CHECK(!h.has("Set-Cookie"));
    CHECK_EQ(value(h, "Missing"), "<missing>");

    h.add("Accept", "text/html");
    h.add("Accept", "application/json");
    CHECK_EQ(h.getJoined("accept"), "text/html, application/json");
    CHECK_EQ(h.getJoined("nothing"), "");

    h.add("Connection", "keep-alive, Upgrade");
    h.add("Transfer-Encoding", "gzip;q=1 , chunked");
    CHECK(h.hasToken("connection", "Keep-Alive"));
    CHECK(h.hasToken("Connection", "upgrade"));
    CHECK(!h.hasToken("Connection", "close"));
    CHECK(h.hasToken("Transfer-Encoding", "chunked"));
    CHECK(h.hasToken("Transfer-Encoding", "gzip"));
    CHECK(!h.hasToken("Transfer-Encoding", "chunk"));
    CHECK(!h.hasToken("Missing", "x"));

    CHECK_THROWS(h.add("Bad Name", "x"), std::invalid_argument);
    CHECK_THROWS(h.add("", "x"), std::invalid_argument);
    CHECK_THROWS(h.add("a:b", "x"), std::invalid_argument);
    CHECK_THROWS(h.add("X-Test\r\n", "x"), std::invalid_argument);
    CHECK_THROWS(h.add("X-Test", "a\r\nInjected: yes"), std::invalid_argument);
    CHECK_THROWS(h.set("X-Test", "a\nb"), std::invalid_argument);
    CHECK_THROWS(h.add("X-Test", std::string("a\0b", 3)), std::invalid_argument);
    CHECK_THROWS(h.add("X-Test", "a\x7F"), std::invalid_argument);
    h.add("X-Tab", "a\tb");
    h.add("X-Utf8", "caf\xC3\xA9");
    h.add("X-Empty", "");
    h.add("!#$%&'*+-.^_`|~09azAZ", "token chars");
    CHECK_EQ(value(h, "x-tab"), "a\tb");
    CHECK_EQ(value(h, "X-Empty"), "");

    Headers len{{"Content-Length", "42"}};
    CHECK(len.contentLength() == 42u);
    len.set("Content-Length", "4 2");
    CHECK(!len.contentLength());
    len.set("Content-Length", "-1");
    CHECK(!len.contentLength());
    len.set("Content-Length", "99999999999999999999");
    CHECK(!len.contentLength());
    len.set("Content-Length", "18446744073709551615");
    CHECK(!len.contentLength());  // 20 digits, over the 19 digit limit
    len.set("Content-Length", "9999999999999999999");
    CHECK(len.contentLength() == 9999999999999999999ull);
    CHECK(!Headers{}.contentLength());

    Headers type{{"Content-Type", "Application/JSON; charset=utf-8"}};
    CHECK_EQ(type.mediaType(), "application/json");
    CHECK_EQ(Headers{}.mediaType(), "");
    CHECK_THROWS((Headers{{"Bad Name", "x"}}), std::invalid_argument);
}

static void testRequest(){
    const net::Url url = net::Url::parse("http://api.example.com:8080/users?page=2#ignored").unwrap();
    Request post = Request::forUrl(Method::Post, url);
    CHECK_EQ(post.target, "/users?page=2");
    CHECK_EQ(post.path(), "/users");
    CHECK_EQ(post.rawQuery(), "page=2");
    CHECK_EQ(post.query().get("page").value_or("?"), "2");
    post.setJson(json::Object{{"name", "bob"}});
    CHECK_EQ(post.serialize(),
        "POST /users?page=2 HTTP/1.1\r\n"
        "Host: api.example.com:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 14\r\n"
        "\r\n"
        "{\"name\":\"bob\"}");
    CHECK(post.parseJson().unwrap() == (json::Object{{"name", "bob"}}));

    const Request get = Request::forUrl(Method::Get, net::Url::parse("https://[::1]/").unwrap());
    CHECK_EQ(get.serialize(), "GET / HTTP/1.1\r\nHost: [::1]\r\n\r\n");

    Request emptyPost(Method::Put, "/x");
    CHECK_EQ(emptyPost.serialize(), "PUT /x HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    emptyPost.version = http::Version::Http10;
    CHECK_EQ(emptyPost.serialize(), "PUT /x HTTP/1.0\r\nContent-Length: 0\r\n\r\n");

    Request deleteWithBody(Method::Delete, "/item/1");
    deleteWithBody.setText("why");
    CHECK_EQ(deleteWithBody.serialize(),
        "DELETE /item/1 HTTP/1.1\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\nwhy");

    Request chunked(Method::Post, "/upload");
    chunked.headers.set("Transfer-Encoding", "chunked");
    chunked.body = "hello world!";
    CHECK_EQ(chunked.serialize(), "POST /upload HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nc\r\nhello world!\r\n0\r\n\r\n");
    chunked.body = std::string(255, 'x');
    CHECK(chunked.serialize().find("\r\n\r\nff\r\n") != std::string::npos);
    chunked.body.clear();
    CHECK_EQ(chunked.serialize(), "POST /upload HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");

    Request form(Method::Post, "/login");
    form.setForm(net::Query{{"name", "a b"}, {"x", "1&2"}});
    CHECK_EQ(form.body, "name=a%20b&x=1%262");
    CHECK_EQ(value(form.headers, "content-type"), "application/x-www-form-urlencoded");

    Request explicitLength(Method::Post, "/");
    explicitLength.body = "abc";
    explicitLength.headers.set("Content-Length", "3");
    CHECK_EQ(explicitLength.serialize(), "POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    explicitLength.headers.set("Content-Length", "4");
    CHECK_THROWS(explicitLength.serialize(), std::invalid_argument);

    CHECK_THROWS(Request(Method::Get, "").serialize(), std::invalid_argument);
    CHECK_THROWS(Request(Method::Get, "/a b").serialize(), std::invalid_argument);
    CHECK_THROWS(Request(Method::Get, "/a\r\nX-Injected: 1").serialize(), std::invalid_argument);
    CHECK_THROWS(Request(Method::Get, "/caf\xC3\xA9").serialize(), std::invalid_argument);
    CHECK_EQ(Request(Method::Options, "*").serialize(), "OPTIONS * HTTP/1.1\r\n\r\n");

    Request alive;
    CHECK(alive.keepAlive());
    alive.headers.set("Connection", "Close");
    CHECK(!alive.keepAlive());
    alive.version = http::Version::Http10;
    alive.headers.remove("Connection");
    CHECK(!alive.keepAlive());
    alive.headers.set("Connection", "keep-alive");
    CHECK(alive.keepAlive());

    Request noQuery(Method::Get, "/plain");
    CHECK_EQ(noQuery.rawQuery(), "");
    CHECK(noQuery.query().empty());
}

static void testResponse(){
    CHECK_EQ(Response().serialize(), "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    const Response created = Response::makeJson(json::Object{{"id", 7}}, Status::Created);
    CHECK_EQ(created.serialize(), "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: 8\r\n\r\n{\"id\":7}");
    CHECK(created.ok());
    CHECK_EQ(created.parseJson().unwrap()["id"].asInt(), 7);

    const Response text = Response::makeText("hello");
    CHECK_EQ(text.serialize(), "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 5\r\n\r\nhello");
    CHECK_EQ(text.serializeHeaders(), "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 5\r\n\r\n");

    Response noContent(Status::NoContent);
    noContent.body = "dropped";
    CHECK_EQ(noContent.serialize(), "HTTP/1.1 204 No Content\r\n\r\n");
    CHECK_EQ(Response(Status::NotModified).serialize(), "HTTP/1.1 304 Not Modified\r\n\r\n");
    CHECK(!Response(Status::NotFound).ok());

    Response custom(static_cast<Status>(299));
    CHECK_EQ(custom.serialize(), "HTTP/1.1 299 \r\nContent-Length: 0\r\n\r\n");
    custom.reason = "Totally Fine";
    CHECK_EQ(custom.serializeHeaders(), "HTTP/1.1 299 Totally Fine\r\nContent-Length: 0\r\n\r\n");
    custom.reason = "Bad\r\nX: y";
    CHECK_THROWS(custom.serialize(), std::invalid_argument);
    CHECK_THROWS(Response(static_cast<Status>(42)).serialize(), std::invalid_argument);
    CHECK_THROWS(Response(static_cast<Status>(1000)).serialize(), std::invalid_argument);

    const Response redirect = Response::makeRedirect("/login?next=%2F");
    CHECK_EQ(redirect.serialize(), "HTTP/1.1 302 Found\r\nLocation: /login?next=%2F\r\nContent-Length: 0\r\n\r\n");
    CHECK_EQ(Response::makeRedirect("/x", Status::PermanentRedirect).serializeHeaders().substr(0, 32), "HTTP/1.1 308 Permanent Redirect\r");
    CHECK_THROWS(Response::makeRedirect("/x\r\nSet-Cookie: evil=1"), std::invalid_argument);

    Response chunked = Response::makeText("abc");
    chunked.headers.set("Transfer-Encoding", "chunked");
    CHECK_EQ(chunked.serialize(),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");

    Response wrongLength = Response::makeText("abc");
    wrongLength.headers.set("Content-Length", "10");
    CHECK_THROWS(wrongLength.serialize(), std::invalid_argument);
    CHECK(wrongLength.serializeHeaders().find("Content-Length: 10\r\n") != std::string::npos);  // HEAD answers may differ

    Response old;
    old.version = http::Version::Http10;
    CHECK(!old.keepAlive());
    CHECK_EQ(old.serialize().substr(0, 17), "HTTP/1.0 200 OK\r\n");
}

int main(){
    testMethodsAndStatus();
    testHeaders();
    testRequest();
    testResponse();
    return finishTests();
}
