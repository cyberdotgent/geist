#include "geist/detail/trap_catalog_ir.hpp"

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

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using TokenKey = std::pair<std::uint32_t, std::size_t>;

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record) {
  const auto found =
      std::find_if(records.begin(), records.end(), [&](const auto &record) {
        return record.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

std::string range_text(const DecodedLogicalRecordSource &record,
                       const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size())
    return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::string collapse(std::string value) {
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n");
  return value.substr(0, end);
}

DocumentSourceSliceIR token_slice(const DecodedLogicalRecordSource &record,
                                  std::size_t segment_index,
                                  std::size_t token_begin,
                                  std::size_t token_end) {
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.segment_index = segment_index;
  slice.token_begin = token_begin;
  slice.token_end = token_end;
  if (token_begin < token_end && token_end <= record.ir.tokens.size()) {
    slice.byte_begin = record.ir.tokens[token_begin].byte_range.begin;
    slice.byte_end = record.ir.tokens[token_end - 1].byte_range.end;
  }
  return slice;
}

DocumentSourceSliceIR segment_slice(const DecodedLogicalRecordSource &record,
                                    const ControlSegmentIR &segment) {
  if (segment.source_tokens.empty())
    return {record.logical_record, segment.segment_index, 0, 0, 0, 0};
  return token_slice(record, segment.segment_index,
                     segment.source_tokens.front(),
                     segment.source_tokens.back() + 1);
}

bool structural_padding_words(const TokenWords &words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(), [](const auto word) {
           return word == ' ' || word == '?' || word == 0x2666 || word < 0x20;
         });
}

bool layout_glyph(char ch) {
  return std::string("-<>/\"=()[{").find(ch) != std::string::npos;
}

bool sentence_punctuation(char ch) {
  return std::string(".,:;!?]}").find(ch) != std::string::npos;
}

bool closes_open_delimiter(const std::string &preceding, char closing) {
  char opener = '\0';
  switch (closing) {
  case ')': opener = '('; break;
  case '>': opener = '<'; break;
  case ']': opener = '['; break;
  case '}': opener = '{'; break;
  default: return false;
  }
  std::size_t depth = 0;
  for (const auto ch : preceding) {
    if (ch == opener)
      ++depth;
    else if (ch == closing && depth != 0)
      --depth;
  }
  return depth != 0;
}

struct RowRef {
  const DisplayRunIR *run = nullptr;
  const PhysicalRowIR *row = nullptr;
  std::size_t row_index = 0;
};

struct SourceIndex {
  std::map<CellKey, const OwnedSourceCellIR *> cells;
  std::map<TokenKey, RowRef> marker_rows;
  std::map<TokenKey, RowRef> rows_by_token;
  std::map<TokenKey, std::vector<RowRef>> rows_by_segment;
};

SourceIndex index_sources(const LayoutIR &layout,
                          const OwnershipIR &ownership) {
  SourceIndex index;
  for (const auto &cell : ownership.cells)
    index.cells.emplace(
        CellKey{cell.logical_record, cell.token_index, cell.word_index},
        &cell);
  for (const auto &run : layout.runs)
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      RowRef ref{&run, &row, row_index};
      if (row.marker)
        index.marker_rows.emplace(
            TokenKey{row.marker->logical_record, row.marker->token_index},
            ref);
      for (auto token = row.token_begin; token < row.token_end; ++token)
        index.rows_by_token.emplace(TokenKey{row.logical_record, token}, ref);
      index.rows_by_segment[{row.logical_record, row.segment_index}]
          .push_back(ref);
    }
  return index;
}

bool all_space_token(const LogicalTokenIR &token) {
  return !token.decoded_words.empty() &&
         std::all_of(token.decoded_words.begin(), token.decoded_words.end(),
                     [](const auto word) { return word == ' '; });
}

// The source facts that classify a marker slot (Format/markup.md "Repeated
// row-control signatures"): `?` placeholder runs, the decoder sentinel
// (value 4), and single layout glyphs are layout; sentence punctuation
// closes the preceding text, except that a punctuation slot opening a
// payload at the three-space origin is the boundary control itself; a
// dictionary word at the three-space origin is the row boundary control
// unless a padding run follows the origin (the word is then the wrapped
// last word of the previous display line); a dictionary word at any other
// origin is lexical text.
TrapCellRoleIR marker_role(const DecodedLogicalRecordSource &record,
                           const PhysicalRowIR &row, bool payload_leading) {
  const auto &marker = *row.marker;
  const auto &text = marker.decoded_text;
  if (text.empty() || marker.encoded_value == 4 ||
      std::all_of(text.begin(), text.end(),
                  [](const char ch) { return ch == '?'; }))
    return TrapCellRoleIR::layout_marker;
  if (text.size() == 1 && layout_glyph(text.front()))
    return TrapCellRoleIR::layout_marker;
  const auto punctuation =
      text.size() == 1 && sentence_punctuation(text.front());
  const auto lexical =
      std::all_of(text.begin(), text.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
      });
  if (!punctuation && !lexical)
    return TrapCellRoleIR::layout_marker;
  const auto textual = punctuation ? TrapCellRoleIR::punctuation_marker
                                   : TrapCellRoleIR::lexical_marker;
  if (row.native_origin != 3)
    return textual;
  if (punctuation && payload_leading)
    return TrapCellRoleIR::layout_marker;
  // Signature: `<slot><3 spaces>` followed directly by the row text is the
  // boundary control. A padding token or a decoder placeholder after the
  // origin means the slot word is the wrapped tail of the previous display
  // line (`send the trap       to`, `segment.   <sep>   Segment`).
  const auto after_origin = marker.token_index + 2;
  if (after_origin < record.ir.tokens.size() &&
      (all_space_token(record.ir.tokens[after_origin]) ||
       structural_padding_words(record.ir.tokens[after_origin].decoded_words) ||
       std::any_of(record.ir.tokens[after_origin].decoded_words.begin(),
                   record.ir.tokens[after_origin].decoded_words.end(),
                   [](const auto word) { return word > 0xff; })))
    return textual;
  return TrapCellRoleIR::layout_marker;
}

