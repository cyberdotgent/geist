// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The reader's dialogs: settings, book information, about and the Print
// Book progress window.

#pragma once

#include "book_source.hpp"
#include "win32_util.hpp"

#include <functional>
#include <string>

namespace geist_reader {

// Modeless; changes apply to every window as they are made.
HWND create_settings_dialog(HWND owner);
// Modal to `owner`.
void show_book_information(HWND owner, const BookInformation& information);
void show_about_dialog(HWND owner);
// The embedded licence notices (this program and its static libraries).
void show_licenses_dialog(HWND owner);

// A modeless progress window with a Cancel button that calls `cancel`.
class ProgressDialog {
public:
  ProgressDialog(HWND owner, std::function<void()> cancel);
  ~ProgressDialog();
  ProgressDialog(const ProgressDialog&) = delete;
  ProgressDialog& operator=(const ProgressDialog&) = delete;
  void set_text(const std::wstring& text);
  void set_progress(std::size_t done, std::size_t total);

private:
  static INT_PTR CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  std::function<void()> cancel_;
};

} // namespace geist_reader
