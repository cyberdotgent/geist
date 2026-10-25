// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "dialogs.hpp"

#include "app.hpp"
#include "resource.h"

#include "geist/version.hpp"

#include <commctrl.h>
#include <shlwapi.h>

#include <algorithm>
#include <set>
#include <vector>

namespace geist_reader {
namespace {

const int kSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32};

void fill_sizes(HWND combo, int selected) {
  for (int size : kSizes) {
    const std::wstring text = std::to_wstring(size);
    const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), size);
    if (size == selected) SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
  }
}

int selected_size(HWND combo, int fallback) {
  const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR) return fallback;
  return static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0));
}

std::wstring combo_text(HWND combo) {
  const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR) return {};
  const auto length = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&text[0]));
  text.resize(static_cast<std::size_t>(length));
  return text;
}

void show_association_state(HWND dialog) {
  SetDlgItemTextW(dialog, IDC_ASSOCIATION_STATE,
                  file_type_registered() ? L".boo files open with this reader for this user."
                                         : L".boo files are not registered to this reader.");
}

INT_PTR CALLBACK settings_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM) {
  switch (message) {
  case WM_INITDIALOG: {
    const Settings& settings = App::get().settings();
    HWND family = GetDlgItem(dialog, IDC_FONT_FAMILY);
    std::set<std::wstring> names;
    HDC screen = GetDC(nullptr);
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(
        screen, &query,
        [](const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM data) -> int {
          if (font->lfFaceName[0] != L'@')
            reinterpret_cast<std::set<std::wstring>*>(data)->insert(font->lfFaceName);
          return 1;
        },
        reinterpret_cast<LPARAM>(&names), 0);
    ReleaseDC(nullptr, screen);
    names.insert(settings.font_family);
    for (const auto& name : names)
      SendMessageW(family, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(family, CB_SELECTSTRING, static_cast<WPARAM>(-1),
                 reinterpret_cast<LPARAM>(settings.font_family.c_str()));
    fill_sizes(GetDlgItem(dialog, IDC_TOC_SIZE), settings.contents_size);
    fill_sizes(GetDlgItem(dialog, IDC_TOPIC_SIZE), settings.topic_size);
    show_association_state(dialog);
    return TRUE;
  }
  case WM_COMMAND:
    switch (LOWORD(wparam)) {
    case IDC_FONT_FAMILY:
    case IDC_TOC_SIZE:
    case IDC_TOPIC_SIZE:
      if (HIWORD(wparam) == CBN_SELCHANGE) {
        Settings settings = App::get().settings();
        const std::wstring family = combo_text(GetDlgItem(dialog, IDC_FONT_FAMILY));
        if (!family.empty()) settings.font_family = family;
        settings.contents_size = selected_size(GetDlgItem(dialog, IDC_TOC_SIZE), settings.contents_size);
        settings.topic_size = selected_size(GetDlgItem(dialog, IDC_TOPIC_SIZE), settings.topic_size);
        App::get().update_settings(settings);
      }
      return TRUE;
    case IDC_REGISTER:
    case IDC_UNREGISTER: {
      std::wstring error;
      const bool ok = LOWORD(wparam) == IDC_REGISTER ? register_file_type(error) : unregister_file_type(error);
      if (!ok) show_error(dialog, L"File Association", error);
      show_association_state(dialog);
      return TRUE;
    }
    case IDOK:
    case IDCANCEL:
      DestroyWindow(dialog);
      return TRUE;
    default:
      return FALSE;
    }
  case WM_DESTROY:
    App::get().track_dialog(dialog, false);
    return FALSE;
  default:
    return FALSE;
  }
}

std::wstring grouped(std::uint64_t value) {
  const std::wstring digits = std::to_wstring(value);
  wchar_t separator[8] = L",";
  GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, 8);
  std::wstring out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0) out += separator;
    out += digits[i];
  }
  return out;
}

struct InformationState {
  const BookInformation* information = nullptr;
  std::vector<HFONT> fonts;
  std::set<HWND> secondary;
  HBRUSH background = nullptr;
};

