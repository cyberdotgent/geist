#include "reader_window.hpp"

#include "styling.hpp"

#include <QApplication>
#include <QBuffer>
#include <QGuiApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineView>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

namespace geist_reader {
namespace {

constexpr int kTopicIdRole = Qt::UserRole + 1;
constexpr auto kTopicScheme = "geist";
constexpr auto kResourceScheme = "geistres";
// The topic pane's own origin. Serving images from the same scheme as the
// page keeps them same-origin, so nothing is blocked as a cross-origin load.
constexpr auto kTopicBaseUrl = "geistres://book/";

// The renderer is told to spell intra-book topic links in this private
// scheme, so TopicPage can turn a click into a tree selection.
QString topic_url(const QString& topic_id, const QString& fragment = {}) {
  QString url = QStringLiteral("%1://topic/%2")
                    .arg(QLatin1String(kTopicScheme),
                         QString::fromUtf8(
                             QUrl::toPercentEncoding(topic_id)));
  if (!fragment.isEmpty()) {
    url += QLatin1Char('#');
    url += QString::fromUtf8(QUrl::toPercentEncoding(fragment));
  }
  return url;
}

// `resource:69` -> `69`; anything else is passed through, matching the
// renderer's own reading of a stored-object reference.
std::string resource_object_id(const std::string& reference) {
  static constexpr std::string_view marker = "resource:";
  if (reference.size() > marker.size() &&
      reference.compare(0, marker.size(), marker) == 0) {
    return reference.substr(marker.size());
  }
  return reference;
}

// History entries carry the fragment alongside the topic.
QString history_entry(const QString& topic, const QString& fragment) {
  return fragment.isEmpty() ? topic
                            : QStringLiteral("%1#%2").arg(topic, fragment);
}

std::pair<QString, QString> split_history_entry(const QString& entry) {
  const auto hash = entry.indexOf(QLatin1Char('#'));
  return hash < 0 ? std::make_pair(entry, QString())
                  : std::make_pair(entry.left(hash), entry.mid(hash + 1));
}

// A JavaScript string literal for `value`.
QString js_string(const QString& value) {
  QString escaped = value;
  escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
  return QStringLiteral("'%1'").arg(escaped);
}

// A stored object, addressed in the private resource scheme.
QString resource_url(const std::string& object_id) {
  return QStringLiteral("%1://book/%2")
      .arg(QLatin1String(kResourceScheme),
           QString::fromUtf8(QUrl::toPercentEncoding(
               QString::fromStdString(object_id))));
}

// The object id from `geistres://book/<id>`.
QString path_id(const QUrl& url) {
  QString id = url.path();
  if (id.startsWith(QLatin1Char('/'))) {
    id.remove(0, 1);
  }
  return QUrl::fromPercentEncoding(id.toUtf8());
}

// The reader's own page chrome around a topic fragment. `html_fragment()` is
// documented as semantic content for "a consumer that owns the page around
// it"; this is that page. The header line reproduces the original reader's
// "<topic id> <title>" banner above the topic body.
QString wrap_topic(const QString& topic_id, const QString& title,
                   const QString& fragment) {
  return QStringLiteral(
             "<!doctype html>\n<html lang=\"en\"><head>"
             "<meta charset=\"utf-8\"><title>%1</title><style>%2\n%3</style>"
             "</head><body class=\"geist-book\">"
             "<div class=\"reader-topic-header\">"
             "<span class=\"reader-topic-id\">%4</span> "
             "<span class=\"reader-topic-title\">%1</span></div>%5"
             "</body></html>")
      .arg(title.toHtmlEscaped(),
           QString::fromUtf8(kPublishedStylesheet),
           QStringLiteral(
               ".reader-topic-header{margin:0 0 1rem 0;padding:0 0 .35rem 0;}"
               ".reader-topic-id{font-style:italic;margin-right:.5rem;}"
               ".reader-topic-title{font-weight:700;}"),
           topic_id.toHtmlEscaped(), fragment);
}

} // namespace

void register_url_schemes() {
  QWebEngineUrlScheme topics(kTopicScheme);
  topics.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  topics.setFlags(QWebEngineUrlScheme::LocalAccessAllowed);
  QWebEngineUrlScheme::registerScheme(topics);

  QWebEngineUrlScheme resources(kResourceScheme);
  resources.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  resources.setFlags(QWebEngineUrlScheme::SecureScheme |
                     QWebEngineUrlScheme::LocalAccessAllowed |
                     QWebEngineUrlScheme::CorsEnabled);
  QWebEngineUrlScheme::registerScheme(resources);
}

ResourceSchemeHandler::ResourceSchemeHandler(QObject* parent)
    : QWebEngineUrlSchemeHandler(parent) {}

void ResourceSchemeHandler::set_document(const geist::BooDocument* document) {
  document_ = document;
  cache_.clear();
}

void ResourceSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job) {
  if (document_ == nullptr) {
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }
  const auto id = path_id(job->requestUrl());

  auto cached = cache_.constFind(id);
  if (cached == cache_.constEnd()) {
    QByteArray png;
    try {
      const auto bytes = document_->read_resource_png(id.toStdString());
      png = QByteArray(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<qsizetype>(bytes.size()));
    } catch (const std::exception&) {
      // A legacy format libgeist cannot decode. Record the failure so it is
      // not retried, and let the pane show a broken image rather than
      // inventing bytes.
      png.clear();
    }
    cached = cache_.insert(id, png);
  }

  if (cached->isEmpty()) {
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }

  auto* buffer = new QBuffer(job);
  buffer->setData(*cached);
  buffer->open(QIODevice::ReadOnly);
  job->reply(QByteArrayLiteral("image/png"), buffer);
}

