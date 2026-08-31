// Topic dispatch and render-trace provenance.
//
// The typed dispatcher must decline cleanly on a source-free topic, render
// stable Markdown for the families it accepts, and name the BOO file bytes
// behind every rendered word.  The families pinned here are the ones packet
// carries: prose, generated navigation, generated list, and a declared table
// inside a prose topic.  The comment-delivery, publication, trap-catalog and
// fixed-prose pins went with the books that cannot be published (issue #59).

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

#ifdef GEIST_FIXTURE_DIR
  const auto document = geist::BooDocument::open(
      std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo");

  const auto *contents_entry = document.find_toc_entry("CONTENTS");
  const auto *figures_entry = document.find_toc_entry("FIGURES");
  const auto *table_entry = document.find_toc_entry("2.4.4");
  const auto *ordinary_entry = document.find_toc_entry("2.1");
  if (!require(contents_entry != nullptr && figures_entry != nullptr &&
                   table_entry != nullptr && ordinary_entry != nullptr,
               "production fixtures are absent from the public TOC"))
    return 1;

  const auto contents = contents_entry->markdown();
  const auto contents_again = contents_entry->markdown();
  const auto figures = figures_entry->markdown();
  const auto figures_again = figures_entry->markdown();
  const auto table = table_entry->markdown();
  const auto table_again = table_entry->markdown();
  const auto ordinary = ordinary_entry->markdown();
  const auto ordinary_again = ordinary_entry->markdown();

  // Every topic id in the lightweight index reaches the loader, including any
  // that the public TOC does not list.
  for (const auto &topic : document.topics()) {
    if (!require(!document.topic_markdown(topic.id).empty(),
                 "direct topic loader route returned empty Markdown for " +
                     topic.id))
      return 1;
  }

  if (!require(contents.rfind("# CONTENTS ", 0) == 0,
               "generated navigation heading lost public topic identity") ||
      !require(contents == contents_again,
               "repeated generated navigation rendering was unstable") ||
      !require(contents.find("`1.0` [An Introduction to Packet Radio](<#1.0>)")
                   != std::string::npos,
               "generated navigation content was not rendered") ||
      !require(contents.find("<B>") == std::string::npos,
               "generated navigation retained raw HTML") ||
      !require(figures.rfind("# FIGURES ", 0) == 0,
               "generated list heading lost public topic identity") ||
      !require(figures == figures_again,
               "repeated generated list rendering was unstable") ||
      !require(figures.find("LoRa Frame Format") != std::string::npos,
               "generated list content was not rendered") ||
      !require(table.rfind("##### 2\\.4\\.4 Address Classes", 0) == 0,
               "typed table topic heading lost public topic identity") ||
      !require(table == table_again,
               "repeated typed table rendering was unstable") ||
      !require(table.find("<a id=\"TBLTBLUNIQ17\"></a>") != std::string::npos,
               "typed table lost its stable anchor") ||
      !require(table.find("| Class | Range | Default Netmask |") !=
                   std::string::npos,
               "typed table lost its header row") ||
      !require(!ordinary.empty() && ordinary.front() == '#',
               "typed prose topic did not render a heading") ||
      !require(ordinary == ordinary_again,
               "repeated prose rendering was unstable") ||
      !require(ordinary.find("<BOOK>") == std::string::npos,
               "prose topic leaked a selector alternative"))
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
    std::size_t slice_count;  // how many source slices back the run
    std::uint32_t logical_record; // of the first slice
    std::uint32_t byte_begin; // BOO file offset of the first slice
    std::uint32_t byte_end;
    const char *source_text;  // what those bytes decode to
  };
  const TracePin pins[] = {
      // generated navigation heading
      {"CONTENTS", "Table of Contents", "block[0]/inline[1]", "text", 1, 7,
       267371, 267379, "???????????????????????ST TableofContents"},
      // prose heading
      {"2.4.4", "Address Classes", "block[0]/inline[1]", "text", 1, 65,
       286500, 286504, "AddressClasses"},
      // a cell of a declared `cz OFF TABLE` grid
      {"2.4.4", "Default Netmask", "block[4]/row[0]/cell[2]/inline[0]", "text",
       1, 65, 286670, 286674, "DefaultNetmask"},
      // generated list entry label, assembled from twelve source runs
      {"FIGURES", "LoRa Frame Format", "block[9]/inline[0]", "link label", 12,
       11, 268990, 268991, "9"},
      // a selector label broken over two display rows
      {"6.2", "Web Locations of Packet", "block[1]/inline[1]", "link label", 2,
       308, 364738, 364739, "\""},
      // a preformatted line inside a prose topic
      {"A.0", "rsync", "block[3]/line[0]", "preformatted line", 6, 364, 379674,
       379678, "$rsync-"},
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
        !require(span->slices.size() == pin.slice_count,
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
    geist::TraceSourceReader source_reader(document);
    const auto decoded = source_reader.decode(slice);
    if (!require(decoded == pin.source_text,
                 std::string("BOO bytes of the traced word decode to '") +
                     decoded + "' in " + pin.topic))
      return 1;
    // Every later slice of a multi-run label must name real bytes too.
    for (const auto &part : span->slices) {
      if (!require(part.byte_end > part.byte_begin,
                   std::string("a traced slice names an empty byte range in ") +
                       pin.topic))
        return 1;
    }
  }
#endif

  return 0;
}
