// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/ir/sr_anchor_name.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
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

bool is_row_control_slot(const std::vector<DecodedLogicalRecordSource>& records,
                         const TokenView& view) {
  return is_display_line_length_token(records[view.record], view.token);
}

std::string word_text(std::uint16_t word) {
  // This is the one displayed-text projection used for ordinary prose cells,
  // native rows and the byte offsets in source claims. A glyph still occupies
  // one layout column, but its rendered spelling can occupy several UTF-8
  // bytes (U+2192 is three, U+2666 projects to the two-byte degree sign).
  // Keeping body_text() on this same projection lets the verifier measure a
  // partial token claim in the encoding that its inline actually carries.
  return figure_display_glyph(word);
}

std::string body_text(const TokenView& view) {
  std::string text;
  for (const auto word : view.body)
    text += word == bullet_glyph_word || word == unmapped_word
                ? figure_display_glyph(word)
                : token_words_to_ascii({word});
  return text;
}

// The token's words for a decline message: printable ASCII as itself, any
// other word as its code point.  The decoder's ASCII projection spells every
// non-ASCII word `?`, which made a correctly decoded arrow (DREICMST 1.5.5.4
// record 162 token 310, `U+2193`) read like an undecodable byte.
static std::string diagnostic_text(const TokenView& view) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string text;
  for (const auto word : view.body) {
    if (word >= 0x20 && word <= 0x7E) {
      text += static_cast<char>(word);
      continue;
    }
    text += "U+";
    for (int shift = 12; shift >= 0; shift -= 4)
      text += hex[(word >> shift) & 0xF];
  }
  return text;
}

std::string display_body_text(const TokenView& view) {
  std::string text;
  for (const auto word : view.body) text += word_text(word);
  return text;
}


// ---------------------------------------------------------------------------
// Provenance helpers
// ---------------------------------------------------------------------------

std::size_t segment_of(const DecodedLogicalRecordSource& record,
                       std::size_t token) {
  // A token claimed by no segment (an inter-segment decoder separator that
  // is text in the CZ dialect) reports the segment it follows so slice
  // keys stay monotonic.
  std::size_t preceding = 0;
  for (const auto& segment : record.control_segments) {
    if (std::binary_search(segment.source_tokens.begin(),
                           segment.source_tokens.end(), token))
      return segment.segment_index;
    if (!segment.source_tokens.empty() && segment.source_tokens.back() < token)
      preceding = segment.segment_index;
  }
  return preceding;
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


// Records that `inline_index` of `block` owns the whole decoded word of one
// text token.  Rows that own their source verbatim (drawn boxes, `cz OFF XMP`
// example blocks) call this once per token; the reflowed prose path in
// prose_topic_blocks.cpp builds its claims cell by cell because a span may
// split one word between two inlines.
bool claim_token_whole(const std::vector<DecodedLogicalRecordSource>& records,
                       Ledger& ledger, std::size_t record, std::size_t token,
                       std::size_t block, std::size_t inline_index,
                       std::string* error) {
  auto& entry = ledger.at(record, token);
  if (entry.block != npos && entry.block != block)
    return fail(error, "text token shared by two blocks");
  if (!entry.claims.empty()) {
    const auto& claim = entry.claims.front();
    if (entry.claims.size() != 1 || claim.block != block ||
        claim.inline_index != inline_index)
      return fail(error, "text token claimed by two inlines");
    return true;
  }
  entry.block = block;
  entry.inline_index = inline_index;
  entry.claims.push_back(
      {block, inline_index, 0,
       static_cast<std::uint32_t>(
           body_text(view_token(records, record, token)).size())});
  return true;
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
  auto tokens = source_tokens_intersecting_output(record.assembled, words.begin,
                                                  words.end);
  // A control instruction and its operands occupy their own native display
  // line.  The flattened decoded byte range can end on the next line's blank
  // length/origin spellings; if that line opens with numeric words identical
  // to operand words, mapping the flattened range alone aliases those drawn
  // tokens into the instruction.  FA1PLMM0 3.2.3 record 187 carries
  // `cfont 7 1 2 9 1 2 11 1 2 13 1 2` on line 16 and the distinct row
  // `       3 7 2 2 and provide ...` on line 17.  Only line 16 owns the
  // operand, while line 17's four digits sit at the stated display columns.
  const auto opcode_words = decoded_byte_range_to_word_range(
      record.assembled,
      {segment.opcode_range.begin, segment.opcode_range.end});
  const auto opcode_tokens = source_tokens_intersecting_output(
      record.assembled, opcode_words.begin, opcode_words.end);
  if (!opcode_tokens.empty()) {
    if (const auto* line = display_line_of_token(record, opcode_tokens.front()))
      tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                  [&](const auto token) {
                                    return token <= line->prefix_token ||
                                           token >= line->token_end;
                                  }),
                   tokens.end());
  }
  return tokens;
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
    // The row-control slot that opens the next display line is structure,
    // not this control's payload: the operand parser ran past the framing
    // boundary and picked up the length byte's dictionary spelling.
    if (is_padding(view) || is_row_control_slot(records, view) ||
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

// The `c.cp` keep-together depth: a decimal count with an optional unit
// suffix.  Observed corpus-wide: bare counts (`4`..`999`), `1i`/`2i`
// (inches), `50p` (points) and `8DV` (device units).
// SCRIPT page controls that carry no display of their own.  `c.cp` is the
// keep-together depth, `c.cc` the conditional-column depth and `c.pa` the page
// eject.  All three are stored as one dictionary token spelling `c.<xx>` and
// hosted BookServer prints none of them: GG24-4302-00 8.1.5 record 613
// (`SREFIG` + separator + `c.cc` + `20`) serves the figure and its caption
// with no `20`; SC09-138 8.3.1.8 record 1482 (`each` `other` `.` `,` `c.pa`)
// serves `each other.` and starts the next figure; FA1PLMM0 6.1.2 record 353
// (`PSF` `/` `VSE` `,` `c.cp`) serves the list row as `PSF/VSE`, with neither
// the comma nor the opcode.
bool pagination_control(const std::string& lower_text) {
  return lower_text == "c.cp" || lower_text == "c.cc" || lower_text == "c.pa";
}

// Any SCRIPT control opcode: `c.` and letters only.  The reader skips every
// display line whose control name starts with `C.` (the version 1.0/1.1
// books write `C.SK <n>` 7,689 times; EDCGV1R2 3.4.6 record 68 line 11).
bool script_control(const std::string& lower_text) {
  return lower_text.size() > 2 && lower_text.compare(0, 2, "c.") == 0 &&
         std::all_of(lower_text.begin() + 2, lower_text.end(),
                     [](const unsigned char ch) {
                       return ch >= 'a' && ch <= 'z';
                     });
}

// Whether `token` is the first token after its display line's length byte,
// i.e. the opcode position of a control line.
bool opens_display_line(const DecodedLogicalRecordSource& record,
                        const std::size_t token) {
  const auto* line = display_line_of_token(record, token);
  return line != nullptr && line->prefix_token + 1 == token;
}

bool pagination_operand(const std::string& text) {
  std::size_t digits = 0;
  while (digits < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[digits])) != 0)
    ++digits;
  if (digits == 0 || digits > 4 || text.size() - digits > 3) return false;
  return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(digits),
                     text.end(), [](const unsigned char ch) {
                       return std::isalpha(ch) != 0;
                     });
}

// The five documented c.sp operand forms carry vertical spacing, never
// display text: a bare count, <n> c, <n> p, <n>p p c and <n>mm p c.
bool vertical_space_operands(const std::string& lower_text) {
  std::size_t digits = 0;
  while (digits < lower_text.size() &&
         std::isdigit(static_cast<unsigned char>(lower_text[digits])) != 0)
    ++digits;
  if (digits == 0 || digits > 3) return false;
  const auto rest = lower_text.substr(digits);
  return rest.empty() || rest == " c" || rest == " p" ||
         rest == "p p c" || rest == "mm p c";
}

// Whether `token` sits on a display row opened by a menu control
// (`cmenu`, `cmitem`, `cemenu`), whose extent the menu pass owns.
bool on_menu_control_row(const std::vector<DecodedLogicalRecordSource>& records,
                         std::size_t record_index, std::size_t token) {
  const auto& record = records[record_index];
  const auto* line = display_line_of_token(record, token);
  if (line == nullptr || line->prefix_token + 1 >= line->token_end)
    return false;
  const auto opcode = ascii_lower(
      body_text(view_token(records, record_index, line->prefix_token + 1)));
  return opcode == "cmenu" || opcode == "cmitem" || opcode == "cemenu";
}

// Front matter carries a named `CHDLEVEL` form instead of an `h1`-`h6`
// level.  Hosted BookServer serves every one of these forms as a level-1
// heading; the form itself is never visible.  Verified topic by topic
// against the hosted `<H1>` element: `cover` (ACPZMST1 COVER,
// `<H1> COVER   Book Cover</H1>`, DT 19920319123146), `vnotice`
// (ACPZMST1 EDITION), `toc` (ACPZMST1 CONTENTS), `index` (ACPZMST1 INDEX),
// `preface` (ACPZMST1 PREFACE), `notices` (GC23-046 NOTICES, DT
// 19920330095121), `glossary` (FA1PLMM0 GLOSSARY, DT 19910927114801),
// `soa` (DREICMST CHANGES, DT 19911219125856), `title` (ITPPIBOK TITLE,
// DT 19910628074854), `bibliog` (ITPPIBOK BIBLIOGRAPHY), `abstract`
// (FA1PLMM0 ABSTRACT) and `abbrev` (GG24-4302-00 ABBREVIATIONS, DT
// 19950308184737).  The reader has no list of these forms: a level that is
// not `H1`-`H6` or `MSGNO` heads the topic at level 1 whatever its name, and
// `legend` is served the same way (SC31-6171-00 and SC31-6173-01 LEGEND,
// `<h1> LEGEND   Notices</h1>`, DT 19930505133732).  The form must still be
// a name; an empty or unreadable operand stays fail-closed.
bool named_heading_form(const std::string& lower_form) {
  if (lower_form.empty()) return false;
  for (const auto ch : lower_form)
    if (std::isalnum(static_cast<unsigned char>(ch)) == 0) return false;
  return true;
}

// The decoded spelling of a run of tokens, in token order, with the spacing
// prefix of each dropped.  Used where a control's extent is bounded by the
// carried display-line framing rather than by the flattened string, so its
// text cannot be taken from a byte range of that string.
std::string token_run_text(const std::vector<DecodedLogicalRecordSource>& records,
                           std::size_t record_index,
                           const std::vector<std::size_t>& tokens) {
  std::string text;
  for (const auto token : tokens)
    text += body_text(view_token(records, record_index, token));
  return text;
}

// A topic id is not always numeric.  `SHPREFACE.2`, `SHB.3` and
// `SHAPPENDIX1.9.5` are real BookManager ids, and the segment classifier only
// reads an `SH` word as the topic-start control when a digit follows the
// `SH`, or when `promote_corroborated_topic_start` sees the `CTOPICN` control
// follow it in the same record.  The metadata run continues across a record
// break like any other run (see `parse_envelope`), and when the break falls
// immediately after the topic-start control the corroboration lands one
// record later, where that pass cannot see it: OFCUSEOV PREFACE.2 record 16
// carries `SHPREFACE.2` and nothing else, and record 17 opens with
// `ctopicn 7`; QSYSNEWG B.3 record 327 carries `shb.3` and record 328 opens
// with `ctopicn 120`.  The evidence is the same one, so the envelope walk
// accepts it across the break.
//
// Fail-closed: the segment must be the record's first, it must stand on the
// record's first display line with the line's length byte in front of it and
// nothing else visible anywhere in the segment, and its one token must spell
// an `SH`-prefixed identifier.  The token's own dictionary words decide that,
// not the flattened projection, and whether the byte in front of it is a
// length byte is read from the decoder's carried framing.
bool leading_topic_identifier_segment(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t record_index, const ControlSegmentIR& segment) {
  if (segment.kind != BookControlKind::text || segment.segment_index != 0)
    return false;
  const auto& record = records[record_index];
  const auto* lines = record_display_lines(record);
  if (lines == nullptr || lines->empty()) return false;
  const auto& first_line = lines->front();
  std::size_t identifier = record.ir.tokens.size();
  for (const auto token : segment.source_tokens) {
    const auto view = view_token(records, record_index, token);
    if (is_row_control_slot(records, view) || is_padding(view) ||
        !is_visible(view))
      continue;
    if (identifier < record.ir.tokens.size()) return false;
    identifier = token;
  }
  if (identifier >= record.ir.tokens.size()) return false;
  if (identifier != first_line.prefix_token + 1 ||
      identifier >= first_line.token_end)
    return false;
  const auto spelling = ascii_lower(body_text(view_token(records, record_index,
                                                         identifier)));
  if (spelling.size() <= 2 || spelling.compare(0, 2, "sh") != 0) return false;
  return std::all_of(spelling.begin(), spelling.end(),
                     [](const unsigned char ch) {
                       return std::isalnum(ch) != 0 || ch == '_' ||
                              ch == '-' || ch == '.';
                     });
}

// A bare `SR<id>` structural control (not one of the reserved block
// openers/closers) standing among the topic metadata controls.
bool envelope_anchor_segment(const ControlSegmentIR& segment) {
  if (segment.kind != BookControlKind::structural || segment.malformed)
    return false;
  const auto lower = ascii_lower(segment.opcode);
  return lower.rfind("sr", 0) == 0 && !reserved_structural(lower) &&
         segment.opcode.size() > 2;
}

// The served anchor name is the control's complete decoded output without the
// leading `SR`, so a payload extends the name: `SRLEN ADDRESS` is served as
// `<a name="LEN ADDRESS">`.  Every token of the control is envelope metadata;
// none of it is visible body text.
bool claim_envelope_anchor(const std::vector<DecodedLogicalRecordSource>& records,
                           Ledger& ledger, std::size_t record_index,
                           const ControlSegmentIR& segment,
                           ProseAnchorIR& anchor, std::string* error) {
  const auto& record = records[record_index];
  auto id = trim_ascii(operand_text(record, segment.complete));
  if (id.size() < 3 || ascii_lower(id).rfind("sr", 0) != 0)
    return fail(error, "envelope anchor '" + id + "' is not an SR control");
  id.erase(0, 2);
  id = trim_ascii(id);
  if (id.empty() ||
      !std::all_of(id.begin(), id.end(), [](const unsigned char ch) {
        return ch >= 0x20 && ch < 0x7F;
      }))
    return fail(error, "envelope anchor id '" + id + "' is invalid");
  if (segment.source_tokens.empty())
    return fail(error, "envelope anchor control has no source token");
  for (const auto token : segment.source_tokens) {
    const auto view = view_token(records, record_index, token);
    if (!ledger.assign(record_index, token,
                       is_padding(view) ? ProseTokenRoleIR::padding
                                        : ProseTokenRoleIR::envelope,
                       error))
      return false;
  }
  anchor.id = std::move(id);
  // Named after the row as stored where the control opens its own: blank
  // runs kept, an unmapped word as the reader's substitution byte.
  if (const auto stored =
          sr_row_anchor_name(record, segment.source_tokens.front()))
    anchor.id = *stored;
  anchor.source = token_slice(record, segment.source_tokens.front(),
                              segment.source_tokens.back() + 1);
  return true;
}

// An individually indexed message topic uses `SRMSG <message-id>
// [symbolic-name]` as metadata between CSUMMARY and CHDLEVEL.  Like the
// ordinary SR envelope anchor above, the complete control line is the served
// anchor identity: hosted message catalogs retain a symbolic tail in names
// such as `MSG 256 (snmp_br_dot1dStpPortState)`.  Claiming the whole segment
// also retains the message metadata's exact source extent; none of it is body
// prose.
bool claim_envelope_message_anchor(
    const std::vector<DecodedLogicalRecordSource>& records, Ledger& ledger,
    std::size_t record_index, const ControlSegmentIR& segment,
    ProseAnchorIR& anchor, std::string* error) {
  const auto& record = records[record_index];
  auto identity = trim_ascii(operand_text(record, segment.complete));
  // The flattened segment can own the length byte that opens the following
  // CHDLEVEL row.  Its dictionary spelling is not part of the message
  // identity; remove only tokens whose carried framing proves that role.
  auto source_end = segment.source_tokens.size();
  while (source_end != 0) {
    const auto token = segment.source_tokens[source_end - 1];
    const auto view = view_token(records, record_index, token);
    if (!is_row_control_slot(records, view)) break;
    const auto suffix = body_text(view);
    if (!suffix.empty() && identity.size() >= suffix.size() &&
        identity.compare(identity.size() - suffix.size(), suffix.size(),
                         suffix) == 0)
      identity = trim_ascii(identity.substr(0, identity.size() - suffix.size()));
    --source_end;
  }
  if (identity.size() < 5 || ascii_lower(identity).rfind("srmsg", 0) != 0)
    return fail(error, "message envelope identity '" + identity +
                           "' is not an SRMSG control");
  identity = trim_ascii(identity.substr(5));
  if (identity.empty() ||
      !std::all_of(identity.begin(), identity.end(),
                   [](const unsigned char ch) {
                     return ch >= 0x20 && ch < 0x7F;
                   }))
    return fail(error, "message envelope identity '" + identity +
                           "' is invalid");
  if (source_end == 0)
    return fail(error, "message envelope control has no source token");
  for (const auto token : segment.source_tokens) {
    const auto view = view_token(records, record_index, token);
    if (!ledger.assign(record_index, token,
                       is_padding(view) ? ProseTokenRoleIR::padding
                                        : ProseTokenRoleIR::envelope,
                       error))
      return false;
  }
  anchor.id = "MSG " + identity;
  // The name is the row as stored, blank runs included (`MSG   25`), where
  // the control opens a row of its own.
  if (const auto stored = message_row_anchor_name(record, segment);
      stored)
    anchor.id = *stored;
  anchor.source = token_slice(record, segment.source_tokens.front(),
                              segment.source_tokens[source_end - 1] + 1);
  return true;
}

