// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "app.hpp"

#include "dialogs.hpp"
#include "protocol.hpp"
#include "reader_window.hpp"
#include "resource.h"

#include <algorithm>

namespace geist_reader {
namespace {

std::string resource_text(HINSTANCE instance, int id) {
  HRSRC found = FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
  if (!found) return {};
  HGLOBAL loaded = LoadResource(instance, found);
  const void* data = loaded ? LockResource(loaded) : nullptr;
  if (!data) return {};
  return std::string(static_cast<const char*>(data), SizeofResource(instance, found));
}

HACCEL build_accelerators() {
  ACCEL table[] = {
      {FVIRTKEY | FCONTROL, 'N', ID_FILE_NEW_WINDOW},
      {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN},
      {FVIRTKEY | FCONTROL, 'W', ID_FILE_CLOSE_WINDOW},
      {FVIRTKEY | FCONTROL, 'C', ID_EDIT_COPY},
      {FVIRTKEY | FCONTROL, 'A', ID_EDIT_SELECT_ALL},
      {FVIRTKEY | FCONTROL, VK_OEM_COMMA, ID_EDIT_SETTINGS},
      {FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_VIEW_LARGER},
      {FVIRTKEY | FCONTROL, VK_ADD, ID_VIEW_LARGER},
      {FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_VIEW_SMALLER},
      {FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_VIEW_SMALLER},
      {FVIRTKEY | FALT, VK_LEFT, ID_NAV_BACK},
      {FVIRTKEY | FALT, VK_RIGHT, ID_NAV_FORWARD},
      {FVIRTKEY | FCONTROL, VK_OEM_4, ID_NAV_BACK},
      {FVIRTKEY | FCONTROL, VK_OEM_6, ID_NAV_FORWARD},
      {FVIRTKEY | FCONTROL, 'F', ID_SEARCH_BOOK},
  };
  return CreateAcceleratorTableW(table, static_cast<int>(sizeof table / sizeof table[0]));
}

} // namespace

App& App::get() {
  static App app;
  return app;
}

bool App::initialise(HINSTANCE instance) {
  instance_ = instance;
  settings_ = load_settings();
  stylesheet_ = resource_text(instance, IDR_STYLESHEET) + "\n" + resource_text(instance, IDR_IE_STYLESHEET);
  accelerators_ = build_accelerators();
  configure_browser_features();
  if (!register_protocol()) {
    show_error(nullptr, L"Geist BOO Reader", L"The geist: URL handler could not be registered.");
    return false;
  }
  return ReaderWindow::register_class(instance);
}

HICON App::icon(int size) const {
  return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, LR_SHARED));
}

void App::update_settings(const Settings& settings) {
  settings_ = settings;
  save_settings(settings_);
  for (ReaderWindow* reader : readers_) reader->settings_changed();
}

ReaderWindow* App::make_reader() {
  ReaderWindow* reader = ReaderWindow::create();
  if (reader) readers_.push_back(reader);
  return reader;
}

void App::open_paths(const std::vector<std::wstring>& paths, ReaderWindow* first) {
  for (const auto& path : paths) {
    ReaderWindow* reader = first;
    first = nullptr;
    if (!reader)
      for (ReaderWindow* candidate : readers_)
        if (!candidate->has_book()) {
          reader = candidate;
          break;
        }
    if (!reader) reader = make_reader();
    if (!reader) return;
    reader->open_book(path);
    SetForegroundWindow(reader->hwnd());
  }
}

void App::reader_destroyed(ReaderWindow* reader) {
  readers_.erase(std::remove(readers_.begin(), readers_.end(), reader), readers_.end());
  if (readers_.empty()) PostQuitMessage(0);
}

void App::show_settings(HWND owner) {
  if (settings_dialog_ && IsWindow(settings_dialog_)) {
    SetForegroundWindow(settings_dialog_);
    return;
  }
  settings_dialog_ = create_settings_dialog(owner);
}

void App::show_about(HWND owner) { show_about_dialog(owner); }

std::string App::next_host() { return "b" + std::to_string(++hosts_); }

void App::track_dialog(HWND dialog, bool add) {
  dialogs_.erase(std::remove(dialogs_.begin(), dialogs_.end(), dialog), dialogs_.end());
  if (add) dialogs_.push_back(dialog);
  if (!add && dialog == settings_dialog_) settings_dialog_ = nullptr;
}

bool App::dispatch_to_dialog(MSG& message) {
  for (HWND dialog : dialogs_)
    if (IsWindow(dialog) && IsDialogMessageW(dialog, &message)) return true;
  return false;
}

int App::run() {
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (dispatch_to_dialog(message)) continue;
    HWND root = GetAncestor(message.hwnd, GA_ROOT);
    ReaderWindow* reader = nullptr;
    for (ReaderWindow* candidate : readers_)
      if (candidate->hwnd() == root) reader = candidate;
    if (reader) {
      if (TranslateAcceleratorW(reader->hwnd(), accelerators_, &message)) continue;
      if (reader->pre_translate(message)) continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  // Close whatever is still open (File > Exit).
  for (ReaderWindow* reader : std::vector<ReaderWindow*>(readers_)) DestroyWindow(reader->hwnd());
  return static_cast<int>(message.wParam);
}

} // namespace geist_reader
