// Render provenance (issue #58).
//
// Every rendered topic carries a typed diagnostic saying how well it was
// rendered and why.  This test pins the severities that occur in the one
// redistributable fixture, pins the synthetic ladder below them, and proves
// that the diagnostic and the `bootrace --coverage` metric describe every
// topic of a whole book identically -- they read the same RenderDiagnostic,
// and this is the assertion that keeps it that way.
//
// packet now carries only `typed`: it has no `typed-degraded` topic, so the
// degraded-block pin that used to stand on SC31-711 5.0 (a message catalog
// whose table candidate fell back to preformatted) is gone with the books
// that cannot be published (issue #59), and since #74 modelled its two `cz`
// title-page regions it has no `best-effort` topic either.  Both of those
// rungs are pinned synthetically below.

#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/lowering/typed_route_inventory.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(const bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << "\n";
  geist_test::record_failure();
}

std::filesystem::path book(const std::string &name) {
  return std::filesystem::path(GEIST_FIXTURE_DIR) / name;
}

const geist::TocEntry &topic(const geist::BooDocument &document,
                             const std::string &id) {
  const auto *entry = document.find_toc_entry(id);
  if (entry == nullptr) {
    std::cerr << "FAIL: topic not found: " << id << "\n";
    geist_test::record_failure();
    static const geist::TocEntry empty;
    return empty;
  }
  return *entry;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// A fully typed topic reports `typed`, and its Markdown carries no marker at
// all: that is what keeps the typed part of the corpus byte-identical to a
// pipeline without a diagnostics channel.
void fully_typed_topic() {
  const auto document = geist::BooDocument::open(book("packet.boo"));
  const auto &entry = topic(document, "2.1.1");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::typed,
          "packet 2.1.1 should be typed, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.route == "typed", "packet 2.1.1 route should be typed");
  require(diagnostic.family == "prose",
          "packet 2.1.1 family should be prose, is " + diagnostic.family);
  require(diagnostic.degradations.empty(),
          "a fully typed topic has no degradations");
  require(geist::render_diagnostic_comment(diagnostic).empty(),
          "a fully typed topic emits no marker");
  require(!contains(entry.markdown(), "<!-- geist-render:"),
          "a fully typed topic's Markdown carries no marker");
  require(diagnostic.source.start_logical_record != 0 &&
              diagnostic.source.end_logical_record >
                  diagnostic.source.start_logical_record,
          "the diagnostic names the topic's source record range");
}

// Every topic of the one redistributable fixture is now claimed by a typed
// family, so there is no corpus-backed subject for the `best-effort` route
// left in it.
//
// This assertion has moved three times as coverage grew: from packet
// GLOSSARY, which the glossary family claimed (#69), to packet 4.5.1, which
// the `cz FLOW` admission claimed (#75), to packet COVER, which the generated
// title-page projection now claims (#74).  Each move was the fix working.
// packet's last two declining topics were COVER and TITLE, both `cz`
// title-page regions; hosted BookServer does not serve them verbatim either,
// and modelling them was the last thing standing between packet and a fully
// typed book.
//
// Rather than delete the check, say the consequence out loud: the only
// distributable fixture can no longer witness the verbatim route from a real
// book at all.  What stands here instead is the fact itself -- so a
// regression that starts declining a packet topic again is caught by name --
// and a synthetic pin on the route's own classification and output, which is
// what the corpus case was really guarding.  If a redistributable book with a
// declining topic is ever added, restore the corpus-backed form.
void no_declining_topic_left() {
  const auto document = geist::BooDocument::open(book("packet.boo"));
  const auto diagnostics = document.render_diagnostics();
  const auto &contents = document.table_of_contents();
  require(diagnostics.size() == contents.size(),
          "one diagnostic per TOC topic");
  for (std::size_t index = 0;
       index < diagnostics.size() && index < contents.size(); ++index) {
    const auto &diagnostic = diagnostics[index];
    const auto typed =
        diagnostic.severity == geist::RenderSeverity::typed ||
        diagnostic.severity == geist::RenderSeverity::typed_degraded;
    require(typed, "packet " + contents[index].id +
                       " is no longer typed, it is " +
                       std::string(geist::to_string(diagnostic.severity)) +
                       ": " + diagnostic.detail);
  }

  // The route the fixture can no longer reach, pinned synthetically: a
  // declined lowering carries the typed family's own rejection unchanged, and
  // the verbatim renderer emits the topic's words in a fenced block rather
  // than withholding them.
  geist::detail::TopicIdentityIR identity;
  identity.id = "COVER";
  identity.title = "Book Cover";
  identity.start_logical_record = 2;
  identity.end_logical_record = 3;
  const auto declined = geist::detail::classify_typed_lowering(
      identity, nullptr, "prose topic rejected: cz off cover carries display "
                         "text",
      {});
  require(declined.severity == geist::RenderSeverity::best_effort,
          "a declined lowering is best-effort, is " +
              std::string(geist::to_string(declined.severity)));
  require(declined.route == "best-effort", "route should be best-effort");
  require(declined.reason == "typed-lowering-declined",
          "reason code, is " + declined.reason);
  require(declined.detail ==
              "prose topic rejected: cz off cover carries display text",
          "the declining family's own reason is kept, is: " +
              declined.detail);
  const auto marker = geist::render_diagnostic_comment(declined);
  require(contains(marker, "<!-- geist-render: severity=best-effort"),
          "a declined topic's Markdown opens with the marker: " + marker);
  require(contains(marker, "cz off cover"),
          "the marker carries the rejection into the file: " + marker);
  geist::detail::VerbatimRowIR row;
  row.text = "   Amateur Packet Radio";
  const auto verbatim =
      geist::detail::render_best_effort_markdown(identity, {row}, {});
  require(contains(verbatim, "Amateur Packet Radio"),
          "the verbatim route emits the topic's own words");
  require(contains(verbatim, "<pre>\n") && contains(verbatim, "</pre>\n"),
          "the verbatim route emits them as preformatted content");
}

// A declared `cz OFF TABLE` grid lowers to a real table and does NOT degrade
// its topic: degradation is reserved for real loss, and nothing is lost here.
void declared_table_topic() {
  const auto document = geist::BooDocument::open(book("packet.boo"));
  const auto &entry = topic(document, "2.4.4");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::typed,
          "packet 2.4.4 should be typed, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.degradations.empty(),
          "2.4.4 must report no degradation for its declared table");
  const auto markdown = entry.markdown();
  require(markdown.find("<!--") == std::string::npos,
          "a clean topic carries no render-diagnostic comment");
  require(contains(markdown, "| Class | Range | Default Netmask |"),
          "2.4.4 lost the declared table it does not degrade over");
}

