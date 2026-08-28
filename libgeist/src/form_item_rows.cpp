#include "geist/detail/form_item_rows.hpp"

#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>

namespace geist::detail {

namespace {

constexpr const char* kBallotGlyph = "__";

bool fail(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

const DecodedLogicalRecordSource* find_record(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  const auto found = std::find_if(
      records.begin(), records.end(), [&](const auto& candidate) {
        return candidate.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

// The row's display line: the assembled tokens from its native-origin token
// to its end, with the origin columns removed. This is the untrimmed form of
// PhysicalRowIR::visible_text and the coordinate space of CFONT columns.
std::optional<std::string> display_line(const DecodedLogicalRecordSource& record,
                                        const PhysicalRowIR& row) {
  const auto origin_token = row.marker ? row.token_begin + 1 : row.token_begin;
  if (origin_token >= row.token_end || row.token_end > record.tokens.size()) {
    return std::nullopt;
  }
  std::vector<TokenWords> tokens(
      record.tokens.begin() + static_cast<std::ptrdiff_t>(origin_token),
      record.tokens.begin() + static_cast<std::ptrdiff_t>(row.token_end));
  auto text = token_words_to_ascii(assemble_logical_record(tokens));
  if (text.size() < row.native_origin ||
      text.compare(0, row.native_origin,
                   std::string(row.native_origin, ' ')) != 0) {
    return std::nullopt;
  }
  text.erase(0, row.native_origin);
  if (trim_ascii(text) != trim_ascii(row.visible_text)) {
    return std::nullopt;
  }
  return text;
}

bool is_space(char ch) {
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

const char* phrase_tag(const FontSpanIR& span) {
  switch (span.style) {
  case FontStyleIR::highlight_1:
    return "hp1";
  case FontStyleIR::highlight_2:
    return "hp2";
  case FontStyleIR::highlight_3:
    return "hp3";
  case FontStyleIR::citation:
  case FontStyleIR::example_phrase:
  case FontStyleIR::keyword:
  case FontStyleIR::variable:
  case FontStyleIR::bold_phrase:
  case FontStyleIR::italic_phrase:
  case FontStyleIR::unknown:
    break;
  }
  // Compact CFONTDEF codes `X` (XPH) and `E` (XMP) are example phrases.
  if (ascii_equals_case_insensitive(span.code, "x") ||
      ascii_equals_case_insensitive(span.code, "e")) {
    return "xph";
  }
  return nullptr;
}

// A span conserves whole display words of `line` when it starts and ends on
// word boundaries and covers visible text.
bool span_on_words(const std::string& line, const FontSpanIR& span) {
  const auto begin = span.column;
  const auto end = span.column + span.length;
  if (span.length == 0 || end > line.size()) {
    return false;
  }
  if (begin > 0 && !is_space(line[begin - 1])) {
    return false;
  }
  if (end < line.size() && !is_space(line[end])) {
    return false;
  }
  if (is_space(line[begin]) || is_space(line[end - 1])) {
    return false;
  }
  return true;
}

} // namespace

std::string form_item_plain_text(const std::string& gml_text) {
  std::string output;
  output.reserve(gml_text.size());
  for (std::size_t cursor = 0; cursor < gml_text.size();) {
    if (gml_text[cursor] != ':') {
      output.push_back(gml_text[cursor++]);
      continue;
    }
    const auto dot = gml_text.find('.', cursor + 1);
    if (dot == std::string::npos) {
      output.push_back(gml_text[cursor++]);
      continue;
    }
    const auto tag = ascii_lower(gml_text.substr(cursor + 1, dot - cursor - 1));
    if (tag == "hp1" || tag == "ehp1" || tag == "hp2" || tag == "ehp2" ||
        tag == "hp3" || tag == "ehp3" || tag == "xph" || tag == "exph") {
      cursor = dot + 1;
      continue;
    }
    output.push_back(gml_text[cursor++]);
  }
  return collapse_ascii_whitespace(std::move(output));
}

std::optional<std::vector<FormItemFontRowIR>> extract_form_item_font_rows_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error) {
  std::vector<FormItemFontRowIR> rows;
  if (records.empty()) {
    return rows;
  }
  LayoutIR layout;
  try {
    layout = extract_layout_ir(records);
    if (!verify_layout_ir(records, layout, error)) {
      return std::nullopt;
    }
  } catch (const std::exception& exception) {
    fail(error, exception.what());
    return std::nullopt;
  }
  // The checklist body is the topic's `ST` title segment (BookControlKind
  // title); a plain text run is admitted for record-continuation bodies.
  if (layout.runs.empty() ||
      (layout.runs.front().control_kind != BookControlKind::title &&
       layout.runs.front().control_kind != BookControlKind::text)) {
    fail(error, "topic does not start with a title or text display run");
    return std::nullopt;
  }
  for (std::size_t run_index = 1; run_index < layout.runs.size();
       ++run_index) {
    const auto& run = layout.runs[run_index];
    if (run.control_kind != BookControlKind::font) {
      fail(error, "display run after the text run is not a CFONT run");
      return std::nullopt;
    }
    // Every row of one CFONT run shares the control's operand spans; each
    // span must land on whole words of exactly one row.
    std::vector<std::string> lines;
    std::vector<const DecodedLogicalRecordSource*> row_records;
    for (const auto& row : run.rows) {
      const auto* record = find_record(records, row.logical_record);
      if (record == nullptr) {
        fail(error, "CFONT row has no source record");
        return std::nullopt;
      }
      auto line = display_line(*record, row);
      if (!line) {
        fail(error, "CFONT row display line does not conserve its text");
        return std::nullopt;
      }
      lines.push_back(std::move(*line));
      row_records.push_back(record);
    }
    const auto& first_row = run.rows.front();
    if (first_row.segment_index >= row_records.front()->control_segments.size()) {
      fail(error, "CFONT row segment index is out of range");
      return std::nullopt;
    }
    const auto spans = decode_font_control_spans(
        *row_records.front(),
        row_records.front()->control_segments[first_row.segment_index], error);
    if (!spans) {
      return std::nullopt;
    }
    std::vector<std::vector<FontSpanIR>> spans_by_row(run.rows.size());
    for (const auto& span : spans->spans) {
      std::size_t owner = run.rows.size();
      for (std::size_t index = 0; index < lines.size(); ++index) {
        if (span_on_words(lines[index], span)) {
          if (owner != run.rows.size()) {
            fail(error, "CFONT span lands on more than one row");
            return std::nullopt;
          }
          owner = index;
        }
      }
      if (owner == run.rows.size() || phrase_tag(span) == nullptr) {
        fail(error, "CFONT span does not conserve whole display words");
        return std::nullopt;
      }
      spans_by_row[owner].push_back(span);
    }
    for (std::size_t index = 0; index < run.rows.size(); ++index) {
      auto& row_spans = spans_by_row[index];
      std::sort(row_spans.begin(), row_spans.end(),
                [](const auto& left, const auto& right) {
                  return left.column > right.column;
                });
      FormItemFontRowIR item;
      item.run = run.id;
      item.row_index = index;
      const auto first = lines[index].find_first_not_of(' ');
      if (first != std::string::npos &&
          lines[index].compare(first, 2, kBallotGlyph) == 0 &&
          first + 3 < lines[index].size() && is_space(lines[index][first + 2]) &&
          is_space(lines[index][first + 3])) {
        item.starts_item = true;
        // The ballot glyph is row geometry; a span styling it would not be
        // conserved once the glyph is dropped.
        if (std::any_of(row_spans.begin(), row_spans.end(),
                        [&](const auto& span) { return span.column < first + 2; })) {
          fail(error, "CFONT span styles the ballot glyph");
          return std::nullopt;
        }
      }
      auto gml = lines[index];
      for (const auto& span : row_spans) {
        const std::string tag = phrase_tag(span);
        gml.insert(span.column + span.length, ":e" + tag + ".");
        gml.insert(span.column, ":" + tag + ".");
      }
      if (item.starts_item) {
        gml.erase(0, first + 2);
      }
      item.gml_text = collapse_ascii_whitespace(std::move(gml));
      item.plain_text = form_item_plain_text(item.gml_text);
      if (item.plain_text.empty()) {
        fail(error, "CFONT row has no visible text");
        return std::nullopt;
      }
      rows.push_back(std::move(item));
    }
  }
  return rows;
}

} // namespace geist::detail
