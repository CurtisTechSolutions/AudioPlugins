# ap_core: header-only, framework-independent DSP shared by all plugins.
# Nothing in here may include JUCE; it is unit-tested on its own.

add_library(ap_core INTERFACE)
target_include_directories(ap_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ap_core INTERFACE cxx_std_20)