TopicPage::TopicPage(QObject* parent) : QWebEnginePage(parent) {}

bool TopicPage::acceptNavigationRequest(const QUrl& url, NavigationType,
                                        bool is_main_frame) {
  if (url.scheme() == QLatin1String(kTopicScheme)) {
    if (is_main_frame) {
      emit topicRequested(path_id(url), url.fragment());
    }
    return false;
  }
  if (url.scheme() == QLatin1String("http") ||
      url.scheme() == QLatin1String("https")) {
    QDesktopServices::openUrl(url);
    return false;
  }
  // `data:` and `about:blank` are how setHtml() delivers a topic.
  return true;
}

ReaderWindow::ReaderWindow() {
  contents_ = new QTreeWidget(this);
  contents_->setHeaderHidden(true);
  contents_->setMinimumWidth(260);
  contents_->setUniformRowHeights(true);

  view_ = new QWebEngineView(this);
  auto* page = new TopicPage(view_);
  view_->setPage(page);

  // Stored images are served over the private resource scheme.
  resources_ = new ResourceSchemeHandler(this);
  page->profile()->installUrlSchemeHandler(
      QByteArray(kResourceScheme), resources_);

  auto* divider = new QSplitter(Qt::Horizontal, this);
  divider->addWidget(contents_);
  divider->addWidget(view_);
  divider->setStretchFactor(0, 0);
  divider->setStretchFactor(1, 1);
  divider->setChildrenCollapsible(false);
  divider->setSizes({300, 880});
  setCentralWidget(divider);

  build_actions();
  build_toolbar();
  build_menus();

  connect(contents_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
            if (item == nullptr || navigating_) {
              return;
            }
            const auto id = item->data(0, kTopicIdRole).toString();
            if (!id.isEmpty()) {
              navigate_to(id);
            }
          });
  connect(page, &TopicPage::topicRequested, this,
          [this](const QString& id, const QString& fragment) {
            navigate_to(id, fragment);
          });

  // A topic loads asynchronously, so a cross reference into the middle of one
  // can only be scrolled to once the load has finished.
  connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) {
    if (!ok || pending_fragment_.isEmpty()) {
      pending_fragment_.clear();
      return;
    }
    view_->page()->runJavaScript(
        QStringLiteral("var e=document.getElementById(%1);"
                       "if(e){e.scrollIntoView(true);}")
            .arg(js_string(pending_fragment_)));
    pending_fragment_.clear();
  });

  resize(1200, 820);
  setWindowTitle(tr("Geist Hardcopy Reader"));
  apply_toc_font();
  apply_topic_font();
  update_navigation_state();
}

