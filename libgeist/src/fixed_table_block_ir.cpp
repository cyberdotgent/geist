#include "geist/detail/fixed_table_block_ir.hpp"

#include "geist/detail/figure_block_ir.hpp"
#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/display_lines.hpp"
#include "geist/detail/selector_link_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using SourcePosition = std::pair<std::uint32_t, std::size_t>;

constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

// Widest display line seen in a hosted fixed table (GG24-4302-00 10.2 box
// is 120 columns); a line wider than a 132-column page ran two lines
// together.
constexpr std::size_t kPageWidth = 132;

constexpr std::uint16_t kHorizontal = 0x2500;
constexpr std::uint16_t kVertical = 0x2502;

// Source coordinates of one whole display line's token run.
DocumentSourceSliceIR display_line_slice(
    const DecodedLogicalRecordSource &record, std::size_t token_begin,
    std::size_t token_end) {
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.token_begin = token_begin;
  slice.token_end = token_end;
  for (const auto &segment : record.control_segments)
    if (std::binary_search(segment.source_tokens.begin(),
                           segment.source_tokens.end(), token_begin))
      slice.segment_index = segment.segment_index;
  if (token_begin < token_end && token_end <= record.ir.tokens.size()) {
    slice.byte_begin = record.ir.tokens[token_begin].byte_range.begin;
    slice.byte_end = record.ir.tokens[token_end - 1].byte_range.end;
  }
  return slice;
}

bool left_border(std::uint16_t word) {
  return word == 0x250c || word == 0x251c || word == 0x2514;
}

bool right_border(std::uint16_t word) {
  return word == 0x2510 || word == 0x2524 || word == 0x2518;
}

bool junction(std::uint16_t word) {
  return word == 0x252c || word == 0x253c || word == 0x2534;
}

bool line_start(std::uint16_t word) {
  return left_border(word) || word == kVertical;
}

bool box_glyph(std::uint16_t word) {
  return word == kHorizontal || word == kVertical || left_border(word) ||
         right_border(word) || junction(word);
}

CellKey key(const PositionedRowCellIR &cell) {
  return {cell.logical_record, cell.token_index, cell.word_index};
}

// One source word of the table envelope after control opcode/operand words
// and spacing prefixes were removed.
struct Word {
  const DecodedLogicalRecordSource *record = nullptr;
  std::size_t token = 0;
  std::size_t word = 0;
  std::uint16_t value = 0;
  std::size_t output = npos;
  const PositionedRowCellIR *positioned = nullptr;
  const OwnedSourceCellIR *owned = nullptr;
  bool token_start = false;
  std::uint8_t width = 0;
  bool spaces_token = false;
  // A token that is only a spacing prefix (`0`..`3`): attachment between
  // words, or a paragraph break before a line origin. Never displayed.
  bool control_only = false;
  // The token lies inside some control segment's source tokens (a token
  // between segments is a hidden slot, e.g. `,` before SRETBL).
  bool covered = false;
  std::size_t segment = 0;
};

struct PlacedWord {
  std::size_t column = 0;
  const Word *word = nullptr;
};

struct Line {
  const DecodedLogicalRecordSource *record = nullptr;
  std::size_t first_token = 0;
  bool rule = false;
  bool right_implied = false;
  // The top rule lost its right corner to a following control; the width is
  // taken from its last rule glyph.
  bool corner_implied = false;
  // Gap geometry: a control-only prefix token closed the previous line, so
  // the hosted page shows a blank line before this one (row separator).
  bool paragraph_before = false;
  // Gap geometry: content columns that start a cell for certain (first word
  // of the line, words after an unambiguous in-line gap of two or more
  // blank columns).
  std::vector<std::size_t> cell_starts;
  std::vector<PlacedWord> words;
  std::vector<const Word *> structural_before;
  std::vector<std::size_t> junctions;
  std::vector<std::size_t> verticals;
  std::vector<FontSpanIR> font_spans;
};

struct FlatRow {
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  const PhysicalRowIR *row = nullptr;
};

std::vector<FlatRow> flatten(const LayoutIR &layout) {
  std::vector<FlatRow> rows;
  for (const auto &run : layout.runs)
    for (std::size_t index = 0; index < run.rows.size(); ++index)
      rows.push_back({run.id, index, &run.rows[index]});
  return rows;
}

struct Candidate {
  const DecodedLogicalRecordSource *start_record = nullptr;
  const ControlSegmentIR *start = nullptr;
  std::uint32_t end_record = 0;
  std::size_t end_token = 0;
  std::string object_id;
  bool declared_table = false;
  std::string failure;
};

std::string object_id_of(const ControlSegmentIR &segment) {
  const auto opcode = segment.opcode;
  if (ascii_lower(opcode.substr(0, 5)) == "srtbl")
    return opcode.substr(5);
  return opcode;
}

// The segment's source tokens spelled as lower-case ASCII with blanks
// removed, so `cz OFF TABLE` reads `czofftable`. Non-ASCII words become `?`
// and never join two words into a keyword.
std::string segment_keyword(const DecodedLogicalRecordSource &record,
                            const ControlSegmentIR &segment) {
  std::string text;
  for (const auto token : segment.source_tokens) {
    if (token >= record.tokens.size())
      continue;
    for (const auto word : record.tokens[token]) {
      if (word < 4 || word == ' ' || word == 0xA0)
        continue;
      text.push_back(word > 0x7f
                         ? '?'
                         : ascii_lower_char(static_cast<char>(word)));
    }
  }
  return text;
}

// True when a `cz OFF TABLE` layout directive opens this SRTBL envelope: the
// nearest preceding control segment, skipping display text, spells it.
// Evidence: SC09-2417-00 4.1.4.1 record 723 segment 15 is `cz OFF TABLE` and
// segment 16 `SRTBLLNKSPEC`, closed by `SRETBL` / `cz OFF ETABLE 0 0`;
// GG24-4302-00 10.2 record 713 opens `SRTBLDBCTL51` with no directive at all.
bool declared_table_region(
    const std::vector<DecodedLogicalRecordSource> &records,
    std::size_t record_index, std::size_t segment_index) {
  for (auto record = record_index + 1; record-- > 0;) {
    const auto &scanned = records[record];
    auto at = record == record_index ? segment_index
                                     : scanned.control_segments.size();
    while (at-- > 0) {
      const auto &segment = scanned.control_segments[at];
      if (segment.kind == BookControlKind::text || segment.display_text)
        continue;
      return segment.kind == BookControlKind::layout_directive &&
             segment_keyword(scanned, segment).rfind("czofftable", 0) == 0;
    }
    if (record == 0)
      break;
  }
  return false;
}

std::vector<Candidate>
find_candidates(const std::vector<DecodedLogicalRecordSource> &records) {
  std::vector<Candidate> result;
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto &record = records[index];
    for (std::size_t segment_index = 0;
         segment_index < record.control_segments.size(); ++segment_index) {
      const auto &segment = record.control_segments[segment_index];
      if (segment.kind != BookControlKind::table_start ||
          segment.source_tokens.empty())
        continue;
      Candidate candidate;
      candidate.start_record = &record;
      candidate.start = &segment;
      candidate.object_id = object_id_of(segment);
      candidate.declared_table =
          declared_table_region(records, index, segment_index);
      bool closed = false;
      for (std::size_t scan = index; scan < records.size() && !closed;
           ++scan) {
        const auto &scanned = records[scan];
        const auto first = scan == index ? segment_index + 1 : std::size_t{0};
        for (std::size_t at = first; at < scanned.control_segments.size();
             ++at) {
          const auto &next = scanned.control_segments[at];
          if (next.kind == BookControlKind::table_start) {
            candidate.failure = "nested table envelope";
            closed = true;
            break;
          }
          if (next.kind == BookControlKind::table_end) {
            candidate.end_record = scanned.logical_record;
            candidate.end_token = next.source_tokens.empty()
                                      ? scanned.tokens.size()
                                      : next.source_tokens.front();
            closed = true;
            break;
          }
        }
      }
      if (!closed)
        candidate.failure = "table envelope is not closed by SRETBL";
      result.push_back(std::move(candidate));
    }
  }
  return result;
}

class Extractor {
public:
  Extractor(const std::vector<DecodedLogicalRecordSource> &records,
            const LayoutIR &layout, const OwnershipIR &ownership)
      : records_(records), flat_(flatten(layout)) {
    for (const auto &cell : ownership.cells)
      dispositions_.emplace(
          CellKey{cell.logical_record, cell.token_index, cell.word_index},
          &cell);
    for (const auto &cell : ownership.row_cells)
      positioned_.emplace(key(cell), &cell);
    for (const auto &row : flat_)
      rows_by_id_.emplace(std::make_pair(row.run, row.row_index), row.row);
    for (const auto &cell : ownership.row_cells)
      by_row_[{cell.run, cell.row_index}].push_back(&cell);
    for (std::size_t index = 0; index < records_.size(); ++index)
      record_index_.emplace(records_[index].logical_record, index);
  }

  const std::vector<FlatRow> &flat() const { return flat_; }

  std::optional<FixedTableBlockIR> extract(const Candidate &candidate,
                                           const LayoutRowRangeIR &range,
                                           bool &outside_range,
                                           std::string &reason) {
    outside_range = false;
    if (!candidate.failure.empty()) {
      reason = candidate.failure;
      return std::nullopt;
    }
    const SourcePosition start{candidate.start_record->logical_record,
                               candidate.start->source_tokens.front()};
    const SourcePosition end{candidate.end_record, candidate.end_token};

    // Physical rows owned by the envelope.
    std::size_t first = npos;
    std::size_t last = npos;
    for (std::size_t ordinal = 0; ordinal < flat_.size(); ++ordinal) {
      const auto &row = *flat_[ordinal].row;
      const SourcePosition row_begin{row.logical_record, row.token_begin};
      const SourcePosition row_end{row.logical_record, row.token_end};
      if (!(row_begin < end && start < row_end))
        continue;
      if (row_begin < start || end < row_end) {
        reason = "physical row straddles the table envelope";
        return std::nullopt;
      }
      if (first == npos)
        first = ordinal;
      else if (ordinal != last + 1) {
        reason = "table envelope rows are not contiguous";
        return std::nullopt;
      }
      last = ordinal;
    }
    if (first == npos) {
      reason = "table envelope owns no physical rows";
      return std::nullopt;
    }
    if (last < range.begin || first >= range.end) {
      outside_range = true;
      return std::nullopt;
    }
    if (first < range.begin || last >= range.end) {
      reason = "table envelope crosses the requested row range";
      return std::nullopt;
    }

    FixedTableBlockIR block;
    block.rows = {first, last + 1};
    for (auto ordinal = first; ordinal <= last; ++ordinal)
      block.source_rows.push_back(
          {flat_[ordinal].run, flat_[ordinal].row_index});
    block.object_id = candidate.object_id;
    block.object_source.logical_record =
        candidate.start_record->logical_record;
    block.object_source.segment_index = candidate.start->segment_index;
    block.object_source.token_begin = candidate.start->source_tokens.front();
    block.object_source.token_end = candidate.start->source_tokens.back() + 1;
    const auto &ir_tokens = candidate.start_record->ir.tokens;
    if (block.object_source.token_end <= ir_tokens.size()) {
      block.object_source.byte_begin =
          ir_tokens[block.object_source.token_begin].byte_range.begin;
      block.object_source.byte_end =
          ir_tokens[block.object_source.token_end - 1].byte_range.end;
    }

    collect_index_lines(start, end, block);
    block.source_declared_table = candidate.declared_table;

    auto table = block;
    std::string table_reason;
    const auto table_proven =
        extract_table_geometry(candidate, start, end, table, table_reason);
    lines_.clear();
    std::string preformatted_reason;
    if (table_proven) {
      // The column model owns every cell of the region, so the verbatim
      // lines are recorded alongside it without claiming anything: the
      // lowering renders the lines (the file holds character art, not a
      // grid) while the recovered columns stay in the IR for consumers,
      // provenance and the geometry tests.
      auto lines_only = block;
      if (admit_preformatted(candidate, start, end, lines_only,
                             preformatted_reason)) {
        table.preformatted_lines = std::move(lines_only.preformatted_lines);
        table.pictures = std::move(lines_only.pictures);
      }
      return table;
    }
    // No column structure was proven.  The envelope's display lines still
    // are, through the record's length-byte line model, so the region is
    // reproduced verbatim instead of failing the whole topic.
    if (admit_preformatted(candidate, start, end, block, preformatted_reason))
      return block;
    reason = table_reason + "; not preformatted: " + preformatted_reason;
    return std::nullopt;
  }

