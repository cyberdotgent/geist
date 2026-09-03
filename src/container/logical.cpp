// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace geist::detail {

std::optional<std::uint16_t> read_compact_length(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::size_t end) {
  if (offset >= end) {
    return std::nullopt;
  }

  const auto first = bytes[offset++];
  if (first <= 0xEF) {
    return first;
  }
  if (offset >= end) {
    return std::nullopt;
  }

  const auto second = bytes[offset++];
  return static_cast<std::uint16_t>(((first - 0xF0) << 8) + second);
}

std::uint32_t physical_page_for_logical(const BooDirectory& directory,
                                        std::uint32_t logical_page) {
  if (logical_page == 0) {
    throw std::runtime_error("BOO logical page numbers are 1-based");
  }
  return directory.page_number + logical_page - 1;
}

TokenWords decode_dictionary_words(const std::vector<std::uint8_t>& bytes,
                                   std::size_t offset,
                                   std::size_t count) {
  TokenWords output;
  output.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    output.push_back(EbcdicCodec::cp500().decode_word(bytes[offset + i]));
  }
  return output;
}

void lowercase_positions(TokenWords& value,
                         const std::vector<std::uint8_t>& positions) {
  for (const auto position : positions) {
    if (position < value.size()) {
      value[position] = map_token_word_to_lower_ascii(value[position]);
    }
  }
}

void uppercase_positions(TokenWords& value,
                         const std::vector<std::uint8_t>& positions) {
  for (const auto position : positions) {
    if (position < value.size()) {
      value[position] = map_token_word_to_upper_ascii(value[position]);
    }
  }
}

void lowercase_words(TokenWords& value) {
  for (auto& word : value) {
    word = map_token_word_to_lower_ascii(word);
  }
}

void uppercase_words(TokenWords& value) {
  for (auto& word : value) {
    word = map_token_word_to_upper_ascii(word);
  }
}

void decode_dictionary_delta_range(
    std::map<std::uint16_t, TokenWords>& token_strings,
    std::uint16_t first_key,
    const std::vector<std::uint8_t>& bytes,
    std::size_t begin,
    std::size_t end) {
  if (begin >= end) {
    return;
  }

  auto cursor = begin;
  const auto base_count = bytes[cursor++];
  if (cursor + base_count > end) {
    return;
  }

  auto value = decode_dictionary_words(bytes, cursor, base_count);
  cursor += base_count;
  auto key = first_key;
  token_strings[key++] = value;

  while (cursor < end) {
    const auto op = bytes[cursor++];
    const auto mode = static_cast<std::uint8_t>(op >> 6);
    const auto count = static_cast<std::uint8_t>(op & 0x3F);

    if (mode == 0) {
      if (cursor + count > end) {
        break;
      }
      uppercase_words(value);
      const std::vector<std::uint8_t> positions(bytes.begin() + cursor,
                                                bytes.begin() + cursor + count);
      cursor += count;
      lowercase_positions(value, positions);
    } else if (mode == 1) {
      if (cursor >= end) {
        break;
      }
      const auto literal_count = static_cast<std::uint8_t>(bytes[cursor++] &
                                                           0x3F);
      if (cursor + literal_count > end) {
        break;
      }
      value.resize(std::min<std::size_t>(count, value.size()));
      lowercase_words(value);
      const auto literal_words =
          decode_dictionary_words(bytes, cursor, literal_count);
      value.insert(value.end(), literal_words.begin(), literal_words.end());
      cursor += literal_count;
    } else if (mode == 2) {
      if (cursor + count > end) {
        break;
      }
      lowercase_words(value);
      const std::vector<std::uint8_t> positions(bytes.begin() + cursor,
                                                bytes.begin() + cursor + count);
      cursor += count;
      uppercase_positions(value, positions);
    } else {
      if (cursor + count > end) {
        break;
      }
      lowercase_words(value);
      const auto literal_words = decode_dictionary_words(bytes, cursor, count);
      value.insert(value.end(), literal_words.begin(), literal_words.end());
      cursor += count;
    }

    token_strings[key++] = value;
  }
}

