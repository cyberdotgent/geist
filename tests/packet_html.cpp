// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// End-to-end HTML output over the one redistributable BOO fixture.
//
// The synthetic renderer test proves the markup for a document built by hand.
// This one proves the properties that only a real book can exercise: that a
// whole-book export's cross references land on ids the export really emits,
// that the two output forms agree, and that only markup the renderer emits
// reaches the page.

#include "geist/document.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (condition)
    return;
  std::cerr << "packet_html: " << message << "\n";
  geist_test::record_failure();
}

void require_contains(const std::string& haystack, const std::string& needle,
                      const std::string& label) {
  require(haystack.find(needle) != std::string::npos,
          label + " (missing: " + needle + ")");
}

// Every value of an attribute, e.g. `id="..."` or `href="#..."`.
std::vector<std::string> attribute_values(const std::string& html,
                                          const std::string& marker) {
  std::vector<std::string> values;
  std::size_t at = 0;
  while ((at = html.find(marker, at)) != std::string::npos) {
    at += marker.size();
    const auto end = html.find('"', at);
    if (end == std::string::npos)
      break;
    values.push_back(html.substr(at, end - at));
    at = end;
  }
  return values;
}

// Every tag name the document contains.
std::set<std::string> tag_names(const std::string& html) {
  std::set<std::string> tags;
  for (std::size_t at = 0; at < html.size(); ++at) {
    if (html[at] != '<')
      continue;
    auto start = at + 1;
    if (start < html.size() && html[start] == '/')
      ++start;
    auto end = start;
    while (end < html.size() &&
           std::isalnum(static_cast<unsigned char>(html[end])) != 0)
      ++end;
    if (end != start)
      tags.insert(html.substr(start, end - start));
  }
  return tags;
}

// The one destination in this book that names nothing inside it: the reader's
// own "Summarize" affordance on a generated contents page, which addresses
// BookServer's summary view rather than a place in the book. The Markdown
// export spells the same dead destination.
const char* const synthesized_destination = "CONTENTS-summary";

void check_destinations(const std::string& html, const std::string& prefix,
                        const std::string& label) {
  const auto ids = attribute_values(html, " id=\"");
  const std::set<std::string> emitted(ids.begin(), ids.end());
  std::set<std::string> dangling;
  for (const auto& href : attribute_values(html, "href=\"#")) {
    if (href.empty()) // a marked unresolvable reference, which is not a target
      continue;
    if (href == prefix + synthesized_destination)
      continue;
    if (emitted.count(href) == 0)
      dangling.insert(href);
  }
  require(dangling.empty(),
          label + ": " + std::to_string(dangling.size()) +
              " fragment destinations name an id the export does not emit" +
              (dangling.empty() ? "" : ", first: " + *dangling.begin()));
  require(!emitted.empty(), label + ": no ids were emitted at all");
}

} // namespace

int main() {
  const auto book = std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
  const auto document = geist::BooDocument::open(book);

  const auto fragment = document.html_fragment();

  // The id a link targets is the id that is actually emitted. A divergence
  // here is invisible on the page -- the text still reads normally -- and
  // silently breaks every destination it touches.
  check_destinations(fragment, "", "whole-book fragment");

  // The same holds under a configured id prefix, which is applied to emitted
  // ids and to generated hrefs through one function so they cannot diverge.
  geist::HtmlRenderOptions prefixed;
  prefixed.id_prefix = "bk-";
  const auto prefixed_fragment = document.html_fragment(prefixed);
  check_destinations(prefixed_fragment, "bk-", "prefixed whole-book fragment");
  require(prefixed_fragment.find(" id=\"bk-FIGFIGUNIQ5\"") !=
              std::string::npos,
          "the id prefix was not applied to an emitted anchor id");
  require(prefixed_fragment.find("href=\"#bk-FIGFIGUNIQ5\"") !=
              std::string::npos,
          "the id prefix was not applied to the href that names it");
  require(prefixed_fragment.find("data-geist-topic=\"bk-") ==
              std::string::npos,
          "the id prefix leaked into the unprefixed topic identity attribute");

  // Only markup the renderer emits for a recognised typed record reaches the
  // page. Anything else here would mean source bytes became markup.
  const std::set<std::string> allowed = {
      "a",   "aside", "body",       "br",    "code",  "dd",   "div",
      "dl",  "dt",    "em",         "figcaption", "figure", "h1", "h2",
      "h3",  "h4",    "h5",         "h6",    "head",  "html", "img",
      "li",  "meta",  "nav",        "ol",    "p",     "pre",  "section",
      "span", "strong", "style",    "table", "tbody", "td",   "th",
      "thead", "title", "tr",       "ul",    "link",  "doctype"};
  const auto complete = document.html_document();
  for (const auto& tag : tag_names(complete))
    require(allowed.count(tag) != 0,
            "an unexpected tag reached the output: " + tag);

  // The complete document reuses the fragment renderer rather than
  // re-deriving the content, so the fragment appears in it unchanged.
  require_contains(complete, fragment,
                   "the complete document did not reuse the fragment bytes");
  require_contains(complete, "<!doctype html>",
                   "the complete document has no doctype");
  require_contains(complete, "<meta charset=\"utf-8\">",
                   "the complete document does not declare its encoding");
  require_contains(complete, "<title>Amateur Packet Radio",
                   "the complete document did not take its title from the "
                   "book");

  // A single topic is the same bytes as that topic inside the whole book.
  const auto topic = document.topic_html_fragment("2.1.1");
  require_contains(fragment, topic,
                   "a topic fragment differs from the same topic inside the "
                   "whole-book fragment");
  require_contains(topic, "data-geist-topic=\"2.1.1\"",
                   "a topic fragment does not identify its topic");
  require_contains(topic, "data-geist-severity=\"",
                   "a topic fragment does not state its fidelity");

  // Severity is a fact about the topic, not about the format: it must agree
  // with the diagnostic the Markdown route is classified by.
  const auto diagnostics = document.render_diagnostics();
  const auto& toc = document.table_of_contents();
  require(diagnostics.size() == toc.size(),
          "diagnostics and TOC entries disagree in count");
  std::map<std::string, int> severities;
  for (std::size_t index = 0; index < toc.size(); ++index) {
    const auto* name = geist::to_string(diagnostics[index].severity);
    ++severities[name];
    const auto html = toc[index].html_fragment();
    std::string expected = "data-geist-severity=\"";
    expected += name;
    expected += '"';
    require_contains(html, expected,
                     "a topic's HTML severity disagrees with its diagnostic");
  }
  require(severities.size() >= 1, "no severities were classified");

  // Book text containing markup characters is escaped, never passed through.
  require_contains(fragment, "&amp;",
                   "no escaped ampersand appears anywhere in the book");

  geist_test::exit_with_failures();
  std::cout << "packet HTML checks passed\n";
  return 0;
}
