// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/drawn_word_conservation.hpp"
#include "geist/detail/lowering/drawn_word_source.hpp"

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/topic_identity.hpp"
#include "geist/detail/lowering/topic_lowering_outcome.hpp"
#include "geist/detail/render/html_visible_text.hpp"
#include "geist/detail/render/markdown_visible_text.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace geist::detail {

namespace {

bool alphanumeric_byte(const char raw) {
  const auto byte = static_cast<unsigned char>(raw);
  return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z');
}

// Markdown emphasis, code fencing and escaping, which the renderer writes
// *inside* a word: the source's `CLISTs` reaches the file as `` `CLIST`s ``.
// These bytes join rather than separate, on both sides of the comparison, so
// the two agree on where a word ends. Underscore is deliberately not here: it
// separates on both sides, and an escaped `\_` still does once the backslash
// is gone.
bool markup_glue_byte(const char raw) {
  return raw == '*' || raw == '`' || raw == '\\';
}

} // namespace

std::vector<std::string> conservation_words(const std::string& text) {
  std::vector<std::string> words;
  std::string current;
  for (const auto raw : text) {
    if (markup_glue_byte(raw)) continue;
    if (alphanumeric_byte(raw)) {
      current.push_back(ascii_lower_char(raw));
      continue;
    }
    if (!current.empty()) {
      words.push_back(current);
      current.clear();
    }
  }
  if (!current.empty()) words.push_back(current);
  return words;
}

namespace {

bool unit_mark(const char raw) {
  switch (raw) {
  case ',':
  case '.':
  case ';':
  case ':':
  case '!':
  case ')':
    return true;
  default:
    return false;
  }
}

} // namespace

std::vector<std::string> conservation_punctuation_units(const std::string& text) {
  std::vector<std::string> units;
  std::string current;
  for (const auto raw : text) {
    if (markup_glue_byte(raw)) continue;
    if (alphanumeric_byte(raw)) {
      current.push_back(ascii_lower_char(raw));
      continue;
    }
    if (!current.empty() && unit_mark(raw)) units.push_back(current + raw);
    current.clear();
  }
  return units;
}