// Issue #81, the shape a block-scoped decline would take: a topic whose
// frame, and every block but one, are proven, and whose one unproven region
// is emitted verbatim and marked degraded.
//
// This is a synthetic pin on the *mechanism*, not on any family. It exists
// because packet carries no `typed-degraded` topic (see the header note), so
// the corpus-backed pin that used to stand on SC31-711 5.0 is gone, and
// because the block-level degradation rule rests on three claims about this
// layer that ought to be assertions rather than prose:
//
//   1. `verify_document_ir` accepts a document containing a degraded block
//      without any relaxation -- fidelity is orthogonal to verification, so
//      admitting a degraded block weakens no structural guarantee.
//   2. One degraded block, and only a degraded block, moves the topic from
//      `typed` to `typed-degraded`; the block's own reason code reaches the
//      reader through `render_diagnostic_comment`.
//   3. Degradation is unreachable when the frame is unproven: a declined
//      lowering is `best-effort` and carries no degradations at all.
//
//   4. The renderer is no longer fidelity-blind (issue #81).  A degraded
//      block carries a `<!-- geist-block: degraded=... -->` marker on its own
//      fence, because a proven verbatim region -- an ASCII-drawn figure body,
//      a `cz OFF XMP` example -- renders as the same fence and one
//      topic-level `degraded=` code cannot say which of them it means.  The
//      marker is the *whole* difference from the same block proven, which is
//      what keeps every typed output byte-identical to what it was before the
//      marker existed.  The note recorded this as an open gap and wrote the
//      assertion below to fail when it closed; it has closed, so the
//      assertion now reads the other way and still pins what it pinned.
void degraded_block_topic() {
  using namespace geist::detail;

  const auto slice_at = [](const std::uint32_t record,
                           const std::size_t segment) {
    DocumentSourceSliceIR slice;
    slice.logical_record = record;
    slice.segment_index = segment;
    slice.token_begin = 0;
    slice.token_end = 12;
    return slice;
  };
  const auto region_slice = slice_at(29, 4);

  const auto inline_at = [&](const std::string &text,
                             const std::uint32_t record,
                             const std::size_t segment) {
    InlineIR node;
    node.node = TextInlineIR{text};
    node.origin.derivation = DocumentDerivationIR::decoded;
    node.origin.slices = {slice_at(record, segment)};
    return node;
  };

  const auto build = [&](const DocumentFidelityIR fidelity) {
    DocumentIR document;
    document.topic.id = "PREFACE.2.1";
    document.topic.title = "Syntax Diagrams";
    document.topic.start_logical_record = 28;
    document.topic.end_logical_record = 32;

    BlockIR heading;
    heading.node =
        HeadingBlockIR{2, {inline_at("Syntax Diagrams", 28, 0)}};
    heading.origin.derivation = DocumentDerivationIR::semantic_lowering;
    heading.origin.detail = "topic heading";
    heading.origin.slices = {slice_at(28, 0)};
    document.blocks.push_back(std::move(heading));

    BlockIR before;
    before.node = ParagraphBlockIR{
        {inline_at("Read the syntax diagrams from left to right.", 29, 3)}};
    before.origin.derivation = DocumentDerivationIR::semantic_lowering;
    before.origin.detail = "prose paragraph";
    before.origin.slices = {slice_at(29, 3)};
    document.blocks.push_back(std::move(before));

    // The unproven region. Its boundary is a matched `cz OFF SYNTAX` ..
    // `cz OFF ESYNTAX` pair, so the block owns exactly the region's rows and
    // asserts nothing about their shape.
    BlockIR region;
    PreformattedBlockIR verbatim;
    verbatim.lines = {"   >>__STATEMENT__ _______________ ___________><",
                      "                  |_optional_item_|"};
    region.node = std::move(verbatim);
    region.origin.derivation = DocumentDerivationIR::semantic_lowering;
    region.origin.detail = "cz OFF SYNTAX region: verbatim body";
    region.origin.slices = {region_slice};
    region.origin.fidelity = fidelity;
    if (fidelity == DocumentFidelityIR::degraded) {
      region.origin.degradation_code = "cz-off-region-unmodelled";
      region.origin.degradation_detail =
          "cz OFF SYNTAX is not modelled; the region is bounded by its own "
          "matched cz OFF ESYNTAX and is emitted verbatim";
    }
    document.blocks.push_back(std::move(region));

    BlockIR after;
    after.node = ParagraphBlockIR{
        {inline_at("Optional items appear below the main path.", 29, 7)}};
    after.origin.derivation = DocumentDerivationIR::semantic_lowering;
    after.origin.detail = "prose paragraph";
    after.origin.slices = {slice_at(29, 7)};
    document.blocks.push_back(std::move(after));
    return document;
  };

  const auto degraded_document = build(DocumentFidelityIR::degraded);
  const auto typed_document = build(DocumentFidelityIR::typed);

  // (1) Verification is fidelity-agnostic. The same structural check that
  // guards every typed document accepts this one unchanged; nothing had to be
  // relaxed to let a degraded block through.
  std::string error;
  require(verify_document_ir(degraded_document, &error),
          "a document carrying a degraded block still verifies: " + error);
  require(verify_document_ir(typed_document, &error),
          "the same document without the degradation verifies: " + error);

  // (2) The degraded block, and nothing else, changes the topic's claim.
  TypedLoweringTraceIR trace;
  trace.family = "prose";
  const auto degraded = classify_typed_lowering(
      degraded_document.topic, &degraded_document, {}, trace);
  require(degraded.severity == geist::RenderSeverity::typed_degraded,
          "one degraded block makes the topic typed-degraded, is " +
              std::string(geist::to_string(degraded.severity)));
  require(degraded.route == "typed", "a degraded topic still took the typed "
                                     "route, is " + degraded.route);
  require(degraded.reason == "degraded-block",
          "the topic reason names the degraded block, is " + degraded.reason);
  require(degraded.degradations.size() == 1,
          "exactly the one unproven region is named");
  require(!degraded.degradations.empty() &&
              degraded.degradations.front().reason ==
                  "cz-off-region-unmodelled",
          "the block's own reason code survives into the diagnostic");
  require(!degraded.degradations.empty() &&
              degraded.degradations.front().source.logical_record ==
                  region_slice.logical_record,
          "the degradation names the region's source record, not the topic's");

  const auto clean = classify_typed_lowering(typed_document.topic,
                                             &typed_document, {}, trace);
  require(clean.severity == geist::RenderSeverity::typed,
          "the identical document without the degradation is plain typed");
  require(clean.degradations.empty(), "and carries no degradations");

  // The reader is told, in the file, that one block is unproven and why.
  const auto marker = geist::render_diagnostic_comment(degraded);
  require(contains(marker, "severity=typed-degraded"),
          "the marker states the severity: " + marker);
  require(contains(marker, "degraded=cz-off-region-unmodelled"),
          "the marker names the unproven construct: " + marker);
  require(contains(marker, "cz OFF SYNTAX is not modelled"),
          "the marker carries the block's explanation: " + marker);
  require(geist::render_diagnostic_comment(clean).empty(),
          "and the undegraded twin carries no marker at all");

  // (3) Degradation cannot be reached without a proven frame. When no typed
  // family claimed the topic there is no document, so there is no block to
  // degrade and the topic falls whole -- exactly as it does today.
  const auto unframed = classify_typed_lowering(
      degraded_document.topic, nullptr,
      "prose topic rejected: topic carries a second ST control", trace);
  require(unframed.severity == geist::RenderSeverity::best_effort,
          "an unproven frame is best-effort, never degraded");
  require(unframed.degradations.empty(),
          "an unproven frame names no degraded block");

  // (4) The honesty gap is closed at the block.  A reader of the file can see
  // *which* fence is unproven, not merely that the topic carries one
  // somewhere -- and removing the marker line reproduces the undegraded
  // render byte for byte, which is the assertion that keeps every proven
  // block's output unchanged.
  const auto degraded_markdown = render_document_markdown(degraded_document);
  const auto typed_markdown = render_document_markdown(typed_document);
  require(degraded_markdown != typed_markdown,
          "an unproven verbatim region no longer renders like a proven one");
  const std::string block_marker =
      "<!-- geist-block: degraded=cz-off-region-unmodelled -->\n";
  require(contains(degraded_markdown, block_marker + "```"),
          "the marker names the code immediately before the region's fence: " +
              degraded_markdown);
  require(!contains(typed_markdown, "<!-- geist-block:"),
          "a proven verbatim region carries no marker at all");
  auto stripped = degraded_markdown;
  const auto marker_at = stripped.find(block_marker);
  if (marker_at != std::string::npos)
    stripped.erase(marker_at, block_marker.size());
  require(stripped == typed_markdown,
          "the marker is the whole difference: removing it reproduces the "
          "undegraded render byte for byte");
}