std::map<std::uint16_t, TokenWords> decode_experimental_dictionary(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  std::map<std::uint16_t, TokenWords> token_strings;
  const auto page_count = bytes.size() / boo_page_size;
  const auto dictionary_page_end =
      directory.dictionary_start_page +
      static_cast<std::uint32_t>(directory.dictionary_page_count);

  for (std::uint32_t logical_page = directory.dictionary_start_page;
       logical_page < dictionary_page_end;
       ++logical_page) {
    const auto page = physical_page_for_logical(directory, logical_page);
    if (page >= page_count) {
      continue;
    }
    const auto page_base = page * boo_page_size;
    if (read_be16(bytes, page_base) != 0x0100) {
      continue;
    }

    const auto used_end = read_be16(bytes, page_base + 2);
    std::size_t top_offset = page_base + 4;
    const auto page_end = page_base + std::min<std::size_t>(used_end,
                                                            boo_page_size);

    while (top_offset < page_end) {
      auto top_length_offset = top_offset;
      const auto top_length =
          read_compact_length(bytes, top_length_offset, page_end);
      if (!top_length || *top_length == 0 ||
          top_length_offset + *top_length > page_end) {
        break;
      }

      const auto top_payload = top_length_offset;
      const auto top_end = top_payload + *top_length;
      if (top_payload + 3 <= top_end) {
        const auto prefix_length = bytes[top_payload + 2];
        const auto nested_begin =
            top_payload + 3 + static_cast<std::size_t>(prefix_length);
        if (nested_begin < top_end) {
          std::size_t nested_offset = nested_begin;
          while (nested_offset < top_end) {
            auto nested_length_offset = nested_offset;
            const auto nested_length =
                read_compact_length(bytes, nested_length_offset, top_end);
            if (!nested_length || *nested_length == 0 ||
                nested_length_offset + *nested_length > top_end) {
              break;
            }

            const auto nested_payload = nested_length_offset;
            const auto nested_end = nested_payload + *nested_length;
            if (nested_payload + 2 < nested_end) {
              const auto key = read_be16(bytes, nested_payload);
              decode_dictionary_delta_range(token_strings,
                                            key,
                                            bytes,
                                            nested_payload + 2,
                                            nested_end);
            }
            nested_offset = nested_end;
          }
        }
      }

      top_offset = top_end;
    }
  }

  return token_strings;
}

TokenWords resolve_experimental_token(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::uint8_t first,
    std::optional<std::uint8_t> second) {
  std::uint16_t key = 0;
  if (second) {
    key = static_cast<std::uint16_t>((first << 8) | *second);
  } else {
    const auto token_map_entry =
        static_cast<std::size_t>(directory.page_number) * boo_page_size +
        directory.token_map_offset + static_cast<std::size_t>(first) * 2;
    if (token_map_entry + 2 > bytes.size()) {
      return {};
    }
    key = read_be16(bytes, token_map_entry);
  }

  const auto found = token_strings.find(key);
  if (found == token_strings.end()) {
    return {};
  }
  return found->second;
}

