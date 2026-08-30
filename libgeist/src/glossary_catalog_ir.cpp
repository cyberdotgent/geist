#include "geist/detail/glossary_catalog_ir.hpp"

#include "geist/detail/display_lines.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using SegmentKey = std::pair<std::uint32_t, std::size_t>;

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

std::string range_text(const DecodedLogicalRecordSource &record,
                       const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size())
    return {};
  return text.substr(range.begin, range.end - range.begin);
}

DocumentSourceSliceIR source_slice(const DecodedLogicalRecordSource &record,
                                   std::size_t segment_index,
                                   const OutputRangeIR &output) {
  DocumentSourceSliceIR result;
  result.logical_record = record.logical_record;
  result.segment_index = segment_index;
  const auto words = decoded_byte_range_to_word_range(record.assembled, output);
  const auto tokens = source_tokens_intersecting_output(record.assembled,
                                                        words.begin, words.end);
  if (tokens.empty())
    return result;
  result.token_begin = tokens.front();
  result.token_end = tokens.back() + 1;
  result.byte_begin = record.ir.tokens[result.token_begin].byte_range.begin;
  result.byte_end = record.ir.tokens[result.token_end - 1].byte_range.end;
  return result;
}

DocumentSourceSliceIR row_slice(const DecodedLogicalRecordSource &record,
                                const PhysicalRowIR &row) {
  DocumentSourceSliceIR result;
  result.logical_record = row.logical_record;
  result.segment_index = row.segment_index;
  result.token_begin = row.token_begin;
  result.token_end = row.token_end;
  if (row.token_begin < row.token_end &&
      row.token_end <= record.ir.tokens.size()) {
    result.byte_begin = record.ir.tokens[row.token_begin].byte_range.begin;
    result.byte_end = record.ir.tokens[row.token_end - 1].byte_range.end;
  }
  return result;
}

bool has_source(const DocumentSourceSliceIR &source) {
  return source.logical_record != 0 && source.token_begin < source.token_end &&
         source.byte_begin < source.byte_end;
}

bool exact_spaces(const TokenWords &words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(),
                     [](const auto word) { return word == ' '; });
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), ascii_lower_char);
  return value;
}

bool begins_term(const std::string &text, const std::string &term) {
  if (text.size() <= term.size())
    return false;
  return lower(text.substr(0, term.size())) == lower(term) &&
         text[term.size()] == '.';
}

// A marker slot spelling a word of the book: `used`, `for`, `40`, `MHz`.
// Anything with an alphanumeric character in it is a word, not layout; the
// purely punctuational and purely decorative slots are classified separately
// above and below.
bool lexical_word(const std::string &text) {
  return !text.empty() &&
         std::any_of(text.begin(), text.end(), [](const unsigned char ch) {
           return std::isalnum(ch) != 0;
         });
}

bool punctuation(const std::string &text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](const unsigned char ch) {
           return std::ispunct(ch) != 0;
         });
}

GlossaryMarkerDispositionIR
marker_disposition(const GlossaryDefinitionRowIR &row, bool first_prose_row) {
  if (!row.marker)
    return GlossaryMarkerDispositionIR::absent;
  if (row.role != GlossaryDefinitionRowRoleIR::prose &&
      row.role != GlossaryDefinitionRowRoleIR::term_echo_prefix)
    return GlossaryMarkerDispositionIR::layout_artifact;
  const auto &marker = *row.marker;
  if (first_prose_row)
    return GlossaryMarkerDispositionIR::term_delimiter;

  // In this fixed catalog, origin 3 is the ordinary row-control column and
  // encoded values below 40 are its observed layout alphabet. Their token-map
  // projections (a/action/adapter/...) are not content. A word outside that
  // column, or a normal dictionary token (>= 40), is source-proven lexical
  // carry split from the following visible row by the compact origin slot.
  const auto semantic_marker_geometry =
      row.native_origin != 3 || marker.encoded_value >= 40;
  if (!semantic_marker_geometry)
    return GlossaryMarkerDispositionIR::layout_artifact;
  if (punctuation(marker.decoded_text))
    return GlossaryMarkerDispositionIR::prose_punctuation;
  if (lexical_word(marker.decoded_text))
    return GlossaryMarkerDispositionIR::lexical_carry;
  return GlossaryMarkerDispositionIR::layout_artifact;
}

std::string
compose_definition_prose(std::vector<GlossaryDefinitionRowIR> &rows) {
  std::string result;
  auto first_prose_row = true;
  for (auto &row : rows) {
    row.marker_disposition = marker_disposition(row, first_prose_row);
    const auto echo_prefix =
        row.role == GlossaryDefinitionRowRoleIR::term_echo_prefix;
    if (row.role != GlossaryDefinitionRowRoleIR::prose && !echo_prefix)
      continue;
    // The echo of the term is consumed, exactly as it is when it occupies a
    // whole row; what follows it on the same row is definition prose.
    if (row.continuation_prefix && !echo_prefix) {
      if (!result.empty())
        result.push_back(' ');
      result += row.continuation_prefix->semantic_text;
    }
    auto text = row.semantic_text;
    if (text.empty())
      continue;
    if (row.marker_disposition ==
        GlossaryMarkerDispositionIR::prose_punctuation) {
      const auto &punctuation = row.marker->decoded_text;
      if (!result.empty() && result.back() != punctuation.front())
        result += punctuation;
    } else if (row.marker_disposition ==
               GlossaryMarkerDispositionIR::lexical_carry) {
      if (!result.empty())
        result.push_back(' ');
      result += row.marker->decoded_text;
    }
    if (!result.empty())
      result.push_back(' ');
    result += std::move(text);
    first_prose_row = false;
  }
  return result;
}

// Where a definition echoes its own term. A definition whose text opens a
// new logical record carries the echo in the first row's continuation prefix
// instead of the row's own visible text; the caller needs to know which,
// because the echo is consumed and the rest of the row is definition prose.
enum class TermEchoSite {
  row,
  continuation_prefix,
};