namespace {

using CellKey = std::pair<std::size_t, std::size_t>;

// The cells of one record the ownership ledger attributes to a control's
// opcode or operand. Those cells are markup the reader consumes rather than
// prints, so they are not drawn text even though the framing positions them on
// a display line.
using ControlCells = std::unordered_map<std::uint32_t, std::vector<CellKey>>;

// Control opcodes the corpus stores that the segment decoder does not model,
// each of which owns its whole display line: the reader consumes the line and
// prints none of it.
//
// The decoder classifies the controls the renderer acts on and leaves the rest
// as text, which is the right trade for rendering and the wrong one for a
// conservation check: the operands of an unmodelled control stand inside their
// display line, where display text stands, and would be counted as drawn.
//
//   `si`         a topic's index entry -- the term and its qualifiers, which
//                the reader files into the book index and never prints in the
//                topic body (GX27-3999-00 record 18 lines 0-1,
//                `SI installation materials` / `SI blank diskettes`, neither
//                of which hosted serves in `1.2`).
//   `citerm`     a compiled index topic's term row, `cidelm` its delimiter,
//                `cgpsep` its letter-group separator, `cendindex` its closer
//   `cidelm`     (packet.boo record 369, the generated `INDEX` topic).
//   `cgpsep`
//   `cendindex`
//   `ctoce`      a compiled contents topic's entry row, `ctocdef` the level
//   `ctocdef`    format that topic opens with (XWEBDEMO record 3 lines 12-18,
//                `ctocdef=0 1 0 2` ... `ctocdef=6 0 8`).
//   `c.<word>`   every SCRIPT control (`c.cp`, `c.cc`, `C.SK`, `C.PA`, ...):
//                the reader skips any line whose control name starts with
//                `C.`, operands included.  The version 1.0/1.1 books write
//                `C.SK <n>` 7,689 times (EDCGV1R2 3.4.6 record 68 line 11
//                `C.SK 1`; hosted DT 19891031000000 serves no `1`).
//   `cvselect`   a selector inside an artwork description (`cartdesc` lines):
//                `<column> <length> <target>` over the description text the
//                reader puts in the picture's `alt`, never printed itself
//                (SC33-0668-02 1.2.6 record 141 line 39
//                `cvselect 13 8 FIGDSTAB`; hosted DT 19930625001740 serves
//                the description, and no `13 8 FIGDSTAB`, anywhere).
//
// An opcode that carries its first operand in the same token is matched on the
// part before the `=`, which is how `ctocdef=0` is stored.
//
// This exclusion applies only to the first token after the native length
// prefix. Indented prose such as SC24-546 B.2's literal `SI` lines is still
// counted: its first token is indentation, not the keyword. A prose word at
// the exact native control origin remains ambiguous without further source
// evidence; do not extend the exclusion past indentation to guess its role.
bool unmodelled_control_opcode(std::string opcode) {
  const auto operand = opcode.find('=');
  if (operand != std::string::npos) opcode.resize(operand);
  static const char* const opcodes[] = {"si",     "citerm",    "cidelm",
                                        "cgpsep", "cendindex", "ctoce",
                                        "ctocdef", "cvselect"};
  if (opcode.size() > 2 && opcode.compare(0, 2, "c.") == 0 &&
      std::all_of(opcode.begin() + 2, opcode.end(), [](const unsigned char ch) {
        return ch >= 'a' && ch <= 'z';
      }))
    return true;
  return std::any_of(std::begin(opcodes), std::end(opcodes),
                     [&](const char* candidate) { return opcode == candidate; });
}

// A modelled control whose display line carries printed text after the opcode:
// the title and the menu item's label stand on the opcode's line and reach
// the render. SRMSG is different: its number and optional symbolic name form
// an anchor identity; the following native row draws the message headline.
// Every other control's line is operands, which do not reach the render.
bool control_line_carries_text(BookControlKind kind) {
  switch (kind) {
  case BookControlKind::title:
  case BookControlKind::menu_item:
    return true;
  default:
    return false;
  }
}

// ASCII spelling of one token's own words, without its spacing prefix and
// surrounding spaces, folded to lower case.
std::string token_opcode_text(const LogicalTokenIR& token) {
  TokenWords words;
  for (const auto word : token.decoded_words)
    if (word >= 4) words.push_back(word);
  return ascii_lower(trim_ascii(token_words_to_ascii(words)));
}

// What one record's display-line framing proves about its control segments.
//
// A control's opcode is the first token of its own display line: the encoder
// writes the line's length byte and then the opcode, in every control the
// corpus stores. A segment whose opcode stands anywhere else claims a control
// the framing does not prove -- which is exactly how a word merely spelled like
// a control comes to be deleted from the render with an anchor put in its
// place.
//
// The ownership ledger conserves such a word, but as the phantom control's
// opcode, and attribution to a control is still attribution. So the ledger's
// control attribution is honoured here only where the framing proves the
// control it names; the cells of an unproven one stay drawn text and have to
// survive into the render like any other word.
struct RecordControlFraming {
  // Display lines the reader consumes whole: a proven control whose payload is
  // operands, or an opcode the segment decoder does not model.
  std::vector<bool> owned_lines;
  // Record-local tokens belonging to a proven control segment. A ledger cell
  // outside these is not excluded, whatever the ledger calls it.
  std::vector<bool> proven_tokens;
};

RecordControlFraming control_framing(
    const DecodedLogicalRecordSource& record,
    const std::vector<DisplayLineIR>& lines) {
  RecordControlFraming framing;
  framing.owned_lines.assign(lines.size(), false);
  framing.proven_tokens.assign(record.ir.tokens.size(), false);
  std::unordered_map<std::size_t, std::size_t> line_of_opening_token;
  for (std::size_t index = 0; index < lines.size(); ++index)
    line_of_opening_token.emplace(lines[index].prefix_token + 1, index);
  for (const auto& segment : record.control_segments) {
    if (segment.opcode.empty() || segment.display_text) continue;
    const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                        segment.opcode_range);
    if (words.begin >= record.assembled.sources.size()) continue;
    const auto& source = record.assembled.sources[words.begin];
    if (source.kind != LogicalWordSourceKind::token_word) continue;
    const auto found = line_of_opening_token.find(source.token_index);
    if (found == line_of_opening_token.end()) continue;
    for (const auto token : segment.source_tokens)
      if (token < framing.proven_tokens.size())
        framing.proven_tokens[token] = true;
    if (!control_line_carries_text(segment.kind))
      framing.owned_lines[found->second] = true;
  }
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto opening = lines[index].prefix_token + 1;
    if (opening >= lines[index].token_end) continue;
    if (unmodelled_control_opcode(token_opcode_text(record.ir.tokens[opening])))
      framing.owned_lines[index] = true;
  }
  return framing;
}

