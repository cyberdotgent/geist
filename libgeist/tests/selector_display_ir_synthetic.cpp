#include "geist/detail/internal.hpp"
#include "geist/detail/selector_display_ir.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

DecodedLogicalRecordSource make_source(std::uint32_t logical_record,
                                       std::vector<TokenWords> tokens) {
  DecodedLogicalRecordSource source;
  source.logical_record = logical_record;
  source.tokens = std::move(tokens);
  source.encoded_tokens.resize(source.tokens.size());
  source.ir.logical_record = logical_record;
  auto byte = static_cast<std::uint32_t>(logical_record * 100);
  for (std::size_t index = 0; index < source.tokens.size(); ++index) {
    source.encoded_tokens[index] = {static_cast<std::uint16_t>(0x20 + index),
                                    1};
    source.ir.tokens.push_back({index,
                                source.encoded_tokens[index],
                                source.tokens[index],
                                {byte, static_cast<std::uint32_t>(byte + 1)},
                                false,
                                3});
    ++byte;
  }
  source.ir.payload_range = {static_cast<std::uint32_t>(logical_record * 100),
                             byte};
  source.assembled =
      geist::detail::assemble_logical_record_with_sources(source.tokens);
  source.control_segments = geist::detail::decode_control_segments(
      source.logical_record, source.assembled);
  return source;
}

struct Pipeline {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::SelectorCatalogIR selectors;
  geist::detail::LayoutIR layout;
  geist::detail::OwnershipIR ownership;
};

Pipeline pipeline(std::vector<DecodedLogicalRecordSource> sources) {
  Pipeline result;
  result.sources = std::move(sources);
  std::string error;
  const auto selectors =
      geist::detail::extract_selector_catalog_ir(result.sources, &error);
  require(selectors.has_value(), "raw selector extraction failed: " + error);
  result.selectors = *selectors;
  result.layout = geist::detail::extract_layout_ir(result.sources);
  result.ownership =
      geist::detail::build_ownership_ir(result.sources, result.layout);
  require(
      geist::detail::verify_layout_ir(result.sources, result.layout, &error),
      "synthetic layout verification failed: " + error);
  require(geist::detail::verify_ownership_ir(result.sources, result.layout,
                                             result.ownership, &error),
          "synthetic ownership verification failed: " + error);
  return result;
}

std::optional<geist::detail::SelectorDisplayIR>
extract(const Pipeline &value, std::string *error = nullptr) {
  return geist::detail::extract_selector_display_ir(
      value.sources, value.selectors, value.layout, value.ownership, error);
}

std::vector<TokenWords> inline_selector(const std::string &operands,
                                        const std::string &text) {
  return {words("cselect " + operands), words("marker"), words("   "),
          words(text)};
}

void verify_inline_and_provenance() {
  const auto value =
      pipeline({make_source(10, inline_selector("3 4 HDR", "ABCD"))});
  std::string error;
  const auto display = extract(value, &error);
  require(display && display->rows.size() == 1 &&
              display->bindings.size() == 1 &&
              display->rows[0].association ==
                  geist::detail::SelectorRowAssociation::inline_payload &&
              display->rows[0].spans[0].cell_begin == 3 &&
              display->rows[0].spans[0].cell_end == 7,
          "inline selector did not bind exact native cells: " + error);
  require(display && display->rows[0].cells.size() == 7 &&
              std::all_of(display->rows[0].cells.begin(),
                          display->rows[0].cells.end(),
                          [](const auto &cell) {
                            return cell.source.has_value() &&
                                   cell.source->token_bytes.end ==
                                       cell.source->token_bytes.begin + 1;
                          }),
          "inline selector row lost source-cell byte provenance");
  require(display && geist::detail::verify_selector_display_ir(
                         value.sources, value.selectors, value.layout,
                         value.ownership, *display, &error),
          "inline selector display verifier failed: " + error);
  require(display && geist::detail::format_selector_display_ir(*display).find(
                         "association=inline") != std::string::npos,
          "selector display trace omitted association");
  if (display) {
    auto mutated = *display;
    mutated.rows[0].spans[0].cell_end = 6;
    require(!geist::detail::verify_selector_display_ir(
                value.sources, value.selectors, value.layout, value.ownership,
                mutated),
            "selector display verifier admitted mutated geometry");
  }
}

void verify_same_record_deferred() {
  auto source =
      make_source(20, {words("cselect 3 4 HDR"), words("cfont 3 4 2"),
                       words("fontmark"), words("   "), words("ABCD")});
  const auto value = pipeline({std::move(source)});
  std::string error;
  const auto display = extract(value, &error);
  require(display && display->rows.size() == 1 &&
              display->rows[0].association ==
                  geist::detail::SelectorRowAssociation::deferred_same_record &&
              display->rows[0].owner.segment_index == 1,
          "same-record CFONT continuation did not consume selector: " + error);
}

