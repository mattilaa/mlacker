cmake_minimum_required(VERSION 3.15.0)

# One build tree for all Mla VST3 plugins: the VST3 SDK is added (and compiled)
# once here, and each plugin project joins it with add_subdirectory(). mlacker's
# mlang.toml drives it (tasks build-vst3sdk, build-plugins, test-plugins).
project(MlaPlugins
    VERSION 0.1.0
    DESCRIPTION "Mla VST3 plugins sharing one VST3 SDK build"
)

# --- Required inputs ---------------------------------------------------------
# VST3_SDK_ROOT  : the fetched Steinberg vst3sdk checkout (with submodules).
# MLA_PLUGINS    : plugin directory names under plugins/ (list or space separated).
# MLANG_ROOT     : MLang checkout providing build/libmlang_std.a.
foreach(var VST3_SDK_ROOT MLA_PLUGINS)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} must be set; build the plugins with ./build.sh --plugins.")
    endif()
endforeach()
get_filename_component(VST3_SDK_ROOT "${VST3_SDK_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(MLANG_ROOT)
    get_filename_component(MLANG_ROOT "${MLANG_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
separate_arguments(MLA_PLUGINS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build only the plug-in support from the SDK, no examples/validator/GUI.
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_DISABLE_CODE_SIGNING ON CACHE BOOL "" FORCE)
set(SMTG_BUILD_UNIVERSAL_BINARY OFF CACHE BOOL "" FORCE)

add_subdirectory("${VST3_SDK_ROOT}" "${PROJECT_BINARY_DIR}/vst3sdk")
smtg_enable_vst3_sdk()

include(CTest)

# mla_plugins builds every plugin bundle, mla_plugin_tests every test runner.
add_custom_target(mla_plugins)
add_custom_target(mla_plugin_tests)
# Each plugin's DSP object comes from its own compile-dsp task (build/obj/).
foreach(plugin IN LISTS MLA_PLUGINS)
    set(MLANG_DSP_OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/${plugin}/build/obj/${plugin}_dsp.o")
    add_subdirectory(${plugin})
    get_directory_property(plugin_targets DIRECTORY ${plugin} BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS plugin_targets)
        if(target MATCHES "_tests$")
            add_dependencies(mla_plugin_tests ${target})
        else()
            add_dependencies(mla_plugins ${target})
        endif()
    endforeach()
endforeach()
