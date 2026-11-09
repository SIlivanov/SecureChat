#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include <memory>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace crypto {

constexpr size_t AES_KEY_SIZE = 32; // 256 бит
constexpr size_t AES_IV_SIZE  = 16; // 128 бит (размер блока AES)

// RAII-обертка для контекста OpenSSL (автоматически вызовет EVP_CIPHER_CTX_free)
struct CipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const {
        if (ctx) EVP_CIPHER_CTX_free(ctx);
    }
};
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter>;

// Временный общий ключ (32 байта)
inline const std::vector<uint8_t> DEFAULT_KEY = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

// Шифрование строки -> возвращает массив байт
inline std::vector<uint8_t> encrypt(const std::string& plaintext, const std::vector<uint8_t>& key = DEFAULT_KEY) {
    if (key.size() != AES_KEY_SIZE) throw std::runtime_error("Invalid AES key size");

    // Генерируем случайный IV
    std::vector<uint8_t> iv(AES_IV_SIZE);
    if (RAND_bytes(iv.data(), AES_IV_SIZE) != 1) {
        throw std::runtime_error("Failed to generate random IV");
    }

    // Создаем контекст шифрования
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throw std::runtime_error("Failed to create EVP_CIPHER_CTX");

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }

    // Выделяем буфер в начале место под IV, затем под шифротекст
    std::vector<uint8_t> result(AES_IV_SIZE + plaintext.size() + AES_IV_SIZE);
    std::memcpy(result.data(), iv.data(), AES_IV_SIZE); // Записываем IV в первые 16 байт

    int len = 0;
    int ciphertext_len = 0;

    // Шифруем основную часть
    if (EVP_EncryptUpdate(ctx.get(), result.data() + AES_IV_SIZE, &len,
                          reinterpret_cast<const uint8_t*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }
    ciphertext_len = len;

    
    if (EVP_EncryptFinal_ex(ctx.get(), result.data() + AES_IV_SIZE + len, &len) != 1) {
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    ciphertext_len += len;

    // Обрезаем вектор ровно по реальному размеру
    result.resize(AES_IV_SIZE + ciphertext_len);
    return result;
}

// Расшифровка массива байт -> возвращает исходную строку
inline std::string decrypt(const std::vector<uint8_t>& packet, const std::vector<uint8_t>& key = DEFAULT_KEY) {
    if (key.size() != AES_KEY_SIZE) throw std::runtime_error("Invalid AES key size");
    if (packet.size() <= AES_IV_SIZE) throw std::runtime_error("Packet too short to contain IV and data");

    // Извлекаем IV из первых 16 байт пакета
    const uint8_t* iv = packet.data();
    const uint8_t* ciphertext = packet.data() + AES_IV_SIZE;
    int ciphertext_len = static_cast<int>(packet.size() - AES_IV_SIZE);

    CipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throw std::runtime_error("Failed to create EVP_CIPHER_CTX");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1) {
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }

    std::vector<uint8_t> plaintext(ciphertext_len + AES_IV_SIZE);
    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ciphertext, ciphertext_len) != 1) {
        throw std::runtime_error("EVP_DecryptUpdate failed");
    }
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) != 1) {
        throw std::runtime_error("Decryption failed (wrong key or corrupted packet)");
    }
    plaintext_len += len;
    std::string result(reinterpret_cast<char*>(plaintext.data()), plaintext_len);
    // Затираем текст в памяти
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    return result;
}

} // namespace crypto