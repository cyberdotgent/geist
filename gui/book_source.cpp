// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "book_source.hpp"

#include "book_url.hpp"
#include "styling.hpp"

#include <QBuffer>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

#include <exception>
#include <string_view>

namespace geist_reader {
namespace {

// `resource:69` -> `69`; anything else is passed through, matching the
// renderer's own reading of a stored-object reference.
QString object_id_of(const QString& reference) {
  static const QString marker = QStringLiteral("resource:");
  return reference.startsWith(marker) ? reference.mid(marker.size())
                                      : reference;
}

// The reader's page chrome around a topic fragment. `html_fragment()` is
// documented as semantic content for "a consumer that owns the page around
// it"; this is that page. The header line reproduces the original reader's
// "<topic id> <title>" banner above the topic body.
QByteArray wrap_topic(const QString& topic_id, const QString& title,
                      const QString& fragment) {
  static const QString chrome = QStringLiteral(
      ".reader-topic-header{margin:0 0 1rem 0;padding:0 0 .35rem 0;}"
      ".reader-topic-id{font-style:italic;margin-right:.5rem;}"
      ".reader-topic-title{font-weight:700;}");
  return QStringLiteral(
             "<!doctype html>\n<html lang=\"en\"><head>"
             "<meta charset=\"utf-8\"><title>%1</title><style>%2\n%3</style>"
             "</head><body class=\"geist-book\">"
             "<div class=\"reader-topic-header\">"
             "<span class=\"reader-topic-id\">%4</span> "
             "<span class=\"reader-topic-title\">%1</span></div>%5"
             "</body></html>")
      .arg(title.toHtmlEscaped(), QString::fromUtf8(kPublishedStylesheet),
           chrome, topic_id.toHtmlEscaped(), fragment)
      .toUtf8();
}

} // namespace

void register_book_scheme() {
  QWebEngineUrlScheme scheme(kBookScheme);
  scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                  QWebEngineUrlScheme::LocalAccessAllowed |
                  QWebEngineUrlScheme::CorsEnabled);
  QWebEngineUrlScheme::registerScheme(scheme);
}

void BookSource::set_document(const geist::BooDocument* document) {
  document_ = document;
  link_index_.clear();
  link_index_built_ = false;
  objects_.clear();
}

void BookSource::ensure_link_index() {
  if (link_index_built_ || document_ == nullptr) {
    return;
  }

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
  // Raised once the index is complete, so a build that throws part way is
  // retried rather than leaving a half-filled index marked finished.
  link_index_built_ = true;
}

QByteArray BookSource::topic_html(const QString& topic_id) {
  if (document_ == nullptr) {
    return {};
  }
  const auto* entry = document_->find_toc_entry(topic_id.toStdString());
  if (entry == nullptr) {
    return {};
  }
  ensure_link_index();

  geist::HtmlRenderOptions render;
  render.resolve_topic =
      [](const std::string& id) -> std::optional<std::string> {
    return topic_url(QString::fromStdString(id)).toStdString();
  };
  // Without this every picture renders as `geist-image--unresolved`: the
  // renderer only emits a usable `src` when a consumer resolves the object.
  render.resolve_resource =
      [](const std::string& id) -> std::optional<std::string> {
    return object_url(QString::fromStdString(id)).toStdString();
  };
  // A named destination the renderer cannot place on its own. Left alone it
  // becomes `#<id>`, which only reaches a destination inside the topic on
  // screen; anything owned by another topic is a dead link.
  render.resolve_anchor =
      [this, topic_id](const std::string& spelled)
      -> std::optional<std::string> {
    const auto id = QString::fromStdString(spelled);
    const auto found = link_index_.constFind(id);
    if (found == link_index_.constEnd()) {
      // A generated list -- the contents, the index, the table and figure
      // lists -- names a topic by the topic's own id. That is not a link
      // target any topic reports, so the index cannot know it, and left
      // alone every entry of those lists is a dead `#<id>`.
      if (document_->find_toc_entry(spelled) != nullptr) {
        return topic_url(id).toStdString();
      }
      // Genuinely local: an anchor such as a footnote, which `#<id>`
      // already reaches inside the topic on screen.
      return std::nullopt;
    }
    if (!found->resource.isEmpty()) {
      // A figure whose body is a stored object resolves to the object.
      return object_url(object_id_of(found->resource)).toStdString();
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

  return wrap_topic(topic_id, QString::fromStdString(entry->title).trimmed(),
                    QString::fromStdString(entry->html_fragment(render)));
}

QByteArray BookSource::object_png(const QString& object_id) {
  if (document_ == nullptr) {
    return {};
  }
  const auto cached = objects_.constFind(object_id);
  if (cached != objects_.constEnd()) {
    return *cached;
  }

  QByteArray png;
  try {
    const auto bytes = document_->read_resource_png(object_id.toStdString());
    png = QByteArray(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<qsizetype>(bytes.size()));
  } catch (const std::exception&) {
    // A legacy format libgeist cannot decode. Record the failure so it is
    // not retried, and let the pane show a broken image rather than
    // inventing bytes.
    png.clear();
  }
  objects_.insert(object_id, png);
  return png;
}

BookSchemeHandler::BookSchemeHandler(BookSource& source, QObject* parent)
    : QWebEngineUrlSchemeHandler(parent), source_(source) {}

void BookSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job) {
  const auto route = route_for(job->requestUrl());

  QByteArray body;
  QByteArray type;
  switch (route.kind) {
  case BookRoute::Kind::topic:
    body = source_.topic_html(route.id);
    type = QByteArrayLiteral("text/html");
    break;
  case BookRoute::Kind::object:
    body = source_.object_png(route.id);
    type = QByteArrayLiteral("image/png");
    break;
  case BookRoute::Kind::unknown:
    break;
  }

  if (body.isEmpty()) {
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }

  auto* buffer = new QBuffer(job);
  buffer->setData(body);
  buffer->open(QIODevice::ReadOnly);
  job->reply(type, buffer);
}

} // namespace geist_reader
