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

#include "geist/detail/display_lines.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/prose_topic_document_lowering.hpp"
#include "geist/detail/prose_topic_ir.hpp"
#include "geist/detail/render_diagnostic_ir.hpp"
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
  a_closed_unmodelled_region_degrades_its_block();
  an_unclosed_unmodelled_region_declines_the_topic();
  geist_test::exit_with_failures();
  std::cout << "cz_degraded_region_synthetic: ok\n";
  return 0;
}
