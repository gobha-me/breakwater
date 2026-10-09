cmake_minimum_required(VERSION 3.28)
project(breakwater VERSION 0.1.0 LANGUAGES CXX)

option(BREAKWATER_BUILD_TESTS "Build the core unit tests" ON)
option(BREAKWATER_BUILD_EXTENSION "Build the Godot GDExtension (fetches godot-cpp)" ON)
option(BREAKWATER_WERROR "Treat compiler warnings as errors" ON)
set(BREAKWATER_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list for core and tests (e.g. address,undefined)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

add_subdirectory(core)

if(BREAKWATER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BREAKWATER_BUILD_EXTENSION)
    if(BREAKWATER_SANITIZE)
        message(FATAL_ERROR "The extension is loaded by an unsanitized Godot; build it without BREAKWATER_SANITIZE")
    endif()
    add_subdirectory(extension)
endif()
