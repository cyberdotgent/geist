#include "geist/detail/comment_delivery_ir.hpp"
#include "test_failures.hpp"
#include "geist/detail/internal.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
    return;
  }
}

struct LoadedBook {
  geist::BooDocument document;
  geist::detail::LogicalDecodeContext context;

  explicit LoadedBook(const std::filesystem::path& path)
      : document(geist::BooDocument::open(path)) {
    context.bytes = geist::detail::read_file(path);
    const auto directory_page = geist::detail::read_be16(context.bytes, 0);
    const auto base = static_cast<std::size_t>(directory_page) *
                      geist::boo_page_size;
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
    context.decoded_records =
        geist::detail::decode_experimental_logical_records(
            context.bytes, context.directory, &context.record_payload_ranges);
  }
};

std::unique_ptr<LoadedBook> load_book(const std::filesystem::path& path) {
  return std::make_unique<LoadedBook>(path);
}

std::optional<geist::detail::CommentDeliveryIR> extract_topic(
    LoadedBook& book, const geist::TopicInfo& topic, std::string* error,
    std::vector<geist::detail::DecodedLogicalRecordSource>* sources_out =
        nullptr,
    geist::detail::LayoutIR* layout_out = nullptr,
    geist::detail::OwnershipIR* ownership_out = nullptr) {
  auto sources = geist::detail::decode_logical_record_sources(
      book.context, topic.start_logical_record, topic.end_logical_record);
  auto layout = geist::detail::extract_layout_ir(sources);
  auto ownership = geist::detail::build_ownership_ir(sources, layout);
  const auto result = geist::detail::extract_comment_delivery_ir(
      sources, layout, ownership, error);
  if (sources_out != nullptr) *sources_out = std::move(sources);
  if (layout_out != nullptr) *layout_out = std::move(layout);
  if (ownership_out != nullptr) *ownership_out = std::move(ownership);
  return result;
}

const geist::TopicInfo* topic(const LoadedBook& book, const std::string& id) {
  const auto found = std::find_if(
      book.document.topics().begin(), book.document.topics().end(),
      [&](const auto& candidate) { return candidate.id == id; });
  return found == book.document.topics().end() ? nullptr : &*found;
}

std::vector<const geist::detail::CommentSourceFragmentIR*> semantic_affixes(
    const geist::detail::CommentDeliveryIR& delivery) {
  std::vector<const geist::detail::CommentSourceFragmentIR*> result;
  for (const auto& block : delivery.blocks)
    for (const auto& line : block.lines)
      for (const auto& field : line.fields)
        for (const auto& affix : field.affixes) result.push_back(&affix);
  return result;
}

