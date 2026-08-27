#include "geist/document.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

std::string markdown_visible_text(const std::string& markdown) {
  std::string visible;
  visible.reserve(markdown.size());
  for (std::size_t index = 0; index < markdown.size(); ++index) {
    if (markdown[index] == '\\' && index + 1 < markdown.size()) {
      visible.push_back(markdown[++index]);
    } else {
      visible.push_back(markdown[index]);
    }
  }
  return visible;
}

void require_contains(const std::string& text, const std::string& expected,
                      const char* label) {
  if (text.find(expected) != std::string::npos) {
    return;
  }
  std::cerr << "missing " << label << ": " << expected << '\n';
  ++failures;
}

void require_absent(const std::string& text, const std::string& unexpected,
                    const char* label) {
  if (text.find(unexpected) == std::string::npos) {
    return;
  }
  std::cerr << "unexpected " << label << ": " << unexpected << '\n';
  ++failures;
}

void require_separate_paragraphs(const std::string& text,
                                 const std::string& left,
                                 const std::string& right,
                                 const char* label) {
  const auto left_at = text.find(left);
  const auto right_at = left_at == std::string::npos
                            ? std::string::npos
                            : text.find(right, left_at + left.size());
  if (left_at != std::string::npos && right_at != std::string::npos &&
      text.substr(left_at + left.size(), right_at - left_at - left.size())
              .find("\n\n") != std::string::npos) {
    return;
  }
  std::cerr << "missing independent publication paragraphs for " << label
            << '\n';
  ++failures;
}

} // namespace

