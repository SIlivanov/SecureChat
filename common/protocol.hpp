#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>

namespace protocol {

constexpr size_t HEADER_SIZE = sizeof(uint32_t); // 4 байта
constexpr size_t MAX_BODY_SIZE = 64 * 1024;      // Защита от переполнения памяти: макс 64 КБ на сообщение

// Упаковывает полезную нагрузку (payload) в сетевой пакет
inline std::vector<uint8_t> pack_message(const std::vector<uint8_t>& payload) {
    if (payload.size() > MAX_BODY_SIZE) {
        throw std::runtime_error("Payload too large");
    }

    uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
    std::vector<uint8_t> packet(HEADER_SIZE + payload.size());

    std::memcpy(packet.data(), &net_len, HEADER_SIZE);
    std::memcpy(packet.data() + HEADER_SIZE, payload.data(), payload.size());

    return packet;
}

// Распаковывает 4 байта заголовка и возвращает длину тела сообщения
inline uint32_t unpack_header(const uint8_t* header_bytes) {
    uint32_t net_len = 0;
    std::memcpy(&net_len, header_bytes, HEADER_SIZE);
    uint32_t host_len = ntohl(net_len); // Переводим обратно в порядок байт процессора

    if (host_len > MAX_BODY_SIZE) {
        throw std::runtime_error("Invalid packet header: body size exceeds limit");
    }
    return host_len;
}

} // namespace protocol