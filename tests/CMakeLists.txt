add_executable(ap_tests
    main.cpp
    test_scales.cpp
    test_pitch.cpp
    test_notes.cpp
    test_shifter.cpp
    test_synthesis.cpp
    test_dynamics.cpp)

target_link_libraries(ap_tests PRIVATE ap_core)

if(MSVC)
    target_compile_options(ap_tests PRIVATE /W4)
else()
    target_compile_options(ap_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME ap_core_tests COMMAND ap_tests)
