#include <iostream>
#include <thread>
#include <deque>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class ChatClient {
public:
    ChatClient(boost::asio::io_context& io_context, const tcp::resolver::results_type& endpoints)
        : io_context_(io_context), socket_(io_context) {
        do_connect(endpoints);
    }

    void write(const std::string& msg) {
        // boost::asio::post гарантирует потокобезопасную передачу задачи из потока main в сетевой поток!
        boost::asio::post(io_context_,
            [this, msg]() {
                bool write_in_progress = !write_msgs_.empty();
                write_msgs_.push_back(msg);
                if (!write_in_progress) {
                    do_write();
                }
            });
    }

    void close() {
        boost::asio::post(io_context_, [this]() { socket_.close(); });
    }

private:
    void do_connect(const tcp::resolver::results_type& endpoints) {
        boost::asio::async_connect(socket_, endpoints,
            [this](boost::system::error_code ec, tcp::endpoint) {
                if (!ec) {
                    std::cout << "[Connected to server! Type messages and press Enter]\n";
                    do_read();
                }
            });
    }

    void do_read() {
        boost::asio::async_read_until(socket_, buffer_, '\n',
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::istream is(&buffer_);
                    std::string line;
                    std::getline(is, line);
                    std::cout << ">> " << line << "\n";
                    do_read();
                } else {
                    std::cout << "[Disconnected from server]\n";
                    socket_.close();
                }
            });
    }

    void do_write() {
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_msgs_.pop_front();
                    if (!write_msgs_.empty()) {
                        do_write();
                    }
                } else {
                    socket_.close();
                }
            });
    }

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    boost::asio::streambuf buffer_;
    std::deque<std::string> write_msgs_;
};

int main(int argc, char* argv[]) {
    try {
        std::string host = "127.0.0.1";
        std::string port = "8080";

        std::cout << "Enter your nickname: ";
        std::string nickname;
        std::getline(std::cin, nickname);

        boost::asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(host, port);

        ChatClient client(io_context, endpoints);

        // Запускаем сетевой движок в отдельном потоке
        std::thread net_thread([&io_context]() { io_context.run(); });

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/quit") break;
            std::string full_msg = "[" + nickname + "]: " + line + "\n";
            client.write(full_msg);
        }

        client.close();
        net_thread.join(); // Дожидаемся завершения сетевого потока перед выходом
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}