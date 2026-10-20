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

# --- tests -------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(retune-core-tests test/RetuneCoreTests.cpp)
    target_link_libraries(retune-core-tests PRIVATE retune_core)
    add_test(NAME retune-core COMMAND retune-core-tests)
endif()