// Reads a 16-bit paged index (doc/boo-spec/topics.adoc, doc/boo-spec/toc.adoc).
// Each table is `count_be, next_be, value_be[count]`. When the root holds fewer
// than the expected values, `next_be` is the 1-based logical page (relative to
// the directory page) of a continuation table with the same layout at page
// offset 0; `0` ends the chain. Observed topic-start roots: GG24-395 (317
// topics: root 248 values, next 0x4e -> page 0x151), DREICMST (374: next 0x3e
// -> page 0x43), SC09-138 (546: next 0xd5 -> page 0x12a). Content-page roots
// of SC09-138, SC34-425, N2AH1MST, and SC24-5520-00 hold no values at all
// (`0000 <next>`) and point at one full table. With include_terminal the word
// following the final table's values is read as the terminal entry. Fails
// closed with an empty vector unless exactly the expected number of values is
// collected.
std::vector<std::uint32_t> parse_paged_u16_index(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    std::size_t root,
    std::size_t expected_count,
    bool include_terminal) {
  std::vector<std::uint32_t> values;
  values.reserve(expected_count + 1);
  std::set<std::size_t> visited;
  auto table = root;
  while (true) {
    if (table + 4 > bytes.size() || !visited.insert(table).second) {
      return {};
    }
    const auto count = static_cast<std::size_t>(read_be16(bytes, table));
    const auto next = read_be16(bytes, table + 2);
    if (values.size() + count > expected_count ||
        table + 4 + count * 2 > bytes.size()) {
      return {};
    }
    for (std::size_t index = 0; index < count; ++index) {
      values.push_back(read_be16(bytes, table + 4 + index * 2));
    }
    if (values.size() == expected_count) {
      if (include_terminal) {
        const auto terminal = table + 4 + count * 2;
        if (terminal + 2 > bytes.size()) {
          return {};
        }
        values.push_back(read_be16(bytes, terminal));
      }
      return values;
    }
    if (next == 0) {
      return {};
    }
    table = static_cast<std::size_t>(
                physical_page_for_logical(directory, next)) *
            boo_page_size;
  }
}

std::vector<std::uint32_t> parse_content_page_record_starts(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  const auto root = static_cast<std::size_t>(directory.page_number) *
                        boo_page_size +
                    directory.content_page_index_offset;
  auto starts = parse_paged_u16_index(bytes,
                                      directory,
                                      root,
                                      directory.content_page_count,
                                      true);
  // Unlike the per-page entries, the final word is the inclusive total
  // logical-record number rather than the next page's first record.
  if (!starts.empty()) {
    ++starts.back();
  }
  return starts;
}

std::vector<std::uint32_t> parse_topic_record_starts(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  const auto root = static_cast<std::size_t>(directory.page_number) *
                        boo_page_size +
                    directory.stream_table_offset;
  auto starts = parse_paged_u16_index(bytes,
                                      directory,
                                      root,
                                      directory.stream_table_count,
                                      false);
  if (!starts.empty()) {
    starts.push_back(static_cast<std::uint32_t>(directory.logical_record_count) +
                     1);
  }
  return starts;
}

AssembledLogicalRecord assemble_logical_record_with_sources(
    const std::vector<TokenWords>& tokens) {
  AssembledLogicalRecord assembled;
  std::uint16_t spacing_control = 2;

  const auto remove_pending_space = [&]() {
    if (!assembled.words.empty() && assembled.words.back() == ' ') {
      const auto source_token = assembled.sources.back().token_index;
      assembled.words.pop_back();
      assembled.sources.pop_back();
      if (source_token < assembled.tokens.size()) {
        assembled.tokens[source_token].output_end = assembled.words.size();
      }
    }
  };

  assembled.tokens.reserve(tokens.size());
  for (std::size_t token_index = 0; token_index < tokens.size();
       ++token_index) {
    const auto& token = tokens[token_index];
    TokenWords words = token;
    spacing_control = words.empty() ? 3 : words.front();
    const auto has_control = !words.empty() && words.front() < 4;
    const auto effective_spacing_control =
        has_control ? words.front() : std::uint16_t{3};
    const auto first_source_word = has_control ? std::size_t{1} : 0;
    LogicalTokenSpan token_span;
    token_span.token_index = token_index;
    token_span.has_control = has_control;
    token_span.spacing_control = effective_spacing_control;
    token_span.control_only = has_control && words.size() == 1;

    if (has_control) {
      words.erase(words.begin());
      if (!assembled.words.empty()) {
        if (spacing_control == 1) {
          remove_pending_space();
          if (words.empty()) {
            spacing_control = 2;
          }
        } else if (spacing_control == 0) {
          remove_pending_space();
          spacing_control = 2;
        }
      }
    }
    token_span.output_begin = assembled.words.size();

    assembled.words.insert(assembled.words.end(), words.begin(), words.end());
    for (std::size_t word_index = 0; word_index < words.size(); ++word_index) {
      assembled.sources.push_back(
          {token_index,
           first_source_word + word_index,
           LogicalWordSourceKind::token_word,
           has_control,
           effective_spacing_control});
    }

    if (!words.empty() && words.back() == ' ') {
      spacing_control = 2;
    }
    if (spacing_control != 2) {
      assembled.words.push_back(' ');
      assembled.sources.push_back(
          {token_index,
           token.size(),
           LogicalWordSourceKind::inserted_space,
           has_control,
           effective_spacing_control});
    }
    token_span.output_end = assembled.words.size();
    assembled.tokens.push_back(token_span);
  }

  if (assembled.words.size() > 1 && spacing_control != 2) {
    assembled.words.pop_back();
    assembled.sources.pop_back();
    if (!assembled.tokens.empty()) {
      assembled.tokens.back().output_end = assembled.words.size();
    }
  }

  if (!assembled.words.empty() && assembled.words.front() != ' ' &&
      assembled.words.front() != 'S') {
    for (auto& word : assembled.words) {
      if (word == ' ' || word == '=' || word == 0) {
        break;
      }
      word = map_token_word_to_upper_ascii(word);
    }
  }

  return assembled;
}

