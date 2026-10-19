// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/picture_figure_content_ir.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/picture_placement_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {
using Key = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using Token = std::pair<std::uint32_t, std::size_t>;
constexpr auto npos = std::numeric_limits<std::size_t>::max();
bool fail(std::string *error, std::string message) {
  if (error)
    *error = std::move(message);
  return false;
}
std::vector<std::size_t> range_tokens(const DecodedLogicalRecordSource &r,
                                      OutputRangeIR bytes) {
  const auto words = decoded_byte_range_to_word_range(r.assembled, bytes);
  return source_tokens_intersecting_output(r.assembled, words.begin, words.end);
}
bool blank(std::uint16_t word) { return word == ' ' || word == 0xa0; }
bool outline(std::uint16_t word) {
  return blank(word) || word == '_' || word == '|' || word == 0x2666 ||
         word == 0xffff || (word >= 0x2500 && word <= 0x25ff);
}
std::string part_text(const FigureContentPartIR &part) {
  std::string text;
  for (const auto &r : part.runs)
    text += r.text;
  return text;
}
// Caption grammar is an annotation, never a content-admission condition.
std::size_t caption_title_column(const std::string &text) {
  std::istringstream words(text);
  std::string name, number;
  if (!(words >> name >> number) || ascii_lower(name) != "figure" ||
      number.size() < 2 || (number.back() != '.' && number.back() != ':') ||
      !std::isalnum(static_cast<unsigned char>(number.front())))
    return npos;
  const auto number_at = text.find(number, text.find(name) + name.size());
  auto at = number_at + number.size();
  while (at < text.size() && text[at] == ' ')
    ++at;
  return at;
}
struct Row {
  const DecodedLogicalRecordSource *record = nullptr;
  DisplayLineIR line;
  std::vector<DisplayLineCellIR> cells;
  std::vector<bool> hidden;
  std::vector<FontSpanIR> fonts;
  std::vector<const SelectorIR *> selectors;
  bool index = false;
  bool metadata = false;
  bool barrier = false;
};
struct Attr {
  FontStyleIR style = FontStyleIR::unknown;
  const SelectorIR *link = nullptr;
};
struct Reader {
  const std::vector<DecodedLogicalRecordSource> &records;
  const LayoutIR &layout;
  const OwnershipIR &ownership;
  const SelectorCatalogIR &selectors;
  const PictureFigureSourceIR &source;
  FigureSourceBlockIR &figure;
  std::string *error;
  std::map<Key, FigureCellRoleIR> roles;
  std::map<Token, std::size_t> owners;
  std::map<std::uint32_t, const DecodedLogicalRecordSource *> by_record;
  std::set<Token> controls;
  std::vector<Row> rows;
  bool allow_wrap = false;