  // The first token of `line` that carries a displayable word, i.e. that is
  // neither a spacing prefix (`0`..`3`) nor all blanks.
  static std::size_t first_visible_token(
      const DecodedLogicalRecordSource &record, const DisplayLineIR &line) {
    for (auto token = line.prefix_token + 1;
         token < line.token_end && token < record.tokens.size(); ++token) {
      const auto &values = record.tokens[token];
      if (values.empty())
        continue;
      if (std::all_of(values.begin(), values.end(), [](const auto value) {
            return value < 4 || value == ' ' || value == 0xA0;
          }))
        continue;
      return token;
    }
    return npos;
  }

  // A display line of the envelope whose first visible word is the `SI`
  // subject-index keyword is an index entry, not table material: hosted
  // BookServer prints no part of it (SC09-138 4.1.4 `LANG`, DT
  // 19910321130500).  Recorded only when no word of the line holds a
  // positioned display cell, so a line the layout does place stays with the
  // geometry that owns it.
  void collect_index_lines(const SourcePosition &start,
                           const SourcePosition &end,
                           FixedTableBlockIR &block) const {
    for (const auto &record : records_) {
      if (record.logical_record < start.first ||
          record.logical_record > end.first)
        continue;
      const auto parsed = record_display_lines(record);
      if (!parsed)
        continue;
      for (const auto &line : *parsed) {
        if (record.logical_record == start.first &&
            line.prefix_token <= start.second)
          continue;
        if (record.logical_record == end.first && line.token_end > end.second)
          continue;
        const auto keyword = first_visible_token(record, line);
        if (keyword == npos)
          continue;
        if (ascii_lower(token_words_to_ascii(record.tokens[keyword])) != "si")
          continue;
        bool positioned = false;
        for (auto token = keyword; token < line.token_end; ++token)
          for (std::size_t word = 0; word < record.tokens[token].size(); ++word)
            if (positioned_.count({record.logical_record, token, word}) != 0)
              positioned = true;
        if (positioned)
          continue;
        FixedTablePreformattedLineIR entry;
        entry.logical_record = record.logical_record;
        entry.prefix_token = keyword;
        entry.token_end = line.token_end;
        entry.text = trim_trailing_spaces(
            collapse_ascii_whitespace(display_line_text(record, line)));
        block.index_lines.push_back(std::move(entry));
      }
    }
  }

  bool extract_table_geometry(const Candidate &candidate,
                              const SourcePosition &start,
                              const SourcePosition &end,
                              FixedTableBlockIR &block, std::string &reason) {
    if (!has_top_rule(start, end)) {
      block.geometry = FixedTableGeometryIR::gap;
      if (!split_gap_lines(start, end, reason))
        return false;
      attach_font_spans(candidate, start, end);
      if (!build_gap_rows(block, reason))
        return false;
      detect_header(block);
      return true;
    }
    const auto split = split_lines(candidate, start, end, block, reason);
    if (split)
      attach_font_spans(candidate, start, end);
    if (!split)
      return false;
    if (!classify_lines(block, reason))
      return false;
    if (!build_rows(block, reason))
      return false;
    detect_header(block);
    return true;
  }

  // --- Preformatted admission ------------------------------------------
  //
  // Every SRTBL envelope, whatever it draws, is a run of display lines of
  // its logical records: `<length byte><that many bytes of tokens>`
  // (`record_display_lines`).  Hosted BookServer serves those lines
  // verbatim inside `<pre>` -- box rules included -- so an envelope whose
  // column structure cannot be proven is still reproduced exactly instead
  // of failing its topic.  Evidence: SC09-138 7.5.2 `TBLUNIQ116` (a box
  // table whose caption line follows the bottom rule),
  // SC24-5527-02 3.6.2 `TBLUNIQ47`/`TBLUNIQ49` (a single command line
  // wrapped in SRTBL, served as `   <B>vmfrec</B> <B>ppf</B> ...`).
  //
  // Fails closed when a record does not parse into display lines, when the
  // SRTBL or SRETBL control does not sit on a line boundary, when a line
  // mixes a control with visible text, when the envelope carries a
  // selector (its link would have no cell to attach to), or when no
  // non-blank line remains.
  struct PreLine {
    const DecodedLogicalRecordSource *record = nullptr;
    DisplayLineIR line;
  };

  static bool blank_text(const std::string &text) {
    return text.find_first_not_of(' ') == std::string::npos;
  }

  static std::string trim_trailing_spaces(std::string text) {
    const auto last = text.find_last_not_of(' ');
    text.erase(last == std::string::npos ? 0 : last + 1);
    return text;
  }

  std::vector<DocumentSourceRowIR>
  rows_in(const FixedTableBlockIR &block, const PreLine &view) const {
    std::vector<DocumentSourceRowIR> rows;
    for (auto ordinal = block.rows.begin; ordinal < block.rows.end; ++ordinal) {
      const auto &row = *flat_[ordinal].row;
      if (row.logical_record != view.record->logical_record)
        continue;
      if (row.token_end <= view.line.prefix_token ||
          row.token_begin >= view.line.token_end)
        continue;
      rows.push_back({flat_[ordinal].run, flat_[ordinal].row_index});
    }
    return rows;
  }

  const std::vector<std::size_t> &
  byte_offsets(const DecodedLogicalRecordSource &record) const {
    auto found = byte_offsets_.find(record.logical_record);
    if (found != byte_offsets_.end())
      return found->second;
    std::vector<std::size_t> offsets(record.assembled.words.size() + 1);
    for (std::size_t word = 0; word < record.assembled.words.size(); ++word)
      offsets[word + 1] =
          offsets[word] +
          token_words_to_ascii({record.assembled.words[word]}).size();
    return byte_offsets_.emplace(record.logical_record, std::move(offsets))
        .first->second;
  }

  // True when the selector's operand target names a book picture resource,
  // filling in the columns it covers and the resource it addresses.  The
  // operand is `<column> <length> <target>` (`3 11 PIC69`); the picture
  // target spelling is the figure block's (`figure_picture_target`), so both
  // families recognise exactly the same selectors.
  static bool picture_selector(const DecodedLogicalRecordSource &record,
                               const ControlSegmentIR &segment,
                               FixedTablePictureIR *picture = nullptr) {
    const auto text = token_words_to_ascii(record.assembled.words);
    if (segment.operand_range.end <= segment.operand_range.begin ||
        segment.operand_range.begin >= text.size())
      return false;
    const auto operand =
        text.substr(segment.operand_range.begin,
                    std::min(segment.operand_range.end, text.size()) -
                        segment.operand_range.begin);
    std::istringstream input(operand);
    std::string column;
    std::string length;
    std::string target;
    std::string trailing;
    if (!(input >> column >> length >> target))
      return false;
    if (!figure_picture_target(target))
      return false;
    if (picture == nullptr)
      return true;
    // Only a canonical operand can name the columns the image replaces.
    if (input >> trailing)
      return false;
    const auto decimal = [](const std::string &word, std::size_t *value) {
      if (word.empty() || word.size() > 9 ||
          !std::all_of(word.begin(), word.end(), [](const auto ch) {
            return std::isdigit(static_cast<unsigned char>(ch)) != 0;
          }))
        return false;
      *value = static_cast<std::size_t>(std::stoul(word));
      return true;
    };
    if (!decimal(column, &picture->column) ||
        !decimal(length, &picture->length) || picture->length == 0)
      return false;
    picture->resource = figure_picture_resource(target);
    picture->placeholder = figure_picture_placeholder(picture->resource);
    picture->logical_record = record.logical_record;
    picture->segment_index = segment.segment_index;
    return true;
  }

  // Leading payload tokens of a `LNK` selector: the `<...>` alternatives
  // that carry the destination (selector_link_ir.hpp).  They are control
  // metadata -- the hosted page never displays one -- so a selector line
  // that carries them still displays nothing.
  static std::size_t link_alternative_tokens(
      const DecodedLogicalRecordSource &record,
      const ControlSegmentIR &segment) {
    if (segment.kind != BookControlKind::select)
      return 0;
    const auto text = token_words_to_ascii(record.assembled.words);
    const auto operand = ascii_lower(
        text.substr(segment.operand_range.begin,
                    segment.operand_range.end - segment.operand_range.begin));
    const auto last = operand.find_last_not_of(' ');
    if (last == std::string::npos)
      return 0;
    const auto first = operand.find_last_of(' ', last);
    if (operand.substr(first == std::string::npos ? 0 : first + 1,
                       last - (first == std::string::npos ? 0 : first)) != "lnk")
      return 0;
    std::vector<std::string> alternatives;
    for (const auto token : segment.source_tokens) {
      if (token >= record.tokens.size())
        break;
      const auto word = token_words_to_ascii(record.tokens[token]);
      if (word.size() < 2 || word.front() != '<' || word.back() != '>')
        continue;
      alternatives.push_back(word);
    }
    std::string error;
    if (!parse_selector_link(alternatives, &error))
      return 0;
    return alternatives.size();
  }

  // True when every token of `line` after its length byte lies in the
  // control's opcode/operand bytes, i.e. the line displays nothing.  A `LNK`
  // selector's alternative tokens count with the operands.
  static bool control_only_line(const DecodedLogicalRecordSource &record,
                                const std::vector<std::size_t> &offsets,
                                const ControlSegmentIR &segment,
                                const DisplayLineIR &line) {
    if (segment.payload_range.end <= segment.payload_range.begin)
      return true;
    auto boundary = segment.payload_range.begin;
    auto alternatives = link_alternative_tokens(record, segment);
    if (alternatives != 0) {
      for (const auto token : segment.source_tokens) {
        if (token >= record.tokens.size())
          break;
        const auto word = token_words_to_ascii(record.tokens[token]);
        if (word.size() < 2 || word.front() != '<' || word.back() != '>')
          continue;
        for (const auto &span : record.assembled.tokens)
          if (span.token_index == token && span.output_begin < offsets.size())
            boundary = std::max(
                boundary, offsets[span.output_begin] + word.size());
        if (--alternatives == 0)
          break;
      }
    }
    for (const auto &span : record.assembled.tokens) {
      if (span.token_index <= line.prefix_token ||
          span.token_index >= line.token_end)
        continue;
      if (span.output_begin >= offsets.size())
        return false;
      if (offsets[span.output_begin] >= boundary)
        return false;
    }
    return true;
  }

