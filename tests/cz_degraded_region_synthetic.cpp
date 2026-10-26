// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// An unmodelled `cz OFF <tag>` region degrades its block, not its topic
// (issue #81).
//
// The CZ dialect declined a whole topic at `CZ layout cz off <tag> is not
// modelled`. Under the block-level degradation invariant that is more than
// the evidence requires when the source itself closes the region:
//
//   1. the topic's frame -- metadata envelope, segmentation, layout ledger,
//      ownership -- is proven before the CZ block builder runs at all;
//   2. the region's boundary is proven *independently of the failing check*
//      by a matched `cz OFF <tag>` / `cz OFF E<tag>` pair the frame already
//      recognised as layout directives; the check that failed is only that
//      the tag has no model;
//   3. that boundary is a block boundary: a `cz OFF` directive is the
//      dialect's own block delimiter, so the extents of the blocks before and
//      after are fixed by the delimiters and not by the region's content;
//   4. the block claims every text token of every display row between them;
//   5. it lowers to display rows and nothing else -- no inline model, no
//      links, no ordinal, no nesting;
//   6. it is marked at the block, with a stable code and the check that
//      failed.
//
// So the region went out verbatim, marked degraded, and the prose around it
// stayed typed. A region the source never closes with its own `E<tag>` fails
// condition 2 and the topic still declines whole.
//
// The region is now modelled: BookServer serves every `cz OFF <tag>` it has
// no rule for as one generic block -- `<pre width="80">` and the rows as
// stored, highlighting and links kept, up to the next `CZ FLOW` -- and the
// hosted census of 22 books with such a closed region confirms it.  The
// unmodelled tag below is therefore a typed verbatim block, no longer a
// degraded one; the open region still declines.
//
// This is deliberately synthetic. The tag that happens to be unmodelled moves
// as the families land -- `cz OFF COVER` and `cz OFF TIPAGE` reached this
// path until #74 modelled the generated title-page projection, and
// `packet.boo`, the only redistributable fixture (#59), now has no topic that
// reaches it at all. Pinning the mechanism on a hand-built record keeps the
// invariant under test whatever the modelled tag list happens to be.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
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
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "cz_degraded_region_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// The same builder shape the other record-level synthetics use: one display
// line per call, its length byte first, then the line's own tokens.  Nothing
// here re-derives the framing; the decoder's own walk reads it back.
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

// A CZ-dialect topic: the metadata envelope, the `ST` title, one typed
// paragraph, a region opened by an unmodelled `cz OFF <tag>` and closed by
// `closer` with two display rows in it, and a typed paragraph after the
// closer.  Passing a closer that does not match the opener produces the
// never-closed shape the invariant must refuse.
DecodedLogicalRecordSource topic_record(const std::string &tag,
                                        const std::string &closer) {
  RecordBuilder builder;
  builder.line({words("sh1.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("9"), words("0"), words("9")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  builder.line({words("ST"), words("Widget"), words("Notes")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Read"), words("the"), words("widget"),
                words("note.")});
  builder.line({words("cz"), words("OFF"), words(tag)});
  builder.line({words("   "), words("alpha"), words("beta")});
  builder.line({words("   "), words("gamma"), words("delta")});
  builder.line({words("cz"), words("OFF"), words(closer), words("0"),
                words("0")});
  builder.line({words("   "), words("After"), words("the"), words("region.")});
  return builder.build(21);
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::LayoutIR layout;
  std::optional<geist::detail::VerifiedOwnershipIR> ownership;
  std::optional<geist::detail::ProseTopicIR> prose;
  std::string error;
};

Extracted extract(const std::string &tag, const std::string &closer) {
  Extracted out;
  auto record = topic_record(tag, closer);
  require(record.ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");
  out.sources.push_back(std::move(record));
  out.layout = geist::detail::extract_layout_ir(out.sources);
  out.ownership = geist::detail::build_verified_ownership_ir(
      out.sources, out.layout, &out.error);
  // Invariant 1: the frame must be proven before degradation is even
  // reachable, and it is proven the same way for both shapes below.
  require(out.ownership.has_value(),
          "the synthetic topic's frame is not verifiable: " + out.error);
  if (!out.ownership) return out;
  out.prose = geist::detail::extract_prose_topic_ir(
      out.sources, out.layout, *out.ownership, topic_title, nullptr,
      &out.error);
  return out;
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

void inferred_parameter_prelude(RecordBuilder &builder) {
  builder.line({words("cz"), words("OFF"), words("SYNTAX")});
  builder.line({words("   "), words(">>WIDGET<<")});
  builder.line({words("cz"), words("OFF"), words("ESYNTAX"), words("0"),
                words("0")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("ALl")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("is"), words("the"),
                words("default.")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});
}

Extracted extract_record(RecordBuilder builder, std::uint32_t record) {
  Extracted extracted;
  extracted.sources.push_back(builder.build(record));
  extracted.layout = geist::detail::extract_layout_ir(extracted.sources);
  extracted.ownership = geist::detail::build_verified_ownership_ir(
      extracted.sources, extracted.layout, &extracted.error);
  require(extracted.ownership.has_value(),
          "the compound CZ fixture's frame is not verifiable: " +
              extracted.error);
  if (!extracted.ownership) return extracted;
  extracted.prose = geist::detail::extract_prose_topic_ir(
      extracted.sources, extracted.layout, *extracted.ownership, topic_title,
      nullptr, &extracted.error);
  if (extracted.prose) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                extracted.sources, extracted.layout, *extracted.ownership,
                topic_title, nullptr, *extracted.prose, &verify_error),
            "the compound CZ source tree did not verify: " + verify_error);
  }
  return extracted;
}

std::optional<geist::detail::DocumentIR> lower_document(
    const Extracted &extracted) {
  if (!extracted.prose) return std::nullopt;
  std::string error;
  auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(),
          "the compound CZ tree did not lower: " + error);
  return document;
}

// H6 is a real CZ flow heading, not body prose.  DFHP7A00 2.3.4.5.2 stores
// `cz FLOW H6 3 3` before each command-family subheading; the source heading
// level, rather than its displayed words, is the structural evidence.
void an_h6_directive_builds_a_level_six_heading() {
  RecordBuilder builder;
  builder.line({words("sh1.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("3"), words("0"), words("3")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  builder.line({words("ST"), words("Widget"), words("Notes")});
  builder.line({words("cz"), words("FLOW"), words("H6"), words("3"),
                words("3")});
  builder.line({words("   "), words("Connection"), words("properties")});

  Extracted extracted;
  extracted.sources.push_back(builder.build(22));
  extracted.layout = geist::detail::extract_layout_ir(extracted.sources);
  extracted.ownership = geist::detail::build_verified_ownership_ir(
      extracted.sources, extracted.layout, &extracted.error);
  require(extracted.ownership.has_value(),
          "the H6 fixture's frame is not verifiable: " + extracted.error);
  if (!extracted.ownership) return;
  extracted.prose = geist::detail::extract_prose_topic_ir(
      extracted.sources, extracted.layout, *extracted.ownership, topic_title,
      nullptr, &extracted.error);
  require(extracted.prose.has_value(),
          "cz FLOW H6 was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto heading = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::heading;
      });
  require(heading != extracted.prose->blocks.end() &&
              heading->heading_level == 6,
          "cz FLOW H6 did not retain heading level 6");

  std::string error;
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "lowering the H6 fixture failed: " + error);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "###### Connection properties"),
          "the H6 heading did not survive DocumentIR rendering: " + markdown);
}