bool parse_envelope(const std::vector<DecodedLogicalRecordSource>& records,
                    Ledger& ledger, Envelope& envelope, std::string* error) {
  const std::vector<BookControlKind> required = {
      BookControlKind::topic_start, BookControlKind::topic_number,
      BookControlKind::parent,      BookControlKind::forward_level,
      BookControlKind::back_level,  BookControlKind::summary,
      BookControlKind::heading_level, BookControlKind::source_file};
  // The metadata envelope is a run of control segments, not a property of one
  // logical record: the encoder breaks the record wherever the payload page
  // ends, so the run can continue in the next record.  Byte-level evidence
  // from `bootrace --ir`: GC28-183 2.3.5 keeps `sh2.3.5` and `ctopicn` in
  // record 163 and `cparent`..`csourcefn` plus `SRHDRJBEXNET` and `ST` in
  // record 164; QSYSINFO 2.1.57 breaks after `csummary` (record 163/164);
  // SC34-425 1.5.5 after `chdlevel` (241/242); SC41-485 COMMENTS after
  // `cbacklevel` (455/456); ACPZMST1 5.7 after `csourcefn` (289/290).  The
  // cursor below therefore walks the segments of the topic in source order
  // and the envelope simply ends where its controls end.
  struct Cursor {
    std::size_t record = 0;
    std::size_t segment = 0;
  };
  Cursor cursor;
  const auto at_end = [&]() {
    while (cursor.record < records.size() &&
           cursor.segment >= records[cursor.record].control_segments.size()) {
      ++cursor.record;
      cursor.segment = 0;
    }
    return cursor.record >= records.size();
  };
  const auto current = [&]() -> const ControlSegmentIR& {
    return records[cursor.record].control_segments[cursor.segment];
  };
  // A segment made of nothing but display-line length bytes is row geometry
  // whatever the splitter classified it as: the byte can spell a metadata
  // opcode.  EZ12GMST 1.2.134 record 939 frames its `SRLEN` and `ST` lines
  // with the length byte 0x31, which the dictionary spells `csourcefn`, so
  // two operand-less `csourcefn` segments stand in the envelope beside the
  // genuine `csourcefn J1AH9INE`.
  const auto row_control_slot_segment = [&]() {
    const auto& segment = current();
    return (segment.kind == BookControlKind::text ||
            std::find(required.begin() + 1, required.end(), segment.kind) !=
                required.end()) &&
           !segment.source_tokens.empty() &&
           std::all_of(segment.source_tokens.begin(),
                       segment.source_tokens.end(),
                       [&](const std::size_t token) {
                         return is_display_line_length_token(
                             records[cursor.record], token);
                       });
  };
  // The envelope is read the way the reader consumes a topic header
  // (doc/boo-spec/topics.adoc, "Reading The Envelope"): it opens with the
  // topic-start control, and the seven metadata controls that follow it are
  // each optional and carry no order.  None of them draws anything; only
  // `CHDLEVEL` changes what is served, by naming the heading level, and a
  // topic without one is headed at the top level.  10,501 of the 10,502
  // declared topics of the `BOO/` fixtures write all seven in one fixed
  // order -- the walk below admits exactly that sequence as before -- and
  // SC09-138 FRONT record 2 writes five of them (no `chdlevel`, no
  // `csourcefn`) before an empty `ST`; hosted BookServer serves it
  // (DT 19910321130500) as `<h1> FRONT </h1>` and an empty body.
  //
  // The envelope ends at the first segment that is not one of its controls
  // -- the title, a body control, or text -- and the body stream, which
  // declines visible text and metadata controls ahead of the title, decides
  // whether the topic is well formed from there.  Still fail-closed: a
  // metadata control written twice is declined (which one is meant would be
  // a guess the corpus never needs), and so is any envelope that does not
  // open with the topic-start control.
  if (records.front().control_segments.empty())
    return fail(error, "first record lacks the topic metadata envelope");
  std::vector<BookControlKind> seen;
  const auto metadata_kind = [&](const BookControlKind kind) {
    return std::find(required.begin() + 1, required.end(), kind) !=
           required.end();
  };
  const auto already_seen = [&](const BookControlKind kind) {
    return std::find(seen.begin(), seen.end(), kind) != seen.end();
  };
  // The reader skips every `CSUMMARY` line without reading it, so a summary
  // the source wrote across several lines is several controls of one kind.
  // EDCGV1R2 CONTENTS record 9 lines 5 and 6 are both `CSUMMARY CONTENTS
  // ...`; hosted BookServer serves neither (DT 19891031000000).  Every other
  // metadata control written twice still declines.
  const auto repeatable = [](const BookControlKind kind) {
    return kind == BookControlKind::summary;
  };
  // Where the next metadata control stands once the envelope interleavings
  // the walk consumes ahead of a control -- row slots, one `SRMSG`, `SR<id>`
  // anchors and revision-code definitions, in that order -- are passed over.
  // Non-mutating: the walk itself consumes them only when a metadata control
  // follows, so the ones that stand directly in front of the title stay with
  // the body stream, where they are read exactly as in a complete envelope.
  const auto next_metadata_kind = [&]() -> std::optional<BookControlKind> {
    const auto saved = cursor;
    const auto skip_slots = [&]() {
      while (!at_end() && row_control_slot_segment()) ++cursor.segment;
    };
    skip_slots();
    if (!at_end() && current().kind == BookControlKind::message_start)
      ++cursor.segment;
    while (!at_end() && envelope_anchor_segment(current())) ++cursor.segment;
    while (!at_end() &&
           (row_control_slot_segment() ||
            current().kind == BookControlKind::revision_definition))
      ++cursor.segment;
    skip_slots();
    std::optional<BookControlKind> kind;
    if (!at_end() && metadata_kind(current().kind)) kind = current().kind;
    cursor = saved;
    return kind;
  };
  Cursor heading_cursor;
  bool heading_seen = false;
  for (std::size_t index = 0;; ++index) {
    if (index != 0) {
      if (envelope.glued_title || envelope.untitled ||
          seen.size() + 1 == required.size())
        break;
      const auto next = next_metadata_kind();
      if (!next) break;
      if (already_seen(*next) && !repeatable(*next))
        return fail(error, "a topic metadata control is repeated");
    }
    if (at_end())
      return fail(error, "topic metadata controls are incomplete");
    // A record can break immediately before the optional SRMSG envelope
    // control.  The continued record then opens with SRMSG's native
    // display-line length byte, which the flattened splitter may leave in a
    // text segment of its own.  Consume carried row geometry before deciding
    // whether the next semantic control is SRMSG.  IBMMMSTR 1.5.420 records
    // 528/529 are the corpus witness: record 529 token 0 is value 0x04 at
    // [0x2e005,0x2e006), framed as the length of `SRMSG IEL0438I E`.
    while (!at_end() && row_control_slot_segment()) {
      const auto& slot = current();
      const auto slot_record = cursor.record;
      ++cursor.segment;
      for (const auto token : slot.source_tokens)
        if (!ledger.assign(slot_record, token, ProseTokenRoleIR::envelope,
                           error))
          return false;
    }
    if (at_end())
      return fail(error, "topic metadata controls are incomplete");
    // A message-section heading topic carries one bare `SRMSG` inside its
    // metadata (SC31-711 record 127, SC09-138 record 2066).  Individually
    // indexed message topics use the same envelope position with an operand
    // (`IBMMMSTR` record 1257: `SRMSG IBM002I`) and CHDLEVEL :MSGNO.  The
    // operand is a source anchor and metadata, never body prose.
    if (current().kind == BookControlKind::message_start) {
      const auto& message = current();
      const auto message_record = cursor.record;
      ++cursor.segment;
      if (at_end())
        return fail(error, "topic metadata controls are incomplete");
      if (message.operand_range.begin == message.operand_range.end) {
        // The payload may be nothing but the next display line's length
        // byte, which the flattened segment runs on into when no boundary
        // byte separates them: dfhg4a02 1.6 record 67 line 6 is `SRMSG`
        // alone and line 7 opens with the length byte `04` at 0x225CB,
        // spelled as a box-glyph run, before `chdlevel :H2`.  A length byte
        // is the row-control slot, never display text.
        const auto opcode_words = decoded_byte_range_to_word_range(
            records[message_record].assembled,
            {message.opcode_range.begin, message.opcode_range.end});
        const auto opcode_tokens = source_tokens_intersecting_output(
            records[message_record].assembled, opcode_words.begin,
            opcode_words.end);
        const auto only_length_bytes = std::all_of(
            message.source_tokens.begin(), message.source_tokens.end(),
            [&](const auto token) {
              return std::binary_search(opcode_tokens.begin(),
                                        opcode_tokens.end(), token) ||
                     is_display_line_length_token(records[message_record],
                                                  token) ||
                     is_padding(view_token(records, message_record, token));
            });
        if (message.payload_range.begin != message.payload_range.end &&
            !only_length_bytes)
          return fail(error, "bare SRMSG inside the envelope carries payload");
        if (!assign_segment_tokens(records, ledger, message_record, message,
                                   ProseTokenRoleIR::envelope, false, error))
          return false;
      } else {
        ProseAnchorIR anchor;
        if (!claim_envelope_message_anchor(records, ledger, message_record,
                                           message, anchor, error))
          return false;
        envelope.leading_anchors.push_back(std::move(anchor));
      }
    }
    // Envelope anchor variant: a bare `SR<id>` structural control can sit
    // between the metadata controls.  Hosted BookServer serves it as a plain
    // anchor immediately before the topic heading and prints nothing of its
    // own; a visible payload extends the served anchor name rather than
    // becoming body text.  Proven by `SRLEN <text>` between `csummary` and
    // `chdlevel` in SC24-546, SC33-033 and SC34-425: SC24-546 3.1 record 161
    // segment 6 (`SRLEN ADDRESS`, complete=[90,103)) is served as
    // `<a name="LEN ADDRESS"><a name="HDRADDRESS"><H2> 3.1   ADDRESS</H2>`
    // at DT 19940323131240; SC24-546 4.3.6 (`SRLEN` with no payload) is
    // served as `<a name="LEN">`; SC34-425 2.4.3 as
    // `<a name="LEN FLMCSPDB DB2 Bind/Free Translator">` at DT
    // 19921112160049; SC33-033 4.6 as `<a name="LEN CHAATT">` at DT
    // 19930422134757.  None of the payload text appears in the body.
    while (!at_end() && envelope_anchor_segment(current())) {
      const auto& anchor_segment = current();
      const auto anchor_record = cursor.record;
      ++cursor.segment;
      ProseAnchorIR anchor;
      if (!claim_envelope_anchor(records, ledger, anchor_record,
                                 anchor_segment, anchor, error))
        return false;
      envelope.leading_anchors.push_back(std::move(anchor));
    }
    // Revision-code definitions are envelope metadata wherever they occur
    // between the metadata controls; each occupies its own native display
    // line and draws no body text.  Definitions after the last metadata
    // control are left to the body stream, which reads them the same way:
    // SC09-138 FRONT record 2 carries eleven `c.rev <id> <bar>` lines
    // between `csummary` and its empty `ST`.
    while (!at_end()) {
      if (row_control_slot_segment()) {
        const auto &slot = current();
        const auto slot_record = cursor.record;
        ++cursor.segment;
        for (const auto token : slot.source_tokens)
          if (!ledger.assign(slot_record, token,
                             ProseTokenRoleIR::envelope, error))
            return false;
        continue;
      }
      if (current().kind != BookControlKind::revision_definition) break;
      const auto &revision = current();
      const auto revision_record = cursor.record;
      ++cursor.segment;
      if (revision.malformed)
        return fail(error, "revision definition is malformed");
      if (!assign_segment_tokens(records, ledger, revision_record, revision,
                                 ProseTokenRoleIR::envelope, false, error))
        return false;
    }
    // The envelope run continues across a record break, and the record it
    // continues into opens with a display line like every other record: its
    // first byte is that line's length byte.  The byte is row geometry and
    // never display text, whatever word the dictionary spells for it, so the
    // flattened splitter can open a segment on it in the middle of the
    // metadata run.  SC09-138 8.5.6.6 breaks after `cparent 8.5.6` and
    // record 1670 token 0 (encoded value 1, width 1) spells `.`, arriving as
    // a segment of its own ahead of `cforwardlevel`; DREICMST 1.5.4.1 breaks
    // after `cforwardlevel 1.5.4.2` and record 155 token 0 does the same
    // ahead of `cbacklevel`.  Whether a token is a length byte is read from
    // the decoder's stored framing, never re-derived here.
    while (!at_end() && row_control_slot_segment()) {
      const auto& slot = current();
      const auto slot_record = cursor.record;
      ++cursor.segment;
      for (const auto token : slot.source_tokens)
        if (!ledger.assign(slot_record, token, ProseTokenRoleIR::envelope,
                           error))
          return false;
    }
    if (at_end())
      return fail(error, "topic metadata controls are incomplete");
    const auto& segment = current();
    const auto& record = records[cursor.record];
    const auto record_index = cursor.record;
    const auto expected =
        index == 0 ? segment.kind == BookControlKind::topic_start
                   : metadata_kind(segment.kind) &&
                         (!already_seen(segment.kind) || repeatable(segment.kind));
    if (!expected) {
      // The one admissible substitution: a record-leading non-numeric topic
      // id whose `CTOPICN` corroboration sits in the next record.
      const auto corroborated_topic_start = [&]() {
        if (index != 0 ||
            !leading_topic_identifier_segment(records, record_index, segment))
          return false;
        Cursor probe = cursor;
        ++probe.segment;
        std::swap(probe, cursor);
        while (!at_end() && row_control_slot_segment()) ++cursor.segment;
        const auto next_is_topic_number =
            !at_end() && current().kind == BookControlKind::topic_number;
        std::swap(probe, cursor);
        return next_is_topic_number;
      }();
      if (!corroborated_topic_start)
        return fail(error,
                    "topic metadata controls are incomplete or out of order");
      // The id itself is envelope metadata, exactly as it is when the
      // classifier promotes it: hosted heads the topic with its catalogue
      // number and title and prints the id nowhere.
      ++cursor.segment;
      for (const auto token : segment.source_tokens) {
        const auto view = view_token(records, record_index, token);
        const auto role = is_padding(view) || is_row_control_slot(records, view)
                              ? ProseTokenRoleIR::padding
                              : ProseTokenRoleIR::envelope;
        if (!ledger.assign(record_index, token, role, error)) return false;
      }
      continue;
    }
    if (index != 0 && !already_seen(segment.kind)) seen.push_back(segment.kind);
    if (segment.kind == BookControlKind::heading_level) {
      heading_cursor = cursor;
      heading_seen = true;
    }
    ++cursor.segment;
    if (segment.malformed && segment.kind != BookControlKind::forward_level &&
        segment.kind != BookControlKind::back_level &&
        segment.kind != BookControlKind::parent)
      return fail(error, "topic metadata control is malformed");
    if (segment.kind == BookControlKind::source_file) {
      // `csourcefn <name> ? ST? <title>`: the title control can be glued
      // into the source-file segment (ACPZMST1 record 78).
      const auto operands = operand_tokens(record, segment);
      bool st_seen = false;
      // Where the record's display lines parse, `CSOURCEFN` owns exactly its
      // own display line.  When the line after it does not open with `ST`,
      // the topic has no title line and that line is the first body row:
      // EDCUSER 8.4.2.1-8.4.2.6 (records 1156-1176) write `CSOURCEFN
      // EDCULOCL` and then `   LC_CTYPE`, and the reader, which emits a
      // heading only for an `ST` line, serves no heading at all (hosted DT
      // 19900523000000).
      const auto* own_line =
          segment.source_tokens.empty()
              ? nullptr
              : display_line_of_token(record, segment.source_tokens.front());
      const auto next_line_opens_title = [&](std::size_t from) {
        for (const auto token : segment.source_tokens) {
          if (token < from) continue;
          const auto view = view_token(records, record_index, token);
          if (is_display_line_length_token(record, token) ||
              is_padding(view) || is_separator(view))
            continue;
          return ascii_lower(body_text(view)) == "st";
        }
        return true;
      };
      for (const auto token : segment.source_tokens) {
        const auto view = view_token(records, record_index, token);
        if (!st_seen && own_line != nullptr && token >= own_line->token_end &&
            !next_line_opens_title(token)) {
          for (const auto rest : segment.source_tokens)
            if (rest >= token) envelope.untitled_tokens.push_back(rest);
          envelope.untitled = true;
          envelope.untitled_record = record_index;
          break;
        }
        if (std::binary_search(operands.begin(), operands.end(), token)) {
          if (!ledger.assign(record_index, token, ProseTokenRoleIR::envelope,
                             error))
            return false;
          continue;
        }
        if (st_seen) {
          envelope.glued_title_tokens.push_back(token);
          continue;
        }
        // `csourcefn RBAFUP21 additional` (QSYSINFO record 273 segment 7):
        // `additional` is the length byte that opens the following `ST`
        // display line, not a second source-file operand.  Only the carried
        // framing separates the two -- the dictionary spelling is an
        // ordinary word.  Checked after the glued title, not before it: the
        // length bytes *inside* a glued title run (ACPZMST1 record 273,
        // whose `csourcefn` segment swallows four whole display lines) are
        // the title run's own framing, and diverting them out of it breaks
        // the run the title is rebuilt from.
        if (is_padding(view) || is_row_control_slot(records, view) ||
            is_separator(view) ||
            (view.width == 1 && punctuation_glyph_token(view))) {
          if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                             error))
            return false;
          continue;
        }
        if (ascii_lower(body_text(view)) == "st") {
          st_seen = true;
          envelope.glued_title_opcode = token;
          if (!ledger.assign(record_index, token, ProseTokenRoleIR::envelope,
                             error))
            return false;
          continue;
        }
        return fail(error, "control csourcefn carries visible payload '" +
                               body_text(view) + "'");
      }
      envelope.glued_title = st_seen;
      envelope.glued_title_record = record_index;
      continue;
    }
    if (!assign_segment_tokens(records, ledger, record_index, segment,
                               ProseTokenRoleIR::envelope, false, error))
      return false;
  }
  // A leading `SR<id>` anchor stands between `csourcefn` and the `ST` title,
  // and the flattened splitter glues the title line into the anchor's
  // segment.  ACPZMST1 record 72 spells the two apart exactly: display line
  // 8 is `SRHDRNETCMD` (tokens 26-26) and line 9 is
  // `ST| NetWare Domain Controller` (length byte 27, tokens 28-33), yet both
  // arrive as segment 8.  Read on the flattened string the anchor "carries
  // visible payload 'ST'" and the topic falls back to verbatim; read on the
  // carried framing the anchor ends where its own display line ends, and the
  // rest is the title -- the same hand-over `csourcefn` already performs for
  // the same shape one control earlier.
  //
  // Fail-closed in both directions: an unframed record decides nothing and
  // keeps the old reading, and a tail that does not open with `ST` is left
  // to the body stream, which declines it as before.
  while (!at_end() && !envelope.glued_title && !envelope.untitled &&
         envelope_anchor_segment(current())) {
    const auto& segment = current();
    const auto anchor_record = cursor.record;
    const auto& record = records[anchor_record];
    if (segment.source_tokens.empty()) break;
    const auto* line = display_line_of_token(record, segment.source_tokens.front());
    if (line == nullptr) break;
    std::vector<std::size_t> own;
    std::vector<std::size_t> tail;
    for (const auto token : segment.source_tokens)
      (token < line->token_end ? own : tail).push_back(token);
    if (own.empty() || tail.empty()) break;
    // The tail is a display line of its own; its leading structure (the
    // length byte, spacing, the marker slot) precedes the `ST` opcode.
    std::size_t st_at = npos;
    for (const auto token : tail) {
      const auto view = view_token(records, anchor_record, token);
      if (is_row_control_slot(records, view) || is_padding(view) ||
          is_separator(view) ||
          (view.width == 1 && punctuation_glyph_token(view)))
        continue;
      if (ascii_lower(body_text(view)) == "st") st_at = token;
      break;
    }
    if (st_at == npos) break;
    auto id = trim_ascii(token_run_text(records, anchor_record, own));
    if (id.size() < 3 || ascii_lower(id).rfind("sr", 0) != 0) break;
    id.erase(0, 2);
    id = trim_ascii(id);
    if (id.empty() ||
        !std::all_of(id.begin(), id.end(), [](const unsigned char ch) {
          return ch >= 0x20 && ch < 0x7F;
        }))
      break;
    for (const auto token : own) {
      const auto view = view_token(records, anchor_record, token);
      if (!ledger.assign(anchor_record, token,
                         is_padding(view) ? ProseTokenRoleIR::padding
                                          : ProseTokenRoleIR::envelope,
                         error))
        return false;
    }
    bool after_st = false;
    for (const auto token : tail) {
      if (after_st) {
        envelope.glued_title_tokens.push_back(token);
        continue;
      }
      const auto view = view_token(records, anchor_record, token);
      if (!ledger.assign(anchor_record, token,
                         token == st_at ? ProseTokenRoleIR::envelope
                                        : ProseTokenRoleIR::padding,
                         error))
        return false;
      if (token == st_at) after_st = true;
    }
    ProseAnchorIR anchor;
    anchor.id = std::move(id);
    anchor.source = token_slice(record, own.front(), own.back() + 1);
    envelope.leading_anchors.push_back(std::move(anchor));
    envelope.glued_title = true;
    envelope.glued_title_record = anchor_record;
    envelope.glued_title_opcode = st_at;
    ++cursor.segment;
  }
  // Without `CHDLEVEL` the topic is headed at the top level, under its title
  // or -- for an empty `ST` -- its topic id alone; the source form stays
  // empty, since the book declared none.
  at_end();
  envelope.body_record = std::min(cursor.record, records.size() - 1);
  envelope.body_segment_begin =
      cursor.record < records.size()
          ? cursor.segment
          : records.back().control_segments.size();
  if (!heading_seen) {
    envelope.heading_form.clear();
    envelope.heading_level = "h1";
    return true;
  }
  const auto& heading_record = records[heading_cursor.record];
  auto heading_level = ascii_lower(trim_ascii(operand_text(
      heading_record,
      heading_record.control_segments[heading_cursor.segment].operand_range)));
  // The reader copies the level from a fixed column of the line, the one
  // after `chdlevel` and the character that follows its blank, so that
  // character is skipped whatever it is: `:` almost everywhere, `.` in the
  // foil book GG24-4137-00 (`chdlevel .H3`, record 306 line 6), which hosted
  // (DT 19941221141316) heads `<H3> 2.21.1   FOIL: ...</H3>`.
  if (!heading_level.empty() &&
      (heading_level.front() == ':' || heading_level.front() == '.'))
    heading_level.erase(heading_level.begin());
  envelope.heading_form = heading_level;
  // :H0 is the chapter/Part level. HTML has no level zero; render it at
  // the top heading level while retaining the source form as evidence.
  if (heading_level == "h0") heading_level = "h1";
  // :MSGNO heads an individually indexed message at the lowest level:
  // hosted BookServer serves IBMMMSTR 1.5.420 (DT 19911004151140) as
  // `<h6> 1.5.420   IEL0438I E</h6>`, as it does in every sampled message
  // book (doc/boo-spec/markup.adoc, "Front-matter heading forms").
  if (heading_level == "msgno") heading_level = "h6";
  if (heading_level.size() != 2 || heading_level.front() != 'h' ||
      heading_level.back() < '1' || heading_level.back() > '6') {
    // The generated figure and table lists are the generated-list
    // family's; prose would reflow their rows into paragraphs.
    if (heading_level == "figlist" || heading_level == "tlist" ||
        !named_heading_form(heading_level))
      return fail(error, "heading level '" + heading_level +
                             "' is not an h1-h6 prose heading");
    heading_level = "h1";
  }
  envelope.heading_level = heading_level;
  return true;
}

