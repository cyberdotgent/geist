// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Geist BOO Reader for Windows: a native Win32 consumer of libgeist that
// shows topics in the system's MSHTML WebBrowser control.

#include "app.hpp"
#include "reader_window.hpp"

#include "geist/version.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <shellapi.h>

#include <string>
#include <vector>

using namespace geist_reader;

namespace {

// --version, written to the standard output this GUI program was given or
// to the console it was started from.
int print_version() {
  std::string text = std::string("geist-boo-reader/") + GEIST_READER_VERSION;
  if (std::string(GEIST_READER_REVISION) != "unknown") text += std::string(" (") + GEIST_READER_REVISION + ")";
  text += std::string(" libgeist/") + geist::library_version();
  if (std::string(geist::library_revision()) != "unknown")
    text += std::string(" (") + geist::library_revision() + ")";
  text += "\n";
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!out || out == INVALID_HANDLE_VALUE) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) out = GetStdHandle(STD_OUTPUT_HANDLE);
  }
  DWORD written = 0;
  if (out && out != INVALID_HANDLE_VALUE)
    WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  return 0;
}

// Hands the paths to a reader already running for this user, so a
// double-clicked book opens as a window of that process.
bool forward_to_running(const std::vector<std::wstring>& paths) {
  HWND running = FindWindowW(kReaderWindowClass, nullptr);
  if (!running) return false;
  std::wstring payload;
  for (const auto& path : paths) {
    payload += path;
    payload += L'\0';
  }
  COPYDATASTRUCT data{};
  data.dwData = kOpenPathsMessage;
  data.cbData = static_cast<DWORD>(payload.size() * sizeof(wchar_t));
  data.lpData = payload.empty() ? nullptr : &payload[0];
  DWORD process = 0;
  GetWindowThreadProcessId(running, &process);
  AllowSetForegroundWindow(process);
  DWORD_PTR result = 0;
  return SendMessageTimeoutW(running, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000,
                             &result) &&
         result == 1;
}

std::wstring full_path(const std::wstring& path) {
  const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (!length) return path;
  std::wstring out(length, L'\0');
  const DWORD written = GetFullPathNameW(path.c_str(), length, &out[0], nullptr);
  out.resize(written);
  return out;
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  int count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::wstring> paths;
  for (int i = 1; i < count; ++i) {
    const std::wstring argument = arguments[i];
    if (argument == L"--version" || argument == L"-V") {
      LocalFree(arguments);
      return print_version();
    }
    if (argument == L"--new-instance") continue;
    paths.push_back(full_path(argument));
  }
  bool new_instance = false;
  for (int i = 1; i < count; ++i)
    if (std::wstring(arguments[i]) == L"--new-instance") new_instance = true;
  LocalFree(arguments);
  if (!new_instance && forward_to_running(paths)) return 0;

  INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES |
                                                     ICC_WIN95_CLASSES};
  InitCommonControlsEx(&controls);
  if (FAILED(OleInitialize(nullptr))) return 1;
  App& app = App::get();
  int status = 1;
  if (app.initialise(instance)) {
    if (paths.empty()) app.make_reader();
    else app.open_paths(paths);
    status = app.run();
  }
  OleUninitialize();
  return status;
}
