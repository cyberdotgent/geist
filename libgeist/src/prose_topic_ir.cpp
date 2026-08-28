#include "geist/detail/prose_topic_ir.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/implicit_grid.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/menu_ir.hpp"
#include "geist/detail/menu_topic_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

constexpr auto npos = static_cast<std::size_t>(-1);
constexpr std::uint16_t unmapped_word = 0xFFFF;
constexpr std::uint16_t bullet_glyph_word = 0x2666;

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

// ---------------------------------------------------------------------------
// Token classification
// ---------------------------------------------------------------------------

struct TokenView {
  std::size_t record = 0;  // index into records
  std::size_t token = 0;
  std::uint16_t prefix = 3;  // spacing prefix, 3 == default
  bool has_prefix = false;
  std::vector<std::uint16_t> body;  // words after the prefix
  std::uint8_t width = 0;
  std::uint16_t value = 0;  // encoded one-byte value when width == 1
};

// Row-control bytes observed in one-byte marker slots across the corpus:
// the sentinel/box runs 4..6, glyph slots 15..27, and the word-shaped slots
// 28..43 (SC31-711 `a`/`action`/`any`/`application`/`access`, ACPZMST1 `a`
// 42, N2AH1MST `access` 0x1c).  Hosted pages never display those words.  A
// one-byte word above this range before a lone origin run is a genuine word
// of an exactly full row or of an aligned column (QSYSNEWG FRONT_1 `400`
// 214, SC31-711 1.2 `C` 139); such a topic fails closed.
constexpr std::uint16_t row_control_byte_limit = 48;

TokenView view_token(const std::vector<DecodedLogicalRecordSource>& records,
                     std::size_t record, std::size_t token) {
  TokenView view;
  view.record = record;
  view.token = token;
  const auto& source = records[record].ir.tokens[token];
  view.width = source.encoded.width;
  view.value = source.encoded.value;
  const auto& words = source.decoded_words;
  std::size_t begin = 0;
  if (!words.empty() && words.front() < 4) {
    view.has_prefix = true;
    view.prefix = words.front();
    begin = 1;
  }
  view.body.assign(words.begin() + static_cast<std::ptrdiff_t>(begin),
                   words.end());
  return view;
}

bool is_bare(const TokenView& view) { return view.body.empty(); }
bool is_space_run(const TokenView& view) {
  return !view.body.empty() &&
         std::all_of(view.body.begin(), view.body.end(),
                     [](const auto word) { return word == ' '; });
}
// Box drawing, block elements and geometric shapes are decoder placeholders
// (`?`-runs, the 0x25BA row sentinel); the bullet glyph 0x2666 is separate.
bool box_word(std::uint16_t word) { return word >= 0x2500 && word <= 0x25FF; }
bool is_placeholder_run(const TokenView& view) {
  return !view.body.empty() &&
         std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
           return box_word(word) || word == unmapped_word;
         });
}
bool is_glyph(const TokenView& view) {
  return view.body.size() == 1 &&
         (view.body.front() == bullet_glyph_word ||
          view.body.front() == unmapped_word);
}
bool is_bullet_glyph(const TokenView& view) {
  return view.body.size() == 1 && view.body.front() == bullet_glyph_word;
}
// Visible: carries at least one displayable non-space word.
bool is_visible(const TokenView& view) {
  return !view.body.empty() && !is_space_run(view);
}
bool is_padding(const TokenView& view) {
  return is_bare(view) || is_space_run(view) || is_placeholder_run(view) ||
         is_bullet_glyph(view);
}

std::string word_text(std::uint16_t word) {
  if (word == unmapped_word) return "?";
  return token_words_to_ascii({word});
}

std::string body_text(const TokenView& view) {
  std::string text;
  for (const auto word : view.body) text += word_text(word);
  return text;
}

// ---------------------------------------------------------------------------
// Provenance helpers
// ---------------------------------------------------------------------------

std::size_t segment_of(const DecodedLogicalRecordSource& record,
                       std::size_t token) {
  for (const auto& segment : record.control_segments)
    if (std::binary_search(segment.source_tokens.begin(),
                           segment.source_tokens.end(), token))
      return segment.segment_index;
  return 0;
}

DocumentSourceSliceIR token_slice(const DecodedLogicalRecordSource& record,
                                  std::size_t begin, std::size_t end) {
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.segment_index = segment_of(record, begin);
  slice.token_begin = begin;
  slice.token_end = end;
  slice.byte_begin = record.ir.tokens[begin].byte_range.begin;
  slice.byte_end = record.ir.tokens[end - 1].byte_range.end;
  return slice;
}

// Compresses source-ordered token references into contiguous slices.
std::vector<DocumentSourceSliceIR> slices_for(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<std::pair<std::size_t, std::size_t>>& refs) {
  std::vector<DocumentSourceSliceIR> result;
  std::optional<std::pair<std::size_t, std::size_t>> open;  // record, begin
  std::size_t open_end = 0;
  for (const auto& [record, token] : refs) {
    if (open && open->first == record && token == open_end) {
      ++open_end;
      continue;
    }
    if (open && open->first == record && token < open_end) continue;
    if (open)
      result.push_back(token_slice(records[open->first], open->second, open_end));
    open = {record, token};
    open_end = token + 1;
  }
  if (open)
    result.push_back(token_slice(records[open->first], open->second, open_end));
  return result;
}

// ---------------------------------------------------------------------------
// Body stream
// ---------------------------------------------------------------------------

enum class ItemKind {
  token,
  font,
  select,
  anchor,
  segment_end,
};

struct Item {
  ItemKind kind = ItemKind::token;
  TokenView token;
  // An inter-segment token claimed by no control segment.
  bool separator = false;
  bool title_start = false;
  bool index_start = false;
  bool continuation_start = false;
  std::vector<FontSpanIR> spans;
  std::size_t column = 0;
  std::size_t length = 0;
  std::string target;
  std::string anchor_id;
  DocumentSourceSliceIR source;
};

struct Ledger {
  const std::vector<DecodedLogicalRecordSource>* records = nullptr;
  std::vector<ProseTokenDispositionIR> entries;
  std::map<std::pair<std::uint32_t, std::size_t>, std::size_t> index;

  explicit Ledger(const std::vector<DecodedLogicalRecordSource>& sources)
      : records(&sources) {
    for (const auto& record : sources)
      for (std::size_t token = 0; token < record.ir.tokens.size(); ++token) {
        index.emplace(std::make_pair(record.logical_record, token),
                      entries.size());
        entries.push_back({{record.logical_record, token},
                           ProseTokenRoleIR::unassigned, npos, npos});
      }
  }
  ProseTokenDispositionIR& at(std::size_t record, std::size_t token) {
    return entries[index.at({(*records)[record].logical_record, token})];
  }
  bool assign(std::size_t record, std::size_t token, ProseTokenRoleIR role,
              std::string* error) {
    auto& entry = at(record, token);
    if (entry.role != ProseTokenRoleIR::unassigned) {
      return fail(error, "token " + std::to_string(token) + " of record " +
                             std::to_string(entry.token.logical_record) +
                             " received two dispositions");
    }
    entry.role = role;
    return true;
  }
};

bool valid_anchor_id(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
         });
}

bool reserved_structural(const std::string& lower_opcode) {
  for (const char* reserved :
       {"srfig", "srefig", "srtbl", "sretbl", "srftn", "sreftn", "srmsg",
        "srlist", "srelist", "srlblbox", "srelblbox"})
    if (lower_opcode.rfind(reserved, 0) == 0) return true;
  return false;
}

std::string operand_text(const DecodedLogicalRecordSource& record,
                         const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

// Tokens whose decoded output intersects the opcode/operand ranges of a
// segment; the remaining segment tokens form its payload.
std::vector<std::size_t> operand_tokens(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment) {
  const auto words =
      decoded_byte_range_to_word_range(record.assembled,
                                       {segment.opcode_range.begin,
                                        segment.operand_range.end});
  return source_tokens_intersecting_output(record.assembled, words.begin,
                                           words.end);
}

// Decoder separators (`,` / `?`) can precede a control word inside its own
// segment: SC31-711 record 37 reads `, sh2.2.1`.  Such tokens lie wholly
// before the opcode range.
std::vector<std::size_t> leading_separator_tokens(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment) {
  if (segment.opcode_range.begin <= segment.complete.begin) return {};
  const auto words = decoded_byte_range_to_word_range(
      record.assembled, {segment.complete.begin, segment.opcode_range.begin});
  auto tokens = source_tokens_intersecting_output(record.assembled, words.begin,
                                                  words.end);
  const auto opcode_words = decoded_byte_range_to_word_range(
      record.assembled, {segment.opcode_range.begin, segment.opcode_range.end});
  const auto opcode_tokens = source_tokens_intersecting_output(
      record.assembled, opcode_words.begin, opcode_words.end);
  tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                              [&](const auto token) {
                                return std::binary_search(opcode_tokens.begin(),
                                                          opcode_tokens.end(),
                                                          token);
                              }),
               tokens.end());
  return tokens;
}

bool punctuation_glyph_token(const TokenView& view) {
  return !view.body.empty() &&
         std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
           return word < 0x80 && std::ispunct(static_cast<int>(word)) != 0;
         });
}

