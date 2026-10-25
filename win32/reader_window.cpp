// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "reader_window.hpp"

#include "app.hpp"
#include "dialogs.hpp"
#include "protocol.hpp"
#include "resource.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <thread>

namespace geist_reader {

const wchar_t kReaderWindowClass[] = L"GeistBOOReaderWindow";

namespace {

const wchar_t kEmptyClass[] = L"GeistBOOReaderEmpty";
const wchar_t kAppTitle[] = L"Geist BOO Reader";
constexpr UINT kSearchResults = WM_APP + 1;
constexpr UINT kOpenLater = WM_APP + 4;
constexpr int kToolbarHeight = 40;
constexpr int kSplitterWidth = 5;

struct SearchPayload {
  unsigned generation;
  std::vector<SearchHit> hits;
};

bool font_installed(const wchar_t* family) {
  HDC screen = GetDC(nullptr);
  LOGFONTW font{};
  font.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(font.lfFaceName, family, LF_FACESIZE);
  bool found = false;
  EnumFontFamiliesExW(
      screen, &font,
      [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM data) -> int {
        *reinterpret_cast<bool*>(data) = true;
        return 0;
      },
      reinterpret_cast<LPARAM>(&found), 0);
  ReleaseDC(nullptr, screen);
  return found;
}

// Locale-aware, case- and diacritic-insensitive search over UTF-8 text,
// answering with a UTF-8 byte offset.
std::optional<std::size_t> locale_matcher(const std::string& haystack, const std::string& needle) {
  const std::wstring wide_haystack = widen(haystack);
  const std::wstring wide_needle = widen(needle);
  if (wide_needle.empty() || wide_haystack.empty()) return std::nullopt;
  int found_length = 0;
  const int index = FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,
                                    FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC,
                                    wide_haystack.c_str(), static_cast<int>(wide_haystack.size()),
                                    wide_needle.c_str(), static_cast<int>(wide_needle.size()), &found_length,
                                    nullptr, nullptr, 0);
  if (index < 0) return ascii_matcher(haystack, needle);
  return narrow(wide_haystack.substr(0, static_cast<std::size_t>(index))).size();
}

bool has_boo_extension(const std::wstring& path) {
  const auto dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos) return false;
  return CompareStringOrdinal(path.c_str() + dot, -1, L".boo", -1, TRUE) == CSTR_EQUAL;
}

std::wstring css_string(const std::wstring& value) {
  std::wstring out = L"'";
  for (wchar_t c : value) {
    if (c == L'\'' || c == L'\\') out += L'\\';
    out += c;
  }
  return out + L"'";
}

HMENU build_menu() {
  HMENU bar = CreateMenu();
  auto popup = [&](const wchar_t* title) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), title);
    return menu;
  };
  HMENU file = popup(L"&File");
  AppendMenuW(file, MF_STRING, ID_FILE_NEW_WINDOW, L"&New Window\tCtrl+N");
  AppendMenuW(file, MF_STRING, ID_FILE_OPEN, L"&Open Book…\tCtrl+O");
  AppendMenuW(file, MF_STRING, ID_FILE_CLOSE_BOOK, L"Close &Book");
  AppendMenuW(file, MF_STRING, ID_FILE_CLOSE_WINDOW, L"&Close Window\tCtrl+W");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"E&xit");
  HMENU edit = popup(L"&Edit");
  AppendMenuW(edit, MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
  AppendMenuW(edit, MF_STRING, ID_EDIT_SELECT_ALL, L"Select &All\tCtrl+A");
  AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(edit, MF_STRING, ID_EDIT_SETTINGS, L"&Settings…\tCtrl+,");
  HMENU view = popup(L"&View");
  AppendMenuW(view, MF_STRING, ID_VIEW_SIDEBAR, L"Show or Hide &Sidebar");
  AppendMenuW(view, MF_STRING, ID_VIEW_LARGER, L"&Larger Topic Text\tCtrl++");
  AppendMenuW(view, MF_STRING, ID_VIEW_SMALLER, L"S&maller Topic Text\tCtrl+-");
  HMENU navigate = popup(L"&Navigate");
  AppendMenuW(navigate, MF_STRING, ID_NAV_CONTENTS, L"&Table of Contents");
  AppendMenuW(navigate, MF_STRING, ID_NAV_BACK, L"&Back\tAlt+Left");
  AppendMenuW(navigate, MF_STRING, ID_NAV_FORWARD, L"&Forward\tAlt+Right");
  AppendMenuW(navigate, MF_STRING, ID_NAV_PREVIOUS, L"&Previous Topic");
  AppendMenuW(navigate, MF_STRING, ID_NAV_NEXT, L"&Next Topic");
  HMENU search = popup(L"&Search");
  AppendMenuW(search, MF_STRING, ID_SEARCH_BOOK, L"Search &Book\tCtrl+F");
  HMENU help = popup(L"&Help");
  AppendMenuW(help, MF_STRING, ID_HELP_INFORMATION, L"Book &Information");
  AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&About Geist BOO Reader…");
  return bar;
}

} // namespace

