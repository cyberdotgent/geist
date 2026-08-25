#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const auto document = geist::BooDocument::open(root / "QS3X36CM.BOO");

  const auto& directory = document.directory();
  require(directory.content_page_index_offset == 0x0e82,
          "unexpected content-page index offset");
  require(directory.logical_record_count == 0x00f1,
          "unexpected logical-record count");
  require(directory.stream_table_offset == 0x0068,
          "unexpected topic-start index offset");
  require(directory.stream_table_count == 10,
          "unexpected topic count");
  require(document.topics().size() == 10,
          "lightweight topic index has the wrong size");

  const auto* intro = document.find_toc_entry("1.0");
  require(intro != nullptr, "missing 1.0 TOC entry");
  require(intro->raw_records.empty(),
          "opening eagerly rendered a TOC topic body");
  const auto markdown = intro->markdown();
  require(markdown.find("# 1.0 Introduction") != std::string::npos,
          "lazy TOC entry rendering produced unexpected Markdown");

  const auto packet = geist::BooDocument::open(root / "packet.boo");
  const auto index = packet.topic_markdown("INDEX");
  require(index.find("## A") != std::string::npos,
          "direct lazy topic rendering lost the generated index");

  // GG24-4302 stores the SH boundary for 2.6 separately from its CTopicN,
  // CHdLevel, and ST metadata.  The boundary must still resolve the TOC
  // entry to the correct topic body instead of extending 2.5 through 2.6.
  const auto split_header =
      geist::BooDocument::open(root / "GG24-4302-00.boo");
  const auto* discontinued = split_header.find_toc_entry("2.6");
  require(discontinued != nullptr, "missing split-header 2.6 TOC entry");
  require(discontinued->raw_records.empty(),
          "opening eagerly rendered split-header topic body");
  const auto discontinued_markdown = split_header.topic_markdown("2.6");
  require(discontinued_markdown.find("## 2.6 Discontinued Support") !=
              std::string::npos,
          "split-header topic lost its TOC heading");
  require(discontinued_markdown.find("LU6.1 adapter") != std::string::npos,
          "split-header topic lost its body");

  // Only the directory-declared content run contains topic logical records.
  // GG24 has later class-0x0001 pages belonging to another stream; decoding
  // those pages used to extend the final COMMENTS topic by 1,304 junk records.
  require(split_header.decoded_logical_records().size() ==
              split_header.directory().logical_record_count,
          "decoder included logical records outside the content run");
  const auto* comments = split_header.find_toc_entry("COMMENTS");
  require(comments != nullptr, "missing COMMENTS TOC entry");
  require(comments->end_logical_record - comments->start_logical_record == 4,
          "COMMENTS topic absorbed a non-content logical-record stream");
  const auto comments_markdown = comments->markdown();
  require(comments_markdown.size() < 5000,
          "COMMENTS topic expanded into fabricated structures");
  require(comments_markdown.find("questionnaire") != std::string::npos &&
              comments_markdown.find("QUALITY @ WTSCPOK") !=
                  std::string::npos,
          "COMMENTS topic lost its questionnaire body");

  const struct {
    const char* topic;
    const char* phrase;
  } heading_body_cases[] = {
      {"5.0", "This chapter describes the IMS Version 5.1 enhancements"},
      {"5.1.1", "RSR requires the services of DBRC"},
      {"7.0", "those enhancements that do not fall into any of the previous"},
      {"8.0", "This chapter brings IMS V5.1 into true perspective"},
      {"10.1", "Before IMS 5.1, CICS users had the choice"},
      {"11.0", "The ability to access common data from many systems"},
  };
  for (const auto& regression : heading_body_cases) {
    const auto topic = split_header.topic_markdown(regression.topic);
    require(topic.find(regression.phrase) != std::string::npos,
            "heading-attached topic prose was discarded");
  }

  const auto dbctl_table = split_header.topic_markdown("10.2");
  require(dbctl_table.find(
              "| Processing Cost Enhancements | N-way data sharing | ** |") !=
              std::string::npos,
          "explicit table row boundaries were merged");
  require(dbctl_table.find("| Dropped Local DL/1 support |  |  |") !=
              std::string::npos,
          "final explicit table rows were discarded");

  const auto exit_table = split_header.topic_markdown("3.6.2");
  for (const auto* expected : {"DFSERA30", "DFSERA40", "DFSERA50",
                               "DFSERA60", "DFSERA70", "DSHRDSSN",
                               "UNDO  PHYSICAL REPLACE"}) {
    require(exit_table.find(expected) != std::string::npos,
            "table or fixed report lost a representative field");
  }

  const auto image_figure = split_header.topic_markdown("5.1.8");
  require(image_figure.find("![Resource 9](resource:9)") != std::string::npos,
          "image-backed figure lost its resource");
  require(image_figure.find("```text") == std::string::npos,
          "image-backed figure retained its duplicate ASCII placeholder");
  const auto delayed_image = split_header.topic_markdown("8.5.3");
  require(delayed_image.find("![Resource 25](resource:25)") !=
              std::string::npos,
          "picture selector without inline display text lost its resource");
  require(delayed_image.find("```text") == std::string::npos,
          "picture selector retained its duplicate ASCII placeholder");

  const auto rmf_reports = split_header.topic_markdown("3.3.4");
  for (const auto* expected : {"END/SEC   12.03", "ENDED     10826",
                               "RESPONSE TIME BREAKDOWN",
                               "INTERVAL 14.59.579", "RESOURCE GROUP=*NONE",
                               "Figure 13."}) {
    require(rmf_reports.find(expected) != std::string::npos,
            "fixed report lost a representative row");
  }

  for (const auto& entry : split_header.table_of_contents()) {
    const auto markdown = entry.markdown();
    for (const auto* leaked : {"c.cc ", "cmenu", "cmitem", "cemenu",
                               "cfont ", "ctopicn", "cparent",
                               "cforwardlevel", "cbacklevel", "csummary",
                               "chdlevel", "csourcefn", ":H3", ":H4"}) {
      require(markdown.find(leaked) == std::string::npos,
              "generated BookManager control leaked into Markdown");
    }
  }
  const auto preface = split_header.topic_markdown("PREFACE");
  require(preface.find("implementing an IMS system") != std::string::npos &&
              preface.find("PREFACE.4 Acknowledgments") != std::string::npos,
          "control suppression discarded preface prose or menu entries");
  require(preface.find("```text") == std::string::npos,
          "generated menu prose remained in an unintended code fence");
}
