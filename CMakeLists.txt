cmake_minimum_required(VERSION 3.20)

# =========================================================================
# Project
# =========================================================================
project(GeoFinder
    VERSION      0.1.0
    DESCRIPTION  "Windows application launcher with GLFW + OpenGL"
    LANGUAGES    C CXX
)

# =========================================================================
# C++ standard
# =========================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =========================================================================
# Build-type defaults
# =========================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# =========================================================================
# Global compile options
# =========================================================================
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# =========================================================================
# Options
# =========================================================================
option(GEOFINDER_USE_VULKAN "Use Vulkan backend instead of OpenGL" OFF)
option(GEOFINDER_BUILD_TESTS "Build unit tests" OFF)

# =========================================================================
# Output directories
# =========================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# =========================================================================
# Dependencies
# =========================================================================
include(cmake/Dependencies.cmake)

# =========================================================================
# Source file discovery (modules activated per sprint)
# =========================================================================

# --- App lifecycle ---
file(GLOB GF_APP_SOURCES
    src/app/Application.hpp
    src/app/Application.cpp
)

# --- Renderer (Sprint 1) ---
file(GLOB_RECURSE GF_RENDER_SOURCES CONFIGURE_DEPENDS
    src/render/*.hpp
    src/render/*.cpp
)

# --- Resource (Sprint 1) ---
file(GLOB_RECURSE GF_RESOURCE_SOURCES CONFIGURE_DEPENDS
    src/resource/*.hpp
    src/resource/*.cpp
)

# --- UI (Sprint 3) ---
file(GLOB_RECURSE GF_UI_SOURCES CONFIGURE_DEPENDS
    src/ui/*.hpp
    src/ui/*.cpp
)

# --- System (Sprint 1) ---
file(GLOB_RECURSE GF_SYSTEM_SOURCES CONFIGURE_DEPENDS
    src/system/*.hpp
    src/system/*.cpp
)

# --- Data (Sprint 3: DataTypes.hpp shared type) ---
file(GLOB_RECURSE GF_DATA_SOURCES CONFIGURE_DEPENDS
    src/data/*.hpp
    src/data/*.cpp
)

# --- Utils ---
file(GLOB_RECURSE GF_UTILS_SOURCES CONFIGURE_DEPENDS
    src/utils/*.hpp
    src/utils/*.cpp
)

# --- mini-pinyin (Sprint 4, vendored) ---
file(GLOB GF_PINYIN_SOURCES CONFIGURE_DEPENDS
    third_party/mini-pinyin/*.c
    third_party/mini-pinyin/*.h
)

# --- Main entry point (executable only) ---
file(GLOB GF_MAIN_SOURCES
    src/main.cpp
)

# =========================================================================
# Static library: geofinder_core
# =========================================================================
set(GF_CORE_SOURCES
    ${GF_APP_SOURCES}
    ${GF_RENDER_SOURCES}
    ${GF_RESOURCE_SOURCES}
    ${GF_UI_SOURCES}
    ${GF_SYSTEM_SOURCES}
    ${GF_DATA_SOURCES}
    ${GF_PINYIN_SOURCES}
    ${GF_UTILS_SOURCES}
)

add_library(geofinder_core STATIC ${GF_CORE_SOURCES})

target_include_directories(geofinder_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mini-pinyin"
)

target_link_libraries(geofinder_core
    PUBLIC
        glfw
        OpenGL::GL
        GLEW::GLEW
        ${FREETYPE_LIBRARY_RELEASE}
        SQLite3::SQLite3
)

# FreeType include directories (not a modern imported target)
target_include_directories(geofinder_core
    PUBLIC
        ${FREETYPE_INCLUDE_DIR_ft2build}
        ${FREETYPE_INCLUDE_DIR_freetype2}
)
# 已通过 Dependencies.cmake 导入：SQLite3::SQLite3, nlohmann_json (header-only)
# TODO(Sprint12): add fmt::fmt

# GLEW static link — 必须在 #include <GL/glew.h> 之前定义
target_compile_definitions(geofinder_core PRIVATE GLEW_STATIC)

# GLFW native access — 允许 glfwGetWin32Window
target_compile_definitions(geofinder_core PRIVATE GLFW_EXPOSE_NATIVE_WIN32)

# =========================================================================
# Executable: geofinder
# =========================================================================
add_executable(geofinder WIN32 ${GF_MAIN_SOURCES})

target_link_libraries(geofinder
    PRIVATE
        geofinder_core
)

# Windows-specific link libraries
if(WIN32)
    target_link_libraries(geofinder PRIVATE dwmapi user32 gdi32 shell32 ole32 version imm32)
endif()

# =========================================================================
# Backend selection
# =========================================================================
if(GEOFINDER_USE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_compile_definitions(geofinder_core PRIVATE GEOFINDER_USE_VULKAN)
    # TODO(Sprint1): add VulkanBackend sources
    target_link_libraries(geofinder PRIVATE Vulkan::Vulkan)
else()
    target_compile_definitions(geofinder_core PRIVATE GEOFINDER_USE_OPENGL)
    # TODO(Sprint1): add OpenGLBackend sources
endif()

# =========================================================================
# IDE source-group organisation
# =========================================================================
foreach(_target geofinder_core geofinder)
    if(TARGET ${_target})
        get_target_property(_sources ${_target} SOURCES)
        if(_sources)
            source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
                PREFIX "Source Files"
                FILES ${_sources}
            )
        endif()
    endif()
endforeach()

# =========================================================================
# Tests (future: -DGEOFINDER_BUILD_TESTS=ON)
# =========================================================================
if(GEOFINDER_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_subdirectory(tests)
    endif()
endif()

# =========================================================================
# Print build configuration summary
# =========================================================================
message(STATUS "")
message(STATUS "GeoFinder ${PROJECT_VERSION} — build configuration")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Backend     : $<IF:$<BOOL:${GEOFINDER_USE_VULKAN}>,Vulkan,OpenGL>")
message(STATUS "")
