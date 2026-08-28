#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

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
  require(markdown.find("# 1\\.0 Introduction") != std::string::npos,
          "lazy TOC entry rendering produced unexpected Markdown");

  const auto packet = geist::BooDocument::open(root / "packet.boo");
  const auto index = packet.topic_markdown("INDEX");
  require(index.find("## A") != std::string::npos,
          "direct lazy topic rendering lost the generated index");
}
