add_executable(ap_tests
    main.cpp
    test_scales.cpp
    test_pitch.cpp
    test_notes.cpp
    test_shifter.cpp
    test_synthesis.cpp
    test_dynamics.cpp
    test_core_extras.cpp
    test_queue.cpp)

find_package(Threads REQUIRED)
target_link_libraries(ap_tests PRIVATE ap_core Threads::Threads)

if(MSVC)
    target_compile_options(ap_tests PRIVATE /W4)
else()
    target_compile_options(ap_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME ap_core_tests COMMAND ap_tests)
