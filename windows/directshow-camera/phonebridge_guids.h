#pragma once
#include <guiddef.h>

namespace phonebridge::directshow {
// Separate from the existing Media Foundation CLSID. Never register the MF
// object as a DirectShow filter: it does not implement IBaseFilter.
inline constexpr GUID kFilterClsid =
    {0x497d53e0, 0x1d46, 0x4e4b, {0xa9, 0x59, 0x88, 0xa4, 0x13, 0x13, 0x94, 0x66}};
inline constexpr wchar_t kClsidString[] = L"{497D53E0-1D46-4E4B-A959-88A413139466}";
inline constexpr wchar_t kFriendlyName[] = L"PhoneBridge (DirectShow)";
inline constexpr wchar_t kComKey[] =
    L"SOFTWARE\\Classes\\CLSID\\{497D53E0-1D46-4E4B-A959-88A413139466}";
inline constexpr wchar_t kInprocKey[] =
    L"SOFTWARE\\Classes\\CLSID\\{497D53E0-1D46-4E4B-A959-88A413139466}\\InprocServer32";
} // namespace phonebridge::directshow
