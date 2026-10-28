# verdant_tests: unit tests for the Verdant engine (JUCE-free).
# Test sources are globbed: drop a test_<area>.cpp in this folder.

file(GLOB VERDANT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

add_executable(verdant_tests main.cpp ${VERDANT_TEST_SOURCES})
target_include_directories(verdant_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(verdant_tests PRIVATE verdant_engine ap_core)

find_package(Threads REQUIRED)
target_link_libraries(verdant_tests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(verdant_tests PRIVATE /W4)
else()
    target_compile_options(verdant_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME verdant_tests COMMAND verdant_tests)
set_tests_properties(verdant_tests PROPERTIES TIMEOUT 600)