struct Piece {
  std::size_t token = 0;
  std::string text;
  bool glue = false;
  // Reader spacing before this piece (one space per inserted or padding
  // space cell) so CFONT columns can be mapped onto the display text.
  std::string display_gap;
  std::size_t cell_begin = 0;
};

// Projects one decoded segment payload into text, ledgering every payload
// cell with exactly one role.
bool walk_payload(const DecodedLogicalRecordSource &record,
                  const ControlSegmentIR &segment, const SourceIndex &index,
                  TrapTextIR &out, std::vector<TrapSourceCellIR> &cells,
                  std::string *error, bool operand_tail = false) {
  const auto range =
      decoded_byte_range_to_word_range(record.assembled, segment.payload_range);
  std::vector<Piece> pieces;
  std::string display;
  std::string gap;
  bool pending_space = false;
  bool row_seen = false;
  std::optional<std::size_t> first_token;
  std::optional<std::size_t> last_token;
  std::vector<DocumentSourceRowIR> rows;
  const auto add_row = [&](const RowRef &ref) {
    const DocumentSourceRowIR row{ref.run->id, ref.row_index};
    if (rows.empty() || rows.back().display_run != row.display_run ||
        rows.back().row_index != row.row_index)
      if (std::none_of(rows.begin(), rows.end(), [&](const auto &item) {
            return item.display_run == row.display_run &&
                   item.row_index == row.row_index;
          }))
        rows.push_back(row);
  };
  const auto emit_char = [&](std::size_t token, char ch, bool glue) {
    if (pieces.empty() || pieces.back().token != token) {
      Piece piece;
      piece.token = token;
      piece.glue = glue || (!pending_space && !pieces.empty());
      piece.display_gap = piece.glue ? std::string{} : gap;
      piece.cell_begin = cells.size();
      pieces.push_back(std::move(piece));
    }
    pending_space = false;
    gap.clear();
    pieces.back().text.push_back(ch);
  };
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      pending_space = true;
      gap.push_back(' ');
      continue;
    }
    const auto token = source.token_index;
    const auto word = source.word_index;
    if (token >= record.ir.tokens.size() ||
        word >= record.ir.tokens[token].decoded_words.size())
      return fail(error, "trap payload word has no token provenance");
    const auto value = record.ir.tokens[token].decoded_words[word];
    if (!first_token)
      first_token = token;
    last_token = token;
    const auto covering = index.rows_by_token.find({record.logical_record, token});
    if (covering != index.rows_by_token.end()) {
      add_row(covering->second);
      row_seen = true;
    }
    const auto owned =
        index.cells.find({record.logical_record, token, word});
    const auto disposition = owned == index.cells.end()
                                 ? SourceDisposition::opaque
                                 : owned->second->disposition;
    TrapSourceCellIR cell;
    cell.logical_record = record.logical_record;
    cell.token_index = token;
    cell.word_index = word;
    cell.word = value;
    cell.disposition = disposition;
    const auto marker = index.marker_rows.find({record.logical_record, token});
    if (marker != index.marker_rows.end()) {
      add_row(marker->second);
      cell.role = marker_role(record, *marker->second.row, pieces.empty());
      if (cell.role == TrapCellRoleIR::punctuation_marker) {
        if (value <= 0xff)
          emit_char(token, static_cast<char>(value), true);
      } else if (cell.role == TrapCellRoleIR::lexical_marker) {
        if (value <= 0xff && value >= 0x20)
          emit_char(token, static_cast<char>(value), false);
      } else {
        pending_space = true;
        if (gap.empty())
          gap.push_back(' ');
      }
    } else if (disposition == SourceDisposition::control_operand) {
      cell.role = TrapCellRoleIR::control;
    } else if (value < 0x20) {
      cell.role = TrapCellRoleIR::spacing;
    } else if (value == ' ') {
      cell.role = TrapCellRoleIR::spacing;
      pending_space = true;
      gap.push_back(' ');
    } else if (value > 0xff ||
               structural_padding_words(record.ir.tokens[token].decoded_words)) {
      cell.role = TrapCellRoleIR::placeholder;
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    } else if (disposition == SourceDisposition::visible_content) {
      cell.role = TrapCellRoleIR::text;
      emit_char(token, static_cast<char>(value), false);
    } else if (disposition == SourceDisposition::layout_origin ||
               disposition == SourceDisposition::layout_padding) {
      cell.role = TrapCellRoleIR::spacing;
      pending_space = true;
      gap.push_back(' ');
    } else if (disposition == SourceDisposition::marker_slot) {
      cell.role = TrapCellRoleIR::layout_marker;
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    } else {
      // LayoutIR loses positioned provenance for a record prefix (the
      // continuation of a control whose payload ran past the record end), for
      // the gap between two physical rows of one payload (a sentence stop
      // before a wide padding run), and for the SRMSG operand tail. An
      // unowned printable cell anywhere else is not explained.
      if (covering != index.rows_by_token.end() ||
          (!operand_tail && segment.segment_index != 0 && !row_seen))
        return fail(error, "trap payload has an unowned cell outside a record "
                           "prefix or row gap at LR" +
                               std::to_string(record.logical_record) +
                               " token " + std::to_string(token));
      cell.role = TrapCellRoleIR::unrowed_text;
      emit_char(token, static_cast<char>(value), false);
    }
    cells.push_back(cell);
  }
  // A single layout glyph that closes the segment, or that a padding run of
  // two or more spaces follows, is the marker slot of a display line whose
  // origin token the following control or padding swallowed, unless it
  // closes a delimiter that the preceding text of the same segment left
  // open. Such glyphs are conserved as terminal_glyph cells.
  std::string preceding;
  std::vector<Piece> kept;
  for (std::size_t index = 0; index < pieces.size(); ++index) {
    const auto &piece = pieces[index];
    const auto &token = record.ir.tokens[piece.token];
    const auto next = piece.token + 1;
    const auto followed_by_padding =
        index + 1 == pieces.size() ||
        (next < record.ir.tokens.size() &&
         all_space_token(record.ir.tokens[next]) &&
         record.ir.tokens[next].decoded_words.size() >= 2);
    if (token.encoded.width == 1 && piece.text.size() == 1 &&
        layout_glyph(piece.text.front()) && followed_by_padding &&
        !closes_open_delimiter(preceding, piece.text.front())) {
      for (auto &cell : cells)
        if (cell.logical_record == record.logical_record &&
            cell.token_index == piece.token &&
            cell.role == TrapCellRoleIR::text)
          cell.role = TrapCellRoleIR::terminal_glyph;
      continue;
    }
    preceding += piece.text;
    kept.push_back(piece);
  }
  pieces = std::move(kept);
  std::string text;
  display.clear();
  for (const auto &piece : pieces) {
    if (!text.empty() && !piece.glue)
      text.push_back(' ');
    text += piece.text;
    if (!display.empty() && !piece.glue)
      display += piece.display_gap;
    display += piece.text;
  }
  text = collapse(std::move(text));
  if (!out.text.empty() && !text.empty())
    out.text.push_back(' ');
  out.text += text;
  display = trim_ascii(std::move(display));
  if (!out.display_text.empty() && !display.empty())
    out.display_text.push_back(' ');
  out.display_text += display;
  if (first_token && last_token)
    out.source_slices.push_back(token_slice(record, segment.segment_index,
                                            *first_token, *last_token + 1));
  out.source_rows.insert(out.source_rows.end(), rows.begin(), rows.end());
  return true;
}


