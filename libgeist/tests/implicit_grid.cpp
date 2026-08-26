#include "geist/detail/implicit_grid.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

void open_context(const std::filesystem::path& path,
                  geist::detail::LogicalDecodeContext& context) {
  context.bytes = geist::detail::read_file(path);
  const auto page = geist::detail::read_be16(context.bytes, 0);
  const auto base = static_cast<std::size_t>(page) * geist::boo_page_size;
  context.directory.page_number = page;
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

bool contains(const std::vector<std::vector<std::string>>& rows,
              const std::string& key,
              const std::string& value) {
  for (const auto& row : rows) {
    if (row.size() == 2 && row[0] == key &&
        row[1].find(value) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool contains_fragment(const std::vector<std::vector<std::string>>& rows,
                       const std::string& fragment) {
  for (const auto& row : rows) {
    for (const auto& cell : row) {
      if (cell.find(fragment) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

geist::detail::DecodedLogicalRecordSource synthetic_b_rows() {
  geist::detail::DecodedLogicalRecordSource record;
  record.logical_record = 1;
  for (int row = 0; row < 6; ++row) {
    const auto append = [&](std::uint16_t encoded,
                            geist::detail::TokenWords words) {
      record.encoded_tokens.push_back({encoded, 1});
      record.tokens.push_back(std::move(words));
    };
    append(0x01, {1, '.'});
    append(0x00, {1});
    append(0x12, {'>'});
    append(0x09, {' ', ' ', ' '});
    append(0x40 + row,
           {'k', 'e', 'y', static_cast<std::uint16_t>('0' + row)});
    append(0x50 + row,
           geist::detail::TokenWords(11, static_cast<std::uint16_t>(' ')));
    append(0x60 + row,
           {'v', 'a', 'l', static_cast<std::uint16_t>('0' + row)});
  }
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  return record;
}

} // namespace

int main() {
  const auto synthetic = synthetic_b_rows();
  const auto synthetic_markers =
      geist::detail::source_row_markers({synthetic}, 3);
  require(synthetic_markers.size() == 6 &&
              synthetic_markers.front().marker == ">" &&
              synthetic_markers.front().following_text == "key0",
          "source row marker ownership was not retained");
  require(geist::detail::source_row_markers({synthetic}, 4).empty(),
          "wrong column origin fabricated source row markers");
  const auto synthetic_grid = geist::detail::extract_implicit_grid(
      {synthetic}, {{3, 3}, {7, 4}, {18, 5}});
  require(synthetic_grid && synthetic_grid->semantic_rows.size() == 6 &&
              contains(synthetic_grid->semantic_rows, "key5", "val5"),
          "repeated source-owned B rows were not classified");
  require(!geist::detail::extract_implicit_grid(
               {synthetic}, {{3, 3}, {7, 4}, {12, 5}}),
          "single-group CFONT heading activated an implicit grid");
  const std::string synthetic_header =
      "cfont 3 3 2 7 4 2 18 5 2    Key Col        Value";
  const auto synthetic_rendered =
      geist::detail::render_gml_records_with_source_layout(
          {synthetic_header}, {synthetic});
  require(std::find(synthetic_rendered.begin(), synthetic_rendered.end(),
                    ":table cols='2'.") != synthetic_rendered.end(),
          "proven tail-owned implicit grid was not rendered");
  const auto guarded_tail =
      geist::detail::render_gml_records_with_source_layout(
          {synthetic_header, "SI semantic tail"}, {synthetic});
  require(std::find(guarded_tail.begin(), guarded_tail.end(),
                    ":table cols='2'.") == guarded_tail.end(),
          "implicit grid suppressed a later semantic control");

  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  geist::detail::LogicalDecodeContext sc31;
  open_context(root / "SC31-711.boo", sc31);

  const auto trademark_sources =
      geist::detail::decode_logical_record_sources(sc31, 10, 11);
  const auto trademark_segments =
      geist::detail::split_decoded_markup_segment_spans(
          sc31.decoded_records[9]);
  const auto trademark_segment = std::find_if(
      trademark_segments.begin(), trademark_segments.end(), [](const auto& span) {
        return span.text.rfind("cfont 3 4 1", 0) == 0;
      });
  require(trademark_segment != trademark_segments.end(),
          "missing SC31 terminal CFONT segment span");
  const auto trademark_grid = geist::detail::extract_terminal_styled_grid(
      trademark_sources.front(), *trademark_segment,
      {{3, 4}, {20, 9}, {30, 2}}, {"Term", "Trademark of"});
  require(trademark_grid && trademark_grid->semantic_rows.size() == 2 &&
              contains(trademark_grid->semantic_rows, "DynaText",
                       "Electronic Book Technologies, Inc.") &&
              contains(trademark_grid->semantic_rows, "Motif",
                       "Open Software Foundation, Inc."),
          "SC31 terminal styled grid lost its exact source rows");
  auto two_byte_origin = trademark_sources.front();
  two_byte_origin.encoded_tokens[273].width = 2;
  require(!geist::detail::extract_terminal_styled_grid(
              two_byte_origin, *trademark_segment,
              {{3, 4}, {20, 9}, {30, 2}}, {"Term", "Trademark of"}),
          "two-byte row-origin lookalike activated a terminal grid");
  auto two_byte_padding = trademark_sources.front();
  two_byte_padding.encoded_tokens[272].width = 2;
  require(!geist::detail::extract_terminal_styled_grid(
              two_byte_padding, *trademark_segment,
              {{3, 4}, {20, 9}, {30, 2}}, {"Term", "Trademark of"}),
          "two-byte padding lookalike activated a terminal grid");
  auto nonterminal = *trademark_segment;
  --nonterminal.output_end;
  require(!geist::detail::extract_terminal_styled_grid(
              trademark_sources.front(), nonterminal,
              {{3, 4}, {20, 9}, {30, 2}}, {"Term", "Trademark of"}),
          "nonterminal styled segment activated a grid");
  require(!geist::detail::extract_terminal_styled_grid(
              trademark_sources.front(), *trademark_segment,
              {{3, 4}, {21, 9}, {31, 2}}, {"Term", "Trademark of"}) &&
              !geist::detail::extract_terminal_styled_grid(
                  trademark_sources.front(), *trademark_segment,
                  {{3, 4}, {20, 9}, {30, 2}, {45, 3}},
                  {"Term", "Trademark of"}),
          "shifted or three-group header geometry activated a terminal grid");

  const auto directories = geist::detail::extract_implicit_grid(
      geist::detail::decode_logical_record_sources(sc31, 19, 21),
      {{3, 9}, {28, 4}, {33, 2}, {36, 5}});
  require(directories.has_value(), "SC31 directory grid was not classified");
  require(directories->semantic_rows.size() == 18,
          "SC31 directory grid has the wrong semantic row count");
  require(contains(directories->semantic_rows, "/usr/lpp/lnm/gifs",
                   "GIF files"),
          "SC31 directory grid lost its GIF row");
  require(contains(directories->semantic_rows, "/usr/lpp/lnm/reports",
                   "history files"),
          "SC31 directory continuation lost its row ownership");
  require(!contains_fragment(directories->semantic_rows, "GIF files=") &&
              !contains_fragment(directories->semantic_rows,
                                 "Registration filesaddress"),
          "SC31 directory grid retained structural row markers");

  const auto lr21 = geist::detail::extract_implicit_grid(
      geist::detail::decode_logical_record_sources(sc31, 21, 22),
      {{3, 9}, {23, 4}, {28, 2}, {31, 5}});
  require(lr21.has_value() && lr21->owns_source_tail,
          "SC31 LR21 implicit grid lost source-tail ownership");
  require(lr21->semantic_rows.size() == 9,
          "SC31 LR21 implicit grid has the wrong semantic row count");
  require(contains(lr21->semantic_rows, "/usr/OV/help/C/lnm",
                   "LNM for AIX help files"),
          "SC31 LR21 implicit grid lost its final row");

  const auto processes = geist::detail::extract_implicit_grid(
      geist::detail::decode_logical_record_sources(sc31, 22, 24),
      {{3, 7}, {11, 4}, {18, 11}});
  require(processes.has_value(), "SC31 process grid was not classified");
  if (processes && processes->semantic_rows.size() != 19) {
    std::cerr << "process rows=" << processes->semantic_rows.size() << "\n";
    for (const auto& row : processes->semantic_rows) {
      std::cerr << "  " << row[0] << " => " << row[1] << "\n";
    }
  }
  require(processes->semantic_rows.size() == 19,
          "SC31 process grid has the wrong semantic row count");
  require(contains(processes->semantic_rows, "lnmhubint", "Hub Manager"),
          "SC31 process continuation lost its row ownership");
  require(!contains_fragment(processes->semantic_rows, "LNM )") &&
              !contains_fragment(processes->semantic_rows, "????????"),
          "SC31 process grid retained structural row markers");

  const auto messages = geist::detail::extract_implicit_grid(
      geist::detail::decode_logical_record_sources(sc31, 160, 164),
      {{3, 3}, {7, 3}, {11, 3}, {15, 9}});
  require(!messages.has_value(),
          "ordinary SC31 message prose was classified as an implicit grid");

  geist::detail::LogicalDecodeContext gg24;
  open_context(root / "GG24-4302-00.boo", gg24);
  const auto headings = geist::detail::extract_implicit_grid(
      geist::detail::decode_logical_record_sources(gg24, 35, 43),
      {{7, 4}, {12, 3}, {16, 11}, {28, 7}});
  require(!headings.has_value(),
          "GG24 run-in headings were classified as an implicit grid");
}
