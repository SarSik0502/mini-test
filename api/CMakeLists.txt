cmake_minimum_required(VERSION 3.20)
project(mini-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ICU REQUIRED COMPONENTS uc)
find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
find_path(NATS_INCLUDE_DIR nats/nats.h REQUIRED)
find_library(NATS_LIBRARY nats REQUIRED)

add_library(mini-test-common STATIC database.cpp jetstream.cpp)
target_include_directories(mini-test-common PUBLIC ${NATS_INCLUDE_DIR})
target_link_libraries(mini-test-common PUBLIC PostgreSQL::PostgreSQL ${NATS_LIBRARY})

add_executable(mini-test-api main.cpp)
target_link_libraries(mini-test-api PRIVATE
    mini-test-common
    ICU::uc
    nlohmann_json::nlohmann_json
    PkgConfig::HTTPLIB
    PkgConfig::HIREDIS
)

add_executable(mini-test-worker worker.cpp)
target_link_libraries(mini-test-worker PRIVATE mini-test-common nlohmann_json::nlohmann_json)