// Display columns `[begin, end)` of one display line that a picture's image
// replaces.
struct PlaceholderColumns {
  std::size_t line = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  // The row is the first display line of the record that follows.
  bool next_record = false;
};

// The `<n>` of a `PIC<n>` selector target, or empty.
std::string picture_number(const std::string& target) {
  if (target.size() < 4 || ascii_lower(target.substr(0, 3)) != "pic")
    return {};
  const auto number = target.substr(3);
  if (!std::all_of(number.begin(), number.end(), [](const char ch) {
        return ch >= '0' && ch <= '9';
      }))
    return {};
  return number;
}

// Every placeholder row of one record: a framing-proven `cselect <col> <len>
// PIC<n>` whose picture the document shows, and whose columns on the display
// line directly below the selector spell exactly `<word> <n>`.  The reader
// reads display lines as one stream, so a selector on a record's last line
// names the first line of the record that follows (`next`, when it is the
// adjacent framed record; SC31-8573-00 B.1 records 2282-2283).  Anything
// else -- a row that spells something else, a picture the document lacks --
// names no placeholder and its cells stay drawn.
std::vector<PlaceholderColumns> picture_placeholder_columns(
    const DecodedLogicalRecordSource& record,
    const std::vector<DisplayLineIR>& lines,
    const DrawnPicturePlaceholdersIR& pictures,
    const DecodedLogicalRecordSource* next) {
  std::vector<PlaceholderColumns> found;
  if (pictures.word.empty() || pictures.shown.empty()) return found;
  std::unordered_map<std::size_t, std::size_t> line_of_opening_token;
  for (std::size_t index = 0; index < lines.size(); ++index)
    line_of_opening_token.emplace(lines[index].prefix_token + 1, index);
  const auto text = token_words_to_ascii(record.assembled.words);
  for (const auto& segment : record.control_segments) {
    if (segment.kind != BookControlKind::select || segment.malformed ||
        segment.display_text)
      continue;
    const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                        segment.opcode_range);
    if (words.begin >= record.assembled.sources.size()) continue;
    const auto& source = record.assembled.sources[words.begin];
    if (source.kind != LogicalWordSourceKind::token_word) continue;
    const auto opening = line_of_opening_token.find(source.token_index);
    if (opening == line_of_opening_token.end()) continue;
    const auto* next_lines =
        next != nullptr && next->logical_record == record.logical_record + 1
            ? record_display_lines(*next)
            : nullptr;
    const auto crosses = opening->second + 1 >= lines.size();
    if (crosses && (next_lines == nullptr || next_lines->empty())) continue;
    const auto range = segment.operand_range;
    if (range.begin > range.end || range.end > text.size()) continue;
    std::istringstream operands(text.substr(range.begin, range.end - range.begin));
    std::string column_word, length_word, target, extra;
    if (!(operands >> column_word >> length_word >> target) ||
        (operands >> extra))
      continue;
    const auto number = picture_number(target);
    if (number.empty() || pictures.shown.count(number) == 0) continue;
    std::size_t column = 0;
    std::size_t length = 0;
    try {
      std::size_t used = 0;
      column = std::stoul(column_word, &used);
      if (used != column_word.size()) continue;
      length = std::stoul(length_word, &used);
      if (used != length_word.size()) continue;
    } catch (const std::exception&) {
      continue;
    }
    const auto row = crosses ? std::size_t{0} : opening->second + 1;
    const auto cells = crosses ? display_line_cells(*next, next_lines->front())
                               : display_line_cells(record, lines[row]);
    if (length == 0 || column + length > cells.size()) continue;
    std::string spelled;
    for (auto at = column; at < column + length; ++at)
      spelled += token_words_to_ascii({cells[at].word});
    if (spelled != pictures.word + " " + number) continue;
    found.push_back({row, column, column + length, crosses});
  }
  return found;
}