bool is_separator(const TokenView& view) {
  return !view.body.empty() &&
         std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
           return word == ',' || word == '?' || word == ' ' ||
                  word == unmapped_word;
         });
}

bool parse_selector_operand(const std::string& text, std::size_t& column,
                            std::size_t& length, std::string& target) {
  std::istringstream in(text);
  std::string col, len;
  if (!(in >> col >> len >> target)) return false;
  std::string extra;
  if (in >> extra) return false;
  const auto decimal = [](const std::string& value, std::size_t& out) {
    if (value.empty() || value.size() > 6) return false;
    out = 0;
    for (const auto ch : value) {
      if (std::isdigit(static_cast<unsigned char>(ch)) == 0) return false;
      out = out * 10 + static_cast<std::size_t>(ch - '0');
    }
    return true;
  };
  return decimal(col, column) && decimal(len, length) &&
         valid_anchor_id(target);
}

struct Envelope {
  std::string heading_level;
  std::size_t body_segment_begin = 0;  // first non-envelope segment of record 0
  std::vector<ProseAnchorIR> leading_anchors;
  bool glued_title = false;
  std::vector<std::size_t> glued_title_tokens;
};

struct StreamBuild {
  std::vector<Item> items;
  std::vector<ProseAnchorIR> leading_anchors;
  std::vector<ProseAnchorIR> trailing_anchors;
  std::vector<ProseIndexTermIR> trailing_index_terms;
  std::size_t menu_record = npos;  // first record holding menu controls
  std::size_t menu_segment = npos;
};

bool assign_segment_tokens(const std::vector<DecodedLogicalRecordSource>& records,
                           Ledger& ledger, std::size_t record_index,
                           const ControlSegmentIR& segment,
                           ProseTokenRoleIR control_role, bool allow_payload,
                           std::string* error) {
  const auto& record = records[record_index];
  const auto operands = operand_tokens(record, segment);
  const auto leading = leading_separator_tokens(record, segment);
  for (const auto token : segment.source_tokens) {
    const auto is_operand = std::binary_search(operands.begin(), operands.end(),
                                               token);
    const auto view = view_token(records, record_index, token);
    if (is_operand) {
      if (!ledger.assign(record_index, token, control_role, error))
        return false;
      continue;
    }
    if (is_padding(view) ||
        (std::binary_search(leading.begin(), leading.end(), token) &&
         is_separator(view))) {
      if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding, error))
        return false;
      continue;
    }
    if (!allow_payload)
      return fail(error, "control " + segment.opcode +
                             " carries visible payload '" + body_text(view) +
                             "' in record " +
                             std::to_string(record.logical_record));
    return fail(error, "internal: payload token reached assign_segment_tokens");
  }
  return true;
}

bool parse_envelope(const std::vector<DecodedLogicalRecordSource>& records,
                    Ledger& ledger, Envelope& envelope, std::string* error) {
  const auto& record = records.front();
  const std::vector<BookControlKind> required = {
      BookControlKind::topic_start, BookControlKind::topic_number,
      BookControlKind::parent,      BookControlKind::forward_level,
      BookControlKind::back_level,  BookControlKind::summary,
      BookControlKind::heading_level, BookControlKind::source_file};
  if (record.control_segments.size() < required.size() + 1)
    return fail(error, "first record lacks the topic metadata envelope");
  std::size_t segment_cursor = 0;
  std::size_t heading_segment = 0;
  for (std::size_t index = 0; index < required.size(); ++index) {
    if (segment_cursor >= record.control_segments.size())
      return fail(error, "topic metadata controls are incomplete");
    // A message-section heading topic carries one bare `SRMSG` inside its
    // metadata (SC31-711 record 127, SC09-138 record 2066); the control has
    // no operand and no payload.
    if (record.control_segments[segment_cursor].kind ==
            BookControlKind::message_start &&
        segment_cursor + 1 < record.control_segments.size()) {
      const auto& message = record.control_segments[segment_cursor];
      if (message.payload_range.begin != message.payload_range.end ||
          message.operand_range.begin != message.operand_range.end)
        return fail(error, "SRMSG inside the envelope carries operands");
      if (!assign_segment_tokens(records, ledger, 0, message,
                                 ProseTokenRoleIR::envelope, false, error))
        return false;
      ++segment_cursor;
    }
    const auto& segment = record.control_segments[segment_cursor];
    if (segment.kind != required[index])
      return fail(error, "topic metadata controls are incomplete or out of order");
    if (segment.kind == BookControlKind::heading_level)
      heading_segment = segment_cursor;
    ++segment_cursor;
    if (segment.malformed && segment.kind != BookControlKind::forward_level &&
        segment.kind != BookControlKind::back_level &&
        segment.kind != BookControlKind::parent)
      return fail(error, "topic metadata control is malformed");
    if (segment.kind == BookControlKind::source_file) {
      // `csourcefn <name> ? ST? <title>`: the title control can be glued
      // into the source-file segment (ACPZMST1 record 78).
      const auto operands = operand_tokens(record, segment);
      bool st_seen = false;
      for (const auto token : segment.source_tokens) {
        const auto view = view_token(records, 0, token);
        if (std::binary_search(operands.begin(), operands.end(), token)) {
          if (!ledger.assign(0, token, ProseTokenRoleIR::envelope, error))
            return false;
          continue;
        }
        if (st_seen) {
          envelope.glued_title_tokens.push_back(token);
          continue;
        }
        if (is_padding(view) || is_separator(view) ||
            (view.width == 1 && punctuation_glyph_token(view))) {
          if (!ledger.assign(0, token, ProseTokenRoleIR::padding, error))
            return false;
          continue;
        }
        if (ascii_lower(body_text(view)) == "st") {
          st_seen = true;
          if (!ledger.assign(0, token, ProseTokenRoleIR::envelope, error))
            return false;
          continue;
        }
        return fail(error, "control csourcefn carries visible payload '" +
                               body_text(view) + "'");
      }
      envelope.glued_title = st_seen;
      continue;
    }
    if (!assign_segment_tokens(records, ledger, 0, segment,
                               ProseTokenRoleIR::envelope, false, error))
      return false;
  }
  auto heading_level = ascii_lower(trim_ascii(operand_text(
      record, record.control_segments[heading_segment].operand_range)));
  if (!heading_level.empty() && heading_level.front() == ':')
    heading_level.erase(heading_level.begin());
  if (heading_level.size() != 2 || heading_level.front() != 'h' ||
      heading_level.back() < '1' || heading_level.back() > '6')
    return fail(error, "heading level '" + heading_level +
                           "' is not an h1-h6 prose heading");
  envelope.heading_level = heading_level;
  envelope.body_segment_begin = segment_cursor;
  return true;
}

