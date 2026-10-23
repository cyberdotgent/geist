// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/container/topic_catalogue_framing.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace geist::detail;
using geist::boo_page_size;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}

struct Book {
  LogicalDecodeContext context;
  std::map<std::uint16_t, TokenWords> dictionary;
  std::uint16_t next = 0x8000;

  Book() {
    context.directory.token_threshold = 0x80;
    context.directory.version_text = " 1.2";
    context.directory.dictionary_text_parameter = 500;
    context.bytes.resize(512);
    for (std::size_t byte = 0; byte < 128; ++byte) {
      const auto key = static_cast<std::uint16_t>(0x100 + byte);
      context.bytes[byte * 2] = key >> 8;
      context.bytes[byte * 2 + 1] = key & 0xff;
      dictionary[key] = {'?'};
    }
  }

  void record(const std::vector<std::vector<std::string>> &lines,
              const std::string &prefix = "?") {
    const auto begin = context.bytes.size();
    for (const auto &words : lines) {
      const auto length = static_cast<std::uint8_t>(2 * words.size());
      dictionary[0x100 + length] = TokenWords(prefix.begin(), prefix.end());
      context.bytes.push_back(length);
      for (const auto &word : words) {
        dictionary[next] = TokenWords(word.begin(), word.end());
        context.bytes.push_back(next >> 8);
        context.bytes.push_back(next & 0xff);
        ++next;
      }
    }
    context.record_payload_ranges.push_back(
        {static_cast<std::uint32_t>(begin),
         static_cast<std::uint32_t>(context.bytes.size())});
    context.decoded_records.emplace_back();
  }

  void finish() {
    context.source_dictionary =
        std::make_shared<const std::map<std::uint16_t, TokenWords>>(dictionary);
    for (std::size_t i = 0; i < context.decoded_records.size(); ++i) {
      const auto sources = decode_logical_record_sources(context, i + 1, i + 2);
      require(sources.size() == 1, "synthetic source record did not decode");
      if (!sources.empty())
        context.decoded_records[i] =
            token_words_to_ascii(sources.front().assembled.words);
    }
  }

  // Minimal three-page container input for the streaming decoder: one token
  // map, one literal-only dictionary page, and one content page. All words
  // are encoded explicitly with CP500; no real book bytes are included.
  std::vector<std::uint8_t> serialized(geist::BooDirectory &directory) const {
    std::vector<std::uint8_t> bytes(3 * boo_page_size);
    std::copy_n(context.bytes.begin(), 512, bytes.begin());
    const auto put_word = [&](std::size_t at, std::size_t value) {
      bytes[at] = static_cast<std::uint8_t>(value >> 8);
      bytes[at + 1] = static_cast<std::uint8_t>(value & 0xff);
    };
    const auto append_length = [](std::vector<std::uint8_t> &out,
                                  std::size_t length) {
      if (length <= 0xef)
        out.push_back(static_cast<std::uint8_t>(length));
      else {
        out.push_back(static_cast<std::uint8_t>(0xf0 + (length >> 8)));
        out.push_back(static_cast<std::uint8_t>(length & 0xff));
      }
    };
    std::vector<std::uint8_t> top{0, 0, 0};
    for (const auto &[key, word] : dictionary) {
      std::vector<std::uint8_t> entry{static_cast<std::uint8_t>(key >> 8),
                                      static_cast<std::uint8_t>(key & 0xff),
                                      static_cast<std::uint8_t>(word.size())};
      for (const auto value : word) {
        bool found = false;
        for (std::size_t byte = 0; byte < 256; ++byte)
          if (EbcdicCodec::cp500().decode_word(
                  static_cast<std::uint8_t>(byte)) == value) {
            entry.push_back(static_cast<std::uint8_t>(byte));
            found = true;
            break;
          }
        require(found, "synthetic dictionary word is not CP500 encodable");
      }
      append_length(top, entry.size());
      top.insert(top.end(), entry.begin(), entry.end());
    }
    std::vector<std::uint8_t> dictionary_payload;
    append_length(dictionary_payload, top.size());
    dictionary_payload.insert(dictionary_payload.end(), top.begin(), top.end());
    put_word(boo_page_size, 0x0100);
    put_word(boo_page_size + 2, 4 + dictionary_payload.size());
    std::copy(dictionary_payload.begin(), dictionary_payload.end(),
              bytes.begin() + boo_page_size + 4);
    std::vector<std::uint8_t> content;
    for (const auto &range : context.record_payload_ranges) {
      append_length(content, range.end - range.begin);
      content.insert(content.end(), context.bytes.begin() + range.begin,
                     context.bytes.begin() + range.end);
    }
    put_word(2 * boo_page_size + 2, 4 + content.size());
    std::copy(content.begin(), content.end(),
              bytes.begin() + 2 * boo_page_size + 4);
    directory = context.directory;
    directory.dictionary_start_page = 2;
    directory.dictionary_page_count = 1;
    directory.content_start_page = 3;
    directory.content_page_count = 1;
    return bytes;
  }
};