// Leading chain of CFONT spans starting at the catalog origin column whose
// members follow one another with single-space gaps.
std::vector<FontSpanIR> leading_chain(const FontControlSpansIR &spans,
                                      std::size_t origin_column) {
  auto sorted = spans.spans;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const auto &left, const auto &right) {
                     return left.column < right.column;
                   });
  std::vector<FontSpanIR> chain;
  for (const auto &span : sorted) {
    if (span.length == 0)
      break;
    if (chain.empty()) {
      if (span.column != origin_column)
        break;
    } else if (span.column != chain.back().column + chain.back().length + 1) {
      break;
    }
    chain.push_back(span);
  }
  return chain;
}

bool map_chain(const std::vector<FontSpanIR> &chain, std::size_t origin_column,
               const std::string &display, std::vector<TrapStyledSpanIR> &out,
               std::string &joined) {
  out.clear();
  joined.clear();
  for (const auto &span : chain) {
    if (span.column < origin_column)
      return false;
    const auto offset = span.column - origin_column;
    const auto end = offset + span.length;
    if (end > display.size())
      return false;
    if (offset != 0 && display[offset - 1] != ' ')
      return false;
    if (end != display.size() && display[end] != ' ')
      return false;
    const auto text = display.substr(offset, span.length);
    if (text.empty() || text.front() == ' ' || text.back() == ' ')
      return false;
    out.push_back({text, span});
    if (!joined.empty())
      joined.push_back(' ');
    joined += text;
  }
  return !out.empty();
}