  bool inside(std::uint32_t record, std::size_t token) const {
    const auto found = source.tokens.find(record);
    return found != source.tokens.end() && token >= found->second.first &&
           token <= found->second.second;
  }
  void mark_token(const DecodedLogicalRecordSource &r, std::size_t t,
                  FigureCellRoleIR role) {
    if (!inside(r.logical_record, t) || t >= r.tokens.size())
      return;
    for (std::size_t w = 0; w < r.tokens[t].size(); ++w)
      roles[{r.logical_record, t, w}] = role;
  }
  void mark_cells(const Row &row, std::size_t begin, std::size_t end,
                  FigureCellRoleIR role) {
    for (auto i = begin; i < end && i < row.cells.size(); ++i) {
      const auto &c = row.cells[i];
      if (!row.hidden[i] && c.token != npos &&
          inside(row.record->logical_record, c.token))
        roles[{row.record->logical_record, c.token, c.word_index}] = role;
    }
  }
  const ControlSegmentIR *control_at(const DecodedLogicalRecordSource &r,
                                     const DisplayLineIR &line) const {
    for (const auto &s : r.control_segments) {
      if (s.display_text || s.opcode.empty())
        continue;
      const auto opcode = range_tokens(r, s.opcode_range);
      if (!opcode.empty() && opcode.front() == line.prefix_token + 1 &&
          inside(r.logical_record, opcode.front()))
        return &s;
    }
    return nullptr;
  }
  bool index_at(const DecodedLogicalRecordSource &r,
                const DisplayLineIR &line) const {
    return is_subject_index_display_line(r, line);
  }
  std::vector<DocumentSourceRowIR> physical_rows(const Row &row) const {
    std::vector<DocumentSourceRowIR> result;
    for (const auto &run : layout.runs)
      for (std::size_t i = 0; i < run.rows.size(); ++i) {
        const auto &r = run.rows[i];
        if (r.logical_record == row.record->logical_record &&
            r.token_begin < row.line.token_end &&
            r.token_end > row.line.prefix_token + 1)
          result.push_back({run.id, i});
      }
    return result;
  }
  bool prepare() {
    bool description_open = false;
    for (const auto &r : records) {
      by_record[r.logical_record] = &r;
      for (const auto &s : r.control_segments)
        for (const auto t : s.source_tokens)
          owners.emplace(Token{r.logical_record, t}, s.segment_index);
      const auto range = source.tokens.find(r.logical_record);
      if (range == source.tokens.end())
        continue;
      const auto *lines = record_display_lines(r);
      if (!lines)
        return fail(error, "picture region has undecided display framing");
      for (const auto &line : *lines) {
        if (line.token_end <= range->second.first ||
            line.prefix_token > range->second.second)
          continue;
        Row row;
        row.record = &r;
        row.line = line;
        row.cells = display_line_cells(r, line);
        row.hidden.resize(row.cells.size());
        mark_token(r, line.prefix_token, FigureCellRoleIR::line_prefix);
        for (auto token = line.prefix_token + 1; token < line.token_end;
             ++token) {
          if (!inside(r.logical_record, token))
            continue;
          for (std::size_t w = 0; w < r.tokens[token].size(); ++w)
            if (w == 0 && r.tokens[token][w] < 4)
              roles[{r.logical_record, token, w}] =
                  FigureCellRoleIR::text_layout;
        }
        row.index = index_at(r, line);
        if (row.index) {
          for (auto t = line.prefix_token + 1; t < line.token_end; ++t)
            mark_token(r, t, FigureCellRoleIR::index_term);
          figure.index_terms.push_back(display_line_text(r, line));
        }
        const auto *control = control_at(r, line);
        if (control) {
          const bool owned =
              std::any_of(source.segments.begin(), source.segments.end(),
                          [&](const auto &ref) {
                            return ref.logical_record == r.logical_record &&
                                   ref.segment_index == control->segment_index;
                          });
          if (!owned)
            return fail(error, "picture content reaches an unrelated control");
          const auto tokens = range_tokens(
              r, {control->opcode_range.begin, control->operand_range.end});
          for (const auto t : tokens)
            if (t > line.prefix_token && t < line.token_end) {
              controls.insert({r.logical_record, t});
              mark_token(r, t, FigureCellRoleIR::control);
            }
          if (control->kind == BookControlKind::art_description_start)
            description_open = true;
          if (control->kind == BookControlKind::font && !description_open) {
            auto fonts = decode_font_control_spans(r, *control, error);
            if (!fonts)
              return false;
            row.fonts = fonts->spans;
          }
          if (control->kind == BookControlKind::select) {
            const auto it =
                std::find_if(selectors.selectors.begin(),
                             selectors.selectors.end(), [&](const auto &s) {
                               return s.logical_record == r.logical_record &&
                                      s.segment_index == control->segment_index;
                             });
            if (it == selectors.selectors.end() || !it->canonical_operands ||
                it->inside_table)
              return fail(error,
                          "picture selector has no independent canonical span");
            row.selectors.push_back(&*it);
            for (const auto t : selector_link_alternative_tokens(r, *control)) {
              controls.insert({r.logical_record, t});
              mark_token(r, t, FigureCellRoleIR::control);
            }
          }
          row.metadata =
              description_open ||
              control->kind == BookControlKind::art_description ||
              control->kind == BookControlKind::art_description_start ||
              control->kind == BookControlKind::art_description_end;
          row.barrier = control->kind == BookControlKind::layout_directive;
          if (control->kind == BookControlKind::art_description_end)
            description_open = false;
        }
        // Description formatting (including CVFONT) can occupy independent
        // native lines without being split into its own control segment.
        row.metadata = row.metadata || description_open;
        // SCRIPT pagination is a native whole-line control, even when the
        // flattened splitter left it inside a selector's display payload.
        const auto native = trim_ascii_whitespace(display_line_text(r, line));
        if (native == "c.cp" || native == "c.cc" || native == "c.cp 0" ||
            native == "c.cc 0") {
          for (auto t = line.prefix_token + 1; t < line.token_end; ++t) {
            controls.insert({r.logical_record, t});
            mark_token(r, t, FigureCellRoleIR::control);
          }
        } else if (native.rfind("c.cp ", 0) == 0 ||
                   native.rfind("c.cc ", 0) == 0) {
          return fail(error,
                      "unsupported pagination operands inside picture region");
        }
        for (std::size_t i = 0; i < row.cells.size(); ++i) {
          const auto &c = row.cells[i];
          row.hidden[i] =
              c.token != npos && (!inside(r.logical_record, c.token) ||
                                  controls.count({r.logical_record, c.token}) ||
                                  row.index || row.metadata);
          if (row.metadata && c.token != npos &&
              inside(r.logical_record, c.token) &&
              !controls.count({r.logical_record, c.token}))
            roles[{r.logical_record, c.token, c.word_index}] =
                FigureCellRoleIR::description;
        }
        // The native three-column revision margin is layout, not caption
        // text. Do not strip vertical rules anywhere else in a text row.
        if (row.cells.size() > 3 && blank(row.cells[0].word) &&
            (row.cells[1].word == '|' || row.cells[1].word == 0x2502) &&
            blank(row.cells[2].word)) {
          const auto &bar = row.cells[1];
          if (!row.hidden[1] && bar.token != npos) {
            roles[{r.logical_record, bar.token, bar.word_index}] =
                FigureCellRoleIR::text_layout;
            row.hidden[1] = true;
          }
        }
        rows.push_back(std::move(row));
      }
    }
    return true;
  }
  DocumentSourceSliceIR slice(const Row &row,
                              const DisplayLineCellIR &c) const {
    const auto &r = *row.record;
    DocumentSourceSliceIR out;
    out.logical_record = r.logical_record;
    const auto owner = owners.find({r.logical_record, c.token});
    out.segment_index = owner == owners.end() ? 0 : owner->second;
    out.token_begin = c.token;
    out.token_end = c.token + 1;
    out.byte_begin = r.ir.tokens[c.token].byte_range.begin;
    out.byte_end = r.ir.tokens[c.token].byte_range.end;
    const auto &words = r.tokens[c.token];
    const auto start = !words.empty() && words[0] < 4 ? 1u : 0u;
    out.character_begin = static_cast<std::uint32_t>(
        token_words_to_ascii(
            TokenWords(words.begin() + start, words.begin() + c.word_index))
            .size());
    out.character_end =
        out.character_begin +
        static_cast<std::uint32_t>(token_words_to_ascii({c.word}).size());
    return out;
  }
  bool text_part(Row &row, std::size_t begin, std::size_t end,
                 const std::vector<Attr> &attrs) {
    while (begin < end && (row.hidden[begin] || blank(row.cells[begin].word)))
      ++begin;
    while (end > begin &&
           (row.hidden[end - 1] || blank(row.cells[end - 1].word)))
      --end;
    if (begin == end)
      return true;
    FigureContentPartIR part;
    part.column = begin;
    part.rows = physical_rows(row);
    bool space = false;
    for (auto at = begin; at < end; ++at) {
      const auto &c = row.cells[at];
      if (row.hidden[at])
        continue;
      const auto &attr = attrs[at];
      if (attr.link && attr.style != FontStyleIR::unknown)
        return fail(error,
                    "styled link inside picture text is not representable");
      if (c.word == 0xffff)
        return fail(error, "unmapped cell inside picture text");
      if (blank(c.word)) {
        space = !part.runs.empty();
        continue;
      }
      auto text = figure_display_glyph(c.word);
      const auto same = [&](const FigureTextRunIR &run) {
        return run.style == attr.style &&
               (run.link.has_value() == (attr.link != nullptr)) &&
               (!attr.link ||
                (run.link->selector.logical_record ==
                     attr.link->logical_record &&
                 run.link->selector.segment_index == attr.link->segment_index));
      };
      if (space) {
        if (!part.runs.empty())
          part.runs.back().text += ' ';
        space = false;
      }
      if (part.runs.empty() || !same(part.runs.back())) {
        FigureTextRunIR run;
        run.style = attr.style;
        if (attr.link) {
          FigureLinkIR link;
          link.selector = {attr.link->logical_record, attr.link->segment_index,
                           attr.link->selector_ordinal};
          link.target = attr.link->target;
          if (ascii_lower(link.target) == "lnk") {
            std::vector<std::string> alternatives;
            const auto &r = *by_record.at(attr.link->logical_record);
            selector_link_alternative_tokens(
                r, r.control_segments.at(attr.link->segment_index),
                &alternatives);
            const auto parsed = parse_selector_link(alternatives, error);
            if (!parsed || parsed->kind == SelectorLinkKindIR::external_image)
              return fail(error, "unsupported link inside picture text");
            link.target = parsed->destination;
            link.external = parsed->kind == SelectorLinkKindIR::external_link;
          }
          run.link = std::move(link);
        }
        part.runs.push_back(std::move(run));
      }
      part.runs.back().text += text;
      if (c.token != npos) {
        auto source = slice(row, c);
        auto &slices = part.runs.back().slices;
        if (!slices.empty() &&
            slices.back().logical_record == source.logical_record &&
            slices.back().segment_index == source.segment_index &&
            slices.back().token_begin == source.token_begin)
          slices.back().character_end = source.character_end;
        else
          slices.push_back(source);
        roles[{row.record->logical_record, c.token, c.word_index}] =
            FigureCellRoleIR::text_content;
      }
    }
    if (part.runs.empty())
      return true;
    // Native indentation proves soft wrapping of an explicitly numbered
    // caption. No arbitrary prose is absorbed as a continuation.
    std::string native_text;
    for (auto at = begin; at < end; ++at)
      if (!row.hidden[at])
        native_text += figure_display_glyph(row.cells[at].word);
    const auto title = caption_title_column(native_text);
    part.caption = title != npos;
    part.continuation_column = part.caption ? part.column + title : npos;
    if (allow_wrap && !figure.content.empty() && !part.caption) {
      auto &previous = figure.content.back();
      if (!previous.picture && previous.caption &&
          previous.continuation_column == part.column) {
        previous.runs.back().text += ' ';
        previous.runs.insert(previous.runs.end(), part.runs.begin(),
                             part.runs.end());
        previous.rows.insert(previous.rows.end(), part.rows.begin(),
                             part.rows.end());
        return true;
      }
    }
    figure.content.push_back(std::move(part));
    allow_wrap = true;
    return true;
  }
  std::size_t picture_index(const SelectorIR &selector) const {
    const SelectorRefIR ref{selector.logical_record, selector.segment_index,
                            selector.selector_ordinal};
    if (ref.logical_record == figure.selector.logical_record &&
        ref.segment_index == figure.selector.segment_index)
      return 0;
    for (std::size_t i = 0; i < figure.additional_pictures.size(); ++i) {
      const auto &r = figure.additional_pictures[i].selector;
      if (ref.logical_record == r.logical_record &&
          ref.segment_index == r.segment_index)
        return i + 1;
    }
    return npos;
  }
  bool emit() {
    std::vector<FontSpanIR> fonts;
    std::vector<const SelectorIR *> pending;
    std::set<std::size_t> pictures;
    for (auto &row : rows) {
      fonts.insert(fonts.end(), row.fonts.begin(), row.fonts.end());
      pending.insert(pending.end(), row.selectors.begin(), row.selectors.end());
      bool visible = false, frame = true;
      for (std::size_t i = 0; i < row.cells.size(); ++i)
        if (!row.hidden[i] && !blank(row.cells[i].word)) {
          visible = true;
          frame = frame && outline(row.cells[i].word);
        }
      if (!visible) {
        if (row.line.token_end == row.line.prefix_token + 1 || row.barrier)
          allow_wrap = false;
        continue;
      }
      if (frame && pending.empty()) {
        mark_cells(row, 0, row.cells.size(),
                   FigureCellRoleIR::placeholder_suppressed);
        fonts.clear();
        continue;
      }
      std::vector<Attr> attrs(row.cells.size());
      for (const auto &font : fonts) {
        if (font.style == FontStyleIR::unknown && font.code != "0")
          return fail(error,
                      "unsupported font in picture region: " + font.code);
        if (font.column >= attrs.size() || font.length == 0)
          return fail(error, "font lies outside native picture text row");
        const auto end =
            font.column + std::min(font.length, attrs.size() - font.column);
        for (auto at = font.column; at < end; ++at) {
          if (attrs[at].style != FontStyleIR::unknown &&
              attrs[at].style != font.style)
            return fail(error, "overlapping picture font spans");
          attrs[at].style = font.style;
        }
      }
      fonts.clear();
      std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> placed;
      for (const auto *selector : pending) {
        if (selector->column >= attrs.size() || selector->length == 0)
          return fail(error, "selector lies outside native picture text row");
        const auto end =
            selector->column +
            std::min(selector->length, attrs.size() - selector->column);
        const auto image = picture_index(*selector);
        if (image != npos) {
          std::string label;
          for (auto at = selector->column; at < end; ++at) {
            if (row.hidden[at])
              return fail(error,
                          "picture placement intersects control material");
            label += figure_display_glyph(row.cells[at].word);
          }
          if (selector->target != "LNK" &&
              ascii_lower(selector->target) != "lnk" &&
              !picture_placement_span(
                  row.cells, selector->column, selector->length,
                  figure_picture_resource(selector->target)))
            return fail(
                error,
                "picture placement does not match its native placeholder");
          if (!pictures.insert(image).second)
            return fail(error, "picture has multiple placements");
          if (image == 0)
            figure.placeholder_text = trim_ascii_whitespace(label);
          else
            figure.additional_pictures[image - 1].placeholder_text =
                trim_ascii_whitespace(label);
          placed.emplace_back(selector->column, end, image);
        } else {
          for (auto at = selector->column; at < end; ++at) {
            if (attrs[at].link)
              return fail(error, "overlapping links in picture text");
            attrs[at].link = selector;
          }
        }
      }
      pending.clear();
      std::sort(placed.begin(), placed.end());
      std::size_t begin = 0;
      for (const auto &place : placed) {
        const auto from = std::get<0>(place), to = std::get<1>(place);
        if (from < begin)
          return fail(error, "overlapping picture placement spans");
        // A box frame is not prose around the image it encloses.
        const auto emit_text = [&](std::size_t a, std::size_t b) {
          bool frame_only = true;
          for (auto i = a; i < b; ++i)
            if (!row.hidden[i] && !outline(row.cells[i].word))
              frame_only = false;
          if (frame_only) {
            mark_cells(row, a, b, FigureCellRoleIR::placeholder_suppressed);
            return true;
          }
          return text_part(row, a, b, attrs);
        };
        if (!emit_text(begin, from))
          return false;
        FigureContentPartIR part;
        part.picture = true;
        part.picture_index = std::get<2>(place);
        part.rows = physical_rows(row);
        figure.content.push_back(std::move(part));
        allow_wrap = false;
        mark_cells(row, from, to, FigureCellRoleIR::placeholder_suppressed);
        begin = to;
      }
      bool frame_only = true;
      for (auto i = begin; i < row.cells.size(); ++i)
        if (!row.hidden[i] && !outline(row.cells[i].word))
          frame_only = false;
      if (frame_only)
        mark_cells(row, begin, row.cells.size(),
                   FigureCellRoleIR::placeholder_suppressed);
      else if (!text_part(row, begin, row.cells.size(), attrs))
        return false;
    }
    if (pictures.size() != figure.additional_pictures.size() + 1)
      return fail(error, "picture region leaves an image unplaced");
    if (!pending.empty())
      return fail(error, "picture region leaves a selector unbound");
    // A sole terminal label is the unnumbered caption. This is a region
    // relationship, not a six-line grammar or a test of its English words.
    if (!figure.content.empty() && !figure.content.back().picture) {
      auto &last = figure.content.back();
      if (!last.caption && figure.content.size() == 2 &&
          figure.content.front().picture)
        last.caption = true;
    }
    figure.caption.reset();
    for (auto &part : figure.content) {
      std::vector<FigureTextRunIR> merged;
      for (auto &run : part.runs) {
        if (!merged.empty() && !run.link && !merged.back().link &&
            merged.back().style == run.style) {
          merged.back().text += run.text;
          merged.back().slices.insert(merged.back().slices.end(),
                                      run.slices.begin(), run.slices.end());
        } else
          merged.push_back(std::move(run));
      }
      part.runs = std::move(merged);
      if (!part.caption)
        continue;
      figure.caption = FigureCaptionIR{part_text(part), part.rows, {}};
      for (const auto &run : part.runs)
        for (const auto &slice : run.slices) {
          const auto *record = by_record.at(slice.logical_record);
          const auto &words = record->tokens[slice.token_begin];
          const auto start = !words.empty() && words[0] < 4 ? 1u : 0u;
          std::size_t offset = 0;
          for (auto at = start; at < words.size(); ++at) {
            const auto end = offset + token_words_to_ascii({words[at]}).size();
            if (offset >= slice.character_begin && end <= slice.character_end) {
              auto found =
                  roles.find({slice.logical_record, slice.token_begin, at});
              if (found != roles.end() &&
                  found->second == FigureCellRoleIR::text_content)
                found->second =
                    (words[at] >= 0x80 ||
                     std::isalnum(static_cast<unsigned char>(words[at])))
                        ? FigureCellRoleIR::caption_content
                        : FigureCellRoleIR::caption_layout;
            }
            offset = end;
          }
        }
    }
    return true;
  }
  bool claim() {
    for (const auto &cell : ownership.cells) {
      if (!inside(cell.logical_record, cell.token_index))
        continue;
      const Key key{cell.logical_record, cell.token_index, cell.word_index};
      auto found = roles.find(key);
      const auto *record = by_record.at(cell.logical_record);
      if (found == roles.end()) {
        // Non-column spacing-prefix words and origin spaces are layout,
        // never a license to suppress a visible word.
        if (cell.word < 4 || blank(cell.word))
          roles[key] = FigureCellRoleIR::text_layout;
        else
          return fail(error,
                      "ordered figure leaves an unclassified source cell");
        found = roles.find(key);
      }
      FigureSourceCellIR claimed;
      claimed.logical_record = cell.logical_record;
      const auto owner = owners.find({cell.logical_record, cell.token_index});
      claimed.segment_index = owner == owners.end() ? 0 : owner->second;
      claimed.token_index = cell.token_index;
      claimed.word_index = cell.word_index;
      claimed.word = cell.word;
      claimed.token_bytes = record->ir.tokens[cell.token_index].byte_range;
      claimed.role = found->second;
      figure.cells.push_back(std::move(claimed));
    }
    for (const auto &row : rows) {
      const auto rs = physical_rows(row);
      figure.suppressed_rows.insert(figure.suppressed_rows.end(), rs.begin(),
                                    rs.end());
    }
    std::sort(figure.suppressed_rows.begin(), figure.suppressed_rows.end(),
              [](const auto &a, const auto &b) {
                return std::tie(a.display_run, a.row_index) <
                       std::tie(b.display_run, b.row_index);
              });
    figure.suppressed_rows.erase(
        std::unique(figure.suppressed_rows.begin(),
                    figure.suppressed_rows.end(),
                    [](const auto &a, const auto &b) {
                      return a.display_run == b.display_run &&
                             a.row_index == b.row_index;
                    }),
        figure.suppressed_rows.end());
    return !figure.cells.empty() ||
           fail(error, "ordered figure owns no source cells");
  }
};
} // namespace

