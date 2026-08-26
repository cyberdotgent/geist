#include "geist/detail/glossary_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
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

void append_row(GlossaryParagraphIR& paragraph, const PhysicalRowIR& row,
                std::size_t row_index, std::string text) {
  text = compact(std::move(text));
  if (text.empty()) return;
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
  paragraph.text += text;
  paragraph.source_rows.emplace_back(row.run, row_index);
}

bool paragraph_equal(const GlossaryParagraphIR& left,
                     const GlossaryParagraphIR& right) {
  return left.text == right.text && left.source_rows == right.source_rows;
}

} // namespace

std::optional<GlossaryIntroductionIR> extract_glossary_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<GlossaryIntroductionIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  if (records.empty() || !ownership.conflicts.empty())
    return fail("source ownership is unavailable or conflicted");

  std::optional<std::pair<std::uint32_t, std::size_t>> first_term;
  bool glossary_heading = false;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::heading_level) {
        auto level = compact(range_text(record, segment.operand_range));
        if (!level.empty() && level.front() == ':') level.erase(level.begin());
        glossary_heading = glossary_heading ||
                           ascii_equals_case_insensitive(level, "glossary");
      }
      if (ascii_equals_case_insensitive(segment.opcode, "srgls") && !first_term)
        first_term = {record.logical_record, segment.complete.begin};
    }
  }
  if (!glossary_heading || !first_term)
    return fail("source is not a glossary introduction");

  const auto before_first_term = [&](const PhysicalRowIR& row) {
    if (row.logical_record != first_term->first)
      return row.logical_record < first_term->first;
    const auto* record = find_record(records, row.logical_record);
    if (record == nullptr ||
        row.segment_index >= record->control_segments.size())
      return false;
    return record->control_segments[row.segment_index].complete.begin <
           first_term->second;
  };

  const DisplayRunIR* title_run = nullptr;
  std::vector<const DisplayRunIR*> body_runs;
  for (const auto& run : layout.runs) {
    if (run.rows.empty() || !before_first_term(run.rows.front())) continue;
    if (run.control_kind == BookControlKind::title) {
      if (title_run != nullptr) return fail("multiple glossary title runs");
      title_run = &run;
    } else if (run.control_kind == BookControlKind::font ||
               run.control_kind == BookControlKind::text) {
      body_runs.push_back(&run);
    }
  }
  if (title_run == nullptr || title_run->rows.size() != 1 || body_runs.empty())
    return fail("glossary introduction layout is incomplete");

  GlossaryIntroductionIR result;
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
    for (std::size_t row_index = 0; row_index < run->rows.size(); ++row_index) {
      const auto& row = run->rows[row_index];
      if (!before_first_term(row)) break;
      auto text = row.visible_text;
      const auto gap = wide_gap(text);
      if (gap != std::string::npos &&
          ends_statement(compact(text.substr(0, gap)))) {
        append_row(current, row, row_index, text.substr(0, gap));
        if (current.text.empty() || in_cross_references)
          return fail("ambiguous glossary cross-reference boundary");
        result.sources.push_back(std::move(current));
        current = {};
        auto right = gap;
        while (right < text.size() && text[right] == ' ') ++right;
        append_row(result.cross_reference_lead, row, row_index,
                   text.substr(right));
        in_cross_references = true;
        continue;
      }
      if (!current.text.empty() && row.native_origin == 3 &&
          ends_statement(current.text)) {
        (in_cross_references ? result.cross_references : result.sources)
            .push_back(std::move(current));
        current = {};
      }
      append_row(current, row, row_index, std::move(text));
    }
    if (!current.text.empty())
      (in_cross_references ? result.cross_references : result.sources)
          .push_back(std::move(current));
  }

  if (result.title.empty() || result.lead.text.empty() ||
      result.sources.size() != 5 || result.cross_reference_lead.text.empty() ||
      result.cross_references.size() != 6)
    return fail("glossary introduction semantic sections are incomplete");
  for (auto& paragraph : result.cross_references)
    while (paragraph.text.size() >= 2 &&
           paragraph.text.compare(paragraph.text.size() - 2, 2, "..") == 0)
      paragraph.text.pop_back();
  if (error != nullptr) error->clear();
  return result;
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
    out << '\n';
  };
  for (const auto& paragraph : introduction.sources) emit("source", paragraph);
  emit("cross_reference_lead", introduction.cross_reference_lead);
  for (const auto& paragraph : introduction.cross_references)
    emit("cross_reference", paragraph);
  return out.str();
}

} // namespace geist::detail
