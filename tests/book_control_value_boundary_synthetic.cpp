// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Where a book control's value ends (issue #94).
//
// A header record spells its controls one after another, and between one
// control's value and the next control's key there is a separator that
// belongs to neither.  What that separator renders as is a per-book
// accident: ` ??*` in the AS/400 CL Reference, `, ` in the CPCS books, a
// bare `(` in `b1bw1a00.boo`.  Enumerating those spellings is how a book
// with no title came to be published as the title `(`, and how a heading
// level once swallowed its topic's prose.
//
// The decode carries something better than an alphabet of separators: the
// token boundaries underneath the rendered text.  `extract_logical_controls`
// therefore ends a value by walking back from the next control's key over
// the tokens that carry no word of their own -- no letter, no digit -- and
// stopping at the first token that does.  The separator run leaves the
// value whatever it is spelled with, and a real value is never shortened,
// because a real value's last token has a letter or a digit in it.
//
// That behaviour is correct today and nothing pinned it, so this test does.
// It matters in both directions, and both are asserted here:
//
//   * too little walking back leaves ` ??*` attached to the title;
//   * too much walking back eats a genuine trailing word.
//
// Everything is synthetic.  The corpus books cannot ship, so the records
// below are written out directly as their decode renders them, token by
// token, and no BOO container is opened (issue #59).  A token's own output
// carries the space the assembler inserts after it, which is why each token
// text below ends with its trailing space when one follows.

#include "geist/detail/core/internal.hpp"
#include "test_failures.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::BooBookProperties;
using geist::BooLogicalControl;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "book_control_value_boundary_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

// A record as the decoder hands it on: the flattened text, plus the byte
// offset at which each source token's own output begins.  This is exactly
// what `assembled_token_output_offsets` produces for a real record, built
// here by concatenation so the offsets cannot drift from the text.
struct DecodedRecord {
  std::string text;
  std::vector<std::size_t> token_offsets;
};

DecodedRecord decoded(const std::vector<std::string> &tokens) {
  DecodedRecord record;
  for (const auto &token : tokens) {
    record.token_offsets.push_back(record.text.size());
    record.text += token;
  }
  return record;
}

std::string value_of(const std::vector<BooLogicalControl> &controls,
                     const std::string &key) {
  for (const auto &control : controls) {
    if (control.key == key) {
      return control.value;
    }
  }
  return "<absent>";
}

bool has(const std::vector<BooLogicalControl> &controls,
         const std::string &key) {
  for (const auto &control : controls) {
    if (control.key == key) {
      return true;
    }
  }
  return false;
}

void expect_value(const std::vector<BooLogicalControl> &controls,
                  const std::string &key, const std::string &expected,
                  const std::string &what) {
  require(has(controls, key), key + " was not extracted at all (" + what + ")");
  const auto actual = value_of(controls, key);
  require(actual == expected, what + ": " + key + " read as \"" + actual +
                                  "\", expected \"" + expected + "\"");
}

// `qbka8202.boo`, whose separator run renders as ` ??*` -- the `?` being what
// an unrepresentable word projects to.  The title published for this book was
// once `... Control Language Reference ??*`; the run must leave the value.
//
//   ctitle=Application System/400: Programming: Control Language Reference ??*
//   cstitle=AS/400 CL Reference ??* ccopyright=(C) Copyright IBM Corp. 1994
//   cdate=October 1994 cdocnum=SC41-3722-00
DecodedRecord placeholder_separator_record() {
  return decoded({"ctitle=", "Application ", "System/400: ", "Programming: ",
                  "Control ", "Language ", "Reference ", "??* ", "cstitle=",
                  "AS/400 ", "CL ", "Reference ", "??* ", "ccopyright=",
                  "(C) ", "Copyright ", "IBM ", "Corp. ", "1994 ", "cdate=",
                  "October ", "1994 ", "cdocnum=", "SC41-3722-00"});
}

// `b1bw1a00.boo`: a book with no title at all, whose separator happens to
// render as a bare `(`.  Reading the separator as the value is how this book
// came to be published under the title `(`.  The same record separates its
// later controls with `, `, and two of those controls are likewise empty.
//
//   ctitle= ( cstitle=CPCS Prog. Number 5734-F11 LPS 1.0, ccopyright=,
//   csecurity=, cdate=March 1990
DecodedRecord bracket_separator_record() {
  return decoded({"ctitle= ", "( ", "cstitle=", "CPCS ", "Prog. ", "Number ",
                  "5734-F11 ", "LPS ", "1.0, ", "ccopyright=", ", ",
                  "csecurity=", ", ", "cdate=", "March ", "1990"});
}