void ReaderWindow::build_actions() {
  const auto icon = [this](QStyle::StandardPixmap id) {
    return style()->standardIcon(id);
  };

  open_action_ = new QAction(icon(QStyle::SP_DialogOpenButton),
                             tr("&Open Book..."), this);
  open_action_->setShortcut(QKeySequence::Open);
  connect(open_action_, &QAction::triggered, this, &ReaderWindow::choose_book);

  close_action_ = new QAction(icon(QStyle::SP_DialogCloseButton),
                              tr("&Close Book"), this);
  connect(close_action_, &QAction::triggered, this, &ReaderWindow::close_book);

  back_action_ = new QAction(icon(QStyle::SP_ArrowBack), tr("&Back"), this);
  back_action_->setShortcut(QKeySequence::Back);
  connect(back_action_, &QAction::triggered, this, &ReaderWindow::go_back);

  forward_action_ = new QAction(icon(QStyle::SP_ArrowForward), tr("&Forward"),
                                this);
  forward_action_->setShortcut(QKeySequence::Forward);
  connect(forward_action_, &QAction::triggered, this,
          &ReaderWindow::go_forward);

  previous_action_ = new QAction(icon(QStyle::SP_ArrowUp),
                                 tr("&Previous Topic"), this);
  previous_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
  connect(previous_action_, &QAction::triggered, this,
          [this] { go_relative(-1); });

  next_action_ = new QAction(icon(QStyle::SP_ArrowDown), tr("&Next Topic"),
                             this);
  next_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
  connect(next_action_, &QAction::triggered, this,
          [this] { go_relative(1); });

  contents_action_ = new QAction(icon(QStyle::SP_DirHomeIcon),
                                 tr("&Table of Contents"), this);
  contents_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Home));
  connect(contents_action_, &QAction::triggered, this, [this] {
    if (root_item_ != nullptr && root_item_->childCount() > 0) {
      contents_->setCurrentItem(root_item_->child(0));
    }
    contents_->setFocus();
  });

  search_action_ = new QAction(icon(QStyle::SP_FileDialogContentsView),
                               tr("&Find in Topic..."), this);
  search_action_->setShortcut(QKeySequence::Find);
  connect(search_action_, &QAction::triggered, this,
          &ReaderWindow::find_in_topic);

  print_action_ = new QAction(icon(QStyle::SP_FileDialogDetailedView),
                              tr("&Print Topic..."), this);
  print_action_->setShortcut(QKeySequence::Print);
  connect(print_action_, &QAction::triggered, this,
          &ReaderWindow::print_topic);

  about_action_ = new QAction(icon(QStyle::SP_MessageBoxQuestion),
                              tr("&About Geist Hardcopy Reader"), this);
  connect(about_action_, &QAction::triggered, this, &ReaderWindow::show_about);

  copy_action_ = view_->pageAction(QWebEnginePage::Copy);
  copy_action_->setText(tr("&Copy"));
  select_all_action_ = view_->pageAction(QWebEnginePage::SelectAll);
  select_all_action_->setText(tr("Select &All"));
}

void ReaderWindow::build_toolbar() {
  auto* bar = addToolBar(tr("Reader"));
  bar->setMovable(false);
  bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

  bar->addAction(open_action_);
  bar->addAction(close_action_);
  bar->addSeparator();
  bar->addAction(back_action_);
  bar->addAction(forward_action_);
  bar->addSeparator();
  bar->addAction(previous_action_);
  bar->addAction(next_action_);
  bar->addAction(contents_action_);
  bar->addSeparator();
  bar->addAction(search_action_);
  bar->addAction(print_action_);
  bar->addSeparator();
  bar->addAction(about_action_);

  // The original reader right-aligns the font controls; a stretching spacer
  // is how a QToolBar does that.
  auto* spacer = new QWidget(bar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  bar->addWidget(spacer);

  bar->addWidget(new QLabel(tr("Font "), bar));
  font_box_ = new QFontComboBox(bar);
  font_box_->setCurrentFont(QFont(QStringLiteral("Sans Serif")));
  font_box_->setMinimumWidth(180);
  bar->addWidget(font_box_);

  const auto add_size_box = [this, bar](const QString& label, int initial) {
    bar->addWidget(new QLabel(label, bar));
    auto* box = new QComboBox(bar);
    for (int size : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32}) {
      box->addItem(QString::number(size), size);
    }
    box->setCurrentIndex(box->findData(initial));
    bar->addWidget(box);
    return box;
  };

  // Two independent size controls, as in the original: one for the topic
  // tree, one for the topic pane.
  toc_size_box_ = add_size_box(tr(" TOC Size "), 14);
  topic_size_box_ = add_size_box(tr(" Topic Size "), 14);

  connect(font_box_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont&) {
            apply_toc_font();
            apply_topic_font();
          });
  connect(toc_size_box_, &QComboBox::currentIndexChanged, this,
          [this](int) { apply_toc_font(); });
  connect(topic_size_box_, &QComboBox::currentIndexChanged, this,
          [this](int) { apply_topic_font(); });
}

