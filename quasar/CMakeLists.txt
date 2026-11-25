cmake_minimum_required(VERSION 3.20)

project(Quasar
    VERSION 1.0.0
    DESCRIPTION "A modern, dependency-free UCI chess engine written in C++20"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUASAR_NATIVE "Optimise for the host CPU (non-portable binaries)" OFF)
option(QUASAR_BUILD_TESTS "Build the test suite" ON)
option(QUASAR_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(Threads REQUIRED)

set(QUASAR_SOURCES
    src/bench.cpp
    src/bitboard.cpp
    src/eval.cpp
    src/movegen.cpp
    src/position.cpp
    src/search.cpp
    src/transposition.cpp
    src/uci.cpp
    src/zobrist.cpp)

add_library(quasar_core STATIC ${QUASAR_SOURCES})
target_include_directories(quasar_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quasar_core PUBLIC Threads::Threads)
target_compile_definitions(quasar_core PUBLIC QUASAR_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(quasar_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(quasar_core PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
endif()

if(QUASAR_NATIVE)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
    if(HAS_MARCH_NATIVE)
        target_compile_options(quasar_core PUBLIC -march=native)
    endif()
endif()

if(QUASAR_SANITIZE AND NOT MSVC)
    target_compile_options(quasar_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(quasar_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(quasar src/main.cpp)
target_link_libraries(quasar PRIVATE quasar_core)

if(QUASAR_BUILD_TESTS)
    enable_testing()
    add_executable(quasar_tests tests/test_main.cpp)
    target_link_libraries(quasar_tests PRIVATE quasar_core)

    if(MSVC)
        target_compile_options(quasar_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(quasar_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME quasar_tests COMMAND quasar_tests)
endif()

install(TARGETS quasar RUNTIME DESTINATION bin)