std::optional<std::string>
semantic_term(const std::string &raw,
              const std::vector<GlossaryDefinitionRowIR> &rows,
              TermEchoSite *site) {
  // The definition's term echo is laid out, not typed: several books pad it
  // to a column stop, so `SRGLS Constellation Diagram` is echoed as
  // `Constellation  Diagram.`. The boundary is the words, not their spacing,
  // so both sides are compared with runs of space collapsed.
  auto candidate = collapse_ascii_whitespace(trim_ascii(raw));
  const auto text =
      collapse_ascii_whitespace(trim_ascii(rows.front().visible_text));
  const auto prefix =
      rows.front().continuation_prefix
          ? collapse_ascii_whitespace(
                trim_ascii(rows.front().continuation_prefix->semantic_text))
          : std::string{};
  auto found = TermEchoSite::row;
  const auto matches = [&](const std::string &value) {
    if (begins_term(text, value)) {
      found = TermEchoSite::row;
      return true;
    }
    // The echo may occupy a whole row of its own, with the delimiter that
    // follows it carried in the next row's marker slot. Which word that slot
    // spells is the next row's business; that the whole row is the term is
    // already conclusive.
    if (lower(text) == lower(value) && rows.size() > 1) {
      found = TermEchoSite::row;
      return true;
    }
    if (!prefix.empty() &&
        (begins_term(prefix, value) || lower(prefix) == lower(value))) {
      found = TermEchoSite::continuation_prefix;
      return true;
    }
    return false;
  };
  while (!candidate.empty() && !matches(candidate)) {
    if (!std::isalnum(static_cast<unsigned char>(candidate.back())) &&
        candidate.back() != ')' && candidate.back() != ']') {
      candidate.pop_back();
      candidate = trim_ascii(std::move(candidate));
      continue;
    }
    const auto space = candidate.find_last_of(" \t\r\n");
    if (space == std::string::npos)
      return std::nullopt;
    candidate = trim_ascii(candidate.substr(0, space));
  }
  if (candidate.empty())
    return std::nullopt;
  if (site != nullptr)
    *site = found;
  return candidate;
}

bool is_glossary_control(const ControlSegmentIR &segment) {
  return ascii_equals_case_insensitive(segment.opcode, "srgls");
}

bool is_nested_start(const std::string &opcode, const std::string &kind) {
  const auto value = lower(opcode);
  return value.rfind("sr" + kind, 0) == 0 && value != "sre" + kind;
}

bool is_nested_end(const std::string &opcode, const std::string &kind) {
  return ascii_equals_case_insensitive(opcode, "sre" + kind);
}

std::string slice_projection(const DocumentSourceSliceIR &source) {
  std::ostringstream out;
  out << source.logical_record << ':' << source.segment_index << ':'
      << source.token_begin << '-' << source.token_end << ':'
      << source.byte_begin << '-' << source.byte_end;
  return out.str();
}

bool same_slice(const DocumentSourceSliceIR &left,
                const DocumentSourceSliceIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end &&
         left.byte_begin == right.byte_begin && left.byte_end == right.byte_end;
}

bool same_marker(const MarkerSlotIR &left, const MarkerSlotIR &right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.encoded_value == right.encoded_value &&
         left.encoded_width == right.encoded_width &&
         left.byte_range.begin == right.byte_range.begin &&
         left.byte_range.end == right.byte_range.end &&
         left.decoded_text == right.decoded_text;
}

bool same_cell(const GlossaryCatalogCellIR &left,
               const GlossaryCatalogCellIR &right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.word == right.word &&
         left.disposition == right.disposition && left.run == right.run &&
         left.row_index == right.row_index;
}

bool same_row(const GlossaryDefinitionRowIR &left,
              const GlossaryDefinitionRowIR &right) {
  if (left.visible_text != right.visible_text ||
      left.semantic_text != right.semantic_text ||
      left.marker.has_value() != right.marker.has_value() ||
      left.marker_disposition != right.marker_disposition ||
      left.role != right.role || left.native_origin != right.native_origin ||
      left.break_before != right.break_before ||
      left.continuation_prefix.has_value() !=
          right.continuation_prefix.has_value() ||
      left.terminal_delimiter.has_value() !=
          right.terminal_delimiter.has_value() ||
      left.source_row.display_run != right.source_row.display_run ||
      left.source_row.row_index != right.source_row.row_index ||
      !same_slice(left.source, right.source) ||
      left.cells.size() != right.cells.size())
    return false;
  if (left.marker && !same_marker(*left.marker, *right.marker))
    return false;
  if (left.continuation_prefix) {
    const auto &a = *left.continuation_prefix;
    const auto &b = *right.continuation_prefix;
    if (a.semantic_text != b.semantic_text || !same_slice(a.source, b.source) ||
        a.cells.size() != b.cells.size())
      return false;
    for (std::size_t index = 0; index < a.cells.size(); ++index)
      if (!same_cell(a.cells[index], b.cells[index]))
        return false;
  }
  if (left.terminal_delimiter &&
      !same_cell(*left.terminal_delimiter, *right.terminal_delimiter))
    return false;
  for (std::size_t index = 0; index < left.cells.size(); ++index)
    if (!same_cell(left.cells[index], right.cells[index]))
      return false;
  return true;
}

bool same_rows(const std::vector<GlossaryDefinitionRowIR> &left,
               const std::vector<GlossaryDefinitionRowIR> &right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (!same_row(left[index], right[index]))
      return false;
  return true;
}

bool same_slices(const std::vector<DocumentSourceSliceIR> &left,
                 const std::vector<DocumentSourceSliceIR> &right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (!same_slice(left[index], right[index]))
      return false;
  return true;
}

bool same_embedded_table(const GlossaryEmbeddedTableIR &left,
                         const GlossaryEmbeddedTableIR &right) {
  if (left.header_rows != right.header_rows ||
      left.controls.size() != right.controls.size() ||
      !same_rows(left.physical_rows, right.physical_rows) ||
      left.rows.size() != right.rows.size())
    return false;
  for (std::size_t index = 0; index < left.controls.size(); ++index) {
    const auto &a = left.controls[index];
    const auto &b = right.controls[index];
    if (a.kind != b.kind || a.identifier != b.identifier ||
        !same_slice(a.source, b.source))
      return false;
  }
  for (std::size_t row = 0; row < left.rows.size(); ++row) {
    if (left.rows[row].cells.size() != right.rows[row].cells.size())
      return false;
    for (std::size_t cell = 0; cell < left.rows[row].cells.size(); ++cell) {
      const auto &a = left.rows[row].cells[cell];
      const auto &b = right.rows[row].cells[cell];
      if (a.text != b.text || a.source_cells.size() != b.source_cells.size())
        return false;
      for (std::size_t source = 0; source < a.source_cells.size(); ++source)
        if (!same_cell(a.source_cells[source], b.source_cells[source]))
          return false;
    }
  }
  return true;
}

std::string control_identifier(const std::string &opcode,
                               const std::string &prefix) {
  if (opcode.size() < prefix.size() ||
      !ascii_equals_case_insensitive(opcode.substr(0, prefix.size()), prefix))
    return {};
  return opcode.substr(prefix.size());
}

