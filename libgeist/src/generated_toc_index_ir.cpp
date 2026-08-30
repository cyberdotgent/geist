#include "geist/detail/generated_toc_index_ir.hpp"

#include "geist/detail/display_lines.hpp"
#include "geist/detail/figure_block_ir.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

// One display cell of a line: the rendered word plus the record-local token it
// came from.  `token` is `no_token` for a space the assembler inserted between
// two tokens.
constexpr auto no_token = static_cast<std::size_t>(-1);

struct LineCursor {
  const DecodedLogicalRecordSource* record = nullptr;
  DisplayLineIR line;
  std::vector<DisplayLineCellIR> cells;
};

// The control segment that owns a record-local token.  Segments partition the
// record, so this is a total function wherever the token exists.
std::size_t segment_of_token(const DecodedLogicalRecordSource& record,
                             std::size_t token) {
  for (const auto& segment : record.control_segments)
    if (std::find(segment.source_tokens.begin(), segment.source_tokens.end(),
                  token) != segment.source_tokens.end())
      return segment.segment_index;
  return 0;
}

// Provenance for a half-open record-local token range, split at control
// segment boundaries so every slice names the segment it lies in.  Byte
// coordinates are the token's own BOO payload bytes, never offsets into a
// decoded string projection.
void append_slices(const DecodedLogicalRecordSource& record,
                   std::size_t token_begin, std::size_t token_end,
                   std::vector<DocumentSourceSliceIR>& out) {
  auto at = token_begin;
  while (at < token_end && at < record.ir.tokens.size()) {
    const auto segment = segment_of_token(record, at);
    auto end = at + 1;
    while (end < token_end && end < record.ir.tokens.size() &&
           segment_of_token(record, end) == segment)
      ++end;
    DocumentSourceSliceIR slice;
    slice.logical_record = record.logical_record;
    slice.segment_index = segment;
    slice.token_begin = at;
    slice.token_end = end;
    slice.byte_begin = record.ir.tokens[at].byte_range.begin;
    slice.byte_end = record.ir.tokens[end - 1].byte_range.end;
    out.push_back(slice);
    at = end;
  }
}

DocumentSourceSliceIR whole_line_slice(const DecodedLogicalRecordSource& record,
                                       const DisplayLineIR& line) {
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.segment_index = segment_of_token(record, line.prefix_token);
  slice.token_begin = line.prefix_token;
  slice.token_end = line.token_end;
  slice.byte_begin = record.ir.tokens[line.prefix_token].byte_range.begin;
  slice.byte_end = record.ir.tokens[line.token_end - 1].byte_range.end;
  return slice;
}

// Collects the distinct tokens of a cell range, in order, as source slices.
void cell_slices(const DecodedLogicalRecordSource& record,
                 const std::vector<DisplayLineCellIR>& cells,
                 std::size_t begin, std::size_t end,
                 std::vector<DocumentSourceSliceIR>& out) {
  auto run_begin = no_token;
  auto run_end = no_token;
  const auto flush = [&] {
    if (run_begin != no_token) append_slices(record, run_begin, run_end, out);
    run_begin = no_token;
    run_end = no_token;
  };
  for (auto index = begin; index < end && index < cells.size(); ++index) {
    const auto token = cells[index].token;
    if (token == no_token) continue;
    if (run_begin == no_token) {
      run_begin = token;
      run_end = token + 1;
    } else if (token + 1 > run_end && token <= run_end) {
      run_end = token + 1;
    } else if (token < run_begin || token > run_end) {
      flush();
      run_begin = token;
      run_end = token + 1;
    }
  }
  flush();
}

std::string cell_text(const std::vector<DisplayLineCellIR>& cells,
                      std::size_t begin, std::size_t end) {
  std::string text;
  for (auto index = begin; index < end && index < cells.size(); ++index)
    text += figure_display_glyph(cells[index].word);
  return text;
}

std::string trim_text(std::string value) { return trim_ascii(std::move(value)); }