// The list-item label the reader blanks.
//
// Inside a list (`cz FLOW UL`, `OL` or `SL` opened and not yet closed by
// `cz OFF EUL`/`EOL`/`ESL`), a `cz FLOW LI <label> <text>` whose two columns
// differ makes the reader overwrite, on the next text line, the word that
// starts at the label column with blanks: the list element numbers or bullets
// the item itself.  The word is drawn by the source and never printed.
// GA27-4120-00 4.0 record 17 lines 16-18, `cz FLOW LI 3 12`,
// `cfont 3 4 2 8 2 2` and `   Step 1.  Power OFF ...`, are served (DT
// 19960419120629) as `<li>    <B>    </B> <B>1.</B>  Power OFF ...`: `Step`
// is blanked and `1.` kept.  The blanked run is the label column up to the
// first blank after it.
struct ListLabelState {
  std::size_t depth = 0;
  std::size_t pending_label = static_cast<std::size_t>(-1);

  // Reads a `cz` directive line; any other line is ignored.
  void directive(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const auto raw : line) {
      if (raw == ' ') {
        if (!field.empty()) fields.push_back(field);
        field.clear();
        continue;
      }
      field.push_back(ascii_lower_char(raw));
    }
    if (!field.empty()) fields.push_back(field);
    if (fields.size() < 3 || fields[0] != "cz") return;
    const auto& mode = fields[1];
    const auto& tag = fields[2];
    if (mode == "flow" && (tag == "ul" || tag == "ol" || tag == "sl")) {
      ++depth;
      pending_label = static_cast<std::size_t>(-1);
    } else if (mode == "off" && (tag == "eul" || tag == "eol" || tag == "esl")) {
      if (depth > 0) --depth;
    } else if (mode == "flow" && tag == "li" && fields.size() >= 5 &&
               depth > 0) {
      const auto number = [](const std::string& text, std::size_t* value) {
        if (text.empty() || text.size() > 4 ||
            !std::all_of(text.begin(), text.end(), [](const char ch) {
              return ch >= '0' && ch <= '9';
            }))
          return false;
        *value = static_cast<std::size_t>(std::stoul(text));
        return true;
      };
      std::size_t label = 0;
      std::size_t indent = 0;
      if (number(fields[3], &label) && number(fields[4], &indent) &&
          label != indent)
        pending_label = label;
    }
  }

  // The columns of `cells` the reader blanks on this text line, as
  // [begin, end); empty when no label is pending.
  std::pair<std::size_t, std::size_t> take(
      const std::vector<DisplayLineCellIR>& cells) {
    const auto label = pending_label;
    pending_label = static_cast<std::size_t>(-1);
    if (label == static_cast<std::size_t>(-1) || label >= cells.size() ||
        cells[label].word == ' ')
      return {0, 0};
    auto end = label + 1;
    while (end < cells.size() && cells[end].word != ' ') ++end;
    return {label, end};
  }
};

} // namespace

std::string picture_placeholder_word(const std::string& language) {
  // Hosted BookServer picks the word by the first letter of `CLANGUAGE`, and
  // for some letters the second.  `P` selects a word whose third byte the
  // reader stores outside ASCII; it is left without a word rather than
  // guessed, as is every letter pair the reader assigns none.  The letters
  // are compared as stored, upper case; any other first byte takes the
  // default word.
  const auto first = language.empty() ? '\0' : language[0];
  const auto second = language.size() < 2 ? '\0' : language[1];
  switch (first) {
  case 'B':
    if (second == 'D') return "AFBEELDING";
    if (second == 'F') return "IMAGE";
    if (second == 'P') return "FIGURA";
    return {};
  case 'C':
    if (second == 'A') return "IMAGEN";
    if (second == 'E') return "PICTURE";
    if (second == 'F') return "IMAGE";
    return {};
  case 'D':
    if (second == 'A') return "ILLUSTRATION";
    if (second == 'U') return "AFBEELDING";
    return {};
  case 'E':
  case 'U':
    return "PICTURE";
  case 'F':
    if (second == 'I') return "PICTURE";
    if (second == 'R') return "IMAGE";
    return {};
  case 'G':
    return "BILD";
  case 'I':
    if (second == 'C') return "PICTURE";
    if (second == 'T') return "IMMAGINE";
    return {};
  case 'N':
    return "BILDE";
  case 'P':
    return {};
  case 'S':
    if (second == 'F') return "IMAGE";
    if (second == 'G' || second == 'W') return "BILD";
    if (second == 'I') return "IMMAGINE";
    if (second == 'P') return "IMAGEN";
    return {};
  default:
    return "PICTURE";
  }
}

