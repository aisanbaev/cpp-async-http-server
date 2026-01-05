#include "headers.h"
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

TEST(iterHeaders, Empty) {
    std::string_view req = "";
    bool called = false;
    iterHeaders(req, [&](std::string_view, std::string_view) { called = true; });
    EXPECT_FALSE(called);
}

TEST(iterHeaders, SkipRequestLine) {
    // Проверяем, что стартовая строка игнорируется
    std::string_view req = "GET /index.html HTTP/1.1\r\n"
                           "Host: example.com\r\n"
                           "\r\n";
    std::vector<std::string> headers;
    iterHeaders(req, [&](std::string_view name, std::string_view) { headers.emplace_back(name); });
    ASSERT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0], "Host");
}

TEST(iterHeaders, SingleHeader) {
    std::string_view req = "GET / HTTP/1.1\r\n"
                           "User-Agent: TestClient\r\n"
                           "\r\n";
    std::string name, value;
    iterHeaders(req, [&](std::string_view n, std::string_view v) {
        name = std::string(n);
        value = std::string(v);
    });
    EXPECT_EQ(name, "User-Agent");
    EXPECT_EQ(value, "TestClient");
}

TEST(iterHeaders, MultipleHeaders) {
    std::string_view req = "POST /api HTTP/1.1\r\n"
                           "Host: api.example.com\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: 123\r\n"
                           "\r\n";
    std::vector<std::pair<std::string, std::string>> collected;
    iterHeaders(
        req, [&](std::string_view n, std::string_view v) { collected.emplace_back(std::string(n), std::string(v)); });
    ASSERT_EQ(collected.size(), 3);
    EXPECT_EQ(collected[0].first, "Host");
    EXPECT_EQ(collected[0].second, "api.example.com");
    EXPECT_EQ(collected[1].first, "Content-Type");
    EXPECT_EQ(collected[1].second, "application/json");
    EXPECT_EQ(collected[2].first, "Content-Length");
    EXPECT_EQ(collected[2].second, "123");
}

TEST(iterHeaders, MultipleSameHeaders) {
    // HTTP позволяет дублирующиеся заголовки (например, Set-Cookie)
    std::string_view req = "GET / HTTP/1.1\r\n"
                           "X-Tag: alpha\r\n"
                           "X-Tag: beta\r\n"
                           "\r\n";
    std::vector<std::string> values;
    iterHeaders(req, [&](std::string_view name, std::string_view v) {
        if (name == "X-Tag") {
            values.emplace_back(v);
        }
    });
    ASSERT_EQ(values.size(), 2);
    EXPECT_EQ(values[0], "alpha");
    EXPECT_EQ(values[1], "beta");
}

TEST(findHostPort, Simple) {
    std::string_view req = "GET / HTTP/1.1\r\n"
                           "Host: example.com:8080\r\n"
                           "\r\n";
    auto [host, port] = findHostPort(req);
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "8080");
}

TEST(findHostPort, NoHost) {
    std::string_view req = "GET / HTTP/1.1\r\n"
                           "User-Agent: test\r\n"
                           "\r\n";
    auto [host, port] = findHostPort(req);
    EXPECT_TRUE(host.empty());
    EXPECT_EQ(port, "80");
}

TEST(findContentLength, Simple) {
    std::string_view rsp = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 42\r\n"
                           "\r\n";
    auto cl = findContentLength(rsp);
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(*cl, 42u);
}

TEST(findContentLength, NoContentLength) {
    std::string_view rsp = "HTTP/1.1 200 OK\r\n"
                           "Date: Mon, 01 Jan 2024 00:00:00 GMT\r\n"
                           "\r\n";
    auto cl = findContentLength(rsp);
    EXPECT_FALSE(cl.has_value());
}

TEST(iterHeaders, HeaderValueWithSpaces) {
    std::string_view req = "GET / HTTP/1.1\r\n"
                           "X-Debug:   value with spaces   \r\n"
                           "\r\n";
    std::string value;
    iterHeaders(req, [&](std::string_view name, std::string_view v) {
        if (name == "X-Debug")
            value = std::string(v);
    });
    EXPECT_EQ(value, "value with spaces");
}

TEST(iterHeaders, EmptyHeadersBlock) {
    std::string_view req = "GET / HTTP/1.1\r\n\r\n";
    int count = 0;
    iterHeaders(req, [&](std::string_view, std::string_view) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(findHostPort, HostWithoutPort) {
    std::string_view req = "GET / HTTP/1.1\r\n"
                           "Host: localhost\r\n"
                           "\r\n";
    auto [host, port] = findHostPort(req);
    EXPECT_EQ(host, "localhost");
    EXPECT_EQ(port, "80");
}

TEST(findContentLength, InvalidContentLength) {
    std::string_view rsp = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: not-a-number\r\n"
                           "\r\n";
    auto cl = findContentLength(rsp);
    EXPECT_FALSE(cl.has_value());
}

TEST(findContentLength, ContentLengthZero) {
    std::string_view rsp = "HTTP/1.1 204 No Content\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";
    auto cl = findContentLength(rsp);
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(*cl, 0u);
}
