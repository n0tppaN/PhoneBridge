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
