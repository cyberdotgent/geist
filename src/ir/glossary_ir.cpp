#include "geist/detail/ir/glossary_ir.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <functional>
#include <cctype>
#include <sstream>

namespace geist::detail {
namespace {

const DecodedLogicalRecordSource*
find_record(const std::vector<DecodedLogicalRecordSource>& records,
            std::uint32_t logical_record) {
  const auto found =
      std::find_if(records.begin(), records.end(), [&](const auto& record) {
        return record.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::size_t wide_gap(const std::string& text, std::size_t minimum = 16) {
  for (std::size_t begin = 0; begin < text.size();) {
    if (text[begin] != ' ') {
      ++begin;
      continue;
    }
    auto end = begin;
    while (end < text.size() && text[end] == ' ') ++end;
    if (end - begin >= minimum) return begin;
    begin = end;
  }
  return std::string::npos;
}

std::string compact(std::string text) {
  return collapse_ascii_whitespace(trim_ascii(std::move(text)));
}

bool ends_statement(const std::string& text) {
  return !text.empty() &&
         std::string(".!?").find(text.back()) != std::string::npos;
}

std::string visible_token(const DecodedLogicalRecordSource& record,
                          std::size_t token) {
  if (token >= record.ir.tokens.size()) return {};
  auto words = record.ir.tokens[token].decoded_words;
  if (!words.empty() && words.front() < 4) words.erase(words.begin());
  words.erase(std::remove(words.begin(), words.end(), 0x2666), words.end());
  return compact(token_words_to_ascii(words));
}

std::string
clean_terminal_boundary(const std::vector<DecodedLogicalRecordSource>& records,
                        const PhysicalRowIR& row, std::string text) {
  const auto* record = find_record(records, row.logical_record);
  if (record == nullptr || row.token_begin >= row.token_end) return text;
  const auto token = row.token_end - 1;
  if (token >= record->ir.tokens.size() ||
      record->ir.tokens[token].encoded.width != 1 ||
      record->ir.tokens[token].has_spacing_control)
    return text;
  const auto terminal = visible_token(*record, token);
  if (terminal.empty() || terminal.size() > 3 ||
      text.size() <= terminal.size() ||
      text.compare(text.size() - terminal.size(), terminal.size(), terminal) !=
          0)
    return text;
  const auto prefix = text.substr(0, text.size() - terminal.size());
  if ((!prefix.empty() && prefix.back() == terminal.back()) ||
      std::string("<>/\"()=").find(terminal.front()) != std::string::npos)
    return trim_ascii(prefix);
  return text;
}

// A font span already proven against the visible text of one physical row,
// expressed in byte offsets of that row's visible text.
struct RowEmphasisIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  FontStyleIR style = FontStyleIR::unknown;
  DocumentSourceSliceIR source;
};

// The font control whose operand governs the first physical row of a run:
// the run's own CFONT, or, for a record-leading text row, a span-only CFONT
// (empty payload) terminating the immediately preceding logical record.
// doc/boo-spec/markup.adoc documents that BookServer applies such span-only controls
// to the following physical text line.
std::optional<FontControlSpansIR>
governing_font_spans(const std::vector<DecodedLogicalRecordSource>& records,
                     const DisplayRunIR& run, std::string* error) {
  const auto& first = run.rows.front();
  const auto* record = find_record(records, first.logical_record);
  if (record == nullptr) return std::nullopt;
  if (run.control_kind == BookControlKind::font) {
    if (first.segment_index >= record->control_segments.size())
      return std::nullopt;
    return decode_font_control_spans(
        *record, record->control_segments[first.segment_index], error);
  }
  if (run.control_kind != BookControlKind::text || first.segment_index != 0 ||
      first.logical_record == 0)
    return std::nullopt;
  const auto* previous = find_record(records, first.logical_record - 1);
  if (previous == nullptr || previous->control_segments.empty())
    return std::nullopt;
  const auto& trailing = previous->control_segments.back();
  if (trailing.kind != BookControlKind::font ||
      trailing.payload_range.begin != trailing.payload_range.end)
    return std::nullopt;
  return decode_font_control_spans(*previous, trailing, error);
}

// Maps operand triples onto the row's visible text. Columns are reader
// display columns; the row's native origin is the column of its first visible
// byte. Every span must cover whole space-delimited display words, or the
// whole control is left unapplied.
std::vector<RowEmphasisIR> map_font_spans(const FontControlSpansIR& fonts,
                                          const PhysicalRowIR& row) {
  std::vector<RowEmphasisIR> result;
  const auto& visible = row.visible_text;
  for (const auto& span : fonts.spans) {
    if (span.column < row.native_origin) return {};
    const auto begin = span.column - row.native_origin;
    const auto end = begin + span.length;
    if (end > visible.size() || visible[begin] == ' ' ||
        visible[end - 1] == ' ' || (begin != 0 && visible[begin - 1] != ' ') ||
        (end != visible.size() && visible[end] != ' '))
      return {};
    if (!result.empty() && begin < result.back().end) return {};
    result.push_back({begin, end, span.style, fonts.operand_source});
  }
  return result;
}

void append_row(GlossaryParagraphIR& paragraph, const PhysicalRowIR& row,
                std::size_t row_index, std::string text,
                std::size_t text_offset = 0,
                const std::vector<RowEmphasisIR>& row_emphasis = {}) {
  // Index map from the uncompacted substring into its compact projection so
  // proven row spans keep exact byte identity after whitespace collapse.
  std::vector<std::size_t> compact_index(text.size(), std::string::npos);
  std::string compacted;
  bool in_space = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    if (std::isspace(byte) != 0 || byte < 0x20) {
      in_space = true;
      continue;
    }
    if (in_space && !compacted.empty()) compacted.push_back(' ');
    compact_index[i] = compacted.size();
    compacted.push_back(text[i]);
    in_space = false;
  }
  const auto raw = text;
  text = compact(std::move(text));
  if (text.empty()) return;
  if (compacted != text) compact_index.clear();
  if (!paragraph.text.empty() && row.marker &&
      row.marker->decoded_text.size() == 1 &&
      std::string(".,:;").find(row.marker->decoded_text.front()) !=
          std::string::npos &&
      std::string(".,:;").find(paragraph.text.back()) == std::string::npos)
    paragraph.text += row.marker->decoded_text;
  const auto lexical_carry =
      row.marker && row.native_origin != 3 && row.native_origin != 7 &&
      !row.marker->decoded_text.empty() &&
      std::all_of(row.marker->decoded_text.begin(),
                  row.marker->decoded_text.end(),
                  [](const unsigned char ch) { return std::islower(ch) != 0; });
  if (!paragraph.text.empty()) paragraph.text += ' ';
  if (lexical_carry) paragraph.text += row.marker->decoded_text + ' ';
  const auto base = paragraph.text.size();
  paragraph.text += text;
  paragraph.source_rows.emplace_back(row.run, row_index);
  if (compact_index.empty()) return;
  for (const auto& span : row_emphasis) {
    if (span.begin < text_offset || span.end > text_offset + raw.size())
      continue;
    const auto first = compact_index[span.begin - text_offset];
    const auto last = compact_index[span.end - 1 - text_offset];
    if (first == std::string::npos || last == std::string::npos) continue;
    const auto expected =
        raw.substr(span.begin - text_offset, span.end - span.begin);
    if (text.compare(first, last + 1 - first, expected) != 0) continue;
    paragraph.emphasis.push_back(
        {base + first, base + last + 1, span.style, span.source});
  }
}

bool paragraph_equal(const GlossaryParagraphIR& left,
                     const GlossaryParagraphIR& right) {
  return left.text == right.text && left.source_rows == right.source_rows &&
         left.emphasis == right.emphasis;
}

// The layout of everything before the first `SRGLS`, shared by both
// introduction shapes so neither re-derives the boundary.
struct IntroductionLayout {
  const DisplayRunIR* title_run = nullptr;
  std::vector<const DisplayRunIR*> body_runs;
  std::function<bool(const PhysicalRowIR&)> before_first_term;
  // The first `SRGLS` boundary: its logical record and the first source
  // token of its segment.
  std::uint32_t first_term_record = 0;
  std::size_t first_term_token = 0;
};

// True when some source cell in front of the first `SRGLS` spells a visible
// word that no display run owns. Such a cell is display text the row model
// left behind (the words in front of a record-leading row's compact marker
// slot are the usual case), so an introduction built from rows alone would
// silently drop it. Fail closed: decline, and leave the topic to a family
// that does carry those words.
bool introduction_conserves_source(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, const IntroductionLayout& shape) {
  if (shape.title_run == nullptr || shape.title_run->rows.empty()) return false;
  const auto& title = shape.title_run->rows.front();
  for (const auto& cell : ownership.cells) {
    if (cell.run != 0 || cell.disposition != SourceDisposition::opaque)
      continue;
    // The topic header stands in front of the title row; its bytes are the
    // container's, not the introduction's.
    if (cell.logical_record < title.logical_record ||
        (cell.logical_record == title.logical_record &&
         cell.token_index < title.token_begin))
      continue;
    if (cell.logical_record > shape.first_term_record) continue;
    if (cell.logical_record == shape.first_term_record &&
        cell.token_index >= shape.first_term_token)
      continue;
    // 0x2666 is the decoder's own separator glyph, not a word of the book,
    // and 0x2500-0x25ff is fixed-layout box art.
    if (cell.word == ' ' || cell.word < 0x21 || cell.word == 0x2666 ||
        (cell.word >= 0x2500 && cell.word <= 0x25ff))
      continue;
    // A display line's length byte is structure whatever word the dictionary
    // spells for it; the decoder already decided that, and nothing here may
    // decide it again.
    const auto* owner = find_record(records, cell.logical_record);
    if (owner != nullptr && is_display_line_length_token(*owner,
                                                         cell.token_index))
      continue;
    return false;
  }
  return true;
}

std::optional<IntroductionLayout> introduction_layout(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<IntroductionLayout> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  if (records.empty() || !ownership.conflicts.empty())
    return fail("source ownership is unavailable or conflicted");

  std::optional<std::pair<std::uint32_t, std::size_t>> first_term;
  std::size_t first_term_token = 0;
  bool glossary_heading = false;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::heading_level) {
        auto level = compact(range_text(record, segment.operand_range));
        if (!level.empty() && level.front() == ':') level.erase(level.begin());
        glossary_heading = glossary_heading ||
                           ascii_equals_case_insensitive(level, "glossary");
      }
      if (ascii_equals_case_insensitive(segment.opcode, "srgls") &&
          !first_term) {
        first_term = {record.logical_record, segment.complete.begin};
        first_term_token = segment.source_tokens.empty()
                               ? std::size_t{0}
                               : segment.source_tokens.front();
      }
    }
  }
  if (!glossary_heading || !first_term)
    return fail("source is not a glossary introduction");

