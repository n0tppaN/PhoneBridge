cmake_minimum_required(VERSION 3.20)
project(phonebridge_windows CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/service
    ${CMAKE_CURRENT_SOURCE_DIR}/service/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/service/connection
    ${CMAKE_CURRENT_SOURCE_DIR}/service/diagnostics
    ${CMAKE_CURRENT_SOURCE_DIR}/service/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/service/video
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera
    ${CMAKE_CURRENT_SOURCE_DIR}/audio-driver
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

# ---------------------------------------------------------------------------
# Media source DLL - loaded by the Windows Frame Server (LOCAL SERVICE), not by our service.
# The .def file is REQUIRED: without it DllGetClassObject etc. are not exported.
# Static CRT: no dependency on the VC++ redistributable inside the Frame Server process.
# ---------------------------------------------------------------------------
add_library(phonebridge_mediasource SHARED
    virtual-camera/dllmain.cpp
    virtual-camera/phonebridge_media_source.cpp
    virtual-camera/phonebridge_media_stream.cpp
    virtual-camera/pb_ks_guids.cpp
    virtual-camera/phonebridge_mediasource.def
)
set_target_properties(phonebridge_mediasource PROPERTIES PREFIX "")
set_property(TARGET phonebridge_mediasource PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Service executable. NOTE: the media source / stream sources are no longer compiled in here.
# ---------------------------------------------------------------------------
add_executable(phonebridge_service
    service/main.cpp
    service/transport/adb_transport.cpp
    service/connection/connection_manager.cpp
    service/diagnostics/logger.cpp
    service/audio/audio_receiver.cpp
    service/audio/audio_ipc.cpp
    service/video/video_receiver.cpp
    service/video/h264_decoder.cpp
    ../protocol/packet_parser.cpp
    virtual-camera/virtual_camera.cpp
    audio-driver/audio_endpoint.cpp
)

if(WIN32)
    target_link_libraries(phonebridge_mediasource PRIVATE mfplat mfuuid ole32 uuid advapi32 ksuser)
    target_link_libraries(phonebridge_service PRIVATE ws2_32 mfplat mfuuid mf mfsensorgroup ole32 advapi32 shlwapi)
endif()

# Separate legacy capture source: OBS enumerates DirectShow, not IMFMediaSource.
option(PHONEBRIDGE_BUILD_DIRECTSHOW "Build the x64 DirectShow camera for OBS" ON)
if(PHONEBRIDGE_BUILD_DIRECTSHOW)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "PhoneBridge DirectShow requires Windows MSVC x64 (-A x64).")
    endif()
    include(cmake/DirectShowBaseClasses.cmake)
    add_library(phonebridge_directshow SHARED
        directshow-camera/phonebridge_filter.cpp
        directshow-camera/dllmain.cpp
        directshow-camera/phonebridge_directshow.def)
    set_target_properties(phonebridge_directshow PROPERTIES PREFIX ""
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_compile_definitions(phonebridge_directshow PRIVATE NOMINMAX UNICODE _UNICODE
        $<$<CONFIG:Debug>:DEBUG>)
    target_compile_options(phonebridge_directshow PRIVATE /W4 /EHsc)
    target_link_libraries(phonebridge_directshow PRIVATE phonebridge_strmbase)

    add_executable(phonebridge_dshow_diagnostic directshow-camera/diagnostic.cpp)
    target_compile_definitions(phonebridge_dshow_diagnostic PRIVATE NOMINMAX UNICODE _UNICODE)
    target_link_libraries(phonebridge_dshow_diagnostic PRIVATE strmiids ole32 oleaut32 uuid)
    target_compile_options(phonebridge_dshow_diagnostic PRIVATE /W4 /EHsc)
endif()

enable_testing()
add_executable(nv12_to_yuy2_tests ../tests/video/nv12_to_yuy2_tests.cpp)
target_include_directories(nv12_to_yuy2_tests PRIVATE directshow-camera)
add_test(NAME nv12_to_yuy2 COMMAND nv12_to_yuy2_tests)
