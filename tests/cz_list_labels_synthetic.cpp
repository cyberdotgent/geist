// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The label columns of a CZ list item.
//
// A `cz FLOW LI <left> <indent>` row stores the item's text from `indent`
// onwards; the columns `left` up to `indent` hold what the reader shows in
// front of it.  Hosted BookServer treats that head differently per list:
//
//  * `UL`: the head is the bullet.  The first level stores the `U+2666`
//    bullet glyph, a nested level a plain `-` (SC21-8295-03 3.1.3.2 record
//    405 `cz FLOW LI 7 11` + `       -   Change management (DSNX)`), and
//    hosted serves both as `<li>` with no character in front of the text.
//  * `OL`: the head is the item's ordinal label (`1.`, `a.`, `7)`, `h)`,
//    `Step 1.`), which hosted's `<ol>` replaces.
//  * `NOTEL`: the head is a stored note number that hosted keeps as text,
//    each item a bare `<dd>` (SC21-8295-03 2.2, 3.1.3.2; SC09-2417-00 2.2.4).
//
// Built synthetically so the rules stay pinned whatever the corpus holds.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <algorithm>
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
    std::cerr << "cz_list_labels_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// One display line per call, its length byte first, then the line's tokens;
// the decoder's own walk reads the framing back.
struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

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
    geist::detail::demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

const char *const topic_title = "Widget Notes";

void metadata(RecordBuilder &builder) {
  builder.line({words("sh1.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("8"), words("0"), words("8")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  builder.line({words("ST"), words("Widget"), words("Notes")});
}

void directive(RecordBuilder &builder, const std::string &mode,
               const std::string &tag, const std::string &left,
               const std::string &indent) {
  builder.line(
      {words("cz"), words(mode), words(tag), words(left), words(indent)});
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::LayoutIR layout;
  std::optional<geist::detail::VerifiedOwnershipIR> ownership;
  std::optional<geist::detail::ProseTopicIR> prose;
  std::string error;
};

Extracted extract_record(RecordBuilder builder, std::uint32_t record) {
  Extracted extracted;
  extracted.sources.push_back(builder.build(record));
  extracted.layout = geist::detail::extract_layout_ir(extracted.sources);
  extracted.ownership = geist::detail::build_verified_ownership_ir(
      extracted.sources, extracted.layout, &extracted.error);
  require(extracted.ownership.has_value(),
          "the list fixture's frame is not verifiable: " + extracted.error);
  if (!extracted.ownership) return extracted;
  extracted.prose = geist::detail::extract_prose_topic_ir(
      extracted.sources, extracted.layout, *extracted.ownership, topic_title,
      nullptr, &extracted.error);
  if (extracted.prose) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                extracted.sources, extracted.layout, *extracted.ownership,
                topic_title, nullptr, *extracted.prose, &verify_error),
            "the list fixture did not verify: " + verify_error);
  }
  return extracted;
}

geist::detail::TopicIdentityIR identity() {
  geist::detail::TopicIdentityIR topic;
  topic.id = "1.1";
  topic.title = topic_title;
  topic.heading_level = 2;
  topic.start_logical_record = 21;
  topic.end_logical_record = 21;
  return topic;
}

std::optional<geist::detail::DocumentIR> lower_document(
    const Extracted &extracted) {
  if (!extracted.prose) return std::nullopt;
  std::string error;
  auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "the list fixture did not lower: " + error);
  return document;
}

std::string block_text(const geist::detail::ProseBlockIR &block) {
  std::string text;
  for (const auto &inline_node : block.inlines) text += inline_node.text;
  return text;
}

const geist::detail::ProseBlockIR *find_block(
    const geist::detail::ProseTopicIR &topic, ProseBlockKindIR kind,
    const std::string &needle) {
  for (const auto &block : topic.blocks)
    if (block.kind == kind && contains(block_text(block), needle))
      return &block;
  return nullptr;
}

// SC21-8295-03 3.1.3.2 record 405: an outer `UL 3 3` item whose bullet is
// the `U+2666` slot, then `cz FLOW UL 7 7` with three `cz FLOW LI 7 11`
// items stored as `       -   Change management (DSNX)`.  Hosted
// (`SC21-829`, DT 19970409135933) serves `<ul>` / `<li>            Change
// management (DSNX)`: the `-` at the item's `left` column, with the text
// resuming at its `indent`, is the nested bullet and prints nothing.
void a_dash_in_the_bullet_columns_is_the_nested_bullet() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "UL", "3", "3");
  directive(builder, "FLOW", "LI", "3", "7");
  builder.line({words("       "), words("Communications")});
  directive(builder, "FLOW", "UL", "7", "7");
  directive(builder, "FLOW", "LI", "7", "11");
  builder.line({words("       "), words("-"), words("  "), words("Change"),
                words("management")});
  directive(builder, "FLOW", "LI", "7", "11");
  builder.line({words("       "), words("-"), words("  "), words("Alert"),
                words("support")});
  directive(builder, "OFF", "EUL", "4", "4");
  directive(builder, "OFF", "EUL", "0", "0");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the nested dash list was declined: " + extracted.error);
  if (!extracted.prose) return;
  for (const auto *needle : {"Change", "Alert"}) {
    const auto *item =
        find_block(*extracted.prose, ProseBlockKindIR::list_item, needle);
    require(item != nullptr, std::string("the item '") + needle +
                                 "' was not built");
    if (item == nullptr) continue;
    require(block_text(*item).find('-') == std::string::npos,
            "the nested bullet stayed in the item text: '" +
                block_text(*item) + "'");
  }
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(!contains(markdown, "\\-") && contains(markdown, "Change management"),
          "the nested bullet reached the rendered item: " + markdown);
}

// The same character anywhere but alone in the bullet columns is text: a
// row whose text does not resume at the item's `indent`, and a `-` glued to
// the word after it, keep their `-`.
void a_dash_outside_the_bullet_columns_stays_text() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "UL", "7", "7");
  directive(builder, "FLOW", "LI", "7", "11");
  builder.line({words("       "), words("-"), words("Change")});
  directive(builder, "FLOW", "LI", "7", "11");
  builder.line({words("       "), words("-"), words(" "), words("Alert")});
  directive(builder, "OFF", "EUL", "0", "0");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the unbulleted dash list was declined: " + extracted.error);
  if (!extracted.prose) return;
  for (const auto *needle : {"Change", "Alert"}) {
    const auto *item =
        find_block(*extracted.prose, ProseBlockKindIR::list_item, needle);
    require(item != nullptr && block_text(*item).find('-') != std::string::npos,
            std::string("a dash outside the bullet columns was dropped from '") +
                needle + "'");
  }
}

} // namespace

int main() {
  a_dash_in_the_bullet_columns_is_the_nested_bullet();
  a_dash_outside_the_bullet_columns_stays_text();
  geist_test::exit_with_failures();
  std::cout << "cz_list_labels_synthetic: ok\n";
  return 0;
}
