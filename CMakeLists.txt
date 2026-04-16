cmake_minimum_required(VERSION 3.25)

# ============================================================================
# vcpkg Integration (MUST be before project())
# ============================================================================
# Recommended: use CMakePresets.json (cmake --preset build-lib / build-test / build-dashboard / build-all)
# Each preset sets VCPKG_MANIFEST_DIR (3rd/), VCPKG_INSTALLED_DIR, CMAKE_TOOLCHAIN_FILE,
# and BUILD_TEST / BUILD_DASHBOARD switches.
#
# Manual usage (without presets):
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake
#                       -DVCPKG_MANIFEST_DIR=<sourceDir>/3rd
#                       -DBUILD_TEST=ON -DBUILD_DASHBOARD=ON
# ============================================================================

# Auto-detect vcpkg toolchain from VCPKG_ROOT if not explicitly provided
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "vcpkg toolchain file")
        message(STATUS "Auto-detected VCPKG_ROOT: $ENV{VCPKG_ROOT}")
    else()
        message(FATAL_ERROR
            "CMAKE_TOOLCHAIN_FILE not set and VCPKG_ROOT not found.\n"
            "Please either:\n"
            "  1. Use a CMake preset: cmake --preset build-lib\n"
            "  2. Set environment variable VCPKG_ROOT to your vcpkg installation, or\n"
            "  3. Pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake")
    endif()
endif()

# ============================================================================
# Project
# ============================================================================

project(rimeclaw LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Build Options
# ============================================================================

option(BUILD_TEST "Build test executable" ON)
option(BUILD_DASHBOARD "Build dashboard executable" ON)
option(BUILD_LLAMA_LOCAL_PROVIDER "Enable local LLM inference via llama.cpp" ON)

# ============================================================================
# Compiler Configuration
# ============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_WINDOWS)
    if(BUILD_DASHBOARD)
        add_definitions(-DBUILD_WITH_GUI)
    endif()
    add_compile_options(/W3)
    add_compile_options(/utf-8)
    add_compile_options($<$<CONFIG:Debug>:/Od> $<$<CONFIG:Release>:/O2>)
    set(CMAKE_DEBUG_POSTFIX "d")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Enable folder organization in Visual Studio
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set(CMAKE_SUPPRESS_REGENERATION true)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(BUILD_DASHBOARD)
        add_definitions(-DBUILD_WITH_GUI)
    endif()
endif()

# ============================================================================
# Find vcpkg-installed Packages
# ============================================================================

message(STATUS "")
message(STATUS "=== vcpkg manifest mode ===")
message(STATUS "  Manifest dir:  ${VCPKG_MANIFEST_DIR}")
message(STATUS "  Installed dir: ${VCPKG_INSTALLED_DIR}")

include(3rd/CMakeLists.txt)

# ============================================================================
# Output Directories
# ============================================================================

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================================
# Project Subdirectories
# ============================================================================
message(STATUS "")
message(STATUS "=== Adding RimeClaw Library ===")
add_subdirectory(rimeclaw)

if(BUILD_DASHBOARD)
    message(STATUS "")
    message(STATUS "=== Adding RimeClaw Dashboard ===")
    add_subdirectory(dashboard)
endif()

if(BUILD_TEST)
    message(STATUS "")
    message(STATUS "=== Adding RimeClaw Test ===")
    add_subdirectory(test)
endif()

message(STATUS "")
message(STATUS "=== Configuration Complete ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
