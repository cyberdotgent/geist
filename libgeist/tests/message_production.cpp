#include "geist/document.hpp"
#include "test_failures.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "message_production: " << message << '\n';
    geist_test::record_failure();
    return;
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
  require(count(markdown, "\n**Meaning:**") == 396 &&
              count(markdown, "\n**Action:**") == 396,
          "typed Meaning/Action boundary inventory changed");
  auto message_cursor = std::size_t{};
  auto verified_headlines = std::size_t{};
  while ((message_cursor = markdown.find("<a id=\"MSG ", message_cursor)) !=
         std::string::npos) {
    const auto id_begin = message_cursor + std::string{"<a id=\"MSG "}.size();
    const auto id_end = markdown.find("\"", id_begin);
    require(id_end != std::string::npos,
            "message anchor has no terminating quote");
    const auto id = markdown.substr(id_begin, id_end - id_begin);
    const auto headline_begin = markdown.find("\n\n**", id_end);
    const auto headline_end = headline_begin == std::string::npos
                                  ? std::string::npos
                                  : markdown.find("**\n", headline_begin + 4);
    require(headline_begin != std::string::npos &&
                headline_end != std::string::npos,
            "message anchor has no emphasized headline");
    auto rendered_id = id;
    for (auto dash = rendered_id.find('-'); dash != std::string::npos;
         dash = rendered_id.find('-', dash + 2))
      rendered_id.insert(dash, "\\");
    const auto headline = markdown.substr(headline_begin + 4,
                                          headline_end - headline_begin - 4);
    require(headline.rfind(rendered_id + " ", 0) == 0,
            "rendered message headline does not begin with its anchor ID: " +
                id);
    auto after_id = rendered_id.size();
    while (after_id < headline.size() && headline[after_id] == ' ')
      ++after_id;
    require(headline.compare(after_id, rendered_id.size(), rendered_id) != 0,
            "rendered message headline repeats its anchor ID: " + id);
    require(count(headline, "\\<") == count(headline, "\\>"),
            "rendered message headline has unbalanced placeholders: " + id);
    ++verified_headlines;
    message_cursor = headline_end + 2;
  }
  require(verified_headlines == 396,
          "rendered message headline inventory changed");
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
  require(recovered_218.find("**Action:** Refer to the man page for usage\\.") !=
              std::string::npos,
          "message 218 lost its local pre-SRMSG Action payload");

  for (const auto *expected : {
           "The ProcessID indicates which application encountered",
           "Consequently, the currently open windows may no longer correspond",
           "a shutdown has been issued, it will be normal",
           "the bridge cannot be discovered",
           "matching entry in the SR port table or the TP port table",
           "2 \\- No such name 3 \\- Bad value",
           "If the problem persists, contact IBM Service for more information",
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
  // Verified structured blocks: MSG807 command table, MSG739 checklist,
  // MSG508 explicit preformatted fallback (the row-less `SNMP Trap` field
  // keeps its own source-ordered line instead of a fabricated table cell).
  require_ordered(
      markdown,
      {"<a id=\"MSG 807\"></a>",
       "applications are described in the following list:\n\n"
       "| Command type | Command |\n| --- | --- |\n"
       "| 23006 | LAN ADP LIST SEG=\\<segment number\\> |\n"
       "| 11011 | LAN ADP QUERY ADP=\\<adapter address\\> SEG=\\<segment "
       "number\\> |\n",
       "| 31096 | LAN CAU QUERY UNIT=\\<unit id\\> ATTR=WRAP |\n"
       "| 31127 | LAN CAU QUERY UNIT=\\<unit id\\> MOD=\\<module number\\> |\n"
       "| 31161 | LAN CAU QUERY UNIT=\\<unit id\\> MOD=\\<module number\\> "
       "ATTR=LOBE |\n",
       "| 103000 | LAN CAUQUAL LIST |\n\n**Action:** It is possible",
       "<a id=\"MSG 808\"></a>"},
      "MSG807 command table is not rendered as a Markdown table");
  require(count(markdown, "\n| ") == 27,
          "message Markdown table row inventory changed");
  require_ordered(
      markdown,
      {"<a id=\"MSG 739\"></a>",
       "Verify that the following conditions are true:\n\n"
       "- /usr/lpp/lnm/databases contains lnmlnmemgr\\.pdf\n"
       "- /usr/lib/nls/msg/\\<lang\\> contains a symbolic link to "
       "/usr/lpp/lnm/nls/\\<lang\\>/lnmeapp\\.cat\n"
       "- /usr/lib/nls/msg/\\<lang\\> contains a symbolic link to "
       "/usr/lpp/lnm/nls/\\<lang\\>/lnmlnmemgr\\_dfi\\.cat\n\n"
       "If everything is correctly set, contact IBM Service for more "
       "information\\.\n\n<a id=\"MSG 740\"></a>"},
      "MSG739 checklist is not rendered as a Markdown list");
  require_ordered(
      markdown,
      {"<a id=\"MSG 508\"></a>",
       "**Action:**\n\n```\nApplication Action\n"
       "CP Consult the nettl log for messages associated\n"
       "with the failure of lnmd and lnmtopod daemons.\n"
       "Then restart LNM for AIX.\n"
       "Topology Verify that lnmtopod is running (see message number\n"
       "505)\nSNMP Trap\nVerify that AIX NetView/6000 is running properly\n"
       "Then restart LNM for AIX.\n"
       "lnmfddimgr Restart lnmfddimgr by selecting a FDDI object\n"
       "and requesting a window.\n```\n\n<a id=\"MSG 509\"></a>"},
      "MSG508 fallback is not rendered as one fenced block");
  require(count(markdown, "\n```\n") == 2,
          "message Markdown fenced block inventory changed");
  for (const auto *artifact : {"| action ", "| an ", "action 31096",
                               "an 31127", "an 31161"})
    require(markdown.find(artifact) == std::string::npos,
            "message table leaked a structural marker spelling");

  const auto message_2228 = markdown.find("<a id=\"MSG 2228\"></a>");
  const auto message_2237 = markdown.find("<a id=\"MSG 2237\"></a>");
  require(message_2228 != std::string::npos &&
              message_2237 != std::string::npos &&
              markdown.find("If the problem persists, contact IBM Service for "
                            "more information\\.",
                            message_2228) < message_2237,
          "message 2228 lost its structural-warning payload sentence");
  require_ordered(markdown,
                  {"<a id=\"MSG 2503\"></a>", "following command",
                   "core image", "od \\-c core", "Record the information",
                   "<a id=\"MSG 2504\"></a>"},
                  "message 2503 recovered fields are not source ordered");

  const auto terminal = markdown.find("<a id=\"MSG 2505\"></a>");
  require(terminal != std::string::npos &&
              markdown.find("**Meaning:** The lnmhubint received a second start "
                            "message",
                            terminal) != std::string::npos &&
              markdown.find("**Action:** None", terminal) != std::string::npos,
          "terminal message 2505 lost its Meaning or Action content");

  const auto legacy = document.topic_markdown("2.1");
  // 2.1 renders through the typed prose family (`<#id>` menu destinations).
  require(legacy.find("Subtopics:") != std::string::npos &&
              legacy.find("](<#2.1.1>)") != std::string::npos,
          "non-message topic lost the legacy fallback route");
#endif
}