TokenWords assemble_logical_record(const std::vector<TokenWords>& tokens) {
  return assemble_logical_record_with_sources(tokens).words;
}

namespace {

// True when `[begin, end)` of the projection holds anything a control value
// could be made of.  A separator token never does: it is punctuation, spaces,
// or the `?` an unrepresentable word projects to.
bool token_span_carries_a_word(const std::string& decoded_record,
                               std::size_t begin,
                               std::size_t end) {
  // A word needs a letter or a digit. The separator between a control's
  // value and the next control's key is spelled by tokens of its own, and
  // what they render as varies from book to book: a comma, a run of spaces,
  // a placeholder run, but also `(`, `-` or `|`. Naming the characters seen
  // so far and treating the rest as text is how `ctitle=` in a book whose
  // separator happened to be `(` came out as the title "(" -- a book with an
  // empty title then showed a bracket everywhere its name belonged.
  //
  // None of the keys this reads -- language, version, title, copyright,
  // date, document number and the rest -- has a value that is punctuation
  // alone, so requiring one alphanumeric character costs nothing and stops
  // guessing at the separator alphabet.
  for (auto cursor = begin; cursor < end && cursor < decoded_record.size();
       ++cursor) {
    const auto ch = static_cast<unsigned char>(decoded_record[cursor]);
    if (std::isalnum(ch) != 0) {
      return true;
    }
  }
  return false;
}

} // namespace

std::vector<std::size_t> assembled_token_output_offsets(
    const AssembledLogicalRecord& assembled) {
  std::vector<std::size_t> word_offsets(assembled.words.size() + 1, 0);
  for (std::size_t index = 0; index < assembled.words.size(); ++index) {
    word_offsets[index + 1] =
        word_offsets[index] + token_word_ascii_width(assembled.words[index]);
  }
  std::vector<std::size_t> offsets;
  offsets.reserve(assembled.tokens.size());
  for (const auto& token : assembled.tokens) {
    if (token.output_begin < word_offsets.size()) {
      offsets.push_back(word_offsets[token.output_begin]);
    }
  }
  return offsets;
}