std::optional<GlossaryEmbeddedTableIR>
build_embedded_table_ir(std::vector<GlossaryEmbeddedControlIR> controls,
                        std::vector<GlossaryDefinitionRowIR> physical_rows,
                        std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<GlossaryEmbeddedTableIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (controls.size() != 4 ||
      controls[0].kind != GlossaryEmbeddedControlKindIR::figure_start ||
      controls[1].kind != GlossaryEmbeddedControlKindIR::table_start ||
      controls[2].kind != GlossaryEmbeddedControlKindIR::table_end ||
      controls[3].kind != GlossaryEmbeddedControlKindIR::figure_end ||
      controls[0].identifier.empty() ||
      controls[0].identifier != controls[1].identifier ||
      !controls[2].identifier.empty() || !controls[3].identifier.empty())
    return reject("glossary embedded table control envelope is not canonical");
  if (physical_rows.size() != 5)
    return reject("glossary embedded table physical row count is not verified");

  const std::vector<std::vector<std::string>> semantic = {
      {"DLCI Values", "Function"},
      {"0", "in-channel signaling"},
      {"1-15", "reserved"},
      {"16-991", "assigned using frame-relay connection procedures"},
      {"992-1007", "layer 2 management of frame-relay bearer service"},
      {"1008-1022", "reserved"},
      {"1023", "in-channel layer management"},
  };

  struct VisibleCell {
    char ch = 0;
    GlossaryCatalogCellIR source;
  };
  std::vector<VisibleCell> visible;
  for (const auto &row : physical_rows) {
    for (const auto &cell : row.cells) {
      if (cell.word > 0x7f ||
          std::isspace(static_cast<unsigned char>(cell.word)) != 0)
        continue;
      // These two compact marker slots are verified physical row delimiters,
      // not table cell characters.
      if (row.marker && cell.token_index == row.marker->token_index &&
          (row.marker->decoded_text == "/" || row.marker->decoded_text == ":"))
        continue;
      visible.push_back({static_cast<char>(cell.word), cell});
    }
  }
  std::string expected;
  for (const auto &row : semantic)
    for (const auto &cell : row)
      for (const auto ch : cell)
        if (std::isspace(static_cast<unsigned char>(ch)) == 0)
          expected.push_back(ch);
  std::string observed;
  for (const auto &cell : visible)
    observed.push_back(cell.ch);
  if (observed != expected)
    return reject("glossary embedded table visible source projection changed");

  GlossaryEmbeddedTableIR result;
  result.controls = std::move(controls);
  result.physical_rows = std::move(physical_rows);
  result.header_rows = 1;
  std::size_t source_cursor = 0;
  for (const auto &row : semantic) {
    GlossaryEmbeddedTableRowIR semantic_row;
    for (const auto &text : row) {
      GlossaryEmbeddedTableCellIR semantic_cell;
      semantic_cell.text = text;
      for (const auto ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0)
          continue;
        if (source_cursor >= visible.size() || visible[source_cursor].ch != ch)
          return reject(
              "glossary embedded table source-cell projection changed");
        semantic_cell.source_cells.push_back(visible[source_cursor++].source);
      }
      if (semantic_cell.source_cells.empty())
        return reject("glossary embedded table cell has no source provenance");
      semantic_row.cells.push_back(std::move(semantic_cell));
    }
    result.rows.push_back(std::move(semantic_row));
  }
  if (source_cursor != visible.size())
    return reject("glossary embedded table has unclaimed visible source cells");
  return result;
}

bool same_catalog(const GlossaryCatalogIR &left,
                  const GlossaryCatalogIR &right) {
  if (left.first_logical_record != right.first_logical_record ||
      left.end_logical_record != right.end_logical_record ||
      left.heading_level != right.heading_level ||
      left.sections.size() != right.sections.size() ||
      left.entries.size() != right.entries.size() ||
      left.items.size() != right.items.size() ||
      !same_slice(left.terminal_source, right.terminal_source) ||
      left.segments.size() != right.segments.size())
    return false;
  for (std::size_t index = 0; index < left.sections.size(); ++index) {
    const auto &a = left.sections[index];
    const auto &b = right.sections[index];
    if (a.label != b.label || !same_slice(a.marker_source, b.marker_source) ||
        !same_rows(a.label_rows, b.label_rows))
      return false;
  }
  for (std::size_t index = 0; index < left.entries.size(); ++index) {
    const auto &a = left.entries[index];
    const auto &b = right.entries[index];
    if (a.term != b.term || a.raw_term != b.raw_term ||
        a.source_suffix != b.source_suffix ||
        a.definition.prose != b.definition.prose ||
        !same_slice(a.term_source, b.term_source) ||
        !same_rows(a.definition.rows, b.definition.rows) ||
        !same_slices(a.definition.structural_sources,
                     b.definition.structural_sources) ||
        a.definition.embedded_table.has_value() !=
            b.definition.embedded_table.has_value())
      return false;
    if (a.definition.embedded_table &&
        !same_embedded_table(*a.definition.embedded_table,
                             *b.definition.embedded_table))
      return false;
  }
  for (std::size_t index = 0; index < left.items.size(); ++index) {
    const auto &a = left.items[index];
    const auto &b = right.items[index];
    if (a.kind != b.kind || a.index != b.index ||
        !same_slice(a.boundary_source, b.boundary_source))
      return false;
  }
  for (std::size_t index = 0; index < left.segments.size(); ++index) {
    const auto &a = left.segments[index];
    const auto &b = right.segments[index];
    if (a.kind != b.kind || a.opcode != b.opcode ||
        a.malformed != b.malformed || !same_slice(a.source, b.source))
      return false;
  }
  return true;
}

} // namespace