  bool admit_preformatted(const Candidate &candidate,
                          const SourcePosition &start,
                          const SourcePosition &end,
                          FixedTableBlockIR &block, std::string &reason) {
    const auto begin_record = record_index_.find(start.first);
    const auto end_record = record_index_.find(end.first);
    if (begin_record == record_index_.end() ||
        end_record == record_index_.end() ||
        end_record->second < begin_record->second) {
      reason = "envelope records are not in the topic";
      return false;
    }
    std::vector<PreLine> lines;
    for (auto index = begin_record->second; index <= end_record->second;
         ++index) {
      const auto &record = records_[index];
      const auto parsed = record_display_lines(record);
      if (!parsed) {
        reason = "record " + std::to_string(record.logical_record) +
                 " does not parse into display lines";
        return false;
      }
      for (const auto &line : *parsed)
        lines.push_back({&record, line});
    }

    // The SRTBL control must close a display line and SRETBL must open one:
    // otherwise part of a line lies outside the envelope and the region is
    // not a whole number of lines.
    std::size_t first = lines.size();
    std::size_t last = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto &view = lines[index];
      if (view.record->logical_record == start.first &&
          view.line.prefix_token <= start.second &&
          start.second < view.line.token_end)
        first = index;
      if (view.record->logical_record == end.first &&
          view.line.prefix_token <= end.second &&
          end.second < view.line.token_end)
        last = index;
    }
    if (first == lines.size() || last == lines.size() || last < first) {
      reason = "envelope controls are not on display lines";
      return false;
    }
    // The SRTBL control's segment reaches to the next control, which for a
    // box table is the whole envelope; only the opcode token itself proves
    // the line boundary.
    if (lines[first].line.token_end != start.second + 1) {
      reason = "SRTBL does not close its display line";
      return false;
    }
    for (auto token = lines[last].line.prefix_token + 1; token < end.second;
         ++token)
      for (const auto word : lines[last].record->tokens[token])
        if (word >= 4 && word != ' ' && word != ',' && word != 0xA0) {
          reason = "SRETBL does not open its display line";
          return false;
        }

    // Classify the interior lines.  A line that is exactly one non-text
    // control segment styles or spaces the lines around it and is never
    // displayed; anything else is reproduced.
    std::vector<std::pair<const PreLine *, std::string>> body;
    std::vector<FixedTablePictureIR> pictures;
    for (auto index = first + 1; index < last; ++index) {
      const auto &view = lines[index];
      const auto &record = *view.record;
      const auto content_begin = view.line.prefix_token + 1;
      const auto &offsets = byte_offsets(record);
      bool control_line = false;
      // A control segment reaches from its opcode to the next control, so a
      // segment that started on an earlier line only styles this one; the
      // line is a control line when it opens a segment and carries none of
      // that segment's payload.
      for (const auto &segment : record.control_segments) {
        if (segment.kind == BookControlKind::text ||
            segment.source_tokens.empty())
          continue;
        const auto segment_begin = segment.source_tokens.front();
        if (segment_begin < content_begin ||
            segment_begin >= view.line.token_end)
          continue;
        if (segment.kind != BookControlKind::font &&
            segment.kind != BookControlKind::select &&
            segment.kind != BookControlKind::spacing &&
            segment.kind != BookControlKind::layout_directive) {
          reason = "preformatted region contains control " + segment.opcode;
          return false;
        }
        if (segment_begin != content_begin ||
            !control_only_line(record, offsets, segment, view.line)) {
          reason = "preformatted line mixes control " + segment.opcode +
                   " with display text";
          return false;
        }
        // A `PIC<n>` selector places a picture on the display line that
        // follows it: hosted BookServer serves the image over the columns
        // the selector names and shows the rest of the line unchanged
        // (`FixedTablePictureIR`).  The picture is recorded and its
        // placeholder words are blanked below, so the region keeps both its
        // art and its image.
        if (segment.kind == BookControlKind::select) {
          FixedTablePictureIR picture;
          if (picture_selector(record, segment, &picture)) {
            picture.line = body.size();
            picture.source = display_line_slice(record, view.line.prefix_token,
                                                view.line.token_end);
            pictures.push_back(std::move(picture));
          } else if (picture_selector(record, segment)) {
            reason = "picture selector operand is not canonical";
            return false;
          }
        }
        control_line = true;
      }
      if (control_line)
        continue;
      // A subject-index line displays nothing; it was recorded as an index
      // entry and is not part of the reproduced region.
      const auto index_line = std::any_of(
          block.index_lines.begin(), block.index_lines.end(),
          [&](const auto &entry) {
            return entry.logical_record == record.logical_record &&
                   entry.token_end == view.line.token_end;
          });
      if (index_line)
        continue;
      body.emplace_back(&view,
                        trim_trailing_spaces(
                            display_line_text(record, view.line)));
    }
    // Replace each picture's columns with blanks.  The span must spell the
    // compiler's own placeholder words, which proves the selector's columns
    // are the line-relative ones this text is indexed by; anything else
    // fails closed rather than blanking display text.
    std::sort(pictures.begin(), pictures.end(),
              [](const auto &left, const auto &right) {
                return std::make_pair(left.line, left.column) <
                       std::make_pair(right.line, right.column);
              });
    for (const auto &picture : pictures) {
      if (picture.line >= body.size()) {
        reason = "picture selector has no display line";
        return false;
      }
      auto &text = body[picture.line].second;
      const auto end = std::min(picture.column + picture.length, text.size());
      if (picture.column >= end ||
          trim_ascii(text.substr(picture.column, end - picture.column)) !=
              picture.placeholder) {
        reason = "picture selector does not open its display line: '" + text +
                 "' does not spell '" + picture.placeholder +
                 "' at column " + std::to_string(picture.column);
        return false;
      }
      text.replace(picture.column, end - picture.column,
                   std::string(end - picture.column, ' '));
      text = trim_trailing_spaces(std::move(text));
    }
    // Blank lines around the region are spacing, but a line that carries a
    // picture is never dropped even when blanking emptied it.
    const auto pictured = [&](std::size_t line) {
      return std::any_of(pictures.begin(), pictures.end(),
                         [&](const auto &picture) {
                           return picture.line == line;
                         });
    };
    std::size_t dropped = 0;
    while (!body.empty() && blank_text(body.front().second) &&
           !pictured(dropped)) {
      body.erase(body.begin());
      ++dropped;
    }
    while (!body.empty() && blank_text(body.back().second) &&
           !pictured(dropped + body.size() - 1))
      body.pop_back();
    for (auto &picture : pictures)
      picture.line -= dropped;
    if (body.empty()) {
      reason = "preformatted region has no display lines";
      return false;
    }

    block.geometry = FixedTableGeometryIR::preformatted;
    block.pictures = std::move(pictures);
    for (const auto &[view, text] : body) {
      FixedTablePreformattedLineIR line;
      line.logical_record = view->record->logical_record;
      line.prefix_token = view->line.prefix_token;
      line.token_end = view->line.token_end;
      line.text = text;
      line.rows = rows_in(block, *view);
      line.slice = display_line_slice(*view->record, line.prefix_token,
                                      line.token_end);
      block.preformatted_lines.push_back(std::move(line));
    }
    // Every positioned cell of the envelope's rows belongs to the region.
    for (auto ordinal = block.rows.begin; ordinal < block.rows.end; ++ordinal) {
      const auto found =
          by_row_.find({flat_[ordinal].run, flat_[ordinal].row_index});
      if (found == by_row_.end())
        continue;
      for (const auto *cell : found->second)
        block.structural_cells.push_back(*cell);
    }
    return true;
  }