bool extract_picture_figure_content_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const SelectorCatalogIR &selectors, const PictureFigureSourceIR &source,
    FigureSourceBlockIR &figure, std::string *error) {
  Reader reader{records, layout, ownership, selectors, source, figure, error,
                {},      {},     {},        {},        {},     false};
  return reader.prepare() && reader.emit() && reader.claim();
}

bool same_figure_content_ir(const std::vector<FigureContentPartIR> &a,
                            const std::vector<FigureContentPartIR> &b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].picture != b[i].picture ||
        a[i].picture_index != b[i].picture_index ||
        a[i].caption != b[i].caption || a[i].column != b[i].column ||
        a[i].continuation_column != b[i].continuation_column ||
        a[i].runs.size() != b[i].runs.size() ||
        a[i].rows.size() != b[i].rows.size())
      return false;
    for (std::size_t j = 0; j < a[i].rows.size(); ++j)
      if (a[i].rows[j].display_run != b[i].rows[j].display_run ||
          a[i].rows[j].row_index != b[i].rows[j].row_index)
        return false;
    for (std::size_t j = 0; j < a[i].runs.size(); ++j) {
      const auto &l = a[i].runs[j];
      const auto &r = b[i].runs[j];
      if (l.text != r.text || l.style != r.style || l.slices != r.slices ||
          l.link.has_value() != r.link.has_value())
        return false;
      if (l.link &&
          (l.link->target != r.link->target ||
           l.link->external != r.link->external ||
           l.link->selector.logical_record != r.link->selector.logical_record ||
           l.link->selector.segment_index != r.link->selector.segment_index ||
           l.link->selector.ordinal != r.link->selector.ordinal))
        return false;
    }
  }
  return true;
}
} // namespace geist::detail