// A definition body may have its own DD directive instead of sharing the DT
// display row.  DFHP7A00 1.2.4 stores every function name in `cz FLOW DT 3 6`
// and its explanation in the following `cz FLOW DD 6 6`.
void a_separate_dd_completes_its_definition_term() {
  RecordBuilder builder;
  builder.line({words("sh1.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("5"), words("0"), words("5")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  builder.line({words("ST"), words("Widget"), words("Notes")});
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("6")});
  builder.line({words("   "), words("Monitor:")});
  builder.line({words("cz"), words("FLOW"), words("DD"), words("6"),
                words("6")});
  builder.line({words("      "), words("Handles"), words("unexpected"),
                words("events.")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  Extracted extracted;
  extracted.sources.push_back(builder.build(23));
  extracted.layout = geist::detail::extract_layout_ir(extracted.sources);
  extracted.ownership = geist::detail::build_verified_ownership_ir(
      extracted.sources, extracted.layout, &extracted.error);
  require(extracted.ownership.has_value(),
          "the DD fixture's frame is not verifiable: " + extracted.error);
  if (!extracted.ownership) return;
  extracted.prose = geist::detail::extract_prose_topic_ir(
      extracted.sources, extracted.layout, *extracted.ownership, topic_title,
      nullptr, &extracted.error);
  require(extracted.prose.has_value(),
          "a separate cz FLOW DD was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto entry = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::definition_entry;
      });
  require(entry != extracted.prose->blocks.end() &&
              entry->term_inline_count != 0 &&
              entry->term_inline_count < entry->inlines.size(),
          "the separate DT/DD pair is not one definition entry");

  std::string error;
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "lowering the DD fixture failed: " + error);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "**Monitor:**") &&
              contains(markdown, "Handles unexpected events\\."),
          "the DT/DD definition did not survive rendering: " + markdown);
}

// A parameter list immediately following a syntax diagram can omit its
// `FLOW PARML` opener.  The source still declares every entry with PT, its
// body with the following BREAK, and the list boundary with EPARML.  This is
// the form used by DFHP7A00 2.5.3--2.5.7 for syntax operands.
void syntax_followed_by_pt_builds_an_implicit_parameter_list() {
  RecordBuilder builder;
  builder.line({words("sh1.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("8"), words("0"), words("8")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  builder.line({words("ST"), words("Widget"), words("Notes")});
  builder.line({words("cz"), words("OFF"), words("SYNTAX")});
  builder.line({words("   "), words(">>WIDGET<<")});
  builder.line({words("cz"), words("OFF"), words("ESYNTAX"), words("0"),
                words("0")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("ALl")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("is"), words("the"),
                words("default.")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("Node(value)")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("selects"), words("a"),
                words("node.")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});

  Extracted extracted;
  extracted.sources.push_back(builder.build(24));
  extracted.layout = geist::detail::extract_layout_ir(extracted.sources);
  extracted.ownership = geist::detail::build_verified_ownership_ir(
      extracted.sources, extracted.layout, &extracted.error);
  require(extracted.ownership.has_value(),
          "the PT fixture's frame is not verifiable: " + extracted.error);
  if (!extracted.ownership) return;
  extracted.prose = geist::detail::extract_prose_topic_ir(
      extracted.sources, extracted.layout, *extracted.ownership, topic_title,
      nullptr, &extracted.error);
  require(extracted.prose.has_value(),
          "the implicit PT parameter list was declined: " + extracted.error);
  if (!extracted.prose) return;
  std::vector<const geist::detail::ProseBlockIR *> entries;
  for (const auto &block : extracted.prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::definition_entry)
      entries.push_back(&block);
  require(entries.size() == 2 &&
              entries[0]->list_ordinal == entries[1]->list_ordinal &&
              entries[0]->term_inline_count != 0 &&
              entries[0]->term_inline_count < entries[0]->inlines.size(),
          "PT/BREAK did not retain two entries of one parameter list");

  std::string error;
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "lowering the PT fixture failed: " + error);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "**ALl:** is the default\\.") &&
              contains(markdown,
                       "**Node\\(value\\):** selects a node\\."),
          "the PT/BREAK parameter list did not survive rendering: " +
              markdown);
}

// The opener is omitted after other blocks too, and the entries hold
// paragraphs between them as an opened list does: SC41-5864-02 1.12.1
// follows a table with `cz FLOW PT 3 7` `Length of receiver variable`,
// `cz BREAK 7` `INPUT; BINARY(4)` and a `cz FLOW P 7 7` description, and
// closes the run with `cz OFF EPARML 0 0`.  Hosted serves the term, `<br>`
// and the indented body, exactly like an entry of an opened list.
void a_term_after_a_paragraph_opens_the_list_its_closer_ends() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("The"), words("parameters"),
                words("are:")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("Length")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("INPUT;"), words("BINARY(4)")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("7"),
                words("7")});
  builder.line({words("       "), words("The"), words("size"),
                words("of"), words("the"), words("variable.")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("Request")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("INPUT;"), words("CHAR(53)")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Afterwards.")});

  const auto extracted = extract_record(std::move(builder), 26);
  require(extracted.prose.has_value(),
          "a parameter term after a paragraph was declined: " +
              extracted.error);
  if (!extracted.prose) return;
  std::vector<const geist::detail::ProseBlockIR *> entries;
  for (const auto &block : extracted.prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::definition_entry)
      entries.push_back(&block);
  require(entries.size() == 2 &&
              entries[0]->list_ordinal == entries[1]->list_ordinal,
          "the two terms are not entries of one parameter list");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "The size of the variable.") &&
              contains(markdown, "Afterwards\\.") &&
              markdown.find("Afterwards") > markdown.find("CHAR(53)"),
          "the implicit list lost its body or its trailing paragraph: " +
              markdown);
}

// Without a closer nothing frames the list, and the term still fails.
void a_term_without_a_list_closer_still_declines() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("The"), words("parameters"),
                words("are:")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("Length")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("INPUT;"), words("BINARY(4)")});

  const auto extracted = extract_record(std::move(builder), 27);
  require(!extracted.prose.has_value() &&
              contains(extracted.error, "outside an open parameter list"),
          "a parameter term with no list closer was admitted");
}

// Parameter descriptions in DFHP7A00 2.5.3--2.5.5 and 2.5.7 contain
// source-declared lists after the PT/BREAK paragraph.  The child list keeps
// global block identities for the token ledger while the parameter entry
// explicitly owns it as body content.
void a_nested_list_in_a_parameter_body_keeps_source_ownership() {
  RecordBuilder builder;
  metadata(builder);
  inferred_parameter_prelude(builder);
  builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("State(value)")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("The"), words("values"),
                words("are:")});
  builder.line({words("cz"), words("FLOW"), words("DL"), words("7"),
                words("7")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("7"),
                words("20")});
  builder.line({words("       "), words("OPEN"), words("         "),
                words("Available")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("7"),
                words("20")});
  builder.line({words("       "), words("CLOSED"), words("       "),
                words("Unavailable")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 25);
  require(extracted.prose.has_value(),
          "a nested parameter-body list was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto owner = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::definition_entry &&
               std::any_of(block.inlines.begin(), block.inlines.end(),
                           [](const auto &item) {
                             return contains(item.text, "State(value)");
                           });
      });
  require(owner != extracted.prose->blocks.end() &&
              owner->body_blocks.size() == 2,
          "the parameter entry does not own both nested list entries");
  if (owner == extracted.prose->blocks.end() || owner->body_blocks.empty())
    return;
  const auto owner_index = static_cast<std::size_t>(
      std::distance(extracted.prose->blocks.begin(), owner));
  for (const auto child : owner->body_blocks)
    require(child < extracted.prose->blocks.size() &&
                extracted.prose->blocks[child].parent_block == owner_index &&
                extracted.prose->blocks[child].kind ==
                    geist::detail::ProseBlockKindIR::definition_entry,
            "a nested definition entry lost its parameter-body parent");
  require(owner->body_blocks.size() != 2 ||
              owner->body_blocks[0] < owner->body_blocks[1],
          "nested parameter blocks lost source order");
  if (owner->body_blocks.size() == 2) {
    auto reordered = *extracted.prose;
    auto &children = reordered.blocks[owner_index].body_blocks;
    std::swap(children[0], children[1]);
    std::string error;
    require(!geist::detail::verify_prose_topic_ir(
                extracted.sources, extracted.layout, *extracted.ownership,
                topic_title, nullptr, reordered, &error) &&
                contains(error, "body children are not in source order"),
            "the verifier admitted reordered compound children: " + error);
  }
  const auto document = lower_document(extracted);
  if (!document) return;
  bool found_compound = false;
  for (const auto &candidate : document->blocks) {
    const auto *definitions =
        std::get_if<geist::detail::DefinitionListBlockIR>(&candidate.node);
    if (definitions == nullptr) continue;
    for (const auto &entry : definitions->entries) {
      if (entry.definition_blocks.size() < 2 ||
          !std::holds_alternative<geist::detail::ParagraphBlockIR>(
              entry.definition_blocks.front().node))
        continue;
      found_compound = std::holds_alternative<
          geist::detail::DefinitionListBlockIR>(
          entry.definition_blocks[1].node);
    }
  }
  require(found_compound,
          "the nested definition list was not lowered after its body paragraph");
}