// A ` ??*` run is dropped, and the value in front of it survives whole --
// including its last word, which a boundary that walks back too far would
// take.
void a_placeholder_separator_run_leaves_the_value() {
  const auto record = placeholder_separator_record();
  const auto controls = geist::detail::extract_logical_controls(
      record.text, record.token_offsets);

  expect_value(controls, "CTITLE",
               "Application System/400: Programming: Control Language "
               "Reference",
               "a title followed by a ` ??*` separator run");
  expect_value(controls, "CSTITLE", "AS/400 CL Reference",
               "a short title followed by a ` ??*` separator run");
}

// The same record's ordinary values: no separator oddity in front of the
// next key, so nothing is walked back over and nothing is lost.  `1994`,
// `1994` and `SC41-3722-00` are the words a too-eager boundary would eat.
void an_ordinary_value_is_unchanged() {
  const auto record = placeholder_separator_record();
  const auto controls = geist::detail::extract_logical_controls(
      record.text, record.token_offsets);

  expect_value(controls, "CCOPYRIGHT", "(C) Copyright IBM Corp. 1994",
               "a copyright ending in a real word");
  expect_value(controls, "CDATE", "October 1994",
               "a date ending in a real word");
  expect_value(controls, "CDOCNUM", "SC41-3722-00",
               "a document number at the end of the record");
}

// A separator that renders as `(` is dropped like any other, leaving the
// empty title empty rather than publishing a bracket.
void a_bracket_separator_leaves_an_empty_title() {
  const auto record = bracket_separator_record();
  const auto controls = geist::detail::extract_logical_controls(
      record.text, record.token_offsets);

  expect_value(controls, "CTITLE", "",
               "an empty title whose separator renders as `(`");
}

// And a `, ` separator, twice over, in the same record: both controls are
// empty and neither takes the comma.  The short title in between ends with
// a comma of its own that belongs to its last token, and keeps it.
void a_comma_separator_leaves_an_empty_value() {
  const auto record = bracket_separator_record();
  const auto controls = geist::detail::extract_logical_controls(
      record.text, record.token_offsets);

  expect_value(controls, "CCOPYRIGHT", "",
               "an empty copyright whose separator renders as `, `");
  expect_value(controls, "CSECURITY", "",
               "an empty security whose separator renders as `, `");
  expect_value(controls, "CSTITLE", "CPCS Prog. Number 5734-F11 LPS 1.0,",
               "a short title whose own last token ends in a comma");
  expect_value(controls, "CDATE", "March 1990",
               "a date at the end of the record");
}

// The properties the library actually publishes, so the boundary is pinned
// where a caller can see it and not only inside the extractor.
void the_published_properties_agree() {
  const auto placeholder = placeholder_separator_record();
  const BooBookProperties from_placeholder =
      geist::detail::build_book_properties(
          geist::detail::extract_logical_controls(
              placeholder.text, placeholder.token_offsets));
  require(from_placeholder.title ==
              "Application System/400: Programming: Control Language "
              "Reference",
          "the published title kept the ` ??*` separator run: \"" +
              from_placeholder.title + "\"");
  require(from_placeholder.short_title == "AS/400 CL Reference",
          "the published short title is wrong: \"" +
              from_placeholder.short_title + "\"");
  require(from_placeholder.date == "October 1994",
          "the published date is wrong: \"" + from_placeholder.date + "\"");
  require(from_placeholder.document_number == "SC41-3722-00",
          "the published document number is wrong: \"" +
              from_placeholder.document_number + "\"");

  const auto bracket = bracket_separator_record();
  const BooBookProperties from_bracket = geist::detail::build_book_properties(
      geist::detail::extract_logical_controls(bracket.text,
                                              bracket.token_offsets));
  require(from_bracket.title.empty(),
          "a book with no title was published under the title \"" +
              from_bracket.title + "\"");
  require(from_bracket.copyright.empty(),
          "an empty copyright was published as \"" + from_bracket.copyright +
              "\"");
  require(from_bracket.security.empty(),
          "an empty security was published as \"" + from_bracket.security +
              "\"");
  require(from_bracket.date == "March 1990",
          "the published date is wrong: \"" + from_bracket.date + "\"");
}

} // namespace

int main() {
  a_placeholder_separator_run_leaves_the_value();
  an_ordinary_value_is_unchanged();
  a_bracket_separator_leaves_an_empty_title();
  a_comma_separator_leaves_an_empty_value();
  the_published_properties_agree();
  return 0;
}
