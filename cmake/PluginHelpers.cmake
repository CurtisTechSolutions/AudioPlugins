# ap_add_plugin(<target>
#     PRODUCT_NAME "Name"          # display name
#     PLUGIN_CODE  Xxxx            # 4 chars, exactly one upper-case letter
#     DESCRIPTION  "..."
#     [IS_SYNTH] [NEEDS_MIDI_INPUT] [NEEDS_MIDI_OUTPUT] [IS_MIDI_EFFECT]
#     [VST3_CATEGORIES ...]
#     SOURCES ...)
#
# Wraps juce_add_plugin with the settings shared by every plugin in this repo.

function(ap_add_plugin target)
    set(options        IS_SYNTH NEEDS_MIDI_INPUT NEEDS_MIDI_OUTPUT IS_MIDI_EFFECT)
    set(oneValueArgs   PRODUCT_NAME PLUGIN_CODE DESCRIPTION)
    set(multiValueArgs VST3_CATEGORIES SOURCES)
    cmake_parse_arguments(AP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach(flag IN LISTS options)
        if(AP_${flag})
            set(AP_${flag} TRUE)
        else()
            set(AP_${flag} FALSE)
        endif()
    endforeach()

    set(formats VST3)
    if(AP_BUILD_STANDALONE)
        list(APPEND formats Standalone)
    endif()

    if(NOT AP_VST3_CATEGORIES)
        if(AP_IS_SYNTH)
            set(AP_VST3_CATEGORIES Instrument Synth)
        else()
            set(AP_VST3_CATEGORIES Fx)
        endif()
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME                 "${AP_PRODUCT_NAME}"
        COMPANY_NAME                 "Curtis Tech Solutions"
        COMPANY_WEBSITE              "https://curtistechsolutions.com"
        BUNDLE_ID                    "com.curtistechsolutions.${target}"
        PLUGIN_MANUFACTURER_CODE     Ctsl
        PLUGIN_CODE                  ${AP_PLUGIN_CODE}
        DESCRIPTION                  "${AP_DESCRIPTION}"
        FORMATS                      ${formats}
        IS_SYNTH                     ${AP_IS_SYNTH}
        NEEDS_MIDI_INPUT             ${AP_NEEDS_MIDI_INPUT}
        NEEDS_MIDI_OUTPUT            ${AP_NEEDS_MIDI_OUTPUT}
        IS_MIDI_EFFECT               ${AP_IS_MIDI_EFFECT}
        EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
        COPY_PLUGIN_AFTER_BUILD      ${AP_COPY_PLUGINS}
        NEEDS_WEB_BROWSER            FALSE
        NEEDS_CURL                   FALSE
        VST3_CATEGORIES              ${AP_VST3_CATEGORIES})

    target_sources(${target} PRIVATE ${AP_SOURCES})

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

    if(AP_LINUX_MINIMAL_X11 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PUBLIC
            JUCE_USE_XRANDR=0
            JUCE_USE_XINERAMA=0
            JUCE_USE_XCURSOR=0
            JUCE_USE_XINPUT=0)
    endif()

    target_link_libraries(${target}
        PRIVATE
            ap_core
            ap_common
            juce::juce_audio_processors
            juce::juce_audio_plugin_client
            juce::juce_dsp
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    if(AP_ENABLE_LTO)
        target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
    endif()

    if(AP_BUILD_STANDALONE)
        target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_audio_devices)
    endif()
endfunction()