// DFHP7A00 2.5.3 continues a parameter description with FLOW PC after a
// nested value list.  PC remains part of the same definition body; emitting a
// top-level paragraph would lose that ownership.
void a_parameter_paragraph_continuation_keeps_source_ownership() {
  RecordBuilder builder;
  metadata(builder);
  inferred_parameter_prelude(builder);
  builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("State(value)")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("The"), words("state.")});
  builder.line({words("cz"), words("FLOW"), words("PC"), words("7"),
                words("7")});
  builder.line({words("       "), words("Pending"), words("states"),
                words("continue.")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 26);
  require(extracted.prose.has_value(),
          "a parameter-body PC was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto child = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::paragraph &&
               std::any_of(block.inlines.begin(), block.inlines.end(),
                           [](const auto &item) {
                             return contains(item.text, "Pending");
                           });
      });
  require(child != extracted.prose->blocks.end() &&
              child->parent_block != static_cast<std::size_t>(-1) &&
              extracted.prose->blocks[child->parent_block].kind ==
                  geist::detail::ProseBlockKindIR::definition_entry,
          "the parameter PC is not owned by its definition entry");
}

// A parameter body whose first paragraph styles part of one word, followed
// by a further paragraph of the same entry.  dfha2a06 1.2.4.1.4 record 105
// stores `cz BREAK 7`, `cfont 30 2 V` and the row `       parameter in the
// CONSOLnn PARMLIB member ...`, the `nn` of the one stored word `CONSOLnn`
// styled, then `cz FLOW P 7 7` inside the same entry.  The inline paragraph
// of the definition owns the word once, whatever its inlines split it into.
void a_parameter_body_with_a_subword_style_lowers() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("CONSNAME(name)")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("cfont"), words("17"), words("2"), words("V")});
  builder.line({words("       "), words("the"), words("CONSOLnn"),
                words("member.")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("7"),
                words("7")});
  builder.line({words("       "), words("Define"), words("it"),
                words("always.")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 28);
  require(extracted.prose.has_value(),
          "a parameter body with a sub-word style was declined: " +
              extracted.error);
  const auto document = lower_document(extracted);
  if (!document) return;
  std::string error;
  require(geist::detail::verify_document_ir(*document, &error),
          "the parameter body's DocumentIR does not verify: " + error);
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "CONSOL") && contains(markdown, "always"),
          "the parameter body lost its text: " + markdown);
}

// A term with no row of its own, followed by a `cz BREAK` with rows.  The
// reader prints nothing for the generic `CZ FLOW` and `<br>` plus the rows
// for the break.  SC31-8513-00 2.5.4.35 stores `cz FLOW PT 3 7` `   LU -
// LU_name`, a row-less `cz FLOW PT 3 7` and `cz BREAK 3` with the
// description; hosted (DT 19980219192539) serves `<kbd>LU</kbd> <kbd>-</kbd>
// <var>LU_name</var>` `<p><br>` and the description, under the term.
const geist::detail::ProseBlockIR *entry_with(
    const geist::detail::ProseTopicIR &topic, const std::string &needle) {
  for (const auto &block : topic.blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::definition_entry &&
        std::any_of(block.inlines.begin(), block.inlines.end(),
                    [&](const auto &item) { return contains(item.text, needle); }))
      return &block;
  return nullptr;
}

void a_rowless_term_before_a_break_hands_the_body_on() {
  {
    RecordBuilder builder;
    metadata(builder);
    builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                  words("3")});
    builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                  words("7")});
    builder.line({words("   "), words("LU_name")});
    builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                  words("7")});
    builder.line({words("cz"), words("BREAK"), words("3")});
    builder.line({words("       "), words("The"), words("logical"),
                  words("name.")});
    builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                  words("0")});
    const auto extracted = extract_record(std::move(builder), 31);
    require(extracted.prose.has_value(),
            "a row-less term before a break was declined: " + extracted.error);
    if (extracted.prose) {
      const auto *entry = entry_with(*extracted.prose, "LU_name");
      require(entry != nullptr && entry->term_inline_count != 0 &&
                  contains(entry->inlines.back().text, "name."),
              "the break's rows are not the waiting term's body");
    }
  }
  {
    // The term already stood alone before a row-less break (dfhp3a06
    // 4.6.4.2 `counter_options`, `cz BREAK 7`, then the row-less PT and
    // `cz BREAK 3` with the description).
    RecordBuilder builder;
    metadata(builder);
    builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                  words("3")});
    builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                  words("7")});
    builder.line({words("   "), words("counter_options")});
    builder.line({words("cz"), words("BREAK"), words("7")});
    builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                  words("7")});
    builder.line({words("cz"), words("BREAK"), words("3")});
    builder.line({words("       "), words("specifies"), words("the"),
                  words("options.")});
    builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                  words("0")});
    const auto extracted = extract_record(std::move(builder), 32);
    require(extracted.prose.has_value(),
            "a row-less term after a lone term was declined: " +
                extracted.error);
    if (extracted.prose) {
      const auto *entry = entry_with(*extracted.prose, "counter_options");
      require(entry != nullptr &&
                  contains(entry->inlines.back().text, "options."),
              "the break's rows are not the lone term's body");
    }
  }
  {
    // No term waits: the rows are the body of an entry with no term
    // (dfhwsb02 6.2.3 `cz FLOW PT 3 7`, `cz BREAK 3` with
    // `replyDestination=reply_queue`, `cz BREAK 7` with its description).
    RecordBuilder builder;
    metadata(builder);
    builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                  words("3")});
    builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                  words("7")});
    builder.line({words("   "), words("priority=n")});
    builder.line({words("cz"), words("BREAK"), words("7")});
    builder.line({words("       "), words("Specifies"), words("priority.")});
    builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                  words("7")});
    builder.line({words("cz"), words("BREAK"), words("3")});
    builder.line({words("   "), words("reply=queue")});
    builder.line({words("cz"), words("BREAK"), words("7")});
    builder.line({words("       "), words("Specifies"), words("the"),
                  words("queue.")});
    builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                  words("0")});
    const auto extracted = extract_record(std::move(builder), 33);
    require(extracted.prose.has_value(),
            "a row-less term with no waiting term was declined: " +
                extracted.error);
    const auto document = lower_document(extracted);
    if (!document) return;
    const auto markdown = geist::detail::render_document_markdown(*document);
    require(contains(markdown, "reply=queue") &&
                contains(markdown, "the queue."),
            "the row-less term's break rows were lost: " + markdown);
  }
}

