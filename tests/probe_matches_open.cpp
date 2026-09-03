// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `probe_book` reads a book's identity without opening it, for a shelf
// listing that needs a title and a document number for every book in a
// directory and nothing else.
//
// The whole value of that shortcut rests on it agreeing with the long way
// round.  Two readers of the same bytes that are free to drift are a defect
// class this project has already paid for, so this test pins every field a
// summary publishes against what opening the same file reports.  Both paths
// enter through one container prologue and one control extraction; if someone
// gives the probe its own header parser to make it faster, this fails.

#include "geist/document.hpp"
#include "geist/probe.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>

int main() {
  const auto path = std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";

  const auto summary = geist::probe_book(path);
  const auto document = geist::BooDocument::open(path);

  const auto& probed = summary.properties;
  const auto& opened = document.book_properties();
  require(probed.title == opened.title, "probed title differs from open()");
  require(probed.short_title == opened.short_title,
          "probed short title differs from open()");
  require(probed.document_number == opened.document_number,
          "probed document number differs from open()");
  require(probed.authors == opened.authors,
          "probed authors differ from open()");
  require(probed.date == opened.date, "probed date differs from open()");
  require(probed.language == opened.language,
          "probed language differs from open()");
  require(probed.version == opened.version,
          "probed version differs from open()");
  require(probed.copyright == opened.copyright,
          "probed copyright differs from open()");

  require(summary.metadata.file_size == document.metadata().file_size,
          "probed file size differs from open()");
  require(summary.metadata.page_count == document.metadata().page_count,
          "probed page count differs from open()");
  require(summary.metadata.path == document.metadata().path,
          "probed path differs from open()");

  // The build stamp is what a live BookServer addresses a revision by, so a
  // shelf listing showing a different one from the opened book would send a
  // reader to the wrong revision.
  require(summary.directory.date == document.directory().date,
          "probed build date differs from open()");
  require(summary.directory.time == document.directory().time,
          "probed build time differs from open()");
  require(summary.directory.page_number == document.directory().page_number,
          "probed directory page differs from open()");
  require(summary.directory.version_text == document.directory().version_text,
          "probed version text differs from open()");

  // The fixture states these, so a probe returning empty strings that happen
  // to match an equally broken open() cannot pass the checks above.
  require(!probed.title.empty(), "fixture title is empty");
  require(!probed.document_number.empty(), "fixture document number is empty");
  require(!summary.directory.date.empty(), "fixture build date is empty");
  return 0;
}
