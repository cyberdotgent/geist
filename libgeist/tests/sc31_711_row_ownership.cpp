#include "geist/document.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

std::string canonical_visible_text(const std::string& markdown) {
  std::string visible;
  visible.reserve(markdown.size());
  for (std::size_t index = 0; index < markdown.size(); ++index) {
    const auto ch = markdown[index];
    if (ch == '\\' && index + 1 < markdown.size()) {
      visible.push_back(markdown[++index]);
      continue;
    }
    if (ch == '<' && index + 1 < markdown.size() &&
        (std::isalpha(static_cast<unsigned char>(markdown[index + 1])) != 0 ||
         markdown[index + 1] == '/' || markdown[index + 1] == '!')) {
      const auto end = markdown.find('>', index + 1);
      index = end == std::string::npos ? markdown.size() : end;
      if (!visible.empty() && visible.back() != ' ') {
        visible.push_back(' ');
      }
      continue;
    }
    if (ch == ']' && index + 1 < markdown.size() &&
        markdown[index + 1] == '(') {
      const auto end = markdown.find(')', index + 2);
      if (end != std::string::npos) {
        index = end;
      }
      continue;
    }
    if (ch == '*' || ch == '`' || ch == '[' || ch == ']') {
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '|') {
      if (!visible.empty() && visible.back() != ' ') {
        visible.push_back(' ');
      }
      continue;
    }
    visible.push_back(ch);
  }
  while (!visible.empty() && visible.back() == ' ') {
    visible.pop_back();
  }
  return visible;
}

void require_contains(const std::string& text, const std::string& expected,
                      const char* label) {
  if (text.find(expected) != std::string::npos) {
    return;
  }
  std::cerr << "missing " << label << ": " << expected << "\n";
  ++failures;
}

void require_absent(const std::string& text, const std::string& unexpected,
                    const char* label) {
  if (text.find(unexpected) == std::string::npos) {
    return;
  }
  std::cerr << "unexpected " << label << ": " << unexpected << "\n";
  ++failures;
}

void require_once(const std::string& text, const std::string& expected,
                  const char* label) {
  const auto first = text.find(expected);
  if (first != std::string::npos &&
      text.find(expected, first + expected.size()) == std::string::npos) {
    return;
  }
  std::cerr << "expected exactly one " << label << ": " << expected << "\n";
  ++failures;
}

void require_visible_once(const std::string& markdown,
                          const std::string& expected,
                          const char* label) {
  require_once(canonical_visible_text(markdown), expected, label);
}

void require_visible_near(const std::string& markdown,
                          const std::string& left,
                          const std::string& right,
                          std::size_t maximum_distance,
                          const char* label) {
  const auto visible = canonical_visible_text(markdown);
  const auto left_at = visible.find(left);
  const auto right_at = left_at == std::string::npos
                            ? std::string::npos
                            : visible.find(right, left_at + left.size());
  if (left_at != std::string::npos && right_at != std::string::npos &&
      right_at - left_at <= maximum_distance) {
    return;
  }
  std::cerr << "missing associated " << label << ": " << left << " -> "
            << right << "\n";
  ++failures;
}