// A paragraph while an LI remains open belongs to that item, even when the
// whole list is itself the block body of a parameter.  Both ownership edges
// must survive without replacing the topic-wide source-order block indices.
void a_list_item_continuation_keeps_recursive_source_ownership() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("PARML"), words("3"),
                words("7")});
  builder.line({words("cz"), words("FLOW"), words("PT"), words("3"),
                words("7")});
  builder.line({words("   "), words("Mode")});
  builder.line({words("cz"), words("BREAK"), words("7")});
  builder.line({words("       "), words("selects"), words("one"),
                words("of:")});
  builder.line({words("cz"), words("FLOW"), words("UL"), words("7"),
                words("7")});
  builder.line({words("cz"), words("FLOW"), words("LI"), words("7"),
                words("7")});
  builder.line({words("       "), words("OPEN")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("11"),
                words("11")});
  builder.line({words("           "), words("Allows"), words("work.")});
  builder.line({words("cz"), words("FLOW"), words("LI"), words("7"),
                words("7")});
  builder.line({words("       "), words("CLOSED")});
  builder.line({words("cz"), words("OFF"), words("EUL"), words("0"),
                words("0")});
  builder.line({words("cz"), words("OFF"), words("EPARML"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 29);
  require(extracted.prose.has_value(),
          "a list-item continuation was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto item = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::list_item &&
               std::any_of(block.inlines.begin(), block.inlines.end(),
                           [](const auto &inline_node) {
                             return contains(inline_node.text, "OPEN");
                           });
      });
  const auto sibling = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::list_item &&
               std::any_of(block.inlines.begin(), block.inlines.end(),
                           [](const auto &inline_node) {
                             return contains(inline_node.text, "CLOSED");
                           });
      });
  const auto continuation = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::paragraph &&
               std::any_of(block.inlines.begin(), block.inlines.end(),
                           [](const auto &inline_node) {
                             return contains(inline_node.text, "Allows");
                           });
      });
  require(item != extracted.prose->blocks.end() &&
              sibling != extracted.prose->blocks.end() &&
              continuation != extracted.prose->blocks.end(),
          "the list items or first item's continuation are missing");
  if (item == extracted.prose->blocks.end() ||
      sibling == extracted.prose->blocks.end() ||
      continuation == extracted.prose->blocks.end())
    return;
  const auto item_index = static_cast<std::size_t>(
      std::distance(extracted.prose->blocks.begin(), item));
  const auto continuation_index = static_cast<std::size_t>(
      std::distance(extracted.prose->blocks.begin(), continuation));
  require(item->parent_block != static_cast<std::size_t>(-1) &&
              extracted.prose->blocks[item->parent_block].kind ==
                  geist::detail::ProseBlockKindIR::definition_entry &&
              sibling->parent_block == item->parent_block &&
              continuation->parent_block == item_index &&
              item->body_blocks.size() == 1 &&
              item->body_blocks.front() == continuation_index,
          "the parameter/list-item ownership chain was flattened");
  const auto document = lower_document(extracted);
  if (!document) return;
  bool found_compound_item = false;
  for (const auto &candidate : document->blocks) {
    const auto *definitions =
        std::get_if<geist::detail::DefinitionListBlockIR>(&candidate.node);
    if (definitions == nullptr) continue;
    for (const auto &entry : definitions->entries) {
      for (const auto &body : entry.definition_blocks) {
        const auto *list =
            std::get_if<geist::detail::ListBlockIR>(&body.node);
        if (list == nullptr || list->items.size() != 2) continue;
        found_compound_item = list->items.front().content.empty() &&
                              list->items.front().blocks.size() == 2 &&
                              !list->items.back().content.empty() &&
                              list->items.back().blocks.empty();
      }
    }
  }
  require(found_compound_item,
          "the list-item continuation did not lower inside its item");
}

// GA19 FRONT_3 uses a deliberately empty DT as the owner of a following
// LINES address block.  The empty term and nested preformatted body are both
// source facts; neither may be fabricated as ordinary inline text.
void an_empty_dt_before_lines_keeps_a_compound_definition() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("25")});
  builder.line({words("cz"), words("OFF"), words("LINES")});
  builder.line({words("                         "), words("Nigel"),
                words("Adams")});
  builder.line({words("cz"), words("OFF"), words("ELINES"), words("22"),
                words("22")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 27);
  require(extracted.prose.has_value(),
          "an empty DT plus LINES body was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto entry = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::definition_entry;
      });
  require(entry != extracted.prose->blocks.end() && entry->inlines.empty() &&
              entry->body_blocks.size() == 1 &&
              extracted.prose->blocks[entry->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::preformatted,
          "the empty term does not own its LINES block");
  if (entry == extracted.prose->blocks.end()) return;
  std::vector<std::size_t> cz_segments;
  for (const auto &segment : extracted.sources.front().control_segments)
    if (segment.opcode == "cz") cz_segments.push_back(segment.segment_index);
  require(entry->slices.size() == 1 && cz_segments.size() >= 2 &&
              entry->slices.front().segment_index == cz_segments[1],
          "the empty term does not retain its DT control provenance");
  auto unproven = *extracted.prose;
  const auto entry_index = static_cast<std::size_t>(
      std::distance(extracted.prose->blocks.begin(), entry));
  unproven.blocks[entry_index].slices.clear();
  std::string error;
  require(!geist::detail::verify_prose_topic_ir(
              extracted.sources, extracted.layout, *extracted.ownership,
              topic_title, nullptr, unproven, &error) &&
              contains(error,
                       "empty definition entry has no control provenance"),
          "the verifier admitted an unproven empty term: " + error);
  const auto document = lower_document(extracted);
  if (!document) return;
  bool found_empty_term = false;
  for (const auto &candidate : document->blocks) {
    const auto *definitions =
        std::get_if<geist::detail::DefinitionListBlockIR>(&candidate.node);
    if (definitions == nullptr) continue;
    for (const auto &lowered : definitions->entries)
      found_empty_term =
          found_empty_term ||
          (lowered.empty_term && lowered.term.empty() &&
           lowered.definition.empty() &&
           lowered.definition_blocks.size() == 1 &&
           std::holds_alternative<geist::detail::PreformattedBlockIR>(
               lowered.definition_blocks.front().node));
  }
  require(found_empty_term,
          "the explicit empty term did not lower with its LINES body");
  auto undisposed = *document;
  for (auto &candidate : undisposed.blocks) {
    auto *definitions =
        std::get_if<geist::detail::DefinitionListBlockIR>(&candidate.node);
    if (definitions == nullptr) continue;
    for (auto &lowered : definitions->entries)
      if (lowered.empty_term) lowered.empty_term = false;
  }
  std::string document_error;
  require(!geist::detail::verify_document_ir(undisposed, &document_error) &&
              contains(document_error,
                       "empty-term disposition disagrees"),
          "DocumentIR admitted an unexplained empty definition term: " +
              document_error);
}

// GA19 19.22 puts each printer family in DT, opens an empty DD, then supplies
// the printer names as a LINES region.  The region is a block body of that
// entry, not a sibling preformatted block.
void an_empty_dd_before_lines_keeps_a_compound_definition() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("4")});
  builder.line({words("   "), words("9-pin")});
  builder.line({words("cz"), words("FLOW"), words("DD"), words("4"),
                words("4")});
  builder.line({words("cz"), words("OFF"), words("LINES")});
  builder.line({words("    "), words("IBM"), words("2380"),
                words("Series"), words("II")});
  builder.line({words("cz"), words("OFF"), words("ELINES"), words("1"),
                words("1")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 28);
  require(extracted.prose.has_value(),
          "an empty DD plus LINES body was declined: " + extracted.error);
  if (!extracted.prose) return;
  const auto entry = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::definition_entry;
      });
  require(entry != extracted.prose->blocks.end() && !entry->inlines.empty() &&
              entry->term_inline_count == entry->inlines.size() &&
              entry->body_blocks.size() == 1 &&
              extracted.prose->blocks[entry->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::preformatted,
          "the DD term does not own its LINES block");
}