std::set<std::string>
pictures_shown_in_markdown(const std::string& markdown,
                           const DocumentRenderTraceIR& trace) {
  std::set<std::string> shown;
  for (const auto& span : trace.spans) {
    // A compound block the Markdown cannot express is embedded as HTML, and
    // a figure inside it is an `<img>` element of that block.
    if (span.text_projection == MarkdownTextProjectionIR::html) {
      static const std::string source = " src=\"resource:";
      const auto html = markdown.substr(span.output_begin,
                                        span.output_end - span.output_begin);
      for (auto at = html.find(source); at != std::string::npos;
           at = html.find(source, at + 1)) {
        const auto begin = at + source.size();
        const auto end = html.find('"', begin);
        const auto tag = html.rfind('<', at);
        if (end == std::string::npos || end == begin ||
            tag == std::string::npos || html.compare(tag, 5, "<img ") != 0)
          continue;
        shown.insert(html.substr(begin, end - begin));
      }
      continue;
    }
    if (span.reason != "image destination") continue;
    static const std::string open = "<resource:";
    const auto destination = markdown.substr(
        span.output_begin, span.output_end - span.output_begin);
    if (destination.size() > open.size() + 1 &&
        destination.compare(0, open.size(), open) == 0 &&
        destination.back() == '>')
      shown.insert(destination.substr(open.size(),
                                      destination.size() - open.size() - 1));
  }
  return shown;
}

DrawnTextIR project_drawn_topic_text(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, std::size_t* unframed_records,
    const DrawnPicturePlaceholdersIR* pictures) {
  ControlCells control;
  for (const auto& cell : ownership.cells) {
    if (cell.disposition != SourceDisposition::control_operand) continue;
    control[cell.logical_record].emplace_back(cell.token_index,
                                              cell.word_index);
  }
  for (auto& entry : control) std::sort(entry.second.begin(), entry.second.end());

  static const std::vector<CellKey> none;
  DrawnTextIR drawn;
  ListLabelState list;
  // Placeholder rows a selector on the previous record's last line names.
  std::vector<PlaceholderColumns> carried;
  for (std::size_t record_index = 0; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    const auto* lines = record_display_lines(record);
    auto inherited = std::move(carried);
    carried.clear();
    if (lines == nullptr) {
      ++*unframed_records;
      continue;
    }
    const auto found = control.find(record.logical_record);
    const auto& control_of_record =
        found == control.end() ? none : found->second;
    const auto framing = control_framing(record, *lines);
    auto placeholders = std::move(inherited);
    if (pictures != nullptr)
      for (const auto& columns : picture_placeholder_columns(
               record, *lines, *pictures,
               record_index + 1 < records.size() ? &records[record_index + 1]
                                                 : nullptr))
        (columns.next_record ? carried : placeholders).push_back(columns);
    for (std::size_t index = 0; index < lines->size(); ++index) {
      const auto& line = (*lines)[index];
      const auto text = display_line_text(record, line);
      if (framing.owned_lines[index]) {
        list.directive(trim_ascii(text));
        continue;
      }
      const auto source = drawn.sources.size();
      drawn.sources.push_back(trim_ascii(text));
      const auto cells = display_line_cells(record, line);
      const auto blanked = !cells.empty() && cells.front().word == ' '
                               ? list.take(cells)
                               : std::pair<std::size_t, std::size_t>{0, 0};
      for (std::size_t column = 0; column < cells.size(); ++column) {
        const auto& cell = cells[column];
        // A picture's placeholder words, which its image replaces, and the
        // list-item label the reader blanks.
        const auto placeholder = std::any_of(
            placeholders.begin(), placeholders.end(),
            [&](const PlaceholderColumns& columns) {
              return columns.line == index && column >= columns.begin &&
                     column < columns.end;
            });
        if (placeholder || (column >= blanked.first && column < blanked.second)) {
          drawn.text += ' ';
          drawn.opening.push_back(false);
          drawn.line.push_back(source);
          continue;
        }
        // The ledger's control attribution, accepted only where the framing
        // proves the control it names.
        const auto known_control =
            cell.token != static_cast<std::size_t>(-1) &&
            cell.token < framing.proven_tokens.size() &&
            framing.proven_tokens[cell.token] &&
            std::binary_search(control_of_record.begin(),
                               control_of_record.end(),
                               CellKey{cell.token, cell.word_index});
        const auto opening = cell.token == line.prefix_token + 1;
        const auto piece = known_control
                               ? std::string(" ")
                               : token_words_to_ascii({cell.word});
        drawn.text += piece;
        drawn.opening.insert(drawn.opening.end(), piece.size(), opening);
        drawn.line.insert(drawn.line.end(), piece.size(), source);
      }
      drawn.text.push_back('\n');
      drawn.opening.push_back(false);
      drawn.line.push_back(source);
    }
  }
  return drawn;
}