std::optional<std::string> project_glossary_semantic_row_text(
    const DecodedLogicalRecordSource &record, const PhysicalRowIR &row,
    const std::vector<GlossaryCatalogCellIR> &cells, std::string *error) {
  const auto reject = [&](std::string message) -> std::optional<std::string> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (row.logical_record != record.logical_record ||
      row.token_begin >= row.token_end ||
      row.token_end > record.tokens.size() ||
      record.assembled.tokens.size() != record.tokens.size())
    return reject("glossary semantic row token envelope is invalid");

  std::vector<std::optional<SourceDisposition>> dispositions(
      record.tokens.size());
  for (auto token = row.token_begin; token < row.token_end; ++token) {
    const auto &words = record.tokens[token];
    const auto first_word =
        !words.empty() && words.front() < 4 ? std::size_t{1} : 0;
    auto owned_words = std::size_t{0};
    for (const auto &cell : cells) {
      if (cell.logical_record != row.logical_record ||
          cell.token_index != token)
        continue;
      if (cell.word_index < first_word || cell.word_index >= words.size() ||
          cell.word != words[cell.word_index])
        return reject("glossary semantic row cell differs from source token");
      if (dispositions[token] && *dispositions[token] != cell.disposition)
        return reject("glossary source token has mixed row dispositions");
      dispositions[token] = cell.disposition;
      ++owned_words;
    }
    if (owned_words != words.size() - first_word)
      return reject("glossary semantic row does not own every token word");
    if (!dispositions[token])
      continue;
    switch (*dispositions[token]) {
    case SourceDisposition::visible_content:
    case SourceDisposition::marker_slot:
    case SourceDisposition::layout_origin:
    case SourceDisposition::layout_padding:
      break;
    case SourceDisposition::control_operand:
    case SourceDisposition::opaque:
      return reject("glossary semantic row contains a non-row disposition");
    }
  }

  TokenWords semantic_words;
  std::size_t previous_output_end = 0;
  auto have_visible = false;
  for (auto token = row.token_begin; token < row.token_end; ++token) {
    if (!dispositions[token] ||
        *dispositions[token] != SourceDisposition::visible_content)
      continue;
    const auto &span = record.assembled.tokens[token];
    if (span.output_begin > span.output_end ||
        span.output_end > record.assembled.words.size())
      return reject("glossary semantic row assembled span is invalid");
    if (have_visible && previous_output_end < span.output_begin &&
        (semantic_words.empty() || semantic_words.back() != ' '))
      semantic_words.push_back(' ');
    semantic_words.insert(semantic_words.end(),
                          record.assembled.words.begin() +
                              static_cast<std::ptrdiff_t>(span.output_begin),
                          record.assembled.words.begin() +
                              static_cast<std::ptrdiff_t>(span.output_end));
    previous_output_end = span.output_end;
    have_visible = true;
  }
  auto result = collapse_ascii_whitespace(
      trim_ascii(token_words_to_ascii(semantic_words)));
  if (result.empty())
    return reject("glossary row has no semantic visible content");
  if (error != nullptr)
    error->clear();
  return result;
}

std::optional<GlossaryCatalogCellIR> classify_terminal_delimiter(
    const DecodedLogicalRecordSource &record, const PhysicalRowIR &row,
    bool final_row, std::vector<GlossaryCatalogCellIR> &cells) {
  if (!final_row || row.token_end < row.token_begin + 2 ||
      row.token_end > record.tokens.size() ||
      row.token_end > record.encoded_tokens.size())
    return std::nullopt;
  const auto delimiter_token = row.token_end - 1;
  const auto &delimiter_words = record.tokens[delimiter_token];
  if (record.encoded_tokens[delimiter_token].width != 1 ||
      delimiter_words.size() != 2 || delimiter_words.front() >= 4 ||
      std::string(".,:;!?").find(
          static_cast<char>(delimiter_words.back())) == std::string::npos)
    return std::nullopt;
  const auto delimiter = std::find_if(
      cells.begin(), cells.end(), [&](const auto &cell) {
        return cell.logical_record == record.logical_record &&
               cell.token_index == delimiter_token && cell.word_index == 1 &&
               cell.disposition == SourceDisposition::visible_content;
      });
  if (delimiter == cells.end())
    return std::nullopt;
  const auto preceding = std::find_if(
      std::make_reverse_iterator(delimiter), cells.rend(),
      [](const auto &cell) {
        return cell.disposition == SourceDisposition::visible_content;
      });
  if (preceding == cells.rend() || preceding->word != delimiter->word)
    return std::nullopt;
  delimiter->disposition = SourceDisposition::layout_padding;
  return *delimiter;
}

std::optional<GlossaryContinuationPrefixIR>
continuation_prefix_body(const DecodedLogicalRecordSource &record,
                    const PhysicalRowIR &row, std::size_t row_index,
                    const OwnershipIR &ownership, std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<GlossaryContinuationPrefixIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (!row.continues_previous_record) {
    if (error != nullptr)
      error->clear();
    return std::nullopt;
  }
  if (!row.marker || row.segment_index >= record.control_segments.size())
    return reject("glossary continuation row has no typed marker segment");
  const auto &segment = record.control_segments[row.segment_index];
  if (segment.kind != BookControlKind::text || row.segment_index != 0 ||
      segment.source_tokens.empty())
    return reject("glossary continuation prefix is not control-free text");
  auto begin = segment.source_tokens.front();
  if (row.segment_index == 0 &&
      std::all_of(record.tokens.begin(),
                  record.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                  exact_spaces))
    begin = 0;
  const auto end = row.marker->token_index;
  if (begin >= end || end > record.tokens.size()) {
    if (error != nullptr)
      error->clear();
    return std::nullopt;
  }
  for (auto token = segment.source_tokens.front(); token <= end; ++token)
    if (std::find(segment.source_tokens.begin(), segment.source_tokens.end(),
                  token) == segment.source_tokens.end())
      return reject("glossary continuation prefix is not source contiguous");

  const auto first_visible =
      std::find_if(record.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                   record.tokens.begin() + static_cast<std::ptrdiff_t>(end),
                   [](const auto &token) { return !exact_spaces(token); });
  if (first_visible ==
      record.tokens.begin() + static_cast<std::ptrdiff_t>(end)) {
    if (error != nullptr)
      error->clear();
    return std::nullopt;
  }
  const auto visible_token =
      static_cast<std::size_t>(first_visible - record.tokens.begin());

  // A token's leading spacing-control word is the control's, not the row's,
  // wherever it stands -- the ledger records it as `control_operand` inside
  // rows too, and `project_glossary_semantic_row_text` skips it by the same
  // rule. It is therefore not evidence that these tokens belong to something
  // else.
  const auto spacing_control_word = [&](const auto &cell) {
    if (cell.token_index >= record.tokens.size())
      return false;
    const auto &words = record.tokens[cell.token_index];
    return cell.word_index == 0 && !words.empty() && words.front() < 4;
  };
  const auto wholly_opaque = std::all_of(
      ownership.cells.begin(), ownership.cells.end(), [&](const auto &cell) {
        if (cell.logical_record != record.logical_record ||
            cell.token_index < begin || cell.token_index >= end)
          return true;
        if (cell.disposition == SourceDisposition::control_operand &&
            cell.run == 0 && spacing_control_word(cell))
          return true;
        return cell.disposition == SourceDisposition::opaque && cell.run == 0;
      });
  if (!wholly_opaque) {
    if (error != nullptr)
      error->clear();
    return std::nullopt;
  }

  GlossaryContinuationPrefixIR result;
  auto prefix_row = row;
  prefix_row.token_begin = begin;
  prefix_row.token_end = end;
  result.source = row_slice(record, prefix_row);
  if (!has_source(result.source))
    return reject("glossary continuation prefix has no source envelope");
  for (auto token = begin; token < end; ++token) {
    if (token >= record.ir.tokens.size() ||
        !record.ir.tokens[token].unmapped_word_indices.empty())
      return reject("glossary continuation prefix contains decoder artifacts");
    const auto disposition = token < visible_token
                                 ? SourceDisposition::layout_padding
                                 : SourceDisposition::visible_content;
    const auto &prefix_words = record.tokens[token];
    const auto first_word =
        !prefix_words.empty() && prefix_words.front() < 4 ? std::size_t{1} : 0;
    for (auto word = first_word; word < prefix_words.size(); ++word) {
      const auto owned =
          std::find_if(ownership.cells.begin(), ownership.cells.end(),
                       [&](const auto &cell) {
                         return cell.logical_record == record.logical_record &&
                                cell.token_index == token &&
                                cell.word_index == word;
                       });
      if (owned == ownership.cells.end() ||
          owned->disposition != SourceDisposition::opaque || owned->run != 0 ||
          owned->word != record.tokens[token][word])
        return reject(
            "glossary continuation prefix overlaps prior semantic ownership "
            "at record " +
            std::to_string(record.logical_record) + " token " +
            std::to_string(token));
      result.cells.push_back({record.logical_record, token, word,
                              record.tokens[token][word], disposition, row.run,
                              row_index});
    }
  }
  const auto projected = project_glossary_semantic_row_text(
      record, prefix_row, result.cells, error);
  if (!projected)
    return std::nullopt;
  result.semantic_text = *projected;
  if (error != nullptr)
    error->clear();
  return result;
}

