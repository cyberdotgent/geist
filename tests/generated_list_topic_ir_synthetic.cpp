// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/generated_list_document_lowering.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "test_failures.hpp"
#include "geist/detail/ir/generated_list_topic_ir.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
    return;
  }
}

TokenWords words(const std::string& text) {
  return TokenWords(text.begin(), text.end());
}

std::string label_text(const geist::detail::GeneratedListEntryIR& entry) {
  TokenWords label;
  for (const auto& fragment : entry.label_fragments)
    for (const auto& cell : fragment.cells) label.push_back(cell.word);
  return geist::detail::token_words_to_ascii(label);
}

void require_entry_partition(
    const geist::detail::GeneratedListEntryIR& entry) {
  require(entry.cell_dispositions.size() == entry.display.cells.size() &&
              entry.suppressed_prefix_dispositions.size() ==
                  entry.display.suppressed_prefix_cells.size(),
          "generated-list entry disposition coverage is incomplete");
  std::vector<std::size_t> fragment_coverage(entry.display.cells.size());
  for (const auto& fragment : entry.label_fragments) {
    require(fragment.cell_begin < fragment.cell_end &&
                fragment.cell_end <= entry.display.cells.size() &&
                fragment.cells.size() ==
                    fragment.cell_end - fragment.cell_begin &&
                !fragment.source_slices.empty(),
            "generated-list label fragment range or provenance is invalid");
    for (std::size_t index = fragment.cell_begin;
         index < fragment.cell_end; ++index) {
      ++fragment_coverage[index];
      require(fragment.cells[index - fragment.cell_begin].word ==
                  entry.display.cells[index].word,
              "generated-list fragment differs from display evidence");
    }
  }
  for (std::size_t index = 0; index < fragment_coverage.size(); ++index)
    require(fragment_coverage[index] ==
                (entry.cell_dispositions[index] ==
                         geist::detail::GeneratedListCellDispositionIR::
                             label_fragment
                     ? 1U
                     : 0U),
            "generated-list cell does not have exactly one disposition");
}

DecodedLogicalRecordSource make_source(std::uint32_t logical_record,
                                       std::vector<TokenWords> tokens) {
  DecodedLogicalRecordSource source;
  source.logical_record = logical_record;
  source.tokens = std::move(tokens);
  source.encoded_tokens.resize(source.tokens.size());
  source.ir.logical_record = logical_record;
  auto byte = static_cast<std::uint32_t>(logical_record * 100);
  for (std::size_t index = 0; index < source.tokens.size(); ++index) {
    source.encoded_tokens[index] = {static_cast<std::uint16_t>(0x20 + index), 1};
    source.ir.tokens.push_back(
        {index, source.encoded_tokens[index], source.tokens[index],
         {byte, static_cast<std::uint32_t>(byte + 1)}, false, 3});
    ++byte;
  }
  source.ir.payload_range =
      {static_cast<std::uint32_t>(logical_record * 100), byte};
  geist::detail::assign_display_line_framing(source.ir);
  source.assembled =
      geist::detail::assemble_logical_record_with_sources(source.tokens);
  source.control_segments = geist::detail::decode_control_segments(
      source.logical_record, source.assembled);
  return source;
}

struct Pipeline {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::SelectorCatalogIR selectors;
  geist::detail::LayoutIR layout;
  std::optional<geist::detail::VerifiedOwnershipIR> ownership;
};

Pipeline pipeline(std::vector<DecodedLogicalRecordSource> sources) {
  Pipeline result;
  result.sources = std::move(sources);
  std::string error;
  const auto selectors =
      geist::detail::extract_selector_catalog_ir(result.sources, &error);
  require(selectors.has_value(), "selector extraction failed: " + error);
  result.selectors = *selectors;
  result.layout = geist::detail::extract_layout_ir(result.sources);
  result.ownership =
      geist::detail::build_verified_ownership_ir(result.sources, result.layout);
  require(result.ownership.has_value(),
          "synthetic generated-list ownership is not verifiable");
  return result;
}