// Collects the body stream from the first record's post-envelope segments and
// every later record.  Control tokens receive their ledger roles here; payload
// tokens are emitted as items for the display-line pass.
bool collect_stream(const std::vector<DecodedLogicalRecordSource>& records,
                    const Envelope& envelope, Ledger& ledger,
                    StreamBuild& build, std::string* error) {
  bool title_seen = false;
  bool menu_open = false;
  if (envelope.glued_title) {
    title_seen = true;
    bool first_payload = true;
    for (const auto token : envelope.glued_title_tokens) {
      Item item;
      item.kind = ItemKind::token;
      item.token = view_token(records, 0, token);
      item.title_start = first_payload;
      first_payload = false;
      build.items.push_back(std::move(item));
    }
    Item end;
    end.kind = ItemKind::segment_end;
    build.items.push_back(std::move(end));
  }
  for (std::size_t record_index = 0; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    const auto first_segment =
        record_index == 0 ? envelope.body_segment_begin : std::size_t{0};
    std::vector<bool> claimed(record.ir.tokens.size(), false);
    if (record_index == 0)
      for (const auto token : envelope.glued_title_tokens)
        if (token < claimed.size()) claimed[token] = true;
    // Tokens claimed by no segment are inter-control separators.  Inside the
    // body they carry layout state (a bare spacing token before a control is
    // the paragraph break, a placeholder run the row slot), so they enter the
    // stream in source order; before the title they are envelope padding.
    std::size_t cursor = 0;
    const auto emit_unclaimed = [&](std::size_t end) -> bool {
      for (; cursor < end && cursor < record.ir.tokens.size(); ++cursor) {
        const auto token = cursor;
        if (claimed[token] ||
            ledger.at(record_index, token).role != ProseTokenRoleIR::unassigned)
          continue;
        const auto view = view_token(records, record_index, token);
        if (title_seen && !menu_open &&
            (is_bare(view) || is_space_run(view) || is_placeholder_run(view))) {
          Item item;
          item.kind = ItemKind::token;
          item.token = view;
          item.separator = true;
          build.items.push_back(std::move(item));
          claimed[token] = true;
          continue;
        }
        if (!is_padding(view) && !is_separator(view))
          return fail(error, "unclaimed visible token '" + body_text(view) +
                                 "' in record " +
                                 std::to_string(record.logical_record));
        if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                           error))
          return false;
      }
      return true;
    };
    for (std::size_t segment_index = first_segment;
         segment_index < record.control_segments.size(); ++segment_index) {
      const auto& segment = record.control_segments[segment_index];
      if (!segment.source_tokens.empty()) {
        if (!emit_unclaimed(segment.source_tokens.front())) return false;
        cursor = std::max(cursor, segment.source_tokens.back() + 1);
      }
      for (const auto token : segment.source_tokens)
        if (token < claimed.size()) claimed[token] = true;
      const auto lower_opcode = ascii_lower(segment.opcode);
      const auto push_payload = [&](ProseTokenRoleIR control_role,
                                    bool title_payload, bool index_payload,
                                    bool continuation) -> bool {
        const auto operands = operand_tokens(record, segment);
        bool first_payload = true;
        for (const auto token : segment.source_tokens) {
          if (std::binary_search(operands.begin(), operands.end(), token)) {
            if (!ledger.assign(record_index, token, control_role, error))
              return false;
            continue;
          }
          Item item;
          item.kind = ItemKind::token;
          item.token = view_token(records, record_index, token);
          if (first_payload) {
            item.title_start = title_payload;
            item.index_start = index_payload;
            item.continuation_start = continuation;
            first_payload = false;
          }
          build.items.push_back(std::move(item));
        }
        Item end;
        end.kind = ItemKind::segment_end;
        build.items.push_back(std::move(end));
        return true;
      };

      if (menu_open) {
        if (segment.kind == BookControlKind::menu_item ||
            segment.kind == BookControlKind::menu_end ||
            segment.kind == BookControlKind::menu_start)
          continue;
        if (segment.kind == BookControlKind::text) {
          // Subject-index terms after the menu (FA1PLMM0 record 42): the
          // keyword, its term words and padding only; a row break followed
          // by visible text would be body content and rejects.
          ProseIndexTermIR term;
          std::vector<std::pair<std::size_t, std::size_t>> refs;
          bool keyword_seen = false;
          bool break_seen = false;
          for (const auto token : segment.source_tokens) {
            const auto view = view_token(records, record_index, token);
            if (!keyword_seen) {
              if (is_padding(view) || is_separator(view)) {
                if (!ledger.assign(record_index, token,
                                   ProseTokenRoleIR::padding, error))
                  return false;
                continue;
              }
              if (ascii_lower(body_text(view)) != "si")
                return fail(error, "content follows the trailing menu");
              keyword_seen = true;
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::index_keyword, error))
                return false;
              continue;
            }
            if (is_space_run(view) && view.body.size() >= 3) break_seen = true;
            if (is_bare(view) || is_space_run(view) ||
                is_placeholder_run(view) || is_bullet_glyph(view)) {
              if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                                 error))
                return false;
              continue;
            }
            if (break_seen)
              return fail(error, "content follows the trailing menu");
            if (!term.term.empty()) term.term.push_back(' ');
            term.term += body_text(view);
            refs.emplace_back(record_index, token);
            if (!ledger.assign(record_index, token,
                               ProseTokenRoleIR::index_term, error))
              return false;
          }
          if (!keyword_seen) continue;
          term.term = collapse_ascii_whitespace(term.term);
          term.slices = slices_for(records, refs);
          build.trailing_index_terms.push_back(std::move(term));
          continue;
        }
        if (segment.kind == BookControlKind::structural && !segment.malformed &&
            lower_opcode.rfind("sr", 0) == 0 &&
            !reserved_structural(lower_opcode) &&
            valid_anchor_id(segment.opcode.substr(2))) {
          if (!assign_segment_tokens(records, ledger, record_index, segment,
                                     ProseTokenRoleIR::control, false, error))
            return false;
          const auto operands = operand_tokens(record, segment);
          ProseAnchorIR anchor;
          anchor.id = segment.opcode.substr(2);
          anchor.source = token_slice(record, operands.front(),
                                      operands.back() + 1);
          anchor.after_menu = true;
          build.trailing_anchors.push_back(std::move(anchor));
          continue;
        }
        if (segment.kind == BookControlKind::text &&
            std::all_of(segment.source_tokens.begin(),
                        segment.source_tokens.end(), [&](const auto token) {
                          return is_padding(
                              view_token(records, record_index, token));
                        }))
          continue;
        return fail(error, "content follows the trailing menu");
      }

      switch (segment.kind) {
      case BookControlKind::title: {
        if (title_seen)
          return fail(error, "topic carries a second ST control");
        if (segment.malformed) return fail(error, "ST control is malformed");
        title_seen = true;
        if (!push_payload(ProseTokenRoleIR::envelope, true, false, false))
          return false;
        break;
      }
      case BookControlKind::structural: {
        if (segment.malformed || lower_opcode.rfind("sr", 0) != 0 ||
            reserved_structural(lower_opcode))
          return fail(error, "structural control " + segment.opcode +
                                 " is not a bare anchor");
        const auto id = segment.opcode.substr(2);
        if (!valid_anchor_id(id))
          return fail(error, "anchor id '" + id + "' is invalid");
        if (!assign_segment_tokens(records, ledger, record_index, segment,
                                   ProseTokenRoleIR::control, false, error))
          return false;
        const auto operands = operand_tokens(record, segment);
        if (operands.empty())
          return fail(error, "anchor control has no source token");
        ProseAnchorIR anchor;
        anchor.id = id;
        anchor.source = token_slice(record, operands.front(),
                                    operands.back() + 1);
        if (!title_seen) {
          build.leading_anchors.push_back(std::move(anchor));
        } else {
          Item item;
          item.kind = ItemKind::anchor;
          item.anchor_id = anchor.id;
          item.source = anchor.source;
          build.items.push_back(std::move(item));
        }
        break;
      }
      case BookControlKind::text: {
        std::optional<std::size_t> first_visible;
        for (const auto token : segment.source_tokens) {
          if (is_visible(view_token(records, record_index, token))) {
            first_visible = token;
            break;
          }
        }
        if (!title_seen) {
          // `ST| <title>`: the title control glued to a one-cell marker is
          // split as a text segment (GC23-046 record 151, QSYSNEWG PREFACE).
          if (!first_visible ||
              ascii_lower(body_text(view_token(records, record_index,
                                               *first_visible))) != "st")
            return fail(error, "visible text precedes the ST title");
          title_seen = true;
          bool first_payload = true;
          for (const auto token : segment.source_tokens) {
            if (token <= *first_visible) {
              if (!ledger.assign(record_index, token,
                                 token == *first_visible
                                     ? ProseTokenRoleIR::envelope
                                     : ProseTokenRoleIR::padding,
                                 error))
                return false;
              continue;
            }
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            item.title_start = first_payload;
            first_payload = false;
            build.items.push_back(std::move(item));
          }
          Item end;
          end.kind = ItemKind::segment_end;
          build.items.push_back(std::move(end));
          break;
        }
        if (!first_visible) {
          for (const auto token : segment.source_tokens)
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                               error))
              return false;
          break;
        }
        const auto first_view = view_token(records, record_index, *first_visible);
        const auto first_text = ascii_lower(body_text(first_view));
        if (first_text == "c.cp") {
          // `c.cp <n>` carries pagination state only (markup.cpp keeps the
          // same reading); its trailing spacing tokens stay in the stream
          // because they can carry the paragraph break.
          bool operand_seen = false;
          for (const auto token : segment.source_tokens) {
            const auto view = view_token(records, record_index, token);
            if (token <= *first_visible) {
              if (!ledger.assign(record_index, token,
                                 token == *first_visible
                                     ? ProseTokenRoleIR::control
                                     : ProseTokenRoleIR::padding,
                                 error))
                return false;
              continue;
            }
            if (!operand_seen) {
              if (is_padding(view)) {
                if (!ledger.assign(record_index, token,
                                   ProseTokenRoleIR::padding, error))
                  return false;
                continue;
              }
              const auto text = body_text(view);
              if (text.empty() ||
                  !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
                    return std::isdigit(ch) != 0;
                  }))
                return fail(error, "c.cp control carries visible payload '" +
                                       text + "'");
              operand_seen = true;
              if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                                 error))
                return false;
              continue;
            }
            if (!(is_bare(view) || is_space_run(view) ||
                  is_placeholder_run(view)))
              return fail(error, "c.cp control carries visible payload '" +
                                     body_text(view) + "'");
            Item item;
            item.kind = ItemKind::token;
            item.token = view;
            item.separator = true;
            build.items.push_back(std::move(item));
          }
          if (!operand_seen) return fail(error, "c.cp control has no operand");
          break;
        }
        const auto continuation =
            record_index != 0 && segment_index == 0 && first_text != "si";
        if (!continuation && first_text != "si")
          return fail(error, "text segment begins with control-like word '" +
                                 body_text(first_view) + "' in record " +
                                 std::to_string(record.logical_record));
        if (!push_payload(ProseTokenRoleIR::control, false, !continuation,
                          continuation))
          return false;
        break;
      }
      case BookControlKind::font: {
        if (!title_seen) return fail(error, "font control precedes the title");
        std::string font_error;
        auto spans = decode_font_control_spans(record, segment, &font_error);
        if (!spans)
          return fail(error, "font control rejected: " + font_error);
        for (const auto& span : spans->spans)
          if (span.style == FontStyleIR::unknown)
            return fail(error, "font style code '" + span.code +
                                   "' is not a highlight phrase");
        Item item;
        item.kind = ItemKind::font;
        item.spans = std::move(spans->spans);
        item.source = spans->operand_source;
        build.items.push_back(std::move(item));
        if (!push_payload(ProseTokenRoleIR::control, false, false, false))
          return false;
        break;
      }
      case BookControlKind::select: {
        if (!title_seen)
          return fail(error, "selector control precedes the title");
        if (segment.malformed) return fail(error, "selector is malformed");
        Item item;
        item.kind = ItemKind::select;
        if (!parse_selector_operand(operand_text(record, segment.operand_range),
                                    item.column, item.length, item.target))
          return fail(error, "selector operands are not canonical");
        const auto upper = ascii_lower(item.target);
        if (upper.rfind("pic", 0) == 0 || upper == "lnk")
          return fail(error, "selector targets a picture or external link");
        const auto operands = operand_tokens(record, segment);
        if (operands.empty()) return fail(error, "selector has no source token");
        item.source = token_slice(record, operands.front(), operands.back() + 1);
        build.items.push_back(std::move(item));
        if (!push_payload(ProseTokenRoleIR::control, false, false, false))
          return false;
        break;
      }
      case BookControlKind::menu_start: {
        if (!title_seen) return fail(error, "menu precedes the title");
        menu_open = true;
        build.menu_record = record_index;
        build.menu_segment = segment_index;
        break;
      }
      default:
        return fail(error, "body control " +
                               (segment.opcode.empty() ? std::string("<text>")
                                                       : segment.opcode) +
                               " is outside the prose model");
      }
    }
    if (menu_open) {
      // Everything from the menu start onwards belongs to the menu.
      continue;
    }
    if (!emit_unclaimed(record.ir.tokens.size())) return false;
  }
  if (!title_seen) return fail(error, "topic has no ST title");
  return true;
}

