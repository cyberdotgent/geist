// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "reader_window.hpp"

#include "book_url.hpp"

#include <QApplication>
#include <QBuffer>
#include <QGuiApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUrl>
#include <QWebEngineHistory>
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

} // namespace

TopicPage::TopicPage(QObject* parent) : QWebEnginePage(parent) {}

bool TopicPage::acceptNavigationRequest(const QUrl& url, NavigationType,
                                        bool is_main_frame) {
  if (is_main_frame && (url.scheme() == QLatin1String("http") ||
                        url.scheme() == QLatin1String("https"))) {
    QDesktopServices::openUrl(url);
    return false;
  }
  // Everything else is the book's own URL space, served by
  // BookSchemeHandler. Letting it navigate normally is what gives the reader
  // real back/forward history and real `#anchor` scrolling.
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

  // The book's whole URL space is served from here.
  page->profile()->installUrlSchemeHandler(
      QByteArray(kBookScheme), new BookSchemeHandler(source_, this));

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
  // The engine owns navigation now, so the tree and the toolbar follow the
  // pane rather than driving it.
  connect(view_, &QWebEngineView::urlChanged, this,
          [this](const QUrl& url) { sync_to_url(url); });
  connect(view_, &QWebEngineView::loadFinished, this,
          [this](bool) { update_navigation_state(); });

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
  connect(back_action_, &QAction::triggered, view_, &QWebEngineView::back);

  forward_action_ = new QAction(icon(QStyle::SP_ArrowForward), tr("&Forward"),
                                this);
  forward_action_->setShortcut(QKeySequence::Forward);
  connect(forward_action_, &QAction::triggered, view_,
          &QWebEngineView::forward);

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

  about_action_ = new QAction(tr("&About Geist Hardcopy Reader"), this);
  connect(about_action_, &QAction::triggered, this, &ReaderWindow::show_about);

  info_action_ = new QAction(icon(QStyle::SP_MessageBoxQuestion),
                             tr("Book &Information"), this);
  info_action_->setShortcut(QKeySequence(Qt::Key_F1));
  connect(info_action_, &QAction::triggered, this,
          &ReaderWindow::show_book_information);

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
  bar->addAction(info_action_);

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
         "<p>Licensed under the Apache License, Version 2.0. Bundled Qt, "
         "libpng and giflib components are covered by their own licences.</p>"
         "<p>Named in tribute to the IBM BookManager SoftCopy Reader. This is "
         "not an IBM product and is not affiliated with IBM.</p>")
          .arg(QApplication::applicationVersion()));
}

void ReaderWindow::show_book_information() {
  if (!document_.has_value()) {
    return;
  }
  const auto& metadata = document_->metadata();
  const auto& directory = document_->directory();
  const auto& book = document_->book_properties();
  const auto& header = document_->file_header();

  QString rows;
  const auto section = [&rows](const QString& name) {
    rows += QStringLiteral("<tr><th colspan=\"2\" class=\"s\">%1</th></tr>")
                .arg(name.toHtmlEscaped());
  };
  // Only states what the book states: a field the BOO does not carry is
  // left out rather than shown empty.
  const auto row = [&rows](const QString& name, const QString& value) {
    const auto text = value.trimmed();
    if (!text.isEmpty()) {
      rows += QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
                  .arg(name.toHtmlEscaped(), text.toHtmlEscaped());
    }
  };
  const auto number = [&row](const QString& name, quint64 value) {
    row(name, QLocale().toString(qulonglong(value)));
  };
  const auto text_of = [](const std::string& value) {
    return QString::fromStdString(value);
  };

  section(tr("Book"));
  row(tr("Title"), text_of(book.title));
  row(tr("Short title"), text_of(book.short_title));
  row(tr("Document number"), text_of(book.document_number));
  QStringList authors;
  for (const auto& author : book.authors) {
    authors << text_of(author).trimmed();
  }
  row(tr("Authors"), authors.join(QStringLiteral(", ")));
  row(tr("Language"), text_of(book.language));
  row(tr("Version"), text_of(book.version));
  row(tr("Build version"), text_of(book.build_version));
  row(tr("Date"), text_of(book.date));
  row(tr("Security"), text_of(book.security));
  row(tr("Copyright"), text_of(book.copyright));

  section(tr("Contents"));
  number(tr("Topics"), document_->table_of_contents().size());
  number(tr("Logical records"), directory.logical_record_count);
  number(tr("Stored objects"), document_->resources().size());

  section(tr("File"));
  row(tr("Path"), QDir::toNativeSeparators(
                      QString::fromStdString(metadata.path.string())));
  row(tr("Size"), QLocale().formattedDataSize(
                      static_cast<qint64>(metadata.file_size)));
  number(tr("Page size"), metadata.page_size);
  number(tr("Pages"), metadata.page_count);

  section(tr("Build"));
  row(tr("Built"), QStringLiteral("%1 %2").arg(text_of(directory.date),
                                               text_of(directory.time))
                       .trimmed());
  row(tr("Directory version"), text_of(directory.version_text));
  number(tr("Directory page"), directory.page_number);
  number(tr("Content pages"), directory.content_page_count);
  number(tr("Dictionary pages"), directory.dictionary_page_count);
  row(tr("File copyright"), text_of(header.copyright_text));

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Book Information"));
  dialog.resize(620, 560);

  auto* view = new QTextBrowser(&dialog);
  view->setHtml(QStringLiteral(
                    "<style>td,th{padding:3px 10px 3px 0;vertical-align:top;}"
                    "th.s{padding-top:12px;text-align:left;}"
                    "td.k{white-space:nowrap;font-weight:600;}</style>"
                    "<table>%1</table>")
                    .arg(rows));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(view);
  layout->addWidget(buttons);
  dialog.exec();
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
  source_.set_document(nullptr);
  document_.reset();
  contents_->clear();
  root_item_ = nullptr;
  items_.clear();
  topic_order_.clear();
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
    source_.set_document(&document_.value());
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

void ReaderWindow::navigate_to(const QString& topic_id,
                               const QString& fragment) {
  if (!document_.has_value() || topic_id.isEmpty()) {
    return;
  }
  view_->load(QUrl(topic_url(topic_id, fragment)));
}

// Follows the pane: whatever it is showing becomes the tree selection and
// the anchor for Previous/Next Topic.
void ReaderWindow::sync_to_url(const QUrl& url) {
  const auto route = route_for(url);
  if (route.kind != BookRoute::Kind::topic) {
    update_navigation_state();
    return;
  }
  current_topic_ = route.id;
  if (auto* item = items_.value(route.id, nullptr)) {
    if (contents_->currentItem() != item) {
      navigating_ = true;
      contents_->setCurrentItem(item);
      navigating_ = false;
    }
  }
  update_navigation_state();
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
  auto* history = view_->history();
  back_action_->setEnabled(history->canGoBack());
  forward_action_->setEnabled(history->canGoForward());
  previous_action_->setEnabled(at > 0);
  next_action_->setEnabled(at >= 0 && at + 1 < topic_order_.size());
  contents_action_->setEnabled(loaded && !topic_order_.isEmpty());
  search_action_->setEnabled(loaded);
  print_action_->setEnabled(loaded);
  info_action_->setEnabled(loaded);
}

} // namespace geist_reader
