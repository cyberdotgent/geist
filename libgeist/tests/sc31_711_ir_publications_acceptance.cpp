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

  return failures == 0 ? 0 : 1;
}
