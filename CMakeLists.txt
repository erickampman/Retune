cmake_minimum_required(VERSION 3.21)   # clap-wrapper needs 3.21

# macOS: 26 or later, Apple Silicon only. Set before project();
# ignored on other platforms. Both are cache variables, so -D overrides them.
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "macOS 26 or later")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Apple Silicon only")
# C for DPF's pugl and NanoVG sources (the editor).
project(Retune VERSION 0.9.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)   # DPF's DistrhoUI_macOS.mm and our BackingScale_mac.mm
endif()

# The core is linked into the .clap, a shared module (M2), so it must be position-independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- uw-core (git submodule): only uw::ump, no CLAP host, JACK or ALSA ---------------------
# RETUNE_UW_CORE_DIR points at another uw-core clone (e.g. ../uw-core) to build
# against a change that isn't pushed and pinned yet.
set(RETUNE_UW_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/uw-core" CACHE PATH "uw-core source")
set(UW_CORE_UMP_ONLY ON CACHE BOOL "" FORCE)
set(UW_CORE_TESTS OFF CACHE BOOL "" FORCE)
# uw::clap-notes (ClapNoteRemap, the CLAP note dialect) builds against Retune's own CLAP SDK.
set(UW_CORE_CLAP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/clap/include" CACHE PATH "" FORCE)
add_subdirectory(${RETUNE_UW_CORE_DIR} uw-core)

# --- core: Retune's rules over uw::ump, no CLAP ----------------------------------------------
add_library(retune_core STATIC src/RetuneCore.cpp)
target_include_directories(retune_core PUBLIC src)
target_link_libraries(retune_core PUBLIC uw::ump)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(retune_core PRIVATE -Wall -Wextra -Wconversion)
endif()

# --- DPF, only for the editor (src/UI) -------------------------------------------------------
# UIExporter + NanoVG + pugl, as NoteGran and StruckSynth use it: the CLAP entry point stays ours.
# dgl-opengl is what dpf_add_plugin() would make; SHARED_RESOURCES embeds DGL's DejaVu Sans.
add_subdirectory(external/dpf)
dpf__add_dgl_opengl(TRUE FALSE FALSE)
target_compile_options(dgl-opengl PRIVATE -w)   # vendored: its warnings aren't ours
# Retune's own namespace, as every DPF user's (UmpireDAW's docs/decisions.md, "DPF fork"): on macOS
# pugl names its Objective-C classes after it, and every DPF plugin built with the default ("DGL")
# registers the same ones, so two in one host would collide. Harmless on Linux.
target_compile_definitions(dgl-opengl PUBLIC DGL_NAMESPACE=RetuneDGL)

# resources/* compiled in as bytes with DPF's res2c.py: the logo, read with NanoVG at run time, and
# the help page, which the editor's Help button writes out and opens. Not help.md: res2c.py names
# each symbol by the file's stem, so it would collide with help.html.
#
# help.html is generated from help.md (beside it) by pandoc with the Unlikelyware products' shared
# template, and committed; after editing help.md, run from here:
#   pandoc -f commonmark_x-smart -t html5 \
#     --template ../../Dev-Audio/Shared/Help/help-template.html \
#     --metadata pagetitle=help-Retune resources/help.md -o resources/help.html
# (the template, in Dev-Audio beside Dev-MIDI, inlines its help.css).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(RETUNE_RESOURCES_HPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.hpp")
set(RETUNE_RESOURCES_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.cpp")
file(GLOB RETUNE_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/*.png"
    "${CMAKE_SOURCE_DIR}/resources/*.html")
add_custom_command(
    OUTPUT "${RETUNE_RESOURCES_HPP}" "${RETUNE_RESOURCES_CPP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/generated/resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated/resources"
    COMMAND ${CMAKE_COMMAND} -E copy ${RETUNE_RESOURCE_FILES} "${CMAKE_BINARY_DIR}/generated/resources"
    COMMAND ${Python3_EXECUTABLE} "${DPF_ROOT_DIR}/utils/res2c.py"
            EmbeddedResources "${CMAKE_BINARY_DIR}/generated/resources" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS ${RETUNE_RESOURCE_FILES} "${DPF_ROOT_DIR}/utils/res2c.py"
    COMMENT "Embedding resources/* as compiled-in data")

# --- the .clap ---------------------------------------------------------------------------------
# Hidden visibility, so clap_entry (CLAP_EXPORT in plugin.cpp) is the only exported symbol and two
# plugins with different uw-core pins can't bind to each other's copies.
option(RETUNE_DIALECT_LOG "Log the host's note dialects and incoming note events (diagnostic)" OFF)

set_target_properties(retune_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# The plugin's own sources, compiled into each binary: the .clap and every clap-wrapper format.
# The editor (src/UI, DPF + NanoVG) goes into each too.
function(retune_add_plugin_sources TARGET_NAME)
    target_sources(${TARGET_NAME} PRIVATE src/plugin.cpp src/UI/RetuneUI.cpp "${RETUNE_RESOURCES_CPP}"
        "${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp" "${DPF_ROOT_DIR}/distrho/src/DistrhoUtils.cpp")
    target_include_directories(${TARGET_NAME} PRIVATE external/clap/include src src/UI
        "${CMAKE_BINARY_DIR}/generated" "${DPF_ROOT_DIR}/distrho")
    target_link_libraries(${TARGET_NAME} PRIVATE retune_core uw::clap-notes dgl-opengl)
    target_compile_definitions(${TARGET_NAME} PRIVATE RETUNE_VERSION_STRING="${PROJECT_VERSION}"
        DISTRHO_PLUGIN_TARGET_CLAP DISTRHO_IS_STANDALONE=0
        $<$<BOOL:${RETUNE_DIALECT_LOG}>:RETUNE_DIALECT_LOG>)
    dpf__add_plugin_specific_ui_sources(${TARGET_NAME} FALSE)   # macOS: DistrhoUI_macOS.mm
    if(APPLE)
        target_sources(${TARGET_NAME} PRIVATE src/UI/BackingScale_mac.mm)   # the window's live Retina scale
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        set_source_files_properties(src/plugin.cpp TARGET_DIRECTORY ${TARGET_NAME}
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
        # DPF's own sources reach pugl.hpp through private headers; mute only gcc's <cstdbool> #warning.
        # DGL's headers aren't pedantic (an anonymous struct in Color), and clap-wrapper builds its
        # targets with -Wpedantic -Werror, so the editor's sources turn pedantic off again.
        set_source_files_properties(src/UI/RetuneUI.cpp TARGET_DIRECTORY ${TARGET_NAME}
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-pedantic;$<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>")
        set_source_files_properties("${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp"
            "${DPF_ROOT_DIR}/distrho/src/DistrhoUtils.cpp" "${DPF_ROOT_DIR}/distrho/DistrhoUI_macOS.mm"
            TARGET_DIRECTORY ${TARGET_NAME}
            PROPERTIES COMPILE_OPTIONS "-Wno-pedantic;$<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>")
    endif()
endfunction()

add_library(Retune MODULE)
retune_add_plugin_sources(Retune)
# DGL's libraries keep default visibility, so the linker is told too: clap_entry is the only export
# (another plugin's DPF or uw-core in the same host can't bind to ours).
if(APPLE)
    target_link_options(Retune PRIVATE "LINKER:-exported_symbol,_clap_entry")
elseif(UNIX)
    file(WRITE "${CMAKE_BINARY_DIR}/generated/exports.map" "{ global: clap_entry; local: *; };\n")
    target_link_options(Retune PRIVATE "LINKER:--version-script=${CMAKE_BINARY_DIR}/generated/exports.map")
endif()

if(APPLE)
    set_target_properties(Retune PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/clap-Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unlikelyware.retune"
        MACOSX_BUNDLE_BUNDLE_NAME "Retune"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(RETUNE_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    set(RETUNE_CLAP_ARTIFACT "$<TARGET_BUNDLE_DIR:Retune>")
else()
    set_target_properties(Retune PROPERTIES PREFIX "" SUFFIX ".clap")
    set(RETUNE_CLAP_DIR "$ENV{HOME}/.clap")
    set(RETUNE_CLAP_ARTIFACT "$<TARGET_FILE:Retune>")
endif()

# A separate target, as in the synths, so an ordinary build never writes outside the build tree.
add_custom_target(install-clap
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RETUNE_CLAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${RETUNE_CLAP_DIR}/Retune.clap"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${RETUNE_CLAP_ARTIFACT}" "${RETUNE_CLAP_DIR}/Retune.clap"
    DEPENDS Retune
    VERBATIM
    COMMENT "Symlinking Retune.clap into ${RETUNE_CLAP_DIR}")

# --- VST3 / AUv2 via clap-wrapper (M4) --------------------------------------------------------
# The same plugin, wrapped. Only the MIDI 1.0 (rounded) behaviour survives: the AUv2 wrapper turns
# MIDI 2.0 output into MIDI 1.0, and the VST3 wrapper passes note on/off only, with tuning 0
# (docs/design.md, "What clap-wrapper does with a note effect"). clap-wrapper v0.16.0, as in PlainOldSynth.
option(RETUNE_BUILD_VST3 "Build Retune.vst3 via clap-wrapper" ON)
option(RETUNE_BUILD_AUV2 "Build Retune.component (AUv2 MIDI effect) via clap-wrapper (macOS)" ${APPLE})

if(RETUNE_BUILD_AUV2 AND NOT APPLE)
    message(FATAL_ERROR "RETUNE_BUILD_AUV2 is macOS-only. Reconfigure with -DRETUNE_BUILD_AUV2=OFF.")
endif()

if(RETUNE_BUILD_VST3 OR RETUNE_BUILD_AUV2)
    if(APPLE)
        enable_language(OBJC)   # the wrappers' Objective-C sources (C and Objective-C++ are on already)
    endif()
    set(CLAP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/clap" CACHE PATH "" FORCE)
    add_subdirectory(external/clap-wrapper EXCLUDE_FROM_ALL)
endif()

if(RETUNE_BUILD_VST3)
    set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH "" FORCE)
    if(UNIX AND NOT APPLE)
        # clap-wrapper's Linux branch tests a misspelled TCLP_RESOURCE_DIRECTORY and warns
        # without it (as in PlainOldSynth).
        set(TCLP_RESOURCE_DIRECTORY "")
    endif()
    add_library(Retune_vst3 MODULE)
    retune_add_plugin_sources(Retune_vst3)
    target_add_vst3_wrapper(
        TARGET Retune_vst3
        OUTPUT_NAME "Retune"
        BUNDLE_IDENTIFIER "com.unlikelyware.retune.vst3"
        BUNDLE_VERSION "${PROJECT_VERSION}")
    if(TARGET base-sdk-vst3 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # The VST3 SDK still uses std::wstring_convert, deprecated since C++17.
        target_compile_options(base-sdk-vst3 PRIVATE -Wno-deprecated-declarations)
    endif()

    if(APPLE)
        set(RETUNE_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
        set(RETUNE_VST3_ARTIFACT "$<TARGET_BUNDLE_DIR:Retune_vst3>")
    else()
        set(RETUNE_VST3_DIR "$ENV{HOME}/.vst3")
        # The bundle folder, not the .so inside it. clap-wrapper puts it in Debug/ or Release/
        # (wrap_vst3.cmake, v3root_dor).
        set(RETUNE_VST3_ARTIFACT "${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Retune.vst3")
    endif()
    add_custom_target(install-vst3
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RETUNE_VST3_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${RETUNE_VST3_DIR}/Retune.vst3"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${RETUNE_VST3_ARTIFACT}" "${RETUNE_VST3_DIR}/Retune.vst3"
        DEPENDS Retune_vst3
        VERBATIM
        COMMENT "Symlinking Retune.vst3 into ${RETUNE_VST3_DIR}")
endif()

if(RETUNE_BUILD_AUV2)
    # clap-wrapper fetches Apple's AudioUnitSDK itself unless AUDIOUNIT_SDK_ROOT names a copy.
    if(NOT AUDIOUNIT_SDK_ROOT)
        set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON)
    endif()
    add_library(Retune_auv2 MODULE)
    retune_add_plugin_sources(Retune_auv2)
    # "aumi": a MIDI effect (Logic's MIDI FX slot). "Rtun" under Unlikelyware's "nLKL".
    target_add_auv2_wrapper(
        TARGET Retune_auv2
        OUTPUT_NAME "Retune"
        BUNDLE_IDENTIFIER "com.unlikelyware.retune.auv2"
        BUNDLE_VERSION "${PROJECT_VERSION}"
        MANUFACTURER_NAME "Unlikelyware"
        MANUFACTURER_CODE "nLKL"
        SUBTYPE_CODE "Rtun"
        INSTRUMENT_TYPE "aumi")

    # Copied, not symlinked, as in PlainOldSynth.
    set(RETUNE_AUV2_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    add_custom_target(install-auv2
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RETUNE_AUV2_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${RETUNE_AUV2_DIR}/Retune.component"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_BUNDLE_DIR:Retune_auv2>" "${RETUNE_AUV2_DIR}/Retune.component"
        DEPENDS Retune_auv2
        VERBATIM
        COMMENT "Copying Retune.component into ${RETUNE_AUV2_DIR}")
endif()

# --- tests -------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(retune-core-tests test/RetuneCoreTests.cpp)
    target_link_libraries(retune-core-tests PRIVATE retune_core)
    add_test(NAME retune-core COMMAND retune-core-tests)

    # Drives the built .clap over the CLAP C API: no DAW, no GUI.
    add_executable(retune-clap-tests test/RetuneClapTests.cpp)
    target_include_directories(retune-clap-tests PRIVATE external/clap/include)
    target_link_libraries(retune-clap-tests PRIVATE uw::ump ${CMAKE_DL_LIBS})
    add_dependencies(retune-clap-tests Retune)
    add_test(NAME retune-clap COMMAND retune-clap-tests "${RETUNE_CLAP_ARTIFACT}")
endif()
