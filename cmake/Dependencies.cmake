# =============================================================================
# Dependencies.cmake — locate and link all third-party libraries.
#
# Dependencies are managed via MSYS2 MinGW packages, same as the vibebar (c2)
# project.  Each dependency is added here as the corresponding module is
# implemented; Sprint 0 starts with the bare minimum (GLFW + OpenGL).
# =============================================================================

# -- Module path (so our own cmake/Find*.cmake scripts are visible) ------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# -- MSYS2 MinGW prefix -------------------------------------------------------
if(WIN32 AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    set(_msys2_prefix "C:/msys64/mingw64")
    if(EXISTS "${_msys2_prefix}")
        list(APPEND CMAKE_PREFIX_PATH "${_msys2_prefix}")
        list(APPEND CMAKE_INCLUDE_PATH "${_msys2_prefix}/include")
        list(APPEND CMAKE_LIBRARY_PATH "${_msys2_prefix}/lib")
    endif()
endif()

# -- GLFW (Sprint 1: window creation) -----------------------------------------
find_package(glfw3 REQUIRED)
message(STATUS "  GLFW     : found")

# -- OpenGL (Sprint 1: rendering context) -------------------------------------
find_package(OpenGL REQUIRED)
message(STATUS "  OpenGL   : ${OPENGL_INCLUDE_DIR}")

# -- GLEW (Sprint 1: OpenGL extension loading) ---------------------------------
# 静态链接，需要 -DGLEW_STATIC 编译定义（在 CMakeLists.txt 设置）
find_package(glew REQUIRED)
message(STATUS "  GLEW     : found")

# -- GLM (Sprint 1: vector types for CommandBuffer) ----------------------------
# Header-only；已在 CMAKE_INCLUDE_PATH 的 MSYS2 prefix 下
if(EXISTS "${_msys2_prefix}/include/glm/glm.hpp")
    message(STATUS "  GLM      : found (header-only)")
else()
    message(FATAL_ERROR "GLM not found. Install: pacman -S mingw-w64-x86_64-glm")
endif()

# -- FreeType (Sprint 2: font system) -----------------------------------------
find_package(freetype REQUIRED)
message(STATUS "  FreeType : found")

# -- nlohmann_json (Sprint 4: config system + IndexDatabase) -----------------
# Header-only；已在 CMAKE_INCLUDE_PATH 的 MSYS2 prefix 下
if(EXISTS "${_msys2_prefix}/include/nlohmann/json.hpp")
    message(STATUS "  JSON     : found (header-only)")
else()
    message(FATAL_ERROR "nlohmann_json not found. Install: pacman -S mingw-w64-x86_64-nlohmann-json")
endif()

# -- fmt (Sprint 12: Logger) — install: pacman -S mingw-w64-x86_64-fmt --------
# find_package(fmt REQUIRED)
# message(STATUS "  fmt      : found")

# -- SQLite3 (Sprint 4: IndexDatabase) ----------------------------------------
find_library(SQLite3_LIBRARY
    NAMES sqlite3 libsqlite3 sqlite3.dll
    PATHS "${_msys2_prefix}/lib"
    REQUIRED
)
find_path(SQLite3_INCLUDE_DIR
    NAMES sqlite3.h
    PATHS "${_msys2_prefix}/include"
    REQUIRED
)
if(SQLite3_LIBRARY AND SQLite3_INCLUDE_DIR)
    add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLite3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIR}"
    )
    message(STATUS "  SQLite3  : ${SQLite3_LIBRARY}")
else()
    message(FATAL_ERROR "SQLite3 not found. Install: pacman -S mingw-w64-x86_64-sqlite3")
endif()

# -- Summary ------------------------------------------------------------------
message(STATUS "  Prefix   : ${CMAKE_PREFIX_PATH}")
