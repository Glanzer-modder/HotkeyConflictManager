cmake_minimum_required(VERSION 3.21)

# ============================================================
#  HotkeyConflictManager  -  CMakeLists.txt
#
#  Local dependency layout:
#    Project     : D:\Games\Utilities\VS Projects\HotkeyConflictManager\
#    CommonLibSSE: D:\Games\Utilities\VS Projects-Dependencies\CommonLibSSE-NG\
#    vcpkg       : D:\Games\Utilities\VS Projects-Dependencies\vcpkg\
#
#  Build (from the project root in a Developer PowerShell):
#    cmake --preset vs2022-windows
#    cmake --build build --config Release
# ============================================================

project(
    HotkeyConflictManager
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD         23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

# ---- Locate CommonLibSSE-NG (local source tree) ------------

set(COMMONLIBSSE_PATH
    "D:/Games/Utilities/VS Projects-Dependencies/CommonLibSSE-NG"
    CACHE PATH "Path to the local CommonLibSSE-NG source tree")

if(NOT EXISTS "${COMMONLIBSSE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "CommonLibSSE-NG not found at: ${COMMONLIBSSE_PATH}\n"
        "Set -DCOMMONLIBSSE_PATH=<path> to the correct location.")
endif()

# Disable tests to avoid missing Catch2 dependency
set(BUILD_TESTS   OFF CACHE BOOL "Disable CommonLibSSE-NG tests" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable generic CTest tests"   FORCE)

add_subdirectory("${COMMONLIBSSE_PATH}" CommonLibSSE-NG EXCLUDE_FROM_ALL)

# ---- Find packages -----------------------------------------

find_package(fmt    CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ---- Source files ------------------------------------------

set(SOURCES
    src/main.cpp
    src/KeyRegistry.cpp
    src/Hooks.cpp
    src/PapyrusFunctions.cpp
    src/ControlMapWatcher.cpp
    src/Settings.cpp
    src/Serialization.cpp
)

# ---- Build the SKSE DLL ------------------------------------

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
        src/PCH.h
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        CommonLibSSE::CommonLibSSE
        fmt::fmt
        spdlog::spdlog
)

# ---- Output directory --------------------------------------
#  Always output to build/dist/SKSE/Plugins so the DLL never
#  gets written directly into the game or MO2 directories.
#  Copy it manually to Skyrim when ready to test.
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/SKSE/Plugins"
)
