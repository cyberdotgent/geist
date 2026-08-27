#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "message_production: " << message << '\n';
    std::exit(1);
  }
}

std::size_t count(const std::string &text, const std::string &needle) {
  auto result = std::size_t{};
  auto offset = std::size_t{};
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    ++result;
    offset += needle.size();
  }
  return result;
}

void require_ordered(const std::string &text,
                     const std::vector<std::string> &needles,
                     const std::string &message) {
  auto offset = std::size_t{};
  for (const auto &needle : needles) {
    const auto found = text.find(needle, offset);
    require(found != std::string::npos, message + ": " + needle);
    offset = found + needle.size();
  }
}

} // namespace

int main() {
#ifdef GEIST_REPO_ROOT
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const auto document = geist::BooDocument::open(root / "SC31-711.boo");
  const auto *entry = document.find_toc_entry("5.0");
  require(entry != nullptr, "message catalog is not a TOC topic");
  const auto gml_before = entry->gml_records();

  const auto markdown = document.topic_markdown("5.0");
  const auto repeated = document.topic_markdown("5.0");
  require(markdown == repeated, "repeated typed rendering changed");
  require(entry->gml_records() == gml_before && !gml_before.empty(),
          "typed rendering changed public GML");

  require(count(markdown, "<a id=\"") == 398,
          "message source-anchor inventory changed");
  require(count(markdown, "\n*Meaning:*") == 396 &&
              count(markdown, "\n*Action:*") == 396,
          "typed Meaning/Action boundary inventory changed");
  require(count(markdown, "](<#HDRPROBS>)") == 2 &&
              markdown.find("[Chapter 2, \"Problem](<#HDRPROBS>) "
                            "[Determination\" in topic 2\\.0](<#HDRPROBS>)") !=
                  std::string::npos,
          "introduction lost its two independent typed HDRPROBS links");

  const auto first_message = markdown.find("<a id=\"MSG 023\"></a>");
  require(first_message != std::string::npos,
          "first numeric message anchor is absent");
  const auto introduction = markdown.substr(0, first_message);
  // The source header contributes three blocks (two anchors and one heading),
  // followed by exactly five semantic prose blocks before MSG 023.
  require(count(introduction, "\n\n") == 8,
          "message introduction is not exactly five paragraphs");
  require_ordered(
      introduction,
      {"<a id=\"MSG\"></a>", "<a id=\"HDRMSGS\"></a>",
       "# 5\\.0 Chapter 5\\. Messages",
       "This chapter lists the LNM for AIX messages",
       "Messages with numbers between 1000 and 1999",
       "LNM for AIX appends a \"1\"",
       "You can determine the process that generates a message",
       "If you receive a message and are not able to find the message"},
      "message header or five-paragraph introduction is not source ordered");

  const auto message_072 = markdown.find("<a id=\"MSG 072\"></a>");
  const auto message_101 = markdown.find("<a id=\"MSG 101\"></a>");
  require(message_072 != std::string::npos &&
              message_101 != std::string::npos && message_072 < message_101,
          "recovered message 072 envelope is absent");
  const auto recovered_072 =
      markdown.substr(message_072, message_101 - message_072);
  require(recovered_072.find(
              "If this error becomes critical, the application will issue "
              "error messages that may be used to recover") !=
              std::string::npos,
          "message 072 lost its record-continuation Action text");

  const auto message_203 = markdown.find("<a id=\"MSG 203\"></a>");
  const auto message_204 = markdown.find("<a id=\"MSG 204\"></a>");
  require(message_203 != std::string::npos &&
              message_204 != std::string::npos && message_203 < message_204,
          "message 203 envelope is absent");
  const auto recovered_203 =
      markdown.substr(message_203, message_204 - message_203);
  require(recovered_203.find(
              "After exiting the AIX NetView/6000 graphical interface, stop "
              "LNM for AIX\\. Then execute ovstop followed by ovstart\\. Use "
              "ovstatus to verify the AIX NetView/6000 daemons are running\\. "
              "Restart LNM for AIX\\.") != std::string::npos,
          "message 203 lost its complete source-owned Action continuation");

  const auto message_218 = markdown.find("<a id=\"MSG 218\"></a>");
  const auto message_219 = markdown.find("<a id=\"MSG 219\"></a>");
  require(message_218 != std::string::npos &&
              message_219 != std::string::npos && message_218 < message_219,
          "message 218 envelope is absent");
  const auto recovered_218 =
      markdown.substr(message_218, message_219 - message_218);
  require(recovered_218.find("*Action:* Refer to the man page for usage\\.") !=
              std::string::npos,
          "message 218 lost its local pre-SRMSG Action payload");

  for (const auto *expected : {
           "The ProcessID indicates which application encountered",
           "Consequently, the currently open windows may no longer correspond",
           "a shutdown has been issued, it will be normal",
           "the bridge cannot be discovered",
           "matching entry in the SR port table or the TP port table",
           "2 \\- No such name 3 \\- Bad value",
           "Restart the Concentrator view",
           "concentrator view is set to unknown",
           "has been removed from the database",
           "This message usually indicates that the other process failed",
       })
    require(markdown.find(expected) != std::string::npos,
            std::string("message catalog lost source prose: ") + expected);
  for (const auto *artifact : {
           "LNM ? for AIX",
           "LNM - for AIX",
           "LNM \\- for AIX",
           "- Action",
           "\\- Action",
           "AN Action",
           "Restart the a Concentrator",
           "view agent is",
           "removed by from",
           "iubd<",
           "./*",
           "Care and should",
           "execute\\) ovstop",
           "\\< available",
           "No such name value",
       })
    require(markdown.find(artifact) == std::string::npos,
            std::string("message catalog retained a layout artifact: ") +
                artifact);

  require_ordered(markdown,
                  {"<a id=\"MSG 2108\"></a>", "9\\. EZVDGapplication",
                   "10\\. EZVDGagent", "<a id=\"MSG 2109\"></a>"},
                  "message 2108 lost or reordered its numeric cases");
  require_ordered(markdown,
                  {"<a id=\"MSG 2503\"></a>", "following command",
                   "core image", "od \\-c core", "Record the information",
                   "<a id=\"MSG 2504\"></a>"},
                  "message 2503 recovered fields are not source ordered");

  const auto terminal = markdown.find("<a id=\"MSG 2505\"></a>");
  require(terminal != std::string::npos &&
              markdown.find("*Meaning:* The lnmhubint received a second start "
                            "message",
                            terminal) != std::string::npos &&
              markdown.find("*Action:* None", terminal) != std::string::npos,
          "terminal message 2505 lost its Meaning or Action content");

  const auto legacy = document.topic_markdown("2.1");
  require(legacy.find("Subtopics:") != std::string::npos &&
              legacy.find("](#2.1.1)") != std::string::npos,
          "non-message topic lost the legacy fallback route");
#endif
}