  IntroductionLayout result;
  const auto boundary = *first_term;
  result.first_term_record = boundary.first;
  result.first_term_token = first_term_token;
  result.before_first_term = [&records, boundary](const PhysicalRowIR& row) {
    if (row.logical_record != boundary.first)
      return row.logical_record < boundary.first;
    const auto* record = find_record(records, row.logical_record);
    if (record == nullptr ||
        row.segment_index >= record->control_segments.size())
      return false;
    return record->control_segments[row.segment_index].complete.begin <
           boundary.second;
  };

  for (const auto& run : layout.runs) {
    if (run.rows.empty() || !result.before_first_term(run.rows.front()))
      continue;
    if (run.control_kind == BookControlKind::title) {
      if (result.title_run != nullptr)
        return fail("multiple glossary title runs");
      result.title_run = &run;
    } else if (run.control_kind == BookControlKind::font ||
               run.control_kind == BookControlKind::text) {
      result.body_runs.push_back(&run);
    }
  }
  if (result.title_run == nullptr || result.title_run->rows.size() != 1)
    return fail("glossary introduction has no single title row");
  return result;
}

// The articulated shape: lead, bulleted standards citations, cross-reference
// lead, cross-reference verb definitions. Every part must be present, so this
// asserts the semantics of each paragraph it sorts.
std::optional<GlossaryIntroductionIR> extract_citation_list_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<GlossaryIntroductionIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  const auto shape = introduction_layout(records, layout, ownership, error);
  if (!shape) return std::nullopt;
  const auto* title_run = shape->title_run;
  const auto& body_runs = shape->body_runs;
  const auto& before_first_term = shape->before_first_term;
  if (body_runs.empty())
    return fail("glossary introduction layout is incomplete");

  GlossaryIntroductionIR result;
  result.shape = GlossaryIntroductionShapeIR::citation_list;
  auto title_text = clean_terminal_boundary(
      records, title_run->rows.front(), title_run->rows.front().visible_text);
  const auto title_gap = wide_gap(title_text, 8);
  if (title_gap == std::string::npos)
    return fail("glossary title and lead have no conserved field boundary");
  result.title = compact(title_text.substr(0, title_gap));
  auto lead_begin = title_gap;
  while (lead_begin < title_text.size() && title_text[lead_begin] == ' ')
    ++lead_begin;
  result.lead.text = compact(title_text.substr(lead_begin));
  result.lead.source_rows.emplace_back(title_run->id, 0);

  bool in_cross_references = false;
  for (const auto* run : body_runs) {
    GlossaryParagraphIR current;
    std::string font_error;
    const auto fonts = governing_font_spans(records, *run, &font_error);
    if (!fonts && !font_error.empty())
      return fail("glossary introduction font control rejected: " +
                  font_error);
    const auto first_row_emphasis =
        fonts ? map_font_spans(*fonts, run->rows.front())
              : std::vector<RowEmphasisIR>{};
    for (std::size_t row_index = 0; row_index < run->rows.size(); ++row_index) {
      const auto& row = run->rows[row_index];
      if (!before_first_term(row)) break;
      const auto& row_emphasis =
          row_index == 0 ? first_row_emphasis : std::vector<RowEmphasisIR>{};
      auto text = row.visible_text;
      const auto gap = wide_gap(text);
      if (gap != std::string::npos &&
          ends_statement(compact(text.substr(0, gap)))) {
        append_row(current, row, row_index, text.substr(0, gap), 0,
                   row_emphasis);
        if (current.text.empty() || in_cross_references)
          return fail("ambiguous glossary cross-reference boundary");
        result.sources.push_back(std::move(current));
        current = {};
        auto right = gap;
        while (right < text.size() && text[right] == ' ') ++right;
        append_row(result.cross_reference_lead, row, row_index,
                   text.substr(right), right, row_emphasis);
        in_cross_references = true;
        continue;
      }
      if (!current.text.empty() && row.native_origin == 3 &&
          ends_statement(current.text)) {
        (in_cross_references ? result.cross_references : result.sources)
            .push_back(std::move(current));
        current = {};
      }
      append_row(current, row, row_index, std::move(text), 0, row_emphasis);
    }
    if (!current.text.empty())
      (in_cross_references ? result.cross_references : result.sources)
          .push_back(std::move(current));
  }

  // Every part of the articulated shape must be present for it to be claimed;
  // the paragraph shape carries the rest. (This used to require exactly five
  // citations and six cross-reference definitions, which are the counts in
  // SC31-711 and nowhere else -- a fitted constant, not evidence.)
  if (result.title.empty() || result.lead.text.empty() ||
      result.sources.empty() || result.cross_reference_lead.text.empty() ||
      result.cross_references.empty())
    return fail("glossary introduction semantic sections are incomplete");
  for (auto& paragraph : result.cross_references) {
    while (paragraph.text.size() >= 2 &&
           paragraph.text.compare(paragraph.text.size() - 2, 2, "..") == 0)
      paragraph.text.pop_back();
    for (const auto& span : paragraph.emphasis)
      if (span.end > paragraph.text.size())
        return fail("glossary emphasis outlives its cross-reference text");
  }
  if (error != nullptr) error->clear();
  return result;
}

