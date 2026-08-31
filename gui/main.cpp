// Geist Hardcopy Reader -- a Qt6 desktop reader for IBM BookManager BOO
// books, named in tribute to IBM's BookManager SoftCopy Reader.

#include "book_source.hpp"
#include "reader_window.hpp"

#include <QApplication>

int main(int argc, char** argv) {
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