void framed_selection_keeps_its_payload(bool wrapped, bool ordinal,
                                       bool deferred = false,
                                       bool revision = false,
                                       bool bad_prefix = false,
                                       bool spacing_marker = false,
                                       std::size_t revision_column = 1,
                                       const std::string& mark = "|",
                                       bool cz_envelope = false) {
  auto source = make_source(10, {});
  const auto line = [&](std::vector<TokenWords> tokens,
                        TokenWords prefix = TokenWords{0x2500}) {
    source.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(tokens.size()), 1});
    source.tokens.push_back(std::move(prefix));
    for (auto& token : tokens) {
      source.encoded_tokens.push_back({0x80, 1});
      source.tokens.push_back(std::move(token));
    }
  };
  line({words("chdlevel :FIGLIST")});
  line({words("ST Figures")});
  if (cz_envelope) {
    line({words("cz BREAK 3")});
    line({words("cz OFF FIGLIST")});
  }
  if (wrapped) {
    line({words("cselect 3 9 FIGONE")});
    line({words("   "), words("13"), TokenWords{1, '.'}, words(" "),
          words("Alpha")});
  }
  line({words(ordinal ? "cselect 3 25 FIGONE" :
              revision ? "cselect " + std::to_string(revision_column + 3) +
                             " 19 FIGONE" : "cselect 3 19 FIGONE")});
  const auto payload_begin = source.tokens.size();
  if (ordinal)
    line({words("   "), words("4"), TokenWords{1, '-'},
          TokenWords{1, '1', '3'}, words(" "), words("Libraries"),
          words("  "), words("6.9.3.3")},
         spacing_marker ? TokenWords{1} : words("                 "));
  else if (revision) {
    line({words(std::string(revision_column, ' ')), words(bad_prefix ? "X" : mark), words(" "), words("Libraries"),
          words("  "), words("6.9.3.3")});
  } else
    line({words("   "), words("Libraries"), words("  "),
          words("6.9.3.3")});
  if (cz_envelope) line({words("cz OFF EFIGLIST 0 0")});
  std::vector<DecodedLogicalRecordSource> records;
  if (deferred) {
    auto continuation = make_source(11, {});
    continuation.tokens.assign(source.tokens.begin() + payload_begin,
                               source.tokens.end());
    continuation.encoded_tokens.assign(
        source.encoded_tokens.begin() + payload_begin,
        source.encoded_tokens.end());
    source.tokens.resize(payload_begin);
    source.encoded_tokens.resize(payload_begin);
    records.push_back(std::move(source));
    records.push_back(std::move(continuation));
  } else {
    records.push_back(std::move(source));
  }
  for (auto& record : records) {
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    const auto begin = record.logical_record * 100;
    auto byte = begin;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto spacing = record.tokens[token].front() < 4;
      record.ir.tokens.push_back(
          {token, record.encoded_tokens[token], record.tokens[token],
           {byte, byte + 1}, spacing,
           spacing ? record.tokens[token].front() : std::uint16_t{3}});
      ++byte;
    }
    record.ir.payload_range = {begin, byte};
    geist::detail::assign_display_line_framing(record.ir);
    record.control_segments = geist::detail::decode_control_segments(
        record.logical_record, record.assembled, record.encoded_tokens,
        record.ir.display_lines);
    geist::detail::demote_display_line_owned_controls(record);
    geist::detail::assign_display_line_margin_marks(record);
    require(record.ir.display_lines_parse,
            "generated-list source does not frame");
  }
  const auto value = pipeline(std::move(records));
  std::string error;
  if (!value.ownership) return;
  const geist::detail::OwnershipIR& ownership = *value.ownership;
  if (spacing_marker) {
    auto mutated = ownership;
    bool found = false;
    for (auto& cell : mutated.cells)
      if (cell.logical_record == 11 && cell.token_index == 0 &&
          cell.word_index == 0) {
        found = true;
        require(cell.disposition ==
                    geist::detail::SourceDisposition::marker_slot &&
                    cell.field_role == geist::detail::SourceFieldRole::supplemental,
                "native length byte was interpreted as dictionary spacing control");
        cell.disposition = geist::detail::SourceDisposition::control_operand;
      }
    require(found && !geist::detail::verify_ownership_ir(
                         value.sources, value.layout, mutated, &error),
            "mutated native marker ownership passed verification");
  }
  for (const auto& record : value.sources)
    for (std::size_t token = 0; token < record.tokens.size(); ++token)
      if (record.tokens[token] == words("Libraries"))
        for (const auto& cell : ownership.cells)
          if (cell.logical_record == record.logical_record &&
              cell.token_index == token)
            require(cell.disposition ==
                        geist::detail::SourceDisposition::visible_content,
                    "native caption payload is not owned as visible text");
  const auto list = geist::detail::extract_generated_list_topic_ir(
      value.sources, value.selectors, value.layout, *value.ownership, &error);
  if (bad_prefix || revision_column != 1) {
    require(!list && error.find("excludes leading text") != std::string::npos,
            "native generated-list selection silently dropped leading text");
    return;
  }
  const auto count = wrapped ? 2U : 1U;
  require(list && list->entries.size() == count,
          "wrapped generated-list source declined: " + error);
  if (!list || list->entries.size() != count) return;
  const auto expected = ordinal ? "4-13  Libraries   6.9.3.3" :
                                  "Libraries   6.9.3.3";
  require(label_text(list->entries.back()) == expected,
          "framed caption lost payload: " + label_text(list->entries.back()));
  require_entry_partition(list->entries.back());
  if (revision) {
    const auto& row = list->entries.back().display;
    require(row.spans.front().cell_begin == 4 &&
                row.cells[1].word == static_cast<unsigned char>(mark.front()) &&
                row.cells[4].word == 'L',
            "native revision margin shifted the selected caption columns");
    // A list in a `cz OFF FIGLIST` envelope is served as `<dl><dd>` rows
    // with no margin, and the stored mark is not printed: SC31-6552-00
    // FIGURES (DT 19950503072955) serves `<dd>  <a ...> 2.  APPN-to-Subarea
    // COS Mapping Table Example   3.8</a>` for the row ` |  2.  APPN-to-...`.
    if (cz_envelope) {
      require(!list->entries.back().revision_mark,
              "a selection row in a CZ list envelope carries its mark");
      return;
    }
    // The row's stored mark travels with the entry, and the lowering prints
    // it in the margin in front of the link, as hosted BookServer does.
    require(list->entries.back().revision_mark ==
                static_cast<char32_t>(mark.front()),
            "the selection row lost its revision mark");
    geist::detail::TopicIdentityIR identity;
    identity.id = "FIGURES";
    identity.title = "Figures";
    const auto document =
        geist::detail::lower_generated_list_topic_to_document_ir(identity,
                                                                 *list, &error);
    require(document.has_value(), "generated list did not lower: " + error);
    if (!document) return;
    const auto& last = document->blocks.back();
    const auto* paragraph =
        std::get_if<geist::detail::ParagraphBlockIR>(&last.node);
    const auto* revised =
        paragraph != nullptr && paragraph->content.size() == 1
            ? std::get_if<geist::detail::RevisedInlineIR>(
                  &paragraph->content.front().node)
            : nullptr;
    require(revised != nullptr &&
                revised->mark == static_cast<char32_t>(mark.front()) &&
                revised->children.size() == 1 &&
                std::holds_alternative<geist::detail::CrossReferenceInlineIR>(
                    revised->children.front().node),
            "the revised selection row is not one marked link");
    require(geist::detail::verify_generated_list_topic_document_ir(
                *list, *document, &error),
            "the marked generated list fails its own verifier: " + error);
  } else {
    require(!list->entries.back().revision_mark,
            "an unrevised selection row carries a revision mark");
  }
}