bool starts_with_word(const std::string &text, const std::string &prefix) {
  return !prefix.empty() && text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0 &&
         (text.size() == prefix.size() || text[prefix.size()] == ' ');
}

struct SegmentRef {
  const DecodedLogicalRecordSource *record = nullptr;
  const ControlSegmentIR *segment = nullptr;
};

bool same_text(const TrapTextIR &left, const TrapTextIR &right) {
  const auto rows_equal = [](const auto &a, const auto &b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const auto &x, const auto &y) {
                        return x.display_run == y.display_run &&
                               x.row_index == y.row_index;
                      });
  };
  return left.text == right.text && left.display_text == right.display_text &&
         left.source_slices == right.source_slices &&
         rows_equal(left.source_rows, right.source_rows) &&
         left.cell_begin == right.cell_begin && left.cell_end == right.cell_end;
}

bool same_spans(const std::vector<TrapStyledSpanIR> &left,
                const std::vector<TrapStyledSpanIR> &right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](const auto &a, const auto &b) {
                      return a.text == b.text &&
                             a.span.column == b.span.column &&
                             a.span.length == b.span.length &&
                             a.span.code == b.span.code &&
                             a.span.style == b.span.style;
                    });
}

bool same_line(const TrapLineIR &left, const TrapLineIR &right) {
  return left.font_source == right.font_source &&
         same_spans(left.spans, right.spans) &&
         left.spans_text == right.spans_text && same_text(left.body, right.body);
}

} // namespace

