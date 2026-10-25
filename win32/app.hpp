// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The application: its windows, settings, shared stylesheet and message
// loop. Every open book has a window of its own.

#pragma once

#include "settings.hpp"
#include "win32_util.hpp"

#include <string>
#include <vector>

namespace geist_reader {

class ReaderWindow;

extern const wchar_t kReaderWindowClass[];
constexpr ULONG_PTR kOpenPathsMessage = 0x47454953; // WM_COPYDATA tag

class App {
public:
  static App& get();

  bool initialise(HINSTANCE instance);
  int run();

  HINSTANCE instance() const { return instance_; }
  const Settings& settings() const { return settings_; }
  void update_settings(const Settings& settings);
  const std::string& stylesheet() const { return stylesheet_; }
  HICON icon(int size) const;

  ReaderWindow* make_reader();
  // Opens each path in a window without a book, else in a new window.
  void open_paths(const std::vector<std::wstring>& paths, ReaderWindow* first = nullptr);
  void reader_destroyed(ReaderWindow* reader);
  void show_settings(HWND owner);
  void show_about(HWND owner);
  std::string next_host();
  void track_dialog(HWND dialog, bool add);

private:
  App() = default;
  bool dispatch_to_dialog(MSG& message);

  HINSTANCE instance_ = nullptr;
  Settings settings_;
  std::string stylesheet_;
  HACCEL accelerators_ = nullptr;
  std::vector<ReaderWindow*> readers_;
  std::vector<HWND> dialogs_;
  HWND settings_dialog_ = nullptr;
  unsigned hosts_ = 0;
};

} // namespace geist_reader
