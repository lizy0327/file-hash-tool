cmake_minimum_required(VERSION 3.16)

project(FileHashTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(FILEHASHTOOL_BUILD_SMOKE_TEST "Build the core smoke-test executable" ON)
option(FILEHASHTOOL_BUILD_GUI "Build the native desktop GUI" ON)
option(FILEHASHTOOL_BUILD_BENCHMARK "Build the local hashing benchmark" OFF)

add_library(filehash_core
    src/core/hash_engine.cpp
    src/core/hash_types.cpp
    src/core/crc32.cpp
    src/platform/hash_backend.cpp
)

target_include_directories(filehash_core PUBLIC src)
target_compile_features(filehash_core PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(filehash_core PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0601)
    target_link_libraries(filehash_core PRIVATE bcrypt)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(filehash_core PRIVATE OpenSSL::Crypto)
endif()

if(MSVC)
    target_compile_options(filehash_core PRIVATE /W4 /EHsc /utf-8 /permissive-)
else()
    target_compile_options(filehash_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FILEHASHTOOL_BUILD_SMOKE_TEST)
    enable_testing()
    add_executable(filehash_smoke tests/hash_smoke.cpp)
    target_link_libraries(filehash_smoke PRIVATE filehash_core)
    if(MSVC)
        target_compile_options(filehash_smoke PRIVATE /W4 /EHsc /utf-8 /permissive-)
    endif()
    add_test(NAME filehash_smoke COMMAND filehash_smoke)
endif()

if(FILEHASHTOOL_BUILD_BENCHMARK)
    add_executable(filehash_benchmark tools/hash_benchmark.cpp)
    target_link_libraries(filehash_benchmark PRIVATE filehash_core)
    if(MSVC)
        target_compile_options(filehash_benchmark PRIVATE /W4 /EHsc /utf-8 /permissive-)
    endif()
endif()

if(FILEHASHTOOL_BUILD_GUI)
    if(WIN32)
        add_executable(filehash_tool WIN32 src/windows/main.cpp src/windows/resource.rc)
        target_link_libraries(filehash_tool PRIVATE filehash_core comctl32 comdlg32 shell32 advapi32)
        target_compile_definitions(filehash_tool PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0601)
        if(MSVC)
            target_compile_options(filehash_tool PRIVATE /W4 /EHsc /utf-8 /permissive-)
        endif()
    elseif(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
        add_executable(filehash_tool src/linux/main.cpp)
        target_link_libraries(filehash_tool PRIVATE filehash_core PkgConfig::GTK3)
        if(NOT APPLE)
            install(TARGETS filehash_tool RUNTIME DESTINATION bin)
        endif()
    elseif(APPLE)
        message(STATUS "Native GUI is currently provided for Windows and Linux; building the shared core on macOS")
    endif()
endif()