// ---------------------------------------------------------------------------
// Display lines
// ---------------------------------------------------------------------------

struct Cell {
  std::size_t record = npos;  // npos == synthetic inter-token space
  std::size_t token = 0;
  std::string text;
  bool space = false;
};

struct Span {
  std::size_t begin = 0;
  std::size_t end = 0;
  FontStyleIR style = FontStyleIR::unknown;
  std::string target;  // non-empty == cross-reference span
};

struct Line {
  std::size_t origin = 0;
  std::size_t breaks_before = 0;
  bool anchor_before = false;
  std::size_t anchor_index = npos;
  bool bullet = false;
  std::size_t text_begin = 0;  // first cell after origin/bullet/gap
  std::vector<Cell> cells;
  std::vector<Span> fonts;
  std::vector<Span> links;
};

struct LineBuild {
  std::vector<Line> lines;
  std::vector<ProseAnchorIR> body_anchors;
  std::vector<ProseIndexTermIR> index_terms;
  std::string title;
  std::vector<std::pair<std::size_t, std::size_t>> title_refs;
};

struct LineBuilder {
  LineBuilder(const std::vector<DecodedLogicalRecordSource>& sources,
              const std::vector<Item>& stream, Ledger& owner, LineBuild& build,
              std::string* message)
      : records(sources), items(stream), ledger(owner), out(build),
        error(message) {}

  const std::vector<DecodedLogicalRecordSource>& records;
  const std::vector<Item>& items;
  Ledger& ledger;
  LineBuild& out;
  std::string* error;

  bool in_title = false;
  bool title_done = false;
  bool in_index = false;
  ProseIndexTermIR current_term;
  std::vector<std::pair<std::size_t, std::size_t>> term_refs;
  bool pending_space = false;
  std::size_t trailing_bare = 0;
  bool anchor_pending = false;
  std::size_t pending_anchor_index = npos;
  std::vector<std::size_t> pending_controls;
  bool line_open = false;
  std::size_t line_visible_cells = 0;

  Line& line() { return out.lines.back(); }

  bool is_token(std::size_t index) const {
    return index < items.size() && items[index].kind == ItemKind::token;
  }
  // Next token item after `index`, skipping controls, segment ends and bare
  // spacing tokens.
  std::size_t next_token(std::size_t index) const {
    for (auto cursor = index + 1; cursor < items.size(); ++cursor) {
      if (items[cursor].kind != ItemKind::token) continue;
      if (is_bare(items[cursor].token)) continue;
      return cursor;
    }
    return npos;
  }
  bool space_at(std::size_t index) const {
    return index != npos && is_token(index) && is_space_run(items[index].token);
  }
  bool visible_at(std::size_t index) const {
    return index != npos && is_token(index) && is_visible(items[index].token);
  }
  std::size_t run_length(std::size_t index) const {
    return items[index].token.body.size();
  }
  bool assign(const TokenView& view, ProseTokenRoleIR role) {
    return ledger.assign(view.record, view.token, role, error);
  }

  void open_line(std::size_t origin_cells, const TokenView* origin) {
    Line fresh;
    fresh.origin = origin_cells;
    fresh.breaks_before = trailing_bare;
    trailing_bare = 0;
    if (anchor_pending) {
      fresh.anchor_before = true;
      fresh.anchor_index = pending_anchor_index;
      anchor_pending = false;
    }
    if (origin != nullptr) {
      for (std::size_t word = 0; word < origin->body.size(); ++word)
        fresh.cells.push_back({origin->record, origin->token, " ", true});
      implied_origin = origin->body.size();
    }
    fresh.text_begin = fresh.cells.size();
    out.lines.push_back(std::move(fresh));
    line_open = true;
    line_visible_cells = 0;
    pending_space = false;
  }

  bool ensure_line() {
    if (!line_open) open_line(0, nullptr);
    return true;
  }

  // Finishes the ST title at the first structural boundary.
  bool finish_title() {
    if (!in_title) return true;
    in_title = false;
    title_done = true;
    out.title = collapse_ascii_whitespace(out.title);
    if (out.title.empty()) return fail(error, "ST title is empty");
    return true;
  }

  bool finish_index() {
    if (!in_index) return true;
    in_index = false;
    current_term.term = collapse_ascii_whitespace(current_term.term);
    current_term.slices = slices_for(records, term_refs);
    if (current_term.term.empty())
      return fail(error, "SI control has an empty index term");
    out.index_terms.push_back(std::move(current_term));
    current_term = {};
    term_refs.clear();
    return true;
  }

  void append_space_cells(const TokenView& view, bool literal_gap) {
    (void)literal_gap;
    for (std::size_t word = 0; word < view.body.size(); ++word)
      line().cells.push_back({view.record, view.token, " ", true});
  }

  bool append_visible(const TokenView& view, ProseTokenRoleIR role) {
    if (!ensure_line()) return false;
    if (pending_space && view.prefix != 0 && view.prefix != 1)
      line().cells.push_back({npos, 0, " ", true});
    for (const auto word : view.body)
      line().cells.push_back({view.record, view.token, word_text(word),
                              word == ' '});
    ++line_visible_cells;
    last_visible = body_text(view);
    pending_space = view.prefix != 2;
    if (!assign(view, role)) return false;
    if (role == ProseTokenRoleIR::text && !pending_controls.empty()) {
      for (const auto control : pending_controls) {
        const auto& item = items[control];
        if (item.kind == ItemKind::font) {
          for (const auto& span : item.spans)
            line().fonts.push_back(
                {span.column, span.column + span.length, span.style, {}});
        } else {
          line().links.push_back({item.column, item.column + item.length,
                                  FontStyleIR::unknown, item.target});
        }
      }
      pending_controls.clear();
    }
    return true;
  }