bool decimal_text(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

bool valid_topic_id(const std::string& value) {
  return !value.empty() && value.size() <= 64 &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isalnum(ch) != 0 || ch == '_' || ch == '-' ||
                  ch == '.' || ch == '$' || ch == '#' || ch == '@';
         });
}

// A word-delimited field of a display line, in cell coordinates.
struct FieldRange {
  std::size_t begin = 0;
  std::size_t end = 0;
};

// Splits [begin, end) on space cells.
std::vector<FieldRange> space_fields(
    const std::vector<DisplayLineCellIR>& cells, std::size_t begin,
    std::size_t end) {
  std::vector<FieldRange> fields;
  auto at = begin;
  while (at < end) {
    while (at < end && cells[at].word == ' ') ++at;
    if (at >= end) break;
    auto stop = at;
    while (stop < end && cells[stop].word != ' ') ++stop;
    fields.push_back({at, stop});
    at = stop;
  }
  return fields;
}

// Splits [begin, end) on the declared delimiter word.  A leading delimiter
// opens the first field, so `citerm <D>term<D>1<D>2.2.4` yields exactly the
// three fields `term`, `1`, `2.2.4`.
std::vector<FieldRange> delimiter_fields(
    const std::vector<DisplayLineCellIR>& cells, std::size_t begin,
    std::size_t end, std::uint16_t delimiter) {
  std::vector<FieldRange> fields;
  auto at = begin;
  while (at < end && cells[at].word != delimiter) ++at;
  if (at >= end) return fields;
  ++at;
  auto field_begin = at;
  for (; at < end; ++at) {
    if (cells[at].word != delimiter) continue;
    fields.push_back({field_begin, at});
    field_begin = at + 1;
  }
  fields.push_back({field_begin, end});
  return fields;
}

struct LineView {
  const DecodedLogicalRecordSource* record = nullptr;
  DisplayLineIR line;
  std::vector<DisplayLineCellIR> cells;
  std::string opcode;      // the line's first space-delimited word
  std::size_t body = 0;    // first cell after the opcode
};

std::string lower(std::string value) { return ascii_lower(std::move(value)); }

class Extractor {
public:
  Extractor(const std::vector<DecodedLogicalRecordSource>& records,
            const BookTopicCatalogIR* catalog)
      : records_(records), catalog_(catalog) {}

  std::optional<GeneratedTocIndexTopicIR> run(std::string* error) {
    if (records_.empty()) return reject(error, "generated navigation topic is empty");
    if (!collect_lines(error)) return std::nullopt;
    if (!type_lines(error)) return std::nullopt;
    if (!finish(error)) return std::nullopt;
    if (error != nullptr) error->clear();
    return std::move(topic_);
  }

private:
  std::optional<GeneratedTocIndexTopicIR> reject(std::string* error,
                                                 std::string message) {
    fail(error, std::move(message));
    return std::nullopt;
  }

  bool collect_lines(std::string* error) {
    for (const auto& record : records_) {
      const auto lines = record_display_lines(record);
      if (!lines)
        return fail(error,
                    "generated navigation record " +
                        std::to_string(record.logical_record) +
                        " does not parse into display lines");
      for (const auto& line : *lines) {
        LineView view;
        view.record = &record;
        view.line = line;
        view.cells = display_line_cells(record, line);
        const auto fields = space_fields(view.cells, 0, view.cells.size());
        if (fields.empty())
          return fail(error, "generated navigation display line is empty");
        view.opcode = cell_text(view.cells, fields[0].begin, fields[0].end);
        view.body = fields[0].end;
        views_.push_back(std::move(view));
      }
    }
    return true;
  }