void continuation_does_not_partition_the_catalogue() {
  Book book;
  book.context.decoded_records.resize(901);
  book.context.record_payload_ranges.resize(901, {512, 512});
  book.record({{"shBIBLIOGRAPHY.2"},
               {"ctopicn", "294"},
               {"chdlevel", ":H2"},
               {"ST", "Related Publications"}});
  book.record({{"           ", "OSI/File Services"}});
  book.record({{"           ", "Installation and Customization"}});
  book.record({{"           ", "Guide MVS,"}});
  book.record({{"           ", "SH19-6639"}});
  book.record({{"shINDEX"},
               {"ctopicn", "295"},
               {"chdlevel", ":INDEX"},
               {"ST", "Index"}});
  book.finish();
  const auto topics = build_topics(book.context, true);
  require(topics.size() == 2, "indented publication created a phantom topic");
  if (topics.size() < 2)
    return;
  require(topics.front().id == "BIBLIOGRAPHY.2" &&
              topics.front().start_logical_record == 902 &&
              topics.front().end_logical_record == 907 &&
              topics.front().raw_records.size() == 5 &&
              topics.front().raw_records.back().find("SH19-6639") !=
                  std::string::npos,
          "catalogue range lost the final publication continuation");
}

void standalone_and_nonnumeric_headers_remain_boundaries() {
  Book book;
  book.record({{"sh1.0"}});
  book.record({{"ctopicn", "1"}, {"chdlevel", ":H1"}, {"ST", "First"}});
  book.record({{"shAPPENDIX1"}});
  book.record({{"ctopicn", "2"}, {"chdlevel", ":H1"}, {"ST", "Appendix"}});
  book.record({{"shGLOSSARY"}});
  book.finish();
  const auto topics = build_topics(book.context, false);
  require(topics.size() == 3 && topics[0].id == "1.0" &&
              topics[0].end_logical_record == 3 &&
              topics[1].id == "APPENDIX1" && topics[1].topic_number == 2 &&
              topics[2].id == "GLOSSARY",
          "genuine standalone/nonnumeric headers were rejected");
}

void a_header_spelling_in_a_length_byte_is_not_a_boundary() {
  Book book;
  book.record({{"sh1.0"}});
  // Distinct line length keeps the real header prefix punctuation-only.
  book.record({{"ctopicn", "literal", "text"}}, "sh999");
  book.finish();
  const auto topics = build_topics(book.context, false);
  require(topics.size() == 1 && topics.front().id == "1.0" &&
              topics.front().end_logical_record == 3,
          "SH-looking native length byte became a topic boundary");
}

void undecided_framing_retains_legacy_lexical_admission() {
  Book book;
  book.record({{"sh1.0"}});
  const auto begin = book.context.bytes.size();
  // A bare two-byte token cannot frame as a complete native display row.
  book.dictionary[book.next] = {'S', 'H', '2', '.', '0'};
  book.context.bytes.push_back(book.next >> 8);
  book.context.bytes.push_back(book.next & 0xff);
  book.context.record_payload_ranges.push_back(
      {static_cast<std::uint32_t>(begin),
       static_cast<std::uint32_t>(book.context.bytes.size())});
  book.context.decoded_records.emplace_back();
  book.finish();
  const auto source = decode_logical_record_sources(book.context, 2, 3);
  require(!source.front().ir.display_lines_parse,
          "legacy fixture accidentally acquired native framing");
  const auto topics = build_topics(book.context, false);
  require(topics.size() == 2 && topics.back().id == "2.0",
          "undecided framing changed legacy catalogue admission");
}

void contents_stop_uses_the_same_source_identity() {
  Book book;
  book.record({{"shCONTENTS"},
               {"ctopicn", "1"},
               {"ctoce", "0", "1", "FIRST", "First"}});
  book.record({{"           ", "SH19-6639"}});
  book.record({{"ctoce", "0", "1", "SECOND", "Second"}});
  book.record({{"shINDEX"}});
  book.record({{"ctoce", "0", "1", "NOT_CONTENTS", "Outside"}});
  book.finish();
  const auto sources = decode_logical_record_sources(book.context, 1, 6);
  const auto toc = build_table_of_contents(book.context.decoded_records, {},
                                           false, nullptr, &sources);
  require(toc.size() == 2 && toc[0].id == "FIRST" && toc[1].id == "SECOND",
          "TOC stop scan disagrees with native topic-boundary identity");
}

