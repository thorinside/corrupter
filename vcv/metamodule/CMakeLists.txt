cmake_minimum_required(VERSION 3.22)

# Standalone MetaModule build of the Corrupter VCV plugin (brand "Corrupter").
# The published multi-module build lives in the NoSuchDevice repo; this one
# exists so the wrapper can be cross-compiled and checked from this repo.
if(DEFINED ENV{METAMODULE_SDK_DIR} AND NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
endif()
if(NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../metamodule-plugin-sdk")
endif()
if(NOT EXISTS "${METAMODULE_SDK_DIR}/plugin.cmake")
    message(FATAL_ERROR "MetaModule SDK not found at ${METAMODULE_SDK_DIR}. "
        "Clone https://github.com/4ms/metamodule-plugin-sdk (with submodules) and set METAMODULE_SDK_DIR.")
endif()
include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(CorrupterMetaModulePlugin VERSION 2.1.0 DESCRIPTION "Corrupter for MetaModule" LANGUAGES C CXX ASM)

set(VCV_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(DSP_ROOT ${VCV_ROOT}/..)

add_library(Corrupter STATIC)

target_sources(Corrupter PRIVATE
    ${VCV_ROOT}/src/plugin.cpp
    ${VCV_ROOT}/src/Corrupter.cpp
    ${DSP_ROOT}/src/engine.cpp
    ${DSP_ROOT}/src/c_api.cpp
    ${DSP_ROOT}/src/pitch_quantizer.cpp
    ${DSP_ROOT}/src/internal/clock_engine.cpp
    ${DSP_ROOT}/src/internal/corrupt_engine.cpp
)

target_include_directories(Corrupter PRIVATE
    ${VCV_ROOT}/src
    ${DSP_ROOT}/include
    ${DSP_ROOT}/src
)

target_compile_options(Corrupter PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_property(TARGET Corrupter PROPERTY CXX_STANDARD 20)

if(NOT DEFINED INSTALL_DIR)
    set(INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/dist)
endif()

create_plugin(
    SOURCE_LIB      Corrupter
    PLUGIN_NAME     Corrupter
    PLUGIN_JSON     ${VCV_ROOT}/plugin.json
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION     ${INSTALL_DIR}
)
