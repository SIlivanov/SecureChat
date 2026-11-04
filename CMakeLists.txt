cmake_minimum_required(VERSION 3.14)
project(SecureChat VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ищем зависимости кроссплатформенно
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Создаем интерфейсную библиотеку для общих заголовков и настроек платформы
add_library(chat_common INTERFACE)
target_include_directories(chat_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(chat_common INTERFACE 
    Boost::system 
    Threads::Threads 
    OpenSSL::Crypto
)

if(WIN32)
    target_compile_definitions(chat_common INTERFACE _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
    target_link_libraries(chat_common INTERFACE ws2_32 mswsock crypt32)
endif()

if(MSVC)
    target_compile_options(chat_common INTERFACE /W3 /EHsc)
else()
    target_compile_options(chat_common INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_executable(chat_server server/server.cpp)
target_link_libraries(chat_server PRIVATE chat_common)

add_executable(chat_client client/client.cpp)
target_link_libraries(chat_client PRIVATE chat_common)