void verify_synthetic_contract() {
  const auto value = pipeline({make_source(
      10, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("cselect 3 4 FIGONE"), words("?"), words("   "),
           words("ABCD")})});
  std::string error;
  const auto list = geist::detail::extract_generated_list_topic_ir(
      value.sources, value.selectors, value.layout, *value.ownership, &error);
  require(list && list->entries.size() == 1 &&
              list->kind == geist::detail::GeneratedListTopicKindIR::figures &&
              geist::detail::verify_generated_list_topic_ir(
                  value.sources, value.selectors, value.layout,
                  *value.ownership, *list, &error),
          "complete FIGLIST topic was rejected: " + error);
  if (list) {
    auto mutated = *list;
    ++mutated.heading_source.byte_end;
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 *value.ownership, mutated, &error),
            "generated-list verifier admitted mutated heading provenance");
    mutated = *list;
    require(mutated.entries.front().display.cells.front().source.has_value(),
            "synthetic generated row has no source provenance");
    ++mutated.entries.front()
          .display.cells.front()
          .source->token_bytes.end;
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 *value.ownership, mutated, &error),
            "generated-list verifier admitted mutated cell provenance");
    mutated = *list;
    mutated.entries.front().target.raw_target = "OTHER";
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 *value.ownership, mutated, &error),
            "generated-list verifier admitted mutated raw target identity");
    mutated = *list;
    ++mutated.entries.front()
          .label_fragments.front()
          .source_slices.front()
          .segment_index;
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 *value.ownership, mutated, &error),
            "generated-list verifier admitted mutated fragment provenance");
  }
  geist::detail::TopicIdentityIR identity;
  identity.id = "FIGURES";
  identity.title = "Figures";
  identity.start_logical_record = 10;
  identity.end_logical_record = 10;
  const auto document = list
                            ? geist::detail::lower_generated_list_topic_to_document_ir(
                                  identity, *list, &error)
                            : std::nullopt;
  const auto* paragraph =
      document && document->blocks.size() == 2
          ? std::get_if<geist::detail::ParagraphBlockIR>(
                &document->blocks[1].node)
          : nullptr;
  const geist::detail::CrossReferenceInlineIR* link = nullptr;
  if (paragraph)
    for (const auto& inline_node : paragraph->content)
      if (const auto* candidate =
              std::get_if<geist::detail::CrossReferenceInlineIR>(
                  &inline_node.node))
        link = candidate;
  require(document && link && link->label == "ABCD" &&
              link->targets.size() == 1 &&
              link->targets.front().target.kind ==
                  geist::detail::CrossReferenceTargetKindIR::anchor &&
              link->targets.front().target.value == "FIGONE" &&
              geist::detail::verify_generated_list_topic_document_ir(
                  *list, *document, &error),
          "generated list did not lower to a typed raw anchor: " + error +
              (document ? "\n" + geist::detail::format_document_ir(*document)
                        : std::string{}));

  const auto extra = pipeline({make_source(
      11, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("cselect 3 4 FIGONE"), words("?"), words("   "),
           words("ABCD"), words("CMENU")})});
  require(!geist::detail::extract_generated_list_topic_ir(
               extra.sources, extra.selectors, extra.layout,
               *extra.ownership,
               &error),
          "generated-list envelope admitted a trailing typed object");

  const auto mismatch = pipeline({make_source(
      12, {words("chdlevel :TLIST"), words("ST Figures"),
           words("cselect 3 4 TBLONE"), words("?"), words("   "),
           words("ABCD")})});
  require(!geist::detail::extract_generated_list_topic_ir(
               mismatch.sources, mismatch.selectors, mismatch.layout,
               *mismatch.ownership, &error),
          "generated-list envelope admitted a mismatched ST title");

  const auto directives = pipeline({make_source(
      13, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("c.sp 3p p c"), words("cz BREAK 3"),
           words("cz OFF FIGLIST"), words("cselect 3 4 FIGONE"), words("?"),
           words("   "), words("ABCD"), words("cz OFF EFIGLIST 0 0")})});
  const auto directed = geist::detail::extract_generated_list_topic_ir(
      directives.sources, directives.selectors, directives.layout,
      *directives.ownership, &error);
  require(directed && directed->entries.size() == 1,
          "typed generated-list directive envelope was rejected: " + error);

  const auto wrong_directive = pipeline({make_source(
      14, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("cz BREAK 3"), words("cz OFF TLIST"),
           words("cselect 3 4 FIGONE"), words("?"), words("   "),
           words("ABCD"), words("cz OFF ETLIST 0 0")})});
  require(!geist::detail::extract_generated_list_topic_ir(
               wrong_directive.sources, wrong_directive.selectors,
               wrong_directive.layout, *wrong_directive.ownership, &error),
          "FIGLIST admitted TLIST-specific directives");

  const auto malformed_directive = pipeline({make_source(
      15, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("cz BREAK 4"), words("cselect 3 4 FIGONE"), words("?"),
           words("   "), words("ABCD")})});
  require(!geist::detail::extract_generated_list_topic_ir(
               malformed_directive.sources, malformed_directive.selectors,
               malformed_directive.layout, *malformed_directive.ownership,
               &error),
          "generated list admitted an unknown CZ operand form");
}