// Collects the body stream from the first record's post-envelope segments and
// every later record.  Control tokens receive their ledger roles here; payload
// tokens are emitted as items for the display-line pass.
bool collect_stream(const std::vector<DecodedLogicalRecordSource>& records,
                    const Envelope& envelope, Ledger& ledger,
                    StreamBuild& build, std::string* error) {
  bool title_seen = false;
  // Set when the flattened splitter cut the title's own `ST` display line
  // right after the bare opcode: the record index and the line's end token.
  // The segment that carries the rest of the line is the title.
  std::optional<std::pair<std::size_t, std::size_t>> split_title;
  bool menu_open = false;
  // The display line (record, length-byte token) of the last word a trailing
  // subject-index term claimed after the menu.
  std::optional<std::pair<std::size_t, std::size_t>> index_term_line;
  // The verbatim `cz OFF` regions of the topic.  An `SRFIG`/`SRTBL` envelope
  // marker inside one is not an object envelope -- the span plan leaves it
  // alone (prose_topic_spans.cpp) -- so it reaches this pass as the bare
  // anchor hosted serves it as: SC41-4853-00 `1.2` (DT 19951003131222) emits
  // `<a name="FIGTBLUNIQ1">` and `<a name="TBLTBLUNIQ1">` on two rows of the
  // labelled box, and no `<table>`.
  const auto verbatim_regions = cz_verbatim_regions(records);
  // The opener and closer of a verbatim figure region
  // (`CzVerbatimRegion::figure`) take the region's own tag names, so the
  // builders read the region as they read every verbatim one.
  const auto name_verbatim_figure = [&](std::size_t first_item,
                                        std::size_t record_index,
                                        const ControlSegmentIR& segment) {
    if (segment.source_tokens.empty()) return;
    for (auto at = first_item; at < build.items.size(); ++at) {
      auto& directive = build.items[at].directive;
      if (build.items[at].kind != ItemKind::layout ||
          directive.mode != "off")
        continue;
      for (const auto& region : verbatim_regions) {
        if (!region.figure) continue;
        if (directive.tag == "fig" &&
            region.opener ==
                std::make_pair(record_index, segment.source_tokens.front()))
          directive.tag = "figverbatim";
        else if (directive.tag == "efig" &&
                 region.end ==
                     std::make_pair(record_index, segment.source_tokens.back()))
          directive.tag = "efigverbatim";
      }
    }
  };
  const auto box_markup = box_markup_envelopes(records);
  const auto box_leaders = box_leading_anchors(records);
  const auto box_figure_openers = box_leading_figure_openers(records);
  // Segments an earlier control read as its own operands (a glossary term
  // the splitter opened as a control, `SRGLS SI`).
  std::set<std::pair<std::size_t, std::size_t>> absorbed_segments;
  // The closed drawn boxes of the topic (prose_topic_boxes.cpp).
  const auto planned_boxes = plan_boxes(records);
  const auto in_closed_box = [&](std::size_t record_index,
                                 std::size_t token) {
    const std::pair<std::size_t, std::size_t> at{record_index, token};
    return std::any_of(planned_boxes.begin(), planned_boxes.end(),
                       [&](const BoxRegion& box) {
                         return box.closed &&
                                std::make_pair(box.begin_record,
                                               box.begin_token) <= at &&
                                at <= std::make_pair(box.end_record,
                                                     box.end_token);
                       });
  };
  // Which dialect the topic is written in: a body that carries any `CZ`
  // control names every block boundary explicitly, a body that carries none
  // reconstructs its blocks from row geometry (doc/boo-spec/markup.adoc, "CZ layout
  // directives").  The two spell footnotes differently, so the dialect has
  // to be known before the first `SRFTN<id>` rather than discovered at it.
  // A `cz` that is only a row's length byte names no block boundary.
  const auto cz_dialect = std::any_of(
      records.begin(), records.end(), [](const auto& source) {
        return std::any_of(source.control_segments.begin(),
                           source.control_segments.end(),
                           [&](const auto& segment) {
                             return segment.kind ==
                                        BookControlKind::layout_directive &&
                                    !is_display_line_length_opcode(source,
                                                                   segment);
                           });
      });
  // `SRFTN<id>` of the CZ dialect names the footnote the next `cz FLOW FN`
  // directive opens (packet 1.1 record 17).
  std::string pending_footnote_id;
  // Where in the item stream that anchor stood, and its source: a footnote
  // that `SREFTN` closes before any `cz FLOW FN` opened it is a footnote all
  // the same, opened at the anchor (see `srftn` below).
  std::size_t pending_footnote_item = 0;
  DocumentSourceSliceIR pending_footnote_source;
  // Lower-cased owners of the topic's `SRLIS<owner>` list-item anchors; an
  // `SRELIS<owner>` naming one of them is no anchor (see below).
  std::set<std::string> list_owners;
  // Lower-cased owners the topic's `SRELIS<owner>` closers name.
  std::set<std::string> closed_list_owners;
  // Lower-cased targets of the topic's in-book selectors.
  std::set<std::string> selector_targets;
  // `SRETBL` table ends that no `SRTBL<id>` earlier in the topic opens, as
  // (record index, segment index) (see the unopened `SRETBL` below).
  std::set<std::pair<std::size_t, std::size_t>> unopened_table_ends;
  bool table_started = false;
  for (const auto& source : records)
    for (const auto& segment : source.control_segments) {
      if (segment.kind == BookControlKind::table_start) table_started = true;
      if (segment.kind == BookControlKind::table_end && !table_started &&
          !segment.malformed)
        unopened_table_ends.insert(
            {static_cast<std::size_t>(&source - records.data()),
             static_cast<std::size_t>(&segment -
                                      source.control_segments.data())});
      if (segment.kind == BookControlKind::select && !segment.malformed) {
        std::size_t column = 0;
        std::size_t length = 0;
        std::string target;
        if (parse_selector_operand(
                operand_text(source, segment.operand_range), column, length,
                target))
          selector_targets.insert(ascii_lower(target));
      }
      const auto lower = ascii_lower(segment.opcode);
      if (!segment.malformed && lower.rfind("srlis", 0) == 0 &&
          lower.size() > 5)
        list_owners.insert(lower.substr(5));
      if (!segment.malformed && lower.rfind("srelis", 0) == 0 &&
          lower.size() > 6)
        closed_list_owners.insert(lower.substr(6));
    }
  // An owner may begin with `t`, which spells the opener `SRLIST<rest>` and
  // the closer `SRELIST<rest>`, the reserved `SRLIST`/`SRELIST` prefixes.  No
  // `SRLIST` control of its own has been observed; an opener and a closer
  // of the same topic naming the same owner are what prove the pair to be a
  // list item.  FA1PLMM0 D.0 record 817 (`SRLIStemppar`, `SRELIStemppar`),
  // SC24-5520-00 1.1.7 records 61/62 (`SRLIStimzon`, `SRELIStimzon`) and
  // OFCUSEOV 3.1.1 record 516 (`SRLIStres`, `SRELIStres`): hosted
  // BookServer serves `<a name="LIStemppar">` (DT 19910927114801),
  // `LIStimzon` (DT 19911011135123) and `LIStres` (DT 19900805103816), each
  // wrapping the item's first row, and no `ELIS*` anchor.  Either half
  // without the other keeps the reserved reading and declines.
  //
  // An opener without a closer is proven the same way by an in-book selector
  // of the topic that targets its anchor `LIST<rest>`.  SC21-8295-03 `E.0`
  // record 840 stores a lone `SRLISTWOB`, and records 837 and 840 carry
  // `cselect 69 2 LISTWOB` and `cselect 52 2 LISTWOB` ("go to Step 2b");
  // hosted BookServer (`SC21-829`, DT 19970409135933) serves
  // `<a name="LISTWOB">` around item b's first row and both selectors as
  // `#LISTWOB`.  The comparison ignores case: the topic's other selectors
  // spell `LISWQ1543` for the opener `SRLISwq1543`.
  const auto paired_list_item = [&](const std::string& lower) {
    if (lower.rfind("srelis", 0) == 0)
      return lower.size() > 6 && list_owners.count(lower.substr(6)) != 0;
    if (lower.rfind("srlis", 0) == 0)
      return lower.size() > 5 &&
             (closed_list_owners.count(lower.substr(5)) != 0 ||
              selector_targets.count(lower.substr(2)) != 0);
    return false;
  };
  // Set once a `cz` directive has been collected: unclaimed one-cell
  // decoder separators then reach the display-row pass, which decides
  // between bullet slot, split-off text and padding.
  bool cz_seen = false;
  // The open BUILD 1.3 artwork description (`csartdesc <n>` .. `ceartdesc`)
  // standing in prose: the lower-cased resource id of the picture it
  // describes, or empty outside one.
  std::string art_description_picture;
  // Anchors that stood among the topic metadata controls precede every
  // anchor the body contributes, in source order.
  build.leading_anchors = envelope.leading_anchors;
  // A glued `ST` display line that holds the opcode alone is an empty title
  // (see the `ST` case below); the rows glued after it are body.  SC38-3058-02
  // 11.2.4.17 record 474: `csourcefn ADNL2PRO` and `ST` (token 28, alone on
  // line 9) share a flattened segment with two empty rows and a drawn rule.
  std::vector<std::size_t> glued_body;
  if (envelope.glued_title && envelope.glued_title_opcode != npos) {
    const auto& glued_record = records[envelope.glued_title_record];
    const auto* line =
        display_line_of_token(glued_record, envelope.glued_title_opcode);
    bool line_payload = false;
    bool beyond_line = false;
    if (line != nullptr)
      for (const auto token : envelope.glued_title_tokens) {
        if (token >= line->token_end) {
          beyond_line = true;
          continue;
        }
        const auto view =
            view_token(records, envelope.glued_title_record, token);
        if (is_visible(view) && !is_padding(view) && !is_placeholder_run(view))
          line_payload = true;
      }
    if (line != nullptr && beyond_line && !line_payload) {
      std::vector<std::pair<std::size_t, std::size_t>> refs{
          {envelope.glued_title_record, envelope.glued_title_opcode}};
      for (const auto token : envelope.glued_title_tokens) {
        if (token >= line->token_end) {
          glued_body.push_back(token);
          continue;
        }
        refs.push_back({envelope.glued_title_record, token});
        if (!ledger.assign(envelope.glued_title_record, token,
                           ProseTokenRoleIR::padding, error))
          return false;
      }
      const auto slices = slices_for(records, refs);
      if (slices.empty())
        return fail(error, "ST control has no source provenance");
      auto slice = slices.front();
      slice.token_end = slices.back().token_end;
      slice.byte_end = slices.back().byte_end;
      build.empty_title_source = slice;
    }
  }
  if (envelope.glued_title && !glued_body.empty()) {
    title_seen = true;
    Item title_end;
    title_end.kind = ItemKind::segment_end;
    title_end.empty_title = true;
    build.items.push_back(std::move(title_end));
    for (const auto token : glued_body) {
      Item item;
      item.kind = ItemKind::token;
      item.token = view_token(records, envelope.glued_title_record, token);
      build.items.push_back(std::move(item));
    }
    Item end;
    end.kind = ItemKind::segment_end;
    build.items.push_back(std::move(end));
  } else if (envelope.glued_title) {
    title_seen = true;
    bool first_payload = true;
    for (const auto token : envelope.glued_title_tokens) {
      Item item;
      item.kind = ItemKind::token;
      item.token = view_token(records, envelope.glued_title_record, token);
      item.title_start = first_payload;
      first_payload = false;
      build.items.push_back(std::move(item));
    }
    Item end;
    end.kind = ItemKind::segment_end;
    build.items.push_back(std::move(end));
  }
  if (envelope.untitled) {
    // No title line: the (absent) title is complete before the first body
    // row, which the envelope handed over from the `CSOURCEFN` segment.
    title_seen = true;
    build.untitled = true;
    Item done;
    done.kind = ItemKind::segment_end;
    done.empty_title = true;
    build.items.push_back(std::move(done));
    for (const auto token : envelope.untitled_tokens) {
      Item item;
      item.kind = ItemKind::token;
      item.token = view_token(records, envelope.untitled_record, token);
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
        record_index < envelope.body_record
            ? record.control_segments.size()
            : (record_index == envelope.body_record
                   ? envelope.body_segment_begin
                   : std::size_t{0});
    std::vector<bool> claimed(record.ir.tokens.size(), false);
    if (envelope.glued_title && record_index == envelope.glued_title_record)
      for (const auto token : envelope.glued_title_tokens)
        if (token < claimed.size()) claimed[token] = true;
    if (envelope.untitled && record_index == envelope.untitled_record)
      for (const auto token : envelope.untitled_tokens)
        if (token < claimed.size()) claimed[token] = true;
    // Tokens claimed by no segment are inter-control separators.  Inside the
    // body they carry layout state (a bare spacing token before a control is
    // the paragraph break, a placeholder run the row slot), so they enter the
    // stream in source order; before the title they are envelope padding.
    std::size_t cursor = 0;
    // A one-cell decoder separator claimed by no segment is display text
    // only when row text follows it directly (a space run or a visible
    // word that is not itself a separator or placeholder); before a control
    // opcode or a placeholder slot it is decoder punctuation.
    // Every token any control segment owns, known before the segments are
    // walked: `claimed` only fills in as the walk proceeds, so a separator
    // sitting directly before the next control opcode would otherwise see
    // that opcode as row text (SC41-485 1.3.3 record 50 token 132 `,` before
    // `cmenu`, which hosted does not print).
    std::vector<bool> owned_by_segment(record.ir.tokens.size(), false);
    for (const auto& segment : record.control_segments)
      for (const auto token : segment.source_tokens)
        if (token < owned_by_segment.size()) owned_by_segment[token] = true;
    const auto row_text_follows = [&](std::size_t token) {
      const auto next = token + 1;
      if (next >= record.ir.tokens.size() || claimed[next] ||
          owned_by_segment[next])
        return false;
      const auto view = view_token(records, record_index, next);
      return is_space_run(view) ||
             (is_visible(view) && !is_placeholder_run(view) &&
              !is_separator(view));
    };
    const auto emit_unclaimed = [&](std::size_t end) -> bool {
      for (; cursor < end && cursor < record.ir.tokens.size(); ++cursor) {
        const auto token = cursor;
        if (claimed[token] ||
            ledger.at(record_index, token).role != ProseTokenRoleIR::unassigned)
          continue;
        const auto view = view_token(records, record_index, token);
        // A list bullet standing between two controls is display structure,
        // not padding: GG24-4302-00 PREFACE.2 continues its bibliography list
        // across a record boundary, so record 31's first display line
        // (`   °   IMS/ESA V5 Release Planning Guide, GC26-8031`, tokens
        // 0..4: length byte, three-cell origin, `U+2666`, two-cell gap,
        // `IMS`) reaches the stream before that record's first control.
        // Dropping the glyph moved the row's text from column 7 to column 2
        // and displaced every `cfont 7 7 C 15 2 C ...` column of the row,
        // while the identical rows inside a control payload kept it.  Hosted
        // DT 19950308184737 serves this row exactly like its siblings.
        if (title_seen && !menu_open &&
            (is_bare(view) || is_space_run(view) || is_placeholder_run(view) ||
             is_bullet_glyph(view) ||
             (cz_seen && is_separator(view) && view.body.size() == 1 &&
              ((view.width == 2 && view.body.front() == '?') ||
               row_text_follows(token))))) {
          Item item;
          item.kind = ItemKind::token;
          item.token = view;
          item.separator = true;
          build.items.push_back(std::move(item));
          claimed[token] = true;
          continue;
        }
        // A `,` or `?` stored as a two-byte dictionary word and standing as
        // the last token of its native display line, directly in front of
        // the next control's line, is spelled like decoder punctuation but
        // may be the row's last displayed cell.  Hosted serves it where the
        // row's CFONT names its column: SC24-546 D.0 record 1118 line 7
        // `              LTORG ,` under `cfont 14 5 E 20 1 E` (DT
        // 19940323131240, `<samp>LTORG</samp> <samp>,</samp>`), IEAC6MST
        // 4.8.2 record 355 line 3 ending `LOCATE ?` under `... 70 1 E` (DT
        // 19920124000100), GG24-4302-00 4.3.3.2 record 355 line 0 ending
        // `COPY1) ?` under `... 86 1 E` (DT 19950308184737).  The stream
        // carries it as a candidate; the row model admits it only under that
        // exact span (`styled_terminal`) and keeps it padding otherwise.
        // The word can also be stored in one byte: SC09-1714-00 1.4 record
        // 221 line 5 ends `source_binding ,` with token 78 (value 29, inside
        // the line) under the row's own `cfont ... 47 1 P`, and hosted (DT
        // 19940917171754) serves `<var>source_binding</var> <kbd>,</kbd>`.
        // The framing check below keeps a length byte out.
        if (title_seen && !menu_open && !cz_seen &&
            (view.width == 2 || view.width == 1) &&
            view.body.size() == 1 &&
            (view.body.front() == ',' || view.body.front() == '?')) {
          const auto* line = display_line_of_token(record, token);
          if (line != nullptr && token != line->prefix_token &&
              token + 1 == line->token_end) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view;
            item.separator = true;
            item.styled_terminal = true;
            build.items.push_back(std::move(item));
            claimed[token] = true;
            continue;
          }
        }
        // A one-cell `,` or `.` attached (by its leading glue byte) to the
        // word before it, inside a framed display line, is that line's last
        // drawn cell, not decoder punctuation: the decoder's own separator
        // before a control stands on the next line's length byte instead
        // (SC41-485 1.3.3 record 50 token 132, `02` = line 21's length).
        // ehxb7000 BACK_1.2.7.1 record 424 token 190 (`02` at 0x9B172, glue
        // + `,`) ends line 9 `... Support OSI Network Service,` before the
        // `cfont` line; the row's text is incomplete without it.
        if (title_seen && !menu_open && is_separator(view) &&
            view.width == 1 && view.body.size() == 1 &&
            (view.body.front() == ',' || view.body.front() == '.') &&
            token < record.tokens.size() && !record.tokens[token].empty() &&
            record.tokens[token].front() < 4) {
          const auto* line = display_line_of_token(record, token);
          if (line != nullptr && token > line->prefix_token + 1 &&
              !is_display_line_length_token(record, token)) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view;
            item.separator = true;
            build.items.push_back(std::move(item));
            claimed[token] = true;
            continue;
          }
        }
        // A display line's length byte is the row-control slot, always and
        // only.  It is a raw byte the token reader resolves through the
        // dictionary, so it routinely spells an ordinary word -- SC26-457
        // record 543 line 13 opens with the byte 33, which the book's
        // dictionary spells `-`.  Once a control's payload ends at its own
        // display line the byte belongs to no segment, and reading its
        // spelling would decline the topic over a byte hosted never prints.
        // Checked last, so a byte that already reads as row structure keeps
        // the role the row geometry is built from.
        if (is_row_control_slot(records, view)) {
          if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                             error))
            return false;
          continue;
        }
        // An arrow word inside a display line is a drawn cell of that row.
        // The flattened splitter reads it through the ASCII projection,
        // which spells it `?`, and trims it from the end of the segment it
        // closes, so no segment owns it (DREICMST 1.5.5.4 record 162 token
        // 310, `U+2193` at 0x1B68D, the last cell of line 37).  The stream
        // carries it in source order; the line builder draws it inside a
        // proven drawing region and declines it anywhere else.  The CZ
        // dialect stores the same words in the rows of its verbatim regions
        // (SC41-4870-00 1.10.3.1 record 100 line 15 under `cz OFF
        // CGRAPHIC`), where the line builder draws them as region cells; a
        // token may join box words to the arrow (SC26-4721-01 16.3 record
        // 827 token 12, `U+2514 U+2500 U+2192 U+2502`).
        const auto arrow_word = [](std::uint16_t word) {
          return word >= 0x2190 && word <= 0x2193;
        };
        if (title_seen && !menu_open && !view.body.empty() &&
            std::all_of(view.body.begin(), view.body.end(),
                        [&](const auto word) {
                          return arrow_word(word) ||
                                 box_word(word);
                        }) &&
            std::any_of(view.body.begin(), view.body.end(), arrow_word)) {
          const auto* line = display_line_of_token(record, token);
          if (line != nullptr && token != line->prefix_token) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view;
            item.drawn_arrow = true;
            build.items.push_back(std::move(item));
            claimed[token] = true;
            continue;
          }
        }
        if (!is_padding(view) && !is_separator(view))
          return fail(error, "unclaimed visible token '" +
                                 diagnostic_text(view) + "' (token " +
                                 std::to_string(token) + ") in record " +
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
      if (absorbed_segments.count({record_index, segment_index}) != 0)
        continue;
      const auto lower_opcode = ascii_lower(segment.opcode);
      // The decoded-string splitter opens a segment wherever a word is
      // spelled like a control.  A word is only a control when the record
      // encoder wrote a boundary before it: a decoder-separator token (a
      // box/unmapped glyph run) or the compact separator token (the attach
      // control plus `,` or `.`).  Byte-level evidence: ACPZMST1 CONTENTS
      // record 18 writes `ST` after token 24 (glyph U+2514) and `ctocdef=0`
      // after token 30 (glyph U+2518); SC24-546 record 961 writes
      // `SRHDRIRRR` after the compact `,` that closes `csourcefn DMSB1IRR`.
      // Prose words that merely look like controls carry no such boundary:
      // PRG1SORT 1.1.5.1 record 80 token 2 `SRCFILE` follows an 18-cell space
      // run and is the CL parameter `SRCFILE(LIBRAR2/FILE3)` (hosted DT
      // 19900829171904 serves `<tt>SRCFILE(LIBRAR2/FILE3)</tt>`), and
      // SC24-546 14.0 record 961 spells the routine names `SRRCMIT or
      // SRRBACK.` inside a sentence (hosted DT 19940323131240 serves them as
      // ordinary words).
      const auto boundary_before = [&]() {
        if (segment.source_tokens.empty()) return true;
        const auto first = segment.source_tokens.front();
        if (first == 0) return true;
        const auto before = view_token(records, record_index, first - 1);
        // Decoder separators: a box/unmapped glyph run, the bullet glyph
        // (QSYSINFO GLOSSARY record 756 token 189 stands between the previous
        // entry and `SRGLS AFP`), or the compact separator token.
        if (is_placeholder_run(before) || is_bullet_glyph(before)) return true;
        return before.has_prefix && before.prefix == 1 &&
               before.body.size() == 1 &&
               (before.body.front() == ',' || before.body.front() == '.');
      };
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
        // A `cfont` control between the frame's own lines styles the prose
        // the stream lowers there, exactly as outside a frame; its opcode
        // and operands are never body text.  SC24-5527-02 COMMENTS styles
        // the form's paragraphs and its `Note` box this way inside
        // `SRFIGFIGUNIQ548` (hosted DT 19921218151459 prints the words in
        // `<B>` and no `cfont`).
        if (region == nullptr && frame != nullptr &&
            segment.kind == BookControlKind::font &&
            !(frame->begin_record == record_index &&
              frame->begin_segment == segment_index) &&
            !(frame->end_record == record_index &&
              frame->end_segment == segment_index))
          frame = nullptr;
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
          // Set once a display line's length byte has entered the stream:
          // every token after it is that row's content, its origin run
          // included, never spacing glued to the end marker.
          bool row_opened = false;
          for (const auto token : segment.source_tokens) {
            if (ledger.at(record_index, token).role !=
                ProseTokenRoleIR::unassigned)
              continue;
            const auto view = view_token(records, record_index, token);
            // After a picture-less frame's end marker a display line's
            // length byte is row structure, never spacing to drop: the
            // display-line pass reads the byte of an empty line as the
            // paragraph break it is everywhere else in the body, and any
            // other byte as the control slot that opens its row, whatever
            // word the dictionary spells for it.  ITPPIBOK 4.3.5.4 record 160
            // closes the `SRFIGSCHED` frame with `SREFIG` alone on line 6
            // (token 40); line 7 is empty (its length byte, token 41, is
            // `00`) and line 8 opens `Note that ...`, and the flattened
            // splitter glues lines 7-11 to the `SREFIG` segment.  Hosted DT
            // 19910628074854 serves the caption `Figure 2. ...`, then `<p>`,
            // then `Note that ...`; dropping the bytes as padding ran the
            // caption on into that paragraph.  DREICMST 2.17.1.1 record 524
            // is the same shape after the `Figure 94.` caption (hosted DT
            // 19911219125856 again serves `<p>`), and there the length byte
            // of line 22, token 267, spells `U+251C`, a decoder placeholder
            // the old padding rule swallowed.  A table or figure span's end
            // marker is followed the same way: GG24-395 3.3.15 record 635
            // closes `SRTBLTBLUNIQ21` with `SRETBL` alone on line 28, line
            // 29 is empty and line 30 opens ` | °   Need to reduce ...`
            // (length byte `14` at 0x13FED7); QSYSNEWG A.4 record 320 has
            // the same shape after `SREFIG`.  Hosted (DTs 19941215160749,
            // 19910524085706) prints the first item at its siblings'
            // columns, bullet and all; dropping the length byte and the
            // origin run behind it lost the bullet and declined the list.
            if (first_payload &&
                is_display_line_length_token(records[record_index], token)) {
              Item item;
              item.kind = ItemKind::token;
              item.token = view;
              build.items.push_back(std::move(item));
              row_opened = true;
              continue;
            }
            // A box of the frame's prose draws its own rows
            // (`FrameRegion::prose_boxes`), never padding: SC24-5527-02
            // COMMENTS record 978 opens with the bottom rule of the form's
            // `Note` box.
            const auto box_row = region == nullptr && frame != nullptr &&
                                 frame->in_prose_box(record_index, token);
            // Spacing and the punctuation the decoder glues to the end
            // marker (`SREFIG.`, GG24-4302-00 5.1.8) precede any payload.
            if (first_payload && !box_row && !row_opened &&
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
                                    bool continuation,
                                    std::size_t control_payload = 0) -> bool {
        const auto operands = operand_tokens(record, segment);
        bool first_payload = true;
        for (const auto token : segment.source_tokens) {
          if (std::binary_search(operands.begin(), operands.end(), token)) {
            if (!ledger.assign(record_index, token, control_role, error))
              return false;
            continue;
          }
          // Leading payload tokens the control itself owns (the `<...>`
          // alternatives of a LNK selector); never display text.
          if (control_payload != 0) {
            --control_payload;
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

      // A row's length byte the dictionary spells as a BUILD 1.3 artwork
      // opcode opens no control (as in the layout_directive case below):
      // dfht4c00 4.5.3 record 411 frames a `cz OFF XMP` row with a byte
      // spelled `cartdesc`.  The byte re-enters the token stream as the
      // row's control slot.
      const auto art_length_byte_row = [&]() {
        if (!title_seen || !is_display_line_length_opcode(record, segment))
          return false;
        for (const auto token : segment.source_tokens) {
          Item item;
          item.kind = ItemKind::token;
          item.token = view_token(records, record_index, token);
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
        // A term word spelled like a control splits the `SI` line it stands
        // on: LAA5MST 1.3.9.1 record 228 line 40 is `SI synonyms used to
        // sort FILELIST, SRECF`, and the flattened splitter opens an `SRECF`
        // segment on its last word; qb3agz01 3.6.1.3 record 1441 line 28 is
        // `SI SR (subroutine identifier)`.  The reader skips the whole `SI`
        // line, so these words are the term's, never an anchor or body.
        if (!segment.source_tokens.empty() &&
            !build.trailing_index_terms.empty() && index_term_line) {
          const auto opcode_token = segment.source_tokens.front();
          const auto* line = display_line_of_token(record, opcode_token);
          if (line != nullptr && opcode_token > line->prefix_token + 1 &&
              *index_term_line ==
                  std::make_pair(record_index, line->prefix_token) &&
              is_subject_index_display_line(record, *line)) {
            auto& term = build.trailing_index_terms.back();
            std::vector<std::pair<std::size_t, std::size_t>> refs;
            for (const auto token : segment.source_tokens) {
              const auto view = view_token(records, record_index, token);
              if (token >= line->token_end) {
                if (!is_row_control_slot(records, view))
                  return fail(error, "content follows the trailing menu");
                if (!ledger.assign(record_index, token,
                                   ProseTokenRoleIR::padding, error))
                  return false;
                continue;
              }
              if (is_bare(view) || is_space_run(view) ||
                  is_placeholder_run(view) || is_padding(view)) {
                if (!ledger.assign(record_index, token,
                                   ProseTokenRoleIR::padding, error))
                  return false;
                continue;
              }
              if (!term.term.empty()) term.term.push_back(' ');
              term.term += body_text(view);
              refs.emplace_back(record_index, token);
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::index_term, error))
                return false;
            }
            term.term = collapse_ascii_whitespace(term.term);
            for (auto& slice : slices_for(records, refs))
              term.slices.push_back(std::move(slice));
            continue;
          }
        }
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
            // A trailing subject-index run crosses display lines, so it
            // walks over the length bytes that open them.  Such a byte is
            // the row-control slot: it neither opens an `SI` entry when its
            // dictionary spelling happens to be `si`, nor contributes a word
            // to the term when it spells one (QSYSNEWG record 232 token 0 is
            // the byte 51, spelled `any`).
            if (is_row_control_slot(records, view)) {
              if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                                 error))
                return false;
              continue;
            }
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
              if (const auto* line = display_line_of_token(record, token))
                index_term_line = std::make_pair(record_index,
                                                 line->prefix_token);
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
            if (const auto* line = display_line_of_token(record, token))
              index_term_line = std::make_pair(record_index,
                                               line->prefix_token);
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
        if (segment.kind == BookControlKind::layout_directive &&
            is_display_line_length_opcode(record, segment)) {
          // A row's length byte spelled `cz` (see the layout_directive case
          // below): it opens no directive, and the row it frames may carry
          // no text after the menu any more than any other row.
          for (const auto token : segment.source_tokens) {
            const auto view = view_token(records, record_index, token);
            if (is_visible(view) && !is_row_control_slot(records, view) &&
                !is_placeholder_run(view) && !is_padding(view))
              return fail(error, "content follows the trailing menu");
          }
          continue;
        }
        if (segment.kind == BookControlKind::layout_directive) {
          // The compiled menu sits inside the last open CZ list; its closer
          // and the next-level announcement follow it (SC09-2417-00 2.1
          // `cz OFF EUL 0 0`, `cz FLOW H2 3 3`).  They carry no text.
          // Their payload can include the next row's length token, whose
          // dictionary spelling need not be blank (GA19-5486-13 4.0: '#').
          const auto before = build.items.size();
          if (!collect_layout_directive(records, record_index, segment,
                                        pending_footnote_id, ledger,
                                        build.items, error))
            return false;
          // SC41-3425-00 2.1.4 record 240 closes its figure region with
          // `cz OFF EFIG 0 0` right after the trailing menu's `cemenu`.
          name_verbatim_figure(before, record_index, segment);
          for (auto index = before; index < build.items.size(); ++index)
            if (build.items[index].kind == ItemKind::token &&
                is_visible(build.items[index].token) &&
                !is_row_control_slot(records, build.items[index].token) &&
                !is_placeholder_run(build.items[index].token))
              return fail(error, "content follows the trailing menu");
          continue;
        }
        // Inside the menu, a row's length byte that spells a metadata or
        // font opcode opens no control either: SC09-2540-01 2.1.7.5 record
        // 788 token 267 frames the `cmitem 2.1.7.5.3 ...` row (line 34) and
        // spells `cbacklevel`; dfheba03 1.3.4.2 record 207 opens on a byte
        // spelled `CPARENT` that frames `cmitem 1.3.4.2.4 ...`.  The reader
        // reads the row, an item, and hosted (SC09-2540-01 DT
        // 19990312111603) lists every item and nothing else.  Any visible
        // token the segment carries must sit on a menu control's own row,
        // where the menu pass owns it.
        if (is_display_line_length_opcode(record, segment)) {
          for (const auto token : segment.source_tokens) {
            const auto view = view_token(records, record_index, token);
            if (is_row_control_slot(records, view) || !is_visible(view) ||
                is_padding(view) || is_placeholder_run(view) ||
                on_menu_control_row(records, record_index, token))
              continue;
            return fail(error, "content follows the trailing menu");
          }
          continue;
        }
        return fail(error, "content follows the trailing menu");
      }

      if (split_title && segment.kind != BookControlKind::text)
        return fail(error, "the rest of a split ST title line is a control");
      switch (segment.kind) {
      case BookControlKind::title: {
        if (segment.malformed) return fail(error, "ST control is malformed");
        // A title line after a body that opened without one is a shape no
        // book has shown; decline rather than guess its heading.
        if (build.untitled)
          return fail(error, "ST line follows a topic body with no title");
        if (title_seen) {
          // Every `ST` line heads the topic: the reader keeps no "title
          // already printed" state, so a second one prints a second heading
          // at the topic's level under the same number.  SC31-8212-00 2.3.3
          // record 855 line 14 is `ST  Purpose` (tokens [43,46)) after the
          // empty `ST` of line 10, the row `   open` and `SRHDRHWEOP6`;
          // hosted (DT 19950906051034) serves `<H2> 2.3.3 </H2>`,
          // `   <B>open</B>` and `<a name="HDRHWEOP6"><H2> 2.3.3
          // Purpose</H2></a>`.  The heading's words are exactly the `ST`
          // display line's; what the segment carries past that line is body.
          const auto operands = operand_tokens(record, segment);
          if (segment.source_tokens.empty() || operands.empty())
            return fail(error, "second ST control has no opcode");
          const auto* line =
              display_line_of_token(record, segment.source_tokens.front());
          if (line == nullptr)
            return fail(error, "second ST control has no display line");
          ProseRepeatedTitleIR repeated;
          std::vector<std::pair<std::size_t, std::size_t>> refs;
          std::vector<std::size_t> rest;
          // Word spacing as the first title's (prose_topic_lines.cpp): a
          // blank run is a space, and a word's attach prefix decides whether
          // the next word is separated from it.
          bool space = false;
          bool pending_space = false;
          bool after_opcode = false;
          for (const auto token : segment.source_tokens) {
            if (std::binary_search(operands.begin(), operands.end(), token)) {
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::envelope, error))
                return false;
              after_opcode = true;
              continue;
            }
            if (token >= line->token_end) {
              rest.push_back(token);
              continue;
            }
            const auto view = view_token(records, record_index, token);
            if (is_padding(view) || !is_visible(view)) {
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::padding, error))
                return false;
              space = space || is_space_run(view);
              continue;
            }
            // A mark glued to the opcode is the revision column, which the
            // document model does not carry; decline rather than print it
            // as a title word.
            if (after_opcode && repeated.title.empty() && !space &&
                punctuation_glyph_token(view))
              return fail(error, "second ST control carries a revision mark");
            if (!repeated.title.empty() &&
                (space ||
                 (pending_space && view.prefix != 0 && view.prefix != 1)))
              repeated.title.push_back(' ');
            repeated.title += body_text(view);
            space = false;
            pending_space = view.prefix != 2;
            refs.push_back({record_index, token});
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::title,
                               error))
              return false;
          }
          if (repeated.title.empty())
            return fail(error, "second ST control has no title");
          const auto slices = slices_for(records, refs);
          if (slices.empty())
            return fail(error, "second ST title has no source provenance");
          repeated.source = slices.front();
          repeated.source.token_end = slices.back().token_end;
          repeated.source.byte_end = slices.back().byte_end;
          Item mark;
          mark.kind = ItemKind::span;
          mark.span_index = build.span_base + build.repeated_titles.size();
          build.items.push_back(std::move(mark));
          build.repeated_titles.push_back(std::move(repeated));
          for (const auto token : rest) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            build.items.push_back(std::move(item));
          }
          Item end;
          end.kind = ItemKind::segment_end;
          build.items.push_back(std::move(end));
          break;
        }
        title_seen = true;
        // The title is the `ST` display line and nothing else; a bare `ST`
        // line is an empty title even when the flattened segment that opens
        // on it runs on across the next rows.  GC24-5611-01 FRONT record 2
        // line 6 is `ST` alone (token 18) and line 7 the cover's drawn rule;
        // SC38-3058-02 11.2.4.17 record 474 line 9 is `ST` alone, followed by
        // two empty rows and a rule.  Hosted serves `<h1> FRONT </h1>`
        // (DT 19931102100502) and `<MSGNO> 11.2.4.17 </MSGNO>`
        // (DT 19960430074838), each followed by those rows as body.
        if (!segment.source_tokens.empty()) {
          const auto* line =
              display_line_of_token(record, segment.source_tokens.front());
          const auto operands = operand_tokens(record, segment);
          bool line_payload = false;
          bool beyond_line = false;
          if (line != nullptr)
            for (const auto token : segment.source_tokens) {
              if (std::binary_search(operands.begin(), operands.end(), token))
                continue;
              if (token >= line->token_end) {
                beyond_line = true;
                continue;
              }
              const auto view = view_token(records, record_index, token);
              if (is_visible(view) && !is_padding(view) &&
                  !is_placeholder_run(view))
                line_payload = true;
            }
          if (line != nullptr && beyond_line && !line_payload) {
            std::vector<std::pair<std::size_t, std::size_t>> refs;
            std::vector<std::size_t> rest;
            for (const auto token : segment.source_tokens) {
              if (token >= line->token_end) {
                rest.push_back(token);
                continue;
              }
              refs.push_back({record_index, token});
              if (!ledger.assign(
                      record_index, token,
                      std::binary_search(operands.begin(), operands.end(),
                                         token)
                          ? ProseTokenRoleIR::envelope
                          : ProseTokenRoleIR::padding,
                      error))
                return false;
            }
            const auto slices = slices_for(records, refs);
            if (slices.empty())
              return fail(error, "ST control has no source provenance");
            auto slice = slices.front();
            slice.token_end = slices.back().token_end;
            slice.byte_end = slices.back().byte_end;
            build.empty_title_source = slice;
            Item title_end;
            title_end.kind = ItemKind::segment_end;
            title_end.empty_title = true;
            build.items.push_back(std::move(title_end));
            for (const auto token : rest) {
              Item item;
              item.kind = ItemKind::token;
              item.token = view_token(records, record_index, token);
              build.items.push_back(std::move(item));
            }
            Item end;
            end.kind = ItemKind::segment_end;
            build.items.push_back(std::move(end));
            break;
          }
        }
        const auto before = build.items.size();
        if (!push_payload(ProseTokenRoleIR::envelope, true, false, false))
          return false;
        // An `ST` control with no payload token is an empty title, not a
        // broken one: hosted BookServer heads such a topic with its number
        // alone.  SC09-138 record 1228 writes `csourcefn EDCUPRAG`, a
        // boundary, `ST` (token 25), a boundary and then `cfont 3 5 E` whose
        // payload `chars` is the topic's first body word; DT 19910321130500
        // serves `<H3> 8.1.1.1 </H3>` and opens the body with
        // `   <samp>chars</samp>`.  Verified the same way on 2.1.1.7, 4.1.1,
        // 4.1.2, 8.1.1.2, 8.1.1.5 and 8.7.2.1.
        std::size_t payload = 0;
        for (auto index = before; index < build.items.size(); ++index)
          if (build.items[index].kind == ItemKind::token) ++payload;
        if (payload == 0) {
          // The empty title is the whole `ST` display line.  The flattened
          // splitter can instead cut that line at a title word spelled like
          // a control: S85G-8681-00 11.1 record 1217 line 8 is
          // `ST  SRPI Compatibility` (tokens [25,29)), and the segment after
          // the bare `ST` opens on `SRPI`.  The reader takes the `ST`
          // payload as everything after the opcode up to the end of the
          // display line, whatever its words are spelled like, and hosted
          // (DT 19950803164615) heads the topic `<H2> 11.1   SRPI
          // Compatibility</H2>`; SC34-425 1.7.3.2.5 `ST  SREPLACE Command`
          // (DT 19921112160049), SH45-5547-00 2.21.2 and BLGC4MST 7.2.5 are
          // the same.  The segment that carries the rest of the line is
          // therefore the title (the text case below).  (A title cut after
          // its first word continues across the cut instead; see
          // `title_line_open_after`.)
          if (!segment.source_tokens.empty()) {
            const auto* line =
                display_line_of_token(record, segment.source_tokens.front());
            if (line != nullptr)
              for (auto token = segment.source_tokens.back() + 1;
                   token < line->token_end; ++token) {
                const auto view = view_token(records, record_index, token);
                if (is_visible(view) && !is_padding(view) &&
                    !is_placeholder_run(view)) {
                  split_title = std::make_pair(record_index, line->token_end);
                  break;
                }
              }
          }
          if (split_title) break;
          for (auto index = before; index < build.items.size(); ++index)
            if (build.items[index].kind == ItemKind::segment_end)
              build.items[index].empty_title = true;
          std::vector<std::pair<std::size_t, std::size_t>> refs;
          for (const auto token : segment.source_tokens)
            refs.push_back({record_index, token});
          const auto slices = slices_for(records, refs);
          if (slices.empty())
            return fail(error, "ST control has no source provenance");
          auto slice = slices.front();
          slice.token_end = slices.back().token_end;
          slice.byte_end = slices.back().byte_end;
          build.empty_title_source = slice;
        }
        break;
      }
      case BookControlKind::message_start: {
        if (!title_seen)
          return fail(error, "body SRMSG precedes the title");
        // Some message topics place a second, bare SRMSG between a native
        // separator row and their explanatory paragraph. The flattened
        // segment appears to carry that paragraph as operands, but native
        // framing proves the SRMSG row contains the opcode alone: IBMMMSTR
        // 3.7.14 record 1631 stores `01 68`, followed by the next row's
        // length/origin bytes `19 06`. Only that one-byte row is the anchor;
        // every token after it re-enters the ordinary body stream.
        const auto opcode_words = decoded_byte_range_to_word_range(
            record.assembled,
            {segment.opcode_range.begin, segment.opcode_range.end});
        const auto opcode_tokens = source_tokens_intersecting_output(
            record.assembled, opcode_words.begin, opcode_words.end);
        if (opcode_tokens.empty())
          return fail(error, "body SRMSG has no opcode provenance");
        const auto* native = display_line_of_token(record,
                                                   opcode_tokens.front());
        if (native == nullptr)
          return fail(error, "body SRMSG has no native row framing");
        bool operands = false;
        for (auto token = native->prefix_token + 1;
             token < native->token_end; ++token) {
          if (std::binary_search(opcode_tokens.begin(), opcode_tokens.end(),
                                 token))
            continue;
          const auto view = view_token(records, record_index, token);
          if (is_visible(view) && !is_padding(view)) operands = true;
        }
        if (operands) {
          // A message of a CZ message list (`cz FLOW MSGL`): the SRMSG row
          // names the message, and the whole row after `SR` is the served
          // anchor, exactly as for the envelope SRMSG above.  GX27-3999-00
          // B.0 record 104 line 7 is the row `SRMSG LTC0008` (tokens
          // [56,58)), a row of its own; hosted (DT 19950730184057) serves
          // `<a name="MSG LTC0008"><hr></a>` and prints neither word.  Every
          // token after the row -- the separator rule and the message
          // headline -- re-enters the body stream.
          //
          // The flattened dialect is read the same way: BookServer has no
          // SRMSG-specific rule, and every control row opening on `SR`
          // (except a bare `SRGLS`) names an anchor after the rest of its
          // row, wrapping the next drawn line.  AFM41MST 1.3 record 57 line
          // 8 is the row `SRMSG   25` (tokens [53,56)), served (DT
          // 19910927164256) as `<a name="MSG   25">` around the next line
          // `     25      SPECIFIED WORKSTATION DOES NOT EXIST`; ICHA6MST
          // 1.2 `SRMSG ICH001E`, SC41-3552-00 B.6 `SRMSG COMM 16` and
          // SH24-5221-01 2.0 `SRMSG 000I` are the same.
          if (opcode_tokens.front() != native->prefix_token + 1)
            return fail(error, "body SRMSG does not open its native row");
          auto identity =
              trim_ascii(display_line_text(record, *native));
          if (identity.size() < 6 ||
              ascii_lower(identity).rfind("srmsg ", 0) != 0)
            return fail(error, "body SRMSG row '" + identity +
                                   "' does not spell its opcode");
          identity = trim_ascii(identity.substr(5));
          if (identity.empty() ||
              !std::all_of(identity.begin(), identity.end(),
                           [](const unsigned char ch) {
                             return ch >= 0x20 && ch < 0x7F;
                           }))
            return fail(error, "body SRMSG identity '" + identity +
                                   "' is invalid");
          Item anchor;
          anchor.kind = ItemKind::anchor;
          anchor.anchor_id = "MSG " + identity;
          // Blank runs stay as stored: `SRMSG   25` names `MSG   25`.
          if (const auto stored = message_row_anchor_name(record, segment))
            anchor.anchor_id = *stored;
          anchor.source = token_slice(record, native->prefix_token + 1,
                                      native->token_end);
          build.items.push_back(std::move(anchor));
          for (const auto token : segment.source_tokens) {
            if (token > native->prefix_token && token < native->token_end) {
              const auto view = view_token(records, record_index, token);
              if (!ledger.assign(record_index, token,
                                 is_padding(view) ? ProseTokenRoleIR::padding
                                                  : ProseTokenRoleIR::control,
                                 error))
                return false;
              continue;
            }
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            build.items.push_back(std::move(item));
          }
          Item end;
          end.kind = ItemKind::segment_end;
          build.items.push_back(std::move(end));
          break;
        }
        Item anchor;
        anchor.kind = ItemKind::anchor;
        anchor.anchor_id = "MSG";
        anchor.source = token_slice(record, opcode_tokens.front(),
                                    opcode_tokens.back() + 1);
        build.items.push_back(std::move(anchor));
        if (!push_payload(ProseTokenRoleIR::control, false, false, false))
          return false;
        break;
      }
      // `SRTBL<id>` / `SRETBL` inside a verbatim `cz OFF` region are the
      // labelled box's own anchors, not a table envelope: the span plan
      // leaves them unclaimed there (prose_topic_spans.cpp), so they reach
      // the bare-anchor path with every other `SR<id>` control.  Outside one
      // they are envelope controls the span plan owns, and reaching this pass
      // at all is a decline.
      case BookControlKind::table_start:
      case BookControlKind::table_end:
      case BookControlKind::structural: {
        // The same holds for an envelope drawn wholly inside a closed box
        // (`box_enclosed_envelopes`, prose_topic_boxes.cpp): the opener is
        // the anchor of the box row after it, and the closer is markup that
        // names nothing -- hosted SC24-5527-02 7.4.5 (DT 19921218151459)
        // serves `<a name="TBLTBLUNIQ492">` and no `ETBL` anchor.
        const auto in_box =
            !segment.source_tokens.empty() &&
            box_markup.count({record_index, segment.source_tokens.front()}) != 0;
        // A table end that no table start of the topic precedes closes
        // nothing and names nothing.  SC31-8778-01 writes one `SRETBL` alone
        // on its line after the `statements:`/`dependencies:` rows of 714
        // operand topics, with no `SRTBL` in front of it (2.3.6.1 record 282
        // line 24; 2.3.6.19 record 321 line 26 opens a real table only
        // later, `SRTBLTBLUNIQ127` on record 322 line 9); hosted BookServer
        // (DT 20010606144047) prints the rows around it as stored and
        // nothing for it.  Display text on the closer's own line is not
        // shown by any such topic and still declines (below).
        const auto unopened_table_end =
            segment.kind == BookControlKind::table_end &&
            unopened_table_ends.count({record_index, segment_index}) != 0;
        if ((segment.kind == BookControlKind::table_start ||
             segment.kind == BookControlKind::table_end) &&
            !in_box && !unopened_table_end &&
            !(!segment.source_tokens.empty() &&
              inside_cz_verbatim(verbatim_regions, record_index,
                                 segment.source_tokens.front())))
          return fail(error, "body control " + segment.opcode +
                                 " is outside the prose model");

        if (!segment.malformed && title_seen && cz_dialect &&
            lower_opcode.rfind("srftn", 0) == 0 && lower_opcode.size() > 5) {
          // Footnote start of the CZ dialect: like every `SR<id>` anchor the
          // id is the whole opcode after `SR` (packet 1.1 record 17
          // `SRFTNFTNUNIQ1`, targeted by `cselect 16 4 FTNFTNUNIQ1`); the
          // `FTN` prefix marks it as the footnote the next `cz FLOW FN`
          // body carries.
          //
          // BookServer prints the footnote opening (`<a name="<id>"><hr>
          // <h5>`) for the `SRFTN<id>` line itself and `</h5></a>` for
          // `SREFTN`; `cz FLOW FN` is only its generic `CZ FLOW`, which
          // closes an open `<pre>`.  So books that put the directive after
          // the anchor, before it, or nowhere all serve the same footnote:
          //   - before it: dfhtma09 3.1.3 record 398 lines 40-41
          //     `cz FLOW FN 3 7`, `SRFTNFTNUNIQ40`, the rows, `SREFTN`;
          //   - nowhere: SC41-5612-02 2.4 record 195 lines 23-25
          //     `SRFTNREXLN`, ` |  (6) For REXX procedures, the limit is
          //     33.`, `SREFTN`; hosted (DT 19980522111551) serves
          //     `<a name="FTNREXLN"><hr><h5>` and the row before `</h5></a>`.
          // The rows after the anchor line are then the anchor's flattened
          // payload; they are the footnote's text.
          if (!pending_footnote_id.empty())
            return fail(error, "SR" + pending_footnote_id +
                                   " is not closed before " + segment.opcode);
          const auto id = segment.opcode.substr(2);
          if (!valid_anchor_id(id))
            return fail(error, "footnote id '" + id + "' is invalid");
          const auto operands = operand_tokens(record, segment);
          const auto leading = leading_separator_tokens(record, segment);
          const auto payload = std::any_of(
              segment.source_tokens.begin(), segment.source_tokens.end(),
              [&](const auto token) {
                if (std::binary_search(operands.begin(), operands.end(),
                                       token))
                  return false;
                const auto view = view_token(records, record_index, token);
                return !is_padding(view) &&
                       !is_row_control_slot(records, view) &&
                       !(std::binary_search(leading.begin(), leading.end(),
                                            token) &&
                         is_separator(view));
              });
          // A `cz FLOW FN` waiting for its anchor: the directive and nothing
          // else stands right before this line (the length byte that frames
          // the anchor's line is no text).
          Item* waiting = nullptr;
          for (auto it = build.items.rbegin(); it != build.items.rend();
               ++it) {
            if (it->kind == ItemKind::segment_end) continue;
            if (it->kind == ItemKind::token && it->separator &&
                (is_bare(it->token) || is_space_run(it->token) ||
                 is_row_control_slot(records, it->token)))
              continue;
            if (it->kind == ItemKind::layout && it->directive.mode == "flow" &&
                it->directive.tag == "fn" && it->directive.anchor_id.empty())
              waiting = &*it;
            break;
          }
          if (waiting != nullptr) {
            waiting->directive.anchor_id = id;
          } else {
            pending_footnote_id = id;
            pending_footnote_item = build.items.size();
            pending_footnote_source =
                operands.empty()
                    ? DocumentSourceSliceIR{}
                    : token_slice(record, operands.front(),
                                  operands.back() + 1);
          }
          if (payload) {
            if (operands.empty())
              return fail(error, "footnote anchor has no source token");
            if (!push_payload(ProseTokenRoleIR::control, false, false, false))
              return false;
          } else if (!assign_segment_tokens(records, ledger, record_index,
                                            segment, ProseTokenRoleIR::control,
                                            false, error)) {
            return false;
          }
          break;
        }
        if (!segment.malformed && title_seen && !cz_dialect &&
            lower_opcode == "sreftn") {
          // The flattened dialect has no `cz FLOW FN` to open: the footnote
          // body is carried in the `SRFTN<id>` control's own payload and
          // `SREFTN` only ends it.  Hosted BookServer serves GC23-046 5.1.1
          // (DT 19920330095121) as
          // `<a name="FTNESAFN"><hr><h5>     ( ) MVS/XA is a trademark of
          // the IBM Corporation.</h5></a>`, so the id is the opcode without
          // `SR` -- the `SRFTN<id>` anchor of record 65 segment 1, whose
          // payload is that sentence -- and the end marker itself displays
          // nothing.
          if (!assign_segment_tokens(records, ledger, record_index, segment,
                                     ProseTokenRoleIR::control, false, error))
            return false;
          Item end;
          end.kind = ItemKind::segment_end;
          end.footnote_end = true;
          build.items.push_back(std::move(end));
          break;
        }
        if (!segment.malformed && title_seen && lower_opcode == "sreftn") {
          if (!assign_segment_tokens(records, ledger, record_index, segment,
                                     ProseTokenRoleIR::control, false, error))
            return false;
          if (!pending_footnote_id.empty()) {
            // No `cz FLOW FN` came between the anchor and its end marker:
            // the footnote opened at the anchor (see `srftn` above).
            Item open;
            open.kind = ItemKind::layout;
            open.directive.mode = "flow";
            open.directive.tag = "fn";
            open.directive.anchor_id = pending_footnote_id;
            open.directive.source = pending_footnote_source;
            build.items.insert(
                build.items.begin() +
                    static_cast<std::ptrdiff_t>(pending_footnote_item),
                std::move(open));
            pending_footnote_id.clear();
          }
          const auto operands = operand_tokens(record, segment);
          Item item;
          item.kind = ItemKind::layout;
          item.directive.mode = "off";
          item.directive.tag = "fn";
          if (!operands.empty())
            item.directive.source =
                token_slice(record, operands.front(), operands.back() + 1);
          build.items.push_back(std::move(item));
          break;
        }
        if (!segment.malformed && title_seen && lower_opcode == "srgls") {
          // Glossary-style field anchor of the CZ dialect: `SRGLS <term>`
          // names the anchor `GLS <term>` (SC41-485 1.2.4; hosted
          // `<a name="GLS Configuration description name">`).  The term
          // words are anchor identity, not display text; the following
          // `cz FLOW GD` paragraph repeats them.
          //
          // The term is the rest of the control's own display line and
          // nothing more.  The flattened segment runs on to the next
          // control, so the display lines after it are body text the reader
          // prints: SH24-5219-00 3.0 record 371 is a bare `SRGLS` on line 0
          // followed by the paragraph `Much of the information in this
          // section is procedural ...` on lines 1-3, and hosted
          // (DT 19920710083758) serves that paragraph as text with no
          // anchor at all.  A bare `SRGLS` only makes the reader look ahead
          // for a glossary letter box; it names nothing, so it emits no
          // anchor here.
          const auto operands = operand_tokens(record, segment);
          const DisplayLineIR* control_line = nullptr;
          {
            const auto opcode_words = decoded_byte_range_to_word_range(
                record.assembled, segment.opcode_range);
            const auto opcode_tokens = source_tokens_intersecting_output(
                record.assembled, opcode_words.begin, opcode_words.end);
            if (!opcode_tokens.empty())
              control_line =
                  display_line_of_token(record, opcode_tokens.front());
          }
          if (control_line == nullptr)
            return fail(error, "SRGLS control is not on a framed display line");
          std::vector<std::size_t> body_tokens;
          std::string term;
          std::size_t first = npos;
          std::size_t last = 0;
          for (const auto token : segment.source_tokens) {
            if (token >= control_line->token_end) {
              body_tokens.push_back(token);
              continue;
            }
            const auto view = view_token(records, record_index, token);
            const auto is_operand = std::binary_search(
                operands.begin(), operands.end(), token);
            if (!is_operand && (is_padding(view) || is_separator(view))) {
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::padding, error))
                return false;
              continue;
            }
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                               error))
              return false;
            if (first == npos) first = token;
            last = token;
            if (!is_operand) {
              if (!term.empty()) term.push_back(' ');
              term += body_text(view);
            }
          }
          // The rest of the control line can be a segment of its own when
          // the term is spelled like a control: SY27-0327-05 ABBREVIATIONS
          // record 1330 line 37 stores `SRGLS SI`, the splitter opened `SI`
          // as a subject-index control, and hosted (DT 19961016091447)
          // serves `<a name="GLS SI">`.  Its words are the term.
          if (term.empty() && body_tokens.empty() &&
              segment_index + 1 < record.control_segments.size()) {
            const auto& next = record.control_segments[segment_index + 1];
            if (!next.source_tokens.empty() &&
                next.source_tokens.front() > control_line->prefix_token &&
                next.source_tokens.back() < control_line->token_end) {
              for (const auto token : next.source_tokens) {
                const auto view = view_token(records, record_index, token);
                if (is_padding(view) || is_separator(view)) {
                  if (!ledger.assign(record_index, token,
                                     ProseTokenRoleIR::padding, error))
                    return false;
                  continue;
                }
                if (!ledger.assign(record_index, token,
                                   ProseTokenRoleIR::control, error))
                  return false;
                last = token;
                if (!term.empty()) term.push_back(' ');
                term += body_text(view);
              }
              absorbed_segments.insert({record_index, segment_index + 1});
            }
          }
          if (first == npos)
            return fail(error, "SRGLS control has no source token");
          if (term.empty()) {
            Item lookahead;
            lookahead.kind = ItemKind::segment_end;
            lookahead.glossary_letter = true;
            build.items.push_back(std::move(lookahead));
          }
          if (!term.empty()) {
            ProseAnchorIR anchor;
            anchor.id = "GLS " + collapse_ascii_whitespace(term);
            // Named after the row as stored, blank runs included and
            // punctuation where the row puts it: SH12-5915-01 GLOSSARY
            // `SRGLS Interactive Control Service  (ICS)` is served as
            // `<a name="GLS Interactive Control Service  (ICS)">`.
            if (const auto stored = sr_row_anchor_name(record, first);
                stored && stored->size() > 4 &&
                ascii_equals_case_insensitive(stored->substr(0, 4), "gls "))
              anchor.id = "GLS" + stored->substr(3);
            anchor.source = token_slice(record, first, last + 1);
            Item item;
            item.kind = ItemKind::anchor;
            item.anchor_id = anchor.id;
            item.source = anchor.source;
            build.items.push_back(std::move(item));
          }
          if (!body_tokens.empty()) {
            for (const auto token : body_tokens) {
              Item item;
              item.kind = ItemKind::token;
              item.token = view_token(records, record_index, token);
              build.items.push_back(std::move(item));
            }
            Item end;
            end.kind = ItemKind::segment_end;
            build.items.push_back(std::move(end));
          }
          break;
        }
        // A `SRFTN<id>` of the flattened dialect is an ordinary body
        // anchor whose payload is the footnote text (see `sreftn` above).
        const auto in_verbatim =
            !segment.source_tokens.empty() &&
            inside_cz_verbatim(verbatim_regions, record_index,
                               segment.source_tokens.front());
        // An `SRFIG<id>` that opens no figure is a bare anchor
        // (`SpanPlan::anchor_figure_openers`, prose_topic_spans.cpp).
        const auto figure_anchor =
            build.plan != nullptr && !segment.source_tokens.empty() &&
            build.plan->anchor_figure_openers.count(
                {record_index, segment.source_tokens.front()}) != 0;
        if (segment.malformed || lower_opcode.rfind("sr", 0) != 0 ||
            (reserved_structural(lower_opcode) && !in_verbatim && !in_box &&
             !figure_anchor &&
             !unopened_table_end &&
             !(!segment.source_tokens.empty() &&
               box_figure_openers.count(
                   {record_index, segment.source_tokens.front()}) != 0) &&
             !(!cz_dialect && lower_opcode.rfind("srftn", 0) == 0 &&
               lower_opcode.size() > 5) &&
             !paired_list_item(lower_opcode)))
          return fail(error, "structural control " + segment.opcode +
                                 " is not a bare anchor");
        const auto id = segment.opcode.substr(2);
        if (!valid_anchor_id(id))
          return fail(error, "anchor id '" + id + "' is invalid");
        // `SRELIS<owner>` is the end marker of the list item `SRLIS<owner>`
        // names, not a destination.  SC24-5520-00 1.1.27 opens `SRLISvmcparm`
        // once (record 218 line 24) and closes it five times (records 225,
        // 228, 233, 238, 242); hosted BookServer (DT 19911011135123) serves
        // `<a name="LISvmcparm">` and `LISrcs` and no `ELIS*` anchor at all.
        // The marker may also stand before its opener: GX27-3999-00 2.2
        // record 27 has `SRELISamy` on line 30 and `SRLISamy` on line 33,
        // and hosted (DT 19950730184057) serves only `LISamy`.  A closer
        // whose owner the topic never names is not proven to be one and
        // still declines.
        const auto list_item_closer =
            lower_opcode.rfind("srelis", 0) == 0 && lower_opcode.size() > 6;
        if (list_item_closer && !list_owners.count(lower_opcode.substr(6)))
          return fail(error, "list closer " + segment.opcode +
                                 " names no SRLIS item of the topic");
        // A box's own `SRETBL` or `SREFIG` names nothing either; its payload
        // is the box rows after it (hosted SH09-8098-02 3.3.3.4.1 serves no
        // anchor for either).
        // Inside a verbatim `cz OFF` region the envelope's end markers name
        // nothing as well: the reader ignores every `SRE` control line
        // (bookmgr.exe tests the `SRE` prefix among the lines it skips), and
        // hosted SC41-4853-00 1.2 (DT 19951003131222) serves `FIGTBLUNIQ1`
        // and `TBLTBLUNIQ1` in its labelled box but no `EFIG`.
        const auto list_closer =
            list_item_closer || unopened_table_end ||
            ((in_box || in_verbatim) &&
             (segment.kind == BookControlKind::table_end ||
              lower_opcode == "srefig"));
        // A body anchor can carry the first display line of the text it
        // names.  Hosted BookServer wraps that text in the anchor element
        // and keeps it as body content -- unlike the metadata-envelope
        // anchor above, whose payload extends the served anchor *name*.
        //   ACPZMST1 record 155 `SRSPTSETDC A domain controller handles ...`
        //     is served (DT 19920319123146) as `<a name="SPTSETDC">   A
        //     domain controller handles communications between
        //     CPI-Communications</a>` followed by the rest of the paragraph.
        //   DREICMST record 45 `SRSPTAMEND Changes ...` is served (DT
        //     19911219125856) with `   Changes have been made throughout
        //     this edition and the previous edition,`.
        //   SC33-033 record 177 `SRSPTCHAATT` names
        //     `<a name="SPTCHAATT">   <I>Function</I>:  To establish axis
        //     line attributes.</a>` (DT 19930422134757).
        // The anchor id stays the opcode without `SR`; the payload re-enters
        // the token stream as ordinary display content.
        const auto anchor_operands = operand_tokens(record, segment);
        const auto visible_payload = std::any_of(
            segment.source_tokens.begin(), segment.source_tokens.end(),
            [&](const auto token) {
              if (std::binary_search(anchor_operands.begin(),
                                     anchor_operands.end(), token))
                return false;
              const auto view = view_token(records, record_index, token);
              const auto glyph_slot = view.width == 1 &&
                                      view.value < row_control_byte_limit &&
                                      punctuation_glyph_token(view);
              return !is_padding(view) && !is_separator(view) && !glyph_slot;
            });
        // The payload of a box's own opener or closer is the box rows after
        // it, which draw only box words and spacing; they are the box's to
        // claim, never the control's padding.
        //
        // Inside a `cz OFF` verbatim region the same holds for any anchor
        // whose payload draws box words: every display row of the region is
        // served character for character (`cz_verbatim_regions`,
        // prose_topic_spans.cpp), so a rail row the flattened segment ran on
        // into is a row of the region, not the control's padding.  SC41-485
        // 1.2 record 8 line 29 is `SRFIGTBLUNIQ1` alone, and its segment's
        // payload is line 30, the labelled box's blank rail row
        // `   |` .. `|`; record 9 lines 9 and 11 are the rail rows after
        // `SRETBL` and `SREFIG`.  Read as padding, the three rows vanished
        // from the region while the topic stayed typed.
        const auto drawn_verbatim_payload =
            in_verbatim &&
            std::any_of(segment.source_tokens.begin(),
                        segment.source_tokens.end(), [&](const auto token) {
                          if (std::binary_search(anchor_operands.begin(),
                                                 anchor_operands.end(), token))
                            return false;
                          const auto view =
                              view_token(records, record_index, token);
                          return !is_row_control_slot(records, view) &&
                                 std::any_of(view.body.begin(),
                                             view.body.end(),
                                             [](const auto word) {
                                               return box_word(word);
                                             });
                        });
        // A bare anchor alone on the line above a closed box names the box
        // (`box_leading_anchors`, prose_topic_boxes.cpp): the payload its
        // flattened segment ran on into is the box's first rows, and the
        // anchor wraps the top rule, as hosted SC24-546 4.3.21 (DT
        // 19940323131240) serves `<a name="SPTFDATE">`.
        const auto box_leader =
            !segment.source_tokens.empty() && !list_item_closer &&
            box_leaders.count({record_index, segment.source_tokens.front()}) !=
                0;
        // A payload that runs on into a closed box -- over a rule row and
        // empty lines, up to a selector inside the box -- holds rows of the
        // box, which are the box's to draw, never the control's padding.
        // SG24-4575-00 2.1.1 record 29 stores `SRSPT1F1` (line 9), a
        // 74-cell rule, two empty lines and the picture frame whose
        // `cselect 35 9 PIC1` (line 16) ends the segment; hosted (DT
        // 19950816155957) serves `<a name="SPT1F1"><hr>` and the frame.
        const auto drawn_box_payload =
            !in_box && !list_item_closer &&
            std::any_of(segment.source_tokens.begin(),
                        segment.source_tokens.end(), [&](const auto token) {
                          return !std::binary_search(anchor_operands.begin(),
                                                     anchor_operands.end(),
                                                     token) &&
                                 in_closed_box(record_index, token);
                        });
        // The unopened closer must stand alone on its display line; what the
        // segment carries beyond that line is the next block's text.
        if (unopened_table_end && !in_box) {
          const auto* native =
              segment.source_tokens.empty()
                  ? nullptr
                  : display_line_of_token(record, segment.source_tokens.front());
          if (native == nullptr ||
              native->prefix_token + 1 != segment.source_tokens.front())
            return fail(error, "unopened table end " + segment.opcode +
                                   " does not open its display line");
          for (auto token = native->prefix_token + 2;
               token < native->token_end; ++token) {
            const auto view = view_token(records, record_index, token);
            if (is_visible(view) && !is_padding(view))
              return fail(error, "unopened table end " + segment.opcode +
                                     " carries display text");
          }
        }
        if ((visible_payload || in_box || drawn_verbatim_payload ||
             box_leader || drawn_box_payload) &&
            title_seen) {
          if (anchor_operands.empty())
            return fail(error, "anchor control has no source token");
          // A closer's payload is the next item's text and nothing else.
          if (list_closer) {
            if (!push_payload(ProseTokenRoleIR::control, false, false, false))
              return false;
            break;
          }
          Item item;
          item.kind = ItemKind::anchor;
          item.anchor_id = id;
          item.figure_anchor = figure_anchor;
          item.source = token_slice(record, anchor_operands.front(),
                                    anchor_operands.back() + 1);
          build.items.push_back(std::move(item));
          if (!push_payload(ProseTokenRoleIR::control, false, false, false))
            return false;
          break;
        }
        {
          // A one-byte row-control glyph can be glued to the anchor before
          // the title (SC09-2417-00 2.2 record 188 `SRHDRHCPGIO <<`).
          const auto operands = operand_tokens(record, segment);
          for (const auto token : segment.source_tokens) {
            const auto view = view_token(records, record_index, token);
            if (std::binary_search(operands.begin(), operands.end(), token)) {
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::control, error))
                return false;
              continue;
            }
            const auto glyph_slot = view.width == 1 &&
                                    view.value < row_control_byte_limit &&
                                    punctuation_glyph_token(view);
            // A length byte the anchor's operand run overshot into is the
            // next row's control slot, not the anchor's payload.  Checked
            // last: a byte that already reads as the row's marker glyph
            // keeps the marker role the row geometry is built from.
            if (!is_padding(view) && !is_separator(view) && !glyph_slot &&
                !is_row_control_slot(records, view))
              return fail(error, "control " + segment.opcode +
                                     " carries visible payload '" +
                                     body_text(view) + "' in record " +
                                     std::to_string(record.logical_record));
            if (!ledger.assign(record_index, token,
                               glyph_slot ? ProseTokenRoleIR::marker
                                          : ProseTokenRoleIR::padding,
                               error))
              return false;
          }
        }
        const auto operands = operand_tokens(record, segment);
        if (operands.empty())
          return fail(error, "anchor control has no source token");
        if (list_closer) break;
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
          item.figure_anchor = figure_anchor;
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
        if (split_title) {
          // The rest of the `ST` display line the splitter cut: the title.
          // The line pass ends it at the next display line's length byte.
          if (!first_visible || record_index != split_title->first ||
              *first_visible >= split_title->second)
            return fail(error, "split ST title line has no title words");
          split_title.reset();
          if (!push_payload(ProseTokenRoleIR::control, true, false, false))
            return false;
          break;
        }
        // The record encoder writes a compact separator token -- the attach
        // control plus a comma -- before a body control it does not precede
        // with a boundary byte.  It is the same separator that stands between
        // `csummary` and `chdlevel` in FA1PLMM0 record 352, and hosted
        // BookServer prints no comma for it (FA1PLMM0 6.1.2 record 353 serves
        // `PSF/VSE`, SC09-138 8.3.1.8 record 1482 serves `each other.`).  It
        // is recognised only where the control it separates follows it, so a
        // display comma glued to the word before it stays text.
        if (first_visible) {
          const auto lead = view_token(records, record_index, *first_visible);
          if (lead.has_prefix && lead.prefix == 1 && lead.body.size() == 1 &&
              lead.body.front() == ',') {
            for (const auto token : segment.source_tokens) {
              if (token <= *first_visible) continue;
              const auto view = view_token(records, record_index, token);
              if (!is_visible(view)) continue;
              const auto lower = ascii_lower(body_text(view));
              if (pagination_control(lower) ||
                  (script_control(lower) &&
                   opens_display_line(record, token)))
                first_visible = token;
              break;
            }
          }
        }
        if (!title_seen) {
          // Before the title the record still carries the metadata envelope,
          // and the display line decides what of it is displayed at all.
          //
          // 1. A length byte is never display text, whatever dictionary word
          //    it resolves to.  SC26-457 record 549 token 0 is the first
          //    display line's length byte (encoded value 39) and spells `'`,
          //    which `decode_control_segments` split off as a text segment in
          //    front of the `ST`; hosted DT 19911220191142 serves
          //    `<H4> 3.14.2.3   Deleting an Entry-Sequenced VSAM Cluster ...`
          //    with no apostrophe.
          // 2. An `SR<id>` anchor that is the only displayed word of its own
          //    display line is the envelope anchor variant `parse_envelope`
          //    already models -- it only reaches here because the *next*
          //    line's length byte is glued to the opcode word in the
          //    flattened string, so `classify` sees `SRHDRPCHECK.` and
          //    refuses the identifier.  SC09-138 record 1229 line 8 is
          //    exactly `SRHDRPCHECK` and line 9 exactly `ST`; hosted DT
          //    19910321130500 serves 8.1.1.2 as
          //    `<a name="HDRPCHECK"><H3> 8.1.1.2 </H3></a>`.  Verified the
          //    same way on 4.1.1 (`HDRETOHEAP`), 4.1.3 and 8.1.1.5.
          const auto line_of = [&](const std::size_t token) {
            return display_line_of_token(record, token);
          };
          bool length_byte_only = false;
          if (first_visible) {
            const auto* line = line_of(*first_visible);
            if (line != nullptr && line->prefix_token == *first_visible) {
              // Case 1: the segment opens on a length byte.  Look past it for
              // the first token that really is displayed.
              first_visible.reset();
              for (const auto token : segment.source_tokens) {
                if (token <= line->prefix_token) continue;
                if (is_visible(view_token(records, record_index, token))) {
                  first_visible = token;
                  break;
                }
              }
              length_byte_only = !first_visible.has_value();
            }
          }
          if (length_byte_only) {
            for (const auto token : segment.source_tokens) {
              const auto view = view_token(records, record_index, token);
              if (!ledger.assign(record_index, token,
                                 is_visible(view) ? ProseTokenRoleIR::marker
                                                  : ProseTokenRoleIR::padding,
                                 error))
                return false;
            }
            break;
          }
          if (first_visible) {
            const auto* line = line_of(*first_visible);
            const auto word = body_text(
                view_token(records, record_index, *first_visible));
            const auto lower = ascii_lower(word);
            const auto anchor_shaped =
                word.size() > 2 && lower.rfind("sr", 0) == 0 &&
                !reserved_structural(lower) &&
                std::all_of(word.begin(), word.end(),
                            [](const unsigned char ch) {
                              return std::isalnum(ch) != 0 || ch == '_';
                            });
            bool alone = line != nullptr;
            if (alone)
              for (auto token = line->prefix_token + 1;
                   token < line->token_end; ++token)
                if (token != *first_visible &&
                    is_visible(view_token(records, record_index, token)))
                  alone = false;
            if (anchor_shaped && alone) {
              ProseAnchorIR anchor;
              anchor.id = word.substr(2);
              anchor.source = token_slice(record, segment.source_tokens.front(),
                                          segment.source_tokens.back() + 1);
              for (const auto token : segment.source_tokens) {
                const auto view = view_token(records, record_index, token);
                if (!ledger.assign(record_index, token,
                                   token == *first_visible
                                       ? ProseTokenRoleIR::envelope
                                       : (is_padding(view)
                                              ? ProseTokenRoleIR::padding
                                              : ProseTokenRoleIR::envelope),
                                   error))
                  return false;
              }
              build.leading_anchors.push_back(std::move(anchor));
              break;
            }
          }
          // 3. An `SI` index line draws nothing wherever it stands, and the
          //    reader skips it before the title as anywhere else.  The
          //    version 1.0/1.1 books write index lines between `CSOURCEFN`
          //    and `ST`: EHMPMST 3.2.1 record 67 lines 8-11 (`SI processing,
          //    element` ...), served by hosted (DT 19891020000000) as the
          //    heading `3.2.1   Element processing` and the body only.  Only
          //    an `SI` that opens its own display line is such an entry.
          if (first_visible &&
              ascii_lower(body_text(view_token(records, record_index,
                                               *first_visible))) == "si" &&
              opens_display_line(record, *first_visible)) {
            // The entry is its display line.  What the segment carries past
            // it before the title is the next line's length byte (EHMPMST
            // 3.2.1 record 67 token 63, a 36-cell box run, opens the `ST`
            // line), never display text.
            const auto* line = display_line_of_token(record, *first_visible);
            bool first_payload = true;
            for (const auto token : segment.source_tokens) {
              const auto view = view_token(records, record_index, token);
              if (token >= line->token_end) {
                if (is_visible(view) && !is_row_control_slot(records, view) &&
                    !is_padding(view))
                  return fail(error, "visible text precedes the ST title");
                if (!ledger.assign(record_index, token,
                                   ProseTokenRoleIR::padding, error))
                  return false;
                continue;
              }
              Item item;
              item.kind = ItemKind::token;
              item.token = view;
              item.index_start = first_payload;
              first_payload = false;
              build.items.push_back(std::move(item));
            }
            Item end;
            end.kind = ItemKind::segment_end;
            build.items.push_back(std::move(end));
            break;
          }
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
        // A `C.` opcode that opens its own display line: the reader skips
        // the whole line, so every token of it is control and the rows after
        // it are body text.
        const auto* script_line =
            script_control(first_text) &&
                    opens_display_line(record, *first_visible)
                ? display_line_of_token(record, *first_visible)
                : nullptr;
        if (pagination_control(first_text) || script_line != nullptr) {
          // `c.cp` is the keep-together pagination control.  Its optional
          // operand is the token adjacent to the opcode in the record --
          // the record encoder emits no spacing token between a control
          // opcode and its operand, so an intervening space run proves that
          // the next word is display content and not an operand.  Byte-level
          // evidence from the Token IR:
          //   IEAC6MST record 79: `c.cp` `999` `      ` ` ` `|` `    ` `If`
          //     -- operand 999; hosted DT 19920124000100 serves
          //     ` |     If you do not already have a dump directory, ...`.
          //   GC23-046 record 31: `c.cp` `8DV`; hosted DT 19920330095121
          //     serves no `8DV` and no `DV` anywhere in CHANGES.1.
          //   DREICMST record 600: `c.cp` `2i`; hosted DT 19911219125856
          //     serves no `2i` in 2.20.3.1.4.
          //   SC34-425 record 267: `c.cp` `50p`; SC31-711 record 10:
          //     `c.cp` `54` then the row `:` `   ` `The` `following`.
          //   GC28-183 record 783: `c.cp` `              ` `   ` `6` `.`
          //     -- no operand; hosted DT 19930625102617 serves
          //     `   6.  SYSOUT data sets (except DD3 and DD4) are printed`.
          //   FA1PLMM0 record 369: `c.cp` ` ` `   ` `The` `columns`;
          //     hosted DT 19910927114801 serves
          //     `   The columns have the following meaning:`.
          //   DREICMST record 243: `c.cp` alone, no operand and no payload.
          // Everything after the opcode and its operand is ordinary display
          // content and re-enters the token stream; the legacy renderer
          // dropped it unless it was spelled `<n>:<text>`.
          std::size_t consumed = *first_visible;
          if (script_line != nullptr) {
            for (const auto token : segment.source_tokens)
              if (token > consumed && token < script_line->token_end)
                consumed = token;
          }
          for (std::size_t position = 0;
               script_line == nullptr && position < segment.source_tokens.size();
               ++position) {
            if (segment.source_tokens[position] != *first_visible) continue;
            if (position + 1 >= segment.source_tokens.size()) break;
            const auto candidate = segment.source_tokens[position + 1];
            const auto view = view_token(records, record_index, candidate);
            // The next display line's length byte is not this control's
            // operand, whatever the dictionary spells for the byte; the
            // control simply carried none.
            if (is_padding(view) || is_row_control_slot(records, view)) break;
            const auto text = body_text(view);
            if (!pagination_operand(text))
              return fail(error, first_text + " control carries visible payload '" +
                                     text + "'");
            consumed = candidate;
            break;
          }
          bool payload_visible = false;
          for (const auto token : segment.source_tokens) {
            const auto view = view_token(records, record_index, token);
            if (token <= consumed) {
              if (!ledger.assign(record_index, token,
                                 (token == *first_visible ||
                                  token == consumed ||
                                  (script_line != nullptr &&
                                   token > *first_visible &&
                                   is_visible(view)))
                                     ? ProseTokenRoleIR::control
                                     : ProseTokenRoleIR::padding,
                                 error))
                return false;
              continue;
            }
            Item item;
            item.kind = ItemKind::token;
            item.token = view;
            // Spacing after the control stands at a control boundary, the
            // reading the pagination-only form already used.
            item.separator =
                is_bare(view) || is_space_run(view) || is_placeholder_run(view);
            if (!item.separator) payload_visible = true;
            build.items.push_back(std::move(item));
          }
          if (payload_visible) {
            Item end;
            end.kind = ItemKind::segment_end;
            build.items.push_back(std::move(end));
          }
          break;
        }
        // A control-shaped word the display-line pass proved to be display
        // text opens no control: the flattened string split the segment
        // there, but the row it belongs to continues (SH12-565 4.3.5's list
        // item `SRCVPAC`).  Its tokens are body text in place.  This is the
        // half of the class the encoder's boundary cannot decide, because the
        // word carries a real boundary and is display text all the same.
        if (segment.display_text) {
          if (!push_payload(ProseTokenRoleIR::control, false, false, false))
            return false;
          break;
        }
        // The flattened dialect's footnote end marker can arrive as a text
        // segment, because the flattened splitter cuts on the record's `.`
        // separator and glues it to the opcode: GG24-4302-00 6.7 record 560
        // segment 2 is `SREFTN.` over tokens 75-78 -- the `,` separator, the
        // two-byte word `SREFTN`, the attach control and the attach control
        // plus `.` -- and hosted (DT 19950308184737) closes the footnote with
        // `</h5></a>`, printing neither the opcode nor the stop.
        if (!cz_dialect && first_text == "sreftn") {
          for (const auto token : segment.source_tokens) {
            if (token == *first_visible) {
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::control, error))
                return false;
              continue;
            }
            const auto view = view_token(records, record_index, token);
            if (!is_padding(view) && !is_row_control_slot(records, view) &&
                !is_separator(view) &&
                !(view.width == 1 && punctuation_glyph_token(view)))
              return fail(error, "SREFTN end marker carries visible payload '" +
                                     body_text(view) + "'");
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                               error))
              return false;
          }
          Item end;
          end.kind = ItemKind::segment_end;
          end.footnote_end = true;
          build.items.push_back(std::move(end));
          break;
        }
        // The decoded-string splitter opens a segment wherever a word is
        // spelled like a control.  A word is only a control when the record
        // encoder wrote a boundary before it: a decoder-separator token
        // (a box/unmapped glyph run) or the compact separator token (the
        // attach control plus `,` or `.`).  Byte-level evidence:
        // ACPZMST1 CONTENTS record 18 writes `ST` after token 24 (glyph
        // U+2514) and `ctocdef=0` after token 30 (glyph U+2518);
        // SC24-546 record 961 writes `SRHDRIRRR` after the compact `,`.
        // Prose words that merely look like controls carry no such boundary:
        // PRG1SORT 1.1.5.1 record 80 token 2 `SRCFILE` follows an 18-cell
        // space run and is the CL parameter `SRCFILE(LIBRAR2/FILE3)` (hosted
        // DT 19900829171904 prints it), and SC24-546 14.0 record 961 spells
        // the routine names `SRRCMIT or SRRBACK.` inside a sentence.  Such a
        // segment is the previous row's prose, continued.
        //
        // A boundary does not make a display line's length byte a control
        // either.  The byte is row geometry whatever word the dictionary
        // spells for it (doc/boo-spec/logical-controls.adoc, "A Metadata
        // Opcode In The Body Is A Display-Line Length Byte"), and the line it
        // opens is display text, exactly as when a record opens on one.
        // GG24-395 3.3.1.3
        // record 352 token 51 (encoded value 60) opens the 60-byte line 5
        // and spells `citerm`; OFCUSEOV 1.29.4 record 329 token 228 (value
        // 70) opens the 70-byte line 23 and spells `ctoce`.  Each follows the
        // box rail U+2502 that closes the row before it, a decoder
        // separator.  Hosted BookServer prints both box rows and neither
        // word.  A length byte spelled `si` keeps its existing reading.
        const auto length_byte_lead = is_row_control_slot(records, first_view);
        const auto continuation =
            first_text != "si" &&
            ((record_index != 0 && segment_index == 0) || !boundary_before() ||
             length_byte_lead);
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
        // A `cfont` opcode that is itself a display line's length byte opens
        // no control: the byte is a length and nothing else, whatever
        // dictionary word it happens to spell (doc/boo-spec/logical-controls.adoc,
        // "A Metadata Opcode In The Body Is A Display-Line Length Byte").
        // The signature is exact -- the segment starts on the length byte,
        // the byte's own word spells the opcode, and the operand carries no
        // complete `<column> <length> <code>` triple, because what follows
        // the byte is the line's display text.
        //
        // Byte-level, two books: SC24-546 record 79 has tokens 108 (value 44,
        // the length of display line 3) and 109 (value 59, the word `cfont`)
        // opening the genuine `cfont 13 2 X ... 67 3 X`, then token 149 --
        // the same encoded value 59, this time the length of the 59-byte
        // display line `             ¬<  ¬=  ¬==  >>  ...` -- which the
        // flattened splitter read as a second `cfont`.  N2AH1MST record 89
        // spells it adjacently: token 31 (value 37, the length of line 4) and
        // token 32 (value 37, the opcode word) are byte-identical neighbours,
        // and only the first is geometry.  OFCUSEOV record 276 token 0
        // (value 59) is the length byte of the calendar box's first row.
        // The byte re-enters the token stream so the display-row pass gives
        // it the row-control slot it gives every other length byte.
        if (is_display_line_length_opcode(record, segment)) {
          for (const auto token : segment.source_tokens) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            build.items.push_back(std::move(item));
          }
          Item end;
          end.kind = ItemKind::segment_end;
          build.items.push_back(std::move(end));
          break;
        }
        std::string font_error;
        auto spans = decode_font_control_spans(record, segment, &font_error);
        if (!spans)
          return fail(error, "font control rejected: " + font_error);
        // The `,` operand separator and the underscored highlight phrases
        // `5`..`9` are part of the CFONT operand syntax and of the CFONTDEF
        // code table; both are resolved by decode_font_control_spans.
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
        const auto lower_target = ascii_lower(item.target);
        // A picture selector the figure family did not claim places the
        // image inside the sentence: hosted BookServer replaces exactly the
        // `PICTURE n` placeholder columns the selector names with the
        // `<img>` and leaves the rest of the row (SG24-2047-00 4.1.1
        // `Click on the <img ...> button to start the setup process.`, DT
        // 19971218054640).  The picture must be in the resource catalog,
        // and the display-line pass proves the covered columns spell the
        // placeholder.
        if (lower_target.rfind("pic", 0) == 0) {
          if (!figure_picture_target(item.target))
            return fail(error, "selector targets a picture");
          const auto resource = figure_picture_resource(item.target);
          if (build.resource_ids == nullptr ||
              build.resource_ids->count(ascii_lower(resource)) == 0)
            return fail(error, "inline picture resource " + resource +
                                   " is not in the resource catalog");
          item.picture = true;
          item.target = resource;
          item.target_kind = CrossReferenceTargetKindIR::resource;
        }
        const auto operands = operand_tokens(record, segment);
        if (operands.empty()) return fail(error, "selector has no source token");
        // A `LNK` selector carries its destination in the leading `<...>`
        // payload tokens; they are control metadata, never display text.
        std::size_t alternative_tokens = 0;
        std::vector<std::size_t> alternative_source_tokens;
        if (lower_target == "lnk") {
          std::vector<std::string> tokens;
          alternative_source_tokens =
              selector_link_alternative_tokens(record, segment, &tokens);
          std::string link_error;
          const auto link = parse_selector_link(tokens, &link_error);
          if (!link) return fail(error, "selector rejected: " + link_error);
          alternative_tokens = alternative_source_tokens.size();
          item.target = link->alternatives[5];
          switch (link->kind) {
          case SelectorLinkKindIR::book_contents:
            item.target_kind = CrossReferenceTargetKindIR::book_contents;
            break;
          case SelectorLinkKindIR::book_heading:
            item.target_kind = CrossReferenceTargetKindIR::book_heading;
            break;
          case SelectorLinkKindIR::book_spot:
            item.target_kind = CrossReferenceTargetKindIR::book_spot;
            break;
          case SelectorLinkKindIR::book_anchor:
            item.target_kind = CrossReferenceTargetKindIR::book_anchor;
            break;
          case SelectorLinkKindIR::external_link:
          // An `<IMAGE>` selector outside a figure is the same external link
          // with an image leading its selected columns: hosted BookServer
          // serves XWEBDEMO 1.0 (DT 19970423182524) as `<a href="<alt 4>">
          // <img src="<alt 3>">IBM BookManager BookServer</a>`.  The tuple
          // rides on the target; lowering draws the image.
          case SelectorLinkKindIR::external_image:
            item.target_kind = CrossReferenceTargetKindIR::external;
            break;
          }
          item.target_alternatives = link->alternatives;
        }
        item.source = token_slice(record, operands.front(), operands.back() + 1);
        item.target_source =
            alternative_source_tokens.empty()
                ? item.source
                : token_slice(record, operands.front(),
                              alternative_source_tokens.back() + 1);
        build.items.push_back(std::move(item));
        if (!push_payload(ProseTokenRoleIR::control, false, false, false,
                          alternative_tokens))
          return false;
        break;
      }
      case BookControlKind::spacing: {
        // `c.sp` is the vertical-space control.  It has no display payload:
        // hosted BookServer emits only a paragraph break where it stands
        // (GC28-183 1.3.3 record 91 `c.sp 1 c`, DT 19930625102617;
        // SC33-033 4.6 record 177 `c.sp 1 c` between the heading and
        // `<a name="SPTCHAATT">`, DT 19930422134757; SC34-425 2.4.3 record
        // 1465, DT 19921112160049). The
        // trailing spacing tokens stay in the stream because they can carry
        // the paragraph break, the reading `c.cp` already uses.
        //
        // The reader skips every control line whose name starts with `C.`
        // whatever its operands, so on a framed record the operands are
        // exactly the words of the display line the opcode opens, and the rows
        // after it are body text even where the control segment runs on
        // into them.  GC41-5051-01 1.0 record 4 line 9 is `c.sp 1p c`;
        // hosted (DT 19980619084541) serves no `1p`.  ehxb7000
        // BACK_1.2.7.1 record 417 line 9 is `c.sp 0p8 c`, and the segment
        // reaches into line 11 `   The following documents are ISO
        // standards ...`.  An unframed record keeps the documented
        // count/unit forms only; anything else there fails closed.
        if (!title_seen) return fail(error, "c.sp control precedes the title");
        std::string operands;
        std::size_t last_operand = npos;
        std::optional<std::size_t> opcode_token;
        const DisplayLineIR* control_line = nullptr;
        for (const auto token : segment.source_tokens) {
          const auto view = view_token(records, record_index, token);
          if (control_line != nullptr && token >= control_line->token_end)
            break;
          // A length byte the segment overshot into spells a word but is the
          // next row's control slot, so it is not an operand of `c.sp`.
          if (!is_visible(view) || is_row_control_slot(records, view)) continue;
          if (!opcode_token) {
            opcode_token = token;
            // Only a line the opcode opens is a control line.
            control_line = display_line_of_token(records[record_index], token);
            if (control_line != nullptr && control_line->prefix_token + 1 != token)
              control_line = nullptr;
            continue;
          }
          if (!operands.empty()) operands.push_back(' ');
          operands += ascii_lower(body_text(view));
          last_operand = token;
        }
        if (!opcode_token) return fail(error, "c.sp control has no opcode");
        if (control_line == nullptr && !vertical_space_operands(operands))
          return fail(error, "c.sp control carries visible payload '" +
                                 operands + "'");
        const auto control_end =
            last_operand == npos ? *opcode_token : last_operand;
        for (const auto token : segment.source_tokens) {
          const auto view = view_token(records, record_index, token);
          if (token <= control_end) {
            if (!ledger.assign(record_index, token,
                               is_visible(view) ? ProseTokenRoleIR::control
                                                : ProseTokenRoleIR::padding,
                               error))
              return false;
            continue;
          }
          Item item;
          item.kind = ItemKind::token;
          item.token = view;
          item.separator = true;
          build.items.push_back(std::move(item));
        }
        break;
      }
      case BookControlKind::revision_definition: {
        // A SCRIPT revision declaration changes how later marked rows draw
        // their margin; the declaration itself is metadata. IBMMMSTR
        // PREFACE.6 record 19 carries `c.rev PREF |` after its ST title.
        if (segment.malformed)
          return fail(error, "revision definition is malformed");
        if (!assign_segment_tokens(records, ledger, record_index, segment,
                                   ProseTokenRoleIR::control, false, error))
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
      case BookControlKind::layout_directive: {
        // A `cz` the display-line framing proves to be a row's length byte
        // is that row's control slot, exactly like the metadata spellings
        // in the default case below (doc/boo-spec/markup.adoc, "Reading a
        // CZ body"): dfhcag49 1.1 record 52 token 35 (encoded value 13, one
        // byte) spells `cz` and is the length byte of the 13-byte row
        // `   Message identifiers are of two types.`; read as a directive it
        // had a space run for its mode.  The byte re-enters the token
        // stream and the row it frames is display text.
        if (title_seen && is_display_line_length_opcode(record, segment)) {
          for (const auto token : segment.source_tokens) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            build.items.push_back(std::move(item));
          }
          Item end;
          end.kind = ItemKind::segment_end;
          build.items.push_back(std::move(end));
          break;
        }
        if (!title_seen)
          return fail(error, "layout directive precedes the title");
        const auto first_item = build.items.size();
        // Rows between a footnote anchor and a later `cz FLOW FN` would land
        // outside the footnote the reader prints around them.
        const auto footnote_rows =
            !pending_footnote_id.empty() &&
            std::any_of(build.items.begin() +
                            static_cast<std::ptrdiff_t>(pending_footnote_item),
                        build.items.end(), [&](const Item& item) {
                          return item.kind == ItemKind::token &&
                                 is_visible(item.token) &&
                                 !is_row_control_slot(records, item.token);
                        });
        const auto footnote = pending_footnote_id;
        if (!collect_layout_directive(records, record_index, segment,
                                      pending_footnote_id, ledger, build.items,
                                      error))
          return false;
        name_verbatim_figure(first_item, record_index, segment);
        if (footnote_rows && pending_footnote_id.empty())
          return fail(error, "rows of SR" + footnote +
                                 " precede its cz FLOW FN");
        cz_seen = true;
        break;
      }
      // The BUILD 1.3 artwork envelope outside a figure region
      // (doc/boo-spec/markup.adoc, "The BUILD 1.3 artwork envelope"):
      // `csart` .. `ceart` around a picture selector in running text and
      // `csartdesc <n>` .. `ceartdesc` around the `cartdesc` lines that
      // describe picture <n>.  The boundaries own their opcode (and the
      // picture number) and nothing else; text the decoder glued after a
      // closer is the body that follows it (a59o1001 3.1.1 record 59:
      // `ceartdesc` then `symbol located beside the power switch ...`).
      // Hosted BookServer serves the picture where its selector stands, with
      // the description as the image's alternative text (GA27-4106-02 D.0,
      // DT 19980408153335: `<img ... P16.GIF alt="   REQTEXT">`, then
      // `<a name="PIC16">` and the paragraph after the envelope).
      case BookControlKind::art_start:
      case BookControlKind::art_end:
      case BookControlKind::art_description_end: {
        if (art_length_byte_row()) break;
        if (!title_seen)
          return fail(error, "artwork control precedes the title");
        if (segment.malformed) return fail(error, "artwork control is malformed");
        if (segment.kind == BookControlKind::art_description_end) {
          if (art_description_picture.empty())
            return fail(error, "ceartdesc closes no artwork description");
          art_description_picture.clear();
        }
        if (!push_payload(ProseTokenRoleIR::control, false, false, false))
          return false;
        break;
      }
      case BookControlKind::art_description_start: {
        if (art_length_byte_row()) break;
        if (!title_seen)
          return fail(error, "artwork control precedes the title");
        if (segment.malformed) return fail(error, "artwork control is malformed");
        if (!art_description_picture.empty())
          return fail(error, "artwork description opens inside another");
        const auto picture =
            ascii_lower(trim_ascii(operand_text(record, segment.operand_range)));
        if (picture.empty() ||
            !std::all_of(picture.begin(), picture.end(), [](unsigned char ch) {
              return std::isdigit(ch) != 0;
            }))
          return fail(error, "artwork description names no picture");
        if (build.picture_descriptions.count(picture) != 0)
          return fail(error, "picture " + picture + " is described twice");
        build.picture_descriptions[picture];
        art_description_picture = picture;
        if (!push_payload(ProseTokenRoleIR::control, false, false, false))
          return false;
        break;
      }
      case BookControlKind::art_description: {
        if (art_length_byte_row()) break;
        // One `cartdesc` line: the rest of the opcode's own display line is
        // description text, which the reader collects for the picture's
        // `alt` and never prints.  The escaped `cvselect`/`cvfont` lines
        // between description lines open no segment, so the payload runs on
        // across them; they are description markup too.  Any other row the
        // segment reaches is body text after the envelope.
        if (art_description_picture.empty())
          return fail(error, "cartdesc outside an artwork description");
        if (segment.malformed) return fail(error, "artwork control is malformed");
        const DisplayLineIR* control_line = nullptr;
        bool opcode_seen = false;
        std::string text;
        for (const auto token : segment.source_tokens) {
          const auto view = view_token(records, record_index, token);
          if (!opcode_seen) {
            if (!is_visible(view) || is_row_control_slot(records, view)) {
              if (!ledger.assign(record_index, token,
                                 ProseTokenRoleIR::padding, error))
                return false;
              continue;
            }
            opcode_seen = true;
            control_line = display_line_of_token(record, token);
            if (control_line == nullptr ||
                control_line->prefix_token + 1 != token)
              return fail(error, "cartdesc does not open its display line");
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                               error))
              return false;
            continue;
          }
          if (token < control_line->token_end) {
            if (is_visible(view) && !is_row_control_slot(records, view)) {
              if (!text.empty()) text.push_back(' ');
              text += body_text(view);
            }
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                               error))
              return false;
            continue;
          }
          const auto* line = display_line_of_token(record, token);
          bool escaped = false;
          if (line != nullptr && line->prefix_token + 1 < line->token_end) {
            const auto opener = ascii_lower(trim_ascii(body_text(
                view_token(records, record_index, line->prefix_token + 1))));
            escaped = opener == "cvselect" || opener == "cvfont";
          }
          if (escaped) {
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                               error))
              return false;
            continue;
          }
          Item item;
          item.kind = ItemKind::token;
          item.token = view;
          build.items.push_back(std::move(item));
        }
        if (!opcode_seen) return fail(error, "cartdesc has no opcode");
        Item end;
        end.kind = ItemKind::segment_end;
        build.items.push_back(std::move(end));
        text = collapse_ascii_whitespace(trim_ascii(text));
        auto& description = build.picture_descriptions[art_description_picture];
        if (!text.empty()) {
          if (!description.empty()) description.push_back(' ');
          description += text;
        }
        break;
      }
      default: {
        // Where a record's display lines parse, the length byte that opens a
        // line is that row's control slot -- always and only -- whatever
        // dictionary word it happens to spell (doc/boo-spec/logical-controls.adoc,
        // "Display Lines Govern Reflowed Prose Too").  The one-byte
        // dictionary tokens that spell the topic-metadata opcodes sit in the
        // same low value range as the length bytes, so the flattened
        // splitter opens a metadata segment on a byte that is really row
        // geometry.  After the `ST` title no metadata control is legitimate,
        // so a segment that is exactly one such length byte is the slot and
        // opens no control: SC31-711 3.3 record 94 token 0 (encoded value
        // 45, width 1) spells `cbacklevel` and token 119 (value 48) spells
        // `chdlevel`, and each opens a display line of the `SRWRN` warning
        // block; hosted (DT 19941010174546) serves that block as
        // `<em>Warning:</em> ... <em>impaired.</em>` with neither word and
        // with the row breaks those bytes carry.  The byte re-enters the
        // token stream so the display-row pass gives it the same row-control
        // slot it gives every other length byte.  Ahead of the title the
        // same holds for a segment that is nothing but the length byte, and
        // the byte is the title row's geometry, as it is when the envelope
        // consumes it: EZ12GMST 1.2.134 record 939 frames its `ST` line with
        // 0x31, which spells `csourcefn`, and the splitter leaves that byte
        // as an operand-less `csourcefn` segment between `SRHDRINET` and
        // `ST`.
        if (!title_seen && !segment.source_tokens.empty() &&
            std::all_of(segment.source_tokens.begin(),
                        segment.source_tokens.end(), [&](const auto token) {
                          return is_display_line_length_token(record, token);
                        })) {
          for (const auto token : segment.source_tokens)
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::envelope,
                               error))
              return false;
          break;
        }
        if (title_seen && is_display_line_length_opcode(record, segment)) {
          for (const auto token : segment.source_tokens) {
            Item item;
            item.kind = ItemKind::token;
            item.token = view_token(records, record_index, token);
            build.items.push_back(std::move(item));
          }
          Item end;
          end.kind = ItemKind::segment_end;
          build.items.push_back(std::move(end));
          break;
        }
        return fail(error, "body control " +
                               (segment.opcode.empty() ? std::string("<text>")
                                                       : segment.opcode) +
                               " is outside the prose model");
      }
      }
    }
    if (menu_open) {
      // Everything from the menu start onwards belongs to the menu.
      continue;
    }
    if (!emit_unclaimed(record.ir.tokens.size())) return false;
  }
  if (!title_seen) return fail(error, "topic has no ST title");
  if (split_title) return fail(error, "split ST title line has no title words");
  if (!pending_footnote_id.empty())
    return fail(error, "SR" + pending_footnote_id +
                           " is not followed by cz FLOW FN");
  return true;
}

} // namespace geist::detail::prose_internal
