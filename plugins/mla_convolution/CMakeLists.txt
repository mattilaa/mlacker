cmake_minimum_required(VERSION 3.15)
project(MlaConvolution VERSION 0.1.0 LANGUAGES CXX)
if(NOT TARGET sdk)
    message(FATAL_ERROR "Build through mlacker's plugins/CMakeLists.txt")
endif()
smtg_add_vst3plugin(MlaConvolution src/plugin.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp)
target_link_libraries(MlaConvolution PRIVATE sdk)
smtg_target_set_bundle(MlaConvolution BUNDLE_IDENTIFIER org.mlang.MlaConvolution COMPANY_NAME MLang)
if(BUILD_TESTING)
    add_executable(mla_convolution_tests tests/convolution_tests.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp)
    target_link_libraries(mla_convolution_tests PRIVATE sdk sdk_hosting)
    if(APPLE)
        target_link_libraries(mla_convolution_tests PRIVATE "-framework CoreFoundation")
    endif()
    add_test(NAME convolution_processor COMMAND mla_convolution_tests)
endif()
