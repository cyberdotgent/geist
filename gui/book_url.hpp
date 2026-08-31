// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The URL space the reader serves.
//
// Everything a book offers is addressed under one scheme and one host:
//
//   geist://book/topic/<topic id>[#<anchor>]   a rendered topic
//   geist://book/object/<object id>            a stored image
//
// One origin for both means a topic can load its own images with no
// cross-origin question to answer. The shape is deliberately the shape an
// HTTP front end would serve -- `/topic/<id>` and `/object/<id>` -- so the
// same formatter and router can back a server without changing the spellings
// a rendered book contains.
#pragma once

#include <QLatin1String>
#include <QString>
#include <QUrl>

namespace geist_reader {

inline constexpr char kBookScheme[] = "geist";
inline constexpr char kBookHost[] = "book";

// The origin every topic is served from; also the base URL of a topic page.
inline QString book_origin() {
  return QStringLiteral("%1://%2/").arg(QLatin1String(kBookScheme),
                                        QLatin1String(kBookHost));
}

inline QString encode_segment(const QString& value) {
  return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

inline QString topic_url(const QString& topic_id,
                         const QString& fragment = QString()) {
  QString url = book_origin() + QStringLiteral("topic/") +
                encode_segment(topic_id);
  if (!fragment.isEmpty()) {
    url += QLatin1Char('#') + encode_segment(fragment);
  }
  return url;
}

inline QString object_url(const QString& object_id) {
  return book_origin() + QStringLiteral("object/") +
         encode_segment(object_id);
}

// What a URL in this space asks for.
struct BookRoute {
  enum class Kind { unknown, topic, object };
  Kind kind = Kind::unknown;
  QString id;
  QString fragment;
};

inline BookRoute route_for(const QUrl& url) {
  BookRoute route;
  if (url.scheme() != QLatin1String(kBookScheme)) {
    return route;
  }
  auto path = url.path();
  if (path.startsWith(QLatin1Char('/'))) {
    path.remove(0, 1);
  }
  const auto slash = path.indexOf(QLatin1Char('/'));
  if (slash < 0) {
    return route;
  }
  const auto kind = path.left(slash);
  route.id = QUrl::fromPercentEncoding(path.mid(slash + 1).toUtf8());
  route.fragment = url.fragment();
  if (kind == QLatin1String("topic")) {
    route.kind = BookRoute::Kind::topic;
  } else if (kind == QLatin1String("object")) {
    route.kind = BookRoute::Kind::object;
  }
  return route;
}

} // namespace geist_reader