void require_same_markdown_row(const std::string& markdown,
                               const std::string& left,
                               const std::string& right,
                               const char* label) {
  for (std::size_t begin = 0; begin <= markdown.size();) {
    const auto end = markdown.find('\n', begin);
    const auto line = markdown.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!line.empty() && line.front() == '|') {
      const auto visible = canonical_visible_text(line);
      if (visible.find(left) != std::string::npos &&
          visible.find(right) != std::string::npos) {
        return;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  std::cerr << "missing same-row " << label << ": " << left << " -> "
            << right << "\n";
  ++failures;
}

} // namespace

int main() {
  const auto book = std::filesystem::path(GEIST_REPO_ROOT) / "BOO" /
                    "SC31-711.boo";
  const auto document = geist::BooDocument::open(book);

  const auto preface = document.topic_markdown("PREFACE");
  require_contains(
      preface,
      "This book is designed as a reference manual for the IBM LAN Network "
      "Manager for AIX program",
      "source-owned preface body");
  // PREFACE now renders through the typed prose family, which reflows the
  // display rows into paragraphs and escapes Markdown punctuation.  Hosted
  // (DT 19941010174546) serves the same words.
  require_contains(preface, "\\(SNMP\\)\\-based token\\-ring LAN segments",
                   "source-cleaned SNMP prose row");
  require_contains(preface, "\\(FDDI\\) segments, and SNMP\\-managed bridges",
                   "source-cleaned FDDI prose row");
  for (const auto* leaked : {"Network < Manager", "protocol a (SNMP)",
                              "interface adapter (FDDI)"}) {
    require_absent(preface, leaked, "preface source-row marker");
  }

  const auto chapter_three = document.topic_markdown("3.0");
  require_contains(chapter_three,
                   "configuration and status of your LAN, LNM for AIX",
                   "source-owned chapter introduction");
  require_contains(chapter_three,
                   "AIX NetView/6000 receives and logs all traps",
                   "source-owned second chapter paragraph");
  for (const auto* leaked : {"your < LAN", "and > agents",
                              "application.) AIX"}) {
    require_absent(chapter_three, leaked, "chapter source-row marker");
  }

  // FRONT_1 renders through the typed prose family, equal to hosted.
  const auto fixed_notices = document.topic_markdown("FRONT_1");
  require_contains(fixed_notices, "IBM Director of Licensing, IBM Corporation",
                   "notices prose");
  require_contains(fixed_notices, "IBM Director of Licensing",
                   "fixed notices address");
  require_contains(document.topic_markdown("2.1"), "nettl log",
                   "semantic diagnostic list row");

  const auto contents = document.topic_markdown("CONTENTS");
  for (const auto* expected : {
           "[Customer Information](#2.4.1)",
           "[Customer Information](#2.4.5)",
           "[Additional Problem Information](#2.4.9)",
           "[AIX Operating System Publications](#BACK_1.8)",
       }) {
    require_contains(contents, expected, "source-cleaned CONTENTS title");
  }
  for (const auto* leaked : {
           "[Customer Information /](#2.4.1)",
           "[Customer Information >](#2.4.5)",
           "[Additional Problem Information <](#2.4.9)",
           "[AIX Operating System Publications <](#BACK_1.8)",
       }) {
    require_absent(contents, leaked, "CONTENTS row marker");
  }
  const auto exact_toc_title = [&](const char* id, const char* expected) {
    const auto& toc = document.table_of_contents();
    const auto found =
        std::find_if(toc.begin(), toc.end(),
                     [&](const auto& entry) { return entry.id == id; });
    if (found == toc.end() || found->title != expected) {
      std::cerr << "unexpected parsed TOC title for " << id << '\n';
      ++failures;
    }
  };
  exact_toc_title("2.4.1", "Customer Information");
  exact_toc_title("2.4.5", "Customer Information");
  exact_toc_title("2.4.9", "Additional Problem Information");
  exact_toc_title("BACK_1.8", "AIX Operating System Publications");

  const auto worksheet_menu = document.topic_markdown("2.4");
  for (const auto* expected : {
           "[2\\.4\\.2 Software Version Levels and Applied PTFs on the LNM "
           "for AIX Workstation](<#2.4.2>)",
           "[2\\.4\\.5 Customer Information](<#2.4.5>)",
       }) {
    require_contains(worksheet_menu, expected, "verified CMITEM title");
  }
  for (const auto* leaked : {"Workstation <](#2.4.2)",
                              "Information \"](#2.4.5)"}) {
    require_absent(worksheet_menu, leaked, "CMITEM terminal source token");
  }
  const auto trap_menu = document.topic_markdown("4.1");
  require_contains(trap_menu, "[4\\.1\\.1 Generic Traps](<#4.1.1>)",
                   "verified trap CMITEM title");
  require_absent(trap_menu, "Generic Traps >](#4.1.1)",
                 "trap CMITEM terminal source token");
  const auto bibliography_menu = document.topic_markdown("BACK_1");
  require_contains(
      bibliography_menu,
      // The legacy list route escapes literal `_`/`*` in item text (the
      // typed renderer's convention, compare the `BACK\_1\.8` heading).
      "[BACK\\_1\\.8 AIX Operating System Publications](<#BACK_1.8>)",
      "verified bibliography CMITEM title");
  require_absent(bibliography_menu, "Publications can](#BACK_1.8)",
                 "bibliography CMITEM terminal source token");
  const auto menu_trace = document.trace_logical_records("2.4");
  const auto has_menu_provenance =
      std::any_of(menu_trace.begin(), menu_trace.end(), [](const auto& record) {
        return std::any_of(
            record.ir_semantic_blocks.begin(),
            record.ir_semantic_blocks.end(), [](const auto& block) {
              return block.find("target='2.4.2'") != std::string::npos &&
                     block.find("terminal_marker_encoded=0x13 width=1") !=
                         std::string::npos &&
                     block.find("marker_cells=1") != std::string::npos &&
                     block.find("terminal_marker_bytes=[0xe8ec,0xe8ed)") !=
                         std::string::npos;
            });
      });
  if (!has_menu_provenance) {
    std::cerr << "menu IR trace omitted exact terminal-token provenance\n";
    ++failures;
  }

  const auto cross_book = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC31-605.boo");
  const auto& cross_toc = cross_book.table_of_contents();
  const auto cross_entry = std::find_if(
      cross_toc.begin(), cross_toc.end(),
      [](const auto& entry) { return entry.id == "1.1"; });
  if (cross_entry == cross_toc.end() ||
      cross_entry->title != "Block ID Index") {
    std::cerr << "cross-book TOC punctuation/title regression\n";
    ++failures;
  }

  const auto directories = document.topic_markdown("1.1");
  for (const auto* expected : {
           "/usr/lpp/lnm/gifs",
           "GIF files",
           "/usr/lpp/lnm/registration",
           "Registration files",
           "/usr/lpp/lnm/reports",
           "Files related to report generation, including history files",
       }) {
    require_visible_once(directories, expected, "directory row content");
  }
  require_visible_near(directories, "/usr/lpp/lnm/gifs", "GIF files", 80,
                       "GIF directory row");
  require_visible_near(directories, "/usr/lpp/lnm/registration",
                       "Registration files", 100, "registration row");
  for (const auto* leaked : {"GIF files=", "Registration filesaddress"}) {
    require_absent(directories, leaked, "directory-row marker");
  }

  const auto netview_directories = document.topic_markdown("1.2");
  require_contains(netview_directories, "| Directory | Type of Files |",
                   "NetView directory table header");
  std::size_t netview_rows = 0;
  for (std::size_t at = netview_directories.find("\n| /usr/OV/");
       at != std::string::npos;
       at = netview_directories.find("\n| /usr/OV/", at + 1)) {
    ++netview_rows;
  }
  if (netview_rows != 9) {
    std::cerr << "unexpected NetView directory row count: " << netview_rows
              << '\n';
    ++failures;
  }
  for (const auto* leaked : {
           "Filesa /usr/OV/bitmaps/C", "OVWcan /usr/OV/conf/C",
           "OVWadapter /usr/OV/help/C/lnm", "**files**",
       }) {
    require_absent(netview_directories, leaked,
                   "NetView directory-row marker");
  }

  const auto processes = document.topic_markdown("1.3");
  require_contains(processes, "`man` `topic`\n\nWhere topic is",
                   "source-owned command/prose row boundary");
  require_visible_once(processes, "lnmhubint", "lnmhubint process");
  require_visible_near(
      processes, "lnmhubint",
      "Communicates with the Hub Manager iubd daemon to manage the LNM for "
      "AIX Hub Manager integration function.",
      180, "lnmhubint process row");
  for (const auto* leaked : {"\nagent in the", "lnmhubint/", "process.("}) {
    require_absent(processes, leaked, "process-row marker");
  }

  const auto application_problems = document.topic_markdown("2.3.1");
  require_once(application_problems, "correspond to DFI message numbers",
               "DFI explanation");
  require_once(application_problems, "A \"1\" has been appended to the DFI",
               "DFI number explanation");
  for (const auto* leaked : {"DFI > message", "DFI / message"}) {
    require_absent(application_problems, leaked, "DFI row marker");
  }

  const auto tracing = document.topic_markdown("2.1.3");
  for (const auto* boundary : {
           "following command:\n\n`ps -ef | grep lnm_process`\n\nwhere ",
           "\n\n**2\\.** Turn on tracing by entering the command:\n\n",
           "simply starts tracing for the process\\.\n\n**3\\.** Turn off "
           "tracing",
       }) {
    require_contains(tracing, boundary,
                     "source-owned tracing procedure boundary");
  }

  const auto clearing = document.topic_markdown("2.1.4");
  require_contains(clearing,
                   "**1\\.** Stop the AIX NetView/6000 graphical interface\\."
                   "\n\n**2\\.** Issue the **ovstop** command\\.\n\n"
                   "**3\\.** Issue the **ovstart** command\\.",
                   "source-owned database procedure steps");

  const auto chapter_traps = document.topic_markdown("4.0");
  require_visible_near(chapter_traps, "For information", "about:", 80,
                       "trap cross-reference table heading");
  require_contains(chapter_traps, "Read:",
                   "trap cross-reference table value heading");
  for (const auto& row : {
           std::pair{"LNM OS/2 agent traps", "in topic 4.1"},
           std::pair{"SNMP token-ring traps", "in topic 4.2"},
           std::pair{"SNMP bridge traps", "in topic 4.3"},
           std::pair{"FDDI traps", "in topic 4.4"},
       }) {
    require_visible_near(chapter_traps, row.first, row.second, 220,
                         "trap cross-reference row");
  }
  require_absent(chapter_traps, "\nAS\n", "standalone trap marker");

  const auto fddi_traps = canonical_visible_text(document.topic_markdown("4.4"));
  require_once(fddi_traps,
               "For more information about the data associated with each of "
               "these traps",
               "FDDI trap introduction");
  for (const auto* leaked : {"each of / these traps",
                             "ringInoperative cleared in segment. / All"}) {
    require_absent(fddi_traps, leaked, "FDDI row marker");
  }
  require_contains(fddi_traps,
                   "Elasticity Buffer error count has incremented during a "
                   "station's sampling period",
                   "source-owned FDDI description continuation");
  require_absent(fddi_traps, "incremented a during",
                 "compact alphabetic FDDI row marker");
  require_contains(fddi_traps, "received from a station",
                   "genuine FDDI article");

  // 2.4.4 is a ruled `SRTBL` form the table model does not resolve into
  // columns; the typed route reproduces the hosted display lines verbatim
  // (DT=19941010174546), `&ballot.` included, because that is what the
  // reader prints.
  const auto netview_form = document.topic_markdown("2.4.4");
  require_contains(netview_form,
                   "Which mode was AIX NetView/6000 operating",
                   "NetView mode question");
  require_contains(netview_form, "Read-Write", "NetView mode choice");
  require_contains(netview_form, "ovobjprint", "NetView object-count command");
  require_contains(netview_form,
                   "   | in at the time of the problem?            | "
                   "&ballot.  Read-Write       |",
                   "complete NetView mode question");
  require_contains(netview_form,
                   "   |     (use the command ovobjprint | head)   | "
                   "__________________________ |",
                   "ovobjprint command row");
  require_contains(netview_form,
                   "   |     cache                                 |"
                   "                            |",
                   "ovwdb cache row");
  require_contains(netview_form,
                   "   |     data to the GTMD database             |"
                   "                            |",
                   "GTMD row");
  require_absent(netview_form, "| a |", "standalone form marker a");
  require_absent(netview_form, "| address |",
                 "standalone form marker address");

  const auto log = document.topic_markdown("3.1");
  for (const auto* expected : {
           "Process ID", "Subsystem", "User ID", "Log Class", "Device ID",
           "Path ID", "Connection ID", "Log Instance", "Software",
           "Hostname", "803", "Cannot", "internet address: 9.67.164.24",
       }) {
    require_contains(canonical_visible_text(log), expected,
                     "formatted log field");
  }
  require_visible_near(log, "803", "Cannot connect to LNM OS/2 Agent", 100,
                       "formatted event row");
  for (const auto* leaked : {"\nAS\n", "originate.("}) {
    require_absent(log, leaked, "log-row marker");
  }

  const auto comments_to_ibm = document.topic_markdown("BACK_2");
  const auto comments_visible = canonical_visible_text(comments_to_ibm);
  require_contains(comments_visible, "Publication No. SC31-7111-00",
                   "comments publication number");
  require_contains(comments_visible, "1-800-227-5088",
                   "comments FAX number");
  require_once(comments_to_ibm,
               "If you prefer to send comments by mail",
               "comments-by-mail instruction");
  require_once(comments_to_ibm,
               "If you prefer to send comments by FAX",
               "comments-by-FAX instruction");
  require_absent(comments_to_ibm, "SC31-7111-00/    If",
                 "comments row marker");
  require_absent(comments_to_ibm, "you.adapter", "comments adapter marker");
  require_absent(comments_to_ibm, "<B>", "raw comments HTML");

  const auto questionnaire = document.topic_markdown("COMMENTS");
  require_visible_once(questionnaire, "Overall, how satisfied are you with",
                       "overall satisfaction question heading");
  require_visible_near(questionnaire, "Overall, how satisfied are you with",
                       "the information in this book?", 180,
                       "overall satisfaction question");
  require_visible_once(questionnaire, "How satisfied are you that the",
                       "information-quality question heading");
  require_visible_near(questionnaire, "How satisfied are you that the",
                       "information in this book is:", 180,
                       "information-quality question");
  require_contains(questionnaire, "Satisfied", "satisfaction choice");
  require_contains(questionnaire, "Dissatisfied", "dissatisfaction choice");
  for (const auto* criterion : {"Accurate", "Complete", "Easy to find",
                                "Easy to understand", "Well organized",
                                "Applicable to your task"}) {
    require_visible_once(questionnaire, criterion,
                         "information-quality criterion");
  }
  require_absent(questionnaire, "<B>", "raw questionnaire HTML");

  struct PublicationTopic {
    const char* id;
    const char* title;
  };
  for (const auto& publication : {
           PublicationTopic{"BACK_1.1",
                            "Getting Started with LAN Network Manager for AIX "
                            "(SC31-7109)"},
           PublicationTopic{"BACK_1.7",
                            "FDDI SNMP Proxy Agent User's Guide (GC17-0383)"},
           PublicationTopic{"BACK_1.11",
                            "NETCENTER Operator Tutorial (GC75-0109)"},
           PublicationTopic{"BACK_1.12.2",
                            "OSF/Motif Series (5 volumes)"},
  }) {
    const auto markdown = document.topic_markdown(publication.id);
    require_visible_once(markdown, publication.title, "publication row");
    require_absent(markdown, "<B>", "raw publication HTML");
  }
  require_absent(document.topic_markdown("BACK_1.1"),
                 "(SC31-7109)=", "publication trailing marker");
  require_absent(document.topic_markdown("BACK_1.1"),
                 "(SC31-7109))", "publication duplicate punctuation");
  require_absent(document.topic_markdown("BACK_1.7"),
                 "(GC17-0383)bridge", "publication carry marker");
  require_absent(document.topic_markdown("BACK_1.11"),
                 "(GC75-0109)=", "publication trailing marker");
  require_absent(document.topic_markdown("BACK_1.12.2"),
                 "publications:agent", "publication leading marker");
  require_visible_once(
      document.topic_markdown("BACK_1.11"),
      "NETCENTER Graphic Network Monitor Service Point Interface "
      "Installation (SC75-0111)",
      "second NETCENTER publication row");

  const auto ir_trace = document.trace_logical_records("BACK_1.7");
  std::size_t ansi_source_rows = 0;
  bool saw_typed_font_segment = false;
  bool saw_marker_ownership = false;
  for (const auto& record : ir_trace) {
    saw_typed_font_segment =
        saw_typed_font_segment ||
        std::any_of(record.ir_control_segments.begin(),
                    record.ir_control_segments.end(), [](const auto& value) {
                      return value.find("opcode=cfont") != std::string::npos &&
                             value.find("payload=[") != std::string::npos;
                    });
    ansi_source_rows += static_cast<std::size_t>(std::count_if(
        record.ir_physical_rows.begin(), record.ir_physical_rows.end(),
        [](const auto& value) {
          return value.find("marker='bridge'") != std::string::npos &&
                 value.find("American National Standards Institute") !=
                     std::string::npos;
        }));
    saw_marker_ownership =
        saw_marker_ownership ||
        std::any_of(record.ir_ownership_cells.begin(),
                    record.ir_ownership_cells.end(), [](const auto& value) {
                      return value.find("disposition=marker_slot") !=
                             std::string::npos;
                    });
  }
  if (!saw_typed_font_segment || ansi_source_rows != 2 ||
      !saw_marker_ownership) {
    std::cerr << "typed source IR trace lost control/row/ownership evidence\n";
    ++failures;
  }

  return failures == 0 ? 0 : 1;
}