namespace {

struct DrawnCounts {
  std::size_t opening = 0;
  std::size_t inside = 0;
  // Display lines this word stands inside, in source order and without
  // repetition: the evidence a report needs to name a drop instead of counting
  // it.
  std::vector<std::size_t> lines;
};

// Cuts the drawn text into the same alphanumeric runs `conservation_words`
// produces, and files each run by the position of its first byte.
std::map<std::string, DrawnCounts> count_drawn_words(const DrawnTextIR& drawn,
                                                     std::size_t* total) {
  std::map<std::string, DrawnCounts> counts;
  std::string current;
  bool opening = false;
  std::size_t line = 0;
  const auto flush = [&]() {
    if (current.empty()) return;
    auto& entry = counts[current];
    (opening ? entry.opening : entry.inside) += 1;
    if (!opening &&
        std::find(entry.lines.begin(), entry.lines.end(), line) ==
            entry.lines.end())
      entry.lines.push_back(line);
    ++*total;
    current.clear();
  };
  for (std::size_t at = 0; at < drawn.text.size(); ++at) {
    if (markup_glue_byte(drawn.text[at])) continue;
    if (!alphanumeric_byte(drawn.text[at])) {
      flush();
      continue;
    }
    if (current.empty()) {
      opening = drawn.opening[at];
      line = drawn.line[at];
    }
    current.push_back(ascii_lower_char(drawn.text[at]));
  }
  flush();
  return counts;
}

// The punctuation units of the drawn text (`conservation_punctuation_units`),
// leaving out every unit whose word opens a display line.
std::map<std::string, DrawnCounts> count_drawn_units(const DrawnTextIR& drawn) {
  std::map<std::string, DrawnCounts> counts;
  std::string current;
  bool opening = false;
  std::size_t line = 0;
  for (std::size_t at = 0; at < drawn.text.size(); ++at) {
    const auto raw = drawn.text[at];
    if (markup_glue_byte(raw)) continue;
    if (alphanumeric_byte(raw)) {
      if (current.empty()) {
        opening = drawn.opening[at];
        line = drawn.line[at];
      }
      current.push_back(ascii_lower_char(raw));
      continue;
    }
    if (!current.empty() && unit_mark(raw) && !opening) {
      auto& entry = counts[current + raw];
      ++entry.inside;
      if (std::find(entry.lines.begin(), entry.lines.end(), line) ==
          entry.lines.end())
        entry.lines.push_back(line);
    }
    current.clear();
  }
  return counts;
}

} // namespace
} // namespace geist::detail