  // Classifies the token at `index` as a one-byte marker slot when it is
  // followed by exactly one origin run of three or more cells and then the
  // next line's first visible token.  A width-one word followed by a fill
  // run and another marker is text (the row's last word).
  // A standalone layout glyph (`(`, `)`, `-`, `<`, `>`, `/`, `=`, `"`...)
  // before any line break is a marker in both shapes; attached punctuation
  // (`conditions:`, `useful.`) and words are visible when a fill run
  // separates them from the origin.
  static bool ballot_token(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
                                        [](char ch) { return ch == '_'; });
  }

  static bool alnum_word(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::isalnum(static_cast<int>(word)) != 0;
           });
  }

  std::string last_visible;
  // Origin of the most recent row opened by an explicit origin run; an
  // implied row break (word + lone run + word) reuses it.
  std::size_t implied_origin = 3;

  std::string where(const TokenView& view) const {
    return "record " +
           std::to_string(records[view.record].logical_record) + " token " +
           std::to_string(view.token);
  }

  static bool punctuation_glyph(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::ispunct(static_cast<int>(word)) != 0;
           });
  }

  bool marker_at(std::size_t index, std::size_t& origin_index) const {
    const auto& view = items[index].token;
    if (view.width != 1 || !is_visible(view)) return false;
    const auto space = next_token(index);
    if (!space_at(space)) return false;
    if (space_at(next_token(space))) {
      // Fill/origin pair: a standalone glyph is a marker here, and so is a
      // one-byte alphanumeric piece glued (no space) onto a preceding word:
      // genuine text never joins two alphanumeric pieces without
      // punctuation (FA1PLMM0 record 1133 `Messages` + `access`).
      const auto attached =
          !pending_space || view.prefix == 0 || view.prefix == 1;
      const auto glued_word =
          attached && alnum_word(view) && !last_visible.empty() &&
          std::isalnum(static_cast<unsigned char>(last_visible.back())) != 0;
      if (!(punctuation_glyph(view) && !attached) && !glued_word)
        return false;
      auto last = next_token(space);
      while (space_at(next_token(last))) last = next_token(last);
      origin_index = last;
      return true;
    }
    const auto after = next_token(space);
    if (space_at(after) || !visible_at(after)) return false;
    // `( sp1 │ text` / `a sp1 │ text` (ACPZMST1 record 35 tokens 134 and
    // 163): the one-byte slot before a one-cell origin and the row's visual
    // marker glyph; the glyph proves the row start.
    const auto glyph_before_slot =
        run_length(space) < 3 && pending_space &&
        (punctuation_glyph(view) || view.value < row_control_byte_limit) &&
        is_placeholder_run(items[after].token) &&
        items[after].token.width == 1;
    if (run_length(space) < 3 && !glyph_before_slot) return false;
    if (run_length(space) >= 3 && alnum_word(view) &&
        view.value >= row_control_byte_limit)
      return false;
    // Exception: the following token is itself a marker candidate.
    const auto& following = items[after].token;
    if (following.width == 1 && !is_bullet_glyph(following)) {
      const auto space2 = next_token(after);
      if (space_at(space2) && run_length(space2) >= 3) {
        const auto after2 = next_token(space2);
        if (visible_at(after2) && !space_at(after2)) return false;
      }
    }
    origin_index = space;
    return true;
  }

  bool run() {
    for (std::size_t index = 0; index < items.size(); ++index) {
      const auto& item = items[index];
      switch (item.kind) {
      case ItemKind::segment_end:
        if (!finish_title() || !finish_index()) return false;
        break;
      case ItemKind::font:
      case ItemKind::select:
        if (in_title) return fail(error, "font/selector inside the ST title");
        pending_controls.push_back(index);
        break;
      case ItemKind::anchor: {
        if (!finish_title() || !finish_index()) return false;
        ProseAnchorIR anchor;
        anchor.id = item.anchor_id;
        anchor.source = item.source;
        out.body_anchors.push_back(std::move(anchor));
        anchor_pending = true;
        pending_anchor_index = out.body_anchors.size() - 1;
        line_open = false;
        break;
      }
      case ItemKind::token:
        if (!token(index)) return false;
        break;
      }
    }
    if (!finish_title() || !finish_index()) return false;
    if (!pending_controls.empty())
      return fail(error, "font/selector control has no display text");
    if (anchor_pending) {
      // Trailing anchor: precedes no line; keep as terminal anchor.
      anchor_pending = false;
    }
    if (!title_done) return fail(error, "ST title was never completed");
    return true;
  }

  bool token(std::size_t index) {
    const auto& item = items[index];
    const auto& view = item.token;
    if (skip_until != npos) {
      if (index <= skip_until) {
        if (is_bare(view)) {
          pending_space = false;
          ++trailing_bare;
          return assign(view, ProseTokenRoleIR::spacing);
        }
        return true;
      }
      skip_until = npos;
    }
    if (item.title_start) {
      in_title = true;
      out.title.clear();
    }
    if (item.index_start) {
      if (in_index) return fail(error, "nested SI control");
      in_index = true;
      current_term = {};
      term_refs.clear();
      const auto keyword = body_text(view);
      if (ascii_lower(keyword) != "si")
        return fail(error, "SI keyword mismatch");
      return assign(view, ProseTokenRoleIR::index_keyword);
    }

    if (is_bare(view)) {
      pending_space = false;
      ++trailing_bare;
      return assign(view, ProseTokenRoleIR::spacing);
    }

    if (is_space_run(view)) {
      const auto next = next_token(index);
      if (space_at(next)) {
        // Fill/origin pair: every run but the last is fill.
        auto cursor = index;
        auto last = next;
        while (space_at(next_token(last))) last = next_token(last);
        while (cursor != last) {
          if (!assign(items[cursor].token, ProseTokenRoleIR::fill)) return false;
          cursor = next_token(cursor);
        }
        const auto& origin = items[last].token;
        if (!assign(origin, ProseTokenRoleIR::origin)) return false;
        if (!finish_title() || !finish_index()) return false;
        open_line(origin.body.size(), &origin);
        // Skip the consumed runs (bare tokens between were handled as they
        // came; controls stay in sequence).
        skip_until = last;
        return true;
      }
      // Lone run: literal in-line gap (or trailing fill before a control).
      if (in_title) {
        out.title.push_back(' ');
        return assign(view, ProseTokenRoleIR::gap);
      }
      if (in_index) {
        current_term.term.push_back(' ');
        return assign(view, ProseTokenRoleIR::gap);
      }
      if (!line_open) {
        // Padding before the first line of the body: fill.
        return assign(view, ProseTokenRoleIR::fill);
      }
      // A lone run of three or more cells after a word and before the next
      // word is a row break without a marker byte: the word ends its row
      // and the next row keeps the block indent (SC31-711 LR57 `check` + 10
      // spaces + `the following` renders at indent 3; SC24-5520-00 LR51
      // `are` + 3 spaces + `discussed`).  The gap after a ballot token
      // (`__`) is display spacing inside the row.
      if (view.body.size() >= 3 && pending_space && line_visible_cells != 0 &&
          index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
          visible_at(next_token(index)) &&
          !is_placeholder_run(items[next_token(index)].token) &&
          !ballot_token(last_visible)) {
        if (!assign(view, ProseTokenRoleIR::fill)) return false;
        if (!finish_title() || !finish_index()) return false;
        open_line(implied_origin, nullptr);
        for (std::size_t cell = 0; cell < implied_origin; ++cell)
          line().cells.push_back({npos, 0, " ", true});
        line().text_begin = line().cells.size();
        return true;
      }
      if (pending_space) {
        line().cells.push_back({npos, 0, " ", true});
        pending_space = false;
      }
      append_space_cells(view, true);
      return assign(view, ProseTokenRoleIR::gap);
    }
    // Visible token.
    std::size_t origin_index = npos;
    const auto line_start_marker =
        line_open && line_visible_cells == 0 && view.width == 1 &&
        index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
        is_visible(items[index + 1].token) &&
        !is_placeholder_run(items[index + 1].token);
    if (is_placeholder_run(view) && !line_start_marker &&
        !marker_at(index, origin_index)) {
      // A placeholder run that does not open a row is layout padding: either
      // trailing before a control/segment end, the slot of a fill/origin
      // pair (whose runs are classified when they are reached), or the
      // marker glued between `ST` and the title (ACPZMST1 record 78).
      const auto next = next_token(index);
      // An inter-segment placeholder stands at a control boundary as well.
      const auto before_control =
          index + 1 >= items.size() ||
          items[index + 1].kind != ItemKind::token || item.separator;
      const auto title_marker = in_title && out.title.empty();
      if (next != npos && !space_at(next) && !before_control && !title_marker)
        return fail(error, "placeholder run '" + body_text(view) +
                               "' is followed by visible text at " +
                               where(view));
      if (title_marker) return assign(view, ProseTokenRoleIR::marker);
      if (in_title && !finish_title()) return false;
      if (in_index && !finish_index()) return false;
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      // The slot ends the current row even when its origin run is absent
      // (GC23-046 record 151: `for {9472}... [cfont] | the CIDTABL`).
      line_open = false;
      // A placeholder slot followed by one space run of any width opens a
      // row: GC23-046 record 151 `{9524} sp1 | ◆ The total number`.
      if (space_at(next) && !space_at(next_token(next)) &&
          visible_at(next_token(next))) {
        const auto& origin = items[next].token;
        if (!assign(origin, ProseTokenRoleIR::origin)) return false;
        open_line(origin.body.size(), &origin);
        skip_until = next;
      }
      return true;
    }
    if (marker_at(index, origin_index)) {
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      for (auto cursor = next_token(index); cursor != origin_index;
           cursor = next_token(cursor))
        if (!assign(items[cursor].token, ProseTokenRoleIR::fill)) return false;
      const auto& origin = items[origin_index].token;
      if (!assign(origin, ProseTokenRoleIR::origin)) return false;
      if (!finish_title() || !finish_index()) return false;
      open_line(origin.body.size(), &origin);
      skip_until = origin_index;
      return true;
    }

    if ((in_title || in_index) && is_bullet_glyph(view))
      return assign(view, ProseTokenRoleIR::padding);
    if (in_title && out.title.empty() && view.width == 1 &&
        punctuation_glyph(view))
      return assign(view, ProseTokenRoleIR::marker);
    if (in_title) {
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        out.title.push_back(' ');
      out.title += body_text(view);
      last_visible = body_text(view);
      pending_space = view.prefix != 2;
      out.title_refs.emplace_back(view.record, view.token);
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::title);
    }
    if (in_index) {
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        current_term.term.push_back(' ');
      current_term.term += body_text(view);
      last_visible = body_text(view);
      pending_space = view.prefix != 2;
      term_refs.emplace_back(view.record, view.token);
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::index_term);
    }
    if (!line_open) open_line(0, nullptr);
    trailing_bare = 0;
    if (line_visible_cells == 0 && view.width == 1 &&
        (punctuation_glyph(view) || is_placeholder_run(view)) &&
        index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
        is_visible(items[index + 1].token) &&
        !is_placeholder_run(items[index + 1].token)) {
      // A visual row marker (`|`, box glyph) opening the row directly before
      // its text (GC23-046 record 151 `| ◆ The number of orders`, ACPZMST1
      // record 78 `│ The following sections`).
      return assign(view, ProseTokenRoleIR::marker);
    }
    if (line_visible_cells == 0 && is_glyph(view) && view.width == 1) {
      // A glyph opening the line is the list bullet.
      line().bullet = true;
      if (!assign(view, ProseTokenRoleIR::bullet)) return false;
      line().cells.push_back({view.record, view.token, word_text(view.body[0]),
                              false});
      ++line_visible_cells;
      pending_space = true;
      const auto next = next_token(index);
      if (space_at(next) && !space_at(next_token(next))) {
        line().cells.push_back({npos, 0, " ", true});
        pending_space = false;
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        skip_until = next;
      }
      line().text_begin = line().cells.size();
      return true;
    }
    if (is_glyph(view) || is_placeholder_run(view))
      return fail(error, "placeholder glyph '" + body_text(view) +
                             "' inside prose text at " + where(view));
    if (std::any_of(view.body.begin(), view.body.end(),
                    [](const auto word) { return word == unmapped_word; }))
      return fail(error, "unmapped word '" + body_text(view) +
                             "' inside prose text at " + where(view));
    return append_visible(view, ProseTokenRoleIR::text);
  }

  std::size_t skip_until = npos;
};

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

