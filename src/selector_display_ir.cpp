#include "geist/detail/selector_display_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {

using SegmentKey = std::pair<std::uint32_t, std::size_t>;
using SelectorKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using OwnershipKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

struct IndexedRow {
  const PhysicalRowIR *row = nullptr;
  std::size_t physical_row_index = 0;
};

SelectorRefIR selector_ref(const SelectorIR &selector) {
  return {selector.logical_record, selector.segment_index,
          selector.selector_ordinal};
}

SelectorKey selector_key(const SelectorRefIR &selector) {
  return {selector.logical_record, selector.segment_index, selector.ordinal};
}

bool ref_equal(const SelectorRefIR &left, const SelectorRefIR &right) {
  return selector_key(left) == selector_key(right);
}

bool exact_picture_target(const std::string &target) {
  if (target.size() <= 3 || !ascii_starts_with_case_insensitive(target, "pic"))
    return false;
  return std::all_of(target.begin() + 3, target.end(), [](const auto ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

SelectorTargetIR target_ir(const SelectorIR &selector) {
  SelectorTargetIR target;
  target.raw_target = selector.target;
  target.resolved_target = selector.target;
  if (exact_picture_target(selector.target)) {
    target.kind = SelectorTargetKind::picture_resource;
    target.resolved_target = selector.target.substr(3);
  } else if (ascii_equals_case_insensitive(selector.target, "lnk")) {
    // LNK alternatives require their own typed metadata grammar. Until that
    // exists, conserve the selector as an explicit non-row object rather than
    // guessing which payload bytes are display cells.
    target.kind = SelectorTargetKind::external_deferred;
  }
  return target;
}

bool presentable(std::uint16_t word) {
  return word >= 0x21 && word != '?' && word != 0x2666;
}

bool payload_has_visible_text(const std::string &payload) {
  return std::any_of(payload.begin(), payload.end(), [](const auto ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return byte >= 0x21 && ch != '?';
  });
}

enum class GeneratedListKind {
  none,
  figures,
  tables,
};

std::string range_text(const DecodedLogicalRecordSource &record,
                       const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size())
    return {};
  return text.substr(range.begin, range.end - range.begin);
}

GeneratedListKind exact_generated_list_kind(
    const std::vector<DecodedLogicalRecordSource> &records,
    const SelectorCatalogIR &selectors) {
  GeneratedListKind kind = GeneratedListKind::none;
  bool saw_heading = false;
  bool saw_title = false;
  auto title_record = std::uint32_t{0};
  auto title_segment = std::size_t{0};
  for (const auto &record : records) {
    for (const auto &segment : record.control_segments) {
      if (ascii_equals_case_insensitive(segment.opcode, "chdlevel")) {
        if (saw_heading || saw_title)
          return GeneratedListKind::none;
        const auto operand =
            ascii_lower(trim_ascii(range_text(record, segment.operand_range)));
        if (operand == ":figlist")
          kind = GeneratedListKind::figures;
        else if (operand == ":tlist")
          kind = GeneratedListKind::tables;
        else
          return GeneratedListKind::none;
        saw_heading = true;
      } else if (segment.kind == BookControlKind::title) {
        if (!saw_heading || saw_title)
          return GeneratedListKind::none;
        const auto expected =
            kind == GeneratedListKind::figures ? "figures" : "tables";
        if (!ascii_equals_case_insensitive(
                trim_ascii(range_text(record, segment.payload_range)),
                expected))
          return GeneratedListKind::none;
        saw_title = true;
        title_record = record.logical_record;
        title_segment = segment.segment_index;
      }
    }
  }
  if (!saw_heading || !saw_title || selectors.selectors.empty())
    return GeneratedListKind::none;
  return std::all_of(selectors.selectors.begin(), selectors.selectors.end(),
                     [&](const auto &selector) {
                       return selector.logical_record > title_record ||
                              (selector.logical_record == title_record &&
                               selector.segment_index > title_segment);
                     })
             ? kind
             : GeneratedListKind::none;
}

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record) {
  const auto found =
      std::find_if(records.begin(), records.end(), [&](const auto &record) {
        return record.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

std::optional<std::vector<SelectorDisplayCellIR>>
row_cells(const std::vector<DecodedLogicalRecordSource> &records,
          const std::map<OwnershipKey, const OwnedSourceCellIR *> &owned_cells,
          const PhysicalRowIR &row, std::size_t physical_row_index,
          std::string *error, bool restored_native_fragment = false,
          bool allow_unlaid_generated_content = false) {
  const auto *record = find_record(records, row.logical_record);
  if (record == nullptr) {
    if (error != nullptr)
      *error = "selector row references a missing record";
    return std::nullopt;
  }
  std::vector<SelectorDisplayCellIR> cells;
  for (std::size_t output = 0; output < record->assembled.words.size();
       ++output) {
    if (output >= record->assembled.sources.size()) {
      if (error != nullptr)
        *error = "assembled selector row has no source-word provenance";
      return std::nullopt;
    }
    const auto &source = record->assembled.sources[output];
    if (source.token_index < row.token_begin ||
        source.token_index >= row.token_end)
      continue;
    if (row.marker && source.token_index == row.marker->token_index)
      continue;
    if (source.token_index >= record->ir.tokens.size()) {
      if (error != nullptr)
        *error = "selector row source token is outside token IR";
      return std::nullopt;
    }
    SelectorSourceCellRefIR provenance;
    provenance.logical_record = row.logical_record;
    provenance.token_index = source.token_index;
    provenance.word_index = source.word_index;
    provenance.kind = source.kind == LogicalWordSourceKind::token_word
                          ? SelectorSourceCellKind::token_word
                          : SelectorSourceCellKind::inserted_space;
    provenance.token_bytes = record->ir.tokens[source.token_index].byte_range;
    auto origin = SelectorDisplayCellOrigin::source;
    if (provenance.kind == SelectorSourceCellKind::token_word) {
      const auto owned = owned_cells.find(
          {row.logical_record, source.token_index, source.word_index});
      const auto laid_cell =
          owned != owned_cells.end() && owned->second->run == row.run &&
          owned->second->row_index == physical_row_index &&
          (owned->second->disposition == SourceDisposition::layout_origin ||
           owned->second->disposition == SourceDisposition::layout_padding ||
           owned->second->disposition == SourceDisposition::visible_content);
      const auto conserved_unlaid_cell =
          allow_unlaid_generated_content && owned != owned_cells.end() &&
          owned->second->run == 0 &&
          owned->second->disposition == SourceDisposition::opaque;
      if (!laid_cell && !conserved_unlaid_cell) {
        if (error != nullptr)
          *error = "selector display cell lacks matching row ownership";
        return std::nullopt;
      }
      if (restored_native_fragment &&
          owned->second->disposition == SourceDisposition::layout_origin)
        origin = SelectorDisplayCellOrigin::restored_native_margin;
      else if (restored_native_fragment &&
               owned->second->disposition == SourceDisposition::layout_padding)
        origin = SelectorDisplayCellOrigin::restored_box_padding;
    }
    cells.push_back(
        {record->assembled.words[output], origin, std::move(provenance)});
  }
  if (cells.empty()) {
    if (error != nullptr)
      *error = "selector row has no source-backed cells";
    return std::nullopt;
  }
  return cells;
}

std::optional<std::vector<SelectorDisplayCellIR>> restored_marker_cells(
    const std::vector<DecodedLogicalRecordSource> &records,
    const std::map<OwnershipKey, const OwnedSourceCellIR *> &owned_cells,
    const PhysicalRowIR &row, std::size_t physical_row_index,
    bool include_inserted_spaces, std::string *error) {
  if (!row.marker) {
    if (error != nullptr)
      *error = "selector continuation has no source marker";
    return std::nullopt;
  }
  const auto *record = find_record(records, row.logical_record);
  const auto token = row.marker->token_index;
  if (record == nullptr || token >= record->tokens.size() ||
      token >= record->ir.tokens.size() || row.marker->encoded_width != 1) {
    if (error != nullptr)
      *error =
          "selector continuation marker is not one presentable source cell";
    return std::nullopt;
  }
  std::vector<SelectorDisplayCellIR> cells;
  for (std::size_t output = 0; output < record->assembled.sources.size();
       ++output) {
    const auto &source = record->assembled.sources[output];
    if (source.token_index != token)
      continue;
    if (source.kind == LogicalWordSourceKind::token_word) {
      const auto owned =
          owned_cells.find({row.logical_record, token, source.word_index});
      if (owned == owned_cells.end() || owned->second->run != row.run ||
          owned->second->row_index != physical_row_index ||
          owned->second->disposition != SourceDisposition::marker_slot ||
          !presentable(record->assembled.words[output]))
        continue;
    } else {
      if (!include_inserted_spaces || record->assembled.words[output] != ' ')
        continue;
    }
    SelectorSourceCellRefIR provenance;
    provenance.logical_record = row.logical_record;
    provenance.token_index = token;
    provenance.word_index = source.word_index;
    provenance.kind = source.kind == LogicalWordSourceKind::token_word
                          ? SelectorSourceCellKind::token_word
                          : SelectorSourceCellKind::inserted_space;
    provenance.token_bytes = record->ir.tokens[token].byte_range;
    cells.push_back(
        SelectorDisplayCellIR{record->assembled.words[output],
                              SelectorDisplayCellOrigin::restored_native_marker,
                              std::move(provenance)});
  }
  if (cells.empty()) {
    if (error != nullptr)
      *error = "selector continuation marker lacks matching row ownership";
    return std::nullopt;
  }
  return cells;
}

std::optional<std::vector<SelectorDisplayCellIR>> generated_record_prefix_cells(
    const std::vector<DecodedLogicalRecordSource> &records,
    const std::map<OwnershipKey, const OwnedSourceCellIR *> &owned_cells,
    const PhysicalRowIR &row, std::string *error) {
  const auto *record = find_record(records, row.logical_record);
  if (record == nullptr ||
      row.segment_index >= record->control_segments.size()) {
    if (error != nullptr)
      *error = "generated continuation prefix references a missing segment";
    return std::nullopt;
  }
  const auto &segment = record->control_segments[row.segment_index];
  const std::set<std::size_t> segment_tokens(segment.source_tokens.begin(),
                                             segment.source_tokens.end());
  std::set<std::size_t> opaque_tokens;
  for (const auto &source : record->assembled.sources) {
    if (source.token_index >= row.token_begin ||
        segment_tokens.count(source.token_index) == 0 ||
        source.kind != LogicalWordSourceKind::token_word)
      continue;
    const auto owned = owned_cells.find(
        {row.logical_record, source.token_index, source.word_index});
    if (owned != owned_cells.end() && owned->second->run == 0 &&
        owned->second->disposition == SourceDisposition::opaque)
      opaque_tokens.insert(source.token_index);
  }

  std::vector<SelectorDisplayCellIR> cells;
  for (std::size_t output = 0; output < record->assembled.sources.size();
       ++output) {
    const auto &source = record->assembled.sources[output];
    if (source.token_index >= row.token_begin ||
        opaque_tokens.count(source.token_index) == 0)
      continue;
    if (source.kind == LogicalWordSourceKind::token_word) {
      const auto owned = owned_cells.find(
          {row.logical_record, source.token_index, source.word_index});
      if (owned == owned_cells.end() || owned->second->run != 0 ||
          owned->second->disposition != SourceDisposition::opaque)
        continue;
    } else if (record->assembled.words[output] != ' ') {
      continue;
    }
    SelectorSourceCellRefIR provenance;
    provenance.logical_record = row.logical_record;
    provenance.token_index = source.token_index;
    provenance.word_index = source.word_index;
    provenance.kind = source.kind == LogicalWordSourceKind::token_word
                          ? SelectorSourceCellKind::token_word
                          : SelectorSourceCellKind::inserted_space;
    provenance.token_bytes = record->ir.tokens[source.token_index].byte_range;
    cells.push_back({record->assembled.words[output],
                     SelectorDisplayCellOrigin::source, std::move(provenance)});
  }
  return cells;
}

bool generated_structural_marker(const PhysicalRowIR &row) {
  if (!row.marker)
    return false;
  const auto &text = row.marker->decoded_text;
  return text == "|" ||
         (!text.empty() && std::all_of(text.begin(), text.end(),
                                       [](char ch) { return ch == '?'; }));
}

bool target_equal(const SelectorTargetIR &left, const SelectorTargetIR &right) {
  return left.kind == right.kind && left.raw_target == right.raw_target &&
         left.resolved_target == right.resolved_target;
}

bool source_ref_equal(const SelectorSourceCellRefIR &left,
                      const SelectorSourceCellRefIR &right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.kind == right.kind &&
         left.token_bytes.begin == right.token_bytes.begin &&
         left.token_bytes.end == right.token_bytes.end;
}

bool cell_equal(const SelectorDisplayCellIR &left,
                const SelectorDisplayCellIR &right) {
  if (left.word != right.word || left.origin != right.origin ||
      left.source.has_value() != right.source.has_value())
    return false;
  return !left.source || source_ref_equal(*left.source, *right.source);
}

bool span_equal(const SelectorSpanIR &left, const SelectorSpanIR &right) {
  return ref_equal(left.selector, right.selector) &&
         target_equal(left.target, right.target) &&
         left.cell_begin == right.cell_begin && left.cell_end == right.cell_end;
}

bool row_equal(const SelectorDisplayRowIR &left,
               const SelectorDisplayRowIR &right) {
  if (left.id != right.id ||
      left.owner.logical_record != right.owner.logical_record ||
      left.owner.segment_index != right.owner.segment_index ||
      left.owner.run != right.owner.run ||
      left.owner.physical_row_index != right.owner.physical_row_index ||
      left.owner.token_begin != right.owner.token_begin ||
      left.owner.token_end != right.owner.token_end ||
      left.association != right.association ||
      left.hard_boundary != right.hard_boundary ||
      left.cells.size() != right.cells.size() ||
      left.suppressed_prefix_cells.size() !=
          right.suppressed_prefix_cells.size() ||
      left.spans.size() != right.spans.size())
    return false;
  for (std::size_t index = 0; index < left.cells.size(); ++index)
    if (!cell_equal(left.cells[index], right.cells[index]))
      return false;
  for (std::size_t index = 0; index < left.suppressed_prefix_cells.size();
       ++index)
    if (!cell_equal(left.suppressed_prefix_cells[index],
                    right.suppressed_prefix_cells[index]))
      return false;
  for (std::size_t index = 0; index < left.spans.size(); ++index)
    if (!span_equal(left.spans[index], right.spans[index]))
      return false;
  return true;
}

bool binding_equal(const SelectorBindingIR &left,
                   const SelectorBindingIR &right) {
  return ref_equal(left.selector, right.selector) && left.kind == right.kind &&
         left.owner_id == right.owner_id;
}

bool object_equal(const SelectorObjectIR &left, const SelectorObjectIR &right) {
  return left.id == right.id && ref_equal(left.selector, right.selector) &&
         target_equal(left.target, right.target);
}

const char *association_name(SelectorRowAssociation association) {
  switch (association) {
  case SelectorRowAssociation::inline_payload:
    return "inline";
  case SelectorRowAssociation::deferred_same_record:
    return "deferred-same-record";
  case SelectorRowAssociation::deferred_next_record:
    return "deferred-next-record";
  case SelectorRowAssociation::multiple_queued:
    return "multiple-queued";
  }
  return "invalid";
}

const char *binding_name(SelectorBindingKind kind) {
  switch (kind) {
  case SelectorBindingKind::display_span:
    return "display-span";
  case SelectorBindingKind::resource_object:
    return "resource-object";
  case SelectorBindingKind::table_owned:
    return "table-owned";
  }
  return "invalid";
}

} // namespace

std::optional<SelectorDisplayIR> extract_selector_display_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const SelectorCatalogIR &selectors, const LayoutIR &layout,
    const VerifiedOwnershipIR &verified_ownership, std::string *error) {
  const auto fail =
      [&](const std::string &message) -> std::optional<SelectorDisplayIR> {
    if (error != nullptr)
      *error = message;
    return std::nullopt;
  };
  if (records.empty())
    return fail("selector display source is empty");
  std::string verification_error;
  if (!verify_selector_catalog_ir(records, selectors, &verification_error))
    return fail("selector control IR is invalid: " + verification_error);
  if (!verify_layout_ir(records, layout, &verification_error))
    return fail("selector layout IR is invalid: " + verification_error);
  if (!ownership_verified_for(verified_ownership, records, layout,
                              &verification_error))
    return fail("selector ownership IR is invalid: " + verification_error);
  const OwnershipIR &ownership = verified_ownership;

  std::map<SegmentKey, const SelectorIR *> selector_by_segment;
  for (const auto &selector : selectors.selectors) {
    if (!selector.canonical_operands)
      return fail("selector has noncanonical operands at " +
                  std::to_string(selector.logical_record) + ':' +
                  std::to_string(selector.segment_index));
    if (!selector_by_segment
             .emplace(
                 SegmentKey{selector.logical_record, selector.segment_index},
                 &selector)
             .second)
      return fail("multiple selectors share one typed source segment");
  }

  std::map<SegmentKey, std::vector<IndexedRow>> rows_by_segment;
  for (const auto &run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      rows_by_segment[{row.logical_record, row.segment_index}].push_back(
          {&row, row_index});
    }
  }
  std::map<OwnershipKey, const OwnedSourceCellIR *> owned_cells;
  for (const auto &cell : ownership.cells)
    owned_cells.emplace(
        OwnershipKey{cell.logical_record, cell.token_index, cell.word_index},
        &cell);

  SelectorDisplayIR result;
  const auto generated_list = exact_generated_list_kind(records, selectors);
  const auto in_generated_list = generated_list != GeneratedListKind::none;
  std::vector<const SelectorIR *> pending;
  std::uint64_t next_row_id = 1;
  std::uint64_t next_object_id = 1;

  const auto bind_row = [&](const std::vector<IndexedRow> &candidates,
                            std::string *bind_error) -> bool {
    if (pending.empty() || candidates.empty() ||
        candidates.front().row == nullptr) {
      if (bind_error != nullptr)
        *bind_error = "selector row has no pending owner";
      return false;
    }
    if (in_generated_list && pending.size() != 1) {
      if (bind_error != nullptr)
        *bind_error = "generated selection list queued selectors at " +
                      std::to_string(pending.front()->logical_record) + ':' +
                      std::to_string(pending.front()->segment_index) + " and " +
                      std::to_string(pending.back()->logical_record) + ':' +
                      std::to_string(pending.back()->segment_index);
      return false;
    }
    const auto &indexed = candidates.front();
    const auto candidate_record = indexed.row->logical_record;
    for (const auto *selector : pending) {
      if (candidate_record < selector->logical_record ||
          candidate_record - selector->logical_record > 1) {
        if (bind_error != nullptr)
          *bind_error = "deferred selector crossed a nonadjacent record";
        return false;
      }
    }
    auto cells = row_cells(records, owned_cells, *indexed.row,
                           indexed.physical_row_index, bind_error, false,
                           in_generated_list && indexed.row->run == 0);
    if (!cells)
      return false;
    auto carried_prefix_cells = std::size_t{0};
    if (in_generated_list && indexed.row->segment_index == 0 &&
        std::all_of(pending.begin(), pending.end(), [&](const auto *selector) {
          return selector->logical_record + 1 == indexed.row->logical_record;
        })) {
      auto prefix = generated_record_prefix_cells(records, owned_cells,
                                                  *indexed.row, bind_error);
      if (!prefix)
        return false;
      carried_prefix_cells = prefix->size();
      cells->insert(cells->begin(), std::make_move_iterator(prefix->begin()),
                    std::make_move_iterator(prefix->end()));
    }
    if (in_generated_list && indexed.row->marker &&
        !generated_structural_marker(*indexed.row)) {
      auto markers =
          restored_marker_cells(records, owned_cells, *indexed.row,
                                indexed.physical_row_index, true, bind_error);
      if (!markers)
        return false;
      cells->insert(cells->begin() +
                        static_cast<std::ptrdiff_t>(carried_prefix_cells),
                    std::make_move_iterator(markers->begin()),
                    std::make_move_iterator(markers->end()));
    }

    // Compact punctuation markers can split one native display line into
    // several mechanical PhysicalRowIR fragments. A selector originating in
    // the immediately preceding record still uses coordinates in that native
    // line. Rejoin only contiguous fragments of the same run/record/segment.
    // For a cross-record continuation, restore the first fragment's exact
    // one-byte coordinate marker, then the exact punctuation marker owned by
    // every appended fragment. This is deliberately not a forward search: the
    // first fragment and every appended source token must be contiguous.
    auto required_end = std::size_t{0};
    for (const auto *selector : pending) {
      if (selector->column >
          std::numeric_limits<std::size_t>::max() - selector->length) {
        if (bind_error != nullptr)
          *bind_error = "selector span overflows";
        return false;
      }
      required_end =
          std::max(required_end, selector->column + selector->length);
    }
    auto owner_token_end = indexed.row->token_end;
    if (!in_generated_list && cells->size() < required_end &&
        indexed.row->marker &&
        std::all_of(pending.begin(), pending.end(), [&](const auto *selector) {
          return selector->logical_record != indexed.row->logical_record;
        })) {
      auto markers =
          restored_marker_cells(records, owned_cells, *indexed.row,
                                indexed.physical_row_index, false, bind_error);
      if (!markers)
        return false;
      cells->insert(cells->begin(), std::make_move_iterator(markers->begin()),
                    std::make_move_iterator(markers->end()));
    }
    for (std::size_t candidate = 1;
         (in_generated_list || cells->size() < required_end) &&
         candidate < candidates.size();
         ++candidate) {
      const auto &next = candidates[candidate];
      if (next.row == nullptr || next.row->run != indexed.row->run ||
          next.row->logical_record != indexed.row->logical_record ||
          next.row->segment_index != indexed.row->segment_index ||
          next.row->token_begin != owner_token_end)
        break;
      auto continuation = row_cells(records, owned_cells, *next.row,
                                    next.physical_row_index, bind_error, true);
      if (!continuation)
        return false;
      if (!in_generated_list || !generated_structural_marker(*next.row)) {
        auto markers = restored_marker_cells(records, owned_cells, *next.row,
                                             next.physical_row_index,
                                             in_generated_list, bind_error);
        if (!markers)
          return false;
        cells->insert(cells->end(), std::make_move_iterator(markers->begin()),
                      std::make_move_iterator(markers->end()));
      }
      cells->insert(cells->end(),
                    std::make_move_iterator(continuation->begin()),
                    std::make_move_iterator(continuation->end()));
      owner_token_end = next.row->token_end;
    }

    std::vector<SelectorDisplayCellIR> suppressed_prefix_cells;
    if (in_generated_list) {
      const auto first_visible =
          std::find_if(cells->begin(), cells->end(),
                       [](const auto &cell) { return presentable(cell.word); });
      if (first_visible == cells->end()) {
        if (bind_error != nullptr)
          *bind_error =
              "generated selection row has no presentable source cell";
        return false;
      }
      const auto source_prefix = static_cast<std::size_t>(
          std::distance(cells->begin(), first_visible));
      const auto desired_prefix = pending.front()->column;
      if (source_prefix > desired_prefix) {
        const auto suppress = source_prefix - desired_prefix;
        suppressed_prefix_cells.assign(
            cells->begin(),
            cells->begin() + static_cast<std::ptrdiff_t>(suppress));
        cells->erase(cells->begin(),
                     cells->begin() + static_cast<std::ptrdiff_t>(suppress));
      } else if (source_prefix < desired_prefix) {
        cells->insert(cells->begin(), desired_prefix - source_prefix,
                      SelectorDisplayCellIR{
                          ' ',
                          SelectorDisplayCellOrigin::restored_generated_prefix,
                          std::nullopt});
      }
    }

    std::vector<std::pair<std::size_t, std::size_t>> geometry;
    geometry.reserve(pending.size());
    for (const auto *selector : pending) {
      const auto end = selector->column + selector->length;
      if (end > cells->size()) {
        if (bind_error != nullptr) {
          std::string row_text;
          row_text.reserve(cells->size());
          for (const auto &cell : *cells)
            row_text.push_back(cell.word);
          *bind_error = "display row is shorter than selector geometry at " +
                        std::to_string(selector->logical_record) + ':' +
                        std::to_string(selector->segment_index) +
                        " target=" + selector->target +
                        " cells=" + std::to_string(cells->size()) +
                        " required=" + std::to_string(end) + " text='" +
                        row_text + "'";
        }
        return false;
      }
      if (!std::any_of(
              cells->begin() + static_cast<std::ptrdiff_t>(selector->column),
              cells->begin() + static_cast<std::ptrdiff_t>(end),
              [](const auto &cell) { return presentable(cell.word); })) {
        if (bind_error != nullptr)
          *bind_error = "selector span owns no presentable source cell at " +
                        std::to_string(selector->logical_record) + ':' +
                        std::to_string(selector->segment_index) +
                        " target=" + selector->target;
        return false;
      }
      geometry.emplace_back(selector->column, end);
    }
    auto ordered = geometry;
    std::sort(ordered.begin(), ordered.end());
    for (std::size_t index = 1; index < ordered.size(); ++index) {
      if (ordered[index].first < ordered[index - 1].second) {
        if (bind_error != nullptr)
          *bind_error = "selector spans overlap";
        return false;
      }
    }

    SelectorDisplayRowIR row;
    row.id = next_row_id++;
    row.owner = {indexed.row->logical_record,
                 indexed.row->segment_index,
                 indexed.row->run,
                 indexed.physical_row_index,
                 indexed.row->token_begin,
                 owner_token_end};
    row.cells = std::move(*cells);
    row.suppressed_prefix_cells = std::move(suppressed_prefix_cells);
    row.hard_boundary = in_generated_list;
    if (pending.size() > 1) {
      row.association = SelectorRowAssociation::multiple_queued;
    } else if (pending.front()->logical_record != indexed.row->logical_record) {
      row.association = SelectorRowAssociation::deferred_next_record;
    } else if (pending.front()->segment_index != indexed.row->segment_index) {
      row.association = SelectorRowAssociation::deferred_same_record;
    } else {
      row.association = SelectorRowAssociation::inline_payload;
    }
    for (std::size_t index = 0; index < pending.size(); ++index) {
      const auto *selector = pending[index];
      row.spans.push_back({selector_ref(*selector), target_ir(*selector),
                           geometry[index].first, geometry[index].second});
      result.bindings.push_back(
          {selector_ref(*selector), SelectorBindingKind::display_span, row.id});
    }
    result.rows.push_back(std::move(row));
    pending.clear();
    return true;
  };

  bool after_generated_title = false;
  for (const auto &record : records) {
    for (const auto &segment : record.control_segments) {
      const SegmentKey key{record.logical_record, segment.segment_index};
      const auto selector_found = selector_by_segment.find(key);
      const auto row_found = rows_by_segment.find(key);
      const auto has_rows =
          row_found != rows_by_segment.end() && !row_found->second.empty();

      if (in_generated_list && segment.kind == BookControlKind::title) {
        after_generated_title = true;
        continue;
      }

      if (selector_found != selector_by_segment.end()) {
        const auto *selector = selector_found->second;
        const auto target = target_ir(*selector);
        if (selector->inside_table ||
            target.kind != SelectorTargetKind::internal_anchor) {
          if (!pending.empty())
            return fail(
                "pending selector reached a table/resource object barrier");
          if (selector->inside_table) {
            result.bindings.push_back(
                {selector_ref(*selector), SelectorBindingKind::table_owned, 0});
          } else {
            SelectorObjectIR object{next_object_id++, selector_ref(*selector),
                                    target};
            result.bindings.push_back({selector_ref(*selector),
                                       SelectorBindingKind::resource_object,
                                       object.id});
            result.objects.push_back(std::move(object));
          }
          continue;
        }

        pending.push_back(selector);
        if (has_rows) {
          if (!bind_row(row_found->second, &verification_error))
            return fail(verification_error);
        } else if (payload_has_visible_text(selector->display_payload)) {
          return fail("visible selector payload has no canonical physical row");
        }
        continue;
      }

      if (pending.empty()) {
        if (in_generated_list && after_generated_title &&
            (segment.kind == BookControlKind::text ||
             segment.kind == BookControlKind::font) &&
            payload_has_visible_text(range_text(record, segment.payload_range)))
          return fail("generated selection list has unowned visible content");
        continue;
      }
      const auto eligible = segment.kind == BookControlKind::text ||
                            segment.kind == BookControlKind::font;
      if (has_rows) {
        if (!eligible)
          return fail(
              "pending selector reached a typed display-object barrier");
        if (!bind_row(row_found->second, &verification_error))
          return fail(verification_error);
      } else if (in_generated_list && eligible &&
                 payload_has_visible_text(
                     range_text(record, segment.payload_range)) &&
                 !segment.source_tokens.empty()) {
        PhysicalRowIR unlaid;
        unlaid.logical_record = record.logical_record;
        unlaid.segment_index = segment.segment_index;
        unlaid.token_begin = segment.source_tokens.front();
        unlaid.token_end = segment.source_tokens.back() + 1;
        unlaid.start = PhysicalRowStartKind::record_continuation;
        unlaid.break_before = PhysicalBreakKind::hard_paragraph;
        unlaid.continues_previous_record = true;
        unlaid.visible_text = range_text(record, segment.payload_range);
        if (!bind_row({IndexedRow{&unlaid, 0}}, &verification_error))
          return fail(verification_error);
      } else if (!eligible) {
        return fail("pending selector reached a typed control barrier");
      }
    }
  }
  if (!pending.empty())
    return fail("selector remains unresolved at end of source");
  if (result.bindings.size() != selectors.selectors.size())
    return fail("selector binding ledger does not conserve raw selectors");
  if (error != nullptr)
    error->clear();
  return result;
}

bool verify_selector_display_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const SelectorCatalogIR &selectors, const LayoutIR &layout,
    const VerifiedOwnershipIR &verified_ownership,
    const SelectorDisplayIR &display, std::string *error) {
  const auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return false;
  };
  if (display.bindings.size() != selectors.selectors.size())
    return fail("selector binding count differs from raw selector count");
  std::set<SelectorKey> bound;
  for (const auto &binding : display.bindings) {
    if (!bound.insert(selector_key(binding.selector)).second)
      return fail("selector has more than one semantic disposition");
    if (binding.kind == SelectorBindingKind::display_span) {
      const auto row = std::find_if(display.rows.begin(), display.rows.end(),
                                    [&](const auto &candidate) {
                                      return candidate.id == binding.owner_id;
                                    });
      if (row == display.rows.end() ||
          std::none_of(row->spans.begin(), row->spans.end(),
                       [&](const auto &span) {
                         return ref_equal(span.selector, binding.selector);
                       }))
        return fail("display binding has no matching selector span");
    } else if (binding.kind == SelectorBindingKind::resource_object) {
      const auto object =
          std::find_if(display.objects.begin(), display.objects.end(),
                       [&](const auto &item) {
                         return item.id == binding.owner_id &&
                                ref_equal(item.selector, binding.selector);
                       });
      if (object == display.objects.end())
        return fail("resource binding has no matching object");
    } else if (binding.owner_id != 0) {
      return fail("table-owned selector has a nonzero semantic owner");
    }
  }
  for (const auto &selector : selectors.selectors)
    if (bound.count(selector_key(selector_ref(selector))) != 1)
      return fail("raw selector has no semantic disposition");

  std::uint64_t previous_row = 0;
  std::set<std::tuple<std::uint32_t, std::size_t, std::size_t,
                      SelectorSourceCellKind>>
      owned_source_cells;
  for (const auto &row : display.rows) {
    if (row.id <= previous_row || row.cells.empty() || row.spans.empty())
      return fail("selector display rows are empty or out of order");
    auto ordered = row.spans;
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &left, const auto &right) {
                return left.cell_begin < right.cell_begin;
              });
    for (std::size_t index = 0; index < ordered.size(); ++index) {
      if (ordered[index].cell_begin >= ordered[index].cell_end ||
          ordered[index].cell_end > row.cells.size())
        return fail("selector span is outside its display row");
      if (index != 0 && ordered[index].cell_begin < ordered[index - 1].cell_end)
        return fail("selector display spans overlap");
    }
    for (const auto &cell : row.cells) {
      const auto source_backed =
          cell.origin == SelectorDisplayCellOrigin::source ||
          cell.origin == SelectorDisplayCellOrigin::restored_native_margin ||
          cell.origin == SelectorDisplayCellOrigin::restored_native_marker ||
          cell.origin == SelectorDisplayCellOrigin::restored_box_padding;
      if (source_backed && !cell.source)
        return fail("source-backed display cell has no provenance");
      if (!source_backed && cell.source)
        return fail("synthesized display cell claims source provenance");
      if (cell.source &&
          !owned_source_cells
               .emplace(cell.source->logical_record, cell.source->token_index,
                        cell.source->word_index, cell.source->kind)
               .second)
        return fail("source display cell is owned by more than one row");
    }
    for (const auto &cell : row.suppressed_prefix_cells) {
      if (!cell.source)
        return fail("suppressed generated-list prefix lacks provenance");
      if (!owned_source_cells
               .emplace(cell.source->logical_record, cell.source->token_index,
                        cell.source->word_index, cell.source->kind)
               .second)
        return fail("suppressed source cell is owned by more than one row");
    }
    previous_row = row.id;
  }

  const auto canonical = extract_selector_display_ir(
      records, selectors, layout, verified_ownership, nullptr);
  if (!canonical)
    return fail("source does not admit canonical selector display IR");
  if (canonical->rows.size() != display.rows.size() ||
      canonical->objects.size() != display.objects.size() ||
      canonical->bindings.size() != display.bindings.size())
    return fail("selector display shape differs from canonical lowering");
  for (std::size_t index = 0; index < display.rows.size(); ++index)
    if (!row_equal(canonical->rows[index], display.rows[index]))
      return fail("selector display row differs from canonical lowering at " +
                  std::to_string(index));
  for (std::size_t index = 0; index < display.objects.size(); ++index)
    if (!object_equal(canonical->objects[index], display.objects[index]))
      return fail("selector object differs from canonical lowering at " +
                  std::to_string(index));
  for (std::size_t index = 0; index < display.bindings.size(); ++index)
    if (!binding_equal(canonical->bindings[index], display.bindings[index]))
      return fail("selector binding differs from canonical lowering at " +
                  std::to_string(index));
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_selector_display_ir(const SelectorDisplayIR &display) {
  std::ostringstream out;
  out << "selector_display rows=" << display.rows.size()
      << " objects=" << display.objects.size()
      << " bindings=" << display.bindings.size() << '\n';
  for (const auto &row : display.rows) {
    out << "row=" << row.id << " owner=" << row.owner.logical_record << ':'
        << row.owner.segment_index << " run=" << row.owner.run << ':'
        << row.owner.physical_row_index
        << " association=" << association_name(row.association)
        << " cells=" << row.cells.size()
        << " suppressed-prefix=" << row.suppressed_prefix_cells.size();
    for (const auto &span : row.spans)
      out << " selector=" << span.selector.logical_record << ':'
          << span.selector.segment_index << ':' << span.selector.ordinal
          << " target='" << span.target.raw_target << "' span=["
          << span.cell_begin << ',' << span.cell_end << ')';
    out << '\n';
  }
  for (const auto &binding : display.bindings)
    out << "binding selector=" << binding.selector.logical_record << ':'
        << binding.selector.segment_index << ':' << binding.selector.ordinal
        << " kind=" << binding_name(binding.kind)
        << " owner=" << binding.owner_id << '\n';
  return out.str();
}

} // namespace geist::detail
