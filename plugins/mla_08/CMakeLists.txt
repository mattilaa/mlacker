cmake_minimum_required(VERSION 3.15.0)

project(Mla08
    VERSION 0.1.0
    DESCRIPTION "Mla 08 - MLang VST3 TR-808 style drum machine"
)

# --- Required inputs ---------------------------------------------------------
# VST3_SDK_ROOT     : a fetched Steinberg vst3sdk checkout (with submodules).
# MLANG_DSP_OBJECT  : the compiled object of src/mla_08_dsp.mla.
# MLANG_STD_LIBRARY : the MLang runtime archive (libmlang_std.a). Defaults to
#                     the repository's build/ output; override for other trees.
if(NOT DEFINED VST3_SDK_ROOT)
    message(FATAL_ERROR "VST3_SDK_ROOT must point to the fetched vst3sdk checkout.")
endif()
if(NOT DEFINED MLANG_DSP_OBJECT)
    message(FATAL_ERROR "MLANG_DSP_OBJECT must point to the compiled MLang DSP object.")
endif()

# MLANG_ROOT        : MLang checkout (defaults to mlacker's subprojects/mlang).
if(NOT MLANG_ROOT)
    set(MLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../subprojects/mlang")
endif()
get_filename_component(MLANG_REPO_ROOT "${MLANG_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT DEFINED MLANG_STD_LIBRARY)
    set(MLANG_STD_LIBRARY "${MLANG_REPO_ROOT}/build/libmlang_std.a")
endif()
# Resolve to an absolute path (relative overrides are interpreted against the
# source dir, not the CMake build dir where the linker later runs).
get_filename_component(MLANG_STD_LIBRARY "${MLANG_STD_LIBRARY}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${MLANG_STD_LIBRARY}")
    message(FATAL_ERROR "MLang runtime archive not found: ${MLANG_STD_LIBRARY}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
# Inside plugins/CMakeLists.txt the SDK is already added (and built once)
# for all plugins; a standalone build adds its own copy.
if(NOT TARGET sdk)
    # Build only the plug-in target from the SDK, no examples/validator/GUI.
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(SMTG_ADD_VST3_UTILITIES ON CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_DISABLE_CODE_SIGNING ON CACHE BOOL "" FORCE)
    set(SMTG_BUILD_UNIVERSAL_BINARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
    smtg_enable_vst3_sdk()
endif()

get_filename_component(MLANG_DSP_OBJECT_FILE "${MLANG_DSP_OBJECT}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# plugins/CMakeLists.txt (target mla_plugins) configures before the DSP
# objects are compiled, so it cannot require them yet.
if(NOT TARGET mla_plugins AND NOT EXISTS "${MLANG_DSP_OBJECT_FILE}")
    message(FATAL_ERROR "Resolved MLANG_DSP_OBJECT does not exist: ${MLANG_DSP_OBJECT_FILE}")
endif()
set_source_files_properties("${MLANG_DSP_OBJECT_FILE}"
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

smtg_add_vst3plugin(Mla08
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    src/plugin.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

target_include_directories(Mla08 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Mla08 PRIVATE sdk "${MLANG_STD_LIBRARY}")

# The MLang runtime uses lock-free atomics; on Linux link libatomic and the
# usual runtime companions the static archive may reference.
if(NOT APPLE AND NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(Mla08 PRIVATE atomic Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

smtg_target_set_bundle(Mla08
    BUNDLE_IDENTIFIER org.mlang.Mla08
    COMPANY_NAME MLang
)

# Offline functional tests: load the built bundle through the SDK hosting
# classes and check rendered audio. Run with `mlang pkg --config mlang.toml run test`.
option(MLA_08_BUILD_TESTS "Build the Mla 08 offline functional tests" ON)
if(MLA_08_BUILD_TESTS)
    if(APPLE)
        set(MLA_08_MODULE_LOADER ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    elseif(WIN32)
        set(MLA_08_MODULE_LOADER ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
    else()
        set(MLA_08_MODULE_LOADER ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
    endif()
    add_executable(mla_08_tests tests/mla_08_tests.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${MLA_08_MODULE_LOADER})
    target_link_libraries(mla_08_tests PRIVATE sdk_hosting)
    if(APPLE)
        set_source_files_properties(${MLA_08_MODULE_LOADER} PROPERTIES COMPILE_FLAGS -fobjc-arc)
        target_link_libraries(mla_08_tests PRIVATE "-framework Cocoa" "-framework CoreFoundation")
    else()
        target_link_libraries(mla_08_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_dependencies(mla_08_tests Mla08)
    include(CTest)
    add_test(NAME m08_processor COMMAND mla_08_tests "$<TARGET_BUNDLE_DIR:Mla08>")
endif()
