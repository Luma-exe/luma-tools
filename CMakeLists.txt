cmake_minimum_required(VERSION 3.14)
project(luma-tools VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch dependencies
include(FetchContent)

# cpp-httplib (header-only HTTP server)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (header-only JSON library)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# SQLite3 amalgamation (single-file, no external dependency)
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    URL_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)

# Build SQLite3 as a small static library from the amalgamation
add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
if(WIN32)
    target_compile_definitions(sqlite3_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(luma-tools
    src/main.cpp
    src/common.cpp
    src/platform.cpp
    src/discord.cpp
    src/stats.cpp
    src/routes_download.cpp
    src/routes_tools.cpp
    src/routes_stats.cpp
)

target_link_libraries(luma-tools PRIVATE httplib::httplib nlohmann_json::nlohmann_json sqlite3_lib)
target_include_directories(luma-tools PRIVATE ${CMAKE_SOURCE_DIR}/src/headers ${sqlite3_SOURCE_DIR})

if(WIN32)
    target_link_libraries(luma-tools PRIVATE ws2_32)
endif()

# Copy public folder to build directory
add_custom_command(TARGET luma-tools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/public $<TARGET_FILE_DIR:luma-tools>/public
)