bool ReaderWindow::register_class(HINSTANCE instance) {
  WNDCLASSEXW window{};
  window.cbSize = sizeof window;
  window.lpfnWndProc = window_proc;
  window.hInstance = instance;
  window.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  window.lpszClassName = kReaderWindowClass;
  window.hIcon = App::get().icon(GetSystemMetrics(SM_CXICON));
  window.hIconSm = App::get().icon(GetSystemMetrics(SM_CXSMICON));
  if (!RegisterClassExW(&window)) return false;
  WNDCLASSEXW empty{};
  empty.cbSize = sizeof empty;
  empty.lpfnWndProc = empty_proc;
  empty.hInstance = instance;
  empty.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  empty.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  empty.lpszClassName = kEmptyClass;
  return RegisterClassExW(&empty) != 0;
}

ReaderWindow* ReaderWindow::create() {
  auto* reader = new ReaderWindow();
  HWND hwnd = CreateWindowExW(0, kReaderWindowClass, kAppTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1200, 820, nullptr, build_menu(),
                              App::get().instance(), reader);
  if (!hwnd) {
    delete reader;
    return nullptr;
  }
  const UINT dpi = window_dpi(hwnd);
  RECT frame{};
  GetWindowRect(hwnd, &frame);
  SetWindowPos(hwnd, nullptr, 0, 0, scale(1200, dpi), scale(820, dpi), SWP_NOMOVE | SWP_NOZORDER);
  ShowWindow(hwnd, SW_SHOWNORMAL);
  UpdateWindow(hwnd);
  return reader;
}

ReaderWindow::~ReaderWindow() {
  for (HFONT font : {ui_font_, glyph_font_, hint_font_, tree_font_, detail_font_})
    if (font) DeleteObject(font);
}

LRESULT CALLBACK ReaderWindow::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  ReaderWindow* reader = nullptr;
  if (message == WM_NCCREATE) {
    reader = static_cast<ReaderWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    reader->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reader));
  } else {
    reader = reinterpret_cast<ReaderWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!reader) return DefWindowProcW(hwnd, message, wparam, lparam);
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    delete reader;
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return reader->handle(message, wparam, lparam);
}

LRESULT CALLBACK ReaderWindow::empty_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    auto font = reinterpret_cast<HFONT>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(dc, L"Drop a BOO book here, or choose File > Open Book…", -1, &client,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previous) SelectObject(dc, previous);
    EndPaint(hwnd, &paint);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK ReaderWindow::search_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR, DWORD_PTR data) {
  auto* reader = reinterpret_cast<ReaderWindow*>(data);
  if (message == WM_KEYDOWN && wparam == VK_RETURN) {
    reader->run_search();
    return 0;
  }
  if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
    SetWindowTextW(hwnd, L"");
    return 0;
  }
  if (message == WM_CHAR && (wparam == L'\r' || wparam == 27)) return 0;
  if (message == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, message, wparam, lparam);
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

void ReaderWindow::build() {
  dpi_ = window_dpi(hwnd_);
  HDC screen = GetDC(nullptr);
  system_dpi_ = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
  ReleaseDC(nullptr, screen);
  glyphs_ = font_installed(L"Segoe Fluent Icons") || font_installed(L"Segoe MDL2 Assets");
  build_fonts();
  HINSTANCE instance = App::get().instance();

  // Two flat, themed toolbars (navigation on the left; book information on
  // the right, after the search field). Their buttons carry
  // Segoe Fluent Icons / MDL2 Assets glyphs, crisp at every DPI, or words
  // where neither font exists.
  buttons_ = {
      {ID_VIEW_SIDEBAR, L"Sidebar", 0xE700, false},
      {ID_NAV_BACK, L"Back", 0xE72B, false},
      {ID_NAV_FORWARD, L"Forward", 0xE72A, false},
      {ID_NAV_PREVIOUS, L"Previous Topic", 0xE74A, false},
      {ID_NAV_NEXT, L"Next Topic", 0xE74B, false},
      {ID_NAV_CONTENTS, L"Table of Contents", 0xE8FD, false},
      {ID_HELP_INFORMATION, L"Book Information", 0xE946, true},
  };
  for (HWND* bar : {&lead_bar_, &trail_bar_}) {
    *bar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                           WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS |
                               CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
                           0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
    SendMessageW(*bar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(*bar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DOUBLEBUFFER);
    SendMessageW(*bar, TB_SETIMAGELIST, 0, 0);
  }
  fill_toolbars();
  search_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEARCH)),
                            instance, nullptr);
  SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search Book"));
  SetWindowSubclass(search_, search_proc, 1, reinterpret_cast<DWORD_PTR>(this));
  tree_ = CreateWindowExW(0, WC_TREEVIEWW, nullptr,
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                              TVS_SHOWSELALWAYS | TVS_NONEVENHEIGHT | TVS_INFOTIP | TVS_NOHSCROLL,
                          0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TREE)), instance,
                          nullptr);
  SetWindowTheme(tree_, L"Explorer", nullptr);
  empty_ = CreateWindowExW(0, kEmptyClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, instance,
                           nullptr);
  SetWindowLongPtrW(empty_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(hint_font_));
  browser_ = BrowserHost::create(hwnd_, this, {});
  if (!browser_)
    show_error(hwnd_, kAppTitle, L"The system web browser control (MSHTML) could not be started.");
  SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
  SendMessageW(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(tree_font_), TRUE);
  DragAcceptFiles(hwnd_, TRUE);
  layout();
  update_controls();
}

