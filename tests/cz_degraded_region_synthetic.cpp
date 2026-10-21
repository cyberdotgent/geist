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
// So the region goes out verbatim, marked degraded, and the prose around it
// stays typed. A region the source never closes with its own `E<tag>` fails
// condition 2 and the topic still declines whole.
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

// FLOW LP is a list-part boundary, not a list item.  DFHP7A00 2.3.4.1 uses
// it twice inside one DL, before the `Definition:` and `Operations:` section
// paragraphs that divide two runs of DT entries.
void a_list_part_paragraph_fails_as_mixed_list_content() {
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
  builder.line({words("   "), words("INSTALL"), words("Define"),
                words("resources")});
  builder.line({words("cz"), words("OFF"), words("EDL"), words("0"),
                words("0")});

  const auto extracted = extract_record(std::move(builder), 29);
  require(!extracted.prose.has_value(),
          "a FLOW LP section was flattened outside its definition list");
  require(contains(extracted.error,
                   "needs mixed list block content"),
          "the FLOW LP decline is not structural: " + extracted.error);
}

// The closed region: emitted verbatim, marked degraded, everything around it
// typed.
void a_closed_unmodelled_region_degrades_its_block() {
  auto extracted = extract("WIDGET", "EWIDGET");
  require(extracted.prose.has_value(),
          "a closed unmodelled cz OFF region was declined: " +
              extracted.error);
  if (!extracted.prose) return;

  std::string error;
  require(geist::detail::verify_prose_topic_ir(
              extracted.sources, extracted.layout, *extracted.ownership,
              topic_title, nullptr, *extracted.prose, &error),
          "the degraded topic failed the prose verifier: " + error);
  for (const auto &entry : extracted.prose->ledger)
    require(entry.role != geist::detail::ProseTokenRoleIR::unassigned,
            "the degraded topic left a token unassigned");

  // Invariant 6: the block, not the topic, names the construct and the check
  // that failed.
  const auto degraded = std::find_if(
      extracted.prose->blocks.begin(), extracted.prose->blocks.end(),
      [](const auto &block) { return !block.degradation_code.empty(); });
  require(degraded != extracted.prose->blocks.end(),
          "no block of the topic was marked degraded");
  if (degraded == extracted.prose->blocks.end()) return;
  require(degraded->kind == geist::detail::ProseBlockKindIR::preformatted,
          "the degraded block is not the verbatim region");
  require(degraded->degradation_code == "cz-off-region-unmodelled",
          "degradation code is '" + degraded->degradation_code + "'");
  require(contains(degraded->degradation_detail, "cz OFF WIDGET") &&
              contains(degraded->degradation_detail, "is not modelled") &&
              contains(degraded->degradation_detail, "cz OFF EWIDGET"),
          "the detail names neither the construct nor its proven closer: " +
              degraded->degradation_detail);

  // Invariant 5: display rows in source order, and nothing else.
  require(degraded->preformatted_lines.size() == 2 &&
              contains(degraded->preformatted_lines[0], "alpha beta") &&
              contains(degraded->preformatted_lines[1], "gamma delta"),
          "the degraded block is not the region's two display rows");

  // Traceability: every row of a degraded block names its own source, so
  // `bootrace --explain-offset` resolves a byte of the fence to that row's
  // records, tokens and BOO byte extents and not merely to the region.
  require(degraded->preformatted_line_inlines.size() ==
              degraded->preformatted_lines.size(),
          "the degraded rows carry no per-row provenance");
  for (std::size_t row = 0; row < degraded->preformatted_lines.size(); ++row) {
    const auto inline_index = degraded->preformatted_line_inlines[row];
    if (degraded->preformatted_lines[row].empty()) continue;
    require(inline_index < degraded->inlines.size() &&
                !degraded->inlines[inline_index].slices.empty(),
            "a degraded row names no source slice");
  }

  // The prose on both sides of the region stays typed, which is the whole
  // point: the topic is not discarded over one unproven block.
  std::string typed_text;
  for (const auto &block : extracted.prose->blocks) {
    if (!block.degradation_code.empty()) continue;
    for (const auto &fragment : block.inlines) typed_text += fragment.text + " ";
  }
  require(contains(typed_text, "Read the widget note.") &&
              contains(typed_text, "After the region."),
          "the typed prose around the region was lost; it carries '" +
              typed_text + "'");

  // Down through the lowering: one degraded node, the document still
  // verifies with no relaxation, and the rendered file marks the fence.
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity(), *extracted.prose, &error);
  require(document.has_value(), "lowering the degraded topic failed: " + error);
  if (!document) return;
  require(geist::detail::verify_document_ir(*document, &error),
          "a document carrying a degraded block did not verify: " + error);
  const auto degraded_nodes = static_cast<std::size_t>(std::count_if(
      document->blocks.begin(), document->blocks.end(), [](const auto &block) {
        return block.origin.fidelity ==
               geist::detail::DocumentFidelityIR::degraded;
      }));
  require(degraded_nodes == 1,
          "the document carries " + std::to_string(degraded_nodes) +
              " degraded blocks, not one");

  geist::detail::TypedLoweringTraceIR trace;
  trace.family = "prose";
  const auto diagnostic = geist::detail::classify_typed_lowering(
      identity(), &*document, {}, trace);
  require(diagnostic.severity == geist::RenderSeverity::typed_degraded,
          "the topic is not typed-degraded, it is " +
              std::string(geist::to_string(diagnostic.severity)));

  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown,
                   "<!-- geist-block: degraded=cz-off-region-unmodelled -->\n"
                   "```"),
          "the rendered file does not mark the degraded fence: " + markdown);
  require(contains(markdown, "alpha beta") && contains(markdown, "gamma delta"),
          "the rendered file lost the region's rows");
  require(contains(markdown, "Read the widget note"),
          "the rendered file lost the typed prose before the region");
}

// The open region: the check that failed *is* the closure check, so the
// boundary is not proven and the topic must still fall whole.
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

} // namespace

int main() {
  an_h6_directive_builds_a_level_six_heading();
  a_separate_dd_completes_its_definition_term();
  syntax_followed_by_pt_builds_an_implicit_parameter_list();
  a_nested_list_in_a_parameter_body_keeps_source_ownership();
  a_parameter_paragraph_continuation_keeps_source_ownership();
  a_list_item_continuation_keeps_recursive_source_ownership();
  an_empty_dt_before_lines_keeps_a_compound_definition();
  an_empty_dd_before_lines_keeps_a_compound_definition();
  a_list_part_paragraph_fails_as_mixed_list_content();
  a_closed_unmodelled_region_degrades_its_block();
  an_unclosed_unmodelled_region_declines_the_topic();
  geist_test::exit_with_failures();
  std::cout << "cz_degraded_region_synthetic: ok\n";
  return 0;
}