// GX27-3999-00 2.6.1 and 2.6.2 put an XMP example inside an outer LINES
// region, and 2.6.1 also uses the inverse nesting.  Both delimiter pairs are
// explicit source structure; the inner pair does not close or interrupt the
// outer fixed-line block.
void balanced_nested_verbatim_delimiters_stay_inside_the_outer_region() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("OFF"), words("LINES")});
  builder.line({words("cz"), words("OFF"), words("XMP")});
  builder.line({words("      "), words("DEVICE=C:\\SECOND.SYS")});
  builder.line({words("cz"), words("OFF"), words("EXMP"), words("4"),
                words("4")});
  builder.line({words("cz"), words("OFF"), words("ELINES"), words("4"),
                words("4")});
  builder.line({words("cz"), words("OFF"), words("XMP")});
  builder.line({words("cz"), words("OFF"), words("LINES")});
  builder.line({words("      "), words("LOAD"), words("SECOND.NLM")});
  builder.line({words("cz"), words("OFF"), words("ELINES"), words("4"),
                words("4")});
  builder.line({words("cz"), words("OFF"), words("EXMP"), words("4"),
                words("4")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Continue"), words("afterwards.")});

  const auto extracted = extract_record(std::move(builder), 30);
  require(extracted.prose.has_value(),
          "nested verbatim pairs made an outer region appear unclosed: " +
              extracted.error);
  if (!extracted.prose) return;
  std::vector<const geist::detail::ProseBlockIR *> regions;
  for (const auto &block : extracted.prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::preformatted)
      regions.push_back(&block);
  require(regions.size() == 2 &&
              regions[0]->preformatted_lines.size() == 1 &&
              contains(regions[0]->preformatted_lines.front(),
                       "DEVICE=C:\\SECOND.SYS") &&
              regions[1]->preformatted_lines.size() == 1 &&
              contains(regions[1]->preformatted_lines.front(),
                       "LOAD SECOND.NLM"),
          "outer regions did not retain both nested display rows exactly once");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "DEVICE=C:\\SECOND.SYS") &&
              contains(markdown, "LOAD SECOND.NLM") &&
              contains(markdown, "Continue afterwards"),
          "nested verbatim delimiters lost or reordered rendered content: " +
              markdown);
}

// dfhqaa84 2.69 record 760 nests `cz OFF XMP` .. `cz OFF EXMP 9 9` inside
// `cz OFF LINES` .. `cz OFF ELINES 7 7`, with `cz BREAK 12` row breaks
// between the inner pair.  The breaks are breaks of the region, not
// directives that leave the inner pair unclosed.
void row_breaks_inside_a_nested_verbatim_pair_stay_in_the_region() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("OFF"), words("LINES")});
  builder.line({words("cz"), words("OFF"), words("XMP")});
  builder.line({words("            "), words("EXEC"), words("CICS"),
                words("SYNCPOINT")});
  builder.line({words("cz"), words("BREAK"), words("12")});
  builder.line({words("            "), words("EXEC"), words("CICS"),
                words("INQUIRE"), words("UOW")});
  builder.line({words("cz"), words("OFF"), words("EXMP"), words("9"),
                words("9")});
  builder.line({words("cz"), words("OFF"), words("ELINES"), words("7"),
                words("7")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Continue"), words("afterwards.")});

  const auto extracted = extract_record(std::move(builder), 31);
  require(extracted.prose.has_value(),
          "a row break inside a nested verbatim pair left the outer region "
          "unclosed: " + extracted.error);
  if (!extracted.prose) return;
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  const auto first = markdown.find("EXEC CICS SYNCPOINT");
  const auto second = markdown.find("EXEC CICS INQUIRE UOW");
  require(first != std::string::npos && second != std::string::npos &&
              first < second && contains(markdown, "Continue afterwards"),
          "the nested region's rows were lost or reordered: " + markdown);
}

const geist::detail::ProseBlockIR *find_entry(
    const geist::detail::ProseTopicIR &prose, const std::string &term) {
  for (const auto &block : prose.blocks) {
    if (block.kind != geist::detail::ProseBlockKindIR::definition_entry)
      continue;
    std::string text;
    for (std::size_t item = 0; item < block.term_inline_count; ++item)
      text += block.inlines[item].text;
    if (contains(text, term)) return &block;
  }
  return nullptr;
}

std::string block_text(const geist::detail::ProseBlockIR &block) {
  std::string text;
  for (const auto &item : block.inlines) text += item.text;
  return text;
}

// GA19-5486-13 3.5.1 closes its table notes with `cz FLOW DT 3 5` and a row
// that starts at column 5: the term columns 3..4 are blank and the whole row
// is the definition.  SC21-8295-03 2.2 does the same at `cz FLOW DT 3 13`.
// The empty term is the DT control; the rows are its body.
void an_empty_term_row_at_the_indent_is_the_definition_body() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("5")});
  builder.line({words("   "), words("1"), words("Based"), words("on"),
                words("tests.")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("5")});
  builder.line({words("     "), words("General"), words("Note:"),
                words("Capacities"), words("vary.")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 31);
  require(extracted.prose.has_value(),
          "an empty DT term at the indent column was declined: " +
              extracted.error);
  if (!extracted.prose) return;
  std::vector<const geist::detail::ProseBlockIR *> entries;
  for (const auto &block : extracted.prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::definition_entry)
      entries.push_back(&block);
  require(entries.size() == 2, "the empty term is not its own entry");
  if (entries.size() != 2) return;
  require(entries[0]->term_inline_count != 0 &&
              entries[0]->body_blocks.empty(),
          "the ordinary DT lost its term");
  const auto &empty = *entries[1];
  require(empty.inlines.empty() && empty.term_inline_count == 0 &&
              empty.body_blocks.size() == 1 && empty.slices.size() == 1,
          "the empty term does not own exactly its body paragraph");
  if (empty.body_blocks.size() != 1) return;
  const auto &body = extracted.prose->blocks[empty.body_blocks.front()];
  require(body.kind == geist::detail::ProseBlockKindIR::paragraph &&
              contains(block_text(body), "General Note: Capacities vary."),
          "the empty term's row is not its body paragraph: " +
              block_text(body));
  const auto document = lower_document(extracted);
  if (!document) return;
  bool found = false;
  for (const auto &candidate : document->blocks) {
    const auto *definitions =
        std::get_if<geist::detail::DefinitionListBlockIR>(&candidate.node);
    if (definitions == nullptr || definitions->entries.size() != 2) continue;
    const auto &lowered = definitions->entries[1];
    found = lowered.empty_term && lowered.term.empty() &&
            lowered.definition_blocks.size() == 1 &&
            std::holds_alternative<geist::detail::ParagraphBlockIR>(
                lowered.definition_blocks.front().node);
  }
  require(found, "the empty term did not lower as one two-entry list");
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "General Note: Capacities vary.") &&
              contains(markdown, "Based on tests."),
          "the empty-term definition did not survive rendering: " + markdown);
}

// An empty term before a separate DD has no body of its own to prove.
void an_empty_term_before_a_dd_still_declines() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("5")});
  builder.line({words("     "), words("Orphan"), words("row.")});
  builder.line({words("cz"), words("FLOW"), words("DD"), words("5"),
                words("5")});
  builder.line({words("     "), words("Body.")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});
  const auto extracted = extract_record(std::move(builder), 32);
  require(!extracted.prose.has_value() &&
              contains(extracted.error, "empty term before cz FLOW DD"),
          "an empty term before DD was admitted: " + extracted.error);
}

