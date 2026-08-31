#include "geist/detail/source_rows.hpp"
#include "geist/detail/display_lines.hpp"
#include "test_failures.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
    return;
  }
}

void append(geist::detail::DecodedLogicalRecordSource& record,
            std::uint16_t encoded,
            std::uint8_t width,
            geist::detail::TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
}

void refresh_typed_source(geist::detail::DecodedLogicalRecordSource& record) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  record.ir.tokens.clear();
  std::uint32_t byte = 0;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    const auto has_spacing = !record.tokens[token].empty() &&
                             record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)},
         has_spacing,
         has_spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled);
}

geist::detail::DecodedLogicalRecordSource first_record() {
  geist::detail::DecodedLogicalRecordSource record;
  record.logical_record = 17;
  append(record, 0x31, 1, {'?','?','?'});
  append(record, 0x09, 1, {' ',' ',' '});
  append(record, 0x80, 2, {'a','l','p','h','a'});
  // Visually identical to a marker, but dictionary-owned. It must remain in
  // the first row rather than manufacturing a boundary.
  append(record, 0x1234, 2, {'|'});
  append(record, 0x09, 1, {' ',' ',' '});
  append(record, 0x81, 2, {'s','t','i','l','l'});
  append(record, 0x32, 1, {'|'});
  append(record, 0x0b, 1, {' ',' ',' ',' ',' '});
  append(record, 0x82, 2, {'w','r','a','p'});
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  return record;
}

geist::detail::DecodedLogicalRecordSource second_record() {
  geist::detail::DecodedLogicalRecordSource record;
  record.logical_record = 18;
  append(record, 0x33, 1, {'>'});
  append(record, 0x09, 1, {' ',' ',' '});
  append(record, 0x83, 2, {'b','e','t','a'});
  // Four spaces are not one of the requested stable origins.
  append(record, 0x34, 1, {'>'});
  append(record, 0x0a, 1, {' ',' ',' ',' '});
  append(record, 0x84, 2, {'n','o','t','-','a','-','r','o','w'});
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  return record;
}

geist::detail::DecodedLogicalRecordSource st_record() {
  geist::detail::DecodedLogicalRecordSource record;
  record.logical_record = 23;
  append(record, 0x90, 2, {3, 'S','T'});
  append(record, 0x08, 1, {' ', ' '});
  append(record, 0x94, 2, {'T','i','t','l',0x00e9});
  append(record, 0x12, 1, geist::detail::TokenWords(18, ' '));
  append(record, 0x09, 1, {' ', ' ', ' '});
  append(record, 0x91, 2, {'F','i','r','s','t',' ','r','o','w'});
  append(record, 0x13, 1, {'<'});
  append(record, 0x09, 1, {' ', ' ', ' '});
  append(record, 0x92, 2, {'c','o','n','t','i','n','u','e','d'});
  append(record, 0x20, 1, {'a','g','e','n','t'});
  append(record, 0x09, 1, {' ', ' ', ' '});
  append(record, 0x93, 2, {'f','i','n','i','s','h','e','d'});
  refresh_typed_source(record);
  return record;
}

} // namespace

int main() {
  const std::vector<geist::detail::DecodedLogicalRecordSource> records =
      {first_record(), second_record()};
  const auto rows = geist::detail::slice_fixed_source_rows(records, 3, {5});
  require(rows.size() == 3, "fixed source rows were not sliced across records");
  require(rows[0].logical_record == 17 && rows[0].origin == 3 &&
              !rows[0].continuation && rows[0].text.find("alpha") !=
                  std::string::npos &&
              rows[0].text.find("still") != std::string::npos,
          "two-byte lookalike was not retained in its physical row");
  require(rows[0].marker.text == "???" &&
              rows[0].marker.token_index == 0 &&
              rows[0].marker.origin_token == 1 &&
              rows[0].marker.encoded_value == 0x31 &&
              rows[0].marker.evidence ==
                  geist::detail::SourceRowBoundaryEvidence::question_run,
          "question-run marker provenance was lost");
  require(rows[1].continuation && rows[1].origin == 5 &&
              rows[1].text.find("wrap") != std::string::npos &&
              rows[1].marker.evidence ==
                  geist::detail::SourceRowBoundaryEvidence::separator,
          "separator-evidenced continuation was not retained");
  require(rows[2].logical_record == 18 && rows[2].marker.text == ">" &&
              rows[2].text.find("not-a-row") != std::string::npos,
          "record-local tail was not assigned to the last physical row");

  const auto markers = geist::detail::source_row_markers(records, 3);
  require(markers.size() == 2 && markers[0].following_text == "alpha" &&
              markers[0].provenance.logical_record == 17 &&
              markers[1].following_text == "beta",
          "compatibility marker view did not reuse slicer provenance");
  const auto st = st_record();
  const auto st_decoded = geist::detail::token_words_to_ascii(st.assembled.words);
  const std::vector<geist::detail::DecodedLogicalRecordSource> st_sources{st};
  const auto st_layout = geist::detail::extract_layout_ir(st_sources);
  const auto st_ownership =
      geist::detail::build_verified_ownership_ir(st_sources, st_layout);
  std::string st_error;
  require(st_ownership.has_value(),
          "synthetic ST ownership ledger is not verifiable");
  const auto st_ir = geist::detail::extract_fixed_prose_ir(
      st_sources, st_layout, *st_ownership, &st_error);
  require(st_ir && st_ir->title == u8"Titlé" &&
              st_ir->paragraph == "First row continued finished" &&
              st_ir->rows.size() == 2 &&
              geist::detail::verify_fixed_prose_ir(
                  st_sources, st_layout, *st_ownership, *st_ir,
                  &st_error) &&
              geist::detail::format_fixed_prose_ir(*st_ir).find(
                  "marker_bytes=") != std::string::npos,
          st_error.empty() ? "typed fixed prose IR was not admitted"
                           : st_error.c_str());
  auto mutated_st_ir = *st_ir;
  mutated_st_ir.paragraph += " mutation";
  require(!geist::detail::verify_fixed_prose_ir(
              st_sources, st_layout, *st_ownership, mutated_st_ir,
              &st_error) &&
              !st_error.empty(),
          "mutated fixed prose passed canonical verification");
}
