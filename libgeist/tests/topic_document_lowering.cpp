#include "geist/detail/topic_document_lowering.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/document.hpp"
#include "geist/trace.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string &message) {
  if (!condition)
    std::cerr << "topic_document_lowering: " << message << '\n';
  return condition;
}

geist::detail::TopicIdentityIR identity() {
  geist::detail::TopicIdentityIR result;
  result.id = "TOPIC";
  result.title = "Topic title";
  result.start_logical_record = 10;
  result.end_logical_record = 11;
  return result;
}

} // namespace

int main() {
  using namespace geist::detail;

  std::string rejection;
  const auto nonmatch =
      try_lower_topic_to_document_ir(identity(), {}, nullptr, &rejection);
  if (!require(!nonmatch, "source-free topic was admitted as typed") ||
      !require(rejection.empty(), "non-match was reported as a rejection"))
    return 1;

#ifdef GEIST_REPO_ROOT
  const auto document = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC31-711.boo");
  const auto *back_entry = document.find_toc_entry("BACK_2");
  if (!require(back_entry != nullptr, "BACK_2 is absent from the public TOC"))
    return 1;
  const auto back_gml_before = back_entry->gml_records();
  const auto back = back_entry->markdown();
  const auto back_again = back_entry->markdown();
  const auto back_gml_after = back_entry->gml_records();
  const auto comments = document.topic_markdown("COMMENTS");
  const auto *publication_entry = document.find_toc_entry("BACK_1.1");
  if (!require(publication_entry != nullptr,
               "publication is absent from the public TOC"))
    return 1;
  const auto publication_gml_before = publication_entry->gml_records();
  const auto publication = publication_entry->markdown();
  const auto publication_again = publication_entry->markdown();
  const auto publication_gml_after = publication_entry->gml_records();
  const auto *ordinary_entry = document.find_toc_entry("2.1");
  if (!require(ordinary_entry != nullptr, "ordinary topic is absent from TOC"))
    return 1;
  const auto ordinary = ordinary_entry->markdown();
  const auto ordinary_again = ordinary_entry->markdown();

  const auto itp = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "ITPPIBOK.BOO");
  const auto *communications_entry = itp.find_toc_entry("2.1.2");
  const auto *results_entry = itp.find_toc_entry("4.1.2");
  const auto *partial_prose_entry = itp.find_toc_entry("2.4.2.2");
  if (!require(communications_entry != nullptr && results_entry != nullptr &&
                   partial_prose_entry != nullptr,
               "fixed prose production fixtures are absent from the TOC"))
    return 1;
  const auto communications_gml_before = communications_entry->gml_records();
  const auto communications = communications_entry->markdown();
  const auto communications_again = communications_entry->markdown();
  const auto communications_gml_after = communications_entry->gml_records();
  const auto results_gml_before = results_entry->gml_records();
  const auto results = results_entry->markdown();
  const auto results_again = results_entry->markdown();
  const auto results_gml_after = results_entry->gml_records();
  const auto partial_prose = partial_prose_entry->markdown();

  const geist::TopicInfo *direct_topic = nullptr;
  for (const auto &topic : document.topics()) {
    if (document.find_toc_entry(topic.id) == nullptr) {
      direct_topic = &topic;
      break;
    }
  }
  if (!require(direct_topic != nullptr, "fixture has no non-TOC topic") ||
      !require(!document.topic_markdown(direct_topic->id).empty(),
               "direct non-TOC loader route returned empty Markdown"))
    return 1;

  if (!require(back.rfind("# BACK\\_2 ", 0) == 0,
               "typed delivery heading lost public topic identity") ||
      !require(back == back_again,
               "repeated typed Markdown rendering was unstable") ||
      !require(back_gml_before == back_gml_after && !back_gml_after.empty(),
               "typed rendering changed or discarded public GML records") ||
      !require(comments.rfind("# COMMENTS ", 0) == 0,
               "typed questionnaire heading lost public topic identity") ||
      !require(publication.rfind("## BACK\\_1\\.1 ", 0) == 0,
               "typed publication heading lost public topic identity") ||
      !require(publication == publication_again,
               "repeated typed publication rendering was unstable") ||
      !require(publication_gml_before == publication_gml_after &&
                   !publication_gml_after.empty(),
               "typed publication changed or discarded public GML records") ||
      !require(publication.find(
                   "Getting Started with LAN Network Manager for AIX "
                   "\\(SC31\\-7109\\)") != std::string::npos,
               "typed publication content was not rendered") ||
      !require(publication.find("<B>") == std::string::npos,
               "typed publication retained raw HTML") ||
      !require(comments.find("<a id=\"TBLUNIQ8\"></a>") !=
                   std::string::npos &&
                   comments.find("<a id=\"TBLTBLUNIQ9\"></a>") !=
                       std::string::npos,
               "typed questionnaire lost stable table anchors") ||
      !require(!ordinary.empty() && ordinary.front() == '#',
               "typed non-match did not retain legacy topic rendering") ||
      !require(ordinary == ordinary_again,
               "repeated ordinary fallback rendering was unstable") ||
      !require(communications.rfind("### 2\\.1\\.2 ", 0) == 0 &&
                   communications.find("Communications Controller "
                                       "Requirements") != std::string::npos &&
                   communications.find("TPNS CHEAPP \\(Channel end\\)") !=
                       std::string::npos,
               "unanchored fixed prose did not use typed Markdown") ||
      !require(results.rfind("### 4\\.1\\.2 Expected Results", 0) == 0 &&
                   results.find("<a id=\"HDRPLNEXR\"></a>") !=
                       std::string::npos &&
                   results.find("60 logon requests") != std::string::npos,
               "anchored fixed prose did not use typed Markdown") ||
      !require(communications == communications_again &&
                   results == results_again,
               "repeated typed fixed prose rendering was unstable") ||
      !require(communications_gml_before == communications_gml_after &&
                   !communications_gml_after.empty() &&
                   results_gml_before == results_gml_after &&
                   !results_gml_after.empty(),
               "typed fixed prose changed or discarded public GML records") ||
      !require(partial_prose.find("`TPNSSID1 APPL") != std::string::npos &&
                   partial_prose.find("<BOOK>") == std::string::npos,
               "prose topic lost its example phrase or leaked a marker") ||
      !require(back.find("If you prefer to send comments by mail") !=
                   std::string::npos,
               "typed delivery content was not rendered") ||
      !require(comments.find("the information in this book?") !=
                   std::string::npos,
               "typed questionnaire lost semantic punctuation"))
    return 1;

  // ---------------------------------------------------------------------
  // Render-trace provenance: a rendered word resolves to the node that
  // produced it, and that node's slice names the BOO file bytes which decode
  // back to the same word.  One pin per typed family, byte offsets included,
  // so a lowering that stops naming its source is caught here.
  struct TracePin {
    const char *topic;
    const char *word;         // rendered text to look for
    const char *node_path;    // producing node
    const char *reason;       // trace class of the run
    std::uint32_t logical_record;
    std::uint32_t byte_begin; // BOO file offset of the slice
    std::uint32_t byte_end;
    const char *source_text;  // what those bytes decode to
  };
  const TracePin pins[] = {
      // prose heading
      {"FRONT_1.1", "Trademarks", "block[0]/inline[1]", "text", 10, 43576,
       43578, "Trademarks"},
      // fixed table cell inside a prose topic
      {"FRONT_1.1", "NetView", "block[5]/row[0]/cell[1]/inline[0]",
       "text", 10, 43676, 43677, "NetView"},
      // generated navigation heading
      {"CONTENTS", "Table of Contents", "block[0]/inline[1]", "text", 6,
       42060, 42068, " ST TableofContents"},
      // trap catalog heading
      {"4.1.1", "Generic Traps", "block[0]/inline[1]", "text", 99, 67228,
       67234, " GenericTraps"},
      // comment delivery title
      {"BACK_2", "Communicating Your Comments to IBM", "block[0]/inline[1]",
       "text", 541, 215672, 215680, "CommunicatingYourCommentstoIBM"},
  };
  for (const auto &pin : pins) {
    const auto *entry = document.find_toc_entry(pin.topic);
    if (!require(entry != nullptr,
                 std::string("traced topic is absent: ") + pin.topic))
      return 1;
    geist::RenderTrace trace;
    const auto rendered = entry->markdown(trace);
    if (!require(!trace.spans.empty(),
                 std::string("no render trace for ") + pin.topic))
      return 1;
    // The trace covers every rendered byte exactly once, in order.
    std::size_t cursor = 0;
    for (const auto &span : trace.spans) {
      if (!require(span.output_begin == cursor && span.output_end > cursor,
                   std::string("render trace is not contiguous in ") +
                       pin.topic))
        return 1;
      cursor = span.output_end;
    }
    if (!require(cursor == rendered.size(),
                 std::string("render trace does not cover ") + pin.topic))
      return 1;

    const auto at = rendered.find(pin.word);
    if (!require(at != std::string::npos,
                 std::string("traced word is absent from ") + pin.topic))
      return 1;
    const auto *span = trace.span_at(at);
    if (!require(span != nullptr,
                 std::string("traced word resolves to no span in ") +
                     pin.topic) ||
        !require(span->role == geist::RenderTraceRole::content,
                 std::string("traced word is not content in ") + pin.topic) ||
        !require(span->node_path == pin.node_path,
                 std::string("traced word came from ") + span->node_path +
                     " not " + pin.node_path + " in " + pin.topic) ||
        !require(span->reason == pin.reason,
                 std::string("traced word class is ") + span->reason + " in " +
                     pin.topic) ||
        !require(span->slices.size() == 1,
                 std::string("traced word names ") +
                     std::to_string(span->slices.size()) +
                     " slices in " + pin.topic))
      return 1;
    const auto &slice = span->slices.front();
    if (!require(slice.logical_record == pin.logical_record &&
                     slice.byte_begin == pin.byte_begin &&
                     slice.byte_end == pin.byte_end,
                 std::string("traced word names lr") +
                     std::to_string(slice.logical_record) + " bytes " +
                     std::to_string(slice.byte_begin) + ":" +
                     std::to_string(slice.byte_end) + " in " + pin.topic))
      return 1;
    // Read those bytes back out of the BOO file and decode them again.
    if (!require(document.decode_trace_slice(slice) == pin.source_text,
                 std::string("BOO bytes of the traced word decode to '") +
                     document.decode_trace_slice(slice) + "' in " +
                     pin.topic))
      return 1;
  }
#endif

  return 0;
}
