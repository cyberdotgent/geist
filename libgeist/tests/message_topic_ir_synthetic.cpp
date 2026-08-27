#include "geist/boo.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/message_topic_ir.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void open_context(const std::filesystem::path &path,
                  geist::detail::LogicalDecodeContext &context) {
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  context.directory.token_map_offset =
      geist::detail::read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      geist::detail::read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      geist::detail::read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      geist::detail::read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count =
      geist::detail::read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page =
      geist::detail::read_be16(context.bytes, base + 0x3a);
  context.decoded_records = geist::detail::decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
}

std::optional<geist::detail::MessageTopicIR>
extract(const geist::detail::LogicalDecodeContext &context, std::uint32_t first,
        std::uint32_t end, std::string *error = nullptr) {
  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  return geist::detail::extract_message_topic_ir(sources, layout, ownership,
                                                 error);
}

void verify_corpus_inventory() {
  const auto directory = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  std::vector<std::string> admitted;
  for (const auto &file : std::filesystem::directory_iterator(directory)) {
    if (!file.is_regular_file())
      continue;
    auto extension = file.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    if (extension != ".boo")
      continue;
    const auto book = geist::BooDocument::open(file.path());
    geist::detail::LogicalDecodeContext candidate_context;
    open_context(file.path(), candidate_context);
    for (const auto &candidate : book.topics()) {
      const auto first = candidate.start_logical_record;
      const auto end = candidate.end_logical_record;
      const auto has_message_control =
          std::any_of(candidate_context.decoded_records.begin() +
                          static_cast<std::ptrdiff_t>(first - 1),
                      candidate_context.decoded_records.begin() +
                          static_cast<std::ptrdiff_t>(end - 1),
                      [](const auto &record) {
                        return record.find("SRMSG") != std::string::npos;
                      });
      if (!has_message_control)
        continue;
      if (extract(candidate_context, first, end))
        admitted.push_back(file.path().filename().string() + ':' +
                           candidate.id);
    }
  }
  require(admitted == std::vector<std::string>{"SC31-711.boo:5.0"},
          "message whole-topic corpus admission set changed");
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  geist::detail::LogicalDecodeContext context;
  open_context(root / "SC31-711.boo", context);
  const auto sources =
      geist::detail::decode_logical_record_sources(context, 172, 435);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  std::string error;
  const auto topic = geist::detail::extract_message_topic_ir(sources, layout,
                                                             ownership, &error);
  require(topic.has_value(), error.empty()
                                 ? "complete message topic did not enter IR"
                                 : error.c_str());
  require(topic->first_logical_record == 172 &&
              topic->end_logical_record == 435 &&
              topic->metadata.raw_topic_id == "5.0" &&
              topic->metadata.topic_number == "71." &&
              topic->metadata.forward_level == "GLOSSARY" &&
              topic->metadata.back_level == "4.0" &&
              topic->metadata.heading_level == "H1" &&
              topic->metadata.source_file == "DFYL0MSG" &&
              topic->title == "Chapter 5. Messages",
          "message topic metadata or heading is incomplete");
  require(topic->catalog.entries.size() == 396 &&
              topic->catalog.entries.front().id == "023" &&
              topic->catalog.entries.back().id == "2505" &&
              topic->catalog.entries.back()
                      .sections.back()
                      .paragraphs.front()
                      .text == "None.",
          "message topic lost its complete catalog or terminal Action");
  require(topic->anchors.size() == 398 && topic->anchors.front().id == "MSG" &&
              topic->anchors[1].id == "HDRMSGS" &&
              topic->anchors[2].id == "MSG 023" &&
              topic->anchors.back().id == "MSG 2505",
          "message topic lost source anchors");
  require(
      topic->selectors.size() == 2 &&
          std::all_of(
              topic->selectors.begin(), topic->selectors.end(),
              [](const auto &selector) {
                return selector.target.kind ==
                           geist::detail::CrossReferenceTargetKindIR::anchor &&
                       selector.target.value == "HDRPROBS";
              }),
      "message introduction lost typed cross-reference targets");
  require(topic->heading_row_indices.size() == 2 &&
              topic->introduction_row_indices.size() == 20 &&
              !topic->rows.empty() && !topic->segments.empty(),
          "message heading/introduction or source ledgers are incomplete");
  require(topic->introduction.paragraphs.size() == 5 &&
              topic->introduction.paragraphs[0].atoms.size() == 1 &&
              topic->introduction.paragraphs[1].atoms.size() == 1 &&
              topic->introduction.paragraphs[2].atoms.size() == 1 &&
              topic->introduction.paragraphs[3].atoms.size() == 1 &&
              topic->introduction.paragraphs[4].atoms.size() == 5,
          "message introduction lost its five source-proven paragraphs");
  const auto &intro = topic->introduction.paragraphs;
  require(
      intro[0].atoms[0].text ==
              "This chapter lists the LNM for AIX messages you can receive "
              "when using LNM for AIX. Messages are listed by message ID "
              "number and include an explanation of the message and "
              "suggested actions." &&
          intro[1].atoms[0].text ==
              "Messages with numbers between 1000 and 1999 are sent to LNM "
              "for AIX from the LNM OS/2 agent program. Refer to the "
              "documentation that is provided with the LNM OS/2 agent for "
              "an explanation of the message and suggested actions to take "
              "to resolve the problem." &&
          intro[2].atoms[0].text ==
              "LNM for AIX appends a \"1\" to the front of the message "
              "number that it receives from the LNM OS/2 agent. Before "
              "consulting the LNM OS/2 agent documentation, identify the "
              "appropriate message number for the LNM OS/2 agent by removing "
              "the \"1\" from the number. For example, message number 1300 "
              "on LNM for AIX corresponds to message number 300 on the LNM "
              "OS/2 agent." &&
          intro[3].atoms[0].text ==
              "You can determine the process that generates a message by the "
              "software name given for the message in the formatted nettl "
              "log." &&
          intro[4].atoms[0].text ==
              "If you receive a message and are not able to find the message "
              "in this chapter, call IBM Service for more information. See ",
      "message introduction paragraph text is not source-canonical");
  require(intro[4].atoms[1].kind ==
                  geist::detail::MessageIntroductionAtomKindIR::selector &&
              intro[4].atoms[1].text == "Chapter 2, \"Problem" &&
              intro[4].atoms[1].target &&
              intro[4].atoms[1].target->kind ==
                  geist::detail::CrossReferenceTargetKindIR::anchor &&
              intro[4].atoms[1].target->value == "HDRPROBS" &&
              intro[4].atoms[2].kind ==
                  geist::detail::MessageIntroductionAtomKindIR::text &&
              intro[4].atoms[2].text == " " &&
              intro[4].atoms[3].kind ==
                  geist::detail::MessageIntroductionAtomKindIR::selector &&
              intro[4].atoms[3].text == "Determination\" in topic 2.0" &&
              intro[4].atoms[3].target &&
              intro[4].atoms[3].target->value == "HDRPROBS",
          "message introduction lost its two adjacent typed selectors");
  std::vector<std::size_t> introduction_claims(
      topic->introduction.cells.size());
  for (const auto &paragraph : topic->introduction.paragraphs)
    for (const auto &atom : paragraph.atoms)
      for (const auto cell : atom.cell_indices) {
        require(cell < introduction_claims.size(),
                "message introduction atom references an invalid cell");
        ++introduction_claims[cell];
      }
  for (std::size_t cell = 0; cell < introduction_claims.size(); ++cell) {
    const auto semantic =
        topic->introduction.cells[cell].role ==
            geist::detail::MessageIntroductionCellRoleIR::text ||
        topic->introduction.cells[cell].role ==
            geist::detail::MessageIntroductionCellRoleIR::selector;
    require(introduction_claims[cell] == (semantic ? 1u : 0u),
            "message introduction source-cell conservation is not exact");
  }
  require(std::count_if(topic->introduction.cells.begin(),
                        topic->introduction.cells.end(),
                        [](const auto &cell) {
                          return cell.role ==
                                 geist::detail::MessageIntroductionCellRoleIR::
                                     paragraph_break;
                        }) == 20,
          "message introduction lost its source-owned inline paragraph gap");

  std::set<std::pair<std::uint32_t, std::size_t>> segment_keys;
  for (const auto &segment : topic->segments)
    require(segment_keys
                .insert({segment.source.logical_record,
                         segment.source.segment_index})
                .second,
            "message segment ledger duplicates a source segment");
  std::size_t decoded_segment_count = 0;
  std::size_t source_token_count = 0;
  for (const auto &source : sources)
    decoded_segment_count += source.control_segments.size(),
        source_token_count += source.ir.tokens.size();
  require(segment_keys.size() == decoded_segment_count,
          "message segment ledger does not cover every decoded segment");
  std::set<std::pair<std::uint32_t, std::size_t>> source_token_keys;
  std::size_t separator_tokens = 0;
  for (const auto &token : topic->source_tokens) {
    require(source_token_keys.insert({token.logical_record, token.token_index})
                .second,
            "message payload-token ledger duplicates source ownership");
    if (!token.decoded_segment)
      ++separator_tokens;
  }
  require(source_token_keys.size() == source_token_count &&
              separator_tokens != 0,
          "message payload-token ledger lost source or separator tokens");

  require(geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                 *topic, &error),
          error.empty() ? "message topic verification failed" : error.c_str());
  require(geist::detail::format_message_topic_ir(*topic).find(
              "message_topic records=[172,435)") != std::string::npos,
          "message topic trace omitted its whole-record envelope");

  auto mutated = *topic;
  mutated.metadata.source_file = "changed";
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted mutated metadata");
  mutated = *topic;
  mutated.selectors.front().target.value = "OTHER";
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a mutated typed target");
  mutated = *topic;
  mutated.introduction.paragraphs.erase(
      mutated.introduction.paragraphs.begin() + 1);
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a dropped intro paragraph");
  mutated = *topic;
  std::swap(mutated.introduction.paragraphs[1],
            mutated.introduction.paragraphs[2]);
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted reordered intro paragraphs");
  mutated = *topic;
  mutated.introduction.paragraphs[4].atoms.erase(
      mutated.introduction.paragraphs[4].atoms.begin() + 2);
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted merged adjacent selectors");
  mutated = *topic;
  mutated.introduction.paragraphs[4].atoms[1].target->value = "OTHER";
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a mutated intro selector target");
  mutated = *topic;
  mutated.introduction.paragraphs[4].atoms[1].text += " changed";
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a mutated intro selector label");
  mutated = *topic;
  mutated.introduction.paragraphs[4].atoms[1].cell_indices.pop_back();
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a shortened selector source span");
  mutated = *topic;
  const auto paragraph_break = std::find_if(
      mutated.introduction.cells.begin(), mutated.introduction.cells.end(),
      [](const auto &cell) {
        return cell.role ==
               geist::detail::MessageIntroductionCellRoleIR::paragraph_break;
      });
  require(paragraph_break != mutated.introduction.cells.end(),
          "message introduction has no mutable paragraph gap");
  paragraph_break->role = geist::detail::MessageIntroductionCellRoleIR::layout;
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a mutated paragraph gap");
  mutated = *topic;
  const auto ordinary_padding = std::find_if(
      mutated.introduction.cells.begin(), mutated.introduction.cells.end(),
      [](const auto &cell) {
        return cell.role ==
                   geist::detail::MessageIntroductionCellRoleIR::layout &&
               cell.source_disposition ==
                   geist::detail::SourceDisposition::layout_padding;
      });
  require(ordinary_padding != mutated.introduction.cells.end(),
          "message introduction has no ordinary layout padding");
  ordinary_padding->role =
      geist::detail::MessageIntroductionCellRoleIR::paragraph_break;
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted an invented padding break");
  mutated = *topic;
  const auto lexical_period = std::find_if(
      mutated.introduction.cells.begin(), mutated.introduction.cells.end(),
      [](const auto &cell) {
        return cell.role ==
                   geist::detail::MessageIntroductionCellRoleIR::text &&
               cell.source_disposition ==
                   geist::detail::SourceDisposition::marker_slot &&
               cell.word == '.';
      });
  require(lexical_period != mutated.introduction.cells.end(),
          "message introduction has no lexical marker period");
  lexical_period->role = geist::detail::MessageIntroductionCellRoleIR::layout;
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted a suppressed lexical period");
  mutated = *topic;
  std::swap(mutated.anchors[0], mutated.anchors[1]);
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted reordered source anchors");
  mutated = *topic;
  mutated.introduction.paragraphs[4].atoms[0].cell_indices.push_back(
      mutated.introduction.paragraphs[4].atoms[1].cell_indices.front());
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted duplicated selected source text");
  mutated = *topic;
  mutated.catalog.entries.back().sections.back().paragraphs.front().text =
      "changed";
  require(!geist::detail::verify_message_topic_ir(sources, layout, ownership,
                                                  mutated),
          "message topic verifier admitted mutated terminal content");

  require(!extract(context, 172, 434, &error),
          "truncated message source entered whole-topic IR");
  require(!extract(context, 173, 435, &error),
          "message catalog without metadata entered whole-topic IR");
  require(!extract(context, 435, 518, &error),
          "glossary source entered message topic IR");
  verify_corpus_inventory();
}
