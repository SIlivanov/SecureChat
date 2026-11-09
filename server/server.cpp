#include <iostream>
#include <set>
#include <memory>
#include <deque>
#include <vector>
#include <boost/asio.hpp>
#include "protocol.hpp"

using boost::asio::ip::tcp;

class ChatParticipant {
public:
    virtual ~ChatParticipant() = default;
    virtual void deliver(const std::vector<uint8_t>& packet) = 0;
};

using ParticipantPtr = std::shared_ptr<ChatParticipant>;

class ChatRoom {
public:
    void join(ParticipantPtr participant) {
        participants_.insert(participant);
        std::cout << "[Server] Client joined. Active clients: " << participants_.size() << "\n";
    }

    void leave(ParticipantPtr participant) {
        participants_.erase(participant);
        std::cout << "[Server] Client left. Active clients: " << participants_.size() << "\n";
    }

    void deliver(const std::vector<uint8_t>& packet, ParticipantPtr sender) {
        for (auto& participant : participants_) {
            if (participant != sender) {
                participant->deliver(packet);
            }
        }
    }

private:
    std::set<ParticipantPtr> participants_;
};

class ChatSession : public ChatParticipant, public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(tcp::socket socket, ChatRoom& room)
        : socket_(std::move(socket)), room_(room) {}

    void start() {
        room_.join(shared_from_this());
        do_read_header();
    }

    void deliver(const std::vector<uint8_t>& packet) override {
        bool write_in_progress = !write_packets_.empty();
        write_packets_.push_back(packet);
        if (!write_in_progress) {
            do_write();
        }
    }

private:
    // Читаем ровно 4 байта заголовка
    void do_read_header() {
        auto self(shared_from_this());
        boost::asio::async_read(socket_,
            boost::asio::buffer(read_header_, protocol::HEADER_SIZE),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    try {
                        uint32_t body_len = protocol::unpack_header(read_header_);
                        read_body_.resize(body_len);
                        do_read_body(); // Переходим к чтению тела сообщения
                    } catch (const std::exception& e) {
                        std::cerr << "[Server] Protocol error: " << e.what() << "\n";
                        room_.leave(shared_from_this());
                    }
                } else {
                    room_.leave(shared_from_this());
                }
            });
    }

    // Читаем ровно body_len байт зашифрованного тела
    void do_read_body() {
        auto self(shared_from_this());
        boost::asio::async_read(socket_,
            boost::asio::buffer(read_body_.data(), read_body_.size()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::cout << "[Server] Relayed encrypted packet (" << read_body_.size() << " bytes)\n";
                    // Упаковываем обратно с заголовком и рассылаем остальным клиентам
                    auto full_packet = protocol::pack_message(read_body_);
                    room_.deliver(full_packet, shared_from_this());
                    do_read_header(); // Снова ждем заголовок следующего сообщения
                } else {
                    room_.leave(shared_from_this());
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_packets_.front().data(), write_packets_.front().size()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_packets_.pop_front();
                    if (!write_packets_.empty()) {
                        do_write();
                    }
                } else {
                    room_.leave(shared_from_this());
                }
            });
    }

    tcp::socket socket_;
    ChatRoom& room_;
    uint8_t read_header_[protocol::HEADER_SIZE];
    std::vector<uint8_t> read_body_;
    std::deque<std::vector<uint8_t>> write_packets_;
};

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context, const tcp::endpoint& endpoint)
        : acceptor_(io_context, endpoint) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<ChatSession>(std::move(socket), room_)->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    ChatRoom room_;
};

int main(int argc, char* argv[]) {
    try {
        uint16_t port = 8080;
        if (argc > 1) port = std::stoi(argv[1]);

        boost::asio::io_context io_context;
        tcp::endpoint endpoint(tcp::v4(), port);
        ChatServer server(io_context, endpoint);

        // Перехватываем ctrl + c
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](const boost::system::error_code&, int signal_number) {
            std::cout << "\n[Server] Received signal " << signal_number << ". Stopping gracefully...\n";
            io_context.stop();
        });

        std::cout << "[Server] Secure Chat Server started on port " << port << "...\n";
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}