std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record,
    const std::vector<std::size_t>& token_offsets) {
  struct ControlKey {
    const char* canonical;
    const char* lower;
  };
  // A key absent from the header and a key present with nothing after it are
  // different readings, and both come back as an empty property.  The five
  // BMC Software books -- the only non-IBM publisher in the sample corpus --
  // are the second case, not a gap in this table: their headers state
  //
  //   ... ctitle=BMC Software, Inc.: All BMC Code Message - Book 1
  //   cstitle=BMC MSGS Bk1 - ccopyright=(C) Copyright BMC Corporation 2001,
  //   csecurity=, cresmat1=, cresmat2=, cresmat3=, cdate=, cauthor=,
  //   caline=, cdocnum=, cbasenum=, cdoclevel=, cfront=FRONT, ...
  //
  // (`bmcmst1.boo` record 1; `bmcmst2`, `bmcmst3`, `bmcetam` and `bmcoper`
  // carry the identical run, differing only in `csource=` and `ctitle=`).
  // The publisher writes the key and leaves the value blank, alongside blank
  // `cdate=` and `cauthor=`, and states no number under any other spelling --
  // `cbasenum=` and `cdoclevel=`, which IBM books use to restate the number,
  // are blank in the same run, and no record of any of the five books holds a
  // non-empty one.  So the empty document number is what those books say, and
  // reading a further key would not recover one (issue #95).
  static const std::array<ControlKey, 11> keys = {{
      {"CLANGUAGE", "clanguage="},
      {"CVERSION", "cversion="},
      {"CBLDVERS", "cbldvers="},
      {"CREFLOW", "creflow="},
      {"CTITLE", "ctitle="},
      {"CSTITLE", "cstitle="},
      {"CCOPYRIGHT", "ccopyright="},
      {"CSECURITY", "csecurity="},
      {"CDATE", "cdate="},
      {"CAUTHOR", "cauthor="},
      {"CDOCNUM", "cdocnum="},
  }};

  std::vector<BooLogicalControl> controls;
  const auto lower_record = ascii_lower(decoded_record);
  for (const auto& key : keys) {
    const std::string key_text(key.lower);
    const auto found = lower_record.find(key_text);
    if (found == std::string::npos) {
      continue;
    }

    auto value_begin = found + key_text.size();
    auto value_end = decoded_record.size();
    for (const auto& next_key : keys) {
      const auto next = lower_record.find(next_key.lower, value_begin);
      if (next != std::string::npos) {
        value_end = std::min(value_end, next);
      }
    }
    // Any later token that itself opens a `c<name>=` key ends this value,
    // whatever the separator between them renders as. The keys above are the
    // ones the book properties read; the auxiliary keys around them
    // (`csource=`, `cbasenum=`, `cfontdef=`, ...) are not read but still close
    // the value that precedes them.
    const auto first_later_token = static_cast<std::size_t>(
        std::lower_bound(token_offsets.begin(), token_offsets.end(),
                         value_begin) -
        token_offsets.begin());
    for (auto index = first_later_token; index < token_offsets.size();
         ++index) {
      // A token that opens exactly at the value's end is the next control
      // itself: the separator in front of it still has to leave the value.
      if (token_offsets[index] > value_end) {
        break;
      }
      if (!control_key_begins_at(decoded_record, lower_record,
                                 token_offsets[index])) {
        continue;
      }
      // The tokens between the value and the key spell the separator, and
      // belong to neither.  Whatever they render as -- a comma, a run of
      // spaces, a placeholder -- they carry no word of the value, so the
      // value ends where that run begins.
      auto boundary = token_offsets[index];
      while (index > first_later_token &&
             token_offsets[index - 1] >= value_begin &&
             !token_span_carries_a_word(decoded_record,
                                        token_offsets[index - 1], boundary)) {
        boundary = token_offsets[index - 1];
        --index;
      }
      value_end = boundary;
      break;
    }

    auto value =
        trim_ascii(decoded_record.substr(value_begin, value_end - value_begin));
    controls.push_back({key.canonical,
                        normalize_logical_control_value(key.canonical,
                                                        value)});
  }
  return controls;
}