#ifdef GEIST_FIXTURE_DIR
void load_context(const std::filesystem::path& path,
                  geist::detail::LogicalDecodeContext* context_ptr) {
  auto& context = *context_ptr;
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  // The dictionary text the directory version and parameter select.
  context.directory.version_text =
      geist::detail::EbcdicCodec::cp037().decode_ascii(
          context.bytes, base + 0x10, 4, "short directory");
  context.directory.dictionary_text_parameter = static_cast<std::uint16_t>(
      (context.bytes[base + 0x4c] << 8) | context.bytes[base + 0x4d]);
  context.directory.token_map_offset =
      geist::detail::read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      geist::detail::read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      geist::detail::read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      geist::detail::read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count =
      geist::detail::read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page =
      geist::detail::read_be16(context.bytes, base + 0x3a);
  context.decoded_records = geist::detail::decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
}

void verify_generated_control_evidence(
    const std::string& filename, const std::string& topic_id,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  struct ExpectedControl {
    std::uint32_t logical_record;
    std::size_t segment;
    geist::detail::BookControlKind kind;
    geist::detail::OutputRangeIR complete;
    geist::detail::OutputRangeIR opcode;
    geist::detail::OutputRangeIR operands;
    std::string operand_text;
  };
  std::vector<ExpectedControl> expected;
  if (filename == "packet.boo" && topic_id == "FIGURES") {
    expected = {
        {11, 9, geist::detail::BookControlKind::layout_directive, {148, 158},
         {148, 150}, {150, 158}, "BREAK 3"},
        {11, 10, geist::detail::BookControlKind::layout_directive, {161, 175},
         {161, 163}, {163, 175}, "OFF FIGLIST"},
        {11, 20, geist::detail::BookControlKind::layout_directive, {898, 917},
         {898, 900}, {900, 917}, "OFF EFIGLIST 0 0"},
    };
  } else if (filename == "packet.boo" && topic_id == "TABLES") {
    expected = {
        {12, 9, geist::detail::BookControlKind::layout_directive, {140, 150},
         {140, 142}, {142, 150}, "BREAK 3"},
        {12, 10, geist::detail::BookControlKind::layout_directive, {153, 165},
         {153, 155}, {155, 165}, "OFF TLIST"},
        {12, 18, geist::detail::BookControlKind::layout_directive, {690, 707},
         {690, 692}, {692, 707}, "OFF ETLIST 0 0"},
    };
  } else {
    return;
  }
  require(!expected.empty(), "generated-control evidence set is empty");
  for (const auto& item : expected) {
    const auto record = std::find_if(
        sources.begin(), sources.end(), [&](const auto& candidate) {
          return candidate.logical_record == item.logical_record;
        });
    require(record != sources.end() &&
                item.segment < record->control_segments.size(),
            "generated-control evidence segment is missing");
    const auto& segment = record->control_segments[item.segment];
    const auto text =
        geist::detail::token_words_to_ascii(record->assembled.words);
    const auto slice = [&](const geist::detail::OutputRangeIR& range) {
      return geist::detail::trim_ascii(
          text.substr(range.begin, range.end - range.begin));
    };
    require(segment.kind == item.kind && !segment.malformed &&
                segment.complete.begin == item.complete.begin &&
                segment.complete.end == item.complete.end &&
                segment.opcode_range.begin == item.opcode.begin &&
                segment.opcode_range.end == item.opcode.end &&
                segment.operand_range.begin == item.operands.begin &&
                segment.operand_range.end == item.operands.end &&
                segment.payload_range.begin == segment.payload_range.end &&
                segment.payload_range.begin == item.complete.end &&
                slice(segment.operand_range) == item.operand_text,
            "generated-control source range or operand evidence changed");
  }
}