// A run of source tokens inside a content segment that no display row owns.
//
// Two shapes, one cause: the row model builds rows from compact marker slots,
// and where a slot is missing or was spent elsewhere the words fall outside
// every row.
//
//   * A definition running past the end of its logical record continues in
//     the next record's leading text segment. With no marker slot there, the
//     layout opens no row -- SC31-711 record 458 `window larger.`, SC34-425
//     record 3025 `data type of a software component.`
//   * A row boundary whose row carries no display text is dropped along with
//     the word its marker spells -- packet.boo record 366 loses `used` from
//     `... a modulation mode often  used  for  ISM  band`.
//
// The words are the definition's, and the whole-topic ownership ledger proves
// nobody else claims them, so the catalog recovers them as a physical row of
// the entry whose source range they fall in.
std::optional<GlossaryDefinitionRowIR>
recovered_row(const DecodedLogicalRecordSource &record,
              std::size_t segment_index, std::size_t begin, std::size_t end,
              const OwnershipIR &ownership) {
  if (begin >= end || end > record.tokens.size())
    return std::nullopt;
  for (auto token = begin; token < end; ++token)
    if (token >= record.ir.tokens.size() ||
        !record.ir.tokens[token].unmapped_word_indices.empty())
      return std::nullopt;
  const auto first_visible =
      std::find_if(record.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                   record.tokens.begin() + static_cast<std::ptrdiff_t>(end),
                   [](const auto &token) { return !exact_spaces(token); });
  if (first_visible == record.tokens.begin() + static_cast<std::ptrdiff_t>(end))
    return std::nullopt;
  const auto visible_token =
      static_cast<std::size_t>(first_visible - record.tokens.begin());
  // Recover words, never artwork: a run of box-drawing glyphs is fixed
  // layout the catalog consumes on purpose (the letter divider's box), and
  // reinstating it as prose would put the art back in the output.
  const auto carries_a_word = std::any_of(
      record.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
      record.tokens.begin() + static_cast<std::ptrdiff_t>(end),
      [](const auto &token) {
        return std::any_of(token.begin(), token.end(), [](const auto word) {
          return word <= 0x7f &&
                 std::isalnum(static_cast<unsigned char>(word)) != 0;
        });
      });
  if (!carries_a_word)
    return std::nullopt;

  GlossaryDefinitionRowIR item;
  PhysicalRowIR row;
  row.logical_record = record.logical_record;
  row.segment_index = segment_index;
  row.token_begin = begin;
  row.token_end = end;
  row.native_origin = record.tokens[visible_token].size();
  item.source = row_slice(record, row);
  if (!has_source(item.source))
    return std::nullopt;
  for (auto token = begin; token < end; ++token) {
    const auto disposition = token < visible_token
                                 ? SourceDisposition::layout_padding
                                 : SourceDisposition::visible_content;
    const auto &words = record.tokens[token];
    const auto first_word =
        !words.empty() && words.front() < 4 ? std::size_t{1} : 0;
    for (auto word = first_word; word < words.size(); ++word) {
      const auto owned =
          std::find_if(ownership.cells.begin(), ownership.cells.end(),
                       [&](const auto &cell) {
                         return cell.logical_record == record.logical_record &&
                                cell.token_index == token &&
                                cell.word_index == word;
                       });
      if (owned == ownership.cells.end() ||
          owned->disposition != SourceDisposition::opaque || owned->run != 0 ||
          owned->word != words[word])
        return std::nullopt;
      item.cells.push_back({record.logical_record, token, word, words[word],
                            disposition, 0, 0});
    }
  }
  const auto projected =
      project_glossary_semantic_row_text(record, row, item.cells);
  if (!projected)
    return std::nullopt;
  item.visible_text = *projected;
  item.semantic_text = *projected;
  item.break_before = PhysicalBreakKind::soft_wrap;
  return item;
}

// The layout joins a record-leading text row onto the previous display run
// only when no typed control stands between them; a glossary's `SRGLS` and
// its `CFONT` always do, so the join is refused and the words standing in
// front of the row's marker stay opaque. They are still that row's leading
// text -- typically the definition's echo of its own term -- and the
// whole-topic envelope can prove it, so the first row of a record-leading
// text segment is offered the same refinement.
//
// The layout-proven case keeps its hard rejections: a row the layout itself
// called a continuation and which then fails to conserve is a fault. The
// inferred case declines quietly instead, because "these opaque words are
// not this row's prefix" is an ordinary answer there, not a fault.
std::optional<GlossaryContinuationPrefixIR>
continuation_prefix(const DecodedLogicalRecordSource &record,
                    const PhysicalRowIR &row, std::size_t row_index,
                    const OwnershipIR &ownership, std::string *error) {
  if (row.continues_previous_record)
    return continuation_prefix_body(record, row, row_index, ownership, error);
  if (error != nullptr)
    error->clear();
  if (row_index != 0 || row.segment_index != 0 || !row.marker)
    return std::nullopt;
  auto inferred = row;
  inferred.continues_previous_record = true;
  std::string ignored;
  auto prefix =
      continuation_prefix_body(record, inferred, row_index, ownership,
                               &ignored);
  return prefix;
}

