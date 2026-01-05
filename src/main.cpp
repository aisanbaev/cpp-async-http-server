#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <string_view>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::io_service;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

namespace {
constexpr std::string_view delimiter = "\r\n\r\n";
constexpr size_t kBufferSize = 8192;
}  // namespace

awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    try {
        // 1. Читаем до конца заголовков (включая \r\n\r\n) в строку
        std::string request;
        co_await async_read_until(client_socket, boost::asio::dynamic_buffer(request), delimiter, use_awaitable);
        std::string_view req_view(request);

        // 2. Извлекаем Host
        auto [host, port] = findHostPort(req_view);
        if (host.empty()) {
            std::cerr << "No Host header found\n";
            co_return;
        }

        // 3. Подключаемся к целевому серверу
        tcp::socket server_socket(io_service);
        tcp::resolver resolver(io_service);
        auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);
        co_await boost::asio::async_connect(server_socket, endpoints, use_awaitable);

        // 4. Отправляем всё, что прочитали (заголовки + возможную часть тела)
        co_await boost::asio::async_write(server_socket, boost::asio::buffer(request), use_awaitable);

        // 5. Проверяем, есть ли Content-Length в запросе
        std::optional<size_t> req_content_length = findContentLength(req_view);
        if (req_content_length.has_value()) {
            size_t expected_body_size = req_content_length.value();
            size_t already_read = 0;

            // После \r\n\r\n может идти тело — проверим, сколько уже в request
            size_t header_end = req_view.find(delimiter);
            if (header_end != std::string_view::npos) {
                already_read = req_view.size() - (header_end + 4);  // 4 = длина \r\n\r\n
            }

            if (already_read < expected_body_size) {
                size_t remaining = expected_body_size - already_read;
                std::string body_suffix;
                body_suffix.resize(remaining);
                co_await boost::asio::async_read(client_socket, boost::asio::buffer(body_suffix), use_awaitable);
                co_await boost::asio::async_write(server_socket, boost::asio::buffer(body_suffix), use_awaitable);
            }
            // Если already_read >= expected — тело уже отправлено
        }

        // 6. Теперь читаем ответ от сервера и передаём всё клиенту
        std::array<char, kBufferSize> buffer;
        while (true) {
            auto [ec, n] = co_await server_socket.async_read_some(boost::asio::buffer(buffer),
                                                                  boost::asio::as_tuple(boost::asio::use_awaitable));
            if (ec) {
                if (ec == boost::asio::error::eof) {
                    // Сервер закрыл соединение — нормально
                } else {
                    std::cerr << "Server read error: " << ec.message() << "\n";
                }
                break;
            }
            if (n == 0)
                break;
            co_await boost::asio::async_write(client_socket, boost::asio::buffer(buffer, n), use_awaitable);
        }

    } catch (const std::exception &e) {
        std::cerr << "Session error: " << e.what() << "\n";
    }
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)), socket_(io_service) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(socket_, [this](error_code ec) {
            if (!ec) {
                std::cout << "Accepted connection from: " << socket_.remote_endpoint() << std::endl;
                // Запускаем новую сопрограмму для обработки сессии
                co_spawn(io_service_, session(std::move(socket_), io_service_), boost::asio::detached);

                // Готовим сокет для следующего соединения
                socket_ = tcp::socket(io_service_);
            } else {
                std::cerr << "Accept error: " << ec.message() << std::endl;
            }
            // Продолжаем принимать новые соединения
            do_accept();
        });
    }

    io_service &io_service_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }
        io_service io_service(1);
        Server server(io_service, std::atoi(argv[1]));
        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
