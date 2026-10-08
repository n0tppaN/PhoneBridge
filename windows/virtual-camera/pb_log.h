// File logger usable from ANY process (service, and the DLL inside Frame Server).
// std::cout does not work inside Frame Server (it is a service with no console).
// Log file: C:\ProgramData\PhoneBridge\logs\phonebridge_camera.log
// The install script gives LOCAL SERVICE permission to write there.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace pb::log {

inline std::string guidToString(const GUID& g) {
    wchar_t w[64] = {};
    StringFromGUID2(g, w, 64);
    char a[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), nullptr, nullptr);
    return a;
}

inline void write(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1200];
    int n = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [pid %lu tid %lu] %s\r\n",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                     GetCurrentProcessId(), GetCurrentThreadId(), msg);
    if (n < 0) return;
    if (n >= static_cast<int>(sizeof(line))) n = static_cast<int>(sizeof(line)) - 1;

    OutputDebugStringA(line);  // visible in DebugView too
    HANDLE h = CreateFileW(L"C:\\ProgramData\\PhoneBridge\\logs\\phonebridge_camera.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, line, static_cast<DWORD>(n), &written, nullptr);
        CloseHandle(h);
    }
}

}  // namespace pb::log

#define PB_LOG(...) ::pb::log::write(__VA_ARGS__)