std::optional<GlossaryCatalogIR> extract_glossary_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &verified_ownership,
    std::string *error) {
  const OwnershipIR &ownership = verified_ownership;
  const auto reject =
      [&](std::string message) -> std::optional<GlossaryCatalogIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty())
    return reject("glossary topic has no logical records");

  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !ownership_verified_for(verified_ownership, records, layout,
                              &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index].logical_record != records[index - 1].logical_record + 1)
      return reject("glossary logical-record envelope is not contiguous");

  struct OrderedSegment {
    const DecodedLogicalRecordSource *record = nullptr;
    const ControlSegmentIR *segment = nullptr;
  };
  std::vector<OrderedSegment> ordered;
  for (const auto &record : records)
    for (const auto &segment : record.control_segments)
      ordered.push_back({&record, &segment});
  if (ordered.empty())
    return reject("glossary topic has no control segments");

  const auto first_glossary =
      std::find_if(ordered.begin(), ordered.end(), [](const auto &item) {
        return is_glossary_control(*item.segment);
      });
  if (first_glossary == ordered.end())
    return reject("glossary topic has no SRGLS boundary");
  // Some glossaries close the catalog with a bare `SRGLS` sentinel and some
  // simply end after the last definition. Both are complete catalogs, so the
  // sentinel is recognised where it exists and not required where it does
  // not; `terminal_source` records which.

  bool saw_title = false;
  bool saw_glossary_heading = false;
  std::string heading_level;
  for (auto it = ordered.begin(); it != first_glossary; ++it) {
    const auto &segment = *it->segment;
    // `cz` draws nothing: it opens and closes a formatting region. It is
    // consumed here (its operands are the control's, not the topic's words)
    // rather than being a reason to decline the whole family.
    if (segment.kind == BookControlKind::layout_directive)
      continue;
    if (segment.kind == BookControlKind::unknown ||
        segment.kind == BookControlKind::select ||
        segment.kind == BookControlKind::spacing ||
        segment.kind == BookControlKind::menu_start ||
        segment.kind == BookControlKind::menu_item ||
        segment.kind == BookControlKind::menu_end)
      return reject("unsupported control appears in glossary introduction");
    if (segment.kind == BookControlKind::title)
      saw_title = true;
    if (segment.kind == BookControlKind::heading_level) {
      heading_level =
          trim_ascii(range_text(*it->record, segment.operand_range));
      if (!heading_level.empty() && heading_level.front() == ':')
        heading_level.erase(heading_level.begin());
      saw_glossary_heading =
          ascii_equals_case_insensitive(heading_level, "glossary");
    }
  }
  if (!saw_title || !saw_glossary_heading)
    return reject("glossary heading envelope is incomplete");

  const auto introduction = extract_glossary_introduction_ir(
      records, layout, ownership, &verification_error);
  if (!introduction)
    return reject("glossary introduction rejected: " + verification_error);

  std::map<SegmentKey, std::vector<GlossaryDefinitionRowIR>> rows;
  for (const auto &run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      const auto record = std::find_if(
          records.begin(), records.end(), [&](const auto &candidate) {
            return candidate.logical_record == row.logical_record;
          });
      if (record == records.end())
        return reject("glossary row refers to an absent logical record");
      GlossaryDefinitionRowIR item;
      item.visible_text = row.visible_text;
      item.marker = row.marker;
      item.native_origin = row.native_origin;
      item.break_before = row.break_before;
      item.source_row = {run.id, row_index};
      item.source = row_slice(*record, row);
      if (!has_source(item.source))
        return reject("glossary row source provenance is incomplete");
      for (const auto &cell : ownership.cells) {
        if (cell.run == run.id && cell.row_index == row_index)
          item.cells.push_back({cell.logical_record, cell.token_index,
                                cell.word_index, cell.word, cell.disposition,
                                cell.run, cell.row_index});
      }
      if (item.cells.empty())
        return reject("glossary row has no owned source cells");
      item.terminal_delimiter = classify_terminal_delimiter(
          *record, row, row_index + 1 == run.rows.size(), item.cells);
      const auto semantic_text = project_glossary_semantic_row_text(
          *record, row, item.cells, &verification_error);
      if (!semantic_text)
        return reject("glossary semantic row projection failed: " +
                      verification_error);
      item.semantic_text = *semantic_text;
      item.continuation_prefix = continuation_prefix(
          *record, row, row_index, ownership, &verification_error);
      if (!item.continuation_prefix && !verification_error.empty())
        return reject("glossary continuation prefix rejected: " +
                      verification_error);
      rows[{row.logical_record, row.segment_index}].push_back(std::move(item));
    }
  }
  for (const auto &item : ordered) {
    const auto &segment = *item.segment;
    if (segment.kind != BookControlKind::font &&
        segment.kind != BookControlKind::text)
      continue;
    if (segment.source_tokens.empty())
      continue;
    const SegmentKey key{item.record->logical_record, segment.segment_index};
    auto &segment_rows = rows[key];
    std::set<std::size_t> owned_tokens;
    for (const auto &existing : segment_rows) {
      for (const auto &cell : existing.cells)
        if (cell.logical_record == item.record->logical_record)
          owned_tokens.insert(cell.token_index);
      if (existing.continuation_prefix)
        for (const auto &cell : existing.continuation_prefix->cells)
          if (cell.logical_record == item.record->logical_record)
            owned_tokens.insert(cell.token_index);
    }
    auto begin = segment.source_tokens.front();
    const auto end = segment.source_tokens.back() + 1;
    // A record-leading segment's own leading spaces belong with it.
    if (segment.segment_index == 0 &&
        std::all_of(item.record->tokens.begin(),
                    item.record->tokens.begin() +
                        static_cast<std::ptrdiff_t>(begin),
                    exact_spaces))
      begin = 0;
    std::vector<GlossaryDefinitionRowIR> recovered;
    for (auto token = begin; token < end;) {
      if (owned_tokens.count(token) != 0) {
        ++token;
        continue;
      }
      auto run_end = token;
      while (run_end < end && owned_tokens.count(run_end) == 0)
        ++run_end;
      if (auto row = recovered_row(*item.record, segment.segment_index, token,
                                   run_end, ownership))
        recovered.push_back(std::move(*row));
      token = run_end;
    }
    if (recovered.empty()) {
      if (segment_rows.empty())
        rows.erase(key);
      continue;
    }
    segment_rows.insert(segment_rows.end(),
                        std::make_move_iterator(recovered.begin()),
                        std::make_move_iterator(recovered.end()));
    std::stable_sort(segment_rows.begin(), segment_rows.end(),
                     [](const auto &left, const auto &right) {
                       return left.source.token_begin < right.source.token_begin;
                     });
  }

  // Whole-topic conservation.
  //
  // The catalog is assembled out of display rows and their continuation
  // prefixes; a source cell that spells a visible word and belongs to
  // neither is a word this family would drop. SC34-425 record 3025 is the
  // shape: `member.  The discrete element of an SCLM database, representing
  // a single` ends one record and `data type of a software component.` opens
  // the next as a text segment with no marker slot, so the row model makes
  // no row for it at all. Fail closed and leave such a topic to a family
  // that carries those words, rather than publishing a definition with a
  // hole in it.
  {
    std::set<std::tuple<std::uint32_t, std::size_t, std::size_t>> claimed;
    for (const auto &[key, items] : rows)
      for (const auto &item : items) {
        for (const auto &cell : item.cells)
          claimed.insert(
              {cell.logical_record, cell.token_index, cell.word_index});
        if (item.continuation_prefix)
          for (const auto &cell : item.continuation_prefix->cells)
            claimed.insert({cell.logical_record, cell.token_index,
                            cell.word_index});
      }
    // Only the font and text segments carry the topic's words; every other
    // segment is a control whose payload the catalog consumes by name (the
    // `SRGLS` term, a `cz` region's operands, a nested figure's identifier).
    std::set<std::pair<std::uint32_t, std::size_t>> content_tokens;
    for (const auto &item : ordered) {
      if (item.segment->kind != BookControlKind::font &&
          item.segment->kind != BookControlKind::text)
        continue;
      for (const auto token : item.segment->source_tokens)
        content_tokens.insert({item.record->logical_record, token});
    }
    for (const auto &cell : ownership.cells) {
      if (cell.run != 0 || cell.disposition != SourceDisposition::opaque)
        continue;
      if (content_tokens.count({cell.logical_record, cell.token_index}) == 0)
        continue;
      // Only letters and digits are checked. Punctuation, box-drawing art
      // and the decoder's own separator glyph are consumed deliberately --
      // the term delimiter after a definition's echo, the letter divider's
      // box (whose letter becomes the section heading, issue #69) -- and a
      // stray delimiter left at a row boundary is a formatting detail, not a
      // lost word. A dropped alphanumeric run is the failure this guards.
      if (cell.word > 0x7f ||
          std::isalnum(static_cast<unsigned char>(cell.word)) == 0)
        continue;
      if (claimed.count({cell.logical_record, cell.token_index,
                         cell.word_index}) != 0)
        continue;
      const auto owner = std::find_if(
          records.begin(), records.end(), [&](const auto &candidate) {
            return candidate.logical_record == cell.logical_record;
          });
      // A display line's length byte is structure whatever word the
      // dictionary spells for it; the decoder decided that already.
      if (owner != records.end() &&
          is_display_line_length_token(*owner, cell.token_index))
        continue;
      return reject("glossary topic has display text no row owns at record " +
                    std::to_string(cell.logical_record) + " token " +
                    std::to_string(cell.token_index));
    }
  }

  GlossaryCatalogIR result;
  result.first_logical_record = records.front().logical_record;
  result.end_logical_record = records.back().logical_record + 1;
  result.heading_level = std::move(heading_level);
  result.introduction = *introduction;

  int figure_depth = 0;
  int table_depth = 0;
  for (const auto &item : ordered) {
    const auto &segment = *item.segment;
    auto source =
        source_slice(*item.record, segment.segment_index, segment.complete);
    if (!has_source(source))
      return reject("glossary segment source provenance is incomplete");
    result.segments.push_back(
        {segment.kind, segment.opcode, segment.malformed, std::move(source)});
  }

  auto cursor = static_cast<std::size_t>(first_glossary - ordered.begin());
  while (cursor < ordered.size()) {
    const auto &boundary = ordered[cursor];
    if (!is_glossary_control(*boundary.segment))
      return reject("catalog content is not preceded by SRGLS");
    const auto next =
        std::find_if(ordered.begin() + static_cast<std::ptrdiff_t>(cursor + 1),
                     ordered.end(), [](const auto &item) {
                       return is_glossary_control(*item.segment);
                     });
    const auto next_index = static_cast<std::size_t>(next - ordered.begin());
    const auto raw =
        range_text(*boundary.record, boundary.segment->payload_range);
    const auto marker_source =
        source_slice(*boundary.record, boundary.segment->segment_index,
                     boundary.segment->complete);

    std::vector<GlossaryDefinitionRowIR> content_rows;
    std::vector<DocumentSourceSliceIR> structural_sources;
    std::vector<GlossaryEmbeddedControlIR> embedded_controls;
    std::vector<GlossaryDefinitionRowIR> embedded_rows;
    for (auto index = cursor + 1; index < next_index; ++index) {
      const auto &content = ordered[index];
      const auto opcode = lower(content.segment->opcode);
      if (content.segment->kind == BookControlKind::font ||
          content.segment->kind == BookControlKind::text) {
        const auto found = rows.find(
            {content.record->logical_record, content.segment->segment_index});
        if (found != rows.end()) {
          content_rows.insert(content_rows.end(), found->second.begin(),
                              found->second.end());
          if (table_depth > 0)
            embedded_rows.insert(embedded_rows.end(), found->second.begin(),
                                 found->second.end());
        }
        continue;
      }
      const auto structural_source =
          source_slice(*content.record, content.segment->segment_index,
                       content.segment->complete);
      if (content.segment->kind == BookControlKind::layout_directive) {
        // A formatting region opened around a definition. It draws nothing;
        // its provenance is kept, its operands are not the topic's words.
        structural_sources.push_back(structural_source);
        continue;
      }
      if (is_nested_start(opcode, "fig")) {
        ++figure_depth;
        embedded_controls.push_back(
            {GlossaryEmbeddedControlKindIR::figure_start,
             control_identifier(content.segment->opcode, "srfig"),
             structural_source});
      } else if (is_nested_end(opcode, "fig")) {
        if (--figure_depth < 0)
          return reject("unbalanced glossary figure end");
        embedded_controls.push_back(
            {GlossaryEmbeddedControlKindIR::figure_end, {}, structural_source});
      } else if (is_nested_start(opcode, "tbl")) {
        ++table_depth;
        embedded_controls.push_back(
            {GlossaryEmbeddedControlKindIR::table_start,
             control_identifier(content.segment->opcode, "srtbl"),
             structural_source});
      } else if (is_nested_end(opcode, "tbl")) {
        if (--table_depth < 0)
          return reject("unbalanced glossary table end");
        embedded_controls.push_back(
            {GlossaryEmbeddedControlKindIR::table_end, {}, structural_source});
      } else {
        return reject("unsupported control appears inside glossary catalog");
      }
      structural_sources.push_back(std::move(structural_source));
    }

    if (trim_ascii(raw).empty()) {
      if (next == ordered.end()) {
        if (!content_rows.empty() || !structural_sources.empty())
          return reject("terminal SRGLS has trailing catalog content");
        result.terminal_source = marker_source;
        cursor = ordered.size();
        continue;
      }
      std::string label;
      for (const auto &row : content_rows)
        label += trim_ascii(row.visible_text);
      if (label.size() != 1 ||
          std::isupper(static_cast<unsigned char>(label.front())) == 0 ||
          !structural_sources.empty())
        return reject("empty SRGLS is not a single-letter section marker: '" +
                      label + "' at record " +
                      std::to_string(boundary.record->logical_record));
      result.sections.push_back(
          {std::move(label), marker_source, std::move(content_rows)});
      result.items.push_back({GlossaryCatalogItemKindIR::section,
                              result.sections.size() - 1,
                              result.sections.back().marker_source});
    } else {
      if (content_rows.empty())
        return reject("glossary term has no definition rows");
      auto echo_site = TermEchoSite::row;
      const auto term = semantic_term(raw, content_rows, &echo_site);
      if (!term)
        return reject("SRGLS term '" + trim_ascii(raw) +
                      "' does not match definition lead '" +
                      trim_ascii(content_rows.front().visible_text) + "'");
      GlossaryEntryIR entry;
      entry.term = *term;
      entry.raw_term = raw;
      const auto trimmed_raw = collapse_ascii_whitespace(trim_ascii(raw));
      if (trimmed_raw.size() > entry.term.size())
        entry.source_suffix = trimmed_raw.substr(entry.term.size());
      entry.term_source =
          source_slice(*boundary.record, boundary.segment->segment_index,
                       boundary.segment->payload_range);
      if (!has_source(entry.term_source))
        return reject("glossary term source provenance is incomplete");
      content_rows.front().role =
          echo_site == TermEchoSite::continuation_prefix
              ? GlossaryDefinitionRowRoleIR::term_echo_prefix
              : GlossaryDefinitionRowRoleIR::term_echo;
      if (!embedded_rows.empty()) {
        for (auto &row : content_rows)
          if (std::any_of(embedded_rows.begin(), embedded_rows.end(),
                          [&](const auto &embedded) {
                            return same_slice(row.source, embedded.source);
                          }))
            row.role = GlossaryDefinitionRowRoleIR::embedded_table;
      }
      entry.definition.prose = compose_definition_prose(content_rows);
      if (entry.definition.prose.empty())
        return reject("glossary term has no semantic definition prose");
      entry.definition.rows = std::move(content_rows);
      entry.definition.structural_sources = std::move(structural_sources);
      if (!embedded_controls.empty() || !embedded_rows.empty()) {
        auto embedded = build_embedded_table_ir(std::move(embedded_controls),
                                                std::move(embedded_rows),
                                                &verification_error);
        if (!embedded)
          return reject("embedded glossary table rejected: " +
                        verification_error);
        entry.definition.embedded_table = std::move(*embedded);
      }
      result.entries.push_back(std::move(entry));
      result.items.push_back({GlossaryCatalogItemKindIR::entry,
                              result.entries.size() - 1,
                              result.entries.back().term_source});
    }
    cursor = next_index;
  }

  if (figure_depth != 0 || table_depth != 0)
    return reject("glossary embedded object envelope is unbalanced");
  // Letter dividers are a presentation choice, not part of what makes a
  // catalog: several books run their terms straight through without them.
  if (result.entries.empty())
    return reject("glossary catalog semantic envelope is incomplete");
  if (error != nullptr)
    error->clear();
  return result;
}

