cmake_minimum_required(VERSION 3.22)

# --------------------------------------------------------------------------------------
# These two must be set BEFORE project(), not after.
#
# project() runs the compiler checks that bake in the target architecture. Setting
# CMAKE_OSX_ARCHITECTURES afterwards is accepted without complaint, appears in the cache,
# is echoed back by every `cmake -L`, and produces an arm64-only binary anyway. The build
# log gives no hint. `lipo -archs` on the built binary is the only thing that will tell
# you, so check that and not the log.
# --------------------------------------------------------------------------------------
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")

project(Proximate VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Proximate
    COMPANY_NAME "Stoatworks Labs"
    BUNDLE_ID "com.stoatworks.proximate"
    PLUGIN_MANUFACTURER_CODE Alsg
    PLUGIN_CODE Prx1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Proximate"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Fx" "Dynamics" "EQ"
    AU_MAIN_TYPE "kAudioUnitType_Effect"
    # The standalone exists to try the plugin on a real microphone, so it must be
    # able to open one and to ask for permission to.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Proximate processes the microphone so you can hear what it does to a close voice."
)

juce_generate_juce_header(Proximate)

# --------------------------------------------------------------------------------------
# The DSP depends on nothing but the standard library, on purpose: it is built again as
# a plain library below so the test suite and the analysis tool run without JUCE, an
# audio device or a plugin host. Keep it that way — if something here starts needing
# juce_core, it belongs in a different directory.
# --------------------------------------------------------------------------------------
set(PROXIMATE_CORE_SOURCES
    Source/DSP/Compensator.cpp
)

target_sources(Proximate
    PRIVATE
        ${PROXIMATE_CORE_SOURCES}
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginParameters.cpp
)

# The standalone target only: a custom JUCEApplication that opens no audio input on
# its first run. See Source/StandaloneApp.cpp for why that matters.
if(TARGET Proximate_Standalone)
    target_sources(Proximate_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(Proximate_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_compile_definitions(Proximate
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Proximate
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --------------------------------------------------------------------------------------
# Tests and the analysis tool. Plain executables over the dependency-free core; `ctest`
# runs the test.
# --------------------------------------------------------------------------------------
enable_testing()

add_library(proximate_core STATIC ${PROXIMATE_CORE_SOURCES})
target_include_directories(proximate_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_compensator tests/test_compensator.cpp)
target_link_libraries(test_compensator PRIVATE proximate_core)
add_test(NAME compensator COMMAND test_compensator)

add_executable(proxstat tools/proxstat.cpp)
target_link_libraries(proxstat PRIVATE proximate_core)

# The host harness: loads the built VST3 or AU the way a DAW does and runs a file
# through it, so the plugin the host sees can be compared with the bare compensator.
# Not a ctest — it needs the bundles to exist first — and it is the one tool here that
# needs JUCE.
juce_add_console_app(proxhost PRODUCT_NAME "proxhost")
target_sources(proxhost PRIVATE tools/proxhost.cpp)
target_compile_definitions(proxhost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)
target_link_libraries(proxhost
    PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The frame renderer for the demo video: the plugin's own editor painted over a file,
# offline. It links the plugin's sources directly — a hosted editor is a foreign native
# view proxhost's JUCE cannot paint — so the same defines the plugin builds with are set
# here by hand, minimally.
juce_add_console_app(proxrender PRODUCT_NAME "proxrender")
target_sources(proxrender PRIVATE
    tools/proxrender.cpp
    ${PROXIMATE_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginParameters.cpp
)
target_compile_definitions(proxrender PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_VersionString="${PROJECT_VERSION}"
)
target_link_libraries(proxrender
    PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