// The general shape: the title, then the introduction's prose paragraphs in
// source order. Paragraph boundaries come from the same evidence the
// articulated shape uses -- a row that returns to the body's left margin
// after a sentence-final row -- but nothing is asserted about what any
// paragraph means, so a glossary with a one-line introduction, or none at
// all, is admitted rather than declined.
std::optional<GlossaryIntroductionIR> extract_paragraph_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<GlossaryIntroductionIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  const auto shape = introduction_layout(records, layout, ownership, error);
  if (!shape) return std::nullopt;
  if (!introduction_conserves_source(records, ownership, *shape))
    return fail("glossary introduction has display text no run owns");

  GlossaryIntroductionIR result;
  result.shape = GlossaryIntroductionShapeIR::paragraphs;
  const auto& title_row = shape->title_run->rows.front();
  auto title_text =
      clean_terminal_boundary(records, title_row, title_row.visible_text);
  // The title row carries the first introduction line after a wide field gap
  // when there is one; without a gap the whole row is the title.
  const auto title_gap = wide_gap(title_text, 8);
  result.title = compact(title_text.substr(0, title_gap == std::string::npos
                                                  ? title_text.size()
                                                  : title_gap));
  if (result.title.empty()) return fail("glossary topic has no title row");
  if (title_gap != std::string::npos) {
    auto lead_begin = title_gap;
    while (lead_begin < title_text.size() && title_text[lead_begin] == ' ')
      ++lead_begin;
    GlossaryParagraphIR lead;
    lead.text = compact(title_text.substr(lead_begin));
    lead.source_rows.emplace_back(shape->title_run->id, 0);
    if (!lead.text.empty()) result.paragraphs.push_back(std::move(lead));
  }

  for (const auto* run : shape->body_runs) {
    GlossaryParagraphIR current;
    std::string font_error;
    const auto fonts = governing_font_spans(records, *run, &font_error);
    if (!fonts && !font_error.empty())
      return fail("glossary introduction font control rejected: " +
                  font_error);
    const auto first_row_emphasis =
        fonts ? map_font_spans(*fonts, run->rows.front())
              : std::vector<RowEmphasisIR>{};
    for (std::size_t row_index = 0; row_index < run->rows.size(); ++row_index) {
      const auto& row = run->rows[row_index];
      if (!shape->before_first_term(row)) break;
      const auto& row_emphasis =
          row_index == 0 ? first_row_emphasis : std::vector<RowEmphasisIR>{};
      if (!current.text.empty() && row.native_origin == 3 &&
          ends_statement(current.text)) {
        result.paragraphs.push_back(std::move(current));
        current = {};
      }
      append_row(current, row, row_index, row.visible_text, 0, row_emphasis);
    }
    if (!current.text.empty()) result.paragraphs.push_back(std::move(current));
  }
  if (error != nullptr) error->clear();
  return result;
}

} // namespace

