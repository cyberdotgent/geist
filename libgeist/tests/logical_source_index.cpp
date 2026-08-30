// Logical-record source decoding: the payload index, the source slice, the
// layout and ownership IRs it feeds, and the directory's paged topic-start
// index (issue #58).
//
// This used to sweep record ranges of eighteen books, most of them chosen to
// pin one publication-catalog admission or rejection.  Only packet.boo may be
// redistributed, so the publication catalog family -- which packet does not
// carry at all -- is no longer exercised here beyond the negative: no range of
// packet may enter publication IR (issue #59).  What survives is the part that
// was never book-specific: every source slice of the whole book must decode,
// verify its layout, build a verified ownership ledger, and stay cheap.

#include "geist/detail/internal.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << "\n";
    geist_test::record_failure();
  }
}

std::size_t resident_kib() {
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmRSS:") {
      std::size_t value = 0;
      status >> value;
      return value;
    }
    std::getline(status, key);
  }
  return 0;
}

void open_context(const std::filesystem::path &path,
                  geist::detail::LogicalDecodeContext &context) {
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  context.directory.token_map_offset =
      geist::detail::read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      geist::detail::read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      geist::detail::read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      geist::detail::read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count =
      geist::detail::read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page =
      geist::detail::read_be16(context.bytes, base + 0x3a);
  context.decoded_records = geist::detail::decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
}

// One source slice: it decodes, its layout verifies, its ownership ledger
// verifies, and it does not enter publication IR (packet carries no
// publication catalog).
void verify_slice(geist::detail::LogicalDecodeContext &context,
                  std::uint32_t first, std::uint32_t end,
                  const std::string &label) {
  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  require(sources.size() == end - first,
          label + ": source slice has the wrong record count");
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string layout_error;
  require(geist::detail::verify_layout_ir(sources, layout, &layout_error),
          label + ": layout IR verification failed: " + layout_error);
  std::string ownership_error;
  const auto ownership = geist::detail::build_verified_ownership_ir(
      sources, layout, &ownership_error);
  require(ownership.has_value(),
          label + ": ownership IR verification failed: " + ownership_error);
  if (!ownership)
    return;
  const auto publication = geist::detail::extract_publication_catalog_ir(
      sources, layout, *ownership);
  require(!publication.has_value(),
          label + ": packet prose entered publication catalog IR");
}

} // namespace

int main() {
  const auto path = std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
  const auto document = geist::BooDocument::open(path);

  const auto started = std::chrono::steady_clock::now();
  geist::detail::LogicalDecodeContext context;
  open_context(path, context);
  const auto opened = std::chrono::steady_clock::now();
  const auto open_rss = resident_kib();

  // The payload index covers exactly the directory's record count, one entry
  // per decoded record.
  require(context.record_payload_ranges.size() == context.decoded_records.size(),
          "payload index and decoded records diverged");
  require(context.record_payload_ranges.size() ==
              context.directory.logical_record_count,
          "payload index does not cover the directory record count");

  // Every topic's own record range decodes and verifies.
  const auto first_source = std::chrono::steady_clock::now();
  for (const auto &topic : document.topics()) {
    verify_slice(context, topic.start_logical_record,
                 topic.end_logical_record, "packet " + topic.id);
  }
  const auto sources_done = std::chrono::steady_clock::now();
  const auto source_rss = resident_kib();

  // Single-record slices across the whole book, including the records no
  // topic claims.
  for (std::uint32_t record = 1;
       record < context.directory.logical_record_count; ++record) {
    verify_slice(context, record, record + 1,
                 "packet lr" + std::to_string(record));
  }

  // Repeating a query reuses the decoded dictionary rather than rebuilding it.
  const auto *cached_dictionary = context.source_dictionary.get();
  const auto repeated = geist::detail::decode_logical_record_sources(
      context, 25, 31);
  const auto repeated_source = std::chrono::steady_clock::now();
  require(context.source_dictionary.get() == cached_dictionary &&
              repeated.size() == 6,
          "repeated source query did not reuse dictionary state");

  // The directory topic-start index root holds at most 248 values and
  // continues in a table on the logical page named by its second word
  // (Format/topics.md).  packet's 124 topics fit in the root, so this pins the
  // un-paged shape; the paged continuation shape went with the larger books
  // (issue #59).
  const auto bytes = geist::detail::read_file(path);
  const auto starts =
      geist::detail::parse_topic_record_starts(bytes, document.directory());
  require(starts.size() == 125 && document.directory().stream_table_count == 124,
          "topic-start index did not yield every topic start");
  if (starts.size() == 125) {
    require(std::is_sorted(starts.begin(), starts.end()) &&
                starts.back() ==
                    document.directory().logical_record_count + 1,
            "topic-start index is not a monotonic record sequence");
    require(starts.size() - 1 <= 248,
            "packet should not need a paged topic-start index");
  }
  std::size_t missing = 0;
  for (const auto &topic : document.topics())
    if (std::find(starts.begin(), starts.end(), topic.start_logical_record) ==
        starts.end())
      ++missing;
  require(missing == 0,
          "a decoded topic header is not in the topic-start index");

  const auto millis = [](auto begin, auto finish) {
    return std::chrono::duration_cast<std::chrono::microseconds>(finish - begin)
               .count() /
           1000.0;
  };
  std::cout << "packet.boo"
            << " open_ms=" << millis(started, opened)
            << " all_topics_ms=" << millis(first_source, sources_done)
            << " repeat_source_ms=" << millis(sources_done, repeated_source)
            << " open_rss_kib=" << open_rss
            << " source_rss_kib=" << source_rss << " index_bytes="
            << context.record_payload_ranges.size() *
                   sizeof(geist::detail::LogicalRecordPayloadRange)
            << "\n";

  geist_test::exit_with_failures();
  return 0;
}
