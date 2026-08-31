// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Turns a book into the bytes the reader's URL space serves.
//
// BookSource is the whole of the reader's rendering policy: which link a
// cross reference resolves to, what page chrome a topic gets, and how a
// stored object is delivered. It answers by URL and knows nothing about
// widgets, so an HTTP front end could serve a book by calling the same two
// functions.
#pragma once

#include "geist/boo.hpp"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QWebEngineUrlSchemeHandler>

namespace geist_reader {

// Must run before the QApplication is constructed: Qt requires custom URL
// schemes to be registered before the web engine starts.
void register_book_scheme();

class BookSource {
public:
  // The book to serve, or nullptr for none. The document is owned by the
  // caller and must outlive this use of it.
  void set_document(const geist::BooDocument* document);

  // A complete HTML document for a topic, or empty if the book has no such
  // topic.
  QByteArray topic_html(const QString& topic_id);
  // PNG bytes for a stored object, or empty if it cannot be decoded.
  QByteArray object_png(const QString& object_id);

private:
  // Where a cross reference spelled somewhere in the book actually lands.
  struct LinkDestination {
    QString topic;
    QString fragment;
    QString resource;
  };

  // Maps the ids cross references spell to the topic carrying each
  // destination. Harvesting it renders every topic, so it is built once per
  // book, on first use rather than at open.
  void ensure_link_index();

  const geist::BooDocument* document_ = nullptr;
  QHash<QString, LinkDestination> link_index_;
  bool link_index_built_ = false;
  // Decoded PNG per object id. An empty value records an object that could
  // not be decoded, so it is not retried on every render.
  QHash<QString, QByteArray> objects_;
};

// Serves the book's URL space to the web engine.
class BookSchemeHandler : public QWebEngineUrlSchemeHandler {
  Q_OBJECT

public:
  BookSchemeHandler(BookSource& source, QObject* parent = nullptr);

  void requestStarted(QWebEngineUrlRequestJob* job) override;

private:
  BookSource& source_;
};

} // namespace geist_reader
