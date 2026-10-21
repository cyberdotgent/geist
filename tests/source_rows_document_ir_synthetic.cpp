// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A declined topic still has one renderer-neutral representation.  These
// synthetic source rows reproduce the old split where Markdown and HTML read
// TopicBestEffortIR directly instead of consuming the same DocumentIR.

#include "geist/detail/lowering/source_rows_document_lowering.hpp"
#include "geist/detail/lowering/topic_lowering_outcome.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::PreformattedBlockIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    geist_test::record_failure();
  }
}

void append(DecodedLogicalRecordSource &record, std::uint16_t encoded,
            std::uint8_t width, TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
}

void line(DecodedLogicalRecordSource &record,
          const std::vector<std::string> &words) {
  append(record, static_cast<std::uint16_t>(words.size() * 2), 1,
         {3, 'r', 'o', 'w'});
  for (const auto &word : words) {
    TokenWords token;
    for (const auto ch : word)
      token.push_back(
          static_cast<std::uint16_t>(static_cast<unsigned char>(ch)));
    append(record, 0x40, 2, std::move(token));
  }
}

void refresh(DecodedLogicalRecordSource &record) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  std::uint32_t byte = 100;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    const auto spacing = !record.tokens[token].empty() &&
                         record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
         spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {100, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
}

} // namespace

int main() {
  DecodedLogicalRecordSource record;
  record.logical_record = 17;
  line(record, {"cselect", "6", "4", "TARGET"});
  line(record, {"Alpha", "Beta", "Gamma"});
  refresh(record);

  geist::detail::TopicIdentityIR identity;
  identity.id = "DECLINED";
  identity.title = "Recovered rows";
  identity.heading_level = ":H2";
  identity.start_logical_record = 17;
  identity.end_logical_record = 18;

  const auto lowered = geist::detail::lower_source_rows_to_document_ir(
      identity, std::vector<DecodedLogicalRecordSource>{record});
  require(lowered.source_decoded, "the decoded source was reported missing");
  require(lowered.document.has_value(), "decoded rows produced no DocumentIR");
  require(lowered.has_body, "the visible source row was reported empty");
  if (!lowered.document) return 0;

  std::string error;
  require(geist::detail::verify_document_ir(*lowered.document, &error),
          "recovery DocumentIR is invalid: " + error);
  const auto *pre = std::get_if<PreformattedBlockIR>(
      &lowered.document->blocks.back().node);
  require(pre != nullptr, "the source rows are not a preformatted block");
  if (pre != nullptr) {
    require(pre->lines == std::vector<std::string>{"Alpha Beta Gamma"},
            "the recovery block changed the drawn row");
    require(pre->line_links.size() == 1 && pre->line_links[0].size() == 1,
            "the recovery block discarded the positioned link");
    require(pre->line_origins.size() == 1 &&
                !pre->line_origins[0].slices.empty() &&
                pre->line_origins[0].slices[0].logical_record == 17 &&
                pre->line_origins[0].slices[0].byte_begin >= 100,
            "the recovery row discarded its stable source identity");
  }

  const auto markdown =
      geist::detail::render_document_markdown(*lowered.document);
  const auto html =
      geist::detail::render_document_html_blocks(*lowered.document, {});
  require(markdown.find("## DECLINED Recovered rows") != std::string::npos &&
              markdown.find("<pre>\nAlpha <a href=\"#TARGET\">Beta</a> Gamma\n"
                            "</pre>") != std::string::npos,
          "Markdown did not consume the recovery DocumentIR faithfully: " +
              markdown);
  require(html.find("<h2") != std::string::npos &&
              html.find("Alpha <a class=\"geist-link geist-link--in-book\"") !=
                  std::string::npos &&
              html.find(">Beta</a> Gamma") != std::string::npos,
          "HTML did not consume the same positioned link: " + html);

  geist::detail::TopicLoweringOutcomeIR declined;
  declined.document = *lowered.document;
  declined.representation =
      geist::detail::TopicRepresentationKindIR::source_rows;
  declined.source_decoded = true;
  declined.source_rows_have_body = true;
  declined.rejection = "synthetic family rejection";
  const auto diagnostic =
      geist::detail::classify_topic_lowering(identity, &declined);
  require(diagnostic.severity == geist::RenderSeverity::best_effort &&
              diagnostic.route == "best-effort" &&
              diagnostic.reason == "typed-lowering-declined",
          "a source-row DocumentIR was incorrectly relabelled typed");

  DecodedLogicalRecordSource unlinked_record;
  unlinked_record.logical_record = 18;
  line(unlinked_record, {"Unlinked", "source", "row"});
  refresh(unlinked_record);
  const auto unlinked = geist::detail::lower_source_rows_to_document_ir(
      identity, std::vector<DecodedLogicalRecordSource>{unlinked_record});
  require(unlinked.document.has_value(),
          "an unlinked decoded row produced no DocumentIR");
  if (unlinked.document) {
    const auto rendered =
        geist::detail::render_document_markdown(*unlinked.document);
    require(rendered.find("<pre>\nUnlinked source row\n</pre>") !=
                std::string::npos &&
                rendered.find("```") == std::string::npos,
            "unlinked recovery rows changed from raw source-row HTML: " +
                rendered);
  }

  DecodedLogicalRecordSource empty_record;
  empty_record.logical_record = 17;
  const auto empty = geist::detail::lower_source_rows_to_document_ir(
      identity, std::vector<DecodedLogicalRecordSource>{empty_record});
  require(empty.document.has_value() && !empty.has_body,
          "a decoded empty topic did not retain a heading-only DocumentIR");

  return 0;
}
