# UDS-OB-HFT: C++20 server (replay) + client (paper trading).
cmake_minimum_required(VERSION 3.16)

project(UDS_OB_HFT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(client
    client/main.cpp
    client/market_client.cpp
    client/trade_processor.cpp
)

add_executable(server
    server/main.cpp
    server/market_data_feed.cpp
    server/session_manager.cpp
)

target_include_directories(client PRIVATE
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_SOURCE_DIR}/common
)

target_include_directories(server PRIVATE
    ${CMAKE_SOURCE_DIR}/server/include
    ${CMAKE_SOURCE_DIR}/server
    ${CMAKE_SOURCE_DIR}/common
)

target_compile_options(client PRIVATE
    -Wall -Wextra -Wpedantic
)

target_compile_options(server PRIVATE
    -Wall -Wextra -Wpedantic
)

target_link_libraries(client PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Threads::Threads)