#include "geist/detail/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
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

std::vector<std::uint32_t> parse_direct_u16_index(
    const std::vector<std::uint8_t>& bytes,
    std::size_t root,
    std::size_t expected_count,
    bool include_terminal) {
  const auto value_count = expected_count + (include_terminal ? 1 : 0);
  if (root + 4 > bytes.size() || read_be16(bytes, root) != expected_count ||
      root + 4 + value_count * 2 > bytes.size()) {
    return {};
  }

  std::vector<std::uint32_t> values;
  values.reserve(value_count);
  for (std::size_t index = 0; index < value_count; ++index) {
    values.push_back(read_be16(bytes, root + 4 + index * 2));
  }
  return values;
}

std::vector<std::uint32_t> parse_content_page_record_starts(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  const auto root = static_cast<std::size_t>(directory.page_number) *
                        boo_page_size +
                    directory.content_page_index_offset;
  auto starts = parse_direct_u16_index(bytes,
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
  auto starts = parse_direct_u16_index(bytes,
                                       root,
                                       directory.stream_table_count,
                                       false);
  if (!starts.empty()) {
    starts.push_back(static_cast<std::uint32_t>(directory.logical_record_count) +
                     1);
  }
  return starts;
}

TokenWords assemble_logical_record(const std::vector<TokenWords>& tokens) {
  TokenWords output;
  std::uint16_t spacing_control = 2;

  const auto remove_pending_space = [&]() {
    if (!output.empty() && output.back() == ' ') {
      output.pop_back();
    }
  };

  for (const auto& token : tokens) {
    TokenWords words = token;
    spacing_control = words.empty() ? 3 : words.front();

    if (!words.empty() && words.front() < 4) {
      words.erase(words.begin());
      if (!output.empty()) {
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

    output.insert(output.end(), words.begin(), words.end());

    if (!words.empty() && words.back() == ' ') {
      spacing_control = 2;
    }
    if (spacing_control != 2) {
      output.push_back(' ');
    }
  }

  if (output.size() > 1 && spacing_control != 2) {
    output.pop_back();
  }

  if (!output.empty() && output.front() != ' ' && output.front() != 'S') {
    for (auto& word : output) {
      if (word == ' ' || word == '=' || word == 0) {
        break;
      }
      word = map_token_word_to_upper_ascii(word);
    }
  }

  return output;
}

std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record) {
  struct ControlKey {
    const char* canonical;
    const char* lower;
  };
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
    for (auto cursor = value_begin; cursor + 3 < decoded_record.size();
         ++cursor) {
      if (looks_like_control_boundary(decoded_record, lower_record, cursor)) {
        value_end = std::min(value_end, cursor);
        break;
      }
    }
    static const std::array<const char*, 8> auxiliary_boundaries = {
        "?csource=", "?cbasenum=", "?cdoclevel=", "?cfront=",
        "?ccontents=", "?cfigures=", "?ctables=", "?cindex="};
    for (const auto* boundary : auxiliary_boundaries) {
      const auto next = lower_record.find(boundary, value_begin);
      if (next != std::string::npos) {
        value_end = std::min(value_end, next);
      }
    }

    auto value =
        trim_ascii(decoded_record.substr(value_begin, value_end - value_begin));
    controls.push_back({key.canonical,
                        normalize_logical_control_value(key.canonical,
                                                        value)});
  }
  return controls;
}

std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  std::vector<std::string> records;
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
  for (std::size_t page = 0; page < page_count; ++page) {
    const auto page_base = page * boo_page_size;
    if (read_be16(bytes, page_base) == 0x0001) {
      candidate_pages.push_back(page);
    }
  }

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
      std::vector<TokenWords> record_tokens;
      for (auto cursor = length_offset; cursor < payload_end;) {
        const auto first = bytes[cursor++];
        if (first >= directory.token_threshold && cursor < payload_end) {
          const auto second = bytes[cursor++];
          const auto token_words = resolve_experimental_token(bytes,
                                                              directory,
                                                              token_strings,
                                                              first,
                                                              second);
          record_tokens.push_back(token_words);
        } else {
          const auto token_words = resolve_experimental_token(bytes,
                                                              directory,
                                                              token_strings,
                                                              first,
                                                              std::nullopt);
          record_tokens.push_back(token_words);
        }
      }

      const auto decoded_words = assemble_logical_record(record_tokens);
      records.push_back(token_words_to_ascii(decoded_words));
      record_offset = payload_end;
    }
  }

  return records;
}

std::vector<BooLogicalControl> extract_book_logical_controls(
    const std::vector<std::string>& decoded_records) {
  std::vector<BooLogicalControl> controls;
  for (const auto& decoded : decoded_records) {
    auto record_controls = extract_logical_controls(decoded);
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
