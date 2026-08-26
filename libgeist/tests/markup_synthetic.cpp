#include "geist/detail/internal.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect_equal(const std::string& name,
                  const std::string& actual,
                  const std::string& expected) {
  if (actual == expected) {
    return true;
  }
  std::cerr << name << " mismatch\nexpected: " << expected
            << "\nactual:   " << actual << "\n";
  return false;
}

bool expect_records(const std::string& name,
                    const std::vector<std::string>& decoded,
                    const std::vector<std::string>& expected) {
  const auto actual = geist::detail::render_gml_records(decoded);
  if (actual == expected) {
    return true;
  }
  std::cerr << name << " record mismatch\n";
  for (const auto& record : actual) {
    std::cerr << "  actual:   " << record << "\n";
  }
  for (const auto& record : expected) {
    std::cerr << "  expected: " << record << "\n";
  }
  return false;
}

bool valid_utf8(const std::string& value) {
  for (std::size_t index = 0; index < value.size();) {
    const auto first = static_cast<unsigned char>(value[index]);
    std::size_t length = 1;
    if (first >= 0xC2 && first <= 0xDF) {
      length = 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
      length = 3;
    } else if (first >= 0xF0 && first <= 0xF4) {
      length = 4;
    } else if (first >= 0x80 || first == 0xC0 || first == 0xC1 ||
               first >= 0xF5) {
      return false;
    }
    if (index + length > value.size()) {
      return false;
    }
    for (std::size_t continuation = 1; continuation < length;
         ++continuation) {
      if ((static_cast<unsigned char>(value[index + continuation]) & 0xC0) !=
          0x80) {
        return false;
      }
    }
    index += length;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;

  for (const auto& title : {
           std::pair{"Additional Problem Information <",
                     "Additional Problem Information"},
           std::pair{"Customer Information >", "Customer Information"},
           std::pair{"Customer Information /", "Customer Information"},
       }) {
    ok &= expect_equal("standalone TOC row marker",
                       geist::detail::normalize_toc_title(title.first),
                       title.second);
  }
  for (const auto* title : {"TCP/IP", "Input/Output", "AIX (TM)"}) {
    ok &= expect_equal("TOC title punctuation",
                       geist::detail::normalize_toc_title(title), title);
  }
  ok &= expect_equal("embedded TOC parentheses",
                     geist::detail::normalize_toc_title(
                         "Parenthetical (title)"),
                     "Parenthetical (Title)");

  {
    auto row = geist::detail::assemble_fixed_display_row(
        {"prefix ", ">    continuation"});
    const auto mapping = row.source_columns;
    geist::detail::blank_fixed_display_marker_fields(row, true);
    ok &= expect_equal("fixed marker blanking", row.text,
                       "prefix      continuation");
    const auto mapping_unchanged =
        row.source_columns.size() == mapping.size() &&
        std::equal(row.source_columns.begin(), row.source_columns.end(),
                   mapping.begin(), [](const auto& left, const auto& right) {
                     return left.fragment == right.fragment &&
                            left.column == right.column;
                   });
    if (!mapping_unchanged || row.text.size() != mapping.size() ||
        row.source_columns[7].fragment != 1 ||
        row.source_columns[7].column != 0) {
      ok = false;
      std::cerr << "fixed marker blanking shifted source columns\n";
    }
  }

  {
    auto row = geist::detail::assemble_fixed_display_row(
        {"Use (optional) values when x > y; keep / and \"quotes\"."});
    geist::detail::blank_fixed_display_marker_fields(row, true);
    ok &= expect_equal(
        "literal punctuation survives fixed marker normalization",
        row.text,
        "Use (optional) values when x > y; keep / and \"quotes\".");
  }

  {
    // A fixed form may end its SRTBL payload in the first physical line of a
    // field.  The following CFONT owns the continuation, while its next
    // complete bordered line must still begin a distinct row.
    const auto border = std::string(74, '?');
    const auto short_border = std::string(26, '?');
    auto physical_row = [&](std::string text) {
      text.resize(43, ' ');
      return std::string(")    ?") + text + "?" + std::string(28, ' ') +
             "?      ?" + std::string(43, ' ') + "?" + short_border +
             "?                 ?" + border;
    };
    auto incomplete_row = std::string(")    ?Amount of free space available in the");
    incomplete_row.resize(49, ' ');
    incomplete_row += "?" + std::string(28, ' ') + "?";
    const std::vector<std::string> decoded{
        "SRTBLFORM " + border + physical_row("Amount of memory installed") +
            physical_row("Amount of paging space available") + incomplete_row,
        "cfont 31 7 X " +
            physical_row("file system | that contains /usr/OV"),
        "cfont 39 4 X " + physical_row("Amount of free space available in /tmp"),
        "SRETBL"};
    const auto rendered = geist::detail::render_gml_records(decoded);
    auto joined = std::string{};
    for (const auto& record : rendered) {
      joined += "\n" + record;
    }
    if (joined.find("Amount of free space available in the<br>file system | "
                    "that contains /usr/OV") == std::string::npos ||
        joined.find(":c col='0'.Amount of free space available in /tmp") ==
            std::string::npos) {
      ok = false;
      std::cerr << "fixed-form CFONT continuation lost row ownership\n";
    }
  }

  ok &= expect_records(
      "fixed-width CFONT word spans",
      {"CFONT 8 10 2 19 2 2 22 4 2 27 4 2     Production of This Book"
       "?????     This book was prepared and formatted."},
      {":p.:hp2.Production:ehp2. :hp2.of:ehp2. :hp2.This:ehp2. "
       ":hp2.Book:ehp2.",
       ":p.This book was prepared and formatted."});

  ok &= expect_records(
      "all-E log rows keep one literal row and suppress marker fields",
      {"cfont 6 4 E 11 2 E       User ID     : 0       a",
       "cfont 5 4 2     Next prose"},
      {":line.User ID : 0", ":p.:hp2.Next:ehp2. prose"});
  ok &= expect_records(
      "all-E rules discard unstyled alphabetic marker slots",
      {"cfont 5 10 E      ~~~~~~~~~~   marker"},
      {":line.~~~~~~~~~~"});

  ok &= expect_records(
      "subject-index margins preserve leading prose",
      {"SI overview, operating cost enhancements    The operational aspects "
       "of IMS are enhanced.",
       "SI VSO, implementing    required for the definition and activation."},
      {":line.The operational aspects of IMS are enhanced.",
       ":line.required for the definition and activation."});

  ok &= expect_records(
      "uppercase prose is not a control",
      {"SHOULD an invalid pointer be detected, Fast Path takes action.",
       "SHIPPED with IMS 5.1.",
       "SHARING environment."},
      {":p.SHOULD an invalid pointer be detected, Fast Path takes action. "
       "SHIPPED with IMS 5.1. SHARING environment."});

  {
    geist::detail::TopicData topic;
    topic.id = "ABSTRACT";
    topic.raw_records = {
        "SHabstract CTOPICN 1 CHDLEVEL :ABSTRACT ? ST  Abstract .    First "
        "fixed row.        Second fixed row.?    (217 pages)"};
    geist::TocEntry entry;
    entry.id = "ABSTRACT";
    entry.title = "Abstract";
    geist::detail::attach_topic_data(entry, topic);
    const std::vector<std::string> expected{
        ":abstract.", ":xmp.", ":xline.   First fixed row.", ":xline.",
        ":xline.   Second fixed row.", ":xline.",
        ":xline.   (217 pages)", ":exmp."};
    if (entry.raw_records != expected) {
      ok = false;
      std::cerr << "structural ST body record mismatch\n";
      for (const auto& record : entry.raw_records) {
        std::cerr << "  actual:   " << record << "\n";
      }
      for (const auto& record : expected) {
        std::cerr << "  expected: " << record << "\n";
      }
    }
  }

  {
    geist::detail::TopicData topic;
    topic.id = "2.2";
    topic.title = "Operating Cost Enhancements";
    topic.raw_records = {
        "SH2.2 CTOPICN 22 CPARENT 2.0 CFORWARDLEVEL 2.3 "
        "CBACKLEVEL 2.1 CSUMMARY 6 3 6 CHDLEVEL :H2 "
        "CSOURCEFN 4302CH2 ST  Operating Cost Enhancements"
        "       SI overview, operating cost enhancements       The operational "
        "aspects of IMS are enhanced for online and DBCTL           environments."
        "  CMENU CMITEM 2.2.1 New Automated Operator Facilities CEMENU"};
    geist::TocEntry entry;
    entry.id = topic.id;
    entry.title = topic.title;
    geist::detail::attach_topic_data(entry, topic);
    if (entry.raw_records.empty() ||
        entry.raw_records.front().find("Operating Cost Enhancements") ==
            std::string::npos ||
        std::none_of(entry.raw_records.begin(), entry.raw_records.end(),
                     [](const std::string& record) {
                       return record.find("operational aspects") !=
                              std::string::npos;
                     })) {
      ok = false;
      std::cerr << "ST leading body text was dropped\n";
      for (const auto& record : entry.raw_records) {
        std::cerr << "  actual: " << record << "\n";
      }
    }
  }

  ok &= expect_records(
      "figure-list selections retain display rows",
      {"SHfigures CHDLEVEL :FIGLIST ? ST Figures ? CSELECT 3 40 FIG1 "
       "1. First figure ? CSELECT 3 40 FIG2 2. Second figure"},
      {":figlist.", ":p.:hdref refid='FIG1'.1. First figure:ehdref.",
       ":p.:hdref refid='FIG2'.2. Second figure:ehdref."});
  ok &= expect_records(
      "fixed selection continuation markers are not row text",
      {"SHfigures CHDLEVEL :FIGLIST ? ST Figures ? CSELECT 5 30 FIG1 "
       ".  |   3-1. Status worksheet"},
      {":figlist.", ":p.:hdref refid='FIG1'.3-1. Status worksheet:ehdref."});


  ok &= expect_records(
      "table-list selections retain display rows",
      {"SHTABLES CHDLEVEL :TLIST ? ST Tables ? CSELECT 3 40 TBL1 "
       "1. First table ? CSELECT 3 40 TBL2 2. Second table"},
      {":tlist.", ":p.:hdref refid='TBL1'.1. First table:ehdref.",
       ":p.:hdref refid='TBL2'.2. Second table:ehdref."});

  ok &= expect_records(
      "generated index retains hierarchy and topic targets",
      {"CINDEX", "CGPSEP ?Special Characters", "CITERM ?/command?1?A.0",
       "CITERM ?Parent?1", "CITERM ?Child?2?FRONT_1.1",
       "CITERM ?Multiple?2?4.1.2.1?4.1.2.3", "CITERM ?files?1? /",
       "CENDINDEX", "garbage after index"},
      {":index.", ":grpsep.Special Characters",
       ":i1 level='1' refids='A.0'./command", ":i1 level='1'.Parent",
       ":i1 level='2' refids='FRONT_1.1'.Child",
       ":i1 level='2' refids='4.1.2.1 4.1.2.3'.Multiple",
       ":i1 level='1'.files", ":eindex."});

  ok &= expect_records(
      "legacy SRTBL keeps first row in header segment",
      {"SRTBLTBL1 ? Alpha   Beta   Gamma ? SRETBL"},
      {":table id='TBLTBL1'.", ":row.", ":c col='0'.Alpha",
       ":c col='1'.Beta", ":c col='2'.Gamma", ":tcap.", ":etable."});

  {
    auto left = std::string("For information");
    left.resize(23, ' ');
    auto continuation = std::string("about:");
    continuation.resize(23, ' ');
    auto first_label = std::string("LNM OS/2 agent traps");
    first_label.resize(23, ' ');
    auto first_target = std::string("\"LNM OS/2 Agent Application Traps\" in");
    first_target.resize(48, ' ');
    auto second_label = std::string("SNMP token-ring traps");
    second_label.resize(23, ' ');
    auto second_target = std::string("\"SNMP Token-Ring Traps\" in topic 4.2");
    second_target.resize(48, ' ');
    const std::vector<std::string> decoded{
        "SRTBLGRID " + std::string(73, '?'),
        "cfont 5 3 2               ?" + left + "?Read:",
        "cfont 5 6 2           ?" + continuation + "?" +
            std::string(48, ' ') + "?" + std::string(62, '?'),
        "cselect 29 37 FIRST address    ?" + first_label + "?" +
            first_target + "?" + std::string(62, '?'),
        "cselect 29 36 SECOND any    ?" + second_label + "?" +
            second_target + "?" + std::string(62, '?'),
        "SRETBL"};
    const auto rendered = geist::detail::render_gml_records(decoded);
    const auto has_left = std::find(
                              rendered.begin(), rendered.end(),
                              ":c col='0'.For information<br>about:") !=
                          rendered.end();
    const auto has_right = std::find(rendered.begin(), rendered.end(),
                                     ":c col='1'.Read:") != rendered.end();
    const auto has_first_link = std::any_of(
        rendered.begin(), rendered.end(), [](const auto& record) {
          return record.find(":hdref refid='FIRST'.\"LNM OS/2 Agent ") !=
                 std::string::npos;
        });
    const auto has_second_link = std::any_of(
        rendered.begin(), rendered.end(), [](const auto& record) {
          return record.find(
                     ":hdref refid='SECOND'.\"SNMP Token-Ring Traps\"") !=
                 std::string::npos;
        });
    if (!has_left || !has_right || !has_first_link || !has_second_link) {
      ok = false;
      std::cerr << "fixed table geometry or CSELECT ownership was lost\n";
    }
  }

  {
    const auto fixed_row = [](std::string first,
                              std::string second,
                              std::string third) {
      first.resize(40, ' ');
      second.resize(15, ' ');
      third.resize(15, ' ');
      return "?" + first + "?" + second + "?" + third + "?";
    };
    const std::vector<std::string> decoded{
        "SRTBLQUEST " + std::string(74, '?'),
        "cfont 5 4 2 ?" +
            fixed_row("Question", "Yes", "No").substr(1),
        "cfont 5 5 2 ?" +
            fixed_row("Ready?", "__", "__").substr(1),
        "SRETBL"};
    const auto rendered = geist::detail::render_gml_records(decoded);
    const auto literal = std::find(rendered.begin(), rendered.end(),
                                   ":c col='0'.Ready?");
    if (literal == rendered.end()) {
      ok = false;
      std::cerr << "fixed table lost lexical question punctuation\n";
    }
  }

  ok &= expect_records(
      "fixed-layout notice links",
      {"CSELECT 43 30 HDRNOTICES              ? to read the general "
       "information under \"Special Notices\" in "
       "CSELECT 5 13 HDRNOTICES      ? topic FRONT_1."},
      {":p.to read the general information under :hdref "
       "refid='HDRNOTICES'.\"Special Notices\" in:ehdref. :hdref "
       "refid='HDRNOTICES'.topic FRONT_1:ehdref.."});

  ok &= expect_records(
      "ordinary fixed-layout link",
      {"CSELECT 33 3 SPTPROC         System/36 procedures     Page 2.1"},
      {":p.System/36 procedures Page :hdref refid='SPTPROC'.2.1:ehdref."});

  ok &= expect_records(
      "visual-row marker is not a display cell",
      {"CSELECT 47 17 HDRBIBL | For a list of related publications, see "
       "the \"Bibliography\" in"},
      {":p.For a list of related publications, see the :hdref "
       "refid='HDRBIBL'.\"Bibliography\" in:ehdref."});

  ok &= expect_records(
      "decoder boundary before visual-row marker",
      {"CSELECT 3 18 HDRBIBL ?  | topic BIBLIOGRAPHY."},
      {":p.:hdref refid='HDRBIBL'.topic BIBLIOGRAPHY:ehdref.."});

  ok &= expect_records(
      "picture selection preserves surrounding text",
      {"CSELECT 8 5 PIC1 text Image rest"},
      {":image resource='1'.", ":figcap.text Image rest"});

  ok &= expect_records(
      "wrapped footnote link",
      {"CSELECT 16 4 FTNFTNUNIQ1 ?    technologies. (1)"},
      {":p.technologies. :hdref refid='FTNFTNUNIQ1'.(1):ehdref."});

  ok &= expect_records(
      "first reconstructed display line owns selection",
      {"CSELECT 16 5 FTNFTNUNIQ26 ?    for yourself: (17)"},
      {":p.for yourself: :hdref refid='FTNFTNUNIQ26'.(17):ehdref."});

  ok &= expect_records(
      "in-line punctuation does not start another display line",
      {"CSELECT 58 4 FTNFTNUNIQ10 ;    from a PC running the AX.25 network "
       "stack code on a PC. (8)  Before  being"},
      {":p.from a PC running the AX.25 network stack code on a PC. "
       ":hdref refid='FTNFTNUNIQ10'.(8):ehdref. Before being"});

  ok &= expect_records(
      "suppressed generated list prefix",
      {"CSELECT 19 5 FTNFTNUNIQ64      ?   APRS support (51)"},
      {":p.APRS support :hdref refid='FTNFTNUNIQ64'.(51):ehdref."});

  ok &= expect_records(
      "placeholder-run continuation margin",
      {"CSELECT 9 5 FTNFTNUNIQ60 ???????????????????????        to (47)"},
      {":p.to :hdref refid='FTNFTNUNIQ60'.(47):ehdref."});

  ok &= expect_records(
      "direct continuation with suppressed list prefix",
      {"CSELECT 63 5 FTNFTNUNIQ83 is capable of hitting several kilobaud "
       "with a good radio (67)"},
      {":p.is capable of hitting several kilobaud with a good radio "
       ":hdref refid='FTNFTNUNIQ83'.(67):ehdref."});

  ok &= expect_records(
      "direct display survives trailing marker padding",
      {"CSELECT 20 5 FTNFTNUNIQ42           VE4KLM's website: (30)????"},
      {":p.VE4KLM's website: :hdref "
       "refid='FTNFTNUNIQ42'.(30):ehdref."});

  ok &= expect_records(
      "pending select applied to following font line",
      {"CSELECT 60 4 FTNFTNUNIQ13 -",
       "CFONT 3 3 5 7 4 5 (    for your \"basic AX.25 connections\" and "
       "your NET/ROM node. (9)"},
      {":p.for your \"basic AX.25 connections\" and your NET/ROM "
       "node. :hdref refid='FTNFTNUNIQ13'.(9):ehdref."});

  ok &= expect_records(
      "multiple pending selects share one display line",
      {"CSELECT 11 5 FTNFTNUNIQ57",
       "CSELECT 16 5 FTNFTNUNIQ58 locator: (44) (45)"},
      {":p.locator: :hdref refid='FTNFTNUNIQ57'.(44):ehdref. :hdref "
       "refid='FTNFTNUNIQ58'.(45):ehdref."});

  ok &= expect_equal(
      "markdown link prefix and suffix",
      geist::detail::render_markdown_records(
          {":hdref refid='HDRNOTICES' prefix='see' suffix='.'.Special"}),
      "see [Special](#HDRNOTICES).\n");

  ok &= expect_equal(
      "markdown word suffix spacing",
      geist::detail::render_markdown_records(
          {":hdref refid='FTNFTNUNIQ10' suffix='Before being'.(8)"}),
      "<a id=\"fnref-FTNFTNUNIQ10\"></a>[(8)](#FTNFTNUNIQ10) Before "
      "being\n");

  ok &= expect_equal(
      "markdown picture surroundings",
      geist::detail::render_markdown_records(
          {":image resource='1' prefix='text' suffix='rest'.Image"}),
      "text ![Image](resource:1) rest\n");

  {
    // CFONT lengths/columns count display characters, not UTF-8 bytes.  A
    // delimiter around the not-sign must therefore be placed before/after
    // the complete C2 AC sequence.
    const auto gml = geist::detail::render_gml_records(
        {"CFONT 4 1 x A\xC2\xAC" "B"});
    const auto markdown = geist::detail::render_markdown_records(gml);
    ok &= expect_equal("UTF-8 CFONT span projection", gml.front(),
                       ":p.A:xph.\xC2\xAC:exph.B");
    ok &= expect_equal("UTF-8 CFONT Markdown projection", markdown,
                       "A`\xC2\xAC`B\n");
    if (!valid_utf8(markdown)) {
      ok = false;
      std::cerr << "UTF-8 CFONT Markdown projection is malformed\n";
    }
  }

  return ok ? 0 : 1;
}