void ReaderWindow::fill_toolbars() {
  for (HWND bar : {lead_bar_, trail_bar_}) {
    while (SendMessageW(bar, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(bar, TB_DELETEBUTTON, 0, 0);
    SendMessageW(bar, WM_SETFONT, reinterpret_cast<WPARAM>(glyphs_ ? glyph_font_ : ui_font_), FALSE);
    SendMessageW(bar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));
    SendMessageW(bar, TB_SETPADDING, 0, MAKELPARAM(scale(glyphs_ ? 14 : 12, dpi_), scale(8, dpi_)));
  }
  glyph_text_.clear();
  glyph_text_.reserve(buttons_.size());
  for (const auto& button : buttons_) {
    glyph_text_.push_back(glyphs_ ? std::wstring(1, button.glyph) : std::wstring(button.label));
    TBBUTTON item{};
    item.iBitmap = I_IMAGENONE;
    item.idCommand = button.command;
    item.fsState = TBSTATE_ENABLED;
    item.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    item.iString = reinterpret_cast<INT_PTR>(glyph_text_.back().c_str());
    HWND bar = button.trailing ? trail_bar_ : lead_bar_;
    SendMessageW(bar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&item));
    if (button.command == ID_VIEW_SIDEBAR) {
      TBBUTTON separator{};
      separator.iBitmap = scale(12, dpi_);
      separator.fsStyle = BTNS_SEP;
      SendMessageW(bar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&separator));
    }
  }
  for (HWND bar : {lead_bar_, trail_bar_}) SendMessageW(bar, TB_AUTOSIZE, 0, 0);
}

