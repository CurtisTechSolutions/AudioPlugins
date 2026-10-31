# verdant_engine: Verdant's DSP, modulation, presets and analysis, plain C++
# on ap_core (no JUCE), shared by the plugin, verdant_tests and verdant_render.
#
# Sources are globbed so each engine part can be added without editing this
# file. Headers include each other as "engine/<Name>.h" or "<Name>.h".

file(GLOB VERDANT_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(verdant_engine STATIC ${VERDANT_ENGINE_SOURCES})
target_include_directories(verdant_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(verdant_engine PUBLIC ap_core)
target_compile_features(verdant_engine PUBLIC cxx_std_20)
set_target_properties(verdant_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(verdant_engine PRIVATE /W4)
else()
    target_compile_options(verdant_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