private:
  std::vector<Word> record_words(const DecodedLogicalRecordSource &record,
                                 std::size_t token_begin,
                                 std::size_t token_end,
                                 bool keep_control_only = false) const {
    std::vector<std::size_t> covered(record.tokens.size(), npos);
    std::vector<bool> selector(record.tokens.size(), false);
    for (const auto &segment : record.control_segments)
      for (const auto token : segment.source_tokens)
        if (token < covered.size()) {
          covered[token] = segment.segment_index;
          selector[token] = segment.kind == BookControlKind::select;
        }
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> outputs;
    for (std::size_t output = 0; output < record.assembled.sources.size();
         ++output) {
      const auto &source = record.assembled.sources[output];
      if (source.kind == LogicalWordSourceKind::token_word)
        outputs.emplace(std::make_pair(source.token_index, source.word_index),
                        output);
    }
    std::vector<Word> words;
    std::size_t segment = 0;
    for (auto token = token_begin;
         token < token_end && token < record.tokens.size(); ++token) {
      if (covered[token] != npos)
        segment = covered[token];
      const auto &values = record.tokens[token];
      const auto spaces =
          !values.empty() &&
          std::all_of(values.begin(), values.end(),
                      [](const auto value) { return value == ' '; });
      const auto control_only = values.size() == 1 && values[0] < 4;
      bool first = true;
      for (std::size_t index = 0; index < values.size(); ++index) {
        const auto disposition =
            dispositions_.find({record.logical_record, token, index});
        if (disposition != dispositions_.end() &&
            disposition->second->disposition ==
                SourceDisposition::control_operand &&
            !(keep_control_only && control_only))
          continue;
        // Gap geometry: the link operands of a CSELECT (`LNK <BOOK> <>
        // <SC24-5527-02> <ANY> <HCPA2>`) are opaque, unpositioned words that
        // the hosted page never shows; they are not line content.
        if (keep_control_only && selector[token] &&
            disposition != dispositions_.end() &&
            disposition->second->disposition == SourceDisposition::opaque &&
            positioned_.find({record.logical_record, token, index}) ==
                positioned_.end())
          continue;
        Word word;
        word.record = &record;
        word.token = token;
        word.word = index;
        word.value = values[index];
        const auto output = outputs.find({token, index});
        word.output = output == outputs.end() ? npos : output->second;
        const auto positioned =
            positioned_.find({record.logical_record, token, index});
        word.positioned =
            positioned == positioned_.end() ? nullptr : positioned->second;
        word.owned =
            disposition == dispositions_.end() ? nullptr : disposition->second;
        word.token_start = first;
        word.width = token < record.encoded_tokens.size()
                         ? record.encoded_tokens[token].width
                         : 0;
        word.spaces_token = spaces;
        word.control_only = control_only;
        word.covered = covered[token] != npos;
        word.segment = segment;
        words.push_back(word);
        first = false;
      }
    }
    return words;
  }

  // True when the envelope draws a box: a U+250C top-left corner directly
  // followed by a U+2500 rule or a junction (a lone U+250C is a hidden
  // marker slot glyph). Everything else takes the gap geometry.
  bool has_top_rule(const SourcePosition &start,
                    const SourcePosition &end) const {
    for (const auto &record : records_) {
      if (record.logical_record < start.first ||
          record.logical_record > end.first)
        continue;
      const auto token_begin =
          record.logical_record == start.first ? start.second : 0;
      const auto token_end = record.logical_record == end.first
                                 ? end.second
                                 : record.tokens.size();
      for (auto token = token_begin;
           token < token_end && token < record.tokens.size(); ++token) {
        const auto &values = record.tokens[token];
        for (std::size_t index = 0; index < values.size(); ++index) {
          if (values[index] != 0x250c)
            continue;
          std::uint16_t following = 0;
          if (index + 1 < values.size()) {
            following = values[index + 1];
          } else {
            // Skip control-only attachment tokens between the glyphs.
            auto next = token + 1;
            while (next < token_end && next < record.tokens.size() &&
                   record.tokens[next].size() == 1 &&
                   record.tokens[next][0] < 4)
              ++next;
            if (next < token_end && next < record.tokens.size() &&
                !record.tokens[next].empty()) {
              const auto &words = record.tokens[next];
              following = words[0] < 4 && words.size() > 1 ? words[1] : words[0];
            }
          }
          if (following == kHorizontal || junction(following))
            return true;
        }
      }
    }
    return false;
  }

  static std::string position(const Word &word) {
    return std::to_string(word.record->logical_record) + ":" +
           std::to_string(word.token) + " '" +
           token_words_to_ascii(word.record->tokens[word.token]) + "'";
  }

  static std::size_t skip_token(const std::vector<Word> &words,
                                std::size_t at) {
    const auto token = words[at].token;
    while (at < words.size() && words[at].token == token)
      ++at;
    return at;
  }

  // True when the word at `at` opens the next display line: an optional
  // hidden one-byte marker slot, an exact-space origin and a border glyph
  // that cannot be an in-line separator of the current line.
  static bool line_boundary(const std::vector<Word> &words, std::size_t at,
                            std::size_t base, bool width_known,
                            std::size_t left, std::size_t right,
                            const std::set<std::size_t> &known) {
    const auto &word = words[at];
    if (!word.token_start)
      return false;
    std::size_t glyph = npos;
    if (word.spaces_token) {
      glyph = skip_token(words, at);
      // A hidden marker slot can itself decode to spaces; it is then
      // followed by the exact-space origin.
      if (word.width == 1 && glyph < words.size() &&
          words[glyph].spaces_token && words[glyph].token_start)
        glyph = skip_token(words, glyph);
    } else if (word.width == 1) {
      const auto origin = skip_token(words, at);
      if (origin >= words.size() || !words[origin].spaces_token ||
          !words[origin].token_start)
        return false;
      glyph = skip_token(words, origin);
    } else {
      return false;
    }
    auto origin = glyph - 1;
    if (glyph < words.size() && words[glyph].value == '|' &&
        words[glyph].token_start) {
      // Reflow-off visual `|` marker before the border.
      const auto after = skip_token(words, glyph);
      if (after < words.size() && words[after].token == words[glyph].token + 1)
        glyph = after;
    }
    if (glyph >= words.size() || !line_start(words[glyph].value) ||
        words[glyph].output == npos)
      return false;
    if (!width_known)
      return at > 0 && words[at - 1].segment != word.segment;
    // The exact-space origin directly precedes the glyph (or its visual
    // marker) and puts the glyph at the box's left column; a marker slot
    // that decodes to spaces or to a border glyph does not satisfy this.
    if (words[origin].output == npos ||
        words[glyph].output != words[origin].output + left ||
        !words[origin].spaces_token)
      return false;
    if (words[glyph].output < base)
      return true;
    const auto column = words[glyph].output - base;
    return column != right && known.count(column) == 0;
  }

  bool split_lines(const Candidate &candidate, const SourcePosition &start,
                   const SourcePosition &end, FixedTableBlockIR &block,
                   std::string &reason) {
    (void)candidate;
    words_.clear();
    lines_.clear();
    trailing_.clear();
    bool width_known = false;
    std::set<std::size_t> known;
    std::vector<const Word *> pending;
    for (const auto &record : records_) {
      if (record.logical_record < start.first ||
          record.logical_record > end.first)
        continue;
      const auto token_begin =
          record.logical_record == start.first ? start.second : 0;
      const auto token_end = record.logical_record == end.first
                                 ? end.second
                                 : record.tokens.size();
      words_.push_back(record_words(record, token_begin, token_end));
      const auto &words = words_.back();
      std::size_t at = 0;
      while (at < words.size()) {
        const auto &word = words[at];
        Line line;
        line.record = &record;
        std::size_t base = 0;
        // A logical-record boundary may drop the marker slot and origin of
        // the line it opens; the left border is then the record's first word.
        const auto marker_then_origin = [&] {
          if (word.width != 1 || !word.token_start)
            return false;
          const auto origin = skip_token(words, at);
          if (origin >= words.size() || !words[origin].spaces_token)
            return false;
          const auto glyph = skip_token(words, origin);
          return glyph < words.size() && line_start(words[glyph].value);
        };
        if (at == 0 && line_start(word.value) && width_known &&
            !marker_then_origin()) {
          line.first_token = word.token;
          if (word.output != npos) {
            if (word.output < block.left_column) {
              reason = "table line has no display position at " +
                       position(word);
              return false;
            }
            base = word.output - block.left_column;
          } else {
            // The record assembler drops a leading border glyph from its
            // output map; the decoder still inserts one space after it, so
            // the next positioned word fixes the line base.
            std::size_t next = at + 1;
            while (next < words.size() && words[next].output == npos)
              ++next;
            if (next >= words.size() ||
                words[next].output < block.left_column + 2) {
              reason = "table line has no display position at " +
                       position(word);
              return false;
            }
            base = words[next].output - (block.left_column + 2);
            line.words.push_back({block.left_column, &word});
            ++at;
          }
        } else if (word.spaces_token && word.token_start) {
          auto next = skip_token(words, at);
          // A reflow-off visual `|` row marker may sit between the origin
          // and the left border; BookServer strips it.
          if (next < words.size() && words[next].value == '|' &&
              words[next].token_start && next + 1 < words.size() &&
              words[next + 1].token != words[next].token)
            next = skip_token(words, next);
          if (next < words.size() && line_start(words[next].value) &&
              words[next].token_start && word.output != npos &&
              words[next].output != npos &&
              (!width_known ||
               words[next].output == word.output + block.left_column)) {
            for (auto index = at; index < next; ++index)
              pending.push_back(&words[index]);
            base = word.output;
            line.first_token = word.token;
            at = next;
          } else if (word.width == 1) {
            const auto stop = skip_token(words, at);
            for (auto index = at; index < stop; ++index)
              pending.push_back(&words[index]);
            at = stop;
            continue;
          } else {
            reason = "visible source between table lines at " + position(word);
            return false;
          }
        } else if (word.width == 1 && word.token_start) {
          const auto stop = skip_token(words, at);
          for (auto index = at; index < stop; ++index)
            pending.push_back(&words[index]);
          at = stop;
          continue;
        } else {
          reason = "visible source between table lines at " + position(word);
          return false;
        }
        line.structural_before = std::move(pending);
        pending.clear();

        // Read the line through its right border, or to the point where the
        // next line's marker/origin proves the rest of this line is blank.
        enum class End { none, border, pattern, record };
        auto ended = End::none;
        while (at < words.size()) {
          const auto &current = words[at];
          if (current.output == npos || current.output < base) {
            reason = "table line has no display position at " +
                     position(current);
            return false;
          }
          const auto column = current.output - base;
          if (!width_known) {
            if (!line.words.empty() &&
                line_boundary(words, at, base, false, 0, 0, known)) {
              ended = End::pattern;
              break;
            }
            if (line.words.empty()) {
              block.left_column = column;
            } else if (right_border(current.value)) {
              line.words.push_back({column, &current});
              block.width = column - block.left_column + 1;
              width_known = true;
              ended = End::border;
              ++at;
              break;
            }
            line.words.push_back({column, &current});
            ++at;
            continue;
          }
          const auto right = block.left_column + block.width - 1;
          if (column == right) {
            if (!right_border(current.value) && current.value != kVertical) {
              reason = "table line does not end with a border at " +
                       position(current);
              return false;
            }
            line.words.push_back({column, &current});
            ++at;
            ended = End::border;
            break;
          }
          if (!line.words.empty() &&
              line_boundary(words, at, base, true, block.left_column, right,
                            known)) {
            ended = End::pattern;
            break;
          }
          if (column > right) {
            reason = "table line exceeds the box width at " +
                     position(current) + " column " + std::to_string(column);
            return false;
          }
          line.words.push_back({column, &current});
          ++at;
        }
        if (ended == End::none)
          ended = End::record;
        if (ended != End::border) {
          if (line.words.empty()) {
            reason = "table line is empty";
            return false;
          }
          if (!width_known) {
            if (ended == End::record) {
              reason = "box has no top rule";
              return false;
            }
            block.width = line.words.back().column + 2 - block.left_column;
            width_known = true;
            line.corner_implied = true;
          }
          line.right_implied = true;
        }
        if (!line.words.empty() && left_border(line.words.front().word->value))
          for (std::size_t index = 1; index < line.words.size(); ++index)
            if (junction(line.words[index].word->value))
              known.insert(line.words[index].column);
        lines_.push_back(std::move(line));
      }
    }
    trailing_ = std::move(pending);
    if (lines_.empty()) {
      reason = "table envelope has no display lines";
      return false;
    }
    return true;
  }

  bool classify_lines(FixedTableBlockIR &block, std::string &reason) {
    const auto right = block.left_column + block.width - 1;
    for (auto &line : lines_) {
      if (line.words.empty() || line.words.front().column != block.left_column) {
        reason = "table line does not start at the box border";
        return false;
      }
      const auto first = line.words.front().word->value;
      if (left_border(first)) {
        line.rule = true;
      } else if (first == kVertical) {
        line.rule = false;
      } else {
        reason = "table line does not start with a border";
        return false;
      }
      if (!line.right_implied) {
        const auto last = line.words.back().word->value;
        if (line.words.back().column != right ||
            (line.rule ? !right_border(last) : last != kVertical)) {
          reason = "table line does not end with a matching border";
          return false;
        }
      }
      std::size_t expected = block.left_column + 1;
      for (std::size_t index = 1; index < line.words.size(); ++index) {
        const auto &placed = line.words[index];
        if (index + 1 == line.words.size() && !line.right_implied)
          break;
        if (line.rule) {
          if (placed.column != expected) {
            reason = "rule line has a gap at " + position(*placed.word) +
                     " column " + std::to_string(placed.column) +
                     " expected " + std::to_string(expected);
            return false;
          }
          ++expected;
          if (junction(placed.word->value))
            line.junctions.push_back(placed.column);
          else if (placed.word->value != kHorizontal) {
            reason = "rule line carries a non-rule glyph";
            return false;
          }
        } else if (placed.word->value == kVertical) {
          line.verticals.push_back(placed.column);
        } else if (box_glyph(placed.word->value)) {
          reason = "box glyph inside a table cell";
          return false;
        }
      }
      if (line.rule && expected != right) {
        reason = "rule line has a gap";
        return false;
      }
    }
    if (!lines_.front().rule || lines_.front().words.front().word->value != 0x250c) {
      reason = "box has no top rule";
      return false;
    }
    if (!lines_.back().rule || lines_.back().words.front().word->value != 0x2514) {
      reason = "box is not closed by a bottom rule";
      return false;
    }
    std::set<std::size_t> separators;
    for (const auto &line : lines_)
      if (line.rule)
        separators.insert(line.junctions.begin(), line.junctions.end());
    if (separators.empty()) {
      reason = "box has a single column";
      return false;
    }
    block.separator_columns.assign(separators.begin(), separators.end());
    return true;
  }

  static bool blank_word(std::uint16_t value) {
    return value == ' ' || value == kVertical || value == 0x2666;
  }

  static bool blank_line(const Line &line) {
    for (std::size_t index = 1; index + 1 < line.words.size(); ++index)
      if (!blank_word(line.words[index].word->value))
        return false;
    if (line.right_implied && line.words.size() > 1 &&
        !blank_word(line.words.back().word->value))
      return false;
    return true;
  }

  void claim(std::vector<PositionedRowCellIR> &target, const Word &word) {
    if (word.positioned != nullptr)
      target.push_back(*word.positioned);
  }

  bool build_rows(FixedTableBlockIR &block, std::string &reason) {
    const auto right = block.left_column + block.width - 1;
    std::vector<std::size_t> boundaries;
    boundaries.push_back(block.left_column);
    boundaries.insert(boundaries.end(), block.separator_columns.begin(),
                      block.separator_columns.end());
    boundaries.push_back(right);
    const auto column_count = boundaries.size() - 1;

    for (const auto *word : trailing_)
      claim(block.structural_cells, *word);

    std::vector<std::vector<const Line *>> groups;
    std::vector<const Line *> current;
    std::size_t rule_index = 0;
    const auto top_junction_free = lines_.front().junctions.empty();
    for (const auto &line : lines_) {
      if (line.rule) {
        for (const auto *word : line.structural_before)
          claim(block.structural_cells, *word);
        for (const auto &placed : line.words)
          claim(block.structural_cells, *placed.word);
        const auto aligned =
            line.right_implied
                ? line.junctions.size() <= block.separator_columns.size() &&
                      std::equal(line.junctions.begin(), line.junctions.end(),
                                 block.separator_columns.begin())
                : std::equal(line.junctions.begin(), line.junctions.end(),
                             block.separator_columns.begin(),
                             block.separator_columns.end());
        if (!aligned && !(rule_index == 0 && line.junctions.empty())) {
          reason = "rule junctions do not align with the column boundaries";
          return false;
        }
        ++rule_index;
        if (!current.empty())
          groups.push_back(std::move(current));
        current.clear();
      } else {
        current.push_back(&line);
      }
    }
    if (!current.empty()) {
      reason = "box is not closed by a bottom rule";
      return false;
    }

    bool first_group = true;
    for (const auto &group : groups) {
      const auto caption =
          first_group && top_junction_free &&
          std::all_of(group.begin(), group.end(), [](const auto *line) {
            return line->verticals.empty();
          });
      if (first_group && top_junction_free && !caption) {
        reason = "junction-free top rule is not followed by a caption row";
        return false;
      }
      first_group = false;
      FixedTableRowIR row;
      row.kind = caption ? FixedTableRowKindIR::caption
                         : FixedTableRowKindIR::body;
      row.line_count = group.size();
      const auto cells = caption ? std::size_t{1} : column_count;
      row.cells.resize(cells);
      for (std::size_t index = 0; index < cells; ++index)
        row.cells[index].column = index;
      const auto blank = std::all_of(group.begin(), group.end(),
                                     [](const auto *line) {
                                       return blank_line(*line);
                                     });
      for (const auto *line : group) {
        const auto aligned =
            line->right_implied
                ? line->verticals.size() <= block.separator_columns.size() &&
                      std::equal(line->verticals.begin(),
                                 line->verticals.end(),
                                 block.separator_columns.begin())
                : std::equal(line->verticals.begin(), line->verticals.end(),
                             block.separator_columns.begin(),
                             block.separator_columns.end());
        if (!caption && !aligned) {
          reason = "content line separators do not align with the column "
                   "boundaries";
          return false;
        }
        auto &structural = blank ? block.structural_cells : row.structural_cells;
        for (const auto *word : line->structural_before)
          claim(structural, *word);
        std::vector<TokenWords> texts(cells);
        std::vector<std::vector<PositionedRowCellIR>> claims(cells);
        std::vector<std::vector<OwnedSourceCellIR>> unpositioned(cells);
        std::vector<std::vector<const Word *>> cell_words(cells);
        for (std::size_t index = 0; index < line->words.size(); ++index) {
          const auto &placed = line->words[index];
          const auto edge =
              index == 0 ||
              (index + 1 == line->words.size() && !line->right_implied);
          if (edge || placed.word->value == kVertical ||
              placed.word->value == ' ' || placed.word->value == 0x2666 ||
              blank) {
            claim(structural, *placed.word);
            continue;
          }
          std::size_t cell = 0;
          if (!caption) {
            while (cell + 1 < boundaries.size() &&
                   boundaries[cell + 1] < placed.column)
              ++cell;
            if (cell >= cells || placed.column <= boundaries[cell] ||
                placed.column >= boundaries[cell + 1]) {
              reason = "cell word lies on a column boundary";
              return false;
            }
          }
          const auto span_begin = caption ? block.left_column : boundaries[cell];
          const auto offset = placed.column - span_begin - 1;
          auto &text = texts[cell];
          if (text.size() <= offset)
            text.resize(offset + 1, ' ');
          text[offset] = placed.word->value;
          cell_words[cell].push_back(placed.word);
          if (placed.word->positioned != nullptr) {
            claims[cell].push_back(*placed.word->positioned);
          } else if (placed.word->owned != nullptr &&
                     placed.word->owned->run == 0 &&
                     placed.word->owned->disposition ==
                         SourceDisposition::opaque) {
            unpositioned[cell].push_back(*placed.word->owned);
          } else {
            reason = "cell word has no source cell at " + position(*placed.word);
            return false;
          }
        }
        if (blank)
          continue;
        for (std::size_t cell = 0; cell < cells; ++cell) {
          if (claims[cell].empty() && unpositioned[cell].empty())
            continue;
          FixedTableCellLineIR cell_line;
          cell_line.text = trim_ascii(token_words_to_ascii(texts[cell]));
          if (cell_line.text.empty()) {
            // Only placeholder glyphs: structural, never an empty text line.
            for (const auto &claim : claims[cell])
              row.structural_cells.push_back(claim);
            continue;
          }
          cell_line.source_cells = std::move(claims[cell]);
          cell_line.unpositioned_cells = std::move(unpositioned[cell]);
          cell_line.slice = line_slice(cell_words[cell]);
          row.cells[cell].lines.push_back(std::move(cell_line));
        }
      }
      if (blank)
        continue;
      row.source_rows = row_sources(row);
      if (caption)
        block.caption = std::move(row);
      else
        block.body.push_back(std::move(row));
    }
    if (block.body.empty()) {
      reason = "box has no content rows";
      return false;
    }
    return true;
  }

  // ---- Gap geometry (rule-less tables) ----------------------------------

  static bool glyph_word(std::uint16_t value) {
    return (value >= 0x2500 && value <= 0x257f) || value == 0x2666;
  }

  // Every word of the one-byte token at `at` is a box/placeholder glyph.
  static bool glyph_token(const std::vector<Word> &words, std::size_t at) {
    const auto stop = skip_token(words, at);
    for (auto index = at; index < stop; ++index)
      if (!glyph_word(words[index].value))
        return false;
    return true;
  }

  struct OriginMatch {
    bool found = false;
    // First space word of the origin run and first content word after it.
    std::size_t origin = npos;
    std::size_t content = npos;
    // First word of the structural slot run (hidden marker slot, fills,
    // control-only tokens) before the origin; words between the match start
    // and it are content of the current line.
    std::size_t slot_begin = npos;
    // A control-only prefix token opens the slot run (paragraph break).
    bool paragraph = false;
    // The origin is one space and the content a revision bar: the
    // change-bar line shape, always a line boundary.
    bool bar = false;
    // The slot run has no glyph marker and holds one one-byte dictionary
    // word: either a hidden marker slot or the hanging content word of the
    // current line.
    bool ambiguous = false;
  };

  // Matches `[control-only | one-byte token | fill run]* <origin>
  // <content>` at token `at`; the origin is the last space run before
  // same-segment positioned content. With a glyph marker in the run every
  // one-byte dictionary word before it is content; without one, the last
  // dictionary word is the (ambiguous) slot candidate.
  static OriginMatch match_origin(const std::vector<Word> &words,
                                  std::size_t at) {
    OriginMatch match;
    std::size_t index = at;
    std::size_t last_glyph = npos;
    std::size_t last_dictionary = npos;
    std::size_t after_dictionary = at;
    while (index < words.size()) {
      const auto &word = words[index];
      if (!word.token_start)
        return match;
      const auto next = skip_token(words, index);
      if (word.control_only) {
        index = next;
        continue;
      }
      if (word.spaces_token) {
        if (next < words.size() && !words[next].spaces_token &&
            !words[next].control_only &&
            words[next].segment == word.segment &&
            words[next].output != npos && word.output != npos) {
          match.found = true;
          match.origin = index;
          match.content = next;
          break;
        }
        index = next;
        continue;
      }
      if (word.width == 1) {
        if (glyph_token(words, index)) {
          last_glyph = index;
        } else {
          last_dictionary = index;
          after_dictionary = next;
        }
        index = next;
        continue;
      }
      return match;
    }
    if (!match.found)
      return match;
    // A one-space origin followed by a revision bar is the change-bar line
    // shape (` | ====> qquit`): the run before it is structural for
    // certain, its dictionary word a hidden slot.
    match.bar = revision_bar(words, match.content) &&
                skip_token(words, match.origin) == match.origin + 1;
    if (last_glyph != npos) {
      match.slot_begin = last_dictionary != npos && last_dictionary < last_glyph
                             ? after_dictionary
                             : at;
      if (last_dictionary != npos && last_dictionary > last_glyph) {
        match.slot_begin = last_dictionary;
        match.ambiguous = !match.bar;
      }
    } else if (last_dictionary != npos) {
      match.slot_begin = last_dictionary;
      match.ambiguous = !match.bar;
    } else {
      match.slot_begin = at;
    }
    // A control-only token directly before the slot run opens it.
    while (match.slot_begin > at && words[match.slot_begin - 1].control_only &&
           words[match.slot_begin - 1].token_start)
      --match.slot_begin;
    for (auto index = match.slot_begin; index < match.origin; ++index)
      if (words[index].control_only)
        match.paragraph = true;
    return match;
  }

  static bool revision_bar(const std::vector<Word> &words, std::size_t at) {
    return words[at].width == 1 && words[at].value == '|' &&
           words[at].token_start &&
           (at + 1 >= words.size() || words[at + 1].token_start);
  }

  struct GapLineState {
    std::size_t line = npos;
    std::size_t base = 0;
    std::size_t last_content = npos;
    // Content columns that start a cell for certain: the first word of the
    // line and words after an unambiguous in-line gap of two or more blank
    // columns.
    std::vector<std::size_t> starts;
    std::size_t previous_end = 0;
    bool previous_ambiguous = false;
  };

  // A one-byte token that can be a hidden terminal slot before a control:
  // a placeholder run (box glyphs, `?`), a token outside every control
  // segment (`,` after the CFONT payload before SRETBL in QSYSINFO), or
  // attached punctuation repeating the previous token (`built. .` in
  // SC24-5527-02 3.8.4.2). A dictionary word (`Guide`, `and`) never is.
  static bool terminal_slot(const std::vector<Word> &words, std::size_t at) {
    std::size_t begin = at;
    while (begin > 0 && words[begin - 1].token == words[at].token)
      --begin;
    const auto stop = skip_token(words, begin);
    bool placeholder = true;
    for (auto index = begin; index < stop; ++index)
      if (!glyph_word(words[index].value) && words[index].value != '?')
        placeholder = false;
    if (placeholder || !words[begin].covered)
      return true;
    if (begin == 0 || stop - begin != 1)
      return false;
    const auto value = words[begin].value;
    const auto punctuation = value < 0x80 && !std::isalnum(static_cast<int>(value));
    const auto &previous = words[begin - 1];
    return punctuation && previous.width == 1 && previous.value == value &&
           previous.token + 1 == words[begin].token;
  }

  // Closes the open line: a one-byte non-space token that is the last
  // content before the record end or a control is a hidden terminal slot;
  // a line without content (revision bar only) is a blank line.
  void close_gap_line(const std::vector<Word> &words, GapLineState &state,
                      std::vector<const Word *> &pending, bool &paragraph,
                      bool envelope_end = false) {
    if (state.line == npos)
      return;
    auto &line = lines_[state.line];
    line.cell_starts = state.starts;
    if (state.last_content != npos) {
      const auto &last = words[state.last_content];
      if (last.width == 1 && !last.spaces_token) {
        std::size_t after = state.last_content + 1;
        while (after < words.size() &&
               (words[after].token == last.token || words[after].spaces_token ||
                words[after].control_only))
          ++after;
        // Before SRETBL every trailing one-byte token is a hidden slot
        // (`qquit.`, `vmfview build.`, `Guide,`); before a CFONT only the
        // placeholder/uncovered/duplicate shapes are.
        const bool hidden =
            (after >= words.size() && envelope_end) ||
            ((after >= words.size() || words[after].segment != last.segment) &&
             terminal_slot(words, state.last_content));
        if (hidden) {
          std::vector<PlacedWord> kept;
          for (const auto &placed : line.words) {
            if (placed.word->token == last.token)
              line.structural_before.push_back(placed.word);
            else
              kept.push_back(placed);
          }
          line.words = std::move(kept);
        }
      }
    }
    const auto blank = std::all_of(
        line.words.begin(), line.words.end(),
        [](const PlacedWord &placed) { return placed.word->value == ' '; });
    if (blank) {
      for (const auto *word : line.structural_before)
        pending.push_back(word);
      for (const auto &placed : line.words)
        pending.push_back(placed.word);
      paragraph = true;
      lines_.pop_back();
    }
    state = GapLineState{};
  }

  // One pass over the envelope words. `starts` (null in the first pass)
  // resolves ambiguous slot runs: a one-byte word followed by one space run
  // opens a new line only when the run puts the next word on an established
  // cell start column.
  bool scan_gap(const std::set<std::size_t> *starts, std::string &reason) {
    lines_.clear();
    trailing_.clear();
    std::vector<const Word *> pending;
    bool paragraph = false;
    for (std::size_t record_index = 0; record_index < words_.size();
         ++record_index) {
      const auto &words = words_[record_index];
      GapLineState state;
      std::size_t at = 0;
      const auto open_line = [&](const OriginMatch &match, bool para) {
        for (auto index = at; index < match.content; ++index)
          pending.push_back(&words[index]);
        Line line;
        line.record = words[match.origin].record;
        line.first_token = words[match.origin].token;
        line.paragraph_before = paragraph || para;
        line.structural_before = std::move(pending);
        pending.clear();
        paragraph = false;
        lines_.push_back(std::move(line));
        state = GapLineState{};
        state.line = lines_.size() - 1;
        state.base = words[match.origin].output;
        at = match.content;
        if (revision_bar(words, at) && words[at].output == state.base + 1) {
          lines_.back().structural_before.push_back(&words[at]);
          ++at;
        }
      };
      while (at < words.size()) {
        const auto &word = words[at];
        const auto next = skip_token(words, at);
        if (state.line == npos) {
          const auto match = match_origin(words, at);
          if (!match.found) {
            bool content = false;
            for (auto index = at; index < words.size(); ++index)
              if (!words[index].spaces_token && !words[index].control_only &&
                  words[index].width != 1)
                content = true;
            if (content) {
              reason = at == 0 && record_index > 0
                           ? "logical record continues a table line at " +
                                 position(word)
                           : "table line has no origin at " + position(word);
              return false;
            }
            for (auto index = at; index < words.size(); ++index)
              pending.push_back(&words[index]);
            paragraph = paragraph || match.paragraph;
            at = words.size();
            break;
          }
          open_line(match, match.paragraph);
          continue;
        }
        auto &line = lines_[state.line];
        if (word.spaces_token || word.control_only ||
            (word.width == 1 && word.token_start)) {
          const auto match = match_origin(words, at);
          if (match.found) {
            const auto &origin = words[match.origin];
            // A control between the last content and this token: the token
            // opens the next line. A control between this token and the
            // origin: the token still belongs to this line (the terminal
            // slot rule at close hides the last one-byte token).
            const bool control_before =
                state.last_content != npos &&
                word.segment != words[state.last_content].segment;
            const bool control_after = origin.segment != word.segment;
            bool boundary = control_before || (match.bar && !control_after);
            if (!boundary && !control_after && match.origin != at &&
                state.last_content != npos) {
              if (!match.ambiguous) {
                boundary = true;
              } else if (starts != nullptr) {
                const auto new_column =
                    words[match.content].output - origin.output;
                boundary = starts->count(new_column) != 0;
              }
            }
            if (boundary && match.slot_begin == at) {
              close_gap_line(words, state, pending, paragraph);
              open_line(match, match.paragraph);
              continue;
            }
            if (control_after && word.control_only &&
                (next >= words.size() || words[next].segment != word.segment ||
                 words[next].spaces_token || glyph_token(words, next)))
              paragraph = true;
          }
          if (word.spaces_token) {
            for (auto index = at; index < next; ++index) {
              if (words[index].output == npos || words[index].output < state.base) {
                reason = "table line has no display position at " +
                         position(words[index]);
                return false;
              }
              line.words.push_back({words[index].output - state.base,
                                    &words[index]});
            }
            state.previous_ambiguous =
                state.last_content != npos &&
                words[state.last_content].width == 1;
            at = next;
            continue;
          }
          if (word.control_only ||
              (word.width == 1 && glyph_token(words, at))) {
            for (auto index = at; index < next; ++index)
              line.structural_before.push_back(&words[index]);
            at = next;
            continue;
          }
        }
        // Content token.
        bool first_word = true;
        for (auto index = at; index < next; ++index) {
          const auto &current = words[index];
          if (current.value < 4)
            continue;
          if (current.output == npos || current.output < state.base) {
            reason = "table line has no display position at " +
                     position(current);
            return false;
          }
          const auto column = current.output - state.base;
          if (first_word && current.value != ' ') {
            if (state.last_content == npos)
              state.starts.push_back(column);
            else if (column >= state.previous_end + 2 &&
                     !state.previous_ambiguous)
              state.starts.push_back(column);
          }
          first_word = false;
          line.words.push_back({column, &current});
          state.last_content = index;
          state.previous_end = column + 1;
        }
        state.previous_ambiguous = false;
        at = next;
      }
      close_gap_line(words, state, pending, paragraph,
                     record_index + 1 == words_.size());
    }
    trailing_ = std::move(pending);
    return true;
  }

  bool split_gap_lines(const SourcePosition &start, const SourcePosition &end,
                       std::string &reason) {
    words_.clear();
    for (const auto &record : records_) {
      if (record.logical_record < start.first ||
          record.logical_record > end.first)
        continue;
      const auto token_begin =
          record.logical_record == start.first ? start.second : 0;
      const auto token_end = record.logical_record == end.first
                                 ? end.second
                                 : record.tokens.size();
      words_.push_back(record_words(record, token_begin, token_end, true));
    }
    if (!scan_gap(nullptr, reason))
      return false;
    std::set<std::size_t> starts;
    for (const auto &line : lines_)
      starts.insert(line.cell_starts.begin(), line.cell_starts.end());
    if (!scan_gap(&starts, reason))
      return false;
    if (lines_.empty()) {
      reason = "table envelope has no display lines";
      return false;
    }
    return true;
  }

  static bool first_cell_content(const Line &line, std::size_t boundary) {
    for (const auto &placed : line.words)
      if (placed.word->value != ' ' && placed.column < boundary)
        return true;
    return false;
  }

  // Visible extent of the line's words inside [begin, end).
  static std::pair<std::size_t, std::size_t>
  cell_extent(const Line &line, std::size_t begin, std::size_t end) {
    std::size_t low = npos;
    std::size_t high = 0;
    for (const auto &placed : line.words) {
      if (placed.word->value == ' ' || placed.column < begin ||
          placed.column >= end)
        continue;
      low = std::min(low, placed.column);
      high = std::max(high, placed.column + 1);
    }
    return {low, high};
  }

  // A line whose only content is in the first cell continues the previous
  // row when that text could not have fitted on the row's last first-cell
  // line (the cell is at most `starts[1] - starts[0] - 2` wide):
  // `GDDM Interactive` / `Map Definition` in SC33-033 PREFACE.6.
  static bool wrapped_first_cell(
      const std::vector<std::vector<const Line *>> &rows, const Line &line,
      const std::vector<std::size_t> &starts) {
    if (rows.empty() || rows.back().empty())
      return false;
    for (const auto &placed : line.words)
      if (placed.word->value != ' ' && placed.column >= starts[1])
        return false;
    const auto width = starts[1] - starts[0] - 2;
    const auto current = cell_extent(line, starts[0], starts[1]);
    const auto previous = cell_extent(*rows.back().back(), starts[0], starts[1]);
    if (current.first == npos || previous.first == npos)
      return false;
    const auto joined = (previous.second - starts[0]) + 1 +
                        (current.second - current.first);
    return joined > width;
  }

  bool build_gap_rows(FixedTableBlockIR &block, std::string &reason) {
    for (const auto *word : trailing_)
      claim(block.structural_cells, *word);

    // Cell starts: columns where some line starts a word and every line
    // leaves the two preceding columns blank. A first line that is cut off
    // by a paragraph break and does not fit the geometry of the remaining
    // lines is the caption (SC33-033 4.6 `CHAATT      (count, array)`).
    std::size_t first_line = 0;
    std::vector<std::size_t> starts;
    std::size_t first = npos;
    std::size_t extent = 0;
    if (lines_.size() < 2) {
      // One display line cannot establish gap columns (SC24-5527-02
      // 3.8.4.6 `query rdr * all` output, SG24-204 BACK_1.2 addresses).
      reason = "gap table has a single display line";
      return false;
    }
    if (!gap_geometry(0, starts, first, extent)) {
      if (lines_.size() > 2 && lines_[1].paragraph_before &&
          gap_geometry(1, starts, first, extent))
        first_line = 1;
      else {
        reason = starts.empty() ? "gap table has no content rows"
                                : "gap table has a single column";
        return false;
      }
    }
    if (extent > kPageWidth) {
      // A display line cannot exceed the page: the origin pattern was
      // missed and several lines ran together (SC24-5527-02 3.8.4.6
      // `query rdr * all` listing).
      reason = "gap table line exceeds the page width";
      return false;
    }
    block.left_column = first;
    block.width = extent - first;
    block.separator_columns.assign(starts.begin() + 1, starts.end());
    if (first_line == 1) {
      FixedTableRowIR row;
      row.kind = FixedTableRowKindIR::caption;
      row.line_count = 1;
      row.cells.resize(1);
      std::vector<std::size_t> span{0};
      if (!fill_gap_row(row, {&lines_[0]}, span, reason))
        return false;
      row.source_rows = row_sources(row);
      block.caption = std::move(row);
    }

    // Leading lines whose every word is font-highlighted are the header row
    // when a plain line follows them (QSYSINFO `Order No       Title` over a
    // row that starts with `___`).
    std::vector<std::vector<const Line *>> rows;
    std::size_t header_lines = first_line;
    while (header_lines < lines_.size() && highlighted_line(lines_[header_lines]))
      ++header_lines;
    if (header_lines == lines_.size())
      header_lines = first_line;
    if (header_lines > first_line) {
      rows.emplace_back();
      for (std::size_t index = first_line; index < header_lines; ++index)
        rows.back().push_back(&lines_[index]);
    }
    std::vector<std::vector<const Line *>> groups;
    for (std::size_t index = header_lines; index < lines_.size(); ++index) {
      const auto &line = lines_[index];
      if (groups.empty() || (line.paragraph_before && !groups.back().empty()))
        groups.emplace_back();
      groups.back().push_back(&line);
    }
    for (const auto &group : groups) {
      if (!first_cell_content(*group.front(), starts[1])) {
        rows.push_back(group);
        continue;
      }
      for (const auto *line : group) {
        if ((first_cell_content(*line, starts[1]) &&
             !wrapped_first_cell(rows, *line, starts)) ||
            rows.empty())
          rows.emplace_back();
        rows.back().push_back(line);
      }
    }

    for (const auto &group : rows) {
      FixedTableRowIR row;
      row.kind = FixedTableRowKindIR::body;
      row.line_count = group.size();
      row.cells.resize(starts.size());
      if (!fill_gap_row(row, group, starts, reason))
        return false;
      for (const auto &cell : row.cells)
        for (const auto &line : cell.lines)
          if (ragged_gap(line.text)) {
            reason = "cell text has an unaligned gap: '" + line.text + "'";
            return false;
          }
      row.source_rows = row_sources(row);
      block.body.push_back(std::move(row));
    }
    if (block.body.empty()) {
      reason = "gap table has no content rows";
      return false;
    }
    return true;
  }

  // Cell starts over lines_[from..): false when fewer than two cells
  // result (`starts` then holds what was found; empty when no content).
  bool gap_geometry(std::size_t from, std::vector<std::size_t> &starts,
                    std::size_t &first, std::size_t &extent) const {
    starts.clear();
    first = npos;
    extent = 0;
    std::vector<std::set<std::size_t>> occupied;
    std::set<std::size_t> word_starts;
    for (std::size_t index = from; index < lines_.size(); ++index) {
      const auto &line = lines_[index];
      occupied.emplace_back();
      std::size_t previous = npos;
      for (const auto &placed : line.words) {
        if (placed.word->value == ' ')
          continue;
        occupied.back().insert(placed.column);
        if (previous == npos || placed.column != previous + 1)
          word_starts.insert(placed.column);
        previous = placed.column;
        first = std::min(first, placed.column);
        extent = std::max(extent, placed.column + 1);
      }
    }
    if (first == npos)
      return false;
    starts.push_back(first);
    for (const auto column : word_starts) {
      if (column <= first + 1)
        continue;
      bool clear = true;
      for (const auto &columns : occupied)
        if (columns.count(column - 1) != 0 || columns.count(column - 2) != 0) {
          clear = false;
          break;
        }
      if (clear)
        starts.push_back(column);
    }
    return starts.size() >= 2;
  }

  // An internal run of three or more spaces, or of two spaces not closing a
  // sentence, means the words did not share one cell.
  static bool ragged_gap(const std::string &text) {
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
      if (text[index] != ' ' || text[index + 1] != ' ')
        continue;
      std::size_t run = 0;
      while (index + run < text.size() && text[index + run] == ' ')
        ++run;
      const auto before = index > 0 ? text[index - 1] : ' ';
      if (run > 2 || (before != '.' && before != ':' && before != '?' &&
                      before != '!' && before != ')'))
        return true;
      index += run;
    }
    return false;
  }

  // Distributes the lines' words over `row.cells` by the cell start columns
  // and claims their structure on the row.
  bool fill_gap_row(FixedTableRowIR &row,
                    const std::vector<const Line *> &group,
                    const std::vector<std::size_t> &starts,
                    std::string &reason) {
    const auto cells = row.cells.size();
    for (std::size_t index = 0; index < cells; ++index)
      row.cells[index].column = index;
    for (const auto *line : group) {
      for (const auto *word : line->structural_before)
        claim(row.structural_cells, *word);
      std::vector<TokenWords> texts(cells);
      std::vector<std::vector<PositionedRowCellIR>> claims(cells);
      std::vector<std::vector<OwnedSourceCellIR>> unpositioned(cells);
      std::vector<std::vector<const Word *>> cell_words(cells);
      for (const auto &placed : line->words) {
        if (placed.word->value == ' ' || placed.word->value == 0x2666) {
          claim(row.structural_cells, *placed.word);
          continue;
        }
        std::size_t cell = 0;
        while (cell + 1 < cells && starts[cell + 1] <= placed.column)
          ++cell;
        if (placed.column < starts[cell]) {
          reason = "cell word lies left of the first column at " +
                   position(*placed.word);
          return false;
        }
        const auto offset = placed.column - starts[cell];
        auto &text = texts[cell];
        if (text.size() <= offset)
          text.resize(offset + 1, ' ');
        text[offset] = placed.word->value;
        cell_words[cell].push_back(placed.word);
        if (placed.word->positioned != nullptr) {
          claims[cell].push_back(*placed.word->positioned);
        } else if (placed.word->owned != nullptr &&
                   placed.word->owned->run == 0 &&
                   placed.word->owned->disposition ==
                       SourceDisposition::opaque) {
          unpositioned[cell].push_back(*placed.word->owned);
        } else {
          reason = "cell word has no source cell at " + position(*placed.word);
          return false;
        }
      }
      for (std::size_t cell = 0; cell < cells; ++cell) {
        if (claims[cell].empty() && unpositioned[cell].empty())
          continue;
        FixedTableCellLineIR cell_line;
        cell_line.text = trim_ascii(token_words_to_ascii(texts[cell]));
        if (cell_line.text.empty()) {
          for (const auto &claim : claims[cell])
            row.structural_cells.push_back(claim);
          continue;
        }
        cell_line.source_cells = std::move(claims[cell]);
        cell_line.unpositioned_cells = std::move(unpositioned[cell]);
        cell_line.slice = line_slice(cell_words[cell]);
        row.cells[cell].lines.push_back(std::move(cell_line));
      }
    }
    return true;
  }

  static DocumentSourceSliceIR line_slice(const std::vector<const Word *> &words) {
    DocumentSourceSliceIR slice;
    if (words.empty())
      return slice;
    const auto &record = *words.front()->record;
    slice.logical_record = record.logical_record;
    slice.segment_index = words.front()->segment;
    slice.token_begin = words.front()->token;
    slice.token_end = words.front()->token + 1;
    for (const auto *word : words) {
      slice.token_begin = std::min(slice.token_begin, word->token);
      slice.token_end = std::max(slice.token_end, word->token + 1);
    }
    if (slice.token_end <= record.ir.tokens.size()) {
      slice.byte_begin = record.ir.tokens[slice.token_begin].byte_range.begin;
      slice.byte_end = record.ir.tokens[slice.token_end - 1].byte_range.end;
    }
    return slice;
  }

  static std::vector<DocumentSourceRowIR> row_sources(const FixedTableRowIR &row) {
    std::vector<DocumentSourceRowIR> result;
    const auto add = [&](const PositionedRowCellIR &cell) {
      for (const auto &existing : result)
        if (existing.display_run == cell.run && existing.row_index == cell.row_index)
          return;
      result.push_back({cell.run, cell.row_index});
    };
    for (const auto &cell : row.cells)
      for (const auto &line : cell.lines)
        for (const auto &source : line.source_cells)
          add(source);
    for (const auto &source : row.structural_cells)
      add(source);
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
      return std::make_pair(left.display_run, left.row_index) <
             std::make_pair(right.display_run, right.row_index);
    });
    return result;
  }

  void attach_font_spans(const Candidate &candidate,
                         const SourcePosition &start,
                         const SourcePosition &end) {
    (void)candidate;
    for (const auto &record : records_) {
      if (record.logical_record < start.first ||
          record.logical_record > end.first)
        continue;
      for (const auto &segment : record.control_segments) {
        if (segment.kind != BookControlKind::font ||
            segment.source_tokens.empty())
          continue;
        const SourcePosition at{record.logical_record,
                                segment.source_tokens.front()};
        if (at < start || !(at < end))
          continue;
        const auto spans = decode_font_control_spans(record, segment);
        if (!spans)
          continue;
        std::size_t anchor = segment.source_tokens.front();
        for (const auto token : segment.source_tokens) {
          if (token >= record.tokens.size() || record.tokens[token].empty())
            continue;
          // Skip spacing-prefix-only tokens; they are never operands.
          const std::size_t word = record.tokens[token][0] < 4 ? 1 : 0;
          if (word >= record.tokens[token].size())
            continue;
          const auto found =
              dispositions_.find({record.logical_record, token, word});
          if (found != dispositions_.end() &&
              found->second->disposition == SourceDisposition::control_operand)
            anchor = std::max(anchor, token);
        }
        Line *target = nullptr;
        for (auto &line : lines_) {
          if (line.record != &record || line.first_token <= anchor)
            continue;
          if (target == nullptr || line.first_token < target->first_token)
            target = &line;
        }
        if (target != nullptr)
          target->font_spans.insert(target->font_spans.end(),
                                    spans->spans.begin(), spans->spans.end());
      }
    }
  }

  // Header rows are set in a face the hosted page renders bold: HP2, HP3
  // and the RK/H1..H6 phrases. HP1/CIT italics and PK/PV/XPH monospace mark
  // ordinary cell content (QSYSINFO title cells are HP1 under an HP2
  // header).
  static bool header_style(FontStyleIR style) {
    return style == FontStyleIR::highlight_2 ||
           style == FontStyleIR::highlight_3 ||
           style == FontStyleIR::bold_phrase;
  }

  // Every visible word of the line lies inside a bold font span.
  static bool highlighted_line(const Line &line) {
    bool any = false;
    for (const auto &placed : line.words) {
      if (placed.word->value == ' ' || placed.word->value == 0x2666)
        continue;
      any = true;
      const auto highlighted = std::any_of(
          line.font_spans.begin(), line.font_spans.end(),
          [&](const auto &span) {
            return header_style(span.style) &&
                   placed.column >= span.column &&
                   placed.column < span.column + span.length;
          });
      if (!highlighted)
        return false;
    }
    return any;
  }

  // The first content row is a header when typed font provenance sets
  // every visible word of every one of its display lines in a bold face.
  void detect_header(FixedTableBlockIR &block) {
    if (block.body.empty())
      return;
    auto &row = block.body.front();
    std::set<CellKey> header_words;
    for (const auto &cell : row.cells)
      for (const auto &line : cell.lines)
        for (const auto &source : line.source_cells)
          header_words.insert(key(source));
    if (header_words.empty())
      return;
    std::size_t covered = 0;
    for (const auto &line : lines_) {
      if (line.rule)
        continue;
      for (const auto &placed : line.words) {
        if (placed.word->positioned == nullptr ||
            header_words.count(key(*placed.word->positioned)) == 0)
          continue;
        const auto highlighted = std::any_of(
            line.font_spans.begin(), line.font_spans.end(),
            [&](const auto &span) {
              return header_style(span.style) &&
                     placed.column >= span.column &&
                     placed.column < span.column + span.length;
            });
        if (highlighted)
          ++covered;
      }
    }
    if (covered == header_words.size()) {
      row.kind = FixedTableRowKindIR::header;
      block.header_rows = 1;
    }
  }

  const std::vector<DecodedLogicalRecordSource> &records_;
  std::vector<FlatRow> flat_;
  std::map<CellKey, const OwnedSourceCellIR *> dispositions_;
  std::map<CellKey, const PositionedRowCellIR *> positioned_;
  std::map<std::pair<DisplayRunId, std::size_t>, const PhysicalRowIR *>
      rows_by_id_;
  std::map<std::pair<DisplayRunId, std::size_t>,
           std::vector<const PositionedRowCellIR *>>
      by_row_;
  std::map<std::uint32_t, std::size_t> record_index_;
  mutable std::map<std::uint32_t, std::vector<std::size_t>> byte_offsets_;
  std::vector<std::vector<Word>> words_;
  std::vector<Line> lines_;
  std::vector<const Word *> trailing_;
};