void require_fixed_prefixes(
    const geist::detail::GeneratedListTopicIR& list,
    const std::vector<std::pair<std::size_t, std::string>>& expected,
    std::size_t* audited) {
  for (const auto& item : expected) {
    require(item.first != 0 && item.first <= list.entries.size(),
            "generated-list prefix audit index is invalid");
    const auto& entry = list.entries[item.first - 1];
    const auto label = label_text(entry);
    require(label.rfind(item.second, 0) == 0,
            "generated-list label retained source decoration at entry " +
                std::to_string(item.first) + ": " + label);
    require(std::find(entry.cell_dispositions.begin(),
                      entry.cell_dispositions.end(),
                      geist::detail::GeneratedListCellDispositionIR::
                          layout_decoration) !=
                entry.cell_dispositions.end() ||
                !entry.suppressed_prefix_dispositions.empty(),
            "generated-list fixed prefix has no conserved decoration");
    ++*audited;
  }
}

void verify_corpus_inventory() {
  const auto directory = std::filesystem::path(GEIST_FIXTURE_DIR);
  std::vector<std::string> admitted;
  std::vector<std::string> rejected_candidates;
  auto entries = std::size_t{0};
  auto audited_prefixes = std::size_t{0};
  auto audited_artifacts = std::size_t{0};
  for (const auto& file : std::filesystem::directory_iterator(directory)) {
    if (!file.is_regular_file()) continue;
    auto extension = file.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char ch) { return std::tolower(ch); });
    if (extension != ".boo") continue;
    const auto book = geist::BooDocument::open(file.path());
    geist::detail::LogicalDecodeContext context;
    load_context(file.path(), &context);
    for (const auto& topic : book.topics()) {
      const auto sources = geist::detail::decode_logical_record_sources(
          context, topic.start_logical_record, topic.end_logical_record);
      verify_generated_control_evidence(file.path().filename().string(),
                                        topic.id, sources);
      const auto selectors = geist::detail::extract_selector_catalog_ir(sources);
      if (!selectors) continue;
      const auto layout = geist::detail::extract_layout_ir(sources);
      std::string error;
      const auto ownership =
          geist::detail::build_verified_ownership_ir(sources, layout, &error);
      const auto list =
          ownership ? geist::detail::extract_generated_list_topic_ir(
                          sources, *selectors, layout, *ownership, &error)
                    : std::optional<geist::detail::GeneratedListTopicIR>{};
      if (!list) {
        if (topic.id == "FIGURES" || topic.id == "TABLES")
          rejected_candidates.push_back(file.path().filename().string() + ':' +
                                        topic.id);
        continue;
      }
      require(geist::detail::verify_generated_list_topic_ir(
                  sources, *selectors, layout, *ownership, *list, &error),
              "admitted generated-list topic did not verify: " +
                  file.path().filename().string() + ':' + topic.id + ' ' + error);
      for (const auto& entry : list->entries) require_entry_partition(entry);
      const auto filename = file.path().filename().string();
      geist::detail::TopicIdentityIR identity;
      identity.id = topic.id;
      identity.title = topic.title;
      identity.topic_number = topic.topic_number;
      identity.start_logical_record = topic.start_logical_record;
      identity.end_logical_record = topic.end_logical_record;
      const auto document =
          geist::detail::lower_generated_list_topic_to_document_ir(
              identity, *list, &error);
      require(document &&
                  geist::detail::verify_generated_list_topic_document_ir(
                      *list, *document, &error),
              "generated list did not lower canonically: " + error);
      for (std::size_t index = 0; index < list->entries.size(); ++index) {
        using Slice = std::tuple<std::uint32_t, std::size_t, std::size_t,
                                 std::size_t, std::uint32_t, std::uint32_t>;
        std::set<Slice> expected_slices;
        for (const auto& fragment : list->entries[index].label_fragments)
          for (const auto& slice : fragment.source_slices)
            expected_slices.emplace(
                slice.logical_record, slice.segment_index, slice.token_begin,
                slice.token_end, slice.byte_begin, slice.byte_end);
        const auto* paragraph = document && index + 1 < document->blocks.size()
                                    ? std::get_if<geist::detail::ParagraphBlockIR>(
                                          &document->blocks[index + 1].node)
                                    : nullptr;
        const auto* link =
            paragraph && paragraph->content.size() == 1
                ? std::get_if<geist::detail::CrossReferenceInlineIR>(
                      &paragraph->content.front().node)
                : nullptr;
        std::set<Slice> actual_slices;
        if (link)
          for (const auto& slice : paragraph->content.front().origin.slices)
            actual_slices.emplace(
                slice.logical_record, slice.segment_index, slice.token_begin,
                slice.token_end, slice.byte_begin, slice.byte_end);
        require(link && expected_slices == actual_slices,
                "generated-list lowering invented or lost fragment provenance");
      }
      admitted.push_back(file.path().filename().string() + ':' + topic.id + ':' +
                         std::to_string(list->entries.size()));
      entries += list->entries.size();
    }
  }
  std::sort(admitted.begin(), admitted.end());
  // Only packet.boo may be redistributed; the other 26 book/topic entries of
  // this inventory, and the fixed-prefix, decoder-artifact and cross-record
  // label audits that ran on them, went with those books (issue #59).
  auto expected = std::vector<std::string>{"packet.boo:FIGURES:9",
                                           "packet.boo:TABLES:7"};
  std::sort(expected.begin(), expected.end());
  std::sort(rejected_candidates.begin(), rejected_candidates.end());
  const auto expected_rejections = std::vector<std::string>{};
  require(admitted == expected && entries == 16,
          "whole-topic generated-list inventory changed");
  require(rejected_candidates == expected_rejections,
          "generated-list fail-closed candidate inventory changed");
  require(audited_prefixes == 0 && audited_artifacts == 0,
          "the fixed-prefix and decoder-artifact audits went with the books "
          "that cannot be published; nothing should be counted here");
}
#endif

} // namespace

