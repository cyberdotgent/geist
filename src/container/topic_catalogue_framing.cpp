// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/container/topic_catalogue_framing.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <cctype>

namespace geist::detail {

bool is_topic_header_record(const DecodedLogicalRecordSource &record) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (!is_topic_header_record(text))
    return false;
  if (!record_framing_is_decided(record.ir))
    return true;

  const auto begin = skip_decoded_separators(text);
  auto end = begin + 2;
  while (end < text.size()) {
    const auto ch = text[end];
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' &&
        ch != '_' && ch != '-')
      break;
    ++end;
  }
  ControlSegmentIR candidate;
  candidate.opcode_range = {begin, end};
  if (is_display_line_length_opcode(record, candidate) ||
      is_display_line_embedded_opcode(record, candidate))
    return false;
  const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                      candidate.opcode_range);
  const auto tokens = source_tokens_intersecting_output(record.assembled,
                                                        words.begin, words.end);
  if (tokens.empty())
    return false;
  const auto *line = display_line_of_token(record, tokens.front());
  return line != nullptr && tokens.front() == line->prefix_token + 1 &&
         std::all_of(tokens.begin(), tokens.end(), [&](const auto token) {
           return token > line->prefix_token && token < line->token_end;
         });
}

bool is_topic_header_record(const LogicalDecodeContext &context,
                            std::size_t record_index) {
  if (record_index >= context.decoded_records.size() ||
      !is_topic_header_record(context.decoded_records[record_index]))
    return false;
  const auto retained = context.topic_header_framing.find(record_index);
  if (retained != context.topic_header_framing.end())
    return retained->second != TopicHeaderFraming::non_native;
  // Legacy contexts without source payloads have no contrary framing proof.
  if (record_index >= context.record_payload_ranges.size())
    return true;
  const auto &range = context.record_payload_ranges[record_index];
  if (range.begin > range.end || range.end > context.bytes.size())
    return true;
  DecodedLogicalRecordSource source;
  source.logical_record = static_cast<std::uint32_t>(record_index + 1);
  source.ir = decode_record_payload_ir(
      context.bytes, context.directory, source_dictionary_for(context),
      range.begin, range.end, source.logical_record);
  if (!record_framing_is_decided(source.ir))
    return true;
  source.tokens = project_token_words(source.ir);
  source.assembled = assemble_logical_record_with_sources(source.tokens);
  return is_topic_header_record(source);
}

} // namespace geist::detail