// DFHP7A00 BACK_1.1.2.1 stores each publication group as `cz FLOW DT 3 13`
// with only the term row (`   General`) and then opens `cz FLOW SL 13 13` at
// the term's indent column; 2.6.1.1.2 opens `cz FLOW DL 13 13` after
// `   Return codes`.  The list is the term's definition body.
void a_term_only_dt_owns_the_list_opened_at_its_indent() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("   "), words("General")});
  builder.line({words("cz"), words("FLOW"), words("SL"), words("13"),
                words("13")});
  builder.line({words("cz"), words("FLOW"), words("LI"), words("17"),
                words("17")});
  builder.line({words("                 "), words("Release"),
                words("Guide")});
  builder.line({words("cz"), words("OFF"), words("ESL"), words("10"),
                words("10")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("   "), words("Return"), words("codes")});
  builder.line({words("cz"), words("FLOW"), words("DL"), words("13"),
                words("13")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("13"),
                words("25")});
  builder.line({words("             "), words("UERCNORM"),
                words("    "), words("Continue")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("10"),
                words("10")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 33);
  require(extracted.prose.has_value(),
          "a term-only DT before its nested list was declined: " +
              extracted.error);
  if (!extracted.prose) return;
  const auto *general = find_entry(*extracted.prose, "General");
  const auto *codes = find_entry(*extracted.prose, "Return codes");
  require(general != nullptr && codes != nullptr,
          "the term-only entries were not built");
  if (general == nullptr || codes == nullptr) return;
  require(general->term_inline_count == general->inlines.size() &&
              general->body_blocks.size() == 1 &&
              extracted.prose->blocks[general->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::list_item,
          "the SL item is not the body of its term");
  require(codes->term_inline_count == codes->inlines.size() &&
              codes->body_blocks.size() == 1 &&
              extracted.prose->blocks[codes->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::definition_entry,
          "the nested DL entry is not the body of its term");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "Release Guide") &&
              contains(markdown, "UERCNORM") &&
              contains(markdown, "Return codes"),
          "the term-owned lists did not survive rendering: " + markdown);
}

// A list that does not open at the term's indent column is not its body:
// the term is a term alone and the list follows it.
void a_list_at_another_column_leaves_the_term_without_a_body() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("   "), words("General")});
  builder.line({words("cz"), words("FLOW"), words("SL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("LI"), words("7"),
                words("7")});
  builder.line({words("       "), words("Release"), words("Guide")});
  builder.line({words("cz"), words("OFF"), words("ESL"), words("0"),
                words("0")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});
  const auto extracted = extract_record(std::move(builder), 34);
  // The term stays a term alone (BookServer pairs no term with a body: it
  // prints `<dt>` and the term, then the list), and the list follows it.
  require(extracted.prose.has_value(),
          "a term followed by a list at another column was rejected: " +
              extracted.error);
  if (!extracted.prose) return;
  const auto term = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind ==
               geist::detail::ProseBlockKindIR::definition_entry;
      });
  require(term != extracted.prose->blocks.end() &&
              term->term_inline_count == term->inlines.size() &&
              term->body_blocks.empty(),
          "a list at another column became a term body");
}

// SC21-8295-03 3.1.3.2 writes a DT with no display rows and then the block
// that defines it: `cz FLOW P 13 13` at the term's indent (record 371), and
// `cz OFF SCREEN` (record 431).  Hosted serves an empty `<dt>` and that block
// as the `<dd>`.  A paragraph at another column is not the definition.
void an_empty_dt_owns_the_paragraph_or_screen_that_follows() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("13"),
                words("13")});
  builder.line({words("             "), words("Prompt"), words("1:"),
                words("Word"), words("processing.")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("cz"), words("OFF"), words("SCREEN")});
  builder.line({words("             "), words("|"), words("22.1"),
                words("CNFIGSSP"), words("|")});
  builder.line({words("cz"), words("OFF"), words("ESCREEN"), words("10"),
                words("10")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 36);
  require(extracted.prose.has_value(),
          "an empty DT before its P or SCREEN body was declined: " +
              extracted.error);
  if (!extracted.prose) return;
  std::vector<const geist::detail::ProseBlockIR *> entries;
  for (const auto &block : extracted.prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::definition_entry)
      entries.push_back(&block);
  require(entries.size() == 2, "the empty DTs are not two entries");
  if (entries.size() != 2) return;
  require(entries[0]->inlines.empty() && entries[0]->body_blocks.size() == 1 &&
              extracted.prose->blocks[entries[0]->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::paragraph &&
              contains(block_text(extracted.prose->blocks
                                      [entries[0]->body_blocks.front()]),
                       "Prompt 1: Word processing."),
          "the empty term does not own its P paragraph");
  require(entries[1]->inlines.empty() && entries[1]->body_blocks.size() == 1 &&
              extracted.prose->blocks[entries[1]->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::preformatted,
          "the empty term does not own its SCREEN region");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "Prompt 1: Word processing.") &&
              contains(markdown, "CNFIGSSP"),
          "the empty-term block bodies did not survive rendering: " +
              markdown);

  RecordBuilder other;
  metadata(other);
  other.line({words("cz"), words("FLOW"), words("DL"), words("3"),
              words("3")});
  other.line({words("cz"), words("FLOW"), words("DT"), words("3"),
              words("13")});
  other.line({words("cz"), words("FLOW"), words("P"), words("3"),
              words("3")});
  other.line({words("   "), words("Not"), words("a"), words("definition.")});
  other.line({words("cz"), words("OFF"), words("EDL"), words("0"),
              words("0")});
  const auto declined = extract_record(std::move(other), 37);
  require(!declined.prose.has_value() &&
              contains(declined.error, "cz FLOW DT has no text"),
          "a paragraph at the list margin became an empty term's body: " +
              declined.error);
}

// DFHP7A00 2.6.1.1.2 stores `   Exit-specific parameters` at
// `cz FLOW DT 3 13` and opens `cz FLOW DL 13 13` next.  The term ends with
// the word that straddles column 13 (hosted SC41-485 1.2.2 serves
// `<dt>Generic object<dd><B>name</B>` for the same shape), so `parameters`
// begins the definition, and the list opened at the term's indent continues
// it.  A list opened at another column is not part of the entry.
void a_list_at_the_indent_continues_an_inline_definition() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("   "), words("Exit-specific"), words("parameters")});
  builder.line({words("cz"), words("FLOW"), words("DL"), words("13"),
                words("13")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("13"),
                words("25")});
  builder.line({words("             "), words("UEPSZACT"), words("    "),
                words("Command")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("10"),
                words("10")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("   "), words("Codes"), words("     "),
                words("Values")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});
  builder.line({words("cz"), words("FLOW"), words("UL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("LI"), words("3"),
                words("5")});
  builder.line({words("   "), words("o"), words(" "), words("Outside")});
  builder.line({words("cz"), words("OFF"), words("EUL"), words("0"),
                words("0")});
  const auto extracted = extract_record(std::move(builder), 35);
  require(extracted.prose.has_value(),
          "a list continuing an inline definition was declined: " +
              extracted.error);
  if (!extracted.prose) return;
  const auto *exit = find_entry(*extracted.prose, "Exit-specific");
  require(exit != nullptr, "the overflowing term was not built");
  if (exit == nullptr) return;
  std::string term;
  for (std::size_t item = 0; item < exit->term_inline_count; ++item)
    term += exit->inlines[item].text;
  require(!contains(term, "parameters") &&
              exit->term_inline_count < exit->inlines.size(),
          "the word after the straddling word left the definition: " + term);
  require(exit->body_blocks.size() == 1 &&
              extracted.prose->blocks[exit->body_blocks.front()].kind ==
                  geist::detail::ProseBlockKindIR::definition_entry,
          "the list at the indent is not part of the definition");
  const auto *codes = find_entry(*extracted.prose, "Codes");
  require(codes != nullptr && codes->body_blocks.empty(),
          "a DT followed by its list closer gained a body");
  for (const auto &block : extracted.prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::list_item)
      require(block.parent_block == static_cast<std::size_t>(-1),
              "a list after the definition list joined an entry");
  const auto document = lower_document(extracted);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "UEPSZACT") && contains(markdown, "Outside"),
          "the continued definition did not survive rendering: " + markdown);
}