std::optional<TrapCatalogIR>
extract_trap_catalog_ir(const std::vector<DecodedLogicalRecordSource> &records,
                        const LayoutIR &layout, const OwnershipIR &ownership,
                        const std::string &toc_title, std::string *error) {
  const auto reject = [&](std::string message) {
    fail(error, std::move(message));
    return std::optional<TrapCatalogIR>{};
  };
  if (records.empty() || !ownership.conflicts.empty())
    return reject("source ownership is unavailable or conflicted");
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index].logical_record != records[index - 1].logical_record + 1)
      return reject("trap catalog logical-record envelope is not contiguous");

  std::vector<SegmentRef> ordered;
  for (const auto &record : records)
    for (const auto &segment : record.control_segments)
      ordered.push_back({&record, &segment});
  const auto is_entry_start = [](const SegmentRef &ref) {
    return ref.segment->kind == BookControlKind::message_start &&
           !first_word(range_text(*ref.record, ref.segment->operand_range))
                .empty();
  };
  std::vector<std::size_t> starts;
  for (std::size_t index = 0; index < ordered.size(); ++index)
    if (is_entry_start(ordered[index]))
      starts.push_back(index);
  if (starts.empty())
    return reject("source has no SRMSG entry");

  const auto index = index_sources(layout, ownership);
  TrapCatalogIR catalog;
  catalog.first_logical_record = records.front().logical_record;
  catalog.end_logical_record = records.back().logical_record + 1;

  // Topic header: metadata, title, named anchors, index terms, and the
  // introduction prose.
  std::optional<SegmentRef> title;
  for (std::size_t index_in_order = 0; index_in_order < starts.front();
       ++index_in_order) {
    const auto &ref = ordered[index_in_order];
    const auto &segment = *ref.segment;
    const auto operand_text = collapse(range_text(*ref.record, segment.operand_range));
    switch (segment.kind) {
    case BookControlKind::topic_start:
      if (segment.opcode.size() > 2)
        catalog.raw_topic_id = segment.opcode.substr(2);
      break;
    case BookControlKind::heading_level: {
      auto value = operand_text;
      if (!value.empty() && value.front() == ':')
        value.erase(value.begin());
      catalog.heading_level = value;
      break;
    }
    case BookControlKind::topic_number:
    case BookControlKind::parent:
    case BookControlKind::forward_level:
    case BookControlKind::back_level:
    case BookControlKind::summary:
    case BookControlKind::source_file:
      break;
    case BookControlKind::title:
      if (title)
        return reject("trap catalog has multiple title segments");
      title = ref;
      break;
    case BookControlKind::message_start:
      catalog.anchors.push_back({"MSG", segment_slice(*ref.record, segment)});
      break;
    case BookControlKind::structural:
      if (ascii_starts_with_case_insensitive(segment.opcode, "SRHDR"))
        catalog.anchors.push_back(
            {segment.opcode.substr(2), segment_slice(*ref.record, segment)});
      break;
    case BookControlKind::text:
    case BookControlKind::font:
      break;
    default:
      return reject("trap catalog header contains an unsupported control: " +
                    segment.opcode);
    }
  }
  if (!title || catalog.raw_topic_id.empty() ||
      catalog.heading_level.size() != 2 ||
      (catalog.heading_level.front() != 'H' &&
       catalog.heading_level.front() != 'h') ||
      catalog.heading_level.back() < '1' || catalog.heading_level.back() > '6')
    return reject("trap catalog metadata/title envelope is incomplete");
  const auto title_rows = index.rows_by_segment.find(
      {title->record->logical_record, title->segment->segment_index});
  if (title_rows == index.rows_by_segment.end() ||
      title_rows->second.empty() || title_rows->second.front().row_index != 0)
    return reject("trap catalog title has no leading physical row");
  const auto &title_ref = title_rows->second.front();
  if (title_ref.row->marker)
    return reject("trap catalog title row carries a marker slot");
  const auto title_row_text = collapse(title_ref.row->visible_text);
  if (title_row_text.empty())
    return reject("trap catalog title row is empty");
  auto title_end_token = title_ref.row->token_end;
  const auto expected_title = collapse(toc_title);
  if (!expected_title.empty() && title_row_text != expected_title) {
    // The heading and the first introduction sentence share one physical
    // row; the contents title decides where the heading ends. Accumulate
    // the row's visible cells token by token until they spell the title.
    std::string accumulated;
    bool pending_space = false;
    std::optional<std::size_t> split;
    for (auto token = title_ref.row->token_begin;
         token < title_ref.row->token_end && !split; ++token) {
      const auto &words = title->record->ir.tokens[token].decoded_words;
      for (std::size_t word = 0; word < words.size(); ++word) {
        const auto cell =
            index.cells.find({title->record->logical_record, token, word});
        if (cell != index.cells.end() &&
            cell->second->disposition == SourceDisposition::visible_content &&
            words[word] <= 0xff && words[word] >= 0x20 && words[word] != ' ') {
          if (pending_space && !accumulated.empty())
            accumulated.push_back(' ');
          pending_space = false;
          accumulated.push_back(static_cast<char>(words[word]));
        } else {
          pending_space = true;
        }
      }
      pending_space = true;
      // Inter-token spacing of a title row is not provenance-typed here
      // (hyphenated words split across tokens); compare the glyph sequence.
      const auto without_spaces = [](std::string value) {
        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
        return value;
      };
      if (without_spaces(accumulated) == without_spaces(expected_title))
        split = token + 1;
    }
    if (!split)
      return reject("trap catalog title row does not begin with the contents "
                    "title: [" +
                    title_row_text + "] vs [" + expected_title +
                    "] accumulated [" + accumulated + "]");
    title_end_token = *split;
    catalog.title = expected_title;
  } else {
    catalog.title = title_row_text;
  }
  catalog.title_source =
      token_slice(*title->record, title->segment->segment_index,
                  title_ref.row->token_begin, title_end_token);
  catalog.title_row = {title_ref.run->id, title_ref.row_index};

  // Entries.
  std::optional<std::size_t> origin_column;
  for (std::size_t entry_index = 0; entry_index < starts.size();
       ++entry_index) {
    const auto begin = starts[entry_index];
    const auto end = entry_index + 1 < starts.size() ? starts[entry_index + 1]
                                                      : ordered.size();
    const auto &start = ordered[begin];
    TrapEntryIR entry;
    entry.operand =
        collapse(range_text(*start.record, start.segment->operand_range));
    entry.id = first_word(entry.operand);
    entry.start_source = segment_slice(*start.record, *start.segment);
    {
      // The SRMSG payload carries the operand tail (a symbolic name after
      // the numeric ID) followed by the entry separator placeholder run. Both
      // are suppressed from the entry text; the tail completes the operand.
      TrapTextIR separator;
      std::vector<TrapSourceCellIR> separator_cells;
      if (!walk_payload(*start.record, *start.segment, index, separator,
                        separator_cells, error, true))
        return std::nullopt;
      for (auto &cell : separator_cells) {
        cell.role = TrapCellRoleIR::suppressed;
        entry.cells.push_back(cell);
      }
      const auto tail = collapse(separator.text);
      if (!tail.empty())
        entry.operand += " " + tail;
      entry.suppressed_rows = separator.source_rows;
    }
    bool have_headline = false;
    TrapLineIR *current = nullptr;
    for (auto at = begin + 1; at < end; ++at) {
      const auto &ref = ordered[at];
      const auto &segment = *ref.segment;
      if (segment.kind == BookControlKind::font) {
        std::string font_error;
        const auto spans =
            decode_font_control_spans(*ref.record, segment, &font_error);
        if (!spans)
          return reject("trap entry font control rejected: " + entry.id +
                        ": " + font_error);
        TrapLineIR line;
        line.font_source = segment_slice(*ref.record, segment);
        line.body.cell_begin = entry.cells.size();
        if (!walk_payload(*ref.record, segment, index, line.body, entry.cells,
                          error))
          return std::nullopt;
        // A font control whose payload is empty at the record end carries its
        // text in the next record's leading text segment.
        if (line.body.text.empty() && at + 1 < end &&
            segment.segment_index + 1 == ref.record->control_segments.size() &&
            ordered[at + 1].record->logical_record ==
                ref.record->logical_record + 1 &&
            ordered[at + 1].segment->segment_index == 0 &&
            (ordered[at + 1].segment->kind == BookControlKind::text ||
             ordered[at + 1].segment->kind == BookControlKind::unknown)) {
          ++at;
          if (!walk_payload(*ordered[at].record, *ordered[at].segment, index,
                            line.body, entry.cells, error))
            return std::nullopt;
        }
        line.body.cell_end = entry.cells.size();
        const auto chain = leading_chain(
            *spans, origin_column ? *origin_column
                                  : (spans->spans.empty()
                                         ? std::size_t{0}
                                         : spans->spans.front().column));
        if (!chain.empty()) {
          if (!map_chain(chain, chain.front().column, line.body.display_text,
                         line.spans, line.spans_text))
            return reject("trap entry font spans do not map onto its row text: " +
                          entry.id + " [" + line.body.display_text + "]");
        }
        if (!chain.empty() && !have_headline) {
          if (!origin_column)
            origin_column = chain.front().column;
          if (!line.spans_text.empty() && line.spans_text.back() == ':')
            return reject("trap entry starts with a field label: " + entry.id);
          if (collapse(line.body.text) != collapse(line.spans_text))
            return reject("trap entry headline is not fully highlighted: " +
                          entry.id + " [" + line.body.text + "]");
          entry.headline = std::move(line);
          have_headline = true;
          current = &entry.headline;
        } else if (!chain.empty() && !line.spans_text.empty() &&
                   line.spans_text.back() == ':') {
          if (!starts_with_word(line.body.text, line.spans_text))
            return reject("trap field text does not begin with its label: " +
                          entry.id);
          TrapFieldIR field;
          field.label_text = line.spans_text;
          field.line = std::move(line);
          entry.fields.push_back(std::move(field));
          current = &entry.fields.back().line;
        } else if (!chain.empty()) {
          return reject("trap entry highlighted run is neither headline nor "
                        "label: " +
                        entry.id + " [" + line.spans_text + "]");
        } else {
          if (current == nullptr)
            return reject("trap entry text precedes its headline: " + entry.id);
          auto &body = current->body;
          if (!body.text.empty() && !line.body.text.empty())
            body.text.push_back(' ');
          body.text += line.body.text;
          if (!body.display_text.empty() && !line.body.display_text.empty())
            body.display_text.push_back(' ');
          body.display_text += line.body.display_text;
          body.source_slices.insert(body.source_slices.end(),
                                    line.body.source_slices.begin(),
                                    line.body.source_slices.end());
          body.source_rows.insert(body.source_rows.end(),
                                  line.body.source_rows.begin(),
                                  line.body.source_rows.end());
          body.cell_end = line.body.cell_end;
        }
      } else if (segment.kind == BookControlKind::text ||
                 segment.kind == BookControlKind::unknown ||
                 segment.kind == BookControlKind::structural) {
        TrapTextIR piece;
        piece.cell_begin = entry.cells.size();
        if (!walk_payload(*ref.record, segment, index, piece, entry.cells,
                          error))
          return std::nullopt;
        piece.cell_end = entry.cells.size();
        if (current == nullptr) {
          // A placeholder-only record prefix (separator plus `?` run) before
          // the headline carries no text; its cells stay suppressed.
          if (!piece.text.empty())
            return reject("trap entry text precedes its headline: " + entry.id);
          for (auto cell = piece.cell_begin; cell < piece.cell_end; ++cell)
            entry.cells[cell].role = TrapCellRoleIR::suppressed;
          entry.suppressed_rows.insert(entry.suppressed_rows.end(),
                                       piece.source_rows.begin(),
                                       piece.source_rows.end());
          continue;
        }
        auto &body = current->body;
        if (body.cell_end != piece.cell_begin)
          return reject("trap entry continuation is not contiguous: " +
                        entry.id);
        if (!body.text.empty() && !piece.text.empty())
          body.text.push_back(' ');
        body.text += piece.text;
        if (!body.display_text.empty() && !piece.display_text.empty())
          body.display_text.push_back(' ');
        body.display_text += piece.display_text;
        body.source_slices.insert(body.source_slices.end(),
                                  piece.source_slices.begin(),
                                  piece.source_slices.end());
        body.source_rows.insert(body.source_rows.end(),
                                piece.source_rows.begin(),
                                piece.source_rows.end());
        body.cell_end = piece.cell_end;
      } else {
        return reject("trap entry contains an unsupported control: " +
                      entry.id + " " + segment.opcode);
      }
    }
    if (!have_headline)
      return reject("trap entry has no highlighted headline: " + entry.id);
    if (!starts_with_word(entry.headline.body.text, entry.id))
      return reject("trap entry headline does not begin with its ID: " +
                    entry.id + " [" + entry.headline.body.text + "]");
    if (entry.fields.empty())
      return reject("trap entry has no labelled field: " + entry.id);
    for (const auto &field : entry.fields)
      if (field.line.body.text.size() == field.label_text.size())
        return reject("trap field has no text after its label: " + entry.id +
                      " " + field.label_text);
    catalog.entries.push_back(std::move(entry));
  }
  catalog.origin_column = origin_column.value_or(0);
  // The vocabulary is the ordered label sequence every entry repeats. A
  // labelled line that some entry lacks (an entry-local `Note:`) stays a
  // highlighted continuation of the entry rather than a field.
  for (const auto &field : catalog.entries.front().fields) {
    const auto everywhere = std::all_of(
        catalog.entries.begin(), catalog.entries.end(), [&](const auto &entry) {
          return std::any_of(entry.fields.begin(), entry.fields.end(),
                             [&](const auto &candidate) {
                               return candidate.label_text == field.label_text;
                             });
        });
    if (everywhere)
      catalog.label_vocabulary.push_back(field.label_text);
  }
  if (catalog.label_vocabulary.empty())
    return reject("trap catalog has no label repeated by every entry");
  for (auto &entry : catalog.entries) {
    std::vector<std::string> labels;
    for (auto &field : entry.fields) {
      field.in_vocabulary =
          std::find(catalog.label_vocabulary.begin(),
                    catalog.label_vocabulary.end(),
                    field.label_text) != catalog.label_vocabulary.end();
      if (field.in_vocabulary)
        labels.push_back(field.label_text);
    }
    if (labels != catalog.label_vocabulary)
      return reject("trap entry does not repeat the catalog label vocabulary: " +
                    entry.id);
  }

  // Introduction prose between the title row and the first entry.
  MessageProseEnvelopeIR envelope;
  envelope.begin_record = title->record->logical_record;
  envelope.begin_token = title_end_token;
  envelope.catalog_record = ordered[starts.front()].record->logical_record;
  envelope.catalog_segment = ordered[starts.front()].segment->segment_index;
  catalog.introduction_envelope = envelope;
  bool visible_introduction = false;
  for (const auto &record : records) {
    if (record.logical_record < envelope.begin_record ||
        record.logical_record > envelope.catalog_record)
      continue;
    const auto begin =
        record.logical_record == envelope.begin_record ? envelope.begin_token : 0;
    const auto end =
        record.logical_record == envelope.catalog_record
            ? ordered[starts.front()].segment->source_tokens.front()
            : record.ir.tokens.size();
    for (auto token = begin; token < end && token < record.ir.tokens.size();
         ++token)
      for (std::size_t word = 0;
           word < record.ir.tokens[token].decoded_words.size(); ++word) {
        const auto cell =
            index.cells.find({record.logical_record, token, word});
        if (cell != index.cells.end() &&
            cell->second->disposition == SourceDisposition::visible_content)
          visible_introduction = true;
      }
  }
  std::string prose_error;
  const auto prose = extract_message_prose_paragraphs_ir(
      records, layout, ownership, envelope, &prose_error);
  if (!prose && visible_introduction)
    return reject("trap catalog introduction rejected: " + prose_error);
  if (prose) {
    for (const auto &paragraph : prose->paragraphs) {
      TrapIntroductionParagraphIR item;
      item.text = paragraph.text;
      item.source_slices = paragraph.source_slices;
      item.source_rows = paragraph.source_rows;
      if (!paragraph.source_rows.empty()) {
        const auto &first = paragraph.source_rows.front();
        const auto run = std::find_if(
            layout.runs.begin(), layout.runs.end(), [&](const auto &candidate) {
              return candidate.id == first.display_run;
            });
        if (run != layout.runs.end() && first.row_index < run->rows.size() &&
            run->control_kind == BookControlKind::font) {
          const auto &row = run->rows[first.row_index];
          const auto *record = find_record(records, row.logical_record);
          if (record != nullptr &&
              row.segment_index < record->control_segments.size()) {
            const auto spans = decode_font_control_spans(
                *record, record->control_segments[row.segment_index]);
            if (spans) {
              const auto chain = leading_chain(*spans, catalog.origin_column);
              std::string joined;
              std::vector<TrapStyledSpanIR> mapped;
              if (!chain.empty() &&
                  map_chain(chain, catalog.origin_column, item.text, mapped,
                            joined))
                item.leading_spans = std::move(mapped);
            }
          }
        }
      }
      catalog.introduction.push_back(std::move(item));
    }
  }
  if (error != nullptr)
    error->clear();
  return catalog;
}

