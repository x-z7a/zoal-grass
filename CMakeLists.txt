cmake_minimum_required(VERSION 3.20)
project(zoal_grass VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
    # one self-contained .xpl: no dependency on the MSVC runtime being installed
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(ZOAL_GRASS_BUILD_TESTS "Build the X-Plane-free core tests" ON)
set(ZOAL_GRASS_VERSION "dev" CACHE STRING "Version string compiled into the plugin")
set(XPLANE_SDK_ROOT "${CMAKE_SOURCE_DIR}/.cache/sdk" CACHE PATH "X-Plane SDK root (scripts/ensure-deps.sh fetches it)")
set(XPLANE_ROOT "/Users/dzou/X-Plane 12" CACHE PATH "X-Plane folder used by the install_xplane target")

if(APPLE)
    set(ZOAL_GRASS_PLATFORM "mac_x64")
elseif(WIN32)
    set(ZOAL_GRASS_PLATFORM "win_x64")
elseif(UNIX)
    set(ZOAL_GRASS_PLATFORM "lin_x64")
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

# Public-domain 7-Zip decoder: compressed global scenery, no external executable needed.
add_library(zg_7zip STATIC
    third_party/7zip/7zAlloc.c
    third_party/7zip/7zArcIn.c
    third_party/7zip/7zBuf.c
    third_party/7zip/7zBuf2.c
    third_party/7zip/7zCrc.c
    third_party/7zip/7zCrcOpt.c
    third_party/7zip/7zDec.c
    third_party/7zip/7zStream.c
    third_party/7zip/Bcj2.c
    third_party/7zip/Bra.c
    third_party/7zip/Bra86.c
    third_party/7zip/BraIA64.c
    third_party/7zip/CpuArch.c
    third_party/7zip/Delta.c
    third_party/7zip/Lzma2Dec.c
    third_party/7zip/LzmaDec.c
    third_party/7zip/Ppmd7.c
    third_party/7zip/Ppmd7Dec.c
)
target_include_directories(zg_7zip PUBLIC third_party/7zip)
target_compile_definitions(zg_7zip PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)

# ---------------------------------------------------------------- core (no X-Plane)
add_library(zoal_grass_core STATIC
    src/apt.cpp
    src/assets.cpp
    src/biome.cpp
    src/config.cpp
    src/dsf.cpp
    src/dsf_archive.cpp
    src/http.cpp
    src/mask.cpp
    src/osm.cpp
    src/platform.cpp
)
target_include_directories(zoal_grass_core PUBLIC src)
target_link_libraries(zoal_grass_core PRIVATE zg_7zip)
if(WIN32)
    # windows.h (pulled in by the XPLM headers on Windows) must not define min/max macros over std::min/std::max
    target_compile_definitions(zoal_grass_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(zoal_grass_core PUBLIC winhttp)   # OSM downloads (src/http.cpp)
else()
    target_link_libraries(zoal_grass_core PUBLIC ${CMAKE_DL_LIBS})   # dlopen of the system libcurl
endif()
if(MSVC)
    target_compile_options(zoal_grass_core PUBLIC /W3 /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(zoal_grass_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof)
endif()
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(zoal_grass_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------- plugin
if(NOT EXISTS "${XPLANE_SDK_ROOT}/CHeaders/XPLM/XPLMPlugin.h")
    message(FATAL_ERROR
        "X-Plane SDK not found at ${XPLANE_SDK_ROOT}. Run scripts/ensure-deps.sh, "
        "or pass -DXPLANE_SDK_ROOT=/path/to/SDK.")
endif()

add_library(zoal-grass MODULE src/plugin.cpp src/manager.cpp)
target_link_libraries(zoal-grass PRIVATE zoal_grass_core)
target_include_directories(zoal-grass PRIVATE "${XPLANE_SDK_ROOT}/CHeaders/XPLM")
target_compile_definitions(zoal-grass PRIVATE
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1 XPLM410=1
    ZOAL_GRASS_VERSION="${ZOAL_GRASS_VERSION}")
if(NOT MSVC)
    target_compile_options(zoal-grass PRIVATE -fvisibility=hidden)
endif()

# X-Plane loads <name>.xpl from <plugin>/<platform>/
set_target_properties(zoal-grass PROPERTIES
    PREFIX ""
    OUTPUT_NAME "zoal-grass"
    SUFFIX ".xpl"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ZOAL_GRASS_PLATFORM}")
# Visual Studio appends /<Config> to output directories; keep the .xpl where packaging looks for it
foreach(config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER "${config}" config_upper)
    set_target_properties(zoal-grass PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${ZOAL_GRASS_PLATFORM}"
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${ZOAL_GRASS_PLATFORM}")
endforeach()

if(APPLE)
    target_compile_definitions(zoal-grass PRIVATE APL=1 IBM=0 LIN=0)
    target_link_libraries(zoal-grass PRIVATE "-F${XPLANE_SDK_ROOT}/Libraries/Mac" "-framework XPLM")
elseif(WIN32)
    target_compile_definitions(zoal-grass PRIVATE APL=0 IBM=1 LIN=0)
    target_link_libraries(zoal-grass PRIVATE "${XPLANE_SDK_ROOT}/Libraries/Win/XPLM_64.lib")
else()
    # No XPLM library on Linux: X-Plane resolves those symbols when it loads the plugin.
    target_compile_definitions(zoal-grass PRIVATE APL=0 IBM=0 LIN=1)
endif()

# ---------------------------------------------------------------- tests
if(ZOAL_GRASS_BUILD_TESTS)
    enable_testing()
    add_executable(dsf_test tests/dsf_test.cpp)
    target_link_libraries(dsf_test PRIVATE zoal_grass_core)
    add_test(NAME dsf_test COMMAND dsf_test "${CMAKE_BINARY_DIR}/dsf-test-scratch" "${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE zoal_grass_core)
    add_test(NAME core_test
             COMMAND core_test "${CMAKE_SOURCE_DIR}/resources" "${CMAKE_BINARY_DIR}/test-scratch")
endif()

# ---------------------------------------------------------------- local install (macOS dev loop)
# Lays out <XPLANE_ROOT>/Resources/plugins/zoal-grass exactly like a release.
add_custom_target(install_xplane
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XPLANE_ROOT}/Resources/plugins/zoal-grass/${ZOAL_GRASS_PLATFORM}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:zoal-grass>"
            "${XPLANE_ROOT}/Resources/plugins/zoal-grass/${ZOAL_GRASS_PLATFORM}/zoal-grass.xpl"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${XPLANE_ROOT}/Resources/plugins/zoal-grass/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources"
            "${XPLANE_ROOT}/Resources/plugins/zoal-grass/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses"
            "${XPLANE_ROOT}/Resources/plugins/zoal-grass/licenses"
    DEPENDS zoal-grass
    COMMENT "Installing into ${XPLANE_ROOT}/Resources/plugins/zoal-grass")

# ---------------------------------------------------------------- developer tool (optional)
# tools/offline_check.cpp runs the index, DSF reader, mask and object builder against a real X-Plane folder.
option(ZOAL_GRASS_BUILD_TOOLS "Build tools/offline_check" OFF)
if(ZOAL_GRASS_BUILD_TOOLS)
    add_executable(offline_check tools/offline_check.cpp)
    target_link_libraries(offline_check PRIVATE zoal_grass_core)
endif()
