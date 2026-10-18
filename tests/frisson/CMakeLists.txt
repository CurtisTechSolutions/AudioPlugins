# frisson_tests: unit tests for the Frisson knob modules (JUCE-free).
# Test sources are globbed: drop a test_<module>.cpp in this folder.

file(GLOB FRISSON_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

add_executable(frisson_tests main.cpp ${FRISSON_TEST_SOURCES})
target_include_directories(frisson_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(frisson_tests PRIVATE frisson_modules ap_core)

if(MSVC)
    target_compile_options(frisson_tests PRIVATE /W4)
else()
    target_compile_options(frisson_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME frisson_module_tests COMMAND frisson_tests)
