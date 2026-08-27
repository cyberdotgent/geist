#include "geist/detail/structural_marker_rows.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <exception>

namespace geist::detail {

namespace {

constexpr std::size_t kStructuralMarkerOrigin = 3;
constexpr const char* kSlotPadding = "    ";

bool alphabetic_word(const std::string& text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](const unsigned char ch) {
           return std::isalpha(ch) != 0;
         });
}

} // namespace

std::vector<StructuralMarkerRowIR> extract_structural_marker_rows_ir(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::vector<StructuralMarkerRowIR> rows;
  if (records.empty()) {
    return rows;
  }
  LayoutIR layout;
  try {
    layout = extract_layout_ir(records);
    if (!verify_layout_ir(records, layout)) {
      return rows;
    }
  } catch (const std::exception&) {
    return rows;
  }
  for (const auto& run : layout.runs) {
    for (std::size_t index = 0; index < run.rows.size(); ++index) {
      const auto& row = run.rows[index];
      if (!row.marker || row.native_origin != kStructuralMarkerOrigin ||
          !alphabetic_word(row.marker->decoded_text) ||
          trim_ascii(row.visible_text).empty()) {
        continue;
      }
      rows.push_back({run.id, index, *row.marker, row.visible_text});
    }
  }
  return rows;
}

StructuralMarkerRowEvidence::StructuralMarkerRowEvidence(
    const std::vector<DecodedLogicalRecordSource>& records)
    : records_(&records) {}

const std::vector<StructuralMarkerRowIR>&
StructuralMarkerRowEvidence::rows() const {
  if (!rows_) {
    rows_ = extract_structural_marker_rows_ir(*records_);
  }
  return *rows_;
}

std::size_t StructuralMarkerRowEvidence::marker_word_length_at(
    const std::string& value,
    std::size_t cursor) const {
  if (cursor == 0 || cursor >= value.size() ||
      (value[cursor - 1] != '.' && value[cursor - 1] != ')' &&
       value[cursor - 1] != ':')) {
    return 0;
  }
  auto end = cursor;
  while (end < value.size() &&
         std::isalpha(static_cast<unsigned char>(value[end])) != 0) {
    ++end;
  }
  if (end == cursor || end + 3 >= value.size() ||
      value.compare(end, 4, kSlotPadding) != 0) {
    return 0;
  }
  const auto word = value.substr(cursor, end - cursor);
  auto following = end;
  while (following < value.size() &&
         std::isspace(static_cast<unsigned char>(value[following])) != 0) {
    ++following;
  }
  const auto following_text =
      collapse_ascii_whitespace(value.substr(following));
  for (const auto& row : rows()) {
    if (row.marker.decoded_text != word) {
      continue;
    }
    const auto row_text = collapse_ascii_whitespace(row.visible_text);
    if (!row_text.empty() && following_text.rfind(row_text, 0) == 0) {
      return end - cursor;
    }
  }
  return 0;
}

} // namespace geist::detail
