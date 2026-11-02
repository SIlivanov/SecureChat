#include <iostream>
#include <thread>
#include <deque>
#include <vector>
#include <boost/asio.hpp>
#include "protocol.hpp"
#include "crypto.hpp"

using boost::asio::ip::tcp;

class ChatClient {
public:
    ChatClient(boost::asio::io_context& io_context, const tcp::resolver::results_type& endpoints)
        : io_context_(io_context), socket_(io_context) {
        do_connect(endpoints);
    }

    // Принимает обычную строку, шифрует, упаковывает и ставит в очередь на отправку
    void send_message(const std::string& plaintext) {
        try {
            std::vector<uint8_t> encrypted = crypto::encrypt(plaintext);
            std::vector<uint8_t> packet = protocol::pack_message(encrypted);

            boost::asio::post(io_context_,
                [this, packet = std::move(packet)]() mutable {
                    bool write_in_progress = !write_packets_.empty();
                    write_packets_.push_back(std::move(packet));
                    if (!write_in_progress) {
                        do_write();
                    }
                });
        } catch (const std::exception& e) {
            std::cerr << "[Crypto Error] Failed to encrypt message: " << e.what() << "\n";
        }
    }

    void close() {
        boost::asio::post(io_context_, [this]() { socket_.close(); });
    }

private:
    void do_connect(const tcp::resolver::results_type& endpoints) {
        boost::asio::async_connect(socket_, endpoints,
            [this](boost::system::error_code ec, tcp::endpoint) {
                if (!ec) {
                    std::cout << "[Connected! End-to-End AES-256 Encryption enabled]\n";
                    do_read_header();
                } else {
                    std::cerr << "[Connect failed]: " << ec.message() << "\n";
                }
            });
    }

    void do_read_header() {
        boost::asio::async_read(socket_,
            boost::asio::buffer(read_header_, protocol::HEADER_SIZE),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    try {
                        uint32_t body_len = protocol::unpack_header(read_header_);
                        read_body_.resize(body_len);
                        do_read_body();
                    } catch (const std::exception& e) {
                        std::cerr << "[Protocol error]: " << e.what() << "\n";
                        socket_.close();
                    }
                } else {
                    std::cout << "[Disconnected from server]\n";
                    socket_.close();
                }
            });
    }

    void do_read_body() {
        boost::asio::async_read(socket_,
            boost::asio::buffer(read_body_.data(), read_body_.size()),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    try {
                        std::string decrypted = crypto::decrypt(read_body_);
                        std::cout << ">> " << decrypted << "\n";
                    } catch (const std::exception& e) {
                        std::cerr << ">> [Failed to decrypt incoming message: " << e.what() << "]\n";
                    }
                    do_read_header();
                } else {
                    socket_.close();
                }
            });
    }

    void do_write() {
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_packets_.front().data(), write_packets_.front().size()),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_packets_.pop_front();
                    if (!write_packets_.empty()) {
                        do_write();
                    }
                } else {
                    socket_.close();
                }
            });
    }

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    uint8_t read_header_[protocol::HEADER_SIZE];
    std::vector<uint8_t> read_body_;
    std::deque<std::vector<uint8_t>> write_packets_;
};

int main(int argc, char* argv[]) {
    try {
        std::string host = "127.0.0.1";
        std::string port = "8080";
        if (argc > 2) {
            host = argv[1];
            port = argv[2];
        }

        std::cout << "Enter your nickname: ";
        std::string nickname;
        std::getline(std::cin, nickname);

        boost::asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(host, port);

        ChatClient client(io_context, endpoints);
        std::thread net_thread([&io_context]() { io_context.run(); });

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/quit") break;
            if (line.empty()) continue;

            std::string full_msg = "[" + nickname + "]: " + line;
            client.send_message(full_msg);
        }

        client.close();
        net_thread.join();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}