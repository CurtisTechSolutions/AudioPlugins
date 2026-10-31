# ap_common: JUCE-dependent code shared by every plugin (look & feel, generic
# parameter panels, meters, parameter-layout helpers).
#
# It is an INTERFACE target on purpose: JUCE modules are compiled into whichever
# target links them, so sharing a static library would duplicate symbols. Each
# plugin compiles these few files itself.

add_library(ap_common INTERFACE)

target_include_directories(ap_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(ap_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ParamPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Meters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EditorBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PresetBar.cpp)

target_link_libraries(ap_common INTERFACE ap_core)
