#include "geist/detail/source_rows.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void append(geist::detail::DecodedLogicalRecordSource& record,
            std::uint16_t encoded,
            std::uint8_t width,
            geist::detail::TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
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

  auto numeric = second_record();
  numeric.logical_record = 19;
  numeric.tokens.clear();
  numeric.encoded_tokens.clear();
  append(numeric, 0x1c, 1, {'a'});
  append(numeric, 0x09, 1, {' ', ' ', ' '});
  append(numeric, 0x85, 2, {'d','u','r','i','n','g'});
  append(numeric, 0x1c, 1, {'a'});
  append(numeric, 0x11, 1,
         {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '});
  append(numeric, 0x09, 1, {' ', ' ', ' '});
  append(numeric, 0x86, 2, {'s','t','a','t','i','o','n'});
  numeric.assembled =
      geist::detail::assemble_logical_record_with_sources(numeric.tokens);
  auto catalog_rendered = std::vector<std::string>{
      ":p.The count incremented a during sampling; received from a station."};
  const auto catalog_records = std::vector<std::string>{
      "SRMSG 1073741827 cfont 3 12 2 Description"};
  geist::detail::project_semantic_srmsg_source_markers(
      catalog_rendered, catalog_records, {numeric});
  require(catalog_rendered.front().find("incremented during") !=
              std::string::npos &&
              catalog_rendered.front().find("from a station") !=
                  std::string::npos,
          "numeric catalog marker projection removed the wrong article");

  auto symbolic_rendered =
      std::vector<std::string>{":p.messages logged by action LNM for AIX"};
  auto symbolic = numeric;
  symbolic.tokens[0] = {'a','c','t','i','o','n'};
  symbolic.tokens[2] = {'L','N','M'};
  symbolic.assembled =
      geist::detail::assemble_logical_record_with_sources(symbolic.tokens);
  geist::detail::project_semantic_srmsg_source_markers(
      symbolic_rendered,
      {"SRMSG bridgeHistoryDataComplete cfont 3 12 2 Description"},
      {symbolic});
  require(symbolic_rendered.front().find("logged by LNM for AIX") !=
              std::string::npos,
          "mixed-case symbolic SRMSG marker was not projected");

  auto uppercase_rendered = catalog_rendered;
  uppercase_rendered.front() = ":p.incremented a during";
  geist::detail::project_semantic_srmsg_source_markers(
      uppercase_rendered, {"SRMSG FLM00101 cfont 3 12 2 Description"},
      {numeric});
  require(uppercase_rendered.front().find("a during") != std::string::npos,
          "uppercase product-message catalog activated marker projection");

  auto empty_rendered = uppercase_rendered;
  geist::detail::project_semantic_srmsg_source_markers(
      empty_rendered, {"SRMSG ", "cfont 3 12 2 Description"}, {numeric});
  require(empty_rendered == uppercase_rendered,
          "empty SRMSG wrapper activated marker projection");

  auto two_byte_marker = numeric;
  two_byte_marker.encoded_tokens[0].width = 2;
  auto negative_rendered = std::vector<std::string>{":p.incremented a during"};
  geist::detail::project_semantic_srmsg_source_markers(
      negative_rendered, catalog_records, {two_byte_marker});
  require(negative_rendered.front().find("a during") != std::string::npos,
          "two-byte dictionary word was treated as a compact marker");

  geist::detail::DecodedLogicalRecordSource toc_source;
  toc_source.logical_record = 19;
  append(toc_source, 0x17, 1, {'/'});
  append(toc_source, 0x09, 1, {' ', ' ', ' '});
  append(toc_source, 0x90, 2,
         {'c','t','o','c','e',' ','1',' ','2',' ','A',' ','I','n','p','u','t',
          '/','O','u','t','p','u','t',' '});
  append(toc_source, 0x17, 1, {'/'});
  append(toc_source, 0x91, 2,
         {' ','c','t','o','c','e',' ','1',' ','2',' ','B',' ','N','e','x','t'});
  toc_source.assembled =
      geist::detail::assemble_logical_record_with_sources(toc_source.tokens);
  const auto decoded = geist::detail::token_words_to_ascii(
      toc_source.assembled.words);
  const auto cleaned = geist::detail::clean_source_owned_toc_title_markers(
      {decoded}, {toc_source});
  require(cleaned.size() == 1 && cleaned[0] != decoded &&
              cleaned[0].find("Input/Output") != std::string::npos,
          "learned terminal CTOCE marker was not narrowly removed");

  auto dictionary_boundary = toc_source;
  dictionary_boundary.encoded_tokens[3].width = 2;
  const auto retained = geist::detail::clean_source_owned_toc_title_markers(
      {decoded}, {dictionary_boundary});
  require(retained[0] == decoded,
          "dictionary-owned title punctuation was removed");

  auto semicolon_boundary = toc_source;
  semicolon_boundary.tokens[3] = {';'};
  semicolon_boundary.encoded_tokens[3] = {0x35, 1};
  semicolon_boundary.assembled =
      geist::detail::assemble_logical_record_with_sources(
          semicolon_boundary.tokens);
  const auto semicolon_decoded = geist::detail::token_words_to_ascii(
      semicolon_boundary.assembled.words);
  const auto semicolon_cleaned =
      geist::detail::clean_source_owned_toc_title_markers(
          {semicolon_decoded}, {semicolon_boundary});
  require(semicolon_cleaned[0].find("Input/Output;") == std::string::npos &&
              semicolon_cleaned[0].find("Input/Output") != std::string::npos,
          "terminal fixed-row semicolon was retained in a TOC title");
}