namespace {

// Decodes one token at `cursor`.  A byte at or above the book's token
// threshold starts a two-byte dictionary reference as long as a second byte
// is available before `two_byte_end`.
LogicalTokenIR decode_one_token(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t& cursor,
    std::size_t two_byte_end,
    std::size_t token_index) {
  const auto token_begin = cursor;
  const auto first = bytes[cursor++];
  LogicalTokenIR token;
  token.token_index = token_index;
  if (first >= directory.token_threshold && cursor < two_byte_end) {
    const auto second = bytes[cursor++];
    token.encoded = {static_cast<std::uint16_t>((first << 8) | second), 2};
    token.decoded_words =
        resolve_experimental_token(bytes, directory, token_strings, first,
                                   second);
  } else {
    token.encoded = {first, 1};
    token.decoded_words = resolve_experimental_token(
        bytes, directory, token_strings, first, std::nullopt);
  }
  token.byte_range = {static_cast<std::uint32_t>(token_begin),
                      static_cast<std::uint32_t>(cursor)};
  token.has_spacing_control =
      !token.decoded_words.empty() && token.decoded_words.front() < 4;
  token.spacing_control =
      token.has_spacing_control ? token.decoded_words.front()
                                : std::uint16_t{3};
  for (std::size_t word = 0; word < token.decoded_words.size(); ++word)
    if (token.decoded_words[word] == std::numeric_limits<std::uint16_t>::max())
      token.unmapped_word_indices.push_back(word);
  return token;
}

// Re-decodes the payload as the length-prefixed display lines it is
// (`doc/boo-spec/logical-controls.adoc`, "Display Lines Inside A Record Payload"):
// one byte of line length, then exactly that many bytes of tokens.  The
// plain left-to-right walk cannot express a length byte that is itself at or
// above the token threshold -- it swallows the line's first content byte
// into a two-byte dictionary reference and every following line prefix lands
// mid-token (PRG1SORT record 47 byte 0xe5ae is length 0xdc but reads as
// token 0xdc18 `classification`, a word hosted never prints).  Declines
// unless every line lands exactly on a token boundary.
std::optional<std::vector<LogicalTokenIR>> decode_display_line_tokens(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t payload_begin,
    std::size_t payload_end) {
  std::vector<LogicalTokenIR> tokens;
  for (auto cursor = payload_begin; cursor < payload_end;) {
    const auto length = bytes[cursor];
    const auto line_end = cursor + 1 + length;
    if (line_end > payload_end) return std::nullopt;
    auto prefix_cursor = cursor;
    tokens.push_back(decode_one_token(bytes, directory, token_strings,
                                      prefix_cursor, cursor + 1,
                                      tokens.size()));
    cursor = prefix_cursor;
    while (cursor < line_end)
      tokens.push_back(decode_one_token(bytes, directory, token_strings,
                                        cursor, payload_end, tokens.size()));
    if (cursor != line_end) return std::nullopt;
  }
  return tokens;
}

} // namespace

LogicalRecordIR decode_record_payload_ir(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t payload_begin,
    std::size_t payload_end,
    std::uint32_t logical_record) {
  LogicalRecordIR record;
  record.logical_record = logical_record;
  record.payload_range = {
      static_cast<std::uint32_t>(payload_begin),
      static_cast<std::uint32_t>(payload_end),
  };
  for (auto cursor = payload_begin; cursor < payload_end;)
    record.tokens.push_back(decode_one_token(bytes, directory, token_strings,
                                             cursor, payload_end,
                                             record.tokens.size()));
  // The plain walk is authoritative wherever it already agrees with the
  // record's own display-line structure; only a record whose line prefixes
  // land mid-token is re-decoded line by line, and only when that re-decode
  // consumes every line exactly.
  if (!token_display_lines(record.tokens, record.payload_range.end)) {
    auto relined = decode_display_line_tokens(bytes, directory, token_strings,
                                              payload_begin, payload_end);
    if (relined && token_display_lines(*relined, record.payload_range.end))
      record.tokens = std::move(*relined);
  }
  // The framing is decided here, once, and stored on the record: every
  // consumer reads it instead of walking the payload again.
  assign_display_line_framing(record);
  return record;
}

// Re-decodes the token run stored in `[byte_begin, byte_end)` of the BOO
// file straight from the file bytes, with no record, layout, or semantic
// context.  A provenance slice names a byte range that begins and ends on a
// token boundary, so a caller can prove a rendered element against the file
// by decoding exactly those bytes again.  Returns nothing when the window is
// outside the file or does not tile into whole tokens.
std::optional<std::vector<LogicalTokenIR>> decode_source_byte_range_tokens(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t byte_begin,
    std::size_t byte_end) {
  if (byte_begin > byte_end || byte_end > bytes.size()) return std::nullopt;
  std::vector<LogicalTokenIR> tokens;
  for (auto cursor = byte_begin; cursor < byte_end;) {
    tokens.push_back(decode_one_token(bytes, directory, token_strings, cursor,
                                      byte_end, tokens.size()));
    if (cursor > byte_end) return std::nullopt;
  }
  return tokens;
}

