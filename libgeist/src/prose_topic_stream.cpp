#include "geist/detail/prose_topic_internal.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/selector_link_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
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

// `c.sp` operands.  Corpus wide only two spellings occur: `<n> c` (the
// common vertical skip) and `<n>p p c`.  Neither carries display text.
bool vertical_space_operands(const std::string& lower_text) {
  std::size_t digits = 0;
  while (digits < lower_text.size() &&
         std::isdigit(static_cast<unsigned char>(lower_text[digits])) != 0)
    ++digits;
  if (digits == 0 || digits > 3) return false;
  const auto rest = lower_text.substr(digits);
  return rest == " c" || rest == "p p c";
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
// 19950308184737).  Anything else stays fail-closed.
bool front_matter_heading_form(const std::string& lower_form) {
  for (const char* form :
       {"abbrev", "abstract", "bibliog", "cover", "glossary", "index",
        "notices", "preface", "soa", "title", "toc", "vnotice"})
    if (lower_form == form) return true;
  return false;
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
  anchor.source = token_slice(record, segment.source_tokens.front(),
                              segment.source_tokens.back() + 1);
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
  {
    // The topic must still open with its metadata run: enough segments must
    // follow the start control for the eight required controls and the title.
    std::size_t available = 0;
    for (const auto& record : records)
      available += record.control_segments.size();
    if (records.front().control_segments.empty() ||
        available < required.size() + 1)
      return fail(error, "first record lacks the topic metadata envelope");
  }
  Cursor heading_cursor;
  for (std::size_t index = 0; index < required.size(); ++index) {
    if (at_end())
      return fail(error, "topic metadata controls are incomplete");
    // A message-section heading topic carries one bare `SRMSG` inside its
    // metadata (SC31-711 record 127, SC09-138 record 2066); the control has
    // no operand and no payload.
    if (current().kind == BookControlKind::message_start) {
      const auto& message = current();
      const auto message_record = cursor.record;
      ++cursor.segment;
      if (at_end())
        return fail(error, "topic metadata controls are incomplete");
      if (message.payload_range.begin != message.payload_range.end ||
          message.operand_range.begin != message.operand_range.end)
        return fail(error, "SRMSG inside the envelope carries operands");
      if (!assign_segment_tokens(records, ledger, message_record, message,
                                 ProseTokenRoleIR::envelope, false, error))
        return false;
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
    if (at_end())
      return fail(error, "topic metadata controls are incomplete");
    const auto& segment = current();
    const auto& record = records[cursor.record];
    const auto record_index = cursor.record;
    if (segment.kind != required[index])
      return fail(error, "topic metadata controls are incomplete or out of order");
    if (segment.kind == BookControlKind::heading_level)
      heading_cursor = cursor;
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
      for (const auto token : segment.source_tokens) {
        const auto view = view_token(records, record_index, token);
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
        if (is_padding(view) || is_separator(view) ||
            (view.width == 1 && punctuation_glyph_token(view))) {
          if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                             error))
            return false;
          continue;
        }
        if (ascii_lower(body_text(view)) == "st") {
          st_seen = true;
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
  const auto& heading_record = records[heading_cursor.record];
  auto heading_level = ascii_lower(trim_ascii(operand_text(
      heading_record,
      heading_record.control_segments[heading_cursor.segment].operand_range)));
  if (!heading_level.empty() && heading_level.front() == ':')
    heading_level.erase(heading_level.begin());
  envelope.heading_form = heading_level;
  if (heading_level.size() != 2 || heading_level.front() != 'h' ||
      heading_level.back() < '1' || heading_level.back() > '6') {
    if (!front_matter_heading_form(heading_level))
      return fail(error, "heading level '" + heading_level +
                             "' is not an h1-h6 prose heading");
    heading_level = "h1";
  }
  envelope.heading_level = heading_level;
  at_end();
  envelope.body_record = std::min(cursor.record, records.size() - 1);
  envelope.body_segment_begin =
      cursor.record < records.size()
          ? cursor.segment
          : records.back().control_segments.size();
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
  // Which dialect the topic is written in: a body that carries any `CZ`
  // control names every block boundary explicitly, a body that carries none
  // reconstructs its blocks from row geometry (Format/markup.md, "CZ layout
  // directives").  The two spell footnotes differently, so the dialect has
  // to be known before the first `SRFTN<id>` rather than discovered at it.
  const auto cz_dialect = std::any_of(
      records.begin(), records.end(), [](const auto& source) {
        return std::any_of(source.control_segments.begin(),
                           source.control_segments.end(),
                           [](const auto& segment) {
                             return segment.kind ==
                                    BookControlKind::layout_directive;
                           });
      });
  // Display lines per record, decoded once: a record whose lines parse
  // proves where every row-control length byte stands.
  std::map<std::size_t, std::optional<std::vector<DisplayLineIR>>> record_lines;
  const auto length_byte_at = [&](std::size_t record_index,
                                  std::size_t token) {
    auto entry = record_lines.find(record_index);
    if (entry == record_lines.end())
      entry = record_lines
                  .emplace(record_index,
                           record_display_lines(records[record_index]))
                  .first;
    if (!entry->second) return false;
    for (const auto& line : *entry->second)
      if (line.prefix_token == token) return true;
    return false;
  };
  // `SRFTN<id>` of the CZ dialect names the footnote the next `cz FLOW FN`
  // directive opens (packet 1.1 record 17).
  std::string pending_footnote_id;
  // Set once a `cz` directive has been collected: unclaimed one-cell
  // decoder separators then reach the display-row pass, which decides
  // between bullet slot, split-off text and padding.
  bool cz_seen = false;
  // Anchors that stood among the topic metadata controls precede every
  // anchor the body contributes, in source order.
  build.leading_anchors = envelope.leading_anchors;
  if (envelope.glued_title) {
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
        if (segment.kind == BookControlKind::layout_directive) {
          // The compiled menu sits inside the last open CZ list; its closer
          // and the next-level announcement follow it (SC09-2417-00 2.1
          // `cz OFF EUL 0 0`, `cz FLOW H2 3 3`).  They carry no text.
          const auto before = build.items.size();
          if (!collect_layout_directive(records, record_index, segment,
                                        pending_footnote_id, ledger,
                                        build.items, error))
            return false;
          for (auto index = before; index < build.items.size(); ++index)
            if (build.items[index].kind == ItemKind::token &&
                is_visible(build.items[index].token) &&
                !is_placeholder_run(build.items[index].token))
              return fail(error, "content follows the trailing menu");
          continue;
        }
        return fail(error, "content follows the trailing menu");
      }

      switch (segment.kind) {
      case BookControlKind::title: {
        if (title_seen)
          return fail(error, "topic carries a second ST control");
        if (segment.malformed) return fail(error, "ST control is malformed");
        title_seen = true;
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
      case BookControlKind::structural: {
        if (!segment.malformed && title_seen && cz_dialect &&
            lower_opcode.rfind("srftn", 0) == 0 && lower_opcode.size() > 5) {
          // Footnote start of the CZ dialect: like every `SR<id>` anchor the
          // id is the whole opcode after `SR` (packet 1.1 record 17
          // `SRFTNFTNUNIQ1`, targeted by `cselect 16 4 FTNFTNUNIQ1`); the
          // `FTN` prefix marks it as the footnote the next `cz FLOW FN`
          // body carries.
          if (!pending_footnote_id.empty())
            return fail(error, "SR" + pending_footnote_id +
                                   " is not closed before " + segment.opcode);
          if (!assign_segment_tokens(records, ledger, record_index, segment,
                                     ProseTokenRoleIR::control, false, error))
            return false;
          pending_footnote_id = segment.opcode.substr(2);
          if (!valid_anchor_id(pending_footnote_id))
            return fail(error, "footnote id '" + pending_footnote_id +
                                   "' is invalid");
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
          break;
        }
        if (!segment.malformed && title_seen && lower_opcode == "sreftn") {
          if (!assign_segment_tokens(records, ledger, record_index, segment,
                                     ProseTokenRoleIR::control, false, error))
            return false;
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
          const auto operands = operand_tokens(record, segment);
          std::string term;
          std::size_t first = npos;
          std::size_t last = 0;
          for (const auto token : segment.source_tokens) {
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
          if (first == npos)
            return fail(error, "SRGLS control has no source token");
          ProseAnchorIR anchor;
          anchor.id = "GLS";
          if (!term.empty()) anchor.id += " " + collapse_ascii_whitespace(term);
          anchor.source = token_slice(record, first, last + 1);
          Item item;
          item.kind = ItemKind::anchor;
          item.anchor_id = anchor.id;
          item.source = anchor.source;
          build.items.push_back(std::move(item));
          break;
        }
        // A `SRFTN<id>` of the flattened dialect is an ordinary body
        // anchor whose payload is the footnote text (see `sreftn` above).
        if (segment.malformed || lower_opcode.rfind("sr", 0) != 0 ||
            (reserved_structural(lower_opcode) &&
             !(!cz_dialect && lower_opcode.rfind("srftn", 0) == 0 &&
               lower_opcode.size() > 5)))
          return fail(error, "structural control " + segment.opcode +
                                 " is not a bare anchor");
        const auto id = segment.opcode.substr(2);
        if (!valid_anchor_id(id))
          return fail(error, "anchor id '" + id + "' is invalid");
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
        if (visible_payload && title_seen) {
          if (anchor_operands.empty())
            return fail(error, "anchor control has no source token");
          Item item;
          item.kind = ItemKind::anchor;
          item.anchor_id = id;
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
            if (!is_padding(view) && !is_separator(view) && !glyph_slot)
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
              if (pagination_control(ascii_lower(body_text(view))))
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
          const auto lines = record_display_lines(record);
          const auto line_of = [&](const std::size_t token)
              -> const DisplayLineIR* {
            if (!lines) return nullptr;
            for (const auto& candidate : *lines)
              if (token >= candidate.prefix_token &&
                  token < candidate.token_end)
                return &candidate;
            return nullptr;
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
        if (pagination_control(first_text)) {
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
          for (std::size_t position = 0; position < segment.source_tokens.size();
               ++position) {
            if (segment.source_tokens[position] != *first_visible) continue;
            if (position + 1 >= segment.source_tokens.size()) break;
            const auto candidate = segment.source_tokens[position + 1];
            const auto view = view_token(records, record_index, candidate);
            if (is_padding(view)) break;
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
                                 (token == *first_visible || token == consumed)
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
            if (!is_padding(view) && !is_separator(view) &&
                !(view.width == 1 && punctuation_glyph_token(view)))
              return fail(error, "SREFTN end marker carries visible payload '" +
                                     body_text(view) + "'");
            if (!ledger.assign(record_index, token, ProseTokenRoleIR::padding,
                               error))
              return false;
          }
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
        const auto continuation =
            first_text != "si" &&
            ((record_index != 0 && segment_index == 0) || !boundary_before());
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
        // dictionary word it happens to spell (Format/logical-controls.md,
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
        if (!segment.source_tokens.empty() &&
            length_byte_at(record_index, segment.source_tokens.front()) &&
            ascii_equals_case_insensitive(
                body_text(view_token(records, record_index,
                                     segment.source_tokens.front())),
                segment.opcode) &&
            segment.malformed) {
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
        if (lower_target.rfind("pic", 0) == 0)
          return fail(error, "selector targets a picture");
        const auto operands = operand_tokens(record, segment);
        if (operands.empty()) return fail(error, "selector has no source token");
        // A `LNK` selector carries its destination in the leading `<...>`
        // payload tokens; they are control metadata, never display text.
        std::size_t alternatives = 0;
        if (lower_target == "lnk") {
          std::vector<std::string> tokens;
          for (const auto token : segment.source_tokens) {
            if (std::binary_search(operands.begin(), operands.end(), token))
              continue;
            const auto text = body_text(view_token(records, record_index,
                                                   token));
            if (text.size() < 2 || text.front() != '<' || text.back() != '>')
              break;
            tokens.push_back(text);
          }
          std::string link_error;
          const auto link = parse_selector_link(tokens, &link_error);
          if (!link) return fail(error, "selector rejected: " + link_error);
          if (link->kind == SelectorLinkKindIR::external_image)
            return fail(error,
                        "inline external image selector outside a figure");
          alternatives = tokens.size();
          item.target = link->destination;
          item.target_kind = CrossReferenceTargetKindIR::external;
        }
        item.source = token_slice(record, operands.front(), operands.back() + 1);
        build.items.push_back(std::move(item));
        if (!push_payload(ProseTokenRoleIR::control, false, false, false,
                          alternatives))
          return false;
        break;
      }
      case BookControlKind::spacing: {
        // `c.sp` is the vertical-space control.  It has no display payload:
        // hosted BookServer emits only a paragraph break where it stands
        // (GC28-183 1.3.3 record 91 `c.sp 1 c`, DT 19930625102617;
        // SC33-033 4.6 record 177 `c.sp 1 c` between the heading and
        // `<a name="SPTCHAATT">`, DT 19930422134757; SC34-425 2.4.3 record
        // 1465, DT 19921112160049).  Two operand spellings occur corpus
        // wide: `<n> c` and `<n>p p c`; anything else fails closed.  The
        // trailing spacing tokens stay in the stream because they can carry
        // the paragraph break, the reading `c.cp` already uses.
        if (!title_seen) return fail(error, "c.sp control precedes the title");
        std::string operands;
        std::size_t last_operand = npos;
        std::optional<std::size_t> opcode_token;
        for (const auto token : segment.source_tokens) {
          const auto view = view_token(records, record_index, token);
          if (!is_visible(view)) continue;
          if (!opcode_token) {
            opcode_token = token;
            continue;
          }
          if (!operands.empty()) operands.push_back(' ');
          operands += ascii_lower(body_text(view));
          last_operand = token;
        }
        if (!opcode_token) return fail(error, "c.sp control has no opcode");
        if (!vertical_space_operands(operands))
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
      case BookControlKind::menu_start: {
        if (!title_seen) return fail(error, "menu precedes the title");
        menu_open = true;
        build.menu_record = record_index;
        build.menu_segment = segment_index;
        break;
      }
      case BookControlKind::layout_directive: {
        if (!title_seen)
          return fail(error, "layout directive precedes the title");
        if (!collect_layout_directive(records, record_index, segment,
                                      pending_footnote_id, ledger, build.items,
                                      error))
          return false;
        cz_seen = true;
        break;
      }
      default: {
        // Where a record's display lines parse, the length byte that opens a
        // line is that row's control slot -- always and only -- whatever
        // dictionary word it happens to spell (Format/logical-controls.md,
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
        // slot it gives every other length byte.
        if (title_seen && !segment.source_tokens.empty() &&
            length_byte_at(record_index, segment.source_tokens.front())) {
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
  if (!pending_footnote_id.empty())
    return fail(error, "SR" + pending_footnote_id +
                           " is not followed by cz FLOW FN");
  return true;
}

} // namespace geist::detail::prose_internal
