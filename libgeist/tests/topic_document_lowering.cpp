#include "geist/detail/topic_document_lowering.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/document.hpp"

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
      try_lower_topic_to_document_ir(identity(), {}, &rejection);
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
  const auto *ordinary_entry = document.find_toc_entry("2.1");
  if (!require(ordinary_entry != nullptr, "ordinary topic is absent from TOC"))
    return 1;
  const auto ordinary = ordinary_entry->markdown();
  const auto ordinary_again = ordinary_entry->markdown();

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
      !require(comments.find("<a id=\"TBLUNIQ8\"></a>") !=
                   std::string::npos &&
                   comments.find("<a id=\"TBLTBLUNIQ9\"></a>") !=
                       std::string::npos,
               "typed questionnaire lost stable table anchors") ||
      !require(!ordinary.empty() && ordinary.front() == '#',
               "typed non-match did not retain legacy topic rendering") ||
      !require(ordinary == ordinary_again,
               "repeated ordinary fallback rendering was unstable") ||
      !require(back.find("If you prefer to send comments by mail") !=
                   std::string::npos,
               "typed delivery content was not rendered") ||
      !require(comments.find("the information in this book?") !=
                   std::string::npos,
               "typed questionnaire lost semantic punctuation"))
    return 1;
#endif

  return 0;
}