const std::map<std::uint16_t, TokenWords>& source_dictionary_for(
    const LogicalDecodeContext& context) {
  // The dictionary is a pure function of the file bytes and the directory,
  // both immutable once `open()` has returned, so it is computed into a fresh
  // value and published atomically rather than locked: readers see either
  // nothing or the finished map, and a concurrent second build is discarded.
  // Not holding a lock here matters -- `decode_logical_record_sources` used to
  // decode a whole topic under this mutex, which serialised every thread that
  // touched a source decode.
  return *publish_once(
      context.source_dictionary,
      [&context]() -> std::shared_ptr<const std::map<std::uint16_t, TokenWords>> {
        return std::make_shared<const std::map<std::uint16_t, TokenWords>>(
            decode_experimental_dictionary(context.bytes, context.directory));
      });
}

std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    std::vector<LogicalRecordPayloadRange>* payload_ranges,
    std::vector<std::vector<std::size_t>>* header_token_offsets,
    bool stop_after_book_header) {
  std::vector<std::string> records;
  if (payload_ranges != nullptr) {
    payload_ranges->clear();
  }
  // Token offsets are only kept while the book header is still open; a whole
  // book's worth would be several megabytes nothing reads.
  bool header_open = header_token_offsets != nullptr;
  if (header_open) {
    header_token_offsets->clear();
  }
  const auto token_strings = decode_experimental_dictionary(bytes, directory);
  if (token_strings.empty()) {
    return records;
  }

  std::vector<std::size_t> candidate_pages;
  const auto content_page_end =
      directory.content_start_page +
      static_cast<std::uint32_t>(directory.content_page_count);
  for (std::uint32_t logical_page = directory.content_start_page;
       logical_page < content_page_end;
       ++logical_page) {
    candidate_pages.push_back(physical_page_for_logical(directory,
                                                        logical_page));
  }

  const auto page_count = bytes.size() / boo_page_size;
  for (const auto page : candidate_pages) {
    if (page >= page_count) {
      continue;
    }

    const auto page_base = page * boo_page_size;
    const auto page_class = read_be16(bytes, page_base);
    if (page_class != 0x0000 && page_class != 0x0001) {
      continue;
    }

    const auto used_end = read_be16(bytes, page_base + 2);
    const auto page_end = page_base + std::min<std::size_t>(used_end,
                                                            boo_page_size);
    std::size_t record_offset = page_base + 4;
    while (record_offset < page_end) {
      auto length_offset = record_offset;
      const auto record_length =
          read_compact_length(bytes, length_offset, page_end);
      if (!record_length || length_offset + *record_length > page_end) {
        break;
      }

      const auto payload_end = length_offset + *record_length;
      const auto record_ir = decode_record_payload_ir(
          bytes, directory, token_strings, length_offset, payload_end,
          static_cast<std::uint32_t>(records.size() + 1));
      std::string ir_error;
      if (!verify_token_ir(record_ir, &ir_error)) {
        throw std::runtime_error("invalid logical token IR: " + ir_error);
      }
      const auto record_tokens = project_token_words(record_ir);

      const auto assembled =
          assemble_logical_record_with_sources(record_tokens);
      records.push_back(token_words_to_ascii(assembled.words));
      if (header_open) {
        header_token_offsets->push_back(
            assembled_token_output_offsets(assembled));
        // The book header's controls are read up to and including the record
        // that files `cdocnum=`, and in no case past the first topic; nothing
        // after either point is a header control.
        if (ascii_contains_case_insensitive(records.back(), "cdocnum=") ||
            is_topic_header_record(records.back())) {
          header_open = false;
        }
      }
      if (payload_ranges != nullptr) {
        payload_ranges->push_back(
            {static_cast<std::uint32_t>(length_offset),
             static_cast<std::uint32_t>(payload_end)});
      }
      // A caller after the book's own properties has everything it can learn
      // once the header closes; the rest of the stream is topics.  Returning
      // here leaves every out-parameter consistent with `records`.
      if (stop_after_book_header && !header_open) {
        return records;
      }
      record_offset = payload_end;
    }
  }

  return records;
}