void verify_next_record_deferred() {
  const auto value = pipeline(
      {make_source(30, {words("cselect 3 4 HDR")}),
       make_source(31, {words("rowmark"), words("   "), words("ABCD")})});
  std::string error;
  const auto display = extract(value, &error);
  require(display && display->rows.size() == 1 &&
              display->rows[0].association ==
                  geist::detail::SelectorRowAssociation::deferred_next_record &&
              display->rows[0].owner.logical_record == 31,
          "adjacent-record display row did not consume selector: " + error);
}

void verify_source_proven_contiguous_marker_restoration() {
  const auto value =
      pipeline({make_source(32, {words("cselect 6 4 HDR")}),
                make_source(33, {words("/"), words("   "), words("ABC"),
                                 words("."), words(" "), words("DEFG")})});
  std::string error;
  const auto display = extract(value, &error);
  const auto restored =
      display
          ? std::find_if(display->rows[0].cells.begin(),
                         display->rows[0].cells.end(),
                         [](const auto &cell) {
                           return cell.origin ==
                                      geist::detail::SelectorDisplayCellOrigin::
                                          restored_native_marker &&
                                  cell.word == '.';
                         })
          : std::vector<geist::detail::SelectorDisplayCellIR>::const_iterator{};
  require(
      display && display->rows.size() == 1 &&
          display->rows[0].cells.size() >= 10 &&
          display->rows[0].owner.token_begin == 0 &&
          display->rows[0].owner.token_end == 6 &&
          restored != display->rows[0].cells.end() && restored->word == '.' &&
          restored->source.has_value() && restored->source->token_index == 3 &&
          std::any_of(display->rows[0].cells.begin(),
                      display->rows[0].cells.end(),
                      [](const auto &cell) {
                        return cell.origin ==
                                   geist::detail::SelectorDisplayCellOrigin::
                                       restored_native_margin &&
                               cell.source.has_value();
                      }),
      "contiguous source marker was not restored with provenance: " + error +
          (display ? "\n" + geist::detail::format_selector_display_ir(*display)
                   : ""));
  require(display && geist::detail::verify_selector_display_ir(
                         value.sources, value.selectors, value.layout,
                         value.ownership, *display, &error),
          "restored selector display verifier failed: " + error);

  const auto noncontiguous = pipeline(
      {make_source(34, {words("cselect 6 4 HDR")}),
       make_source(35, {words("/"), words("   "), words("ABC"), words("ST"),
                        words("."), words(" "), words("DEFG")})});
  require(!extract(noncontiguous, &error),
          "selector restoration crossed an intervening typed control");
}

void verify_multiple_queued() {
  const auto value = pipeline(
      {make_source(40, {words("cselect 3 2 SAME"), words("cselect 5 2 SAME"),
                        words("rowmark"), words("   "), words("AABB")})});
  std::string error;
  const auto display = extract(value, &error);
  require(display && display->rows.size() == 1 &&
              display->rows[0].association ==
                  geist::detail::SelectorRowAssociation::multiple_queued &&
              display->rows[0].spans.size() == 2 &&
              display->rows[0].spans[0].target.raw_target == "SAME" &&
              display->rows[0].spans[1].target.raw_target == "SAME" &&
              display->rows[0].spans[0].selector.segment_index !=
                  display->rows[0].spans[1].selector.segment_index &&
              display->bindings.size() == 2 &&
              display->bindings[0].owner_id == display->bindings[1].owner_id,
          "multiple queued selectors did not bind one ordered row: " + error);
}

void verify_non_ascii_is_one_source_cell() {
  const auto value =
      pipeline({make_source(45, {words("cselect 3 1 HDR"), words("rowmark"),
                                 words("   "), TokenWords{0x00e9}})});
  std::string error;
  const auto display = extract(value, &error);
  require(display && display->rows.size() == 1 &&
              display->rows[0].cells.size() == 4 &&
              display->rows[0].cells[3].word == 0x00e9 &&
              display->rows[0].spans[0].cell_end == 4,
          "non-ASCII source word did not remain one native display cell: " +
              error);
}

void verify_exact_nonrow_dispositions() {
  const auto objects =
      pipeline({make_source(50, inline_selector("3 4 PIC12", "ABCD")),
                make_source(51, inline_selector("3 4 LNK", "ABCD"))});
  std::string error;
  const auto object_display = extract(objects, &error);
  require(
      object_display && object_display->rows.empty() &&
          object_display->objects.size() == 2 &&
          object_display->bindings.size() == 2 &&
          std::all_of(
              object_display->bindings.begin(), object_display->bindings.end(),
              [](const auto &binding) {
                return binding.kind ==
                       geist::detail::SelectorBindingKind::resource_object;
              }),
      "PIC/LNK selectors were not explicitly conserved as objects: " + error);

  const auto table = pipeline({make_source(
      60, {words("SRTBLTEST"), words("cselect 3 4 HDR"), words("rowmark"),
           words("   "), words("ABCD"), words("SRETBL")})});
  const auto table_display = extract(table, &error);
  require(
      table_display && table_display->bindings.size() == 1 &&
          table_display->bindings[0].kind ==
              geist::detail::SelectorBindingKind::table_owned &&
          table_display->bindings[0].owner_id == 0,
      "table selector did not receive an explicit table-owned disposition: " +
          error);
}

