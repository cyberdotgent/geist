// A `cz OFF E<region>` closer that closes nothing is inert (issue #74).
//
// The book compiler can write the end of a verbatim region whose opener it
// never wrote.  GX27-3999-00 `NOTICES` (DT 19950730184057) draws its `Note`
// box inside an `SRFIG`/`cz OFF FIG` figure region and then closes *two*
// regions where it opened one:
//
//   3 11  SRFIGFIGUNIQ1
//   3 12  cz OFF FIG
//   3 13  cfont 8 4 2
//   3 14      ___ Note ______________________________________________
//   ...       the five remaining box rows
//   3 21  SREFIG
//   3 22  cz OFF ELBLBOX 0 0      <- closes no open LBLBOX
//   3 23  cz OFF EFIG 0 0
//
// No `cz OFF LBLBOX` appears in the topic.  Hosted BookServer serves the body
// as a single
//
//   <pre width="132"><!-- figure -->
//   <a name="FIGFIGUNIQ1">    ___ <B>Note</B> ______ ... </a>
//      ...
//   </pre>
//
// The block is the *figure*'s -- named for it by the `<!-- figure -->`
// comment and anchored on the envelope's id -- and the `ELBLBOX` contributes
// no block, no comment and no character.  A labelled box that really is
// opened is served as `<!-- lblbox -->` instead, so BookServer is
// distinguishing the two: an unopened region closer draws nothing and closes
// nothing.
//
// The model admits it on exactly that evidence and no more: the region was
// never opened in this topic, and the closer draws no display row of its own.
// A closer carrying display rows is a region boundary the model cannot place,
// and still declines.
//
// Everything here is synthetic: the test builds whole topic records by hand
// with `assemble_logical_record_with_sources` and opens no book, because
// `libgeist` may depend on no book but `packet.boo` (issue #59) and
// GX27-3999-00 is not it.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::ProseBlockKindIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "cz_unopened_region_closer_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a length byte covering `content`, whose tokens are two
  // bytes each.  The length byte's dictionary spelling is the row sentinel.
  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  // The eight metadata controls and the `ST` title every prose topic opens
  // with.
  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("ALY0FRO")});
    line({words("ST"), words(title_word)});
  }

  DecodedLogicalRecordSource build(std::uint32_t logical_record) {
    record.logical_record = logical_record;
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
    std::uint32_t byte = 1;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto encoded = record.encoded_tokens[token];
      const auto spacing =
          !record.tokens[token].empty() && record.tokens[token].front() < 4;
      record.ir.tokens.push_back(
          {token, encoded, record.tokens[token],
           {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
           spacing ? record.tokens[token].front() : std::uint16_t{3}});
      byte += encoded.width;
    }
    record.ir.payload_range = {1, byte};
    geist::detail::assign_display_line_framing(record.ir);
    record.control_segments = geist::detail::decode_control_segments(
        record.logical_record, record.assembled, record.encoded_tokens,
        record.ir.display_lines);
    geist::detail::demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

struct Extracted {
  std::vector<DecodedLogicalRecordSource> records;
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
};

Extracted run(DecodedLogicalRecordSource record, const std::string &title) {
  Extracted result;
  require(record.ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");
  result.records.push_back(std::move(record));
  const auto layout = geist::detail::extract_layout_ir(result.records);
  std::string ownership_error;
  const auto ownership = geist::detail::build_verified_ownership_ir(
      result.records, layout, &ownership_error);
  require(ownership.has_value(),
          "the synthetic topic's ownership is not verifiable: " +
              ownership_error);
  if (!ownership) return result;
  result.topic = geist::detail::extract_prose_topic_ir(
      result.records, layout, *ownership, title, nullptr, &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, title, nullptr,
                *result.topic, &verify_error),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

// The drawn `Note` box of GX27-3999-00 `NOTICES`, six rows wide enough to
// carry the box rules, inside the figure envelope.  `closer_rows` decides
// whether the unopened `ELBLBOX` closer draws a display row of its own.
DecodedLogicalRecordSource notices_topic(bool closer_draws_text) {
  RecordBuilder builder;
  builder.heading("2.4", "Notices");
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("SRFIGFIGUNIQ1")});
  builder.line({words("cz"), words("OFF"), words("FIG")});
  builder.line({words("cfont"), words("8"), words("4"), words("2")});
  builder.line({words("    ___ "), words("Note"), words(" " + std::string(40, '_'))});
  builder.line({words("   |"), words(std::string(44, ' ')), words("|")});
  builder.line({words("   | "), words("Before"), words("using"), words("this"),
                words("information,"), words("read"), words("   |")});
  builder.line({words("   | "), words("the"), words("general"),
                words("information."), words(std::string(18, ' ')),
                words("|")});
  builder.line({words("   |"), words(std::string(44, ' ')), words("|")});
  builder.line({words("   |"), words(std::string(44, '_')), words("|")});
  builder.line({words("SREFIG")});
  if (closer_draws_text)
    builder.line({words("cz"), words("OFF"), words("ELBLBOX"), words("0"),
                  words("0"), words("   "), words("Trailing"), words("row.")});
  else
    builder.line(
        {words("cz"), words("OFF"), words("ELBLBOX"), words("0"), words("0")});
  builder.line(
      {words("cz"), words("OFF"), words("EFIG"), words("0"), words("0")});
  return builder.build(3);
}

// The `ELBLBOX` closer opened nothing and draws nothing, so the topic is
// claimed and the figure region is its only block.
void an_unopened_region_closer_is_inert() {
  auto extracted = run(notices_topic(false), "Notices");
  require(extracted.topic.has_value(),
          "a topic whose figure region is followed by an unopened "
          "`cz OFF ELBLBOX` closer was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  // The drawn box is the figure region's body, which the figure family claims
  // token for token, so it is a figure block rather than a prose block.
  const auto &figures = extracted.topic->figures.blocks;
  require(figures.size() == 1,
          "the drawn `Note` box did not become one figure block; the topic "
          "has " + std::to_string(figures.size()));
  if (figures.size() != 1) return;
  require(figures.front().lines.size() == 6,
          "the `Note` box kept " +
              std::to_string(figures.front().lines.size()) +
              " body rows, not the six it draws");
  // The inert closer contributes no character and no block of its own.
  for (const auto &line : figures.front().lines)
    require(line.text.find("ELBLBOX") == std::string::npos,
            "the inert closer leaked its tag into the drawn box: '" +
                line.text + "'");
  require(extracted.topic->blocks.empty(),
          "the inert closer drew a prose block of its own: the topic has " +
              std::to_string(extracted.topic->blocks.size()));
}

// A closer that *does* draw a display row is a region boundary the model
// cannot place, and still fails closed.
void a_closer_carrying_display_text_still_declines() {
  auto extracted = run(notices_topic(true), "Notices");
  require(!extracted.topic.has_value(),
          "an unopened `cz OFF ELBLBOX` carrying its own display row was "
          "admitted; it must fail closed");
}

}  // namespace

int main() {
  an_unopened_region_closer_is_inert();
  a_closer_carrying_display_text_still_declines();
  return 0;
}
