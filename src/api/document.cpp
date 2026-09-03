// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/ir/fixed_table_block_ir.hpp"
#include "geist/detail/ir/comment_delivery_ir.hpp"
#include "geist/detail/ir/selector_display_ir.hpp"
#include "geist/detail/container/source_rows.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/container/toc_entry_framing.hpp"
#include "geist/detail/lowering/topic_identity.hpp"
#include "geist/detail/lowering/topic_lowering_outcome.hpp"
#include "geist/detail/ir/trap_catalog_ir.hpp"
#include "geist/detail/lowering/verbatim_cross_references.hpp"

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

namespace geist {

using namespace detail;

namespace {

std::shared_ptr<const std::set<std::string>> lowercase_resource_ids(
    const std::vector<ResourceEntry>& resources) {
  auto ids = std::make_shared<std::set<std::string>>();
  for (const auto& resource : resources) ids->insert(ascii_lower(resource.id));
  return ids;
}

struct TopicLoaderBundle {
  std::function<std::shared_ptr<const TopicLoweringOutcomeIR>()> document;
  std::function<TopicBestEffortIR()> best_effort;
};

struct LazyTopicState {
  std::shared_ptr<LogicalDecodeContext> context;
  std::shared_ptr<const BookTopicCatalogIR> topic_catalog;
  std::shared_ptr<const std::set<std::string>> resource_ids;
  std::shared_ptr<const std::map<std::string, std::string>> topic_titles;
  TopicData topic;
  TopicIdentityIR identity;
  std::string id;
  std::string title;
  std::uint32_t level = 0;
  std::uint32_t style = 0;

  // The topic's records and their positioned source decode, loaded together
  // so there is one slot rather than two flags to publish.
  struct LoadedTopic {
    std::vector<std::string> raw_records;
    std::vector<DecodedLogicalRecordSource> fixed_layout_sources;
  };
  CacheSlot<LoadedTopic> loaded;

