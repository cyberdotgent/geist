#include "geist/detail/internal.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
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

void verify_book(const std::filesystem::path& path,
                 std::uint32_t first,
                 std::uint32_t end,
                 bool benchmark = false) {
  const auto started = std::chrono::steady_clock::now();
  geist::detail::LogicalDecodeContext context;
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base = static_cast<std::size_t>(directory_page) *
                    geist::boo_page_size;
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
  const auto opened = std::chrono::steady_clock::now();
  const auto open_rss = resident_kib();
  require(context.record_payload_ranges.size() ==
              context.decoded_records.size(),
          "payload index and decoded records diverged");
  require(context.record_payload_ranges.size() ==
              context.directory.logical_record_count,
          "payload index does not cover the directory record count");

  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto first_source = std::chrono::steady_clock::now();
  const auto source_rss = resident_kib();
  require(sources.size() == end - first,
          "candidate-local source slice has the wrong record count");
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string layout_error;
  require(geist::detail::verify_layout_ir(sources, layout, &layout_error),
          layout_error.empty() ? "layout IR verification failed"
                               : layout_error.c_str());
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto logical_record = first + index;
    require(sources[index].logical_record == logical_record,
            "source slice lost logical-record ownership");
    std::string ir_error;
    require(geist::detail::verify_token_ir(sources[index].ir, &ir_error),
            ir_error.empty() ? "token IR verification failed"
                             : ir_error.c_str());
    require(sources[index].ir.logical_record == logical_record,
            "token IR lost logical-record ownership");
    require(geist::detail::project_token_words(sources[index].ir) ==
                sources[index].tokens,
            "resolved token compatibility projection diverged from token IR");
    require(geist::detail::project_encoded_tokens(sources[index].ir) ==
                sources[index].encoded_tokens,
            "encoded token compatibility projection diverged from token IR");
    require(sources[index].encoded_tokens.size() ==
                sources[index].tokens.size(),
            "encoded and resolved token streams diverged");
    const auto& range = context.record_payload_ranges[logical_record - 1];
    auto payload_offset = static_cast<std::size_t>(range.begin);
    for (const auto& encoded : sources[index].encoded_tokens) {
      require(encoded.width == 1 || encoded.width == 2,
              "encoded token width is invalid");
      std::uint16_t value = context.bytes[payload_offset++];
      if (encoded.width == 2) {
        value = static_cast<std::uint16_t>(
            (value << 8) | context.bytes[payload_offset++]);
      }
      require(value == encoded.value,
              "encoded token identity differs from payload bytes");
    }
    require(payload_offset == range.end,
            "encoded tokens do not consume the exact payload slice");
    require(geist::detail::token_words_to_ascii(sources[index].assembled.words) ==
                context.decoded_records[logical_record - 1],
            "source assembly differs from initial record decode");
    std::string segment_error;
    require(geist::detail::verify_control_segments(
                sources[index].assembled, sources[index].control_segments,
                &segment_error),
            segment_error.empty() ? "control segment verification failed"
                                  : segment_error.c_str());
  }

  if (path.filename() == "SC31-711.boo" && first == 528) {
    std::size_t ansi_rows = 0;
    for (const auto& run : layout.runs) {
      for (const auto& row : run.rows) {
        if (row.marker && row.marker->decoded_text == "bridge" &&
            row.visible_text.find("American National Standards Institute") !=
                std::string::npos) {
          ++ansi_rows;
        }
      }
    }
    require(ansi_rows == 2,
            "layout IR did not preserve the two independent ANSI rows");
  }

  const auto* cached_dictionary = context.source_dictionary.get();
  const auto repeated =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto repeated_source = std::chrono::steady_clock::now();
  require(context.source_dictionary.get() == cached_dictionary &&
              repeated.size() == sources.size(),
          "repeated source query did not reuse dictionary state");
  if (benchmark) {
    const auto millis = [](auto begin, auto finish) {
      return std::chrono::duration_cast<std::chrono::microseconds>(finish -
                                                                   begin)
                 .count() /
             1000.0;
    };
    std::cout << path.filename().string()
              << " open_ms=" << millis(started, opened)
              << " first_source_ms=" << millis(opened, first_source)
              << " repeat_source_ms=" << millis(first_source,
                                                  repeated_source)
              << " open_rss_kib=" << open_rss
              << " source_rss_kib=" << source_rss
              << " index_bytes="
              << context.record_payload_ranges.size() *
                     sizeof(geist::detail::LogicalRecordPayloadRange)
              << "\n";
  }
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const bool benchmark = std::getenv("GEIST_BENCH_SOURCE_INDEX") != nullptr;
  verify_book(root / "SC31-711.boo", 19, 21, benchmark);
  verify_book(root / "SC31-711.boo", 22, 24, benchmark);
  verify_book(root / "SC31-711.boo", 528, 529, benchmark);
  verify_book(root / "SG24-204.boo", 1, 2, benchmark);
}
