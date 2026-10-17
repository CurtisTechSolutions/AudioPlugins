# Fetches a pinned JUCE release. To build against a local checkout instead
# (faster incremental builds, offline work), configure with:
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE

include(FetchContent)

set(AP_JUCE_TAG "9.0.4" CACHE STRING "JUCE git tag to build against")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${AP_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

set(JUCE_BUILD_EXTRAS   OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(JUCE)
