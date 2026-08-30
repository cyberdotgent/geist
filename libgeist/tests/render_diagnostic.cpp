// Render provenance (issue #58).
//
// Every rendered topic carries a typed diagnostic saying how well it was
// rendered and why.  This test pins the four severities that occur in the
// corpus against real fixtures, pins the synthetic ladder below them, and
// proves that the diagnostic and the `bootrace --coverage` metric describe
// every topic of a whole book identically -- they read the same
// RenderDiagnostic, and this is the assertion that keeps it that way.

#include "geist/detail/render_diagnostic_ir.hpp"
#include "geist/detail/typed_route_inventory.hpp"
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
  return std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / name;
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
  const auto document = geist::BooDocument::open(book("SC31-711.boo"));
  const auto &entry = topic(document, "2.1.1");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::typed,
          "SC31-711 2.1.1 should be typed, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.route == "typed", "SC31-711 2.1.1 route should be typed");
  require(diagnostic.family == "prose",
          "SC31-711 2.1.1 family should be prose, is " + diagnostic.family);
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

// One of the topics the typed dispatcher declines reports `legacy-fallback`
// carrying the exact typed rejection `bootrace --coverage` prints.
void legacy_fallback_topic() {
  const auto document = geist::BooDocument::open(book("SC31-711.boo"));
  const auto &entry = topic(document, "PREFACE.2");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::legacy_fallback,
          "SC31-711 PREFACE.2 should be legacy-fallback, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.route == "legacy", "PREFACE.2 route should be legacy");
  require(diagnostic.reason == "typed-lowering-declined",
          "PREFACE.2 reason code, is " + diagnostic.reason);
  require(diagnostic.detail ==
              "prose topic rejected: text segment begins with control-like "
              "word 'SRHDRAIXHIGH' in record 14",
          "PREFACE.2 carries its real typed rejection, is: " +
              diagnostic.detail);
  const auto markdown = entry.markdown();
  require(contains(markdown, "<!-- geist-render: severity=legacy-fallback"),
          "a legacy-routed topic's Markdown opens with the marker");
  require(contains(markdown, "SRHDRAIXHIGH"),
          "the marker carries the rejection into the file");
}

// A typed topic whose message section could not prove its structure reports
// `typed-degraded` and names the block, with the message family's own
// fallback_reason surfaced unchanged.
void message_preformatted_fallback_topic() {
  const auto document = geist::BooDocument::open(book("SC31-711.boo"));
  const auto &entry = topic(document, "5.0");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::typed_degraded,
          "SC31-711 5.0 should be typed-degraded, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.route == "typed", "5.0 still takes the typed route");
  require(diagnostic.family == "message catalog",
          "5.0 family should be the message catalog, is " + diagnostic.family);
  require(diagnostic.degradations.size() == 1,
          "5.0 has exactly one degraded block, has " +
              std::to_string(diagnostic.degradations.size()));
  if (diagnostic.degradations.size() == 1) {
    const auto &degradation = diagnostic.degradations.front();
    require(degradation.reason == "message-preformatted-fallback",
            "5.0 degradation code, is " + degradation.reason);
    require(degradation.detail ==
                "table candidate has an unpositioned source continuation",
            "5.0 surfaces the message family's own fallback_reason, is: " +
                degradation.detail);
    require(degradation.source.logical_record != 0,
            "the degraded block names its source record");
  }
  require(contains(entry.markdown(),
                   "degraded=message-preformatted-fallback"),
          "the marker names the degraded block");
}

// A verbatim SRTBL region does NOT degrade its topic.  The BOO file stores
// character art, not a grid -- `SRTBLDBCTL51` (GG24-4302-00 10.2) is an object
// id followed by a 120-character box-rule run, a caption and more rule runs,
// with no column definitions and no cell boundaries -- and the hosted
// BookServer reproduces every such region verbatim inside `<pre>`, emitting no
// `<table>` element anywhere on this corpus except where the source itself
// declares `cz OFF TABLE`.  So reproducing the art line for line *equals* the
// reference renderer: nothing the source contains is lost, and degradation is
// reserved for real loss.  SC31-711 2.4.1 is a drawn problem-determination
// form; hosted (DT 19941010174546) serves it as `<pre>` box art.
void verbatim_table_region_topic() {
  const auto document = geist::BooDocument::open(book("SC31-711.boo"));
  const auto &entry = topic(document, "2.4.1");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::typed,
          "SC31-711 2.4.1 should be typed, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.degradations.empty(),
          "2.4.1 must report no degradation for its verbatim SRTBL region");
  require(entry.markdown().find("<!--") == std::string::npos,
          "a clean topic carries no render-diagnostic comment");
}

// Fail-closed must never mean withholding content.  SC26-457 FRONT_2.1.1 is a
// record whose topic envelope does not parse: both routes produced a heading
// and nothing else, and its words used to disappear silently.  It now exits
// as `best-effort` carrying its own display lines.
void best_effort_topic() {
  const auto document = geist::BooDocument::open(book("SC26-457.boo"));
  const auto &entry = topic(document, "FRONT_2.1.1");
  const auto &diagnostic = entry.render_diagnostic();
  require(diagnostic.severity == geist::RenderSeverity::best_effort,
          "SC26-457 FRONT_2.1.1 should be best-effort, is " +
              std::string(geist::to_string(diagnostic.severity)));
  require(diagnostic.route == "best-effort", "route should be best-effort");
  require(diagnostic.reason == "no-structured-content",
          "reason code, is " + diagnostic.reason);
  require(contains(diagnostic.detail,
                   "first record lacks the topic metadata envelope"),
          "the declining route's own reason is kept, is: " +
              diagnostic.detail);
  const auto markdown = entry.markdown();
  require(contains(markdown, "A new command, CANCEL, has been added."),
          "the verbatim route emits the topic's own words");
  require(contains(markdown, "```text"),
          "the verbatim route emits them as preformatted content");
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
  require(declined.severity == geist::RenderSeverity::legacy_fallback,
          "a declined lowering classifies as legacy-fallback");

  auto with_content = declined;
  geist::detail::escalate_render_diagnostic(with_content, true, true, true);
  require(with_content.severity == geist::RenderSeverity::legacy_fallback,
          "a route that produced content is not escalated");

  auto verbatim = declined;
  geist::detail::escalate_render_diagnostic(verbatim, false, true, true);
  require(verbatim.severity == geist::RenderSeverity::best_effort,
          "no content plus recoverable lines is best-effort");

  auto empty_body = declined;
  geist::detail::escalate_render_diagnostic(empty_body, false, false, true);
  require(empty_body.severity == geist::RenderSeverity::legacy_fallback,
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
  legacy_fallback_topic();
  message_preformatted_fallback_topic();
  verbatim_table_region_topic();
  best_effort_topic();
  escalation_ladder();
  inventory_agrees_with_render("SC31-711.boo");
  inventory_agrees_with_render("SC26-457.boo");
  std::cout << "render diagnostic assertions complete\n";
  return 0;
}
