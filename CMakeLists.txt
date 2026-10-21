cmake_minimum_required(VERSION 3.17)

project(Retune VERSION 0.1.0 LANGUAGES CXX)

# The core is linked into the .clap, a shared module (M2), so it must be position-independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- uw-core (git submodule): only uw::ump, no CLAP host, JACK or ALSA ---------------------
# RETUNE_UW_CORE_DIR points at another uw-core clone (e.g. ~/Dev-Common/uw-core) to build
# against a change that isn't pushed and pinned yet.
set(RETUNE_UW_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/uw-core" CACHE PATH "uw-core source")
set(UW_CORE_UMP_ONLY ON CACHE BOOL "" FORCE)
set(UW_CORE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${RETUNE_UW_CORE_DIR} uw-core)

# --- core: Retune's rules over uw::ump, no CLAP ----------------------------------------------
add_library(retune_core STATIC src/RetuneCore.cpp)
target_include_directories(retune_core PUBLIC src)
target_link_libraries(retune_core PUBLIC uw::ump)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(retune_core PRIVATE -Wall -Wextra -Wconversion)
endif()

# --- the .clap ---------------------------------------------------------------------------------
# Hidden visibility, so clap_entry (CLAP_EXPORT in plugin.cpp) is the only exported symbol and two
# plugins with different uw-core pins can't bind to each other's copies.
option(RETUNE_DIALECT_LOG "Log the host's note dialects and incoming note events (diagnostic)" OFF)

set_target_properties(retune_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
add_library(Retune MODULE src/plugin.cpp src/ClapNotes.cpp)
target_include_directories(Retune PRIVATE external/clap/include)
target_link_libraries(Retune PRIVATE retune_core)
target_compile_definitions(Retune PRIVATE RETUNE_VERSION_STRING="${PROJECT_VERSION}"
    $<$<BOOL:${RETUNE_DIALECT_LOG}>:RETUNE_DIALECT_LOG>)
set_target_properties(Retune PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(Retune PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    set_target_properties(Retune PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
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
