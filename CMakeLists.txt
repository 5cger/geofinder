cmake_minimum_required(VERSION 3.20)
project(GeoFinder VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(MSYS2 "C:/msys64/mingw64")

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ── Static libs ─────────────────────────────────────────────────
add_library(glfw_s STATIC IMPORTED)
set_target_properties(glfw_s PROPERTIES IMPORTED_LOCATION "${MSYS2}/lib/libglfw3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MSYS2}/include")

add_library(glew_s STATIC IMPORTED)
set_target_properties(glew_s PROPERTIES IMPORTED_LOCATION "${MSYS2}/lib/libglew32.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MSYS2}/include")

# FreeType + all transitive deps (harfbuzz, png, brotli, bz2, z, graphite, glib, pcre, intl, iconv)
foreach(_lib freetype harfbuzz png16 brotlidec brotlicommon bz2 z graphite2 glib-2.0 pcre2-8 intl iconv usp10)
    if(EXISTS "${MSYS2}/lib/lib${_lib}.a")
        list(APPEND FT_DEPS "${MSYS2}/lib/lib${_lib}.a")
    endif()
endforeach()

add_library(sqlite_s STATIC IMPORTED)
set_target_properties(sqlite_s PROPERTIES IMPORTED_LOCATION "${MSYS2}/lib/libsqlite3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MSYS2}/include")

# ── Sources ─────────────────────────────────────────────────────
file(GLOB GF_APP      src/app/Application.hpp src/app/Application.cpp)
file(GLOB_RECURSE GF_RENDER   CONFIGURE_DEPENDS src/render/*.hpp src/render/*.cpp)
file(GLOB_RECURSE GF_RESOURCE CONFIGURE_DEPENDS src/resource/*.hpp src/resource/*.cpp)
file(GLOB_RECURSE GF_UI       CONFIGURE_DEPENDS src/ui/*.hpp src/ui/*.cpp)
file(GLOB_RECURSE GF_SYSTEM   CONFIGURE_DEPENDS src/system/*.hpp src/system/*.cpp)
file(GLOB_RECURSE GF_DATA     CONFIGURE_DEPENDS src/data/*.hpp src/data/*.cpp)
file(GLOB_RECURSE GF_UTILS    CONFIGURE_DEPENDS src/utils/*.hpp src/utils/*.cpp)
file(GLOB GF_PINYIN third_party/mini-pinyin/*.c third_party/mini-pinyin/*.h)
file(GLOB GF_MAIN   src/main.cpp)

set(GF_CORE ${GF_APP} ${GF_RENDER} ${GF_RESOURCE} ${GF_UI} ${GF_SYSTEM} ${GF_DATA} ${GF_PINYIN} ${GF_UTILS})
add_library(geofinder_core STATIC ${GF_CORE})

target_include_directories(geofinder_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mini-pinyin"
    PUBLIC  "${MSYS2}/include" "${MSYS2}/include/freetype2")
target_compile_definitions(geofinder_core PRIVATE GLEW_STATIC GLFW_EXPOSE_NATIVE_WIN32)

# ── Executable ───────────────────────────────────────────────────
add_executable(geofinder WIN32 ${GF_MAIN})

target_link_libraries(geofinder PRIVATE
    geofinder_core
    # Static libs (all bundled into exe)
    glfw_s glew_s sqlite_s
    ${FT_DEPS}
    # System libs (present on every Windows PC)
    opengl32 gdi32 user32 shell32 ole32 version imm32 dwmapi comctl32 dwrite rpcrt4 C:/msys64/mingw64/lib/libwinpthread.a
)

# Static C++ runtime
if(MINGW)
    target_link_options(geofinder PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

message(STATUS "GeoFinder 0.1.0 — Release, static (system OpenGL only), C++17")
