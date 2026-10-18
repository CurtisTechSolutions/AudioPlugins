cmake_minimum_required(VERSION 3.22)

project(CurtisAudioPlugins VERSION 0.1.0 LANGUAGES C CXX)

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

if(AP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tests/frisson)
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

    if(AP_BUILD_TESTS)
        add_subdirectory(tests/host)
    endif()
endif()
