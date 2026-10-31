cmake_minimum_required(VERSION 3.22)

# version.txt is the single source of truth for the version; release-please
# bumps it in its release PR, so tagged builds always carry the tag's version.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" AP_VERSION)
string(STRIP "${AP_VERSION}" AP_VERSION)

project(CurtisAudioPlugins VERSION ${AP_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AP_BUILD_PLUGINS      "Build the JUCE plugins"                                        ON)
option(AP_BUILD_TESTS        "Build the core DSP unit tests"                                 ON)
option(AP_BUILD_STANDALONE   "Also build Standalone apps (needs ALSA dev package on Linux)"  OFF)
option(AP_COPY_PLUGINS       "Copy built plugins into the user's plugin folder after build"  OFF)
option(AP_LINUX_MINIMAL_X11  "Build without the optional X11 extension dev packages"         OFF)
option(AP_ENABLE_LTO         "Link-time optimisation for plugins (slow links; on for releases)" ON)
option(AP_BUILD_RENDER       "Build the headless editor PNG renderers (verdant_render, <Plugin>_render; needs AP_BUILD_PLUGINS)" ON)

# Universal binaries on macOS unless the caller already chose architectures.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

# Shared, framework-independent DSP (header-only).
add_subdirectory(core)

# Frisson's knob modules are plain C++ on top of core (no JUCE), built always.
add_subdirectory(plugins/Frisson/modules)

# Verdant's engine is plain C++ on top of core (no JUCE), built always.
add_subdirectory(plugins/Verdant/engine)

if(AP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tests/frisson)
    add_subdirectory(tests/verdant)
endif()

if(AP_BUILD_PLUGINS)
    include(cmake/JUCE.cmake)
    include(cmake/PluginHelpers.cmake)

    add_subdirectory(common)

    add_subdirectory(plugins/Strata)
    add_subdirectory(plugins/VoxNote)
    add_subdirectory(plugins/Intone)
    add_subdirectory(plugins/Ducker)
    add_subdirectory(plugins/Frisson)
    add_subdirectory(plugins/Verdant)

    if(AP_BUILD_RENDER)
        add_subdirectory(tools/editor_render)
    endif()

    if(AP_BUILD_TESTS)
        add_subdirectory(tests/host)
    endif()
endif()
