cmake_minimum_required(VERSION 3.16)

project(LapisGameEngine
    VERSION 0.1.0
    DESCRIPTION "Lapis - a from-scratch C++17 / OpenGL 4.5 game engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(LAPIS_BUILD_SANDBOX "Build the Sandbox demo game" ON)
option(LAPIS_BUILD_TESTS   "Build the unit / render tests" ON)

# Put every executable and library in one place so the Sandbox can find
# its Assets folder relative to the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Shared warning settings, applied per target via lapis_set_warnings().
function(lapis_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

add_subdirectory(ThirdParty/luau)
add_subdirectory(Engine)

if(LAPIS_BUILD_SANDBOX)
    if(WIN32)
        add_subdirectory(Sandbox)
    else()
        message(STATUS "Lapis: Sandbox needs a window backend; only Win32 exists so far - skipping on this platform.")
    endif()
endif()

if(LAPIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
