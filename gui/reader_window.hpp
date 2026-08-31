// The Geist Hardcopy Reader main window, laid out after IBM's BookManager
// SoftCopy Reader: a topic tree on the left, a divider, and a topic pane on
// the right, with navigation buttons and font controls on the toolbar.
//
// This is a pure consumer of the public libgeist API. No BOO parsing or
// rendering logic lives in the GUI: topics arrive as HTML from
// `geist::TocEntry::html_fragment()`, wrapped in the page chrome below.
#pragma once

#include "geist/boo.hpp"

#include "book_source.hpp"

#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QWebEnginePage>

#include <optional>

class QAction;
class QComboBox;
class QFontComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QWebEngineView;

namespace geist_reader {

// Keeps the topic pane inside the book. Everything the book itself names is
// served by BookSchemeHandler and navigated normally, so the web engine
// gives the reader real history and real anchor scrolling; a URL pointing
// out of the book is handed to the desktop browser instead of replacing the
// topic on screen.
class TopicPage : public QWebEnginePage {
  Q_OBJECT

public:
  explicit TopicPage(QObject* parent = nullptr);

protected:
  bool acceptNavigationRequest(const QUrl& url, NavigationType type,
                               bool is_main_frame) override;
};

class ReaderWindow : public QMainWindow {
  Q_OBJECT

public:
  ReaderWindow();

  // Opens `path`, replacing whatever book is loaded. Failures are reported in
  // a dialog; the window stays usable.
  void open_book(const QString& path);

private slots:
  void choose_book();
  void close_book();
  void show_about();
  void print_topic();
  void find_in_topic();

private:
  void build_actions();
  void build_toolbar();
  void build_menus();

  void populate_contents();
  // Reflects the topic the pane is showing into the tree and the toolbar.
  void sync_to_url(const QUrl& url);
  // Renders `topic_id`. `record` appends to the back/forward history; replay
  // from the history itself passes false.
  void navigate_to(const QString& topic_id,
                   const QString& fragment = QString());
  void go_relative(int delta);
  // The two independent size controls the original reader carries: the tree
  // font and the topic font are set separately.
  void apply_toc_font();
  void apply_topic_font();
  void update_navigation_state();
  void clear_book();

  QTreeWidget* contents_ = nullptr;
  QWebEngineView* view_ = nullptr;
  QTreeWidgetItem* root_item_ = nullptr;
  QFontComboBox* font_box_ = nullptr;

  QComboBox* toc_size_box_ = nullptr;
  QComboBox* topic_size_box_ = nullptr;

  QAction* open_action_ = nullptr;
  QAction* close_action_ = nullptr;
  QAction* back_action_ = nullptr;
  QAction* forward_action_ = nullptr;
  QAction* previous_action_ = nullptr;
  QAction* next_action_ = nullptr;
  QAction* contents_action_ = nullptr;
  QAction* search_action_ = nullptr;
  QAction* print_action_ = nullptr;
  QAction* about_action_ = nullptr;
  QAction* copy_action_ = nullptr;
  QAction* select_all_action_ = nullptr;

  std::optional<geist::BooDocument> document_;
  BookSource source_;
  QString book_path_;
  QString book_title_;
  QString last_search_;
  // Topic ids in table-of-contents order, for Previous/Next Topic.
  QStringList topic_order_;
  QHash<QString, QTreeWidgetItem*> items_;
  QString current_topic_;
  bool navigating_ = false;
};

} // namespace geist_reader