bool same_trap_catalog_ir(const TrapCatalogIR &left,
                          const TrapCatalogIR &right) {
  if (left.first_logical_record != right.first_logical_record ||
      left.end_logical_record != right.end_logical_record ||
      left.raw_topic_id != right.raw_topic_id ||
      left.heading_level != right.heading_level || left.title != right.title ||
      !(left.title_source == right.title_source) ||
      left.title_row.display_run != right.title_row.display_run ||
      left.title_row.row_index != right.title_row.row_index ||
      left.anchors.size() != right.anchors.size() ||
      left.introduction_envelope != right.introduction_envelope ||
      left.introduction.size() != right.introduction.size() ||
      left.origin_column != right.origin_column ||
      left.label_vocabulary != right.label_vocabulary ||
      left.entries.size() != right.entries.size())
    return false;
  for (std::size_t index = 0; index < left.anchors.size(); ++index)
    if (left.anchors[index].id != right.anchors[index].id ||
        !(left.anchors[index].source == right.anchors[index].source))
      return false;
  for (std::size_t index = 0; index < left.introduction.size(); ++index) {
    const auto &a = left.introduction[index];
    const auto &b = right.introduction[index];
    if (a.text != b.text || !same_spans(a.leading_spans, b.leading_spans) ||
        a.source_slices != b.source_slices ||
        a.source_rows.size() != b.source_rows.size())
      return false;
    for (std::size_t row = 0; row < a.source_rows.size(); ++row)
      if (a.source_rows[row].display_run != b.source_rows[row].display_run ||
          a.source_rows[row].row_index != b.source_rows[row].row_index)
        return false;
  }
  for (std::size_t index = 0; index < left.entries.size(); ++index) {
    const auto &a = left.entries[index];
    const auto &b = right.entries[index];
    if (a.id != b.id || a.operand != b.operand ||
        !(a.start_source == b.start_source) ||
        !same_line(a.headline, b.headline) || a.fields.size() != b.fields.size() ||
        a.cells != b.cells || a.suppressed_rows.size() != b.suppressed_rows.size())
      return false;
    for (std::size_t field = 0; field < a.fields.size(); ++field)
      if (a.fields[field].label_text != b.fields[field].label_text ||
          a.fields[field].in_vocabulary != b.fields[field].in_vocabulary ||
          !same_line(a.fields[field].line, b.fields[field].line))
        return false;
    for (std::size_t row = 0; row < a.suppressed_rows.size(); ++row)
      if (a.suppressed_rows[row].display_run !=
              b.suppressed_rows[row].display_run ||
          a.suppressed_rows[row].row_index != b.suppressed_rows[row].row_index)
        return false;
  }
  return true;
}