// `cz FLOW LP` inside a DL is a list part: a paragraph at the list's own
// margin that belongs to no entry, between runs of entries of one list.
// Hosted BookServer serves it as `<br>` and the text inside the one `<dl>`,
// and the `<dt>` entries after it continue that list:
//  * SC33-1692-02 2.3.4.1 (DT 19990303174038) stores `cz FLOW LP 3 3`, then
//    `cz FLOW P 3 3` carrying `   Definition:` -- the DFHP7A00 2.3.4.1
//    shape -- and serves `<dl>\n<br>   Definition:\n<dt>   <B>EXEC</B> ...`;
//  * SC33-1692-01 2.3.4.1 (DT 19970821113553) and SC33-1119-00 4.5.1.1
//    (DT 19960129110036) carry the row on the LP itself, the latter after a
//    DT/DD entry: `<br>   The remaining options specify the attributes of
//    the 4-tuple.` and then three more `<dt>` entries in the same `<dl>`.
// HTML has no paragraph inside a `<dl>`, so the part lowers to a paragraph
// between two runs of the list, exactly like the opener's lead-in rows.
void a_list_part_paragraph_divides_its_definition_list() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({words("cz"), words("FLOW"), words("DL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("LP"), words("3"),
                words("3")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Definition:")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("20")});
  builder.line({words("   "), words("INSTALL"), words("          "),
                words("Define"), words("resources")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("13")});
  builder.line({words("   "), words("Protocol")});
  builder.line({words("cz"), words("FLOW"), words("DD"), words("13"),
                words("13")});
  builder.line({words("             "), words("Specifies"), words("the"),
                words("protocol.")});
  builder.line({words("cz"), words("FLOW"), words("LP"), words("3"),
                words("3")});
  builder.line({words("   "), words("Operations:")});
  builder.line({words("cz"), words("FLOW"), words("DT"), words("3"),
                words("20")});
  builder.line({words("   "), words("SET"), words("              "),
                words("Control"), words("resources.")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 29);
  require(extracted.prose.has_value(),
          "a FLOW LP list part was declined: " + extracted.error);
  if (!extracted.prose) return;
  using Kind = geist::detail::ProseBlockKindIR;
  const auto &blocks = extracted.prose->blocks;
  const auto none = static_cast<std::size_t>(-1);
  std::vector<std::size_t> body;
  for (std::size_t index = 0; index < blocks.size(); ++index)
    if (blocks[index].parent_block == none &&
        blocks[index].kind != Kind::heading)
      body.push_back(index);
  const std::vector<Kind> expected = {Kind::paragraph, Kind::definition_entry,
                                      Kind::definition_entry, Kind::paragraph,
                                      Kind::definition_entry};
  require(body.size() == expected.size(),
          "the list parts and entries are not five top-level blocks");
  if (body.size() != expected.size()) return;
  for (std::size_t index = 0; index < body.size(); ++index)
    require(blocks[body[index]].kind == expected[index],
            "top-level block " + std::to_string(index) +
                " has the wrong kind");
  require(contains(block_text(blocks[body[0]]), "Definition:") &&
              contains(block_text(blocks[body[3]]), "Operations:"),
          "the list-part paragraphs lost their text");
  const auto ordinal = blocks[body[1]].list_ordinal;
  require(blocks[body[2]].list_ordinal == ordinal &&
              blocks[body[4]].list_ordinal == ordinal,
          "the entries after a list part left their definition list");
  const auto *protocol = find_entry(*extracted.prose, "Protocol");
  require(protocol != nullptr && protocol->body_blocks.empty(),
          "the list part joined the DD body before it");
  const auto document = lower_document(extracted);
  if (!document) return;
  std::vector<std::size_t> shape;
  for (const auto &candidate : document->blocks) {
    if (std::holds_alternative<geist::detail::ParagraphBlockIR>(
            candidate.node))
      shape.push_back(0);
    else if (const auto *list =
                 std::get_if<geist::detail::DefinitionListBlockIR>(
                     &candidate.node))
      shape.push_back(list->entries.size());
  }
  require(shape == std::vector<std::size_t>{0, 2, 0, 1},
          "the list did not lower as part, two entries, part, one entry");
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "Definition:") &&
              contains(markdown, "Operations:") &&
              contains(markdown, "Specifies the protocol") &&
              contains(markdown, "Control resources"),
          "the divided definition list did not survive rendering: " +
              markdown);
}

// Every LP shape without that evidence keeps declining.
void list_part_declines(const std::vector<std::vector<std::string>> &rows,
                        const std::string &expected,
                        const std::string &what) {
  RecordBuilder builder;
  metadata(builder);
  for (const auto &row : rows) {
    std::vector<TokenWords> line;
    for (const auto &word : row) line.push_back(words(word));
    builder.line(std::move(line));
  }
  const auto extracted = extract_record(std::move(builder), 30);
  require(!extracted.prose.has_value() && contains(extracted.error, expected),
          what + " was admitted or declined for the wrong reason: " +
              extracted.error);
}

void unproven_list_parts_still_decline() {
  const std::vector<std::string> edl = {"cz", "OFF", "EDL", "0", "0"};
  const std::vector<std::string> entry_dt = {"cz", "FLOW", "DT", "3", "20"};
  const std::vector<std::string> entry_row = {"   ", "SET",
                                              "              ", "Control"};
  // A list part before the first item of an item list is the list's
  // lead-in: SC33-6717-00 `3.1.2.1` serves `<ul><br>       User action:`
  // ahead of the first `<li>`.  It is admitted now.
  {
    RecordBuilder builder;
    metadata(builder);
    const std::vector<std::vector<std::string>> rows = {
        {"cz", "FLOW", "UL", "3", "3"},
        {"cz", "FLOW", "LP", "3", "3"},
        {"   ", "Operations:"},
        {"cz", "OFF", "EUL", "0", "0"}};
    for (const auto &row : rows) {
      std::vector<TokenWords> line;
      for (const auto &word : row) line.push_back(words(word));
      builder.line(std::move(line));
    }
    const auto extracted = extract_record(std::move(builder), 30);
    require(extracted.prose.has_value(),
            "an LP before the first item of a UL was rejected: " +
                extracted.error);
  }
  // No text of its own, and no paragraph at its margin to supply one.
  list_part_declines({{"cz", "FLOW", "DL", "3", "3"},
                      {"cz", "FLOW", "LP", "3", "3"},
                      entry_dt,
                      entry_row,
                      edl},
                     "cz FLOW LP has no list-part text",
                     "an LP with no rows before a DT");
  list_part_declines({{"cz", "FLOW", "DL", "3", "3"},
                      {"cz", "FLOW", "LP", "3", "3"},
                      {"cz", "FLOW", "P", "20", "20"},
                      {"                    ", "Operations:"},
                      entry_dt,
                      entry_row,
                      edl},
                     "cz FLOW LP has no list-part text",
                     "an LP before a paragraph at another column");
  // A part away from the list's own margin.
  list_part_declines({{"cz", "FLOW", "DL", "3", "3"},
                      {"cz", "FLOW", "LP", "20", "20"},
                      {"                    ", "Operations:"},
                      entry_dt,
                      entry_row,
                      edl},
                     "cz FLOW LP is not at its definition list's margin",
                     "an LP off the list margin");
  // A term still waiting for its definition.
  list_part_declines({{"cz", "FLOW", "DL", "3", "3"},
                      {"cz", "FLOW", "DT", "3", "13"},
                      {"   ", "Protocol"},
                      {"cz", "FLOW", "LP", "3", "3"},
                      {"   ", "Operations:"},
                      entry_dt,
                      entry_row,
                      edl},
                     "cz FLOW LP follows a term with no definition",
                     "an LP after a term-only DT");
}

// The closed region: emitted verbatim, marked degraded, everything around it
// typed.
void a_closed_generic_region_is_a_typed_verbatim_block() {
  auto extracted = extract("WIDGET", "EWIDGET");
  require(extracted.prose.has_value(),
          "a closed generic cz OFF region was declined: " + extracted.error);
  if (!extracted.prose) return;

  std::string error;
  require(geist::detail::verify_prose_topic_ir(
              extracted.sources, extracted.layout, *extracted.ownership,
              topic_title, nullptr, *extracted.prose, &error),
          "the topic failed the prose verifier: " + error);
  for (const auto &entry : extracted.prose->ledger)
    require(entry.role != geist::detail::ProseTokenRoleIR::unassigned,
            "the topic left a token unassigned");

  // The region is the reader's generic `CZ OFF` block: a verbatim block of
  // display rows, not a degraded fallback.  The closer prints nothing and
  // leaves the region open, so the row it owns is a row of the block too.
  const auto region = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) {
        return block.kind == geist::detail::ProseBlockKindIR::preformatted;
      });
  require(region != extracted.prose->blocks.end(),
          "the generic region is not a verbatim block");
  if (region == extracted.prose->blocks.end()) return;
  require(region->degradation_code.empty(),
          "the generic region is marked degraded: " + region->degradation_code);
  require(region->preformatted_lines.size() == 3 &&
              contains(region->preformatted_lines[0], "alpha beta") &&
              contains(region->preformatted_lines[1], "gamma delta") &&
              contains(region->preformatted_lines[2], "After the region."),
          "the block is not the region's rows and its closer's row");

  // Traceability: every row of the block names its own native display row.
  require(region->preformatted_line_sources.size() ==
              region->preformatted_lines.size(),
          "the region rows carry no per-row provenance");
  for (const auto &source : region->preformatted_line_sources)
    require(source.token_end > source.token_begin,
            "a region row names no source slice");

  std::string typed_text;
  for (const auto &block : extracted.prose->blocks)
    for (const auto &fragment : block.inlines) typed_text += fragment.text + " ";
  require(contains(typed_text, "Read the widget note."),
          "the typed prose before the region was lost; it carries '" +
              typed_text + "'");

  // Down through the lowering: no degraded node, a typed topic.
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "lowering the topic failed: " + error);
  if (!document) return;
  require(geist::detail::verify_document_ir(*document, &error),
          "the document did not verify: " + error);
  require(std::none_of(document->blocks.begin(), document->blocks.end(),
                       [](const auto &block) {
                         return block.origin.fidelity ==
                                geist::detail::DocumentFidelityIR::degraded;
                       }),
          "the document carries a degraded block");

  geist::detail::TypedLoweringTraceIR trace;
  trace.family = "prose";
  const auto diagnostic = geist::detail::classify_typed_lowering(
      identity(), &*document, {}, trace);
  require(diagnostic.severity == geist::RenderSeverity::typed,
          "the topic is not typed, it is " +
              std::string(geist::to_string(diagnostic.severity)));

  const auto markdown = geist::detail::render_document_markdown(*document);
  require(!contains(markdown, "degraded="),
          "the rendered file marks a degraded fence: " + markdown);
  require(contains(markdown, "alpha beta") && contains(markdown, "gamma delta"),
          "the rendered file lost the region's rows");
  require(contains(markdown, "Read the widget note"),
          "the rendered file lost the typed prose before the region");
}