  bool type_lines(std::string* error) {
    for (std::size_t index = 0; index < views_.size(); ++index) {
      const auto& view = views_[index];
      const auto opcode = lower(view.opcode);
      GeneratedTocIndexLineKindIR kind{};
      if (index == 0 && opcode.rfind("sh", 0) == 0) {
        kind = GeneratedTocIndexLineKindIR::topic_start;
      } else if (opcode == "ctopicn" || opcode == "cparent" ||
                 opcode == "cforwardlevel" || opcode == "cbacklevel" ||
                 opcode == "csummary" || opcode == "csourcefn") {
        kind = GeneratedTocIndexLineKindIR::metadata;
      } else if (opcode == "chdlevel") {
        if (!heading_level(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::metadata;
      } else if (opcode == "st") {
        if (!title(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::title;
      } else if (opcode.rfind("sr", 0) == 0 &&
                 valid_topic_id(view.opcode.substr(2)) &&
                 view.body >= view.cells.size()) {
        topic_.anchors.emplace_back(view.opcode.substr(2),
                                    whole_line_slice(*view.record, view.line));
        kind = GeneratedTocIndexLineKindIR::structural_anchor;
      } else if (opcode.rfind("ctocdef=", 0) == 0) {
        if (!definition(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::toc_definition;
      } else if (opcode == "ctoce") {
        if (!entry(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::toc_entry;
      } else if (opcode == "cidelm") {
        if (!delimiter(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::index_delimiter;
      } else if (opcode == "cgpsep") {
        if (!group(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::index_group;
      } else if (opcode == "citerm") {
        if (!term(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::index_term;
      } else if (opcode == "cendindex") {
        kind = GeneratedTocIndexLineKindIR::index_end;
      } else if (opcode == "c.sp") {
        kind = GeneratedTocIndexLineKindIR::spacing;
      } else if (opcode == "cz") {
        if (!region(view, error)) return false;
        kind = GeneratedTocIndexLineKindIR::region_directive;
      } else {
        return fail(error,
                    "generated navigation line is not a known control: '" +
                        view.opcode + "' in record " +
                        std::to_string(view.record->logical_record));
      }
      topic_.lines.push_back(
          {kind, view.opcode, whole_line_slice(*view.record, view.line)});
    }
    return true;
  }

  bool heading_level(const LineView& view, std::string* error) {
    if (!topic_.heading_level.empty())
      return fail(error, "generated navigation CHDLEVEL is duplicated");
    topic_.heading_level =
        lower(trim_text(cell_text(view.cells, view.body, view.cells.size())));
    if (topic_.heading_level == ":toc") {
      topic_.kind = GeneratedTocIndexKindIR::contents;
    } else if (topic_.heading_level == ":index") {
      topic_.kind = GeneratedTocIndexKindIR::index;
    } else {
      return fail(error, "CHDLEVEL is not :TOC or :INDEX");
    }
    return true;
  }

  bool title(const LineView& view, std::string* error) {
    if (!topic_.title.empty())
      return fail(error, "generated navigation ST is duplicated");
    if (topic_.heading_level.empty())
      return fail(error, "generated navigation ST precedes its CHDLEVEL");
    topic_.title = trim_text(cell_text(view.cells, view.body, view.cells.size()));
    if (topic_.title.empty())
      return fail(error, "generated navigation ST carries no title");
    topic_.heading_source = whole_line_slice(*view.record, view.line);
    return true;
  }

  bool definition(const LineView& view, std::string* error) {
    if (topic_.kind != GeneratedTocIndexKindIR::contents)
      return fail(error, "CTOCDEF outside a :TOC topic");
    if (!topic_.entries.empty())
      return fail(error, "CTOCDEF follows a CTOCE entry");
    GeneratedTocDefinitionIR definition;
    const auto equals = view.opcode.find('=');
    const auto ordinal = view.opcode.substr(equals + 1);
    if (!decimal_text(ordinal))
      return fail(error, "CTOCDEF ordinal is not decimal: '" + view.opcode + "'");
    definition.ordinal =
        static_cast<std::uint32_t>(std::strtoul(ordinal.c_str(), nullptr, 10));
    if (definition.ordinal != topic_.definitions.size())
      return fail(error, "CTOCDEF ordinals are not contiguous at '" +
                             view.opcode + "'");
    for (const auto& field :
         space_fields(view.cells, view.body, view.cells.size())) {
      auto operand = cell_text(view.cells, field.begin, field.end);
      if (!decimal_text(operand))
        return fail(error, "CTOCDEF operand is not decimal: '" + operand + "'");
      definition.operands.push_back(std::move(operand));
    }
    if (definition.operands.empty())
      return fail(error, "CTOCDEF carries no operands");
    definition.source = whole_line_slice(*view.record, view.line);
    topic_.definitions.push_back(std::move(definition));
    return true;
  }

  bool entry(const LineView& view, std::string* error) {
    if (topic_.kind != GeneratedTocIndexKindIR::contents)
      return fail(error, "CTOCE outside a :TOC topic");
    if (topic_.definitions.empty())
      return fail(error, "CTOCE precedes every CTOCDEF");
    const auto fields = space_fields(view.cells, view.body, view.cells.size());
    if (fields.size() < 4)
      return fail(error, "CTOCE has fewer than four fields");
    const auto depth = cell_text(view.cells, fields[0].begin, fields[0].end);
    const auto style = cell_text(view.cells, fields[1].begin, fields[1].end);
    if (!decimal_text(depth) || !decimal_text(style))
      return fail(error, "CTOCE level operands are not decimal");
    GeneratedTocEntryIR entry;
    entry.depth =
        static_cast<std::uint32_t>(std::strtoul(depth.c_str(), nullptr, 10));
    entry.style =
        static_cast<std::uint32_t>(std::strtoul(style.c_str(), nullptr, 10));
    if (entry.style >= topic_.definitions.size())
      return fail(error, "CTOCE style has no CTOCDEF");
    entry.topic_id = cell_text(view.cells, fields[2].begin, fields[2].end);
    if (!valid_topic_id(entry.topic_id))
      return fail(error, "CTOCE topic id is not an identifier: '" +
                             entry.topic_id + "'");
    if (catalog_ != nullptr &&
        find_book_topic_catalog_entry(*catalog_, entry.topic_id) == nullptr)
      return fail(error, "CTOCE topic id is absent from the book topic "
                         "catalog: '" + entry.topic_id + "'");
    entry.title =
        trim_text(cell_text(view.cells, fields[3].begin, view.cells.size()));
    if (entry.title.empty())
      return fail(error, "CTOCE carries no title");
    entry.source = whole_line_slice(*view.record, view.line);
    cell_slices(*view.record, view.cells, fields[2].begin, fields[2].end,
                entry.topic_id_slices);
    cell_slices(*view.record, view.cells, fields[3].begin, view.cells.size(),
                entry.title_slices);
    topic_.entries.push_back(std::move(entry));
    return true;
  }

  bool delimiter(const LineView& view, std::string* error) {
    if (topic_.kind != GeneratedTocIndexKindIR::index)
      return fail(error, "CIDELM outside an :INDEX topic");
    if (topic_.delimiter != 0)
      return fail(error, "CIDELM is duplicated");
    const auto fields = space_fields(view.cells, view.body, view.cells.size());
    if (fields.size() != 1 || fields[0].end != fields[0].begin + 1)
      return fail(error, "CIDELM does not declare exactly one delimiter word");
    topic_.delimiter = view.cells[fields[0].begin].word;
    if (topic_.delimiter == ' ' || topic_.delimiter == 0)
      return fail(error, "CIDELM delimiter is not a distinguishable word");
    return true;
  }

  bool group(const LineView& view, std::string* error) {
    if (topic_.delimiter == 0)
      return fail(error, "CGPSEP precedes CIDELM");
    const auto fields = delimiter_fields(view.cells, view.body,
                                         view.cells.size(), topic_.delimiter);
    if (fields.size() != 1)
      return fail(error, "CGPSEP does not carry exactly one delimited label");
    GeneratedIndexGroupIR group;
    group.label = trim_text(cell_text(view.cells, fields[0].begin, fields[0].end));
    if (group.label.empty())
      return fail(error, "CGPSEP carries no group label");
    group.source = whole_line_slice(*view.record, view.line);
    topic_.groups.push_back(std::move(group));
    return true;
  }

  bool term(const LineView& view, std::string* error) {
    if (topic_.delimiter == 0)
      return fail(error, "CITERM precedes CIDELM");
    if (topic_.groups.empty())
      return fail(error, "CITERM precedes every CGPSEP group");
    const auto fields = delimiter_fields(view.cells, view.body,
                                         view.cells.size(), topic_.delimiter);
    if (fields.size() < 2)
      return fail(error, "CITERM carries fewer than a term and a level");
    GeneratedIndexTermIR term;
    term.term = trim_text(cell_text(view.cells, fields[0].begin, fields[0].end));
    // A term field the line wrote as nothing at all.  The field is present --
    // its two delimiter words stand adjacent on the display line, with no cell
    // between them -- so its emptiness is the book's, exactly like the trailing
    // empty target field of a parent term below.  SH20-918 record 636 line 18
    // is `citerm <D><D>1` over tokens 274..277, where token 275 is the single
    // two-byte word pair {U+25BA, U+25BA}: the whole "Special Characters"
    // group's ampersand parent, whose child `See ampersand` carries the entry.
    // It is the only such line in the corpus's 29 INDEX topics (27,530
    // `citerm` lines), and rejecting it dropped that book's entire index.
    //
    // Fail closed twice over.  A field that trims to nothing but *has* cells is
    // a spacing-only term, which is what a misdeclared delimiter would produce,
    // and still rejects; and a textless term may carry no target of its own,
    // because a link with no label would be structure the line does not name.
    const auto term_written_empty = fields[0].begin == fields[0].end;
    if (term.term.empty() && !term_written_empty)
      return fail(error, "CITERM term field carries no term text");
    const auto level = trim_text(
        cell_text(view.cells, fields[1].begin, fields[1].end));
    if (!decimal_text(level) || level.size() != 1 || level == "0")
      return fail(error, "CITERM level is not a single non-zero digit: '" +
                             level + "'");
    term.level =
        static_cast<std::uint32_t>(std::strtoul(level.c_str(), nullptr, 10));
    for (std::size_t field = 2; field < fields.size(); ++field) {
      const auto words = space_fields(view.cells, fields[field].begin,
                                      fields[field].end);
      // A trailing empty field is a parent term whose children carry the
      // targets (GC28-183 record 917 `//*DATASET statement`); hosted serves
      // that line as plain text with no link.
      if (words.empty()) continue;
      GeneratedIndexTargetIR target;
      target.topic_id = cell_text(view.cells, words[0].begin, words[0].end);
      if (words.size() == 3 &&
          lower(cell_text(view.cells, words[1].begin, words[1].end)) == "to") {
        target.range_end_topic_id =
            cell_text(view.cells, words[2].begin, words[2].end);
      } else if (words.size() != 1) {
        return fail(error, "CITERM target is neither a topic id nor a range: '" +
                               trim_text(cell_text(view.cells,
                                                   fields[field].begin,
                                                   fields[field].end)) +
                               "'");
      }
      if (!valid_topic_id(target.topic_id) ||
          (!target.range_end_topic_id.empty() &&
           !valid_topic_id(target.range_end_topic_id)))
        return fail(error, "CITERM target is not an identifier: '" +
                               target.topic_id + "'");
      cell_slices(*view.record, view.cells, fields[field].begin,
                  fields[field].end, target.slices);
      term.targets.push_back(std::move(target));
    }
    if (term.term.empty() && !term.targets.empty())
      return fail(error, "CITERM with no term text carries a target");
    term.source = whole_line_slice(*view.record, view.line);
    cell_slices(*view.record, view.cells, fields[0].begin, fields[0].end,
                term.term_slices);
    topic_.groups.back().terms.push_back(std::move(term));
    return true;
  }

  bool region(const LineView& view, std::string* error) {
    auto operands = lower(trim_text(
        cell_text(view.cells, view.body, view.cells.size())));
    // The reader's region directives around a generated navigation body.
    // `off toc`/`off etoc 0 0` bracket the TOC exactly as `off figlist`
    // brackets a figure list; `break 3` is the page break before it.
    if (operands != "break 3" && operands != "off toc" &&
        operands != "off etoc 0 0" && operands != "off index" &&
        operands != "off eindex 0 0")
      return fail(error, "CZ directive is outside the generated navigation "
                         "grammar: '" + operands + "'");
    return true;
  }

  bool finish(std::string* error) {
    if (topic_.heading_level.empty() || topic_.title.empty())
      return fail(error, "generated navigation envelope is incomplete");
    if (topic_.kind == GeneratedTocIndexKindIR::contents) {
      if (topic_.entries.empty())
        return fail(error, "generated table of contents has no entries");
      if (!topic_.groups.empty() || topic_.delimiter != 0)
        return fail(error, "generated table of contents carries index controls");
      std::uint32_t previous = 0;
      bool first = true;
      for (const auto& entry : topic_.entries) {
        if (!first && entry.depth > previous + 1)
          return fail(error, "generated table of contents skips a level at '" +
                                 entry.topic_id + "'");
        previous = entry.depth;
        first = false;
      }
    } else {
      if (topic_.groups.empty())
        return fail(error, "generated index has no groups");
      if (!topic_.definitions.empty() || !topic_.entries.empty())
        return fail(error, "generated index carries table-of-contents controls");
      for (const auto& group : topic_.groups) {
        if (group.terms.empty())
          return fail(error, "generated index group '" + group.label +
                                 "' has no terms");
        std::uint32_t previous = 0;
        for (const auto& term : group.terms) {
          if (previous != 0 && term.level > previous + 1)
            return fail(error, "generated index skips a level at '" +
                                   term.term + "'");
          previous = term.level;
        }
      }
    }
    return true;
  }

  const std::vector<DecodedLogicalRecordSource>& records_;
  const BookTopicCatalogIR* catalog_;
  std::vector<LineView> views_;
  GeneratedTocIndexTopicIR topic_;
};

bool same_slice(const DocumentSourceSliceIR& left,
                const DocumentSourceSliceIR& right) {
  return left == right;
}

bool same_slices(const std::vector<DocumentSourceSliceIR>& left,
                 const std::vector<DocumentSourceSliceIR>& right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), same_slice);
}

bool same_topic(const GeneratedTocIndexTopicIR& left,
                const GeneratedTocIndexTopicIR& right) {
  if (left.kind != right.kind || left.heading_level != right.heading_level ||
      left.title != right.title ||
      !same_slice(left.heading_source, right.heading_source) ||
      left.delimiter != right.delimiter ||
      left.anchors.size() != right.anchors.size() ||
      left.definitions.size() != right.definitions.size() ||
      left.entries.size() != right.entries.size() ||
      left.groups.size() != right.groups.size() ||
      left.lines.size() != right.lines.size())
    return false;
  for (std::size_t index = 0; index < left.anchors.size(); ++index)
    if (left.anchors[index].first != right.anchors[index].first ||
        !same_slice(left.anchors[index].second, right.anchors[index].second))
      return false;
  for (std::size_t index = 0; index < left.lines.size(); ++index)
    if (left.lines[index].kind != right.lines[index].kind ||
        left.lines[index].opcode != right.lines[index].opcode ||
        !same_slice(left.lines[index].source, right.lines[index].source))
      return false;
  for (std::size_t index = 0; index < left.definitions.size(); ++index) {
    const auto& a = left.definitions[index];
    const auto& b = right.definitions[index];
    if (a.ordinal != b.ordinal || a.operands != b.operands ||
        !same_slice(a.source, b.source))
      return false;
  }
  for (std::size_t index = 0; index < left.entries.size(); ++index) {
    const auto& a = left.entries[index];
    const auto& b = right.entries[index];
    if (a.depth != b.depth || a.style != b.style ||
        a.topic_id != b.topic_id || a.title != b.title ||
        !same_slice(a.source, b.source) ||
        !same_slices(a.topic_id_slices, b.topic_id_slices) ||
        !same_slices(a.title_slices, b.title_slices))
      return false;
  }
  for (std::size_t index = 0; index < left.groups.size(); ++index) {
    const auto& a = left.groups[index];
    const auto& b = right.groups[index];
    if (a.label != b.label || !same_slice(a.source, b.source) ||
        a.terms.size() != b.terms.size())
      return false;
    for (std::size_t term = 0; term < a.terms.size(); ++term) {
      const auto& x = a.terms[term];
      const auto& y = b.terms[term];
      if (x.level != y.level || x.term != y.term ||
          !same_slice(x.source, y.source) ||
          !same_slices(x.term_slices, y.term_slices) ||
          x.targets.size() != y.targets.size())
        return false;
      for (std::size_t target = 0; target < x.targets.size(); ++target)
        if (x.targets[target].topic_id != y.targets[target].topic_id ||
            x.targets[target].range_end_topic_id !=
                y.targets[target].range_end_topic_id ||
            !same_slices(x.targets[target].slices, y.targets[target].slices))
          return false;
    }
  }
  return true;
}

} // namespace

std::optional<GeneratedTocIndexTopicIR> extract_generated_toc_index_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const BookTopicCatalogIR* catalog, std::string* error) {
  return Extractor(records, catalog).run(error);
}

bool verify_generated_toc_index_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const BookTopicCatalogIR* catalog, const GeneratedTocIndexTopicIR& topic,
    std::string* error) {
  const auto canonical =
      extract_generated_toc_index_topic_ir(records, catalog, error);
  if (!canonical) return false;
  if (!same_topic(*canonical, topic))
    return fail(error,
                "generated navigation topic differs from canonical extraction");
  if (error != nullptr) error->clear();
  return true;
}

std::string format_generated_toc_index_topic_ir(
    const GeneratedTocIndexTopicIR& topic) {
  std::ostringstream out;
  out << "generated_navigation kind="
      << (topic.kind == GeneratedTocIndexKindIR::contents ? "contents"
                                                          : "index")
      << " heading='" << topic.heading_level << "' title='" << topic.title
      << "' lines=" << topic.lines.size()
      << " definitions=" << topic.definitions.size()
      << " entries=" << topic.entries.size()
      << " groups=" << topic.groups.size()
      << " delimiter=" << topic.delimiter << '\n';
  for (const auto& anchor : topic.anchors)
    out << "anchor id='" << anchor.first << "' source="
        << anchor.second.logical_record << ':' << anchor.second.token_begin
        << '\n';
  for (const auto& definition : topic.definitions) {
    out << "tocdef=" << definition.ordinal << " operands=";
    for (const auto& operand : definition.operands) out << ' ' << operand;
    out << '\n';
  }
  for (const auto& entry : topic.entries)
    out << "tocentry depth=" << entry.depth << " style=" << entry.style
        << " id='" << entry.topic_id << "' title='" << entry.title
        << "' source=" << entry.source.logical_record << ':'
        << entry.source.token_begin << '\n';
  for (const auto& group : topic.groups) {
    out << "group label='" << group.label << "' terms=" << group.terms.size()
        << '\n';
    for (const auto& term : group.terms) {
      out << "term level=" << term.level << " text='" << term.term
          << "' targets=";
      for (const auto& target : term.targets) {
        out << ' ' << target.topic_id;
        if (!target.range_end_topic_id.empty())
          out << "..." << target.range_end_topic_id;
      }
      out << " source=" << term.source.logical_record << ':'
          << term.source.token_begin << '\n';
    }
  }
  return out.str();
}

} // namespace geist::detail
