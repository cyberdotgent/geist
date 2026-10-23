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
//  * `OL`: the head is the item's ordinal label (`1.`, `a.`, `7)`, `h)`),
//    which hosted's `<ol>` replaces.  A head of several words (`Step 1.`,
//    `__ 1.`) loses its first word and keeps the rest as the item's label.
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
#include <tuple>
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

// SC09-2417-00 2.2.4 record 210: `cz FLOW NOTEL 3 3`, then items
// `cz FLOW LI 3 7` + `   1.  The type parameter ...`.  Hosted (`SC09-241`,
// DT 19961114175628) serves each item as a bare `<dd>    1.  The ...` with
// the number as text and no list element around them; SC21-8295-03 2.2,
// 3.1.3.2 and SC09-2417-00 3.3.1.9 are served the same way.  The note
// number is the item's text, not a list label the reader regenerates.
void a_note_list_keeps_its_numbers_as_text() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "P", "3", "3");
  builder.line({words("   "), words("Notes:")});
  directive(builder, "FLOW", "NOTEL", "3", "3");
  directive(builder, "FLOW", "LI", "3", "7");
  builder.line({words("   "), words("1."), words(" "), words("The"),
                words("type"), words("parameter.")});
  directive(builder, "FLOW", "LI", "3", "7");
  builder.line({words("   "), words("2."), words(" "), words("Deleted"),
                words("records.")});
  directive(builder, "FLOW", "UL", "7", "7");
  directive(builder, "FLOW", "LI", "7", "11");
  builder.line({words("       "), words("-"), words("  "), words("Skipped")});
  directive(builder, "OFF", "EUL", "4", "4");
  directive(builder, "OFF", "ENOTEL", "0", "0");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the note list was declined: " + extracted.error);
  if (!extracted.prose) return;
  for (const auto *needle : {"1. The type", "2. Deleted"}) {
    const auto *note =
        find_block(*extracted.prose, ProseBlockKindIR::paragraph, needle);
    require(note != nullptr, std::string("the note '") + needle +
                                 "' is not a paragraph keeping its number");
  }
  for (const auto &block : extracted.prose->blocks)
    require(!(block.kind == ProseBlockKindIR::list_item && block.ordered),
            "a note became a numbered list item: '" + block_text(block) +
                "'");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "1\\. The type parameter") &&
              contains(markdown, "2\\. Deleted records") &&
              contains(markdown, "- Skipped"),
          "the note numbers did not survive as text: " + markdown);
}

// SC21-8295-03 E.0 records 836-854 nest `cz FLOW OL` lists whose heads are
// `1)`..`9)` (`LI 16 20`), `a)`..`h)` (`LI 20 24`) and `a.` (`LI 12 16`).
// A head of the form `<n>)`/`<letter>)` standing alone in the head columns,
// with the text resuming at `indent`, is the item's ordinal label exactly
// like `<n>.`: hosted (`SC21-829`, DT 19970409135933) serves
// `<li> <br> |              Press F3 three times.` for `7)  Press F3 ...`.
void a_parenthesis_ordinal_is_the_item_label() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "OL", "16", "16");
  directive(builder, "FLOW", "LI", "16", "20");
  builder.line({words("                "), words("6)"), words(" "),
                words("Select"), words("option.")});
  directive(builder, "FLOW", "LI", "16", "20");
  builder.line({words("                "), words("7)"), words(" "),
                words("Press"), words("F3.")});
  directive(builder, "FLOW", "OL", "20", "20");
  directive(builder, "FLOW", "LI", "20", "24");
  builder.line({words("                    "), words("a)"), words(" "),
                words("Exit"), words("tools.")});
  directive(builder, "OFF", "EOL", "17", "17");
  directive(builder, "OFF", "EOL", "13", "13");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the parenthesis-labelled list was declined: " + extracted.error);
  if (!extracted.prose) return;
  for (const auto &[needle, label] :
       std::vector<std::pair<std::string, std::string>>{
           {"Select", "6)"}, {"Press", "7)"}, {"Exit", "a)"}}) {
    const auto *item =
        find_block(*extracted.prose, ProseBlockKindIR::list_item, needle);
    require(item != nullptr && item->ordered && item->ordinal == label &&
                block_text(*item).find(')') == std::string::npos,
            "the head '" + label + "' is not the item's ordinal label: '" +
                (item == nullptr ? std::string() : block_text(*item)) + "'");
  }
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "7. Press F3") && !contains(markdown, "7\\)"),
          "the parenthesis label was not the list number: " + markdown);
  // A list whose first head is one letter word is lettered: GC35-0317-00
  // C.7 `cz FLOW LI 15 19` + `               a)  <insert>` is served
  // (DT 19980406161309) inside `<ol type=a start=1>` with the head blank,
  // as are the `a.` lists of 89 sampled books (doc/boo-spec/markup.adoc,
  // "List item heads").
  const auto html = geist::detail::render_document_html_blocks(*document, {});
  require(contains(html, "type=\"a\"><li class=\"geist-list-item\" "
                         "value=\"1\">Exit tools.") &&
              !contains(html, "a)"),
          "the letter-headed list is not lettered: " + html);
}

