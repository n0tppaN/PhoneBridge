# DirectShow BaseClasses are source samples, not part of current SDK libraries.
# No download and no unverified prebuilt binary is used by this project.
find_path(PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR streams.h
    HINTS "$ENV{PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR}"
    PATH_SUFFIXES Samples/Win7Samples/multimedia/directshow/baseclasses
    DOC "Directory containing Microsoft's DirectShow BaseClasses streams.h and .cpp sources")
if(NOT PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR)
    message(FATAL_ERROR
        "DirectShow enabled but Microsoft BaseClasses sources were not found. "
        "Set -DPHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR=<directory containing streams.h>. "
        "See docs/directshow-compatibility.md for the official source and build instructions; "
        "or explicitly set -DPHONEBRIDGE_BUILD_DIRECTSHOW=OFF for an MF-only build.")
endif()

# Explicit list from Microsoft's baseclasses.vcproj; do not glob arbitrary code.
set(_pb_baseclass_files
    amextra.cpp amfilter.cpp amvideo.cpp arithutil.cpp combase.cpp cprop.cpp
    ctlutil.cpp ddmm.cpp dllentry.cpp dllsetup.cpp mtype.cpp outputq.cpp
    perflog.cpp pstream.cpp pullpin.cpp refclock.cpp renbase.cpp schedule.cpp
    seekpt.cpp source.cpp strmctl.cpp sysclock.cpp transfrm.cpp transip.cpp
    videoctl.cpp vtrans.cpp winctrl.cpp winutil.cpp wxdebug.cpp wxlist.cpp wxutil.cpp)
set(_pb_baseclass_sources)
foreach(_file IN LISTS _pb_baseclass_files)
    if(NOT EXISTS "${PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR}/${_file}")
        message(FATAL_ERROR "Incomplete Microsoft BaseClasses source tree: missing ${_file}")
    endif()
    list(APPEND _pb_baseclass_sources "${PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR}/${_file}")
endforeach()
add_library(phonebridge_strmbase STATIC ${_pb_baseclass_sources})
target_include_directories(phonebridge_strmbase PUBLIC "${PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR}")
target_compile_definitions(phonebridge_strmbase PUBLIC UNICODE _UNICODE
    PRIVATE _LIB $<$<CONFIG:Debug>:DEBUG>)
# Match the official sample's /MD and /MDd. MF keeps its existing /MT CRT.
set_target_properties(phonebridge_strmbase PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    OUTPUT_NAME strmbase OUTPUT_NAME_DEBUG strmbasd)
# Old Microsoft samples predate /permissive-; keep their original compatibility mode.
target_compile_options(phonebridge_strmbase PRIVATE /W3 /EHsc /permissive)
target_link_libraries(phonebridge_strmbase PUBLIC strmiids ole32 oleaut32 uuid winmm advapi32)
message(STATUS "Building strmbase/strmbasd from: ${PHONEBRIDGE_DIRECTSHOW_BASECLASSES_DIR}")