void emit_cell(std::ostringstream &out, const PositionedRowCellIR &cell) {
  out << cell.logical_record << ':' << cell.token_index << ':'
      << cell.word_index << '=' << cell.word << ':'
      << static_cast<int>(cell.role) << '@';
  if (cell.display_column)
    out << *cell.display_column;
  else
    out << '-';
}

void emit_cells(std::ostringstream &out,
                const std::vector<PositionedRowCellIR> &cells) {
  for (const auto &cell : cells) {
    emit_cell(out, cell);
    out << ',';
  }
}

const char *row_kind_name(FixedTableRowKindIR kind) {
  switch (kind) {
  case FixedTableRowKindIR::caption:
    return "caption";
  case FixedTableRowKindIR::header:
    return "header";
  case FixedTableRowKindIR::body:
    return "body";
  }
  return "invalid";
}

void emit_row(std::ostringstream &out, const FixedTableRowIR &row) {
  out << "row kind=" << row_kind_name(row.kind) << " lines=" << row.line_count
      << '\n';
  for (const auto &cell : row.cells) {
    out << " cell column=" << cell.column << '\n';
    for (const auto &line : cell.lines) {
      out << "  line='" << line.text << "' slice=" << line.slice.logical_record
          << ':' << line.slice.segment_index << ':' << line.slice.token_begin
          << ':' << line.slice.token_end << " sources=";
      emit_cells(out, line.source_cells);
      if (!line.unpositioned_cells.empty()) {
        out << " unpositioned=";
        for (const auto &cell : line.unpositioned_cells)
          out << cell.logical_record << ':' << cell.token_index << ':'
              << cell.word_index << '=' << cell.word << ',';
      }
      out << '\n';
    }
  }
  out << " source_rows=";
  for (const auto &source : row.source_rows)
    out << source.display_run << ':' << source.row_index << ',';
  out << " structural=";
  emit_cells(out, row.structural_cells);
  out << '\n';
}

