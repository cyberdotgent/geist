// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "win32_util.hpp"

namespace geist_reader {

std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], length);
  return out;
}

std::string narrow(const std::wstring& utf16) {
  if (utf16.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                                         nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), &out[0], length,
                      nullptr, nullptr);
  return out;
}

std::wstring variant_string(const VARIANT& value) {
  const VARIANT* current = &value;
  while (current->vt == (VT_BYREF | VT_VARIANT) && current->pvarVal) current = current->pvarVal;
  if (current->vt == VT_BSTR && current->bstrVal)
    return std::wstring(current->bstrVal, SysStringLen(current->bstrVal));
  if (current->vt == (VT_BYREF | VT_BSTR) && current->pbstrVal && *current->pbstrVal)
    return std::wstring(*current->pbstrVal, SysStringLen(*current->pbstrVal));
  return {};
}

UINT window_dpi(HWND window) {
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  static const auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
  if (get_dpi && window) {
    const UINT dpi = get_dpi(window);
    if (dpi) return dpi;
  }
  HDC screen = GetDC(nullptr);
  const UINT dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
  ReleaseDC(nullptr, screen);
  return dpi ? dpi : 96;
}

HFONT create_ui_font(UINT dpi, int percent, int weight) {
  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof metrics;
  using ForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
  static const auto for_dpi = reinterpret_cast<ForDpiFn>(reinterpret_cast<void*>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi")));
  LOGFONTW font{};
  if (for_dpi && for_dpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi)) {
    font = metrics.lfMessageFont;
  } else if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0)) {
    font = metrics.lfMessageFont;
    HDC screen = GetDC(nullptr);
    font.lfHeight = MulDiv(font.lfHeight, static_cast<int>(dpi), GetDeviceCaps(screen, LOGPIXELSY));
    ReleaseDC(nullptr, screen);
  } else {
    font.lfHeight = -scale(12, dpi);
    lstrcpynW(font.lfFaceName, L"Segoe UI", LF_FACESIZE);
  }
  font.lfHeight = MulDiv(font.lfHeight, percent, 100);
  font.lfWeight = weight;
  return CreateFontIndirectW(&font);
}

void show_error(HWND owner, const std::wstring& title, const std::wstring& message) {
  OutputDebugStringW((L"Geist BOO Reader: " + title + L": " + message + L"\n").c_str());
  MessageBoxW(owner, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

} // namespace geist_reader
