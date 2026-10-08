#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "transport/adb_transport.h"
#include "connection/connection_manager.h"
#include "control/control_channel.h"
#include "diagnostics/logger.h"
#include "virtual_camera.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <windows.h>
#include <shellapi.h>

static std::atomic<bool> g_serviceRunning{true};

bool IsRunningAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin == TRUE;
}

void RequestAdminElevation() {
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, ARRAYSIZE(szPath))) {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = szPath;
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;
        if (ShellExecuteExW(&sei)) {
            ExitProcess(0);
        }
    }
}

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        std::cout << "\n[Main] Shutdown signal received. Stopping service...\n";
        g_serviceRunning = false;
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char** argv) {
    // --ui : launched by the PhoneBridge desktop app (no console window, controlled over stdin/stdout).
    bool uiMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui") == 0) uiMode = true;
    }

    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (!IsRunningAsAdmin() && uiMode) {
        // The desktop app is elevated and starts us elevated; never pop a second UAC prompt from here.
        phonebridge::control::ControlChannel::emitLine("{\"event\":\"error\",\"code\":\"not_admin\"}");
        return 2;
    }
    if (!IsRunningAsAdmin()) {
        std::cout << "[Main] PhoneBridge requires Administrator privileges to register the virtual camera in HKLM for Windows Camera Frame Server.\n";
        std::cout << "[Main] Prompting Windows UAC for elevation...\n";
        RequestAdminElevation();
        return 0;
    }

    phonebridge::Logger::log(phonebridge::LogLevel::Info, "Main", "PhoneBridge Windows Service (M3A - PhoneBridge Camera Virtual Camera)");

    phonebridge::VirtualCameraDevice virtualCamera;
    if (!virtualCamera.initialize()) {
        phonebridge::Logger::log(phonebridge::LogLevel::Warn, "Main", "Virtual camera initialization encountered fallback/error.");
        if (uiMode) phonebridge::control::ControlChannel::emitLine("{\"event\":\"error\",\"code\":\"camera_init\"}");
    }

    auto transport = std::make_unique<phonebridge::AdbTransport>("127.0.0.1", 27183, "adb");
    phonebridge::ConnectionManager connectionManager(std::move(transport));

    // UI mode: stream nothing until the desktop app says what the user wants (avoids a START/STOP glitch).
    if (uiMode) connectionManager.setWanted(false, false);

    if (!connectionManager.start()) {
        phonebridge::Logger::log(phonebridge::LogLevel::Error, "Main", "Failed to start ConnectionManager.");
        virtualCamera.shutdown();
        return 1;
    }

    std::unique_ptr<phonebridge::control::ControlChannel> control;
    if (uiMode) {
        control = std::make_unique<phonebridge::control::ControlChannel>(connectionManager, g_serviceRunning);
        control->start();
    }

    std::cout << "[Main] PhoneBridge Windows Service is RUNNING continuously ('CÃ¢mera (PhoneBridge)'). Press Ctrl+C to stop.\n\n";

    while (g_serviceRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (control) control->stop();
    connectionManager.stop();
    virtualCamera.shutdown();
    phonebridge::Logger::log(phonebridge::LogLevel::Info, "Main", "PhoneBridge Windows Service stopped cleanly.");
    std::cout.flush();
    // In UI mode a detached thread may still be blocked reading stdin. Everything has been shut down
    // explicitly above, so end the process right here instead of running destructors under that thread.
    if (uiMode) ExitProcess(0);
    return 0;
}