std::optional<GlossaryIntroductionIR> extract_glossary_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  // The articulated shape is the more specific recognizer, so it is offered
  // first; the paragraph shape carries every introduction it declines.
  std::string citation_error;
  if (auto articulated = extract_citation_list_introduction_ir(
          records, layout, ownership, &citation_error))
    return articulated;
  return extract_paragraph_introduction_ir(records, layout, ownership, error);
}

bool verify_glossary_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const GlossaryIntroductionIR& introduction, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto canonical =
      extract_glossary_introduction_ir(records, layout, ownership);
  if (!canonical) return fail("source does not admit a glossary introduction");
  if (canonical->shape != introduction.shape ||
      canonical->paragraphs.size() != introduction.paragraphs.size())
    return fail("glossary introduction shape differs from canonical lowering");
  for (std::size_t i = 0; i < introduction.paragraphs.size(); ++i)
    if (!paragraph_equal(canonical->paragraphs[i], introduction.paragraphs[i]))
      return fail("glossary introduction paragraph differs from canonical "
                  "lowering");
  if (canonical->title != introduction.title ||
      !paragraph_equal(canonical->lead, introduction.lead) ||
      canonical->sources.size() != introduction.sources.size() ||
      !paragraph_equal(canonical->cross_reference_lead,
                       introduction.cross_reference_lead) ||
      canonical->cross_references.size() !=
          introduction.cross_references.size())
    return fail("glossary structure differs from canonical lowering");
  for (std::size_t i = 0; i < introduction.sources.size(); ++i)
    if (!paragraph_equal(canonical->sources[i], introduction.sources[i]))
      return fail("glossary source paragraph differs from canonical lowering");
  for (std::size_t i = 0; i < introduction.cross_references.size(); ++i)
    if (!paragraph_equal(canonical->cross_references[i],
                         introduction.cross_references[i]))
      return fail("glossary cross-reference differs from canonical lowering");
  if (error != nullptr) error->clear();
  return true;
}

