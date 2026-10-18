ap_add_plugin(Frisson
    PRODUCT_NAME    "Frisson"
    PLUGIN_CODE     Frsn
    DESCRIPTION     "Reward-system sound enhancer: nine knobs named for what they do to the listener"
    VST3_CATEGORIES Fx Mastering
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp)

target_link_libraries(Frisson PRIVATE frisson_modules)
target_include_directories(Frisson PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