HWND add_text(HWND dialog, const std::wstring& text, HFONT font, int x, int y, int width, bool selectable,
              int& height) {
  HDC dc = GetDC(dialog);
  HGDIOBJ previous = SelectObject(dc, font);
  RECT bounds{0, 0, width, 0};
  DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  SelectObject(dc, previous);
  ReleaseDC(dialog, dc);
  height = bounds.bottom - bounds.top;
  HWND control = selectable
      ? CreateWindowExW(0, WC_EDITW, text.c_str(), WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY, x, y,
                        width, height, dialog, nullptr, App::get().instance(), nullptr)
      : CreateWindowExW(0, WC_STATICW, text.c_str(), WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y, width, height,
                        dialog, nullptr, App::get().instance(), nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
  return control;
}

INT_PTR CALLBACK information_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<InformationState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
  case WM_INITDIALOG: {
    state = reinterpret_cast<InformationState*>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    const BookInformation& info = *state->information;
    const UINT dpi = window_dpi(dialog);
    HFONT title_font = create_ui_font(dpi, 158, FW_SEMIBOLD);
    HFONT heading_font = create_ui_font(dpi, 108, FW_SEMIBOLD);
    HFONT body_font = create_ui_font(dpi, 108);
    HFONT small_font = create_ui_font(dpi, 100);
    state->fonts = {title_font, heading_font, body_font, small_font};
    state->background = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    const int pad = scale(24, dpi);
    int y = pad, height = 0;
    const int icon = scale(64, dpi);
    HWND image = CreateWindowExW(0, WC_STATICW, nullptr, WS_CHILD | WS_VISIBLE | SS_ICON | SS_REALSIZEIMAGE, pad, y,
                                 icon, icon, dialog, nullptr, App::get().instance(), nullptr);
    SendMessageW(image, STM_SETICON, reinterpret_cast<WPARAM>(App::get().icon(icon)), 0);
    const int text_x = pad + icon + scale(16, dpi);
    const int text_width = scale(470, dpi);
    const std::wstring file = info.file.filename().wstring();
    std::wstring title = widen(!info.title.empty() ? info.title : info.short_title);
    if (title.empty()) title = file;
    int text_y = y;
    add_text(dialog, title, title_font, text_x, text_y, text_width, true, height);
    text_y += height + scale(4, dpi);
    if (!info.short_title.empty() && widen(info.short_title) != title) {
      state->secondary.insert(add_text(dialog, widen(info.short_title), body_font, text_x, text_y, text_width, true, height));
      text_y += height + scale(4, dpi);
    }
    state->secondary.insert(add_text(dialog, file, small_font, text_x, text_y, text_width, true, height));
    text_y += height;
    y = std::max(y + icon, text_y) + scale(22, dpi);

    const int label_width = scale(140, dpi);
    const int value_x = pad + label_width + scale(14, dpi);
    const int value_width = scale(390, dpi);
    auto section = [&](const wchar_t* heading, const std::vector<std::pair<std::wstring, std::wstring>>& rows) {
      if (rows.empty()) return;
      state->secondary.insert(add_text(dialog, heading, heading_font, pad, y, label_width + value_width, false, height));
      y += height + scale(10, dpi);
      for (const auto& row : rows) {
        int label_height = 0;
        state->secondary.insert(add_text(dialog, row.first, body_font, pad, y, label_width, false, label_height));
        add_text(dialog, row.second, body_font, value_x, y, value_width, true, height);
        y += std::max(label_height, height) + scale(9, dpi);
      }
      y += scale(22 - 9, dpi);
    };
    std::vector<std::pair<std::wstring, std::wstring>> publication;
    const std::pair<const wchar_t*, const std::string*> fields[] = {
        {L"Author", &info.authors},           {L"Document number", &info.document_number},
        {L"Date", &info.date},                {L"Language", &info.language},
        {L"Version", &info.version},          {L"Build version", &info.build_version},
        {L"Security", &info.security},        {L"Copyright", &info.copyright}};
    for (const auto& field : fields)
      if (!field.second->empty()) publication.push_back({field.first, widen(*field.second)});
    section(L"Publication", publication);
    section(L"Contents", {{L"Topics", grouped(info.topic_count)},
                          {L"Stored objects", grouped(info.resource_count)},
                          {L"Pages", grouped(info.page_count)}});
    wchar_t size_text[64] = L"";
    StrFormatByteSizeW(static_cast<LONGLONG>(info.file_size), size_text, 64);
    section(L"File", {{L"Name", file},
                      {L"Size", std::wstring(size_text) + L" (" + grouped(info.file_size) + L" bytes)"},
                      {L"Location", info.file.parent_path().wstring()}});

    HWND done = GetDlgItem(dialog, IDOK);
    RECT button{};
    GetWindowRect(done, &button);
    const int button_width = button.right - button.left, button_height = button.bottom - button.top;
    const int width = scale(640, dpi);
    const int total = y + button_height + scale(20, dpi);
    MoveWindow(done, width - pad - button_width, y, button_width, button_height, FALSE);
    RECT frame{0, 0, width, total};
    AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_EXSTYLE)));
    RECT owner{};
    GetWindowRect(GetParent(dialog), &owner);
    const int frame_width = frame.right - frame.left, frame_height = frame.bottom - frame.top;
    SetWindowPos(dialog, nullptr, owner.left + (owner.right - owner.left - frame_width) / 2,
                 owner.top + std::max(0L, (owner.bottom - owner.top - frame_height) / 3), frame_width, frame_height,
                 SWP_NOZORDER);
    SetFocus(done);
    return FALSE;
  }
  case WM_CTLCOLORSTATIC: {
    HDC dc = reinterpret_cast<HDC>(wparam);
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(dc, state && state->secondary.count(reinterpret_cast<HWND>(lparam)) ? GetSysColor(COLOR_GRAYTEXT)
                                                                                      : GetSysColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<INT_PTR>(state ? state->background : GetSysColorBrush(COLOR_BTNFACE));
  }
  case WM_COMMAND:
    if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) EndDialog(dialog, 0);
    return TRUE;
  default:
    return FALSE;
  }
}

} // namespace