bool verify_trap_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const TrapCatalogIR &catalog, std::string *error) {
  // Structural invariants that do not depend on the canonical extraction.
  for (const auto &entry : catalog.entries) {
    std::set<CellKey> seen;
    for (const auto &cell : entry.cells) {
      if (!seen.insert({cell.logical_record, cell.token_index, cell.word_index})
               .second)
        return fail(error, "trap entry ledgers one source cell twice: " +
                               entry.id);
      const auto *record = find_record(records, cell.logical_record);
      if (record == nullptr || cell.token_index >= record->ir.tokens.size() ||
          cell.word_index >=
              record->ir.tokens[cell.token_index].decoded_words.size() ||
          record->ir.tokens[cell.token_index].decoded_words[cell.word_index] !=
              cell.word)
        return fail(error, "trap entry ledger cell does not match its source: " +
                               entry.id);
    }
    const auto check_line = [&](const TrapLineIR &line, const char *what) {
      if (line.body.cell_begin > line.body.cell_end ||
          line.body.cell_end > entry.cells.size())
        return fail(error, std::string("trap ") + what +
                               " cell range is invalid: " + entry.id);
      return true;
    };
    if (!check_line(entry.headline, "headline"))
      return false;
    if (!starts_with_word(entry.headline.body.text, entry.id))
      return fail(error, "trap headline does not begin with its ID: " + entry.id);
    for (const auto &field : entry.fields) {
      if (!check_line(field.line, "field"))
        return false;
      if (field.label_text.empty() || field.label_text.back() != ':' ||
          field.label_text != field.line.spans_text ||
          !starts_with_word(field.line.body.text, field.label_text))
        return fail(error, "trap field label is not source-proven: " + entry.id);
    }
  }
  const auto canonical = extract_trap_catalog_ir(records, layout, ownership,
                                                 catalog.title, error);
  if (!canonical)
    return false;
  if (!same_trap_catalog_ir(*canonical, catalog))
    return fail(error, "trap catalog differs from canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_trap_catalog_ir(const TrapCatalogIR &catalog) {
  std::ostringstream out;
  out << "trap_catalog id=" << catalog.raw_topic_id
      << " heading=" << catalog.heading_level << " title='" << catalog.title
      << "' origin=" << catalog.origin_column << " labels=[";
  for (std::size_t index = 0; index < catalog.label_vocabulary.size(); ++index)
    out << (index == 0 ? "" : "|") << catalog.label_vocabulary[index];
  out << "] anchors=" << catalog.anchors.size()
      << " introduction=" << catalog.introduction.size()
      << " entries=" << catalog.entries.size() << '\n';
  for (const auto &paragraph : catalog.introduction) {
    out << "introduction spans=" << paragraph.leading_spans.size()
        << " rows=" << paragraph.source_rows.size() << " text='"
        << paragraph.text << "'\n";
  }
  for (const auto &entry : catalog.entries) {
    out << "entry id='" << entry.id << "' operand='" << entry.operand
        << "' source=" << entry.start_source.logical_record << ':'
        << entry.start_source.segment_index << " cells=" << entry.cells.size()
        << " suppressed_rows=" << entry.suppressed_rows.size()
        << " headline='" << entry.headline.body.text << "'";
    for (const auto &field : entry.fields)
      out << " field='" << field.label_text << "' text='"
          << field.line.body.text << "'";
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