struct Attr {
  FontStyleIR style = FontStyleIR::unknown;
  std::size_t link = npos;
  bool operator==(const Attr& other) const {
    return style == other.style && link == other.link;
  }
  bool operator!=(const Attr& other) const { return !(*this == other); }
};

struct BlockChar {
  std::string text;
  Attr attr;
  std::size_t record = npos;
  std::size_t token = 0;
  bool space = false;
};

// A span boundary must not split a run of word characters; attached
// punctuation (`AIX.`, `"Bibliography"`) stays outside the styled phrase, as
// BookServer renders it.
bool word_char(const std::string& text) {
  if (text.empty()) return false;
  const auto ch = static_cast<unsigned char>(text.front());
  return std::isalnum(ch) != 0 || ch >= 0x80 || ch == '_';
}

bool boundary_between(const Line& line, std::size_t left, std::size_t right) {
  if (left >= line.cells.size() || right >= line.cells.size()) return true;
  return line.cells[left].space || line.cells[right].space ||
         !word_char(line.cells[left].text) || !word_char(line.cells[right].text);
}

std::string line_text(const Line& line) {
  std::string text;
  for (const auto& cell : line.cells) text += cell.text;
  return text;
}

bool resolve_spans(const Line& line, std::vector<Attr>& attrs,
                   std::vector<std::string>& targets, std::string* error) {
  attrs.assign(line.cells.size(), Attr{});
  const auto apply = [&](const Span& span, bool link) -> bool {
    auto begin = span.begin;
    auto end = span.end;
    const auto where = [&]() {
      return " [" + std::to_string(span.begin) + "," +
             std::to_string(span.end) + ") on '" + line_text(line) + "'";
    };
    if (begin >= end || end > line.cells.size())
      return fail(error, "font/selector span [" + std::to_string(begin) + "," +
                             std::to_string(end) +
                             ") exceeds the display line of " +
                             std::to_string(line.cells.size()) + " cells");
    while (begin < end && line.cells[begin].space) ++begin;
    while (end > begin && line.cells[end - 1].space) --end;
    if (begin >= end) return fail(error, "font/selector span is blank" + where());
    if (begin > line.text_begin && !boundary_between(line, begin - 1, begin))
      return fail(error, "span starts inside a word" + where());
    if (end < line.cells.size() && !boundary_between(line, end - 1, end))
      return fail(error, "span ends inside a word" + where());
    if (begin < line.text_begin) return fail(error, "span covers the bullet");
    for (auto cell = begin; cell < end; ++cell) {
      if (line.cells[cell].space) continue;
      auto& attr = attrs[cell];
      if (link) {
        if (attr.link != npos)
          return fail(error, "overlapping selector spans");
        attr.link = targets.size();
      } else {
        if (attr.style != FontStyleIR::unknown)
          return fail(error, "overlapping font spans");
        attr.style = span.style;
      }
    }
    if (link) targets.push_back(span.target);
    return true;
  };
  for (const auto& span : line.links)
    if (!apply(span, true)) return false;
  for (const auto& span : line.fonts)
    if (!apply(span, false)) return false;
  for (const auto& attr : attrs)
    if (attr.link != npos && attr.style != FontStyleIR::unknown)
      return fail(error, "font span inside a selector span");
  return true;
}

bool build_block(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Line>& lines, std::size_t begin,
                 std::size_t end, ProseBlockIR& block, Ledger& ledger,
                 std::size_t block_index, std::string* error) {
  std::vector<BlockChar> chars;
  std::vector<std::string> targets;
  for (auto index = begin; index < end; ++index) {
    const auto& line = lines[index];
    std::vector<Attr> attrs;
    std::vector<std::string> line_targets;
    if (!resolve_spans(line, attrs, line_targets, error)) return false;
    const auto target_base = targets.size();
    targets.insert(targets.end(), line_targets.begin(), line_targets.end());
    if (index != begin) chars.push_back({" ", {}, npos, 0, true});
    for (std::size_t cell = line.text_begin; cell < line.cells.size(); ++cell) {
      const auto& source = line.cells[cell];
      auto attr = attrs[cell];
      if (attr.link != npos) attr.link += target_base;
      chars.push_back({source.text, attr, source.record, source.token,
                       source.space});
    }
  }
  // Collapse whitespace; a space takes the attributes of its neighbours when
  // they agree and is plain text otherwise.
  std::vector<BlockChar> collapsed;
  for (std::size_t index = 0; index < chars.size(); ++index) {
    const auto& ch = chars[index];
    if (ch.space) {
      if (collapsed.empty() || collapsed.back().space) continue;
      collapsed.push_back(ch);
      collapsed.back().attr = {};
      continue;
    }
    collapsed.push_back(ch);
  }
  while (!collapsed.empty() && collapsed.back().space) collapsed.pop_back();
  if (collapsed.empty()) return fail(error, "block has no visible text");
  for (std::size_t index = 1; index + 1 < collapsed.size(); ++index) {
    if (collapsed[index].space &&
        collapsed[index - 1].attr == collapsed[index + 1].attr)
      collapsed[index].attr = collapsed[index - 1].attr;
  }
  std::vector<std::pair<std::size_t, std::size_t>> block_refs;
  std::size_t run_begin = 0;
  while (run_begin < collapsed.size()) {
    auto run_end = run_begin;
    while (run_end < collapsed.size() &&
           collapsed[run_end].attr == collapsed[run_begin].attr)
      ++run_end;
    ProseInlineIR inline_node;
    const auto& attr = collapsed[run_begin].attr;
    if (attr.link != npos) {
      inline_node.kind = ProseInlineKindIR::cross_reference;
      inline_node.target = targets[attr.link];
    } else if (attr.style != FontStyleIR::unknown) {
      inline_node.kind = ProseInlineKindIR::emphasis;
      inline_node.style = attr.style;
    }
    std::vector<std::pair<std::size_t, std::size_t>> refs;
    for (auto index = run_begin; index < run_end; ++index) {
      inline_node.text += collapsed[index].text;
      if (collapsed[index].record != npos)
        refs.emplace_back(collapsed[index].record, collapsed[index].token);
    }
    // Space cells carry the space-run token; drop those refs (their role is
    // gap/origin) and keep only text tokens.
    std::vector<std::pair<std::size_t, std::size_t>> text_refs;
    for (const auto& ref : refs) {
      auto& entry = ledger.at(ref.first, ref.second);
      if (entry.role != ProseTokenRoleIR::text) continue;
      if (entry.block != npos && entry.block != block_index)
        return fail(error, "text token shared by two blocks");
      entry.block = block_index;
      entry.inline_index = block.inlines.size();
      text_refs.push_back(ref);
    }
    std::sort(text_refs.begin(), text_refs.end());
    text_refs.erase(std::unique(text_refs.begin(), text_refs.end()),
                    text_refs.end());
    inline_node.slices = slices_for(records, text_refs);
    block_refs.insert(block_refs.end(), text_refs.begin(), text_refs.end());
    if (inline_node.kind != ProseInlineKindIR::text &&
        inline_node.slices.empty())
      return fail(error, "styled inline has no source provenance");
    block.inlines.push_back(std::move(inline_node));
    run_begin = run_end;
  }
  std::sort(block_refs.begin(), block_refs.end());
  block.slices = slices_for(records, block_refs);
  return true;
}