void ReaderWindow::build_fonts() {
  for (HFONT* font : {&ui_font_, &glyph_font_, &hint_font_, &tree_font_, &detail_font_})
    if (*font) {
      DeleteObject(*font);
      *font = nullptr;
    }
  ui_font_ = create_ui_font(dpi_);
  hint_font_ = create_ui_font(dpi_, 130);
  detail_font_ = create_ui_font(dpi_, 92);
  LOGFONTW glyph{};
  glyph.lfHeight = -scale(16, dpi_);
  glyph.lfCharSet = DEFAULT_CHARSET;
  glyph.lfQuality = CLEARTYPE_QUALITY;
  lstrcpynW(glyph.lfFaceName, font_installed(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets",
            LF_FACESIZE);
  glyph_font_ = CreateFontIndirectW(&glyph);
  const Settings& settings = App::get().settings();
  LOGFONTW tree{};
  tree.lfHeight = -scale(settings.contents_size, dpi_);
  tree.lfCharSet = DEFAULT_CHARSET;
  tree.lfQuality = CLEARTYPE_QUALITY;
  lstrcpynW(tree.lfFaceName, settings.font_family.c_str(), LF_FACESIZE);
  tree_font_ = CreateFontIndirectW(&tree);
}

void ReaderWindow::layout() {
  if (!hwnd_ || !tree_) return;
  RECT client;
  GetClientRect(hwnd_, &client);
  const int bar = scale(kToolbarHeight, dpi_);
  const int pad = scale(6, dpi_);
  const int gap = scale(4, dpi_);
  // The search field is one line of the UI font tall, plus its frame.
  HDC dc = GetDC(hwnd_);
  HGDIOBJ previous_font = SelectObject(dc, ui_font_);
  TEXTMETRICW metrics{};
  GetTextMetricsW(dc, &metrics);
  SelectObject(dc, previous_font);
  ReleaseDC(hwnd_, dc);
  const int height = std::min(bar - 2 * pad, static_cast<int>(metrics.tmHeight) + scale(10, dpi_));
  // Leading: sidebar and navigation; trailing: search, information.
  SIZE lead{}, trail{};
  SendMessageW(lead_bar_, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&lead));
  SendMessageW(trail_bar_, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&trail));
  const int bar_top = std::max(0, (bar - static_cast<int>(lead.cy)) / 2);
  MoveWindow(lead_bar_, pad, bar_top, lead.cx, lead.cy, TRUE);
  const int x = pad + lead.cx + gap;
  int right = client.right - pad - trail.cx;
  MoveWindow(trail_bar_, right, bar_top, trail.cx, trail.cy, TRUE);
  right -= gap;
  const int search_width = std::max(scale(120, dpi_), std::min(scale(220, dpi_), right - x - gap));
  MoveWindow(search_, right - search_width, (bar - height) / 2, search_width, height, TRUE);

  const int top = bar;
  const int sidebar = sidebar_visible_ ? std::min(scale(sidebar_width_, dpi_),
                                                  std::max(0, static_cast<int>(client.right) - scale(200, dpi_)))
                                       : 0;
  ShowWindow(tree_, sidebar_visible_ ? SW_SHOW : SW_HIDE);
  MoveWindow(tree_, 0, top, sidebar, std::max(0, static_cast<int>(client.bottom) - top), TRUE);
  RECT view{sidebar_visible_ ? sidebar + scale(kSplitterWidth, dpi_) : 0, top, client.right, client.bottom};
  if (view.bottom < view.top) view.bottom = view.top;
  if (browser_) browser_->set_bounds(view);
  MoveWindow(empty_, 0, top, client.right, std::max(0, static_cast<int>(client.bottom) - top), TRUE);
  ShowWindow(empty_, has_book() ? SW_HIDE : SW_SHOW);
  if (!has_book()) SetWindowPos(empty_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

LRESULT ReaderWindow::handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
  case WM_CREATE:
    build();
    return 0;
  case WM_SIZE:
    layout();
    return 0;
  case WM_GETMINMAXINFO: {
    auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
    info->ptMinTrackSize = {scale(640, dpi_), scale(420, dpi_)};
    return 0;
  }
  case WM_DPICHANGED: {
    dpi_ = HIWORD(wparam);
    build_fonts();
    SetWindowLongPtrW(empty_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(hint_font_));
    fill_toolbars();
    SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    SendMessageW(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(tree_font_), TRUE);
    const auto* suggested = reinterpret_cast<const RECT*>(lparam);
    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    settings_changed();
    return 0;
  }
  case WM_SETCURSOR:
    if (LOWORD(lparam) == HTCLIENT && sidebar_visible_ && has_book()) {
      POINT point;
      GetCursorPos(&point);
      ScreenToClient(hwnd_, &point);
      const int edge = scale(sidebar_width_, dpi_);
      if (point.y > scale(kToolbarHeight, dpi_) && point.x >= edge && point.x < edge + scale(kSplitterWidth, dpi_)) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return TRUE;
      }
    }
    break;
  case WM_LBUTTONDOWN: {
    const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
    const int edge = scale(sidebar_width_, dpi_);
    if (sidebar_visible_ && y > scale(kToolbarHeight, dpi_) && x >= edge && x < edge + scale(kSplitterWidth, dpi_)) {
      dragging_splitter_ = true;
      SetCapture(hwnd_);
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (dragging_splitter_) {
      RECT client;
      GetClientRect(hwnd_, &client);
      const int x = std::max(scale(120, dpi_), std::min(GET_X_LPARAM(lparam), static_cast<int>(client.right) - scale(200, dpi_)));
      sidebar_width_ = MulDiv(x, 96, static_cast<int>(dpi_));
      layout();
    }
    return 0;
  case WM_LBUTTONUP:
  case WM_CAPTURECHANGED:
    if (dragging_splitter_) {
      dragging_splitter_ = false;
      if (message == WM_LBUTTONUP) ReleaseCapture();
    }
    return 0;
  case WM_COMMAND:
    if (LOWORD(wparam) == IDC_SEARCH) {
      if (HIWORD(wparam) == EN_CHANGE && GetWindowTextLengthW(search_) == 0 && showing_results_) restore_contents();
      return 0;
    }
    command(LOWORD(wparam));
    return 0;
  case WM_NOTIFY: {
    auto* header = reinterpret_cast<NMHDR*>(lparam);
    if (header->code == TTN_GETDISPINFOW) {
      auto* tip = reinterpret_cast<NMTTDISPINFOW*>(lparam);
      for (const auto& button : buttons_)
        if (static_cast<UINT_PTR>(button.command) == tip->hdr.idFrom) tip->lpszText = const_cast<wchar_t*>(button.label);
      return 0;
    }
    if (header->hwndFrom == tree_) {
      if (header->code == TVN_SELCHANGEDW) {
        auto* change = reinterpret_cast<NMTREEVIEWW*>(lparam);
        const auto index = static_cast<std::ptrdiff_t>(change->itemNew.lParam);
        if (change->action != TVC_UNKNOWN && index >= 0 && index < static_cast<std::ptrdiff_t>(shown_nodes_.size()) &&
            !shown_nodes_[static_cast<std::size_t>(index)].topic.empty())
          navigate_to(shown_nodes_[static_cast<std::size_t>(index)].topic);
      } else if (header->code == NM_CUSTOMDRAW) {
        LRESULT result = CDRF_DODEFAULT;
        tree_custom_draw(reinterpret_cast<NMTVCUSTOMDRAW*>(lparam), result);
        return result;
      } else if (header->code == TVN_GETINFOTIPW) {
        auto* tip = reinterpret_cast<NMTVGETINFOTIPW*>(lparam);
        const auto index = static_cast<std::ptrdiff_t>(tip->lParam);
        if (index >= 0 && index < static_cast<std::ptrdiff_t>(shown_nodes_.size())) {
          const Node& node = shown_nodes_[static_cast<std::size_t>(index)];
          const std::wstring text = node.detail.empty() ? widen(node.topic) : node.detail;
          lstrcpynW(tip->pszText, text.c_str(), tip->cchTextMax);
        }
      }
    }
    return 0;
  }
  case WM_DROPFILES:
    drop_files(reinterpret_cast<HDROP>(wparam));
    return 0;
  case WM_COPYDATA: {
    // Paths handed over by a second launch (a double-clicked book).
    const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    if (!data || data->dwData != kOpenPathsMessage) break;
    std::vector<std::wstring> paths;
    const auto* text = static_cast<const wchar_t*>(data->lpData);
    const std::size_t length = data->cbData / sizeof(wchar_t);
    std::size_t start = 0;
    for (std::size_t i = 0; i < length; ++i)
      if (text[i] == L'\0') {
        if (i > start) paths.emplace_back(text + start, i - start);
        start = i + 1;
      }
    // Opened after the sender has its answer: a large book takes a while.
    auto* later = new std::vector<std::wstring>(std::move(paths));
    if (!PostMessageW(hwnd_, kOpenLater, 0, reinterpret_cast<LPARAM>(later))) delete later;
    return TRUE;
  }
  case kOpenLater: {
    std::unique_ptr<std::vector<std::wstring>> paths(reinterpret_cast<std::vector<std::wstring>*>(lparam));
    if (paths->empty()) {
      if (ReaderWindow* reader = App::get().make_reader()) SetForegroundWindow(reader->hwnd());
    } else {
      App::get().open_paths(*paths);
    }
    return 0;
  }
  case kSearchResults: {
    std::unique_ptr<SearchPayload> payload(reinterpret_cast<SearchPayload*>(lparam));
    show_search_results(payload->generation, std::move(payload->hits));
    return 0;
  }
  case WM_INITMENUPOPUP:
    update_controls();
    return 0;
  case WM_CLOSE:
    DestroyWindow(hwnd_);
    return 0;
  case WM_DESTROY:
    ++search_generation_;
    if (browser_) {
      browser_->destroy();
      browser_ = nullptr;
    }
    if (source_) detach_source(source_.get());
    source_.reset();
    App::get().reader_destroyed(this);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void ReaderWindow::command(int id) {
  switch (id) {
  case ID_FILE_NEW_WINDOW: App::get().make_reader(); break;
  case ID_FILE_OPEN: choose_book(); break;
  case ID_FILE_CLOSE_WINDOW: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
  case ID_FILE_CLOSE_BOOK: close_book(); break;
  case ID_FILE_EXIT: PostQuitMessage(0); break;
  case ID_EDIT_COPY:
    if (GetFocus() == search_) SendMessageW(search_, WM_COPY, 0, 0);
    else if (browser_) browser_->exec(OLECMDID_COPY);
    break;
  case ID_EDIT_SELECT_ALL:
    if (GetFocus() == search_) SendMessageW(search_, EM_SETSEL, 0, -1);
    else if (browser_) browser_->exec(OLECMDID_SELECTALL);
    break;
  case ID_EDIT_SETTINGS: App::get().show_settings(hwnd_); break;
  case ID_VIEW_SIDEBAR:
    sidebar_visible_ = !sidebar_visible_;
    layout();
    break;
  case ID_VIEW_LARGER: step_topic_size(1); break;
  case ID_VIEW_SMALLER: step_topic_size(-1); break;
  case ID_NAV_CONTENTS: show_contents(); break;
  case ID_NAV_BACK: if (browser_) browser_->go_back(); break;
  case ID_NAV_FORWARD: if (browser_) browser_->go_forward(); break;
  case ID_NAV_PREVIOUS: go_relative(-1); break;
  case ID_NAV_NEXT: go_relative(1); break;
  case ID_SEARCH_BOOK:
    if (has_book()) {
      SetFocus(search_);
      SendMessageW(search_, EM_SETSEL, 0, -1);
    }
    break;
  case ID_HELP_INFORMATION: show_information(); break;
  case ID_HELP_ABOUT: App::get().show_about(hwnd_); break;
  default: break;
  }
}

bool ReaderWindow::pre_translate(MSG& message) {
  if (message.message < WM_KEYFIRST || message.message > WM_KEYLAST || !browser_) return false;
  HWND view = browser_->window();
  if (!view || (message.hwnd != view && !IsChild(view, message.hwnd))) return false;
  return browser_->translate_accelerator(&message);
}

int ReaderWindow::current_index() const {
  const auto found = std::find(order_.begin(), order_.end(), current_topic_);
  return found == order_.end() ? -1 : static_cast<int>(found - order_.begin());
}

void ReaderWindow::update_controls() {
  const bool loaded = has_book();
  const int index = current_index();
  const bool back = browser_ && browser_->can_go_back();
  const bool forward = browser_ && browser_->can_go_forward();
  const bool previous = loaded && index > 0;
  const bool next = loaded && index >= 0 && index + 1 < static_cast<int>(order_.size());
  auto enable = [&](int command, bool on) {
    for (const auto& button : buttons_)
      if (button.command == command)
        SendMessageW(button.trailing ? trail_bar_ : lead_bar_, TB_ENABLEBUTTON, static_cast<WPARAM>(command),
                     MAKELPARAM(on ? TRUE : FALSE, 0));
    if (HMENU menu = GetMenu(hwnd_))
      EnableMenuItem(menu, static_cast<UINT>(command), MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
  };
  enable(ID_NAV_BACK, back);
  enable(ID_NAV_FORWARD, forward);
  enable(ID_NAV_PREVIOUS, previous);
  enable(ID_NAV_NEXT, next);
  for (int command : {ID_NAV_CONTENTS, ID_HELP_INFORMATION, ID_SEARCH_BOOK, ID_FILE_CLOSE_BOOK})
    enable(command, loaded);
  EnableWindow(search_, loaded);
}

void ReaderWindow::update_title() {
  std::wstring title = kAppTitle;
  if (has_book()) {
    title = widen(source_->title());
    if (title.empty()) title = source_->path().filename().wstring();
  }
  SetWindowTextW(hwnd_, title.c_str());
}

void ReaderWindow::choose_book() {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                              dialog.put_void())) ||
      !dialog)
    return;
  const COMDLG_FILTERSPEC filters[] = {{L"BookManager books (*.boo)", L"*.boo"}, {L"All files (*.*)", L"*.*"}};
  dialog->SetFileTypes(2, filters);
  dialog->SetTitle(L"Open Book");
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
  if (FAILED(dialog->Show(hwnd_))) return;
  ComPtr<IShellItemArray> items;
  if (FAILED(dialog->GetResults(items.put())) || !items) return;
  DWORD count = 0;
  items->GetCount(&count);
  std::vector<std::wstring> paths;
  for (DWORD i = 0; i < count; ++i) {
    ComPtr<IShellItem> item;
    if (FAILED(items->GetItemAt(i, item.put())) || !item) continue;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
      paths.emplace_back(path);
      CoTaskMemFree(path);
    }
  }
  App::get().open_paths(paths, has_book() ? nullptr : this);
}