namespace geist {

detail::DrawnWordConservationIR BooDocument::drawn_word_conservation(
    const bool retain_rendered) const {
  using namespace detail;
  DrawnWordConservationIR report;

  for (const auto& catalog_entry : topic_catalog_ir_->topics) {
    TocEntry synthesized;
    const TocEntry* entry = nullptr;
    if (!catalog_entry.toc_entries.empty()) {
      entry = &toc_.at(catalog_entry.toc_entries.front().toc_index);
    } else if (catalog_entry.topic_header) {
      synthesized = synthesize_topic_entry(catalog_entry.raw_topic_id);
      entry = &synthesized;
    } else {
      continue;
    }

    DrawnWordTopicIR topic;
    topic.id = catalog_entry.raw_topic_id;
    topic.route = entry->render_diagnostic().route;
    if (catalog_entry.topic_header) {
      topic.source_kind = "topic-index";
      topic.source_index = catalog_entry.topic_header->topic_info_index;
      topic.source_start_logical_record =
          catalog_entry.topic_header->start_logical_record;
      topic.source_end_logical_record =
          catalog_entry.topic_header->end_logical_record;
    } else {
      const auto& toc_source = catalog_entry.toc_entries.front();
      topic.source_kind = "toc-only";
      topic.source_index = toc_source.toc_index;
      topic.source_start_logical_record = toc_source.start_logical_record;
      topic.source_end_logical_record = toc_source.end_logical_record;
    }

    const auto identity = make_topic_identity(*entry);
    if (!topic_identity_has_body(identity)) {
      ++report.topics_checked;
      report.topics.push_back(std::move(topic));
      continue;
    }

    const auto records = decode_logical_record_sources(
        *decode_context_, identity.start_logical_record,
        identity.end_logical_record);
    topic.records = records.size();
    const auto layout = extract_layout_ir(records);
    // The ledger is read for its control attribution only, so a ledger that
    // fails verification is not fatal here: it would cost the check its
    // control exclusions for this topic, which can only add candidate words,
    // never hide one.
    const auto ownership = build_ownership_ir(records, layout);

    // Independently check both serializers. The Markdown serializer records
    // exactly which emitted byte windows are labels, code, raw HTML, visible
    // markers or structural syntax: no general Markdown parser is needed.
    DocumentRenderTraceIR markdown_trace;
    std::string markdown;
    const auto& outcome = entry->lowered().outcome;
    if (outcome && outcome->document)
      markdown = render_document_markdown(*outcome->document, {}, &markdown_trace);

    // The book pictures the lowered document places, read off the image
    // destinations its own render wrote (`<resource:<n>>`).
    DrawnPicturePlaceholdersIR pictures;
    pictures.word = picture_placeholder_word(book_properties_.language);
    pictures.shown = pictures_shown_in_markdown(markdown, markdown_trace);
    const auto drawn = project_drawn_topic_text(
        records, ownership, &topic.unframed_records, &pictures);
    const auto drawn_counts = count_drawn_words(drawn, &topic.drawn_words);
    const auto append = [&](const std::string& bytes,
                            const MarkdownTextProjectionIR projection) {
      if (bytes.empty()) return;
      DocumentTraceSpanIR span;
      span.output_begin = markdown.size();
      markdown += bytes;
      span.output_end = markdown.size();
      span.text_projection = projection;
      markdown_trace.spans.push_back(std::move(span));
    };
    const auto append_separator = [&]() {
      if (!markdown.empty() && markdown.back() != '\n')
        append("\n", MarkdownTextProjectionIR::boundary);
      if (!markdown.empty()) append("\n", MarkdownTextProjectionIR::boundary);
    };
    const auto& diagnostic = entry->render_diagnostic();
    if (diagnostic.severity == RenderSeverity::failed) {
      append_separator();
      // A diagnostic about missing content is not book text and cannot
      // compensate for the content whose failure it reports.
      append(render_failed_markdown(identity, diagnostic),
             MarkdownTextProjectionIR::hidden);
    }
    // Re-rendering must not bypass truncation or mutation in the topic cache
    // wrapper. Account only for the exact diagnostic prefix that render()
    // emits, then require byte identity with the actual public Markdown.
    const auto marker = entry->document_ir_loader_
                            ? render_diagnostic_comment(diagnostic) : std::string();
    const auto prefix = marker.empty() ? std::string() :
        marker + "\n" + (markdown.empty() ? "" : "\n");
    const auto actual = entry->markdown();
    auto markdown_visible =
        verified_markdown_visible_text(actual, markdown, markdown_trace, prefix);
    auto html = entry->html_fragment();
    std::map<std::string, std::size_t> markdown_counts;
    for (const auto& word : conservation_words(markdown_visible))
      ++markdown_counts[word];
    std::map<std::string, std::size_t> html_counts;
    const auto html_visible = html_visible_text(html);
    for (const auto& word : conservation_words(html_visible))
      ++html_counts[word];
    std::map<std::string, std::size_t> markdown_units;
    for (const auto& unit : conservation_punctuation_units(markdown_visible))
      ++markdown_units[unit];
    std::map<std::string, std::size_t> html_units;
    for (const auto& unit : conservation_punctuation_units(html_visible))
      ++html_units[unit];
    for (const auto& [unit, counts] : count_drawn_units(drawn)) {
      const auto emitted_markdown = markdown_units[unit];
      const auto emitted_html = html_units[unit];
      // The HTML fragment is the exact visible text; the Markdown projection
      // reads the `:` of a `**Note:**` label as syntax, so it only informs.
      const auto emitted = emitted_html;
      if (emitted >= counts.inside) continue;
      std::string evidence;
      for (std::size_t at = 0; at < counts.lines.size() && at < 3; ++at) {
        if (!evidence.empty()) evidence += " | ";
        evidence += drawn.sources[counts.lines[at]];
      }
      if (counts.lines.size() > 3)
        evidence += " | +" + std::to_string(counts.lines.size() - 3) +
                    " more lines";
      topic.punctuation_deficits.push_back(
          {unit, counts.inside, emitted, emitted_markdown, emitted_html,
           counts.inside - emitted, evidence});
      topic.unaccounted_punctuation += counts.inside - emitted;
    }
    if (retain_rendered) {
      topic.markdown_visible_text = std::move(markdown_visible);
      topic.html_fragment = std::move(html);
    }
    std::map<std::string, std::size_t> emitted_counts;
    for (const auto& [word, count] : markdown_counts) {
      // Never let a correct HTML label compensate for lost Markdown text, or
      // a Markdown URL compensate for an absent HTML label.
      const auto retained = std::min(count, html_counts[word]);
      emitted_counts[word] = retained;
      topic.emitted_words += retained;
    }
    for (const auto& [word, counts] : drawn_counts) {
      const auto found = emitted_counts.find(word);
      const auto emitted = found == emitted_counts.end() ? 0 : found->second;
      const auto drawn_total = counts.opening + counts.inside;
      if (emitted >= drawn_total) continue;
      const auto shortfall = drawn_total - emitted;
      const auto markdown_found = markdown_counts.find(word);
      const auto emitted_markdown = markdown_found == markdown_counts.end()
                                        ? 0
                                        : markdown_found->second;
      const auto html_found = html_counts.find(word);
      const auto emitted_html =
          html_found == html_counts.end() ? 0 : html_found->second;
      const auto markdown_shortfall =
          drawn_total > emitted_markdown ? drawn_total - emitted_markdown : 0;
      const auto html_shortfall =
          drawn_total > emitted_html ? drawn_total - emitted_html : 0;
      // Forgive the opening occurrences first: those are the ones that could
      // be a control opcode the segment decoder does not classify. What is
      // left stood inside a display line, where no control opcode ever does.
      const auto forgiven = std::min(shortfall, counts.opening);
      // Name the lines the word stands on, so the report enumerates the drop
      // instead of counting it. Three is enough to recognise the site without
      // turning a repeated word into a transcript of the topic.
      std::string evidence;
      for (std::size_t at = 0; at < counts.lines.size() && at < 3; ++at) {
        if (!evidence.empty()) evidence += " | ";
        evidence += drawn.sources[counts.lines[at]];
      }
      if (counts.lines.size() > 3)
        evidence += " | +" + std::to_string(counts.lines.size() - 3) +
                    " more lines";
      const auto unaccounted_markdown =
          markdown_shortfall > counts.opening
              ? markdown_shortfall - counts.opening
              : 0;
      const auto unaccounted_html =
          html_shortfall > counts.opening ? html_shortfall - counts.opening
                                          : 0;
      topic.deficits.push_back(
          {word, counts.opening, counts.inside, emitted, emitted_markdown,
           emitted_html, shortfall - forgiven, unaccounted_markdown,
           unaccounted_html, evidence});
      topic.forgiven += forgiven;
      topic.unaccounted += shortfall - forgiven;
      topic.unaccounted_markdown += unaccounted_markdown;
      topic.unaccounted_html += unaccounted_html;
    }

    ++report.topics_checked;
    if (topic.unframed_records != 0) ++report.topics_with_unframed_records;
    if (topic.unaccounted != 0) ++report.topics_dropping;
    if (topic.unaccounted_markdown != 0) ++report.topics_dropping_markdown;
    if (topic.unaccounted_html != 0) ++report.topics_dropping_html;
    if (topic.unaccounted_punctuation != 0)
      ++report.topics_dropping_punctuation;
    report.unaccounted_punctuation += topic.unaccounted_punctuation;
    report.unaccounted_words += topic.unaccounted;
    report.unaccounted_markdown_words += topic.unaccounted_markdown;
    report.unaccounted_html_words += topic.unaccounted_html;
    report.forgiven_words += topic.forgiven;
    report.topics.push_back(std::move(topic));
  }
  return report;
}

} // namespace geist