// A whole generated-list topic through the typed dispatcher.  Every line is
// a length byte followed by its tokens, as the reader frames a record.
using Line = std::vector<TokenWords>;

DecodedLogicalRecordSource framed_topic(
    const std::vector<Line>& lines,
    const std::vector<std::pair<std::size_t, TokenWords>>& length_spellings) {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto& content = lines[index];
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    // The record's first length byte spells `,` after a spacing control, as
    // the real ones do (dfhtkb00 record 30 token 0, value 2), so that the
    // `sh` line opens the record's first segment.
    TokenWords length =
        record.tokens.empty() ? TokenWords{1, ','} : TokenWords{0x25BA};
    for (const auto& spelling : length_spellings)
      if (spelling.first == index) length = spelling.second;
    record.tokens.push_back(length);
    for (const auto& token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(token);
    }
  }
  record.logical_record = 2;
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = 2;
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
  geist::detail::assign_display_line_margin_marks(record);
  return record;
}

// The dispatched document as `<family>=` and its blocks: `h<n>:<text>`,
// `a:<id>`, `p` or `?`, joined by `|`; `<declined: ...>` when no typed family
// claimed the topic.
std::string dispatched_outline(
    const std::string& id, const std::vector<Line>& body,
    const std::string& level,
    const std::vector<std::pair<std::size_t, TokenWords>>& body_length_spellings =
        {}) {
  std::vector<Line> lines = {
      {words("sh" + geist::detail::ascii_lower(id))},
      {words("ctopicn"), words("6")},
      {words("cparent")},
      {words("cforwardlevel"), words("1.0")},
      {words("cbacklevel")},
      {words("csummary"), words("2"), words("0"), words("2")},
      {words("chdlevel"), words(level)},
      {words("csourcefn"), words("RBANSMST")},
  };
  std::vector<std::pair<std::size_t, TokenWords>> length_spellings;
  for (const auto& spelling : body_length_spellings)
    length_spellings.push_back({lines.size() + spelling.first, spelling.second});
  lines.insert(lines.end(), body.begin(), body.end());
  std::vector<DecodedLogicalRecordSource> records{
      framed_topic(lines, length_spellings)};
  geist::detail::TopicIdentityIR identity;
  identity.id = id;
  identity.start_logical_record = 2;
  identity.end_logical_record = 2;
  std::string rejection;
  geist::detail::TypedLoweringTraceIR trace;
  const auto document = geist::detail::try_lower_topic_to_document_ir(
      identity, records, nullptr, &rejection, &trace);
  if (!document) {
    std::string declined = "<declined: " + rejection;
    for (const auto& reason : trace.declined)
      if (reason.rfind("generated list", 0) == 0) declined += " | " + reason;
    return declined + ">";
  }
  std::string outline = trace.family + "=";
  for (const auto& block : document->blocks) {
    if (outline.back() != '=') outline += '|';
    if (const auto* heading =
            std::get_if<geist::detail::HeadingBlockIR>(&block.node)) {
      outline += "h" + std::to_string(heading->level) + ":";
      for (const auto& item : heading->content)
        if (const auto* text =
                std::get_if<geist::detail::TextInlineIR>(&item.node))
          outline += text->text;
    } else if (const auto* anchor =
                   std::get_if<geist::detail::AnchorBlockIR>(&block.node)) {
      outline += "a:" + anchor->id;
    } else if (const auto* paragraph =
                   std::get_if<geist::detail::ParagraphBlockIR>(&block.node)) {
      outline += "p";
      for (const auto& item : paragraph->content)
        if (const auto* link =
                std::get_if<geist::detail::CrossReferenceInlineIR>(&item.node))
          outline += "[" + link->label + "]";
    } else {
      outline += "?";
    }
  }
  return outline;
}