  // Every topic is offered the same positioned source decode once; typed
  // lowering and the compatibility renderer share this single cache. It is
  // filled once and published atomically (geist/detail/atomic_cache.hpp), so
  // two threads rendering the same topic at the same time each see a complete
  // decode and one of the two is discarded.
  const LoadedTopic& load_sources() {
    return *publish_once(loaded, [this]() -> std::shared_ptr<const LoadedTopic> {
      auto value = std::make_shared<LoadedTopic>();
      value->raw_records.assign(
          context->decoded_records.begin() + topic.start_logical_record - 1,
          context->decoded_records.begin() + topic.end_logical_record - 1);
      value->fixed_layout_sources = decode_logical_record_sources(
          *context, topic.start_logical_record, topic.end_logical_record);
      return value;
    });
  }
};

TopicLoaderBundle make_topic_loaders(
    const std::shared_ptr<LogicalDecodeContext>& context, TopicData topic,
    TopicIdentityIR identity, std::string id, std::string title,
    std::uint32_t level, std::uint32_t style,
    const std::shared_ptr<const BookTopicCatalogIR>& topic_catalog,
    const std::shared_ptr<const std::map<std::string, std::string>>&
        topic_titles,
    const std::shared_ptr<const std::set<std::string>>& resource_ids) {
  auto state = std::make_shared<LazyTopicState>();
  state->context = context;
  state->topic_catalog = topic_catalog;
  state->resource_ids = resource_ids;
  state->topic_titles = topic_titles;
  state->topic = std::move(topic);
  state->identity = std::move(identity);
  state->id = std::move(id);
  state->title = std::move(title);
  state->level = level;
  state->style = style;

  TopicLoaderBundle loaders;
  // The rejection string and the decline trace are kept, not discarded: they
  // are what a declined topic tells the consumer about itself, and they
  // are the same strings `bootrace --coverage` reports.
  loaders.document = [state]() -> std::shared_ptr<const TopicLoweringOutcomeIR> {
    const auto& loaded = state->load_sources();
    auto outcome = std::make_shared<TopicLoweringOutcomeIR>();
    outcome->document = try_lower_topic_to_document_ir(
        state->identity, loaded.fixed_layout_sources,
        state->topic_catalog.get(), &outcome->rejection, &outcome->trace,
        state->resource_ids.get());
    return outcome;
  };
  // Last resort, loaded only when no route produced content.
  loaders.best_effort = [state]() -> TopicBestEffortIR {
    const auto& loaded = state->load_sources();
    TopicBestEffortIR result;
    result.source_decoded = !loaded.fixed_layout_sources.empty();
    // The rows first, then the cross references the source proves on them:
    // a verbatim topic renders faithfully *and* resolves what it names.
    const auto& sources = loaded.fixed_layout_sources;
    result.anchors = best_effort_anchors(sources);
    auto linked = link_verbatim_cross_references(
        sources, best_effort_display_lines(sources, state->identity.title),
        best_effort_footnote_anchors(sources));
    result.rows = std::move(linked.rows);
    result.footnote_anchors = std::move(linked.footnote_anchors);
    return result;
  };
  return loaders;
}

// `:H2` is one step in from `:H1`; anything else -- front matter, an
// unlevelled topic -- sits at the top.
std::uint32_t heading_level_depth(const std::string& heading_level) {
  if (heading_level.size() < 3 || heading_level[0] != ':' ||
      (heading_level[1] != 'H' && heading_level[1] != 'h')) {
    return 0;
  }
  const char digit = heading_level[2];
  if (digit < '1' || digit > '9') {
    return 0;
  }
  return static_cast<std::uint32_t>(digit - '1');
}

} // namespace

BooDocument BooDocument::open(const std::filesystem::path& path) {
  BooDocument document;
  auto context = std::make_shared<LogicalDecodeContext>();
  context->bytes = read_file(path);
  const auto& bytes = context->bytes;

  // Header, directory and the size checks are read through the one shared
  // prologue `probe_book` also enters by, so a listing and an opened document
  // can never disagree about them.
  auto prologue = read_container_prologue(bytes, path);
  document.metadata_ = std::move(prologue.metadata);
  document.file_header_ = std::move(prologue.file_header);
  document.directory_ = std::move(prologue.directory);

  context->directory = document.directory_;
  context->content_page_record_starts =
      parse_content_page_record_starts(bytes, document.directory_);
  context->topic_record_starts = parse_topic_record_starts(
      bytes, document.directory_);
  document.decode_context_ = context;

  document.page_runs_ = build_page_runs(bytes, document.directory_);
  // The experimental decoder currently exposes compact token records rather
  // than the reader's fully assembled logical-record numbering. Decode that
  // inexpensive stream once to preserve established topic boundaries; GML
  // parsing and rendering remain deferred until a topic is requested.
  std::vector<LogicalRecordTokenBoundaries> header_token_boundaries;
  context->decoded_records =
      decode_experimental_logical_records(bytes,
                                          document.directory_,
                                          &context->record_payload_ranges,
                                          &header_token_boundaries);
  const auto topics = build_topics(*context, false);
  const auto first_topic_record = topics.empty()
                                      ? context->decoded_records.size() + 1
                                      : topics.front().start_logical_record;
  const std::vector<std::string> book_header_records(
      context->decoded_records.begin(),
      context->decoded_records.begin() +
          static_cast<std::ptrdiff_t>(first_topic_record - 1));
  header_token_boundaries.resize(book_header_records.size());
  // The raw control stream is a build input only: the book's properties are
  // what the document publishes, and nothing else reads the controls again.
  const auto logical_controls =
      extract_book_logical_controls(book_header_records, header_token_boundaries);
  document.book_properties_ = build_book_properties(logical_controls);

  for (const auto& topic : topics) {
    document.topics_.push_back({topic.id,
                                topic.title,
                                topic.heading_level,
                                topic.topic_number,
                                topic.start_logical_record,
                                topic.end_logical_record});
    document.topic_titles_.emplace(topic.id, topic.title);
  }
  std::vector<std::string> contents_records;
  // The contents records' display-line framing decides where each `CTocE`
  // title ends (toc_entry_framing.hpp), so the table of contents is read from
  // the positioned decode and not from the flattened strings alone.
  std::vector<std::vector<std::size_t>> contents_display_line_starts;
  for (const auto& topic : topics) {
    if (ascii_equals_case_insensitive(topic.id, "contents")) {
      contents_records.assign(
          context->decoded_records.begin() + topic.start_logical_record - 1,
          context->decoded_records.begin() + topic.end_logical_record - 1);
      const auto sources = decode_logical_record_sources(
          *context, topic.start_logical_record, topic.end_logical_record);
      contents_display_line_starts.reserve(sources.size());
      for (const auto& source : sources) {
        contents_display_line_starts.push_back(
            display_line_start_output_offsets(source));
      }
      break;
    }
  }
  document.toc_ = build_table_of_contents(contents_records, topics, false,
                                          &contents_display_line_starts);
  // A book with no CONTENTS topic has no contents page to read a table of
  // contents from, and short books are published that way. Its topics still
  // carry their own hierarchy -- `:H1`, `:H2` -- which is what the
  // BookManager reader draws its tree from, so the topics are the table of
  // contents when the book does not spell one out.
  //
  // Only as a fallback: where a book does have a contents page, that page is
  // the book's own statement of its structure and outranks anything derived.
  if (document.toc_.empty()) {
    for (const auto& topic : topics) {
      TocEntry entry;
      entry.id = topic.id;
      entry.title = topic.title;
      entry.heading_level = topic.heading_level;
      entry.topic_number = topic.topic_number;
      entry.start_logical_record = topic.start_logical_record;
      entry.end_logical_record = topic.end_logical_record;
      // Contents entries carry a level in steps of two, and consumers indent
      // by half of it, so `:H2` is one step in from `:H1`. Front matter has
      // no heading level and sits at the top, where the reader puts it.
      entry.level = heading_level_depth(topic.heading_level) * 2;
      // Front matter's style, which is what an entry the contents page did
      // not spell out most resembles.
      entry.style = 1;
      document.toc_.push_back(std::move(entry));
    }
  }
  std::string catalog_error;
  auto topic_catalog = build_book_topic_catalog_ir(
      document.topics_, document.toc_, &catalog_error);
  if (!topic_catalog || !verify_book_topic_catalog_ir(
                            document.topics_, document.toc_, *topic_catalog,
                            &catalog_error)) {
    throw std::runtime_error("invalid book topic catalog IR: " +
                             catalog_error);
  }
  document.topic_catalog_ir_ =
      std::make_shared<const BookTopicCatalogIR>(std::move(*topic_catalog));
  for (const auto& entry : document.toc_)
    document.topic_titles_[entry.id] = entry.title;
  const auto topic_titles =
      std::make_shared<const std::map<std::string, std::string>>(
          document.topic_titles_);
  // Figure spans prove their `PIC<n>` selectors against the resource
  // catalog, so the catalog is built before the topic loaders that need it.
  document.resources_ = build_resources(bytes, document.directory_);
  const auto resource_ids = lowercase_resource_ids(document.resources_);
  for (auto& entry : document.toc_) {
    const auto* topic = find_topic_data(topics, entry.id);
    if (topic == nullptr) {
      continue;
    }
    auto topic_data = *topic;
    const auto entry_id = entry.id;
    const auto entry_title = entry.title;
    const auto entry_level = entry.level;
    const auto entry_style = entry.style;
    auto loaders = make_topic_loaders(
        context, std::move(topic_data), make_topic_identity(entry), entry_id,
        entry_title, entry_level, entry_style, document.topic_catalog_ir_,
        topic_titles, resource_ids);
    entry.attach_loaders(std::move(loaders.document),
                         std::move(loaders.best_effort));
  }
  return document;
}

const BooMetadata& BooDocument::metadata() const noexcept {
  return metadata_;
}

const BooPage0Header& BooDocument::file_header() const noexcept {
  return file_header_;
}

const BooDirectory& BooDocument::directory() const noexcept {
  return directory_;
}

const BooBookProperties& BooDocument::book_properties() const noexcept {
  return book_properties_;
}

const std::vector<BooPageRun>& BooDocument::page_runs() const noexcept {
  return page_runs_;
}

const std::vector<std::string>& BooDocument::decoded_logical_records()
    const {
  return decode_context_->decoded_records;
}

const std::map<std::string, std::string>& BooDocument::font_definitions()
    const {
  // Fill-once and published atomically (geist/detail/atomic_cache.hpp): the
  // definitions are a pure function of the decoded records, which are built
  // by `open()` and never written again.
  return *publish_once(
      font_definitions_,
      [this]() -> std::shared_ptr<const std::map<std::string, std::string>> {
        return std::make_shared<const std::map<std::string, std::string>>(
            extract_font_definitions(decoded_logical_records()));
      });
}

const std::vector<TocEntry>& BooDocument::table_of_contents() const noexcept {
  return toc_;
}

const std::vector<TopicInfo>& BooDocument::topics() const noexcept {
  return topics_;
}

const std::vector<ResourceEntry>& BooDocument::resources() const noexcept {
  return resources_;
}

const TocEntry* BooDocument::find_toc_entry(const std::string& topic_id)
    const noexcept {
  const auto normalized_id = normalize_toc_id(topic_id);
  const auto found = std::find_if(toc_.begin(), toc_.end(),
                                  [&](const TocEntry& entry) {
                                    return entry.id == normalized_id ||
                                           ascii_equals_case_insensitive(
                                               entry.id, topic_id);
                                  });
  if (found == toc_.end()) {
    return nullptr;
  }
  return &*found;
}

std::vector<RenderDiagnostic> BooDocument::render_diagnostics() const {
  std::vector<RenderDiagnostic> diagnostics;
  diagnostics.reserve(toc_.size());
  for (const auto& entry : toc_) diagnostics.push_back(entry.render_diagnostic());
  return diagnostics;
}

std::string BooDocument::topic_markdown(const std::string& topic_id) const {
  if (const auto* entry = find_toc_entry(topic_id)) {
    return entry->markdown();
  }
  return synthesize_topic_entry(topic_id).markdown();
}

// A topic the TOC does not list, built as a standalone entry with the same
// loaders a listed one gets. Shared by every per-topic render entry point so
// that a format cannot answer for a topic the others cannot reach.
TocEntry BooDocument::synthesize_topic_entry(
    const std::string& topic_id) const {
  const auto found = std::find_if(
      topics_.begin(), topics_.end(), [&](const TopicInfo& topic) {
        return topic.id == normalize_toc_id(topic_id) ||
               ascii_equals_case_insensitive(topic.id, topic_id);
      });
  if (found == topics_.end()) {
    throw std::out_of_range("BOO topic id was not found: " + topic_id);
  }

  TopicData topic;
  topic.id = found->id;
  topic.title = found->title;
  topic.heading_level = found->heading_level;
  topic.topic_number = found->topic_number;
  topic.start_logical_record = found->start_logical_record;
  topic.end_logical_record = found->end_logical_record;
  TocEntry entry;
  entry.id = topic.id;
  entry.title = topic.title;
  entry.heading_level = topic.heading_level;
  entry.topic_number = topic.topic_number;
  entry.start_logical_record = topic.start_logical_record;
  entry.end_logical_record = topic.end_logical_record;
  const auto topic_titles =
      std::make_shared<const std::map<std::string, std::string>>(topic_titles_);
  auto loaders = make_topic_loaders(
      decode_context_, topic, make_topic_identity(entry), topic.id,
      topic.title, 0, 0, topic_catalog_ir_, topic_titles,
      lowercase_resource_ids(resources_));
  entry.attach_loaders(std::move(loaders.document),
                       std::move(loaders.best_effort));
  return entry;
}

std::vector<BooLogicalRecordTrace> BooDocument::trace_logical_records(
    const std::string& topic_id) const {
  const auto topic = std::find_if(
      topics_.begin(), topics_.end(), [&](const TopicInfo& candidate) {
        return candidate.id == normalize_toc_id(topic_id) ||
               ascii_equals_case_insensitive(candidate.id, topic_id);
      });
  if (topic == topics_.end()) {
    throw std::out_of_range("BOO topic id was not found: " + topic_id);
  }
  if (topic->start_logical_record == 0 || topic->end_logical_record == 0 ||
      topic->end_logical_record <= topic->start_logical_record) {
    return {};
  }
  const auto& all_records = decoded_logical_records();
  const auto begin = static_cast<std::size_t>(topic->start_logical_record - 1);
  const auto end = std::min<std::size_t>(topic->end_logical_record - 1,
                                        all_records.size());
  std::vector<std::string> records(all_records.begin() + begin,
                                   all_records.begin() + end);
  const auto sources = detail::decode_logical_record_sources(
      *decode_context_, topic->start_logical_record,
      topic->end_logical_record);
  auto traced = detail::trace_decoded_records(
      records, sources, topic->start_logical_record, font_definitions());
  const auto layout = detail::extract_layout_ir(sources);
  std::string ir_error;
  // One build, one verification for the whole trace: every consumer below
  // receives the verified handle instead of re-deriving the ledger.
  if (!detail::verify_layout_ir(sources, layout, &ir_error)) {
    throw std::runtime_error("invalid source IR trace: " + ir_error);
  }
  const auto verified =
      detail::build_verified_ownership_ir(sources, layout, &ir_error);
  if (!verified) {
    throw std::runtime_error("invalid source IR trace: " + ir_error);
  }
  const detail::OwnershipIR& ownership = *verified;
  const auto trace_for = [&](const std::uint32_t logical_record)
      -> BooLogicalRecordTrace* {
    if (logical_record < topic->start_logical_record) return nullptr;
    const auto index = static_cast<std::size_t>(
        logical_record - topic->start_logical_record);
    return index < traced.size() ? &traced[index] : nullptr;
  };
  for (const auto& source : sources) {
    auto* destination = trace_for(source.logical_record);
    if (destination == nullptr) continue;
    for (const auto& segment : source.control_segments)
      destination->ir_control_segments.push_back(
          detail::format_control_segment_ir(segment));
    for (const auto& token : source.ir.tokens)
      destination->ir_tokens.push_back(detail::format_logical_token_ir(token));
    if (const auto lines = detail::record_display_lines(source)) {
      for (std::size_t index = 0; index < lines->size(); ++index)
        destination->ir_display_lines.push_back(
            detail::format_display_line_ir(source, (*lines)[index], index));
    } else {
      destination->ir_display_lines.push_back("display lines do not parse");
    }
  }
  for (const auto& run : layout.runs) {
    for (const auto& row : run.rows) {
      if (auto* destination = trace_for(row.logical_record))
        destination->ir_physical_rows.push_back(
            detail::format_physical_row_ir(row));
    }
  }
  for (const auto& cell : ownership.cells) {
    if (cell.run == 0 &&
        cell.disposition == detail::SourceDisposition::opaque)
      continue;
    if (auto* destination = trace_for(cell.logical_record))
      destination->ir_ownership_cells.push_back(
          detail::format_owned_source_cell_ir(cell));
  }
  std::string fixed_prose_extraction_error;
  const auto fixed_prose = detail::extract_fixed_prose_ir(
      sources, layout, *verified, &fixed_prose_extraction_error);
  if (fixed_prose) {
    std::string fixed_prose_error;
    if (!detail::verify_fixed_prose_ir(
            sources, layout, *verified, *fixed_prose, &fixed_prose_error))
      throw std::runtime_error("invalid fixed prose IR trace: " +
                               fixed_prose_error);
    if (auto* destination = trace_for(fixed_prose->logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_fixed_prose_ir(*fixed_prose));
  } else if (!fixed_prose_extraction_error.empty() && !sources.empty()) {
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          "fixed_prose_ir_rejected=" + fixed_prose_extraction_error);
  }
  {
    const auto tables = detail::extract_fixed_table_blocks_ir(
        sources, layout, ownership, {0, detail::count_layout_rows(layout)});
    if ((!tables.blocks.empty() || !tables.declined.empty()) &&
        !sources.empty())
      if (auto* destination = trace_for(sources.front().logical_record))
        destination->ir_semantic_blocks.push_back(
            detail::format_fixed_table_blocks_ir(tables));
  }
  std::string comment_extraction_error;
  const auto comment_delivery = detail::extract_comment_delivery_ir(
      sources, layout, *verified, &comment_extraction_error);
  if (comment_delivery && !sources.empty()) {
    std::string comment_error;
    if (!detail::verify_comment_delivery_ir(
            sources, layout, *verified, *comment_delivery, &comment_error))
      throw std::runtime_error("invalid comment/delivery IR trace: " +
                               comment_error);
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_comment_delivery_ir(*comment_delivery));
  } else if (!comment_extraction_error.empty() && !sources.empty()) {
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          "comment_delivery_ir_rejected=" + comment_extraction_error);
  }
  const auto publication =
      detail::extract_publication_catalog_ir(sources, layout, *verified);
  if (publication && !sources.empty()) {
    std::string publication_error;
    if (!detail::verify_publication_catalog_ir(
            sources, layout, *verified, *publication, &publication_error))
      throw std::runtime_error("invalid publication IR trace: " +
                               publication_error);
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_publication_catalog_ir(*publication));
  }
  const auto glossary =
      detail::extract_glossary_introduction_ir(sources, layout, ownership);
  if (glossary && !sources.empty()) {
    std::string glossary_error;
    if (!detail::verify_glossary_introduction_ir(
            sources, layout, ownership, *glossary, &glossary_error))
      throw std::runtime_error("invalid glossary IR trace: " +
                               glossary_error);
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_glossary_introduction_ir(*glossary));
  }
  std::string message_extraction_error;
  const auto message_catalog = detail::extract_message_catalog_ir(
      sources, layout, ownership, &message_extraction_error);
  if (message_catalog && !sources.empty()) {
    std::string message_error;
    if (!detail::verify_message_catalog_ir(
            sources, layout, ownership, *message_catalog, &message_error))
      throw std::runtime_error("invalid message catalog IR trace: " +
                               message_error);
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_message_catalog_ir(*message_catalog));
  } else if (!message_extraction_error.empty() && !sources.empty() &&
             std::any_of(sources.begin(), sources.end(),
                         [](const auto& source) {
                           return std::any_of(
                               source.control_segments.begin(),
                               source.control_segments.end(),
                               [](const auto& segment) {
                                 return segment.kind ==
                                        detail::BookControlKind::message_start;
                               });
                         })) {
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          "message_catalog_ir_rejected=" + message_extraction_error);
    std::string trap_extraction_error;
    const auto toc_entry = std::find_if(
        toc_.begin(), toc_.end(), [&](const TocEntry& candidate) {
          return candidate.id == topic->id;
        });
    const auto trap_catalog = detail::extract_trap_catalog_ir(
        sources, layout, ownership,
        toc_entry == toc_.end() ? std::string{} : toc_entry->title,
        &trap_extraction_error);
    if (trap_catalog) {
      std::string trap_error;
      if (!detail::verify_trap_catalog_ir(sources, layout, ownership,
                                          *trap_catalog, &trap_error))
        throw std::runtime_error("invalid trap catalog IR trace: " +
                                 trap_error);
      if (auto* destination = trace_for(sources.front().logical_record))
        destination->ir_semantic_blocks.push_back(
            detail::format_trap_catalog_ir(*trap_catalog));
    } else if (auto* destination =
                   trace_for(sources.front().logical_record)) {
      destination->ir_semantic_blocks.push_back(
          "trap_catalog_ir_rejected=" + trap_extraction_error);
    }
  }
  std::string selector_extraction_error;
  const auto selectors = detail::extract_selector_catalog_ir(
      sources, &selector_extraction_error);
  if (selectors && !sources.empty()) {
    std::string selector_error;
    if (!detail::verify_selector_catalog_ir(sources, *selectors,
                                            &selector_error))
      throw std::runtime_error("invalid selector IR trace: " +
                               selector_error);
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_selector_catalog_ir(*selectors));
    std::string display_extraction_error;
    const auto display = detail::extract_selector_display_ir(
        sources, *selectors, layout, *verified, &display_extraction_error);
    if (display) {
      std::string display_error;
      if (!detail::verify_selector_display_ir(
              sources, *selectors, layout, *verified, *display,
              &display_error))
        throw std::runtime_error("invalid selector display IR trace: " +
                                 display_error);
      if (auto* destination = trace_for(sources.front().logical_record))
        destination->ir_semantic_blocks.push_back(
            detail::format_selector_display_ir(*display));
    } else if (!display_extraction_error.empty()) {
      if (auto* destination = trace_for(sources.front().logical_record))
        destination->ir_semantic_blocks.push_back(
            "selector_display_ir_rejected=" + display_extraction_error);
    }
  } else if (!selector_extraction_error.empty() && !sources.empty()) {
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          "selector_catalog_ir_rejected=" + selector_extraction_error);
  }
  std::string menu_extraction_error;
  const auto menu = detail::extract_menu_ir(sources, topic_titles_,
                                             &menu_extraction_error);
  if (menu && !sources.empty()) {
    std::string menu_error;
    if (!detail::verify_menu_ir(sources, topic_titles_, *menu, &menu_error))
      throw std::runtime_error("invalid menu IR trace: " + menu_error);
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          detail::format_menu_ir(*menu));
  } else if (!menu_extraction_error.empty() && !sources.empty()) {
    if (auto* destination = trace_for(sources.front().logical_record))
      destination->ir_semantic_blocks.push_back(
          "menu_ir_rejected=" + menu_extraction_error);
  }
  return traced;
}