// The synthetic tail of the ladder: `failed` is reserved for a topic whose
// records could not be decoded at all, and is kept apart from a topic that
// decodes cleanly and simply has no body.
void escalation_ladder() {
  geist::detail::TopicIdentityIR identity;
  identity.id = "X.1";
  identity.title = "Example";
  identity.start_logical_record = 7;
  identity.end_logical_record = 9;

  auto declined = geist::detail::classify_typed_lowering(
      identity, nullptr, "family rejected: no envelope", {});
  require(declined.severity == geist::RenderSeverity::best_effort,
          "a declined lowering classifies as best-effort");

  auto with_content = declined;
  geist::detail::escalate_render_diagnostic(with_content, true, true, true);
  require(with_content.severity == geist::RenderSeverity::best_effort,
          "a route that produced content is not escalated");

  auto verbatim = declined;
  geist::detail::escalate_render_diagnostic(verbatim, false, true, true);
  require(verbatim.severity == geist::RenderSeverity::best_effort,
          "no content plus recoverable lines is best-effort");

  auto empty_body = declined;
  geist::detail::escalate_render_diagnostic(empty_body, false, false, true);
  require(empty_body.severity == geist::RenderSeverity::best_effort,
          "a topic that decodes and has no body keeps its route severity");
  require(empty_body.reason == "empty-topic-body",
          "an empty topic is reported as such, is " + empty_body.reason);

  auto failed = declined;
  geist::detail::escalate_render_diagnostic(failed, false, false, false);
  require(failed.severity == geist::RenderSeverity::failed,
          "undecodable source is the only failure");
  const auto placeholder =
      geist::detail::render_failed_markdown(identity, failed);
  require(contains(placeholder, "X.1") && contains(placeholder, "7-9") &&
              contains(placeholder, failed.reason),
          "the placeholder names the topic, its records and the reason");

  // The marker must survive a rejection string containing `--`, which would
  // otherwise close the HTML comment early.
  auto tricky = declined;
  tricky.detail = "rejected: saw --> in the payload";
  const auto marker = geist::render_diagnostic_comment(tricky);
  require(marker.rfind("-->") == marker.size() - 3,
          "the marker has exactly one comment terminator: " + marker);
}

