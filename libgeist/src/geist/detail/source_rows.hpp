#pragma once

#include "geist/detail/internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

enum class SourceRowBoundaryEvidence {
  compact_marker,
  question_run,
  separator,
};

// Provenance for a source-owned physical-row boundary.  token_index and
// encoded_value refer to the compact token immediately before origin_token.
struct SourceRowMarkerProvenance {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t origin_token = 0;
  std::uint16_t encoded_value = 0;
  SourceRowBoundaryEvidence evidence =
      SourceRowBoundaryEvidence::compact_marker;
  std::string text;
};

struct FixedSourceRow {
  std::uint32_t logical_record = 0;
  std::size_t origin = 0;
  bool continuation = false;
  // Visible content after the origin-space token, without the marker slot.
  std::string text;
  SourceRowMarkerProvenance marker;
};

struct SourceRowMarker {
  std::string marker;
  std::string following_text;
  SourceRowMarkerProvenance provenance;
};

// Slice candidate physical rows whose content begins with an exact token of
// origin spaces. A boundary is owned only when that token is immediately
// preceded by a compact one-byte token; visually identical two-byte dictionary
// words are retained in the preceding row. This mechanical conjunction is not
// semantic proof of a fixed layout: callers must first establish the relevant
// fixed prose/catalog context and a stable origin. Additional origins describe
// continuation rows for callers which compose wrapped one-column material.
std::vector<FixedSourceRow> slice_fixed_source_rows(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t content_origin,
    const std::vector<std::size_t>& continuation_origins = {});

std::vector<SourceRowMarker> source_row_markers(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t key_origin);

bool has_semantic_srmsg_source_candidate(
    const std::vector<std::string>& decoded_records);
void project_semantic_srmsg_source_markers(
    std::vector<std::string>& rendered,
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources);

std::vector<std::string> clean_source_owned_toc_title_markers(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources);

// Separates the topic title from source-owned prose carried in the same ST
// segment and removes only repeated, source-proven physical-row marker slots.
// Ambiguous layouts are returned unchanged.
std::vector<std::string> project_source_owned_st_prose_rows(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources);

} // namespace geist::detail