// A region that neither its own closer nor a `CZ FLOW` ends: nothing proves
// where its rows stop, and the topic must still fall whole.
void an_unclosed_unmodelled_region_declines_the_topic() {
  const auto extracted = extract("WIDGET", "EGIZMO");
  require(!extracted.prose.has_value(),
          "a cz OFF region the source never closes was admitted; invariant 2 "
          "requires the topic to decline whole");
  require(contains(extracted.error, "is not modelled") ||
              contains(extracted.error, "carries display text"),
          "the decline no longer names the unmodelled construct: " +
              extracted.error);
}

// `cz OFF ADDRESS` .. `cz OFF EADDRESS` is a modelled verbatim region, not
// a degraded one.  GA19-5486-13 `EDITION` (record 5 segments 13-14),
// SC21-8295-03 `EDITION`/`FRONT_1` and DFHP7A00 `BACK_4` wrap their mailing
// addresses in it, and hosted BookServer (`GA19-548` DT 19951012161206,
// `SC21-829` DT 19970409135933) serves every one as `<pre width="80">`, the
// rows character for character, exactly as it serves `XMP` and `LINES`.
void an_address_region_is_a_typed_verbatim_block() {
  auto extracted = extract("ADDRESS", "EADDRESS");
  require(extracted.prose.has_value(),
          "a closed cz OFF ADDRESS region was declined: " + extracted.error);
  if (!extracted.prose) return;
  std::string error;
  require(geist::detail::verify_prose_topic_ir(
              extracted.sources, extracted.layout, *extracted.ownership,
              topic_title, nullptr, *extracted.prose, &error),
          "the address topic failed the prose verifier: " + error);
  std::size_t verbatim = 0;
  for (const auto &block : extracted.prose->blocks) {
    require(block.degradation_code.empty(),
            "the address region is still degraded: " + block.degradation_code);
    if (block.kind != geist::detail::ProseBlockKindIR::preformatted) continue;
    ++verbatim;
    // The closer `cz OFF EADDRESS 0 0` prints nothing and leaves the
    // `<pre width="80">` open, so the row it carries is the block's third
    // (binary rule 6; hosted keeps closer rows in the region's `<pre>`).
    require(block.preformatted_lines.size() == 3 &&
                contains(block.preformatted_lines[0], "alpha beta") &&
                contains(block.preformatted_lines[1], "gamma delta") &&
                contains(block.preformatted_lines[2], "After the region."),
            "the address block is not the region's rows and its closer's");
  }
  require(verbatim == 1, "the address region did not become one verbatim "
                         "block");
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "lowering the address topic failed: " + error);
  if (!document) return;
  geist::detail::TypedLoweringTraceIR trace;
  trace.family = "prose";
  const auto diagnostic = geist::detail::classify_typed_lowering(
      identity(), &*document, {}, trace);
  require(diagnostic.severity == geist::RenderSeverity::typed,
          "the address topic is not typed, it is " +
              std::string(geist::to_string(diagnostic.severity)));
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "After the region") &&
              !contains(markdown, "degraded"),
          "the address topic did not render as typed prose: " + markdown);
}

// An address region the source never closes still declines whole.
void an_unclosed_address_region_declines_the_topic() {
  const auto extracted = extract("ADDRESS", "EGIZMO");
  require(!extracted.prose.has_value(),
          "a cz OFF ADDRESS region the source never closes was admitted");
}

} // namespace

int main() {
  an_h6_directive_builds_a_level_six_heading();
  a_separate_dd_completes_its_definition_term();
  syntax_followed_by_pt_builds_an_implicit_parameter_list();
  a_term_after_a_paragraph_opens_the_list_its_closer_ends();
  a_term_without_a_list_closer_still_declines();
  a_nested_list_in_a_parameter_body_keeps_source_ownership();
  a_parameter_paragraph_continuation_keeps_source_ownership();
  a_parameter_body_with_a_subword_style_lowers();
  a_rowless_term_before_a_break_hands_the_body_on();
  a_list_item_continuation_keeps_recursive_source_ownership();
  an_empty_dt_before_lines_keeps_a_compound_definition();
  an_empty_dd_before_lines_keeps_a_compound_definition();
  an_empty_term_row_at_the_indent_is_the_definition_body();
  an_empty_term_before_a_dd_still_declines();
  a_term_only_dt_owns_the_list_opened_at_its_indent();
  a_list_at_another_column_leaves_the_term_without_a_body();
  a_list_at_the_indent_continues_an_inline_definition();
  an_empty_dt_owns_the_paragraph_or_screen_that_follows();
  balanced_nested_verbatim_delimiters_stay_inside_the_outer_region();
  row_breaks_inside_a_nested_verbatim_pair_stay_in_the_region();
  a_list_part_paragraph_divides_its_definition_list();
  unproven_list_parts_still_decline();
  a_closed_generic_region_is_a_typed_verbatim_block();
  an_unclosed_unmodelled_region_declines_the_topic();
  an_address_region_is_a_typed_verbatim_block();
  an_unclosed_address_region_declines_the_topic();
  geist_test::exit_with_failures();
  std::cout << "cz_degraded_region_synthetic: ok\n";
  return 0;
}