template <typename Visitor>
void visit_claims(const FixedTableBlockIR &block, Visitor visitor) {
  const auto visit_row = [&](const FixedTableRowIR &row) {
    for (const auto &cell : row.cells)
      for (const auto &line : cell.lines)
        for (const auto &source : line.source_cells)
          visitor(source);
    for (const auto &source : row.structural_cells)
      visitor(source);
  };
  if (block.caption)
    visit_row(*block.caption);
  for (const auto &row : block.body)
    visit_row(row);
  for (const auto &source : block.structural_cells)
    visitor(source);
}

} // namespace

std::size_t count_layout_rows(const LayoutIR &layout) {
  std::size_t count = 0;
  for (const auto &run : layout.runs)
    count += run.rows.size();
  return count;
}

std::optional<DocumentSourceRowIR> layout_row_at(const LayoutIR &layout,
                                                 std::size_t ordinal) {
  for (const auto &run : layout.runs) {
    if (ordinal < run.rows.size())
      return DocumentSourceRowIR{run.id, ordinal};
    ordinal -= run.rows.size();
  }
  return std::nullopt;
}

FixedTableBlocksIR extract_fixed_table_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const LayoutRowRangeIR &range) {
  FixedTableBlocksIR result;
  const auto candidates = find_candidates(records);
  if (candidates.empty())
    return result;
  if (!ownership.conflicts.empty()) {
    for (const auto &candidate : candidates)
      result.declined.push_back(
          {{}, candidate.object_id, "source ownership is conflicted"});
    return result;
  }
  Extractor extractor(records, layout, ownership);
  for (const auto &candidate : candidates) {
    bool outside = false;
    std::string reason;
    auto block = extractor.extract(candidate, range, outside, reason);
    if (block) {
      result.blocks.push_back(std::move(*block));
    } else if (!outside) {
      FixedTableDeclineIR decline;
      decline.object_id = candidate.object_id;
      decline.reason = reason;
      result.declined.push_back(std::move(decline));
    }
  }
  return result;
}