std::vector<DecodedLogicalRecordSource>
decode_logical_record_sources(const LogicalDecodeContext& context,
                              std::uint32_t first_logical_record,
                              std::uint32_t end_logical_record) {
  std::vector<DecodedLogicalRecordSource> records;
  if (first_logical_record == 0 ||
      end_logical_record < first_logical_record ||
      end_logical_record > context.record_payload_ranges.size() + 1) {
    return records;
  }
  // The dictionary is published once and then immutable, so the decode below
  // holds no lock at all: several threads may decode different topics of one
  // book at the same time.
  const auto& dictionary = source_dictionary_for(context);

  records.reserve(end_logical_record - first_logical_record);
  for (auto logical_record = first_logical_record;
       logical_record < end_logical_record;
       ++logical_record) {
    const auto& range = context.record_payload_ranges[logical_record - 1];
    if (range.begin > range.end || range.end > context.bytes.size()) {
      return {};
    }
    DecodedLogicalRecordSource decoded;
    decoded.logical_record = logical_record;
    decoded.ir = decode_record_payload_ir(context.bytes,
                                          context.directory,
                                          dictionary,
                                          range.begin,
                                          range.end,
                                          logical_record);
    if (!verify_token_ir(decoded.ir)) {
      return {};
    }
    decoded.tokens = project_token_words(decoded.ir);
    decoded.encoded_tokens = project_encoded_tokens(decoded.ir);
    decoded.assembled = assemble_logical_record_with_sources(decoded.tokens);
    decoded.control_segments =
        decode_control_segments(logical_record, decoded.assembled,
                                decoded.encoded_tokens,
                                decoded.ir.display_lines);
    demote_display_line_owned_controls(decoded);
    std::string segment_error;
    if (!verify_control_segments(decoded.assembled, decoded.control_segments,
                                 &segment_error)) {
      throw std::runtime_error("invalid control segment IR in logical record " +
                               std::to_string(logical_record) + ": " +
                               segment_error);
    }
    records.push_back(std::move(decoded));
  }
  return records;
}

std::vector<BooLogicalControl> extract_book_logical_controls(
    const std::vector<std::string>& decoded_records,
    const std::vector<std::vector<std::size_t>>& record_token_offsets) {
  static const std::vector<std::size_t> no_token_offsets;
  std::vector<BooLogicalControl> controls;
  for (std::size_t index = 0; index < decoded_records.size(); ++index) {
    const auto& decoded = decoded_records[index];
    auto record_controls = extract_logical_controls(
        decoded, index < record_token_offsets.size()
                     ? record_token_offsets[index]
                     : no_token_offsets);
    const auto has_docnum =
        std::any_of(record_controls.begin(),
                    record_controls.end(),
                    [](const BooLogicalControl& control) {
                      return control.key == "CDOCNUM";
                    });
    controls.insert(controls.end(),
                    record_controls.begin(),
                    record_controls.end());
    if (has_docnum) {
      return controls;
    }
  }
  return controls;
}

const TopicData* find_topic_data(const std::vector<TopicData>& topics,
                                 const std::string& topic_id) {
  const auto normalized_id = normalize_toc_id(topic_id);
  const auto found = std::find_if(topics.begin(), topics.end(),
                                  [&](const TopicData& topic) {
                                    return topic.id == normalized_id ||
                                           ascii_equals_case_insensitive(
                                               topic.id, topic_id);
                                  });
  if (found == topics.end()) {
    return nullptr;
  }
  return &*found;
}

} // namespace geist::detail