bool has_affix(const geist::detail::CommentDeliveryIR& delivery,
               std::uint32_t record, std::size_t token,
               std::string_view text, std::uint32_t begin,
               std::uint32_t end,
               geist::detail::CommentAffixAttachment attachment,
               geist::detail::CommentAffixSpacing spacing) {
  const auto affixes = semantic_affixes(delivery);
  return std::any_of(affixes.begin(), affixes.end(), [&](const auto* affix) {
    return affix->logical_record == record && affix->token_index == token &&
           affix->text == text && affix->byte_begin == begin &&
           affix->byte_end == end && affix->attachment == attachment &&
           affix->spacing == spacing;
  });
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  auto sc31 = load_book(root / "SC31-711.boo");
  std::string error;

  const auto* back_2 = topic(*sc31, "BACK_2");
  require(back_2 != nullptr, "SC31 fixture has no BACK_2 topic");
  std::vector<geist::detail::DecodedLogicalRecordSource> delivery_sources;
  geist::detail::LayoutIR delivery_layout;
  geist::detail::OwnershipIR delivery_ownership;
  const auto delivery = extract_topic(
      *sc31, *back_2, &error, &delivery_sources, &delivery_layout,
      &delivery_ownership);
  require(delivery.has_value(),
          error.empty() ? "BACK_2 did not enter comment IR" : error.c_str());
  require(delivery &&
              delivery->kind ==
                  geist::detail::CommentDeliveryKind::delivery_instructions &&
              delivery->blocks.size() == 2 &&
              delivery->blocks[0].kind ==
                  geist::detail::CommentDeliveryBlockKind::title_page &&
              delivery->blocks[0].lines.size() == 21 &&
              delivery->blocks[1].kind ==
                  geist::detail::CommentDeliveryBlockKind::
                      delivery_instructions &&
              delivery->blocks[1].lines.size() == 12,
          "BACK_2 semantic block shape is incomplete");
  require(delivery && geist::detail::verify_comment_delivery_ir(
                          delivery_sources, delivery_layout,
                          delivery_ownership, *delivery, &error),
          error.empty() ? "BACK_2 semantic verifier failed" : error.c_str());
  require(delivery &&
              delivery->blocks[0].lines[10].marker_disposition ==
                  geist::detail::CommentMarkerDisposition::lexical_content &&
              delivery->blocks[0].lines[10].marker->decoded_text == "the" &&
              delivery->blocks[0].lines[15].marker_disposition ==
                  geist::detail::CommentMarkerDisposition::lexical_content &&
              delivery->blocks[0].lines[15].marker->decoded_text == "to" &&
              delivery->blocks[0].lines[20].marker_disposition ==
                  geist::detail::CommentMarkerDisposition::lexical_content &&
              delivery->blocks[0].lines[20].marker->decoded_text == "or" &&
              delivery->blocks[0].lines[18].marker_disposition ==
                  geist::detail::CommentMarkerDisposition::layout_artifact &&
              delivery->blocks[0].lines[18].marker->decoded_text == "adapter",
          "BACK_2 lexical and layout marker slots are not distinguished");
  require(delivery && semantic_affixes(*delivery).size() == 10 &&
              has_affix(*delivery, 541, 92, ".", 0x34ad1, 0x34ad2,
                        geist::detail::CommentAffixAttachment::
                            suffix_owning_field,
                        geist::detail::CommentAffixSpacing::none) &&
              has_affix(*delivery, 541, 153, "the", 0x34b26, 0x34b27,
                        geist::detail::CommentAffixAttachment::
                            prefix_current_field,
                        geist::detail::CommentAffixSpacing::space_after) &&
              has_affix(*delivery, 542, 149, ":", 0x34cbc, 0x34cbd,
                        geist::detail::CommentAffixAttachment::
                            suffix_owning_field,
                        geist::detail::CommentAffixSpacing::none),
          "BACK_2 audited punctuation/prefix affixes lost exact provenance");
  require(delivery && delivery->blocks[1].lines.back().fields.size() == 4 &&
              delivery->blocks[1].lines.back().fields[0].text ==
                  "USIB2HPD@VNET.IBM.COM" &&
              delivery->blocks[1].lines.back().fields[1].text ==
                  "Make sure to include the following in your note:" &&
              delivery->blocks[1].lines.back().fields[2].text ==
                  "Title and publication number of this book" &&
              delivery->blocks[1].lines.back().fields[3].text ==
                  "Page number or topic to which your comment applies.",
          "BACK_2 combined delivery/checklist row was not source-subdivided");
  require(delivery &&
              geist::detail::format_comment_delivery_ir(*delivery).find(
                  "source=1:0 record=541 segment=8") != std::string::npos,
          "BACK_2 trace omitted physical source provenance");
  if (delivery) {
    auto mutated = *delivery;
    mutated.blocks[1].lines.front().token_end++;
    require(!geist::detail::verify_comment_delivery_ir(
                delivery_sources, delivery_layout, delivery_ownership,
                mutated),
            "comment verifier admitted mutated delivery provenance");
    mutated = *delivery;
    mutated.blocks[0].lines[4].fields.front().affixes.front().text = "!";
    require(!geist::detail::verify_comment_delivery_ir(
                delivery_sources, delivery_layout, delivery_ownership,
                mutated),
            "comment verifier admitted mutated semantic affix content");
  }

  const auto* comments = topic(*sc31, "COMMENTS");
  require(comments != nullptr, "SC31 fixture has no COMMENTS topic");
  std::vector<geist::detail::DecodedLogicalRecordSource> form_sources;
  geist::detail::LayoutIR form_layout;
  geist::detail::OwnershipIR form_ownership;
  const auto form = extract_topic(*sc31, *comments, &error, &form_sources,
                                  &form_layout, &form_ownership);
  require(form.has_value(),
          error.empty() ? "COMMENTS did not enter comment IR" : error.c_str());
  require(form &&
              form->kind == geist::detail::CommentDeliveryKind::questionnaire &&
              form->blocks.size() == 4 &&
              form->blocks[0].lines.size() == 8 &&
              form->blocks[1].kind ==
                  geist::detail::CommentDeliveryBlockKind::
                      questionnaire_table &&
              form->blocks[1].lines.size() == 7 &&
              form->blocks[2].kind ==
                  geist::detail::CommentDeliveryBlockKind::
                      questionnaire_table &&
              form->blocks[2].lines.size() == 23 &&
              form->blocks[3].kind ==
                  geist::detail::CommentDeliveryBlockKind::response_area &&
              form->blocks[3].lines.size() == 26 &&
              form->blocks[3].lines.front().logical_record == 544 &&
              form->blocks[3].lines.front().segment_index == 2 &&
              form->blocks[3].lines.back().logical_record == 546,
          "COMMENTS semantic objects or structural tail are incomplete");
  require(form && geist::detail::verify_comment_delivery_ir(
                      form_sources, form_layout, form_ownership, *form,
                      &error),
          error.empty() ? "COMMENTS semantic verifier failed" : error.c_str());
  require(form && semantic_affixes(*form).size() == 8 &&
              has_affix(*form, 543, 172, "with", 0x34df0, 0x34df1,
                        geist::detail::CommentAffixAttachment::
                            suffix_owning_field,
                        geist::detail::CommentAffixSpacing::space_before) &&
              has_affix(*form, 543, 208, "?", 0x34e17, 0x34e19,
                        geist::detail::CommentAffixAttachment::
                            suffix_owning_field,
                        geist::detail::CommentAffixSpacing::none) &&
              has_affix(*form, 544, 26, ":", 0x34ec8, 0x34ec9,
                        geist::detail::CommentAffixAttachment::
                            suffix_owning_field,
                        geist::detail::CommentAffixSpacing::none) &&
              has_affix(*form, 546, 16, "information", 0x350ac, 0x350ad,
                        geist::detail::CommentAffixAttachment::
                            prefix_current_field,
                        geist::detail::CommentAffixSpacing::space_after),
          "COMMENTS semantic-cell inventory lost exact token/byte evidence");
  if (form) {
    auto mutated = *form;
    mutated.blocks[1].object_id += "changed";
    require(!geist::detail::verify_comment_delivery_ir(
                form_sources, form_layout, form_ownership, mutated),
            "comment verifier admitted a mutated form object");
    mutated = *form;
    mutated.blocks.pop_back();
    require(!geist::detail::verify_comment_delivery_ir(
                form_sources, form_layout, form_ownership, mutated),
            "comment verifier admitted incomplete form ownership");
    mutated = *form;
    mutated.blocks[3].lines[17].marker_disposition =
        geist::detail::CommentMarkerDisposition::layout_artifact;
    require(!geist::detail::verify_comment_delivery_ir(
                form_sources, form_layout, form_ownership, mutated),
            "comment verifier admitted a changed lexical marker disposition");
    mutated = *form;
    mutated.blocks[3].lines[17].fields[1].token_begin++;
    require(!geist::detail::verify_comment_delivery_ir(
                form_sources, form_layout, form_ownership, mutated),
            "comment verifier admitted a changed semantic field boundary");
    mutated = *form;
    mutated.suppressed_fragments.pop_back();
    require(!geist::detail::verify_comment_delivery_ir(
                form_sources, form_layout, form_ownership, mutated),
            "comment verifier admitted an escaped structural fragment");
  }
  require(form &&
              form->blocks[3].lines[17].marker_disposition ==
                  geist::detail::CommentMarkerDisposition::lexical_content &&
              form->blocks[3].lines[17].marker->decoded_text ==
                  "information" &&
              form->blocks[3].lines[17].fields.size() == 3 &&
              form->blocks[3].lines[17].fields[0].text ==
                  "in any way you choose." &&
              form->blocks[3].lines[17].fields[1].text ==
                  "Please complete this form and mail it to:" &&
              form->blocks[3].lines[17].fields[2].text ==
                  "International Business Machines Corporation" &&
              form->blocks[3].lines[19].fields.size() == 6 &&
              form->blocks[1].lines.front().fields.front().disposition ==
                  geist::detail::CommentSourceFieldIR::Disposition::
                      layout_decoration &&
              form->blocks[2].lines.front().fields.front().disposition ==
                  geist::detail::CommentSourceFieldIR::Disposition::
                      layout_decoration,
          "COMMENTS lexical continuation and mailing fields are incomplete");

  std::size_t candidates = 0;
  std::size_t admitted = 0;
  std::vector<std::string> candidate_names;
  std::vector<std::string> admitted_names;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    auto extension = geist::detail::ascii_lower(entry.path().extension().string());
    if (extension != ".boo") continue;
    auto book = load_book(entry.path());
    for (const auto& candidate : book->document.topics()) {
      if (candidate.id != "COMMENTS" && candidate.id != "BACK_2") continue;
      ++candidates;
      candidate_names.push_back(entry.path().filename().string() + ":" +
                                candidate.id);
      const auto candidate_ir = extract_topic(*book, candidate, &error);
      if (!candidate_ir) continue;
      ++admitted;
      admitted_names.push_back(entry.path().filename().string() + ":" +
                               candidate.id);
    }
  }
  if (candidates != 17) {
    std::cerr << "comment candidates=" << candidates << '\n';
    for (const auto& name : candidate_names) std::cerr << name << '\n';
  }
  require(candidates == 17,
          "comment-envelope fixture inventory changed unexpectedly");
  require(admitted == 2 && admitted_names.size() == 2 &&
              std::find(admitted_names.begin(), admitted_names.end(),
                        "SC31-711.boo:BACK_2") != admitted_names.end() &&
              std::find(admitted_names.begin(), admitted_names.end(),
                        "SC31-711.boo:COMMENTS") != admitted_names.end(),
          "cross-book comment forms entered the bounded SC31 semantic IR");
  return 0;
}