void ReaderWindow::open_book(const std::wstring& path) {
  auto source = std::make_shared<BookSource>(App::get().next_host());
  source->set_stylesheet(App::get().stylesheet());
  std::string error;
  HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
  const bool opened = source->open(std::filesystem::path(path), error);
  SetCursor(previous);
  if (!opened) {
    show_error(hwnd_, L"Cannot Open Book", path + L"\n\n" + widen(error));
    return;
  }
  // A fresh view per book, so Back never leads into the previous book.
  if (browser_) browser_->destroy();
  browser_ = BrowserHost::create(hwnd_, this, {});
  if (source_) detach_source(source_.get());
  source_ = source;
  attach_source(source_);
  ++search_generation_;
  SetWindowTextW(search_, L"");
  contents_nodes_.clear();
  contents_parents_.clear();
  order_.clear();
  std::vector<int> stack;
  for (const auto& row : source_->contents()) {
    Node node;
    node.topic = row.id;
    node.label = widen(row.id.empty() ? row.title : row.id + " " + row.title);
    const int index = static_cast<int>(contents_nodes_.size());
    int parent = -1;
    if (row.level == 0 || stack.empty()) {
      stack.assign(1, index);
    } else {
      const std::size_t slot = std::min<std::size_t>(row.level, stack.size());
      parent = stack[slot - 1];
      stack.resize(slot);
      stack.push_back(index);
    }
    contents_nodes_.push_back(std::move(node));
    contents_parents_.push_back(parent);
    order_.push_back(row.id);
  }
  current_topic_.clear();
  update_title();
  restore_contents();
  layout();
  if (!order_.empty()) navigate_to(order_.front());
  update_controls();
}

