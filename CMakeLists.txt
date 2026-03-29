cmake_minimum_required(VERSION 3.16)
project(flyv-connector-xplane VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── X-Plane SDK ───────────────────────────────────────────────────────────────
# Download the SDK from https://developer.x-plane.com/sdk/plugin-sdk/
# and point CMake to it:
#   cmake -DXPLANE_SDK_PATH=/path/to/XPSDK .. -B build
set(XPLANE_SDK_PATH "" CACHE PATH "Path to the X-Plane Plugin SDK root directory")

if(NOT XPLANE_SDK_PATH OR NOT EXISTS "${XPLANE_SDK_PATH}")
    message(FATAL_ERROR
        "XPLANE_SDK_PATH is not set or does not exist.\n"
        "Download the X-Plane SDK from https://developer.x-plane.com/sdk/plugin-sdk/\n"
        "then re-run CMake with -DXPLANE_SDK_PATH=/path/to/SDK")
endif()

# ── Source files ──────────────────────────────────────────────────────────────
set(FLYV_SOURCES
    src/main.cpp
    src/WebSocketClient.cpp
)

# ── Plugin target (shared library with .xpl extension) ────────────────────────
add_library(flyv-connector-xplane SHARED ${FLYV_SOURCES})

set_target_properties(flyv-connector-xplane PROPERTIES
    PREFIX  ""           # no "lib" prefix
    SUFFIX  ".xpl"
    OUTPUT_NAME "FlyV-Connector-XPlane"
)

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(flyv-connector-xplane PRIVATE
    src/
    "${XPLANE_SDK_PATH}/CHeaders/XPLM"
    "${XPLANE_SDK_PATH}/CHeaders/Widgets"
    "${XPLANE_SDK_PATH}/CHeaders/Wrappers"
)

# ── Platform-specific configuration ──────────────────────────────────────────
if(WIN32)
    # ── Windows ───────────────────────────────────────────────────────────
    target_compile_definitions(flyv-connector-xplane PRIVATE
        IBM=1
        XPLM200=1
        XPLM210=1
        XPLM300=1
        XPLM301=1
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(flyv-connector-xplane PRIVATE
        "${XPLANE_SDK_PATH}/Libraries/Win/XPLM_64.lib"
        ws2_32
    )
    # Ensure we produce a 64-bit DLL (X-Plane 11+ requires 64-bit)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "X-Plane plugins must be built as 64-bit binaries.")
    endif()

elseif(APPLE)
    # ── macOS ─────────────────────────────────────────────────────────────
    target_compile_definitions(flyv-connector-xplane PRIVATE
        APL=1
        XPLM200=1
        XPLM210=1
        XPLM300=1
        XPLM301=1
    )
    target_link_libraries(flyv-connector-xplane PRIVATE
        "-F${XPLANE_SDK_PATH}/Libraries/Mac"
        "-framework XPLM"
        "-framework XPWidgets"
        "-framework CoreFoundation"
    )
    # Universal binary (Intel + Apple Silicon)
    set_target_properties(flyv-connector-xplane PROPERTIES
        OSX_ARCHITECTURES "x86_64;arm64"
    )

else()
    message(FATAL_ERROR "Unsupported platform. Only Windows and macOS are supported.")
endif()

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(flyv-connector-xplane PRIVATE /W3 /wd4996)
else()
    target_compile_options(flyv-connector-xplane PRIVATE
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

# ── Install (optional) ────────────────────────────────────────────────────────
install(TARGETS flyv-connector-xplane
    LIBRARY DESTINATION "."
    RUNTIME DESTINATION "."
)