std::string
format_glossary_introduction_ir(const GlossaryIntroductionIR& introduction) {
  std::ostringstream out;
  out << "glossary title='" << introduction.title << "' lead='"
      << introduction.lead.text << "' sources=" << introduction.sources.size()
      << " cross_references=" << introduction.cross_references.size() << '\n';
  const auto emit = [&](const char* kind,
                        const GlossaryParagraphIR& paragraph) {
    out << kind << " text='" << paragraph.text << "' sources=";
    for (std::size_t i = 0; i < paragraph.source_rows.size(); ++i) {
      if (i != 0) out << ',';
      out << paragraph.source_rows[i].first << ':'
          << paragraph.source_rows[i].second;
    }
    if (!paragraph.emphasis.empty()) {
      out << " emphasis=";
      for (std::size_t i = 0; i < paragraph.emphasis.size(); ++i) {
        const auto& span = paragraph.emphasis[i];
        if (i != 0) out << ',';
        out << span.begin << ':' << span.end << ':'
            << font_style_name(span.style) << '@' << span.source.logical_record
            << ':' << span.source.segment_index << ':'
            << span.source.token_begin << ':' << span.source.token_end;
      }
    }
    out << '\n';
  };
  for (const auto& paragraph : introduction.sources) emit("source", paragraph);
  emit("cross_reference_lead", introduction.cross_reference_lead);
  for (const auto& paragraph : introduction.cross_references)
    emit("cross_reference", paragraph);
  return out.str();
}

} // namespace geist::detail
