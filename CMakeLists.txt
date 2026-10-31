cmake_minimum_required(VERSION 3.10)
project(SecureChat VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ищем библиотеки Boost, потоки ОС и OpenSSL
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${Boost_INCLUDE_DIRS} common)

# Собираем сервер
add_executable(chat_server server/server.cpp)
target_link_libraries(chat_server PRIVATE ${Boost_LIBRARIES} Threads::Threads OpenSSL::Crypto)

# Собираем клиент
add_executable(chat_client client/client.cpp)
target_link_libraries(chat_client PRIVATE ${Boost_LIBRARIES} Threads::Threads OpenSSL::Crypto)