bool verify_fixed_table_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const LayoutRowRangeIR &range, const FixedTableBlocksIR &blocks,
    std::string *error) {
  const auto fail = [&](const std::string &message) {
    if (error)
      *error = message;
    return false;
  };
  if (!ownership.conflicts.empty())
    return fail("fixed table ownership is conflicted");
  const auto canonical =
      extract_fixed_table_blocks_ir(records, layout, ownership, range);
  if (format_fixed_table_blocks_ir(canonical) !=
      format_fixed_table_blocks_ir(blocks))
    return fail("fixed table blocks differ from canonical extraction");

  std::map<CellKey, const PositionedRowCellIR *> owned;
  std::map<CellKey, const OwnedSourceCellIR *> ledger;
  for (const auto &cell : ownership.cells)
    ledger[{cell.logical_record, cell.token_index, cell.word_index}] = &cell;
  std::map<std::pair<DisplayRunId, std::size_t>, std::vector<CellKey>> by_row;
  for (const auto &cell : ownership.row_cells) {
    owned[key(cell)] = &cell;
    by_row[{cell.run, cell.row_index}].push_back(key(cell));
  }
  const auto flat = flatten(layout);
  std::set<CellKey> globally_claimed;
  for (const auto &block : blocks.blocks) {
    if (block.rows.begin >= block.rows.end || block.rows.end > flat.size() ||
        block.rows.begin < range.begin || block.rows.end > range.end)
      return fail("fixed table row span is outside the layout or range");
    if (block.source_rows.size() != block.rows.end - block.rows.begin)
      return fail("fixed table source rows do not match its row span");
    std::set<CellKey> expected;
    for (auto ordinal = block.rows.begin; ordinal < block.rows.end; ++ordinal) {
      const auto &row = flat[ordinal];
      const auto &source = block.source_rows[ordinal - block.rows.begin];
      if (source.display_run != row.run || source.row_index != row.row_index)
        return fail("fixed table source row identity is invalid");
      const auto found = by_row.find({row.run, row.row_index});
      if (found != by_row.end())
        expected.insert(found->second.begin(), found->second.end());
    }
    std::set<CellKey> claimed;
    bool valid = true;
    visit_claims(block, [&](const PositionedRowCellIR &cell) {
      const auto found = owned.find(key(cell));
      if (!claimed.insert(key(cell)).second || found == owned.end() ||
          !globally_claimed.insert(key(cell)).second ||
          found->second->run != cell.run ||
          found->second->row_index != cell.row_index ||
          found->second->word != cell.word ||
          found->second->role != cell.role ||
          found->second->display_column != cell.display_column)
        valid = false;
    });
    if (!valid)
      return fail("fixed table source cell is absent, duplicated or altered");
    std::set<CellKey> unpositioned_claimed;
    const auto visit_unpositioned = [&](const FixedTableRowIR &row) {
      for (const auto &cell : row.cells)
        for (const auto &line : cell.lines)
          for (const auto &source : line.unpositioned_cells) {
            const CellKey source_key{source.logical_record, source.token_index,
                                     source.word_index};
            const auto found = ledger.find(source_key);
            if (found == ledger.end() || found->second->run != 0 ||
                found->second->disposition != SourceDisposition::opaque ||
                found->second->word != source.word ||
                !unpositioned_claimed.insert(source_key).second ||
                claimed.count(source_key) != 0)
              valid = false;
          }
    };
    if (block.caption)
      visit_unpositioned(*block.caption);
    for (const auto &row : block.body)
      visit_unpositioned(row);
    if (!valid)
      return fail("fixed table unpositioned cell is not an opaque ledger cell");
    if (claimed != expected)
      return fail("fixed table does not claim every positioned cell of its "
                  "rows exactly once");
    // A recorded picture addresses a line of the region, its columns are
    // blank there (its placeholder words were replaced by the image), and no
    // two pictures overlap.
    std::size_t previous_line = 0;
    std::size_t previous_end = 0;
    for (std::size_t index = 0; index < block.pictures.size(); ++index) {
      const auto &picture = block.pictures[index];
      if (picture.resource.empty() ||
          picture.placeholder != figure_picture_placeholder(picture.resource))
        return fail("fixed table picture has no resource");
      if (picture.line >= block.preformatted_lines.size())
        return fail("fixed table picture addresses no display line");
      if (index != 0 &&
          (picture.line < previous_line ||
           (picture.line == previous_line && picture.column < previous_end)))
        return fail("fixed table pictures overlap or are out of order");
      previous_line = picture.line;
      previous_end = picture.column + picture.length;
      const auto &text = block.preformatted_lines[picture.line].text;
      const auto end = std::min(previous_end, text.size());
      if (picture.column < end &&
          text.find_first_not_of(' ', picture.column) < end)
        return fail("fixed table picture columns are not blank");
    }
    if (block.geometry == FixedTableGeometryIR::preformatted) {
      if (block.preformatted_lines.empty())
        return fail("preformatted table region has no display lines");
      if (!block.body.empty() || block.caption ||
          !block.separator_columns.empty())
        return fail("preformatted table region carries table geometry");
      bool blank = true;
      for (const auto &line : block.preformatted_lines)
        if (line.text.find_first_not_of(' ') != std::string::npos)
          blank = false;
      if (blank)
        return fail("preformatted table region has no visible line");
      continue;
    }
    // A column-proven region also records its display lines, so the lowering
    // can reproduce the art verbatim; those lines claim no cell of their own,
    // and are absent only when the record's line model did not parse.
    if (block.body.empty() || block.separator_columns.empty())
      return fail("fixed table has no body rows or columns");
    if (block.header_rows > block.body.size())
      return fail("fixed table header count exceeds its rows");
    const auto columns = block.separator_columns.size() + 1;
    for (const auto &row : block.body)
      if (row.cells.size() != columns)
        return fail("fixed table row width differs from its columns");
    if (block.caption && block.caption->cells.size() != 1)
      return fail("fixed table caption is not a single spanning cell");
  }
  if (error)
    error->clear();
  return true;
}