void expect_dispatched(const std::string& label, const std::string& found,
                       const std::string& expected) {
  require(found == expected,
          label + ": '" + found + "', expected '" + expected + "'");
}

void generated_list_envelope_shapes() {
  // A destination row between the metadata and the title.  dfhtkb00
  // FIGURES record 30 stores `SRHDRFIGURES` (segment 8) before `ST
  // Figures`; it names the topic for links and draws nothing.
  expect_dispatched(
      "SRHDRFIGURES before ST",
      dispatched_outline(
          "FIGURES",
          {{words("SRHDRFIGURES")},
           {words("ST"), words("  "), words("Figures")},
           {words("cselect"), words("3"), words("15"), words("FIGONE")},
           {words("   "), words("1."), words("  "), words("Alpha"),
            words("   "), words("1.1")}},
          ":FIGLIST"),
      "generated list=h1:Figures|a:HDRFIGURES|p[1.   Alpha    1.1]");
  // A `c.rev` line between two entries draws nothing (SC41-4304-00 TABLES
  // record 31 segment 16, `c.rev MJA |`; hosted DT 19951006131114).
  expect_dispatched(
      "c.rev between entries",
      dispatched_outline(
          "TABLES",
          {{words("ST"), words("  "), words("Tables")},
           {words("cselect"), words("3"), words("15"), words("TBLONE")},
           {words("   "), words("1."), words("  "), words("Alpha"),
            words("   "), words("1.1")},
           {words("c.rev"), words("MJA"), TokenWords{0x2502}},
           {words("cselect"), words("3"), words("14"), words("TBLTWO")},
           {words("   "), words("2."), words("  "), words("Beta"),
            words("   "), words("1.2")}},
          ":TLIST"),
      "generated list=h1:Tables|p[1.   Alpha    1.1]|p[2.   Beta    1.2]");
  // A list with no entries: the directive pair alone.  SC31-8836-04 TABLES
  // record 22 lines 10-12 are `cz BREAK 3`, `cz OFF TLIST`, `cz OFF ETLIST 0
  // 0`; hosted serves `<h1>    Tables</h1>` and nothing else.
  expect_dispatched(
      "empty TLIST",
      dispatched_outline("TABLES",
                         {{words("ST"), words("  "), words("Tables")},
                          {words("c.sp"), words("1"), words("p")},
                          {words("cz"), words("BREAK"), words("3")},
                          {words("cz"), words("OFF"), words("TLIST")},
                          {words("cz"), words("OFF"), words("ETLIST"),
                           words("0"), words("0")}},
                         ":TLIST"),
      "generated list=h1:Tables");
  // The same where the closer's length byte spells a rule run and the
  // splitter leaves it in the opener's payload: SC31-8836-04 TABLES record
  // 22 line 12, `cz OFF TLIST` + a 62-cell rule spelling, then `cz OFF
  // ETLIST 0 0`.
  expect_dispatched(
      "empty TLIST, closer's length byte spelled as a rule",
      dispatched_outline("TABLES",
                         {{words("ST"), words("  "), words("Tables")},
                          {words("c.sp"), words("1"), words("p")},
                          {words("cz"), words("BREAK"), words("3")},
                          {words("cz"), words("OFF"), words("TLIST")},
                          {words("cz"), words("OFF"), words("ETLIST"),
                           words("0"), words("0")}},
                         ":TLIST", {{4, TokenWords(62, 0x2500)}}),
      "generated list=h1:Tables");
  // A selection's label ends with its row, even where the next row's
  // length byte spells visible glyphs: SC31-8219-00 TABLES record 15 line
  // 13 is the row (styled by the `cfont` of line 12) and line 14 (`cselect
  // 8 40 TBLTRCF32B`, the continuation's selector) opens on the byte `06`,
  // which that book's dictionary spells as a run of 62 rule glyphs.  Hosted prints `1.  Flags and Parameters ... Commands` and the
  // continuation, and no rule.
  expect_dispatched(
      "label ends before the next row's length byte",
      dispatched_outline("TABLES",
                         {{words("ST"), words("  "), words("Tables")},
                          {words("cselect"), words("3"), words("15"),
                           words("TBLONE")},
                          {words("cfont"), words("8"), words("5"),
                           words("2")},
                          {words("   "), words("1."), words("  "),
                           words("Alpha"), words("   "), words("1.1")},
                          {words("cselect"), words("3"), words("14"),
                           words("TBLTWO")},
                          {words("   "), words("2."), words("  "),
                           words("Beta"), words("   "), words("1.2")}},
                         ":TLIST", {{4, TokenWords(62, 0x2500)}}),
      "generated list=h1:Tables|p[1.   Alpha    1.1]|p[2.   Beta    1.2]");
}