void ReaderWindow::close_book() {
  if (browser_) browser_->navigate(L"about:blank");
  if (source_) detach_source(source_.get());
  source_.reset();
  ++search_generation_;
  SetWindowTextW(search_, L"");
  contents_nodes_.clear();
  contents_parents_.clear();
  shown_nodes_.clear();
  topic_items_.clear();
  order_.clear();
  current_topic_.clear();
  TreeView_DeleteAllItems(tree_);
  update_title();
  layout();
  update_controls();
}

void ReaderWindow::navigate_to(const std::string& topic) {
  if (!topic.empty() && browser_ && has_book()) browser_->navigate(widen(source_->topic_url(topic)));
}

void ReaderWindow::go_relative(int delta) {
  const int index = current_index();
  const int next = index + delta;
  if (index >= 0 && next >= 0 && next < static_cast<int>(order_.size())) navigate_to(order_[static_cast<std::size_t>(next)]);
}

void ReaderWindow::show_contents() {
  if (!has_book()) return;
  SetWindowTextW(search_, L"");
  restore_contents();
  if (!order_.empty()) navigate_to(order_.front());
  SetFocus(tree_);
}

void ReaderWindow::fill_tree(const std::wstring& root_label, const std::vector<Node>& nodes,
                             const std::vector<int>& parents) {
  SendMessageW(tree_, WM_SETREDRAW, FALSE, 0);
  TreeView_DeleteAllItems(tree_);
  shown_nodes_ = nodes;
  topic_items_.clear();
  shown_nodes_.push_back({std::string(), root_label, std::wstring()});
  const LPARAM root_index = static_cast<LPARAM>(shown_nodes_.size() - 1);
  TVINSERTSTRUCTW insert{};
  insert.hParent = TVI_ROOT;
  insert.hInsertAfter = TVI_LAST;
  insert.item.mask = TVIF_TEXT | TVIF_PARAM;
  insert.item.pszText = const_cast<wchar_t*>(shown_nodes_.back().label.c_str());
  insert.item.lParam = root_index;
  HTREEITEM root = TreeView_InsertItem(tree_, &insert);
  std::vector<HTREEITEM> items(nodes.size(), nullptr);
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    insert.hParent = parents[i] >= 0 ? items[static_cast<std::size_t>(parents[i])] : root;
    insert.item.pszText = const_cast<wchar_t*>(shown_nodes_[i].label.c_str());
    insert.item.lParam = static_cast<LPARAM>(i);
    items[i] = TreeView_InsertItem(tree_, &insert);
    if (!shown_nodes_[i].topic.empty() && !topic_items_.count(shown_nodes_[i].topic))
      topic_items_[shown_nodes_[i].topic] = items[i];
  }
  TreeView_Expand(tree_, root, TVE_EXPAND);
  SendMessageW(tree_, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(tree_, nullptr, TRUE);
}

