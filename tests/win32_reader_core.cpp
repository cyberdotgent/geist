// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The portable half of the Win32 reader: its URL space, link map, topic
// pages and search. Built and run on every platform.

#include "book_source.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace geist_reader;

namespace {

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

void url_space() {
  assert(encode_segment("A b/c~1.-_") == "A%20b%2Fc~1.-_");
  assert(decode_segment("A%20b%2Fc%zz%4") == "A b/c%zz%4");
  const auto topic = parse_url("geist://B7/topic/1.3%20x#fig%2D1");
  assert(topic && topic->host == "b7" && topic->kind == ReaderUrl::Kind::topic);
  assert(topic->id == "1.3 x" && topic->fragment == "fig-1");
  const auto object = parse_url("GEIST://b1/object/IMG1?x=1");
  assert(object && object->kind == ReaderUrl::Kind::object && object->id == "IMG1");
  assert(!parse_url("geist://b1/other/x"));
  assert(!parse_url("geist://b1/topic/"));
  assert(!parse_url("http://b1/topic/x"));
  assert(clean_snippet("  see [the *guide*](geist://x) \\# now\n\n`ok`  ") ==
         "see the guide now ok");
  assert(ascii_matcher("Hello World", "WORLD") == std::optional<std::size_t>(6));
  assert(!ascii_matcher("Hello", "x"));
}

void packet_book() {
  BookSource source("b1");
  std::string error;
  assert(!source.open(std::string(GEIST_FIXTURE_DIR) + "/missing.boo", error));
  assert(!error.empty() && !source.has_book());
  assert(source.open(std::string(GEIST_FIXTURE_DIR) + "/packet.boo", error));
  assert(source.has_book());
  assert(!source.contents().empty());
  assert(source.information().topic_count == source.contents().size());
  assert(source.information().file_size > 0);
  source.set_stylesheet(".marker-css{}");
  const auto& first = source.contents().front();
  const auto page = source.serve(source.topic_url(first.id));
  assert(page && page->mime == "text/html");
  assert(contains(page->bytes, "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"));
  assert(contains(page->bytes, ".marker-css{}"));
  assert(contains(page->bytes, "class='reader-topic-header'"));
  assert(contains(page->bytes, "<body class='geist-book'>"));
  // Another window's host is not this book.
  BookSource other("b2");
  assert(!source.serve(other.topic_url(first.id)));
  assert(!source.serve(source.topic_url("NO SUCH TOPIC")));

  // Every in-book link the pages carry is in this book's URL space.
  std::size_t links = 0;
  for (const auto& row : source.contents()) {
    const auto html = source.topic_page(row.id);
    assert(html);
    for (std::size_t at = html->find("href=\"geist://"); at != std::string::npos;
         at = html->find("href=\"geist://", at + 1)) {
      const auto end = html->find('"', at + 6);
      const auto url = parse_url(html->substr(at + 6, end - at - 6));
      assert(url && url->host == "b1");
      ++links;
    }
  }
  std::cout << "in-book links: " << links << '\n';

  if (source.information().resource_count) {
    std::size_t served = 0;
    for (const auto& row : source.contents()) {
      const auto html = source.topic_page(row.id);
      const auto at = html->find("src=\"geist://b1/object/");
      if (at == std::string::npos) continue;
      const auto end = html->find('"', at + 5);
      const auto image = source.serve(html->substr(at + 5, end - at - 5));
      assert(image && image->mime == "image/png");
      assert(image->bytes.compare(0, 4, "\x89PNG") == 0);
      ++served;
      break;
    }
    std::cout << "served objects: " << served << '\n';
  }

  const auto hits = source.search(first.title.substr(0, 4));
  assert(!hits.empty());
  assert(source.search("zzzqqq-no-such-text").empty());
  assert(source.search("").empty());

  source.close();
  assert(!source.has_book() && source.contents().empty());
  assert(!source.serve(source.topic_url(first.id)));
}

} // namespace

int main() {
  url_space();
  packet_book();
  std::cout << "win32 reader core: ok\n";
  return 0;
}