std::string format_fixed_table_blocks_ir(const FixedTableBlocksIR &blocks) {
  std::ostringstream out;
  for (const auto &block : blocks.blocks) {
    out << "table object='" << block.object_id << "' geometry="
        << (block.geometry == FixedTableGeometryIR::gap      ? "gap"
            : block.geometry == FixedTableGeometryIR::preformatted
                ? "preformatted"
                : "box")
        << " rows=[" << block.rows.begin
        << ',' << block.rows.end << ") source=" << block.object_source.logical_record
        << ':' << block.object_source.segment_index << ':'
        << block.object_source.token_begin << ':' << block.object_source.token_end
        << " left=" << block.left_column << " width=" << block.width
        << " separators=";
    for (const auto column : block.separator_columns)
      out << column << ',';
    out << " header_rows=" << block.header_rows << " body_rows="
        << block.body.size() << " source_rows=";
    for (const auto &row : block.source_rows)
      out << row.display_run << ':' << row.row_index << ',';
    out << '\n';
    if (block.caption)
      emit_row(out, *block.caption);
    for (const auto &row : block.body)
      emit_row(out, row);
    for (const auto &line : block.preformatted_lines) {
      out << " pre " << line.logical_record << ':' << line.prefix_token << ':'
          << line.token_end << " rows=";
      for (const auto &row : line.rows)
        out << row.display_run << ':' << row.row_index << ',';
      out << " text='" << line.text << "'\n";
    }
    for (const auto &picture : block.pictures)
      out << " picture resource=" << picture.resource << " line="
          << picture.line << " columns=[" << picture.column << ','
          << picture.column + picture.length << ") source="
          << picture.logical_record << ':' << picture.segment_index
          << " alt='" << picture.placeholder << "'\n";
    for (const auto &line : block.index_lines)
      out << " index " << line.logical_record << ':' << line.prefix_token << ':'
          << line.token_end << " text='" << line.text << "'\n";
    out << " block_structural=";
    emit_cells(out, block.structural_cells);
    out << '\n';
  }
  for (const auto &decline : blocks.declined)
    out << "declined object='" << decline.object_id << "' rows=["
        << decline.rows.begin << ',' << decline.rows.end << ") reason='"
        << decline.reason << "'\n";
  return out.str();
}

} // namespace geist::detail
