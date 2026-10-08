# PhoneBridge - Roadmap, Improvements & Known Bugs

## 🐛 Known Bugs & Quirks
1. **UAC Elevation Requirement**: Because the virtual camera requires registering a COM In-Process Server in `HKLM` for the Windows Camera Frame Server (`svchost.exe`), `phonebridge_service.exe` must run with Administrator privileges.
2. **Process Visibility in Task Manager**: Standard user PowerShell scripts cannot directly inspect or kill elevated C++ backend processes (`phonebridge_service.exe`) via `Get-Process` without explicit elevation handling. Handled via session state tracking in the GUI.

## 🚀 Roadmap & Future Improvements
- [ ] Implement bi-directional low-latency audio feedback (PC microphone -> Android speaker).
- [ ] Add adjustable resolution & bitrate sliders in the Android Compose UI.
- [ ] Enhance web frontend (`wwwroot`) with real-time video preview rendering using WebRTC/WebSocket streams.
- [ ] Package automated installer / MSIX installer for the Windows virtual camera driver and COM registration.