void ReaderWindow::build_menus() {
  auto* file = menuBar()->addMenu(tr("&File"));
  file->addAction(open_action_);
  file->addAction(close_action_);
  file->addSeparator();
  file->addAction(print_action_);
  file->addSeparator();
  auto* quit = file->addAction(tr("E&xit"), this, &QWidget::close);
  quit->setShortcut(QKeySequence::Quit);

  auto* edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(copy_action_);
  edit->addAction(select_all_action_);

  auto* view = menuBar()->addMenu(tr("&View"));
  view->addAction(tr("Larger Topic Text"), QKeySequence::ZoomIn, this,
                  [this] {
                    topic_size_box_->setCurrentIndex(
                        std::min(topic_size_box_->currentIndex() + 1,
                                 topic_size_box_->count() - 1));
                  });
  view->addAction(tr("Smaller Topic Text"), QKeySequence::ZoomOut, this,
                  [this] {
                    topic_size_box_->setCurrentIndex(
                        std::max(topic_size_box_->currentIndex() - 1, 0));
                  });

  auto* navigate = menuBar()->addMenu(tr("&Navigate"));
  navigate->addAction(contents_action_);
  navigate->addSeparator();
  navigate->addAction(back_action_);
  navigate->addAction(forward_action_);
  navigate->addSeparator();
  navigate->addAction(previous_action_);
  navigate->addAction(next_action_);

  auto* search = menuBar()->addMenu(tr("&Search"));
  search->addAction(search_action_);

  // The original reader has a Notes menu; note-taking is deliberately not
  // implemented, so the menu is omitted rather than shown inert.
  auto* help = menuBar()->addMenu(tr("&Help"));
  help->addAction(about_action_);
}

void ReaderWindow::choose_book() {
  const auto path = QFileDialog::getOpenFileName(
      this, tr("Open Book"), QString(),
      tr("BookManager books (*.boo *.BOO);;All files (*)"));
  if (!path.isEmpty()) {
    open_book(path);
  }
}

void ReaderWindow::show_about() {
  QMessageBox::about(
      this, tr("About Geist Hardcopy Reader"),
      tr("<h3>Geist Hardcopy Reader</h3>"
         "<p>Version %1</p>"
         "<p>A reader for IBM BookManager <tt>BOO</tt> books, built on "
         "libgeist.</p>"
         "<p>Named in tribute to the IBM BookManager SoftCopy Reader. This is "
         "not an IBM product and is not affiliated with IBM.</p>")
          .arg(QApplication::applicationVersion()));
}

void ReaderWindow::print_topic() {
  QPrinter printer;
  QPrintDialog dialog(&printer, this);
  dialog.setWindowTitle(tr("Print Topic"));
  if (dialog.exec() == QDialog::Accepted) {
    view_->print(&printer);
  }
}

void ReaderWindow::find_in_topic() {
  bool accepted = false;
  const auto text =
      QInputDialog::getText(this, tr("Find in Topic"), tr("Find:"),
                            QLineEdit::Normal, last_search_, &accepted);
  if (accepted) {
    last_search_ = text;
    view_->findText(text);
  }
}

void ReaderWindow::clear_book() {
  if (resources_ != nullptr) {
    resources_->set_document(nullptr);
  }
  document_.reset();
  link_index_.clear();
  link_index_built_ = false;
  pending_fragment_.clear();
  contents_->clear();
  root_item_ = nullptr;
  items_.clear();
  topic_order_.clear();
  history_.clear();
  history_index_ = -1;
  current_topic_.clear();
  book_path_.clear();
  book_title_.clear();
  view_->setHtml(QString());
  update_navigation_state();
}

void ReaderWindow::close_book() {
  clear_book();
  setWindowTitle(tr("Geist Hardcopy Reader"));
}