void ReaderWindow::restore_contents() {
  ++search_generation_;
  showing_results_ = false;
  TreeView_SetItemHeight(tree_, -1);
  if (!has_book()) return;
  std::wstring root = widen(source_->title());
  if (root.empty()) root = L"Book";
  fill_tree(root, contents_nodes_, contents_parents_);
  select_current_in_tree();
}

void ReaderWindow::select_current_in_tree() {
  const auto found = topic_items_.find(current_topic_);
  if (found == topic_items_.end()) return;
  TreeView_SelectItem(tree_, found->second);
  TreeView_EnsureVisible(tree_, found->second);
}

void ReaderWindow::run_search() {
  if (!has_book()) return;
  const int length = GetWindowTextLengthW(search_);
  std::wstring query(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(search_, &query[0], length + 1);
  query.resize(static_cast<std::size_t>(length));
  if (query.empty()) {
    restore_contents();
    return;
  }
  const unsigned generation = ++search_generation_;
  std::shared_ptr<const BookSource> source = source_;
  HWND target = hwnd_;
  const std::string needle = narrow(query);
  SetCursor(LoadCursorW(nullptr, IDC_APPSTARTING));
  std::thread([source, target, generation, needle] {
    auto* payload = new SearchPayload{generation, {}};
    try {
      payload->hits = source->search(needle, locale_matcher);
    } catch (...) {
    }
    if (!PostMessageW(target, kSearchResults, 0, reinterpret_cast<LPARAM>(payload))) delete payload;
  }).detach();
}

void ReaderWindow::show_search_results(unsigned generation, std::vector<SearchHit> hits) {
  if (generation != search_generation_ || !has_book()) return;
  std::vector<Node> nodes;
  std::vector<int> parents;
  for (const auto& hit : hits) {
    nodes.push_back({hit.id, widen(hit.id + " " + hit.title), widen(hit.snippet)});
    parents.push_back(-1);
  }
  showing_results_ = true;
  fill_tree(L"Search Results (" + std::to_wstring(hits.size()) + L")", nodes, parents);
  HDC dc = GetDC(tree_);
  TEXTMETRICW line{}, detail{};
  HGDIOBJ previous = SelectObject(dc, tree_font_);
  GetTextMetricsW(dc, &line);
  SelectObject(dc, detail_font_);
  GetTextMetricsW(dc, &detail);
  SelectObject(dc, previous);
  ReleaseDC(tree_, dc);
  TreeView_SetItemHeight(tree_, line.tmHeight + detail.tmHeight + scale(8, dpi_));
}

void ReaderWindow::tree_custom_draw(NMTVCUSTOMDRAW* draw, LRESULT& result) {
  if (!showing_results_) return;
  if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
    result = CDRF_NOTIFYITEMDRAW;
    return;
  }
  if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return;
  const auto index = static_cast<std::ptrdiff_t>(draw->nmcd.lItemlParam);
  if (index < 0 || index >= static_cast<std::ptrdiff_t>(shown_nodes_.size())) return;
  const Node& node = shown_nodes_[static_cast<std::size_t>(index)];
  if (node.detail.empty()) return;
  RECT text{};
  TreeView_GetItemRect(tree_, reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec), &text, TRUE);
  RECT row = draw->nmcd.rc;
  row.left = text.left;
  const bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
  const bool focused = GetFocus() == tree_;
  HDC dc = draw->nmcd.hdc;
  FillRect(dc, &row, GetSysColorBrush(selected ? (focused ? COLOR_HIGHLIGHT : COLOR_BTNFACE) : COLOR_WINDOW));
  SetBkMode(dc, TRANSPARENT);
  const COLORREF main = GetSysColor(selected && focused ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
  RECT inner = row;
  InflateRect(&inner, -scale(3, dpi_), -scale(3, dpi_));
  HGDIOBJ previous = SelectObject(dc, tree_font_);
  TEXTMETRICW metrics{};
  GetTextMetricsW(dc, &metrics);
  SetTextColor(dc, main);
  RECT first = inner;
  first.bottom = first.top + metrics.tmHeight;
  DrawTextW(dc, node.label.c_str(), -1, &first, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  SelectObject(dc, detail_font_);
  SetTextColor(dc, selected && focused ? main : GetSysColor(COLOR_GRAYTEXT));
  RECT second = inner;
  second.top = first.bottom + scale(1, dpi_);
  DrawTextW(dc, node.detail.c_str(), -1, &second, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  SelectObject(dc, previous);
  result = CDRF_SKIPDEFAULT;
}

bool ReaderWindow::before_navigate(const std::wstring& url) {
  const auto colon = url.find(L':');
  std::wstring scheme = colon == std::wstring::npos ? std::wstring() : url.substr(0, colon);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  if (scheme.empty() || scheme == L"geist" || scheme == L"about" || scheme == L"res" || scheme == L"javascript")
    return false;
  // A link out of the book (http, ftp, mailto, ...) goes to the system.
  ShellExecuteW(hwnd_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return true;
}

void ReaderWindow::navigate_complete(const std::wstring& url) {
  const auto parsed = parse_url(narrow(url));
  if (parsed && parsed->kind == ReaderUrl::Kind::topic && has_book() && parsed->host == source_->host()) {
    current_topic_ = parsed->id;
    if (!showing_results_) select_current_in_tree();
  }
  update_controls();
}

void ReaderWindow::document_complete(const std::wstring&) {
  apply_topic_font();
  update_controls();
}

void ReaderWindow::apply_topic_font() {
  if (!browser_) return;
  if (dpi_ != system_dpi_) {
    Variant zoom(static_cast<long>(MulDiv(100, static_cast<int>(dpi_), static_cast<int>(system_dpi_))));
    browser_->exec(OLECMDID_OPTICAL_ZOOM, OLECMDEXECOPT_DONTPROMPTUSER, zoom.get());
  }
  auto document = browser_->document();
  if (!document) return;
  ComPtr<IHTMLElement> body;
  if (FAILED(document->get_body(body.put())) || !body) return;
  ComPtr<IHTMLStyle> style;
  if (FAILED(body->get_style(style.put())) || !style) return;
  const Settings& settings = App::get().settings();
  Bstr family(css_string(settings.font_family));
  style->put_fontFamily(family.get());
  Variant size(std::to_wstring(settings.topic_size) + L"px");
  style->put_fontSize(*size);
}

void ReaderWindow::step_topic_size(int delta) {
  Settings settings = App::get().settings();
  settings.topic_size = std::max(kMinimumTextSize, std::min(kMaximumTextSize, settings.topic_size + delta));
  App::get().update_settings(settings);
}

void ReaderWindow::settings_changed() {
  build_fonts();
  SendMessageW(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(tree_font_), TRUE);
  if (showing_results_) {
    HDC dc = GetDC(tree_);
    TEXTMETRICW line{}, detail{};
    HGDIOBJ previous = SelectObject(dc, tree_font_);
    GetTextMetricsW(dc, &line);
    SelectObject(dc, detail_font_);
    GetTextMetricsW(dc, &detail);
    SelectObject(dc, previous);
    ReleaseDC(tree_, dc);
    TreeView_SetItemHeight(tree_, line.tmHeight + detail.tmHeight + scale(8, dpi_));
  } else {
    TreeView_SetItemHeight(tree_, -1);
  }
  InvalidateRect(tree_, nullptr, TRUE);
  apply_topic_font();
  layout();
}

void ReaderWindow::show_information() {
  if (has_book()) show_book_information(hwnd_, source_->information());
}

void ReaderWindow::drop_files(HDROP drop) {
  std::vector<std::wstring> books;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < count; ++i) {
    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
    std::wstring path(length + 1, L'\0');
    DragQueryFileW(drop, i, &path[0], length + 1);
    path.resize(length);
    if (has_boo_extension(path)) books.push_back(path);
  }
  DragFinish(drop);
  if (books.empty()) return;
  open_book(books.front());
  books.erase(books.begin());
  if (!books.empty()) App::get().open_paths(books);
}

} // namespace geist_reader