void non_candidates_never_request_source_decoding() {
  LogicalDecodeContext context;
  context.decoded_records = {"ordinary continuation text"};
  context.record_payload_ranges = {{0, 1}};
  context.bytes = {0xff};
  require(build_topics(context, false).empty() &&
              !peek_cache(context.source_dictionary),
          "non-header record entered the source-decoding pipeline");
}

void header_collection_stops_only_at_a_genuine_topic() {
  Book book;
  book.record({{"           ", "SH19-6639"}});
  book.record({{"ctitle=StillHeader"}});
  book.record({{"sh1.0"}});
  book.record({{"cstitle=AfterTopic"}});
  geist::BooDirectory directory;
  const auto bytes = book.serialized(directory);
  std::vector<LogicalRecordTokenBoundaries> boundaries;
  const auto stopped = decode_experimental_logical_records(
      bytes, directory, nullptr, &boundaries, true);
  require(stopped.size() == 3 && boundaries.size() == 3 &&
              stopped[1].find("ctitle=StillHeader") != std::string::npos &&
              is_topic_header_record(stopped.back()),
          "indented SH prematurely terminated streaming header collection");
  const auto all = decode_experimental_logical_records(
      bytes, directory, nullptr, &boundaries, false);
  require(all.size() == 4 && boundaries.size() == 3,
          "header provenance continued past a genuine native SH boundary");
  const auto controls = extract_book_logical_controls(stopped, boundaries);
  bool title = false, after_topic = false;
  for (const auto &control : controls) {
    title =
        title || (control.key == "CTITLE" && control.value == "StillHeader");
    after_topic = after_topic || control.key == "CSTITLE";
  }
  require(title && !after_topic,
          "streaming header controls were lost or admitted after the topic");
}

void initial_native_admission_needs_no_second_dictionary_decode() {
  Book book;
  book.record({{"sh1.0"}});
  book.record({{"           ", "SH19-6639"}});
  book.record({{"ordinary", "continuation"}});
  book.record({{"shINDEX"}});
  const auto bare_begin = book.context.bytes.size();
  book.dictionary[book.next] = {'s', 'h', 'L', 'E', 'G', 'A', 'C', 'Y'};
  book.context.bytes.push_back(book.next >> 8);
  book.context.bytes.push_back(book.next & 0xff);
  book.context.record_payload_ranges.push_back(
      {static_cast<std::uint32_t>(bare_begin),
       static_cast<std::uint32_t>(book.context.bytes.size())});
  LogicalDecodeContext context;
  context.bytes = book.serialized(context.directory);
  context.decoded_records = decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges,
      nullptr, false, &context.topic_header_framing);
  require(context.topic_header_framing.size() == 4 &&
              context.topic_header_framing.at(0) == TopicHeaderFraming::native_origin &&
              context.topic_header_framing.at(1) == TopicHeaderFraming::non_native &&
              context.topic_header_framing.at(3) == TopicHeaderFraming::native_origin &&
              context.topic_header_framing.at(4) == TopicHeaderFraming::undecided,
          "initial token walk retained incorrect or noncandidate evidence");
  require(!peek_cache(context.source_dictionary),
          "initial compact decode populated the lazy source dictionary");
  require(is_topic_header_record(context, 0) &&
              !is_topic_header_record(context, 1) &&
              !is_topic_header_record(context, 2) &&
              is_topic_header_record(context, 3) &&
              is_topic_header_record(context, 4),
          "retained native admission differs from source-proven SH identity");
  require(!peek_cache(context.source_dictionary),
          "catalogue admission redundantly decoded the source dictionary");
}
} // namespace

int main() {
  continuation_does_not_partition_the_catalogue();
  standalone_and_nonnumeric_headers_remain_boundaries();
  a_header_spelling_in_a_length_byte_is_not_a_boundary();
  undecided_framing_retains_legacy_lexical_admission();
  contents_stop_uses_the_same_source_identity();
  non_candidates_never_request_source_decoding();
  header_collection_stops_only_at_a_genuine_topic();
  initial_native_admission_needs_no_second_dictionary_decode();
  return 0;
}