// A head of several words whose last word is an ordinal label.  Hosted
// BookServer blanks the head's first word, numbers the item with its `<ol>`
// and keeps the rest of the head as text (doc/boo-spec/markup.adoc, "List
// item heads"):
//
//  * SC21-8295-03 E.0 record 836: `cz FLOW LI 3 12` + ` | Step 1.  The new
//    release ...`, served (`SC21-829`, DT 19970409135933) as `<li> <br> |
//    1.  The new release ...`;
//  * packet 3.6.3 record 142: `cz FLOW LI 3 10` + `   __ 1.  /etc/...`,
//    served as `<li>       1.  <kbd>/etc/ax25/uronode.announce</kbd>`;
//  * SA41-4150-01 2.2.3 record 165: `cz FLOW LI 3 15` + `   __ Step 1.  Sign
//    on ...`, served (DT 19951214132613) as `<li>       <B>Step</B>
//    <B>1.</B>  Sign on ...`; its nested `__ Step a.` items stand in an
//    `<ol type=1>`, as do SC31-8462-00's `__ a.` items.
//
// The kept words are the item's label: the list numbers the item by its
// position, decimally, and the label prints in front of the text.
void a_head_with_a_word_before_its_ordinal_is_a_label() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "OL", "3", "3");
  directive(builder, "FLOW", "LI", "3", "12");
  builder.line({words("   "), words("Step"), words("1."), words(" "),
                words("Install"), words("it.")});
  directive(builder, "FLOW", "LI", "3", "12");
  builder.line({words("   "), words("Step"), words("2."), words(" "),
                words("Start"), words("it.")});
  directive(builder, "FLOW", "OL", "12", "12");
  directive(builder, "FLOW", "LI", "12", "24");
  builder.line({words("            "), words("__"), words("Step"),
                words("a."), words(" "), words("Check"), words("it.")});
  directive(builder, "OFF", "EOL", "0", "0");
  directive(builder, "OFF", "EOL", "0", "0");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the step list was declined: " + extracted.error);
  if (!extracted.prose) return;
  for (const auto &[needle, label] :
       std::vector<std::pair<std::string, std::string>>{
           {"Install", "1."}, {"Start", "2."}, {"Check", "Step a."}}) {
    const auto *item =
        find_block(*extracted.prose, ProseBlockKindIR::list_item, needle);
    std::string head;
    if (item != nullptr)
      for (std::size_t index = 0;
           index < item->term_inline_count && index < item->inlines.size();
           ++index)
        head += item->inlines[index].text;
    require(item != nullptr && item->ordered && item->ordinal.empty() &&
                head == label,
            "the head of '" + needle + "' is not the label '" + label +
                "': '" + head + "'");
    require(item == nullptr || label == "Step a." ||
                block_text(*item).find("Step") == std::string::npos,
            "the blanked head word survived in '" + needle + "'");
  }
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "1. 1\\. Install it") &&
              contains(markdown, "2. 2\\. Start it") &&
              contains(markdown, "1. Step a\\. Check it") &&
              !contains(markdown, "Step 1"),
          "the step list did not carry its labels: " + markdown);
  const auto html = geist::detail::render_document_html_blocks(*document, {});
  require(contains(html,
                   "<span class=\"geist-list-label\">1.</span> Install") &&
              contains(html, "<span class=\"geist-list-label\">Step "
                             "a.</span> Check") &&
              !contains(html, "type=\"a\"") && !contains(html, "Step 1"),
          "the step list's HTML did not carry its labels: " + html);
}