bool build_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                  const LineBuild& lines_build, Ledger& ledger,
                  ProseTopicIR& topic, std::string* error) {
  const auto& lines = lines_build.lines;
  std::vector<std::pair<std::size_t, std::size_t>> ranges;  // [begin,end)
  std::vector<bool> is_item;
  std::vector<std::size_t> origins;
  std::size_t index = 0;
  std::size_t carried_breaks = 0;
  while (index < lines.size()) {
    const auto& first = lines[index];
    if (first.cells.size() <= first.text_begin) {
      // A row without text is vertical spacing between blocks.
      if (first.bullet)
        return fail(error, "bullet row has no text: '" + line_text(first) +
                               "'");
      carried_breaks += first.breaks_before + 1;
      ++index;
      continue;
    }
    carried_breaks = 0;
    auto end = index + 1;
    while (end < lines.size()) {
      const auto& next = lines[end];
      if (next.breaks_before != 0 || next.anchor_before || next.bullet) break;
      if (next.cells.size() <= next.text_begin) break;
      if (first.bullet && next.origin <= first.origin) break;
      if (!first.bullet && next.origin < first.origin) break;
      ++end;
    }
    ranges.emplace_back(index, end);
    is_item.push_back(first.bullet);
    origins.push_back(first.origin);
    index = end;
  }
  std::size_t list_ordinal = 0;
  for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index) {
    ProseBlockIR block;
    block.origin = origins[block_index];
    if (is_item[block_index]) {
      block.kind = ProseBlockKindIR::list_item;
      if (block_index == 0 || !is_item[block_index - 1])
        ++list_ordinal;
      else if (origins[block_index] != origins[block_index - 1])
        return fail(error, "nested or misaligned list items");
      block.list_ordinal = list_ordinal;
    }
    if (!build_block(records, lines, ranges[block_index].first,
                     ranges[block_index].second, block, ledger, block_index,
                     error))
      return false;
    topic.blocks.push_back(std::move(block));
  }
  // Anchors: leading ones precede block 0; body anchors precede the block
  // that starts at their line, or the end.
  for (std::size_t anchor = 0; anchor < lines_build.body_anchors.size();
       ++anchor) {
    auto placed = lines_build.body_anchors[anchor];
    placed.position = ranges.size();
    for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index) {
      const auto& first = lines[ranges[block_index].first];
      if (first.anchor_before && first.anchor_index == anchor) {
        placed.position = block_index;
        break;
      }
    }
    topic.anchors.push_back(std::move(placed));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Trailing menu
// ---------------------------------------------------------------------------

bool build_menu(const std::vector<DecodedLogicalRecordSource>& records,
                const StreamBuild& build,
                const BookTopicCatalogIR* book_topic_catalog, Ledger& ledger,
                ProseTopicIR& topic, std::string* error) {
  if (build.menu_record == npos) return true;
  if (book_topic_catalog == nullptr)
    return fail(error, "trailing menu needs the book topic catalog");
  std::string menu_error;
  const auto raw = extract_source_menu_ir(records, &menu_error);
  if (!raw) return fail(error, "trailing menu rejected: " + menu_error);
  auto validation =
      validate_source_menu_targets(*raw, *book_topic_catalog, &menu_error);
  if (!validation) {
    // The catalog's topic-header title is a compatibility projection that can
    // carry glued body text (`<title>??? SI ...`); the TOC title is the
    // canonical spelling BookServer prints.  Accept a label that equals the
    // target's TOC title verbatim.
    MenuTargetValidationIR fallback;
    for (const auto& item : raw->items) {
      const auto* entry =
          find_book_topic_catalog_entry(*book_topic_catalog, item.target);
      if (entry == nullptr)
        return fail(error, "trailing menu targets rejected: " + menu_error);
      // The full label is tried first: a one-byte final word such as
      // SH20-918 `... SCRIPT/VS and GML` is title text.  Only when it
      // disagrees is the source-proven compact terminal token (SC31-711
      // record 127 `Generic Traps :`) excluded.
      const auto label_matches = [&](const std::string& candidate) {
        if (candidate.empty()) return false;
        if (std::any_of(entry->toc_entries.begin(), entry->toc_entries.end(),
                        [&](const auto& toc) {
                          return ascii_equals_case_insensitive(
                              candidate,
                              collapse_ascii_whitespace(trim_ascii(toc.title)));
                        }))
          return true;
        if (!entry->topic_header) return false;
        // A glued header title is the label followed by a one-cell row
        // marker and the first body row; the label must end at that
        // non-alphanumeric boundary.
        const auto header =
            collapse_ascii_whitespace(trim_ascii(entry->topic_header->title));
        return header.size() > candidate.size() &&
               ascii_equals_case_insensitive(header.substr(0, candidate.size()),
                                             candidate) &&
               std::isalnum(static_cast<unsigned char>(
                   header[candidate.size()])) == 0;
      };
      auto label = collapse_ascii_whitespace(trim_ascii(item.text));
      auto matches = label_matches(label);
      if (!matches && item.compact_terminal) {
        std::string stripped;
        for (std::size_t cell = 0;
             cell < item.compact_terminal->label_cell_begin &&
             cell < item.label_cells.size();
             ++cell)
          stripped += token_words_to_ascii({item.label_cells[cell].word});
        stripped = collapse_ascii_whitespace(trim_ascii(stripped));
        if (label_matches(stripped)) {
          label = stripped;
          matches = true;
        }
      }
      if (!matches)
        return fail(error, "trailing menu targets rejected: " + menu_error);
      MenuTargetValidationEntryIR validated;
      validated.target = item.target;
      validated.label = label;
      validated.existence =
          MenuTargetValidationEntryIR::ExistenceEvidence::toc_entry;
      validated.label_evidence =
          MenuTargetValidationEntryIR::LabelEvidence::toc_title;
      fallback.items.push_back(std::move(validated));
    }
    validation = std::move(fallback);
  }
  if (validation->items.size() != raw->items.size())
    return fail(error, "trailing menu validation is incomplete");
  // Every token from the menu start onwards belongs to the menu.
  for (auto record_index = build.menu_record; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    std::size_t first_token = 0;
    if (record_index == build.menu_record) {
      if (build.menu_segment >= record.control_segments.size())
        return fail(error, "menu record has no segments");
      const auto& menu = record.control_segments[build.menu_segment];
      if (menu.source_tokens.empty())
        return fail(error, "menu start has no source token");
      first_token = menu.source_tokens.front();
    }
    for (auto token = first_token; token < record.ir.tokens.size(); ++token) {
      if (ledger.at(record_index, token).role != ProseTokenRoleIR::unassigned)
        continue;
      if (!ledger.assign(record_index, token, ProseTokenRoleIR::menu, error))
        return false;
    }
    // Anything before the menu start in this record must already be owned.
  }
  for (std::size_t index = 0; index < raw->items.size(); ++index) {
    const auto& item = raw->items[index];
    const auto& entry = validation->items[index];
    if (entry.target != item.target)
      return fail(error, "menu validation target mismatch");
    ProseMenuItemIR typed;
    typed.target = entry.target;
    typed.label = entry.label;
    const auto record = std::find_if(
        records.begin(), records.end(), [&](const auto& candidate) {
          return candidate.logical_record == item.logical_record;
        });
    if (record == records.end() || item.target_cells.empty())
      return fail(error, "menu item provenance is incomplete");
    auto begin = item.target_cells.front().token_index;
    auto end = begin + 1;
    for (const auto* cells : {&item.target_cells, &item.label_cells})
      for (const auto& cell : *cells) {
        begin = std::min(begin, cell.token_index);
        end = std::max(end, cell.token_index + 1);
      }
    typed.source = token_slice(*record, begin, end);
    topic.menu_items.push_back(std::move(typed));
  }
  if (topic.menu_items.empty()) return fail(error, "trailing menu is empty");
  return true;
}

// ---------------------------------------------------------------------------

// Titles compare after whitespace collapse, ASCII case folding and removal of
// trailing punctuation: TOC titles can carry a leaked one-cell marker
// (`Syntax Notation %`) and ST payloads a terminal period.
std::string normalize_title(std::string value) {
  value = collapse_ascii_whitespace(std::move(value));
  while (!value.empty() &&
         std::ispunct(static_cast<unsigned char>(value.back())) != 0 &&
         value.back() != ')' && value.back() != '"' && value.back() != '\'')
    value.pop_back();
  value = trim_ascii(std::move(value));
  return ascii_lower(std::move(value));
}

} // namespace

// `ProseIndexTermIR` helper used by the builder.
} // namespace geist::detail