std::vector<std::uint8_t> BooDocument::read_page(
    std::uint32_t page_number) const {
  if (page_number >= metadata_.page_count) {
    throw std::out_of_range("BOO page number is outside the file");
  }

  const auto begin = decode_context_->bytes.begin() +
                     static_cast<std::ptrdiff_t>(page_number * boo_page_size);
  return {begin, begin + boo_page_size};
}

std::vector<std::uint8_t> BooDocument::read_resource_data(
    const std::string& resource_id) const {
  auto found = std::find_if(resources_.begin(), resources_.end(),
                            [&](const ResourceEntry& resource) {
                              return resource.id == resource_id ||
                                     ascii_equals_case_insensitive(
                                         resource.id, resource_id);
                            });
  if (found == resources_.end()) {
    throw std::out_of_range("BOO resource id was not found: " + resource_id);
  }
  if (!byte_range_is_valid(decode_context_->bytes, found->offset, found->size)) {
    throw std::runtime_error("BOO resource byte range is outside the file");
  }

  const auto begin = decode_context_->bytes.begin() +
                     static_cast<std::ptrdiff_t>(found->offset);
  return {begin, begin + static_cast<std::ptrdiff_t>(found->size)};
}

std::vector<std::uint8_t> BooDocument::read_resource_png(
    const std::string& resource_id) const {
  auto found = std::find_if(resources_.begin(), resources_.end(),
                            [&](const ResourceEntry& resource) {
                              return resource.id == resource_id ||
                                     ascii_equals_case_insensitive(
                                         resource.id, resource_id);
                            });
  if (found == resources_.end()) {
    throw std::out_of_range("BOO resource id was not found: " + resource_id);
  }

  return render_resource_png(*found, read_resource_data(found->id));
}

} // namespace geist