// A revision flag in front of an ordered item's label.  The row stores a
// one-cell blank, the flag at column 1 and the label at the item's `left`
// column, with the text resuming at `indent`: SC33-1169-00 A.1 record 1840
// `cz FLOW LI 3 7` + ` # 1.  Create the keys`, which hosted (DT
// 19990319122050) serves as `<li> <br> #     Create the keys`.  The label
// columns are blank, as for any `OL` head, and the flag stays visible on
// the line the break opens; `+`, `|` and `g` flags are served the same way
// (doc/boo-spec/markup.adoc, "A revision flag before an ordered item").
void a_revision_flag_before_an_ordinal_opens_the_item_line() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "OL", "3", "3");
  directive(builder, "FLOW", "LI", "3", "7");
  builder.line({words(" "), words("#"), words("1."), words(" "),
                words("Create"), words("the"), words("keys")});
  directive(builder, "FLOW", "OL", "5", "5");
  directive(builder, "FLOW", "LI", "5", "9");
  builder.line({words(" "), words("+"), words("  "), words("a."),
                words(" "), words("Read"), words("a"), words("record")});
  directive(builder, "OFF", "EOL", "0", "0");
  directive(builder, "FLOW", "LI", "3", "7");
  builder.line({words("   "), words("2."), words(" "), words("Sort"),
                words("the"), words("data")});
  directive(builder, "OFF", "EOL", "0", "0");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the flagged list was declined: " + extracted.error);
  if (!extracted.prose) return;
  for (const auto &[needle, flag, label] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"Create", "#", "1."}, {"Read", "+", "a."}}) {
    const auto *item =
        find_block(*extracted.prose, ProseBlockKindIR::list_item, needle);
    // The blanked label numbers nothing: the list keeps its own alphabet
    // and the item its position.
    require(item != nullptr && item->ordered && item->ordinal.empty(),
            "the flagged head '" + label + "' became the item's ordinal");
    if (item == nullptr) continue;
    require(!item->inlines.empty() &&
                item->inlines.front().kind ==
                    geist::detail::ProseInlineKindIR::line_break,
            "the flagged item does not open with a line break: '" +
                block_text(*item) + "'");
    require(block_text(*item).rfind(flag + " " + needle, 0) == 0 &&
                block_text(*item).find(label) == std::string::npos,
            "the flagged item is not flag + text: '" + block_text(*item) +
                "'");
  }
  const auto *plain =
      find_block(*extracted.prose, ProseBlockKindIR::list_item, "Sort");
  require(plain != nullptr && plain->ordinal == "2." &&
              plain->inlines.front().kind !=
                  geist::detail::ProseInlineKindIR::line_break,
          "an unflagged item gained a line break");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "1. <br>\\# Create the keys") &&
              contains(markdown, "1. <br>\\+ Read a record") &&
              contains(markdown, "2. Sort the data") &&
              !contains(markdown, "1\\.") && !contains(markdown, "a\\."),
          "the flagged items did not render as hosted serves them: " +
              markdown);
  const auto html = geist::detail::render_document_html_blocks(*document, {});
  require(contains(html, "<br># Create the keys") &&
              contains(html, "<br>+ Read a record") && !contains(html, "type=\"a\"") &&
              !contains(html, "geist-list-label"),
          "the flagged items' HTML is not break + flag + text: " + html);
}

// A flag whose row carries no ordinal label at `left` proves nothing about
// a head: the row is left as it is stored, flag and words as text.
void a_flag_without_an_ordinal_stays_text() {
  RecordBuilder builder;
  metadata(builder);
  directive(builder, "FLOW", "OL", "3", "3");
  directive(builder, "FLOW", "LI", "3", "7");
  builder.line({words(" "), words("#"), words("Create"), words("the"),
                words("keys")});
  directive(builder, "OFF", "EOL", "0", "0");

  const auto extracted = extract_record(std::move(builder), 21);
  require(extracted.prose.has_value(),
          "the unlabelled flagged list was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto *item =
      find_block(*extracted.prose, ProseBlockKindIR::list_item, "Create");
  require(item != nullptr && item->ordinal.empty() &&
              block_text(*item).rfind("# Create", 0) == 0 &&
              item->inlines.front().kind !=
                  geist::detail::ProseInlineKindIR::line_break,
          "an unlabelled flagged row was rewritten: '" +
              (item == nullptr ? std::string() : block_text(*item)) + "'");
}

} // namespace

int main() {
  a_dash_in_the_bullet_columns_is_the_nested_bullet();
  a_dash_outside_the_bullet_columns_stays_text();
  a_note_list_keeps_its_numbers_as_text();
  a_parenthesis_ordinal_is_the_item_label();
  a_head_with_a_word_before_its_ordinal_is_a_label();
  a_revision_flag_before_an_ordinal_opens_the_item_line();
  a_flag_without_an_ordinal_stays_text();
  geist_test::exit_with_failures();
  std::cout << "cz_list_labels_synthetic: ok\n";
  return 0;
}