// The metric and the renderer describe every topic of a whole book
// identically. This is the assertion that replaces the convention that they
// "should" agree with a property of the shared value.
void inventory_agrees_with_render(const std::string &name) {
  const auto document = geist::BooDocument::open(book(name));
  const auto inventory = document.typed_route_inventory();
  const auto diagnostics = document.render_diagnostics();
  require(diagnostics.size() == inventory.topics.size(),
          name + ": one diagnostic per TOC topic");
  if (diagnostics.size() != inventory.topics.size())
    return;

  std::size_t typed_route = 0;
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    const auto &measured = inventory.topics[index];
    const auto &rendered = diagnostics[index];
    require(measured.id == document.table_of_contents()[index].id,
            name + ": inventory is parallel to the table of contents");
    require(measured.diagnostic.severity == rendered.severity,
            name + " " + measured.id + ": severity disagrees (" +
                geist::to_string(measured.diagnostic.severity) + " vs " +
                geist::to_string(rendered.severity) + ")");
    require(measured.diagnostic.route == rendered.route,
            name + " " + measured.id + ": route disagrees");
    require(measured.diagnostic.family == rendered.family,
            name + " " + measured.id + ": family disagrees");
    require(geist::detail::typed_route_reason(measured) == rendered.detail,
            name + " " + measured.id + ": coverage reason disagrees");
    const auto typed_severity =
        rendered.severity == geist::RenderSeverity::typed ||
        rendered.severity == geist::RenderSeverity::typed_degraded;
    require(typed_severity ==
                (measured.route == geist::detail::TypedRouteKind::typed),
            name + " " + measured.id +
                ": typed severity and typed route disagree");
    if (typed_severity)
      ++typed_route;
  }
  require(typed_route == inventory.typed_count,
          name + ": typed severities equal the typed route count");

  std::size_t counted = 0;
  for (const auto &[severity, count] : inventory.by_severity)
    counted += count;
  require(counted == inventory.topics.size(),
          name + ": the severity histogram covers every topic");
}

} // namespace

int main() {
  fully_typed_topic();
  no_declining_topic_left();
  declared_table_topic();
  degraded_block_topic();
  escalation_ladder();
  inventory_agrees_with_render("packet.boo");
  std::cout << "render diagnostic assertions complete\n";
  return 0;
}
