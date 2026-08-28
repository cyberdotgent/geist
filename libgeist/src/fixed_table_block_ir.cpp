#include "geist/detail/fixed_table_block_ir.hpp"

#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
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

constexpr std::uint16_t kHorizontal = 0x2500;
constexpr std::uint16_t kVertical = 0x2502;

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
  std::string failure;
};

std::string object_id_of(const ControlSegmentIR &segment) {
  const auto opcode = segment.opcode;
  if (ascii_lower(opcode.substr(0, 5)) == "srtbl")
    return opcode.substr(5);
  return opcode;
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

    const auto split = split_lines(candidate, start, end, block, reason);
    if (split)
      attach_font_spans(candidate, start, end);
    if (!split)
      return std::nullopt;
    if (!classify_lines(block, reason))
      return std::nullopt;
    if (!build_rows(block, reason))
      return std::nullopt;
    detect_header(block);
    lines_.clear();
    return block;
  }

private:
  std::vector<Word> record_words(const DecodedLogicalRecordSource &record,
                                 std::size_t token_begin,
                                 std::size_t token_end) const {
    std::vector<std::size_t> covered(record.tokens.size(), npos);
    for (const auto &segment : record.control_segments)
      for (const auto token : segment.source_tokens)
        if (token < covered.size())
          covered[token] = segment.segment_index;
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
      bool first = true;
      for (std::size_t index = 0; index < values.size(); ++index) {
        const auto disposition =
            dispositions_.find({record.logical_record, token, index});
        if (disposition != dispositions_.end() &&
            disposition->second->disposition ==
                SourceDisposition::control_operand)
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
        word.segment = segment;
        words.push_back(word);
        first = false;
      }
    }
    return words;
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

  // The first content row is a header when typed font provenance highlights
  // every visible word of every one of its display lines.
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
              return span.style != FontStyleIR::unknown &&
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
    out << "table object='" << block.object_id << "' rows=[" << block.rows.begin
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