HWND create_settings_dialog(HWND owner) {
  HWND dialog = CreateDialogParamW(App::get().instance(), MAKEINTRESOURCEW(IDD_SETTINGS), owner, settings_proc, 0);
  if (dialog) {
    App::get().track_dialog(dialog, true);
    ShowWindow(dialog, SW_SHOW);
  }
  return dialog;
}

void show_book_information(HWND owner, const BookInformation& information) {
  InformationState state;
  state.information = &information;
  DialogBoxParamW(App::get().instance(), MAKEINTRESOURCEW(IDD_INFORMATION), owner, information_proc,
                  reinterpret_cast<LPARAM>(&state));
  for (HFONT font : state.fonts) DeleteObject(font);
  if (state.background) DeleteObject(state.background);
}

void show_about_dialog(HWND owner) {
  std::wstring text = L"Geist BOO Reader " + widen(GEIST_READER_VERSION);
  if (std::string(GEIST_READER_REVISION) != "unknown") text += L" (" + widen(GEIST_READER_REVISION) + L")";
  text += L"\nlibgeist " + widen(geist::library_version());
  text += L"\n\nA reader for IBM BookManager BOO books.\n\nCopyright 2026 Yvan Janssens.\n"
          L"Licensed under the Apache License, Version 2.0. The program includes libpng, zlib and "
          L"giflib, used under their own licences; see Help > Licences.";
  MSGBOXPARAMSW box{};
  box.cbSize = sizeof box;
  box.hwndOwner = owner;
  box.hInstance = App::get().instance();
  box.lpszText = text.c_str();
  box.lpszCaption = L"About Geist BOO Reader";
  box.dwStyle = MB_OK | MB_USERICON;
  box.lpszIcon = MAKEINTRESOURCEW(IDI_APP);
  MessageBoxIndirectW(&box);
}

namespace {
INT_PTR CALLBACK licenses_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM) {
  if (message == WM_INITDIALOG) {
    HINSTANCE instance = App::get().instance();
    std::string text;
    if (HRSRC found = FindResourceW(instance, MAKEINTRESOURCEW(IDR_LICENSE), MAKEINTRESOURCEW(10)))
      if (HGLOBAL loaded = LoadResource(instance, found))
        if (const void* data = LockResource(loaded))
          text.assign(static_cast<const char*>(data), SizeofResource(instance, found));
    std::wstring wide;
    for (wchar_t c : widen(text)) {
      if (c == L'\r') continue;
      if (c == L'\n') wide += L'\r';
      wide += c;
    }
    SetDlgItemTextW(dialog, IDC_LICENSE_TEXT, wide.c_str());
    return TRUE;
  }
  if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
    EndDialog(dialog, 0);
    return TRUE;
  }
  return FALSE;
}
} // namespace

void show_licenses_dialog(HWND owner) {
  DialogBoxParamW(App::get().instance(), MAKEINTRESOURCEW(IDD_LICENSES), owner, licenses_proc, 0);
}

} // namespace geist_reader
