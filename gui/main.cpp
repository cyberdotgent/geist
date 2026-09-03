// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Geist Hardcopy Reader -- a Qt6 desktop reader for IBM BookManager BOO
// books, named in tribute to IBM's BookManager SoftCopy Reader.

#include "book_source.hpp"
#include "reader_window.hpp"

#include "geist/version.hpp"

#include <QApplication>

#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  // Answered before anything is registered or started: asking a GUI its
  // version should not need a display, a web engine or a window.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") != 0 &&
        std::strcmp(argv[i], "-V") != 0) {
      continue;
    }
    const std::string revision = GEIST_READER_REVISION;
    std::cout << "geist-hardcopy/" << GEIST_READER_VERSION;
    if (!revision.empty() && revision != "unknown") {
      std::cout << " (" << revision << ')';
    }
    std::cout << " libgeist/" << geist::library_version();
    const std::string library = geist::library_revision();
    if (!library.empty() && library != "unknown") {
      std::cout << " (" << library << ')';
    }
    std::cout << '\n';
    return 0;
  }

  // Custom URL schemes must be registered before the web engine starts, so
  // this has to precede the QApplication.
  geist_reader::register_book_scheme();

  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Geist Hardcopy Reader"));
  QApplication::setApplicationVersion(QStringLiteral(GEIST_READER_VERSION));

  geist_reader::ReaderWindow window;
  window.show();

  // A path on the command line opens that book, so the reader can be wired to
  // a file association or invoked from a shell.
  const auto args = QApplication::arguments();
  if (args.size() > 1) {
    window.open_book(args.at(1));
  }
  return QApplication::exec();
}
