#include "geist/detail/prose_topic_internal.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace geist::detail::prose_internal {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

// ---------------------------------------------------------------------------
// Token classification
// ---------------------------------------------------------------------------


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
      if (build.plan != nullptr) {
        // A segment of a planned table/figure region: the region's tokens
        // are already owned by their span (prose_topic_spans.cpp); the span
        // enters the stream once, at its first segment, and any token of
        // the closing segment the region does not own (`SREFIG? The
        // routers ...`) is body text.
        const auto* region =
            build.plan->region_of_segment(record_index, segment_index);
        const auto* frame =
            build.plan->frame_of_segment(record_index, segment_index);
        if (region != nullptr || frame != nullptr) {
          if (!title_seen)
            return fail(error, "table/figure region precedes the ST title");
          if (menu_open)
            return fail(error, "content follows the trailing menu");
          if (frame != nullptr && frame->begin_record == record_index &&
              frame->begin_segment == segment_index) {
            Item item;
            item.kind = ItemKind::anchor;
            item.anchor_id = frame->anchor_id;
            item.source = frame->source;
            build.items.push_back(std::move(item));
          }
          if (region != nullptr && region->begin_record == record_index &&
              region->begin_segment == segment_index) {
            Item item;
            item.kind = ItemKind::span;
            item.span_index = region->span;
            build.items.push_back(std::move(item));
          }
          bool first_payload = true;
          for (const auto token : segment.source_tokens) {
            if (ledger.at(record_index, token).role !=
                ProseTokenRoleIR::unassigned)
              continue;
            const auto view = view_token(records, record_index, token);
            // Spacing and the punctuation the decoder glues to the end
            // marker (`SREFIG.`, GG24-4302-00 5.1.8) precede any payload.
            if (first_payload &&
                (is_padding(view) || is_separator(view) ||
                 (punctuation_glyph_token(view) &&
                  std::all_of(view.body.begin(), view.body.end(),
                              [](const auto word) {
                                return word == '.' || word == ',' ||
                                       word == ';';
                              })))) {
              if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                                 error))
                return false;
              continue;
            }
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            item.continuation_start = first_payload;
            first_payload = false;
            build.items.push_back(std::move(item));
          }
          if (!first_payload) {
            Item end;
            end.kind = ItemKind::segment_end;
            build.items.push_back(std::move(end));
          }
          continue;
        }
      }
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

} // namespace geist::detail::prose_internal