int main() {
  const auto book = std::filesystem::path(GEIST_REPO_ROOT) / "BOO" /
                    "SC31-711.boo";
  const auto document = geist::BooDocument::open(book);

  const auto hub = markdown_visible_text(document.topic_markdown("BACK_1.4"));
  require_contains(
      hub,
      "AIX NetView Hub Management Program/6000 Installation and User's Guide "
      "(SH11-3067)",
      "markerless hub publication row");
  require_absent(hub, "hubs:address", "hub publication marker slot");

  const auto token_ring =
      markdown_visible_text(document.topic_markdown("BACK_1.6"));
  require_contains(token_ring,
                   "IBM Token-Ring Network Problem Determination Guide "
                   "(SZ27-3710)",
                   "first token-ring publication row");
  require_contains(
      token_ring,
      "IBM 8230 Token-Ring Network Controlled Access Base Unit Customer Setup "
      "Instructions (GA27-3905)",
      "wrapped token-ring publication row");

  const auto fddi = markdown_visible_text(document.topic_markdown("BACK_1.7"));
  const std::string ansi_1990 =
      "American National Standards Institute, X3T9/90-X3T9.5/84-49 REV 6.2 "
      "May 18, 1990";
  const std::string ansi_1992 =
      "American National Standards Institute, X3T9/92-X3T9.5/84-49 REV 7.2 "
      "June 25, 1992";
  require_separate_paragraphs(fddi, ansi_1990, ansi_1992,
                              "independent FDDI ANSI citations");
  require_absent(fddi, "bridge American National Standards",
                 "FDDI publication marker slot");

  const auto aix = markdown_visible_text(document.topic_markdown("BACK_1.8"));
  require_contains(aix, "AIX Topic Index and Glossary (GC23-2201)",
                   "AIX topic-index publication row");
  require_contains(
      aix,
      "AIX Communications Concepts and Procedures for IBM RISC System/6000 "
      "(GC23-2203)",
      "wrapped AIX communications publication row");

  const auto x_window =
      markdown_visible_text(document.topic_markdown("BACK_1.12.1"));
  require_separate_paragraphs(
      x_window, "Prentice-Hall, 1989 (ISBN 0-13-972167)",
      "X Window System: Programming and Applications with Xt, OSF/Motif "
      "Edition",
      "independent X Window publications");

  const auto bridges =
      markdown_visible_text(document.topic_markdown("BACK_1.12.3"));
  require_separate_paragraphs(
      bridges, "IBM 8229 Bridge Manual, GA27-4025",
      "IBM Multiprotocol Network Program Configuration, SC31-6691",
      "independent bridge publications");
  require_contains(
      bridges,
      "IBM RouteXpander/2 Introduction and Configuration Examples, GG24-4334",
      "final bridge publication row");

  const auto lfs = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "GG24-395.boo");
  const auto lfs_publications =
      markdown_visible_text(lfs.topic_markdown("3.3.10.6"));
  require_contains(lfs_publications,
                   "LAN File Services/ESA VM Guide and Reference, SH24-5264",
                   "cross-book LFS/ESA publication");
  require_contains(lfs_publications,
                   "LAN File Services/ESA General Information, GH24-5259",
                   "cross-book LFS/ESA publication");

  const auto c370 = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC09-138.boo");
  const auto c370_publications =
      markdown_visible_text(c370.topic_markdown("BIBLIOGRAPHY.1"));
  require_contains(c370_publications,
                   "SAA: Common Programming Interface C Reference, SC09-1308",
                   "cross-book C/370 publication");
  require_contains(
      c370_publications,
      "IBM C/370 Installation and Customization for VSE, GC09-1417",
      "cross-book C/370 publication");

  // Negatives: structurally publication-shaped topics that carry no
  // publication semantics must stay on their legacy rendering rather than
  // being lowered as independent publication paragraphs. GC23-046 FRONT_1.1 is
  // a title-only envelope (a wrapped title run carrying a trademark list whose
  // prose contains the word "publication"); it renders as one preformatted
  // block. IBMMMSTR PREFACE.5 is syntax notation and SG24-204 PREFACE.1 a team
  // biography, both with completely represented entry-run envelopes.
  const auto trademarks = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "GC23-046.boo");
  const auto trademark_markdown = trademarks.topic_markdown("FRONT_1.1");
  require_contains(trademark_markdown, "```text",
                   "legacy preformatted trademark notice");
  require_absent(trademark_markdown, "\n\nIBM\n\n",
                 "trademark notice lowered as a publication entry");

  const auto messages = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "IBMMMSTR.boo");
  const auto syntax = markdown_visible_text(messages.topic_markdown("PREFACE.5"));
  require_contains(syntax,
                   "Special notation that is used in this book follows:",
                   "syntax notation legacy paragraph");
  require_absent(syntax, "\n\nShift-out command\n\n",
                 "syntax notation lowered as a publication entry");

  const auto redbook = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SG24-204.boo");
  const auto team = markdown_visible_text(redbook.topic_markdown("PREFACE.1"));
  require_contains(team, "The Team That Wrote This Redbook",
                   "team biography heading");

  // BACK_1.3: the seventh entry control ends its record with an empty payload
  // and originates its run on the next record's leading text segment. The
  // typed lowering carries all eight entries with no marker-slot leak and a
  // collapsed introduction (hosted BACK_1.3, DT=19941010174546).
  const auto netview = markdown_visible_text(document.topic_markdown("BACK_1.3"));
  require_contains(netview,
                   "The following publications compose the library for "
                   "Version 2 of the AIX SystemView NetView/6000 program:",
                   "NetView/6000 introduction");
  require_contains(netview,
                   "AIX SystemView NetView/6000 Concepts: A General "
                   "Information Manual (GC31-6179)",
                   "wrapped NetView/6000 publication row");
  require_separate_paragraphs(
      netview, "AIX SystemView NetView/6000 Problem Determination (SC31-7021)",
      "AIX SystemView NetView/6000 Programmer's Guide (SC31-7022)",
      "deferred-origin NetView/6000 entry");
  require_separate_paragraphs(
      netview, "AIX SystemView NetView/6000 Programmer's Reference (SC31-7023)",
      "AIX SystemView NetView/6000 User's Guide (SC31-7024)",
      "final NetView/6000 entries");
  require_absent(netview, "ADAPTER", "NetView/6000 marker-slot leak");
  require_absent(netview, "AIX         SystemView",
                 "NetView/6000 introduction padding");

  // D.3: a citation catalog with a deferred-origin entry; hosted D.3
  // (DT=19971218054640) lists ten bulleted publications.
  const auto other = markdown_visible_text(redbook.topic_markdown("D.3"));
  require_contains(other,
                   "These publications are also relevant as further "
                   "information sources.",
                   "redbook publication introduction");
  require_separate_paragraphs(
      other,
      "CICS/VSE, Server Support for CICS Clients, Version 2 Release 3, "
      "SC33-1712",
      "CICS/VSE, Resource Definition (Online), Version 2 Release 3, SC33-0708",
      "redbook publication entries");
  require_contains(other, "CICS Family: Inter-Product Communication, SC33-0824",
                   "redbook entry before the record boundary");
  require_contains(other,
                   "CICS Family: Communicating from CICS on System/390, "
                   "SC33-1697",
                   "redbook deferred-origin entry");
  require_absent(other, "<pre>", "redbook publication preformatted fallback");

  // BIBLIOGRAPHY.9: a title-only envelope (no introduction) whose three
  // whole-line entries share the column-3 list margin; hosted BIBLIOGRAPHY.9
  // (DT=19911015203151) renders three separate entries.
  const auto netview_host = geist::BooDocument::open(
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC31-605.boo");
  const auto related =
      markdown_visible_text(netview_host.topic_markdown("BIBLIOGRAPHY.9"));
  require_separate_paragraphs(
      related,
      "SAA Common User Access: Advanced Interface Design Guide (SC26-4582)",
      "SNA Management Service Reference (SC30-3346)",
      "related publications first and second entries");
  require_separate_paragraphs(related,
                              "SNA Management Service Reference (SC30-3346)",
                              "SNA Technical Overview (GC30-3073)",
                              "related publications second and third entries");
  const auto ncp = markdown_visible_text(netview_host.topic_markdown("BIBLIOGRAPHY.5"));
  require_separate_paragraphs(ncp, "NCP and SSP Library Supplement (SD35-0251)",
                              "NCP and SSP Diagnosis and Reference Supplement "
                              "(LD35-0252)",
                              "NCP publication entries");

  // BIBLIOGRAPHY.3 and BIBLIOGRAPHY.6 carry the introduction on the title row
  // behind a five-space gap; the title/introduction boundary is ambiguous and
  // the catalog fails closed to the legacy path.
  const auto vtam = markdown_visible_text(netview_host.topic_markdown("BIBLIOGRAPHY.3"));
  require_absent(vtam, "## BIBLIOGRAPHY.3 VTAM V3R4 Information The following",
                 "ambiguous title/introduction merged into the heading");

  return failures == 0 ? 0 : 1;
}
