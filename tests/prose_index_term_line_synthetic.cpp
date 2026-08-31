// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// An `SI` subject-index term is bounded by its own display line (issue #79).
//
// An `SI` entry occupies exactly one display line and draws nothing -- hosted
// BookServer serves no part of one -- but it still names the term that a
// cross-reference resolves to.  The decoded-string splitter knows nothing of
// the record's framing, so it opens a new segment wherever a term word is
// spelled like a control.  SH12-565 4.7.5.1 record 374 display line 15 is
// `SI SRVMODE, server initialization parameter` over tokens 90..95 and the
// splitter cuts before `SRVMODE` at token 91, which left the keyword with an
// empty term and sank the whole topic.  The line end, not the splitter's
// boundary, ends the term.
//
// Everything here is synthetic: the test builds a whole topic's
// `DecodedLogicalRecordSource` by hand and opens no book.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "prose_index_term_line_synthetic: " << message << '\n';
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
  // bytes each.  The length byte's dictionary spelling is the row sentinel,
  // the shape this book writes.
  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
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
    // The production decode order (logical.cpp): the framing demotes a
    // control-shaped word that another word already precedes on its own
    // display line.
    geist::detail::demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

// A whole prose topic: the eight metadata controls, the `ST` title, one `SI`
// index line whose term opens with the control-shaped word `SRVMODE`, and one
// row of body prose.
DecodedLogicalRecordSource topic_record() {
  RecordBuilder builder;
  builder.line({words("sh1.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("9"), words("0"), words("9")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  builder.line({words("ST"), words("Server"), words("Running"), words("Mode")});
  builder.line({words("SI"), words("SRVMODE"), TokenWords{1, ','},
                words("server"), words("mode")});
  builder.line({words("   "), words("Alpha"), words("beta"), words("gamma")});
  return builder.build(21);
}

void an_si_term_reaches_the_end_of_its_display_line() {
  const auto record = topic_record();
  require(record.ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");

  // The premise: the splitter really does cut the `SI` line in two, so this
  // fixture reproduces the fault and does not merely pass by accident.
  const auto *index_line = geist::detail::display_line_of_token(
      record, record.ir.display_lines[9].prefix_token + 1);
  require(index_line != nullptr, "the SI display line was not framed");
  std::size_t segments_over_the_line = 0;
  for (const auto &segment : record.control_segments) {
    if (segment.source_tokens.empty()) continue;
    if (segment.source_tokens.front() >= index_line->prefix_token &&
        segment.source_tokens.front() < index_line->token_end)
      ++segments_over_the_line;
  }
  require(segments_over_the_line > 1,
          "the fixture no longer reproduces the split this pins: the SI line "
          "is one segment, so the term was never cut from its keyword");

  const std::vector<DecodedLogicalRecordSource> sources{record};
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout, &error);
  require(ownership.has_value(),
          "the synthetic topic's ownership is not verifiable: " + error);
  if (!ownership) return;

  const auto prose = geist::detail::extract_prose_topic_ir(
      sources, layout, *ownership, "Server Running Mode", nullptr, &error);
  require(prose.has_value(),
          "a topic whose SI term opens with a control-shaped word was "
          "rejected: " + error);
  if (!prose) return;

  require(prose->index_terms.size() == 1,
          "the topic did not carry exactly one subject-index term");
  if (prose->index_terms.size() != 1) return;
  require(prose->index_terms.front().term == "SRVMODE, server mode",
          "the index term is '" + prose->index_terms.front().term +
              "', not the whole display line 'SRVMODE, server mode'");

  // The line draws nothing: every token of it holds an index or structural
  // role, never a visible text cell.
  for (auto token = index_line->prefix_token; token < index_line->token_end;
       ++token) {
    const auto &entry = prose->ledger[token];
    require(entry.role != geist::detail::ProseTokenRoleIR::text,
            "a token of the hidden SI display line was rendered as body "
            "text");
    require(entry.role != geist::detail::ProseTokenRoleIR::unassigned,
            "a token of the SI display line was left unassigned");
  }
  const auto keywords = static_cast<std::size_t>(std::count_if(
      prose->ledger.begin(), prose->ledger.end(), [](const auto &entry) {
        return entry.role == geist::detail::ProseTokenRoleIR::index_keyword;
      }));
  const auto term_tokens = static_cast<std::size_t>(std::count_if(
      prose->ledger.begin(), prose->ledger.end(), [](const auto &entry) {
        return entry.role == geist::detail::ProseTokenRoleIR::index_term;
      }));
  require(keywords == 1 && term_tokens == 4,
          "the SI line's tokens were not all claimed by the index term: " +
              std::to_string(keywords) + " keyword(s), " +
              std::to_string(term_tokens) + " term token(s)");

  // The body row after the index line keeps every word of its own.
  std::string body;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines) body += fragment.text + " ";
  require(body.find("Alpha beta gamma") != std::string::npos,
          "the body row following the SI line was lost; blocks carry '" +
              body + "'");

  require(geist::detail::verify_prose_topic_ir(sources, layout, *ownership,
                                               "Server Running Mode", nullptr,
                                               *prose, &error),
          "the extracted topic failed its own verifier: " + error);
}

} // namespace

int main() {
  an_si_term_reaches_the_end_of_its_display_line();
  return 0;
}