void ReaderWindow::open_book(const QString& path) {
  try {
    auto opened = geist::BooDocument::open(path.toStdString());
    clear_book();
    document_ = std::move(opened);
    resources_->set_document(&document_.value());
  } catch (const std::exception& error) {
    close_book();
    QMessageBox::critical(
        this, tr("Cannot Open Book"),
        tr("%1\n\n%2").arg(path, QString::fromUtf8(error.what())));
    return;
  }

  const auto& book = document_->book_properties();
  const auto title = book.title.empty() ? book.short_title : book.title;
  book_path_ = QDir::toNativeSeparators(path);
  book_title_ = QString::fromStdString(title).trimmed();

  // The original reader's title bar reads "<path>: <book> - <application>".
  setWindowTitle(tr("%1: %2 - Geist Hardcopy Reader")
                     .arg(book_path_, book_title_));

  populate_contents();
  if (root_item_ != nullptr && root_item_->childCount() > 0) {
    contents_->setCurrentItem(root_item_->child(0));
  }
  update_navigation_state();
}

void ReaderWindow::populate_contents() {
  contents_->clear();
  items_.clear();
  topic_order_.clear();

  // The tree is rooted at the book, as in the original reader.
  root_item_ = new QTreeWidgetItem(contents_);
  root_item_->setText(0, book_title_.isEmpty() ? tr("Book") : book_title_);
  root_item_->setData(0, kTopicIdRole, QString());

  // The TOC is flat and source-ordered; `level` gives the depth, so the last
  // item seen at each depth parents the next deeper one. A depth that jumps
  // by more than one falls back to the deepest known parent rather than
  // dropping the entry.
  std::vector<QTreeWidgetItem*> parents;
  for (const auto& entry : document_->table_of_contents()) {
    auto* item = new QTreeWidgetItem();
    const auto id = QString::fromStdString(entry.id);
    const auto title = QString::fromStdString(entry.title).trimmed();
    // The original labels each row "<topic id> <title>".
    item->setText(0, id.isEmpty() ? title
                                  : QStringLiteral("%1 %2").arg(id, title));
    item->setData(0, kTopicIdRole, id);
    item->setToolTip(0, id);

    if (entry.level == 0 || parents.empty()) {
      root_item_->addChild(item);
      parents.assign(1, item);
    } else {
      const auto depth = static_cast<std::size_t>(entry.level);
      const auto slot = std::min(depth, parents.size());
      parents[slot - 1]->addChild(item);
      parents.resize(slot);
      parents.push_back(item);
    }

    items_.insert(id, item);
    topic_order_.append(id);
  }
  root_item_->setExpanded(true);
}

void ReaderWindow::ensure_link_index() {
  if (link_index_built_ || !document_.has_value()) {
    return;
  }
  link_index_built_ = true;

  // Harvesting the targets renders every topic, so show the wait cursor.
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  for (const auto& entry : document_->table_of_contents()) {
    const auto topic = QString::fromStdString(entry.id);
    for (const auto& target : entry.link_targets()) {
      const LinkDestination destination{
          topic, QString::fromStdString(target.fragment),
          QString::fromStdString(target.resource)};
      const auto id = QString::fromStdString(target.id);
      link_index_.insert(id, destination);
      // A figure or table is spelled both bare and with its object prefix.
      if (target.kind == geist::LinkTargetKind::figure) {
        link_index_.insert(QStringLiteral("FIG%1").arg(id), destination);
      } else if (target.kind == geist::LinkTargetKind::table) {
        link_index_.insert(QStringLiteral("TBL%1").arg(id), destination);
      }
    }
  }
  QGuiApplication::restoreOverrideCursor();
}

