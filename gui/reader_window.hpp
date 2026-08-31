// The Geist Hardcopy Reader main window, laid out after IBM's BookManager
// SoftCopy Reader: a topic tree on the left, a divider, and a topic pane on
// the right, with navigation buttons and font controls on the toolbar.
//
// This is a pure consumer of the public libgeist API. No BOO parsing or
// rendering logic lives in the GUI: topics arrive as HTML from
// `geist::TocEntry::html_fragment()`, wrapped in the page chrome below.
#pragma once

#include "geist/boo.hpp"

#include <QByteArray>
#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QWebEnginePage>
#include <QWebEngineUrlSchemeHandler>

#include <optional>

class QAction;
class QComboBox;
class QFontComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QWebEngineView;

namespace geist_reader {

// Must run before the QApplication is constructed: Qt requires custom URL
// schemes to be registered before the web engine starts.
void register_url_schemes();

// Serves a book's stored images to the topic pane over the private
// `geistres://book/<object id>` scheme.
//
// Images are fetched lazily and cached here rather than inlined into the
// topic HTML as `data:` URIs, because `setHtml()` cannot display more than
// 2 MB of markup and a few embedded images would exceed that.
class ResourceSchemeHandler : public QWebEngineUrlSchemeHandler {
  Q_OBJECT

public:
  explicit ResourceSchemeHandler(QObject* parent = nullptr);

  // The book to serve from, or nullptr when no book is open. The pointer is
  // owned by the window and must outlive the handler's use of it.
  void set_document(const geist::BooDocument* document);

  void requestStarted(QWebEngineUrlRequestJob* job) override;

private:
  const geist::BooDocument* document_ = nullptr;
  // Decoded PNG per object id. An empty value records a resource that could
  // not be decoded, so it is not retried on every render.
  QHash<QString, QByteArray> cache_;
};

// Intercepts the links a rendered topic emits. Intra-book topic references
// are resolved to the private `geist:` scheme by the render options, so they
// are reported here instead of being followed as real navigations; external
// URLs are handed to the desktop browser.
class TopicPage : public QWebEnginePage {
  Q_OBJECT

public:
  explicit TopicPage(QObject* parent = nullptr);

signals:
  void topicRequested(const QString& topic_id, const QString& fragment);

protected:
  bool acceptNavigationRequest(const QUrl& url, NavigationType type,
                               bool is_main_frame) override;
};

class ReaderWindow : public QMainWindow {
  Q_OBJECT

public:
  // Where a cross reference spelled somewhere in the book actually lands.
  struct LinkDestination {
    QString topic;
    QString fragment;
    QString resource;
  };

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
  // Builds the book-wide map from the ids cross references spell to the topic
  // that carries each destination. Harvesting it renders every topic, so it
  // is built once per book, on first use rather than at open.
  void ensure_link_index();
  // Renders `topic_id`. `record` appends to the back/forward history; replay
  // from the history itself passes false.
  void navigate_to(const QString& topic_id, const QString& fragment = QString(),
                   bool record = true);
  void go_back();
  void go_forward();
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
  ResourceSchemeHandler* resources_ = nullptr;
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
  QString book_path_;
  QString book_title_;
  QString last_search_;
  // Applied once the topic finishes loading; a topic is delivered
  // asynchronously, so the scroll cannot happen at navigate time.
  QString pending_fragment_;
  QHash<QString, LinkDestination> link_index_;
  bool link_index_built_ = false;
  // Topic ids in table-of-contents order, for Previous/Next Topic.
  QStringList topic_order_;
  QHash<QString, QTreeWidgetItem*> items_;
  // Visit history for Back/Forward, independent of the web engine's own
  // history because every topic is delivered through setHtml().
  QStringList history_;
  int history_index_ = -1;
  QString current_topic_;
  bool navigating_ = false;
};

} // namespace geist_reader