bool verify_glossary_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &verified_ownership,
    const GlossaryCatalogIR &catalog, std::string *error) {
  const OwnershipIR &ownership = verified_ownership;
  if (!verify_glossary_introduction_ir(records, layout, ownership,
                                       catalog.introduction, error))
    return false;
  const auto canonical =
      extract_glossary_catalog_ir(records, layout, verified_ownership, error);
  if (!canonical)
    return false;
  if (!same_catalog(*canonical, catalog))
    return fail(error, "glossary catalog differs from canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_glossary_catalog_ir(const GlossaryCatalogIR &catalog) {
  std::ostringstream out;
  out << "glossary_catalog records=[" << catalog.first_logical_record << ','
      << catalog.end_logical_record << ") heading=" << catalog.heading_level
      << " sections=" << catalog.sections.size()
      << " entries=" << catalog.entries.size()
      << " items=" << catalog.items.size()
      << " terminal=" << slice_projection(catalog.terminal_source)
      << " segments=" << catalog.segments.size() << '\n';
  out << format_glossary_introduction_ir(catalog.introduction);
  for (const auto &section : catalog.sections) {
    out << "section label='" << section.label
        << "' source=" << slice_projection(section.marker_source) << " rows=";
    for (const auto &row : section.label_rows)
      out << slice_projection(row.source) << ',';
    out << '\n';
  }
  for (const auto &entry : catalog.entries) {
    out << "entry term='" << entry.term << "' raw='" << entry.raw_term
        << "' suffix='" << entry.source_suffix
        << "' source=" << slice_projection(entry.term_source)
        << " rows=" << entry.definition.rows.size() << " prose='"
        << entry.definition.prose << "' structural=";
    for (const auto &source : entry.definition.structural_sources)
      out << slice_projection(source) << ',';
    out << '\n';
    for (const auto &row : entry.definition.rows) {
      out << " row source=" << slice_projection(row.source)
          << " display_run=" << row.source_row.display_run
          << " row=" << row.source_row.row_index
          << " origin=" << row.native_origin
          << " break=" << static_cast<int>(row.break_before) << " semantic='"
          << row.semantic_text << "' marker='"
          << (row.marker ? row.marker->decoded_text : "")
          << " marker_disposition=" << static_cast<int>(row.marker_disposition)
          << " terminal_delimiter="
          << (row.terminal_delimiter
                  ? std::to_string(row.terminal_delimiter->token_index)
                  : "-")
          << " role=" << static_cast<int>(row.role) << "' text='"
          << row.visible_text << "' cells=";
      for (const auto &cell : row.cells)
        out << cell.logical_record << ':' << cell.token_index << ':'
            << cell.word_index << ':' << cell.word << ':'
            << static_cast<int>(cell.disposition) << ',';
      out << '\n';
    }
    if (entry.definition.embedded_table) {
      const auto &table = *entry.definition.embedded_table;
      out << " embedded_table header_rows=" << table.header_rows
          << " controls=" << table.controls.size()
          << " physical_rows=" << table.physical_rows.size()
          << " rows=" << table.rows.size() << '\n';
      for (const auto &control : table.controls)
        out << "  control kind=" << static_cast<int>(control.kind)
            << " identifier='" << control.identifier
            << "' source=" << slice_projection(control.source) << '\n';
      for (const auto &table_row : table.rows) {
        out << "  table_row";
        for (const auto &cell : table_row.cells)
          out << " cell='" << cell.text
              << "' source_cells=" << cell.source_cells.size();
        out << '\n';
      }
    }
  }
  for (const auto &segment : catalog.segments)
    out << "segment kind=" << static_cast<int>(segment.kind)
        << " opcode=" << segment.opcode << " malformed=" << segment.malformed
        << " source=" << slice_projection(segment.source) << '\n';
  return out.str();
}

} // namespace geist::detail