void ReaderWindow::navigate_to(const QString& topic_id,
                               const QString& fragment, bool record) {
  if (!document_.has_value() || topic_id.isEmpty()) {
    return;
  }
  const auto* entry = document_->find_toc_entry(topic_id.toStdString());
  if (entry == nullptr) {
    return;
  }
  ensure_link_index();

  geist::HtmlRenderOptions render;
  // Intra-book topic links come back through TopicPage instead of being
  // followed; everything else keeps the renderer's own destination.
  render.resolve_topic =
      [](const std::string& id) -> std::optional<std::string> {
    return topic_url(QString::fromStdString(id)).toStdString();
  };
  // Without this every picture renders as `geist-image--unresolved`: the
  // renderer only emits a usable `src` when a consumer resolves the object.
  render.resolve_resource =
      [](const std::string& id) -> std::optional<std::string> {
    return resource_url(id).toStdString();
  };
  // A named destination the renderer cannot place on its own. Left alone, it
  // becomes `#<id>`, which only reaches a destination that happens to sit in
  // the topic on screen; anything owned by another topic is a dead link.
  render.resolve_anchor =
      [this, topic_id](const std::string& spelled)
      -> std::optional<std::string> {
    const auto found = link_index_.constFind(QString::fromStdString(spelled));
    if (found == link_index_.constEnd()) {
      // A generated list -- the contents, the index, the table and figure
      // lists -- names a topic by the topic's own id. That is not a link
      // target any topic reports, so the index above cannot know it, and
      // left alone every entry of those lists is a dead `#<id>`.
      if (document_->find_toc_entry(spelled) != nullptr) {
        return topic_url(QString::fromStdString(spelled)).toStdString();
      }
      // Genuinely local: an anchor such as a footnote, which `#<id>`
      // already reaches inside the topic on screen.
      return std::nullopt;
    }
    if (!found->resource.isEmpty()) {
      // A figure whose body is a stored object resolves to the object.
      return resource_url(resource_object_id(found->resource.toStdString()))
          .toStdString();
    }
    if (found->topic == topic_id) {
      // Same topic: a plain fragment, but the anchor the topic really emits
      // is not always the id the reference spells.
      return found->fragment.isEmpty()
                 ? std::nullopt
                 : std::optional<std::string>(
                       QStringLiteral("#%1").arg(found->fragment)
                           .toStdString());
    }
    return topic_url(found->topic, found->fragment).toStdString();
  };

  view_->setHtml(wrap_topic(topic_id,
                            QString::fromStdString(entry->title).trimmed(),
                            QString::fromStdString(
                                entry->html_fragment(render))),
                 QUrl(QLatin1String(kTopicBaseUrl)));
  pending_fragment_ = fragment;

  if (record) {
    history_.resize(history_index_ + 1);
    history_.append(history_entry(topic_id, fragment));
    history_index_ = static_cast<int>(history_.size()) - 1;
  }
  current_topic_ = topic_id;

  // Mirror the selection into the tree without re-entering navigation.
  if (auto* item = items_.value(topic_id, nullptr)) {
    if (contents_->currentItem() != item) {
      navigating_ = true;
      contents_->setCurrentItem(item);
      navigating_ = false;
    }
  }
  update_navigation_state();
}

void ReaderWindow::go_back() {
  if (history_index_ > 0) {
    --history_index_;
    const auto entry = split_history_entry(history_.at(history_index_));
    navigate_to(entry.first, entry.second, false);
  }
}

void ReaderWindow::go_forward() {
  if (history_index_ + 1 < history_.size()) {
    ++history_index_;
    const auto entry = split_history_entry(history_.at(history_index_));
    navigate_to(entry.first, entry.second, false);
  }
}

void ReaderWindow::go_relative(int delta) {
  const auto at = topic_order_.indexOf(current_topic_);
  if (at < 0) {
    return;
  }
  const auto target = at + delta;
  if (target >= 0 && target < topic_order_.size()) {
    navigate_to(topic_order_.at(target));
  }
}

void ReaderWindow::apply_toc_font() {
  QFont font = font_box_ != nullptr ? font_box_->currentFont() : contents_->font();
  if (toc_size_box_ != nullptr) {
    font.setPointSize(toc_size_box_->currentData().toInt());
  }
  contents_->setFont(font);
}

void ReaderWindow::apply_topic_font() {
  auto* settings = view_->settings();
  const auto family = font_box_ != nullptr
                          ? font_box_->currentFont().family()
                          : QStringLiteral("Sans Serif");
  settings->setFontFamily(QWebEngineSettings::StandardFont, family);
  settings->setFontFamily(QWebEngineSettings::SansSerifFont, family);
  if (topic_size_box_ != nullptr) {
    settings->setFontSize(QWebEngineSettings::DefaultFontSize,
                          topic_size_box_->currentData().toInt());
  }
}

void ReaderWindow::update_navigation_state() {
  const bool loaded = document_.has_value();
  const auto at = loaded ? topic_order_.indexOf(current_topic_) : -1;

  close_action_->setEnabled(loaded);
  back_action_->setEnabled(history_index_ > 0);
  forward_action_->setEnabled(history_index_ + 1 < history_.size());
  previous_action_->setEnabled(at > 0);
  next_action_->setEnabled(at >= 0 && at + 1 < topic_order_.size());
  contents_action_->setEnabled(loaded && !topic_order_.isEmpty());
  search_action_->setEnabled(loaded);
  print_action_->setEnabled(loaded);
}

} // namespace geist_reader
