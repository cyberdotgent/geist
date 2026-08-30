#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto problem_determination =
      geist::BooDocument::open(root / "SC31-711.boo");
  const auto lnm_messages = problem_determination.topic_markdown("5.0");
  require(lnm_messages.find(
              "See [Chapter 2, \"Problem](<#HDRPROBS>) "
              "[Determination\" in topic 2\\.0](<#HDRPROBS>) for "
              "instructions") !=
              std::string::npos,
          "wrapped same-target selectors lost their visible link sequence");
  for (const auto* expected : {
           "<a id=\"MSG 062\"></a>",
           "<a id=\"MSG 1000-1999\"></a>",
           "<a id=\"MSG 2389\"></a>",
           "<a id=\"MSG 2502\"></a>",
           "Restart the Concentrator view application after the agent communication is reestablished",
           "The view will be set to unknown while the problem persists",
           "has been removed from the database",
           "This message usually indicates that the other process failed",
           // MSG807's listing renders verbatim, as hosted serves it.
           "```\nCommand type Command\n23006 LAN ADP LIST",
           "- /usr/lpp/lnm/databases contains lnmlnmemgr\\.pdf\n- ",
           "```\nApplication Action\nCP Consult the nettl log",
       }) {
    require(lnm_messages.find(expected) != std::string::npos,
            "LNM message catalog lost an anchor or styled row text");
  }
  for (const auto* leaked : {
           "MSG 062 ", "Meaning:action", "Meaning:and", "AN Action",
           "Restart** **the** a **Concentrator", "view** agent **is",
           "removed** by **from", "iubd<",
       }) {
    require(lnm_messages.find(leaked) == std::string::npos,
            "LNM message catalog retained row carryover or metadata");
  }
  require(substring_count(lnm_messages, "\n**Meaning:**") == 396 &&
              substring_count(lnm_messages, "\n**Action:**") == 396,
          "LNM message catalog lost typed Meaning/Action paragraph boundaries");

  const auto lnm_glossary =
      problem_determination.topic_markdown("GLOSSARY");
  for (const auto* expected : {
           "<a id=\"GLS accelerator\"></a>",
           "<a id=\"GLS managed node\"></a>",
           "<a id=\"GLS wildcard character\"></a>",
       }) {
    require(lnm_glossary.find(expected) != std::string::npos,
            "LNM glossary lost its term-specific anchor");
  }
  require(lnm_glossary.find("adapter    active") == std::string::npos &&
              lnm_glossary.find("address    inactive") ==
                  std::string::npos &&
              lnm_glossary.find("alternative to a and") ==
                  std::string::npos &&
              lnm_glossary.find("may address    be stored") ==
                  std::string::npos &&
              lnm_glossary.find(
                  "GLS Consultative Committee on International Telegraph "
                  "and Telephone (CCITT) action") == std::string::npos,
          "LNM glossary retained fixed-row carryover");
  require(lnm_glossary.find("The IBM Dictionary of Computing") !=
              std::string::npos &&
              lnm_glossary.find(
                  "This glossary includes terms and definitions from:") ==
                  lnm_glossary.rfind(
                      "This glossary includes terms and definitions from:"),
          "LNM glossary introduction was lost or duplicated");
  for (const auto* expected : {
           "\n- The American National Standard Dictionary for Information "
           "Systems, ANSI X3\\.172\\-1990",
           "\n- The ANSI/EIA Standard\\-\\-440\\-A, Fiber Optic Terminology\\.",
           "\n- The Information Technology Vocabulary, developed by "
           "Subcommittee 1",
           "\n- The Network Working Group Request for Comments: 1208\\.",
           "\n- The IBM Dictionary of Computing, New York: McGraw\\-Hill, "
           "1994\\.",
           "**Contrast with:** This refers to a term",
           "**Deprecated term for:** This indicates that the term should not "
           "be used\\.",
       }) {
    require(lnm_glossary.find(expected) != std::string::npos,
            "LNM glossary semantic introduction lost a citation or reference");
  }
  for (const auto* leaked : {
           "from:>", "definition. application", "2001 a Pennsylvania",
           "and A working", "SC1.(", "1994. The following", "glossary..",
       }) {
    require(lnm_glossary.find(leaked) == std::string::npos,
            "LNM glossary semantic introduction retained a layout token");
  }

  const auto formatted_log = problem_determination.topic_markdown("3.1");
  for (const auto* expected : {
           "Process ID : 19915 Subsystem : OVEXTERNAL",
           "User ID ( UID ) : 0 Log Class : ERROR",
           "Connection ID : -1 Log Instance : 0",
           "803 Cannot connect to LNM OS/2 Agent with internet address: "
           "9.67.164.24",
       }) {
    require(formatted_log.find(expected) != std::string::npos,
            "banner-gated all-E display lost a physical row");
  }
  require(formatted_log.find("\nAS\n") == std::string::npos &&
              formatted_log.find("Event 803") == std::string::npos,
          "banner-gated all-E display leaked or synthesized a marker");
}
