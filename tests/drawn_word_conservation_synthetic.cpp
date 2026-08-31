// Source-side word conservation (issue #85).
//
// Coverage says which route rendered a topic.  It does not say whether the
// output is complete, and a topic can render, count as fully typed, and still
// drop a word: the flattened decoded string opens a control segment on a word
// that is only *spelled* like a control, and the word leaves the render with an
// anchor put in its place.  The ownership ledger conserves that word -- as the
// phantom control's opcode -- so the ledger alone cannot tell the two apart.
//
// What can tell them apart is the record's own display-line framing.  A payload
// tiles into `<length byte><that many bytes>` display lines; the length byte is
// the row-control slot, always and only, whatever ordinary word the dictionary
// spells for it; and a control's opcode is the first token of its own display
// line.  A word standing anywhere else on a line is display text and nothing
// else, and must reach the render.
//
// Two things are pinned here.  The word cut, synthetically, because both sides
// of the comparison have to agree on where a word ends -- the renderer writes
// emphasis *inside* a word (`CLIST`s), and a dictionary fragment boundary falls
// wherever it falls.  And the whole invariant over `packet.boo`, the one
// redistributable fixture, which drops nothing: the corpus-wide check is
// `tools/drawn_word_conservation.py`, since only `packet.boo` may be opened
// here (issue #59).

#include "geist/detail/drawn_word_conservation.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << "\n";
  geist_test::record_failure();
}

std::string join(const std::vector<std::string> &words) {
  std::string joined;
  for (const auto &word : words) {
    if (!joined.empty())
      joined += ",";
    joined += word;
  }
  return joined;
}

void check_word_cut() {
  const auto cut = [](const std::string &text) {
    return join(geist::detail::conservation_words(text));
  };
  require(cut("AX.25 Protocol") == "ax,25,protocol",
          "punctuation separates and the cut folds case");
  // The renderer fences the stem and leaves the plural outside it; the source
  // draws one word.  Emphasis, code fencing and the Markdown escape join
  // rather than separate, so the two sides agree.
  require(cut("`CLIST`s") == cut("CLISTs"),
          "code fencing inside a word does not split it");
  require(cut("**bold**word") == "boldword",
          "emphasis inside a word does not split it");
  // The escape goes; the underscore it protects still separates, exactly as
  // the same underscore does in the source.
  require(cut("FRONT\\_2") == cut("FRONT_2") && cut("FRONT_2") == "front,2",
          "an escaped underscore separates on both sides");
  require(cut("   ") .empty(), "a blank run yields no words");
}

std::filesystem::path book(const std::string &name) {
  return std::filesystem::path(GEIST_FIXTURE_DIR) / name;
}

void check_packet_conserves_every_drawn_word() {
  const auto document = geist::BooDocument::open(book("packet.boo"));
  const auto report = document.drawn_word_conservation();

  require(report.topics_checked > 0, "packet.boo offered no topic to check");
  require(report.topics_with_unframed_records == 0,
          "packet.boo holds a topic whose records do not tile into display "
          "lines, so its drawn text is undecided");

  for (const auto &topic : report.topics) {
    for (const auto &deficit : topic.deficits) {
      if (deficit.unaccounted == 0)
        continue;
      std::cerr << "FAIL: packet.boo " << topic.id << " [" << topic.route
                << "] draws `" << deficit.word << "` " << deficit.drawn_inline
                << " time(s) inside a display line and emits it "
                << deficit.emitted << " time(s): " << deficit.evidence << "\n";
      geist_test::record_failure();
    }
  }
  require(report.unaccounted_words == 0,
          "packet.boo drops display text its source draws");
}

} // namespace

int main() {
  check_word_cut();
  check_packet_conserves_every_drawn_word();
  return 0;
}
