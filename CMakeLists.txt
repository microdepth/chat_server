cmake_minimum_required(VERSION 3.26)
project(chat_server)

set(CMAKE_CXX_STANDARD 20)

add_executable(chat_server main.cpp)

target_link_libraries(chat_server ws2_32)
