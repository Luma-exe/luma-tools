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

add_executable(luma-tools
    src/main.cpp
    src/common.cpp
    src/platform.cpp
    src/discord.cpp
    src/routes_download.cpp
    src/routes_tools.cpp
)

target_link_libraries(luma-tools PRIVATE httplib::httplib nlohmann_json::nlohmann_json)
target_include_directories(luma-tools PRIVATE ${CMAKE_SOURCE_DIR}/src/headers)

if(WIN32)
    target_link_libraries(luma-tools PRIVATE ws2_32)
endif()

# Copy public folder to build directory
add_custom_command(TARGET luma-tools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/public $<TARGET_FILE_DIR:luma-tools>/public
)
