#include "headers.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

// Сравнение без учёта регистра
constexpr bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

void iterHeaders(std::string_view req, Callback &&callback) {
    // Находим конец заголовков
    auto end_headers = req.find("\r\n\r\n"sv);
    if (end_headers == std::string_view::npos)
        return;

    // Пропускаем стартовую строку
    auto start_headers = req.find("\r\n"sv);
    if (start_headers == std::string_view::npos)
        return;

    auto header_block = req.substr(start_headers + 2, end_headers - (start_headers + 2));

    // Делим на строки по "\r\n"
    auto lines = header_block | std::views::split("\r\n"sv);
    for (auto &&line_view : lines) {
        std::string_view line(line_view.begin(), line_view.end());
        if (line.empty())
            continue;

        auto colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;

        std::string_view name = line.substr(0, colon);
        std::string_view value_raw = line.substr(colon + 1);

        // Обрезаем пробельные символы в начале и конце значения
        auto first = value_raw.find_first_not_of(" \t"sv);
        if (first == std::string_view::npos) {
            callback(name, ""sv);
        } else {
            auto last = value_raw.find_last_not_of(" \t"sv);
            std::string_view value = value_raw.substr(first, last - first + 1);
            callback(name, value);
        }
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::string_view host_with_port;

    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        if (iequals(name, "host"sv)) {
            host_with_port = value;
        }
    });

    if (host_with_port.empty()) {
        return {"", "80"};
    }

    // Находим позицию первого ':' (порт начинается после него)
    size_t colon_pos = host_with_port.find(':');

    if (colon_pos == std::string_view::npos) {
        // Нет порта — возвращаем весь хост и "80"
        return {std::string(host_with_port), "80"};
    }

    std::string_view host_part = host_with_port.substr(0, colon_pos);
    std::string_view port_part = host_with_port.substr(colon_pos + 1);

    // Случай: "Host: example.com:" → порт пустой → используем "80"
    if (port_part.empty()) {
        return {std::string(host_part), "80"};
    }

    return {std::string(host_part), std::string(port_part)};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;

    iterHeaders(rsp, [&](std::string_view name, std::string_view value) {
        if (iequals(name, "content-length"sv)) {
            size_t len = 0;
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), len);
            if (ec == std::errc{}) {
                result = len;
            }
        }
    });

    return result;
}
