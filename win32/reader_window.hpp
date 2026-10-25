// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// One reader window: a toolbar with the book search, the contents tree and
// the topic view, for one open book.

#pragma once

#include "book_source.hpp"
#include "browser_host.hpp"
#include "win32_util.hpp"

#include <commctrl.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace geist_reader {

class ReaderWindow final : public BrowserListener {
public:
  static bool register_class(HINSTANCE instance);
  static ReaderWindow* create();

  HWND hwnd() const { return hwnd_; }
  bool has_book() const { return source_ && source_->has_book(); }
  void open_book(const std::wstring& path);
  void settings_changed();
  // Gives the window's controls a chance at a keyboard message first.
  bool pre_translate(MSG& message);

  // BrowserListener
  bool before_navigate(const std::wstring& url) override;
  void navigate_complete(const std::wstring& url) override;
  void document_complete(const std::wstring& url) override;
  void history_changed() override { update_controls(); }

private:
  struct Node {
    std::string topic;
    std::wstring label;
    std::wstring detail;
  };
  struct Button {
    int command;
    const wchar_t* label;
    wchar_t glyph;
    bool trailing;
  };

  ReaderWindow() = default;
  ~ReaderWindow();
  static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK search_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
  static LRESULT CALLBACK empty_proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

  void build();
  void build_fonts();
  void fill_toolbars();
  void layout();
  void command(int id);
  void update_controls();
  void update_title();
  void choose_book();
  void close_book();
  void navigate_to(const std::string& topic);
  void go_relative(int delta);
  void show_contents();
  void restore_contents();
  void run_search();
  void show_search_results(unsigned generation, std::vector<SearchHit> hits);
  void fill_tree(const std::wstring& root_label, const std::vector<Node>& nodes,
                 const std::vector<int>& parents);
  void select_current_in_tree();
  void apply_topic_font();
  void step_topic_size(int delta);
  void show_information();
  void drop_files(HDROP drop);
  void tree_custom_draw(NMTVCUSTOMDRAW* draw, LRESULT& result);
  int current_index() const;

  HWND hwnd_ = nullptr;
  HWND tree_ = nullptr;
  HWND search_ = nullptr;
  HWND empty_ = nullptr;
  HWND lead_bar_ = nullptr;
  HWND trail_bar_ = nullptr;
  std::vector<Button> buttons_;
  std::vector<std::wstring> glyph_text_;
  BrowserHost* browser_ = nullptr;
  std::shared_ptr<BookSource> source_;

  HFONT ui_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT hint_font_ = nullptr;
  HFONT tree_font_ = nullptr;
  HFONT detail_font_ = nullptr;
  bool glyphs_ = false;
  UINT dpi_ = 96;
  UINT system_dpi_ = 96;
  int sidebar_width_ = 300; // at 96 dpi
  bool sidebar_visible_ = true;
  bool dragging_splitter_ = false;

  std::vector<Node> contents_nodes_;
  std::vector<int> contents_parents_;
  std::vector<Node> shown_nodes_;
  std::map<std::string, HTREEITEM> topic_items_;
  std::vector<std::string> order_;
  std::string current_topic_;
  bool showing_results_ = false;
  unsigned search_generation_ = 0;
};

} // namespace geist_reader