int main() {
  framed_selection_keeps_its_payload(true, false);
  framed_selection_keeps_its_payload(false, false);
  framed_selection_keeps_its_payload(false, true);
  framed_selection_keeps_its_payload(false, true, true);
  framed_selection_keeps_its_payload(false, false, false, true);
  framed_selection_keeps_its_payload(false, false, false, true, true);
  framed_selection_keeps_its_payload(false, true, true, false, false, true);
  framed_selection_keeps_its_payload(false, false, false, true, false, false, 2);
  // SC33-0668-02 FIGURES record 21 line 12 is ` # 39.  DFHJUP program,
  // example 1   2.1.1.5.1` under `cselect 3 42 FIGJUPEG1`: a `#` revision
  // mark in the same margin column.  Hosted (DT 19930625001740) serves
  // ` # <a href=...#FIGJUPEG1>39.  DFHJUP program, example 1   2.1.1.5.1</a>`.
  framed_selection_keeps_its_payload(false, false, false, true, false, false, 1,
                                     "#");
  framed_selection_keeps_its_payload(false, false, false, true, false, false, 1,
                                     "|", true);
  verify_synthetic_contract();
  generated_list_envelope_shapes();
#ifdef GEIST_FIXTURE_DIR
  verify_corpus_inventory();
#endif
}