void verify_fail_closed_cases() {
  std::string error;
  const auto unresolved =
      pipeline({make_source(70, {words("cselect 3 4 HDR")})});
  require(!extract(unresolved, &error) &&
              error.find("unresolved") != std::string::npos,
          "unresolved selector did not fail at EOF");

  const auto geometry =
      pipeline({make_source(80, inline_selector("8 4 HDR", "ABCD"))});
  require(!extract(geometry, &error) &&
              error.find("shorter") != std::string::npos,
          "out-of-range selector geometry was admitted");

  const auto overlap = pipeline(
      {make_source(90, {words("cselect 3 3 FIRST"), words("cselect 5 2 SECOND"),
                        words("rowmark"), words("   "), words("AABB")})});
  require(!extract(overlap, &error) &&
              error.find("overlap") != std::string::npos,
          "overlapping queued selectors were admitted");

  const auto barrier =
      pipeline({make_source(100, {words("cselect 3 4 HDR")}),
                make_source(101, {words("ST"), words("titlemark"), words("   "),
                                  words("ABCD")})});
  require(!extract(barrier, &error) &&
              error.find("barrier") != std::string::npos,
          "selector crossed a typed title barrier");

  const auto gap = pipeline(
      {make_source(110, {words("cselect 3 4 HDR")}),
       make_source(112, {words("rowmark"), words("   "), words("ABCD")})});
  require(!extract(gap, &error) &&
              error.find("nonadjacent") != std::string::npos,
          "selector crossed a nonadjacent logical-record gap");

  auto malformed =
      pipeline({make_source(120, inline_selector("+3 4 HDR", "ABCD"))});
  require(!extract(malformed, &error) &&
              error.find("noncanonical") != std::string::npos,
          "noncanonical raw selector entered display IR");
}

#ifdef GEIST_REPO_ROOT
void verify_sc31_native_continuation() {
  const auto path =
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC31-711.boo";
  const auto document = geist::BooDocument::open(path);
  const auto found =
      std::find_if(document.topics().begin(), document.topics().end(),
                   [](const auto &topic) { return topic.id == "5.0"; });
  require(found != document.topics().end(), "SC31 fixture has no 5.0 topic");

  geist::detail::LogicalDecodeContext context;
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

  const auto sources = geist::detail::decode_logical_record_sources(
      context, found->start_logical_record, found->end_logical_record);
  const auto selectors = geist::detail::extract_selector_catalog_ir(sources);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  std::string error;
  const auto display = selectors
                           ? geist::detail::extract_selector_display_ir(
                                 sources, *selectors, layout, ownership, &error)
                           : std::nullopt;
  std::string selected_text;
  if (display && !display->rows.empty() && !display->rows[0].spans.empty()) {
    const auto &span = display->rows[0].spans[0];
    for (auto cell = span.cell_begin;
         cell < span.cell_end && cell < display->rows[0].cells.size(); ++cell)
      if (display->rows[0].cells[cell].word <= 0xff)
        selected_text.push_back(
            static_cast<char>(display->rows[0].cells[cell].word));
  }
  require(display && display->rows.size() == 2 &&
              display->bindings.size() == 2 &&
              display->rows[0].owner.logical_record == 173 &&
              display->rows[0].owner.segment_index == 0 &&
              display->rows[0].owner.token_begin == 0 &&
              display->rows[0].owner.token_end == 20 &&
              display->rows[0].spans[0].cell_begin == 56 &&
              display->rows[0].spans[0].cell_end == 75 &&
              selected_text == "Chapter 2, \"Problem" &&
              std::count_if(display->rows[0].cells.begin(),
                            display->rows[0].cells.end(),
                            [](const auto &cell) {
                              return cell.origin ==
                                     geist::detail::SelectorDisplayCellOrigin::
                                         restored_native_marker;
                            }) == 2,
          "SC31 5.0 native selector continuation was not admitted exactly: " +
              error + " selected='" + selected_text + "'");
  require(display &&
              geist::detail::verify_selector_display_ir(
                  sources, *selectors, layout, ownership, *display, &error),
          "SC31 5.0 selector continuation failed verification: " + error);
}
#endif

} // namespace

int main() {
  verify_inline_and_provenance();
  verify_same_record_deferred();
  verify_next_record_deferred();
  verify_source_proven_contiguous_marker_restoration();
  verify_multiple_queued();
  verify_non_ascii_is_one_source_cell();
  verify_exact_nonrow_dispositions();
  verify_fail_closed_cases();
#ifdef GEIST_REPO_ROOT
  verify_sc31_native_continuation();
#endif
}
