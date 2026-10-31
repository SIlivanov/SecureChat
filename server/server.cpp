#include <iostream>
#include <set>
#include <memory>
#include <deque>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

// Интерфейс участника чата
class ChatParticipant {
public:
    virtual ~ChatParticipant() = default;
    virtual void deliver(const std::string& msg) = 0;
};

using ParticipantPtr = std::shared_ptr<ChatParticipant>;

// Комната чата: хранит всех подключенных клиентов и рассылает сообщения
class ChatRoom {
public:
    void join(ParticipantPtr participant) {
        participants_.insert(participant);
        std::cout << "[Server] New client joined. Total: " << participants_.size() << "\n";
    }

    void leave(ParticipantPtr participant) {
        participants_.erase(participant);
        std::cout << "[Server] Client left. Total: " << participants_.size() << "\n";
    }

    void deliver(const std::string& msg, ParticipantPtr sender) {
        for (auto& participant : participants_) {
            if (participant != sender) { // Не отправляем сообщение самому себе
                participant->deliver(msg);
            }
        }
    }

private:
    std::set<ParticipantPtr> participants_;
};

// Сессия конкретного клиента
class ChatSession : public ChatParticipant, public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(tcp::socket socket, ChatRoom& room)
        : socket_(std::move(socket)), room_(room) {}

    void start() {
        room_.join(shared_from_this());
        do_read();
    }

    void deliver(const std::string& msg) override {
        bool write_in_progress = !write_msgs_.empty();
        write_msgs_.push_back(msg);
        if (!write_in_progress) {
            do_write();
        }
    }

private:
    void do_read() {
        auto self(shared_from_this());
        // Читаем данные до символа переноса строки '\n'
        boost::asio::async_read_until(socket_, buffer_, '\n',
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::istream is(&buffer_);
                    std::string line;
                    std::getline(is, line);
                    line += "\n";
                    room_.deliver(line, shared_from_this());
                    do_read(); // Ждем следующее сообщение
                } else {
                    room_.leave(shared_from_this());
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_msgs_.pop_front();
                    if (!write_msgs_.empty()) {
                        do_write();
                    }
                } else {
                    room_.leave(shared_from_this());
                }
            });
    }

    tcp::socket socket_;
    ChatRoom& room_;
    boost::asio::streambuf buffer_;
    std::deque<std::string> write_msgs_;
};

// Сам сервер, принимающий подключения
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
                do_accept(); // Ждем следующего клиента
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

        std::cout << "[Server] Started on port " << port << "...\n";
        io_context.run(); // Запускаем бесконечный цикл обработки сетевых событий
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}