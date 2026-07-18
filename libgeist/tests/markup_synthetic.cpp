#include "geist/detail/internal.hpp"

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

} // namespace

int main() {
  bool ok = true;

  ok &= expect_records(
      "fixed-width CFONT word spans",
      {"CFONT 8 10 2 19 2 2 22 4 2 27 4 2     Production of This Book"
       "?????     This book was prepared and formatted."},
      {":p.:hp2.Production:ehp2. :hp2.of:ehp2. :hp2.This:ehp2. "
       ":hp2.Book:ehp2.",
       ":p.This book was prepared and formatted."});

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
      "legacy SRTBL keeps first row in header segment",
      {"SRTBLTBL1 ? Alpha   Beta   Gamma ? SRETBL"},
      {":table id='TBLTBL1'.", ":row.", ":c col='0'.Alpha",
       ":c col='1'.Beta", ":c col='2'.Gamma", ":tcap.", ":etable."});

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

  return ok ? 0 : 1;
}