namespace geist::detail {

std::optional<ProseTopicIR> extract_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    std::string* error) {
  const auto reject = [&](std::string message) -> std::optional<ProseTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty()) return reject("topic has no records");
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !verify_ownership_ir(records, layout, ownership, &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);
  for (const auto& record : records)
    if (record.ir.tokens.size() != record.tokens.size() ||
        record.ir.tokens.size() != record.encoded_tokens.size())
      return reject("record token projections disagree");

  Ledger ledger(records);
  Envelope envelope;
  if (!parse_envelope(records, ledger, envelope, error)) return std::nullopt;
  StreamBuild stream;
  if (!collect_stream(records, envelope, ledger, stream, error))
    return std::nullopt;
  // A plural CFONT header over repeated encoded row controls is a
  // two-column form (SC31-711 1.2 `Directory` / `Type of Files`), which the
  // legacy route draws as a table; prose must not flatten it.
  for (const auto& item : stream.items) {
    if (item.kind != ItemKind::font || item.spans.size() < 2) continue;
    std::vector<ImplicitGridHeaderSpan> header;
    for (const auto& span : item.spans)
      header.push_back({span.column, span.length});
    if (extract_implicit_grid(records, header))
      return reject("body contains an implicit two-column grid");
  }
  LineBuild lines;
  LineBuilder builder(records, stream.items, ledger, lines, error);
  if (!builder.run()) return std::nullopt;

  if (normalize_title(lines.title) != normalize_title(title))
    return reject("ST title '" + lines.title +
                  "' does not match the topic title '" + title + "'");

  ProseTopicIR topic;
  topic.record_count = records.size();
  topic.token_count = ledger.entries.size();
  topic.heading_level = envelope.heading_level;
  topic.title = lines.title;
  {
    auto refs = lines.title_refs;
    std::sort(refs.begin(), refs.end());
    const auto slices = slices_for(records, refs);
    if (slices.empty()) return reject("ST title has no source provenance");
    topic.title_source = slices.front();
    if (slices.size() != 1) {
      topic.title_source.token_end = slices.back().token_end;
      topic.title_source.byte_end = slices.back().byte_end;
    }
  }
  for (auto anchor : stream.leading_anchors) {
    anchor.position = 0;
    topic.anchors.push_back(std::move(anchor));
  }
  if (!build_blocks(records, lines, ledger, topic, error)) return std::nullopt;
  topic.index_terms = lines.index_terms;
  topic.index_terms.insert(topic.index_terms.end(),
                           stream.trailing_index_terms.begin(),
                           stream.trailing_index_terms.end());
  if (!build_menu(records, stream, book_topic_catalog, ledger, topic, error))
    return std::nullopt;
  for (auto anchor : stream.trailing_anchors) {
    anchor.position = topic.blocks.size();
    topic.anchors.push_back(std::move(anchor));
  }

  for (const auto& entry : ledger.entries) {
    if (entry.role == ProseTokenRoleIR::unassigned)
      return reject("token " + std::to_string(entry.token.token_index) +
                    " of record " + std::to_string(entry.token.logical_record) +
                    " has no disposition");
    if (entry.role == ProseTokenRoleIR::text && entry.block == npos)
      return reject("visible token " + std::to_string(entry.token.token_index) +
                    " of record " + std::to_string(entry.token.logical_record) +
                    " belongs to no block");
  }
  topic.ledger = std::move(ledger.entries);
  if (topic.blocks.empty() && topic.menu_items.empty())
    return reject("topic body has no prose blocks");
  if (error != nullptr) error->clear();
  return topic;
}

bool verify_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    const ProseTopicIR& topic, std::string* error) {
  std::size_t total = 0;
  for (const auto& record : records) total += record.ir.tokens.size();
  if (topic.ledger.size() != total || topic.token_count != total ||
      topic.record_count != records.size())
    return fail(error, "prose ledger does not cover every source token");
  std::set<std::pair<std::uint32_t, std::size_t>> seen;
  std::map<std::pair<std::uint32_t, std::size_t>, const ProseTokenDispositionIR*>
      by_token;
  for (const auto& entry : topic.ledger) {
    if (!seen.emplace(entry.token.logical_record, entry.token.token_index).second)
      return fail(error, "prose ledger lists a token twice");
    if (entry.role == ProseTokenRoleIR::unassigned)
      return fail(error, "prose ledger holds an unassigned token");
    if ((entry.role == ProseTokenRoleIR::text) != (entry.block != npos))
      return fail(error, "text disposition and block ownership disagree");
    by_token.emplace(std::make_pair(entry.token.logical_record,
                                    entry.token.token_index),
                     &entry);
  }
  // Every text token is covered by exactly one inline slice and every inline
  // slice covers only text tokens of its own inline.
  std::set<std::pair<std::uint32_t, std::size_t>> covered;
  for (std::size_t block = 0; block < topic.blocks.size(); ++block) {
    const auto& node = topic.blocks[block];
    if (node.inlines.empty()) return fail(error, "prose block has no inlines");
    for (std::size_t index = 0; index < node.inlines.size(); ++index) {
      const auto& inline_node = node.inlines[index];
      if (inline_node.text.empty()) return fail(error, "inline text is empty");
      for (const auto& slice : inline_node.slices) {
        if (slice.token_begin >= slice.token_end)
          return fail(error, "inline slice is empty");
        for (auto token = slice.token_begin; token < slice.token_end; ++token) {
          const auto found = by_token.find({slice.logical_record, token});
          if (found == by_token.end() ||
              found->second->role != ProseTokenRoleIR::text ||
              found->second->block != block ||
              found->second->inline_index != index)
            return fail(error, "inline slice covers a token it does not own");
          if (!covered.emplace(slice.logical_record, token).second)
            return fail(error, "token covered by two inline slices");
        }
      }
    }
  }
  for (const auto& entry : topic.ledger)
    if (entry.role == ProseTokenRoleIR::text &&
        covered.count({entry.token.logical_record, entry.token.token_index}) == 0)
      return fail(error, "visible token is covered by no inline");
  const auto canonical = extract_prose_topic_ir(records, layout, ownership,
                                                title, book_topic_catalog, error);
  if (!canonical) return false;
  if (format_prose_topic_ir(*canonical) != format_prose_topic_ir(topic))
    return fail(error, "prose topic differs from canonical extraction");
  if (error != nullptr) error->clear();
  return true;
}

const char* prose_token_role_name(ProseTokenRoleIR role) {
  switch (role) {
  case ProseTokenRoleIR::unassigned: return "unassigned";
  case ProseTokenRoleIR::envelope: return "envelope";
  case ProseTokenRoleIR::control: return "control";
  case ProseTokenRoleIR::padding: return "padding";
  case ProseTokenRoleIR::title: return "title";
  case ProseTokenRoleIR::spacing: return "spacing";
  case ProseTokenRoleIR::fill: return "fill";
  case ProseTokenRoleIR::origin: return "origin";
  case ProseTokenRoleIR::marker: return "marker";
  case ProseTokenRoleIR::bullet: return "bullet";
  case ProseTokenRoleIR::gap: return "gap";
  case ProseTokenRoleIR::text: return "text";
  case ProseTokenRoleIR::index_keyword: return "index_keyword";
  case ProseTokenRoleIR::index_term: return "index_term";
  case ProseTokenRoleIR::menu: return "menu";
  }
  return "invalid";
}

namespace {

void format_slices(std::ostream& out,
                   const std::vector<DocumentSourceSliceIR>& slices) {
  out << " slices=[";
  for (std::size_t index = 0; index < slices.size(); ++index) {
    const auto& slice = slices[index];
    if (index != 0) out << ' ';
    out << slice.logical_record << ':' << slice.segment_index << ':'
        << slice.token_begin << '-' << slice.token_end << ":0x" << std::hex
        << slice.byte_begin << "-0x" << slice.byte_end << std::dec;
  }
  out << ']';
}

} // namespace

std::string format_prose_topic_ir(const ProseTopicIR& topic) {
  std::ostringstream out;
  out << "prose_topic records=" << topic.record_count
      << " tokens=" << topic.token_count << " heading_level="
      << topic.heading_level << " title='" << topic.title << "'";
  format_slices(out, {topic.title_source});
  out << '\n';
  for (const auto& anchor : topic.anchors) {
    out << "anchor id=" << anchor.id << " position=" << anchor.position
        << (anchor.after_menu ? " after_menu" : "");
    format_slices(out, {anchor.source});
    out << '\n';
  }
  for (std::size_t index = 0; index < topic.blocks.size(); ++index) {
    const auto& block = topic.blocks[index];
    out << "block " << index << ' '
        << (block.kind == ProseBlockKindIR::list_item ? "list_item" : "paragraph")
        << " origin=" << block.origin;
    if (block.kind == ProseBlockKindIR::list_item)
      out << " list=" << block.list_ordinal;
    format_slices(out, block.slices);
    out << '\n';
    for (const auto& inline_node : block.inlines) {
      out << "  ";
      switch (inline_node.kind) {
      case ProseInlineKindIR::text: out << "text"; break;
      case ProseInlineKindIR::emphasis:
        out << "emphasis style=" << font_style_name(inline_node.style);
        break;
      case ProseInlineKindIR::cross_reference:
        out << "xref target=" << inline_node.target;
        break;
      }
      out << " '" << inline_node.text << "'";
      format_slices(out, inline_node.slices);
      out << '\n';
    }
  }
  for (const auto& term : topic.index_terms) {
    out << "index_term '" << term.term << "'";
    format_slices(out, term.slices);
    out << '\n';
  }
  for (const auto& item : topic.menu_items) {
    out << "menu_item target=" << item.target << " label='" << item.label
        << "'";
    format_slices(out, {item.source});
    out << '\n';
  }
  std::map<ProseTokenRoleIR, std::size_t> counts;
  for (const auto& entry : topic.ledger) ++counts[entry.role];
  out << "ledger";
  for (const auto& [role, count] : counts)
    out << ' ' << prose_token_role_name(role) << '=' << count;
  out << '\n';
  for (const auto& entry : topic.ledger) {
    if (entry.role != ProseTokenRoleIR::text) continue;
    out << "  text " << entry.token.logical_record << ':'
        << entry.token.token_index << " block=" << entry.block
        << " inline=" << entry.inline_index << '\n';
  }
  return out.str();
}

} // namespace geist::detail
