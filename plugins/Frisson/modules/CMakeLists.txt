# frisson_modules: the nine knob modules, plain C++ on ap_core (no JUCE), so
# they are shared by the plugin and the frisson_tests binary.
#
# Sources are globbed so each module can be added without editing this file.

file(GLOB FRISSON_MODULE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(frisson_modules STATIC ${FRISSON_MODULE_SOURCES})
target_include_directories(frisson_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(frisson_modules PUBLIC ap_core)
target_compile_features(frisson_modules PUBLIC cxx_std_20)
set_target_properties(frisson_modules PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(frisson_modules PRIVATE /W4)
else()
    target_compile_options(frisson_modules PRIVATE -Wall -Wextra -Wpedantic)
endif()
