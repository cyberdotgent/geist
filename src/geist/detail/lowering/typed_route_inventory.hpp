// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/layout_ir.hpp"
#include "geist/render_diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Which whole-topic route a TOC topic reaches. `typed` means
// try_lower_topic_to_document_ir produced a verified DocumentIR; `legacy`
// means it declined and the verbatim best-effort route renders the topic.
enum class TypedRouteKind {
  typed,
  legacy,
};

// Structural evidence for one topic taken only from the typed Control IR and
// Layout IR: control kinds, SR opcodes, selector targets, and physical row
// start kinds. Nothing here inspects Markdown or normalized string records.
struct TopicStructureIR {
  std::size_t rows = 0;
  // Rows opened by a '?' placeholder run. These mark display-line wraps in
  // prose as well as fixed cell/border geometry, so they are reported but
  // are not table evidence by themselves.
  std::size_t placeholder_rows = 0;
  // Rows whose visible text opens with a bullet or ordinal item marker.
  std::size_t list_rows = 0;
  std::size_t table_controls = 0;   // SRTBL
  std::size_t figure_controls = 0;  // SRFIG
  std::size_t image_selectors = 0;  // CSELECT targeting a book resource
  std::size_t selectors = 0;        // every other CSELECT
  std::size_t font_controls = 0;    // CFONT
  std::size_t menu_controls = 0;    // CMENU/CMITEM/CEMENU
  std::size_t message_controls = 0; // SRMSG
  std::size_t other_structural_controls = 0; // remaining SR* anchors
  std::size_t malformed_controls = 0;
  // Lower-case CHDLEVEL operand (":h1", ":toc", ":cover", ...), empty when
  // the topic header carries none.
  std::string heading_kind;
};

TopicStructureIR extract_topic_structure_ir(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const LayoutIR &layout, const std::set<std::string> &resource_ids);

// Feature set present in the topic body, joined with '+', e.g.
// "lists+selectors"; "prose" when no feature is present.
std::string topic_structure_signature(const TopicStructureIR &structure);
// One census class per topic; see classify_topic_structure in the source for
// the precedence rules.
std::string classify_topic_structure(const std::string &topic_id,
                                     const TopicStructureIR &structure);

struct TypedRouteTopicIR {
  std::string id;
  std::string title;
  std::uint32_t level = 0;
  TypedRouteKind route = TypedRouteKind::legacy;
  // Typed family that claimed the topic (set for typed topics and for
  // verification rejections).
  std::string family;
  // Whole-topic rejection when a family claimed the topic and failed.
  std::string rejection;
  // "<family>: <reason>" for every recognizer that declined the topic.
  std::vector<std::string> declined;
  // How well the topic renders, and why. `route`, `family` and the reason
  // column are all read out of this one value, so the coverage metric and the
  // export renderer cannot describe the same topic differently.
  RenderDiagnostic diagnostic;
  TopicStructureIR structure;
};

struct TypedRouteInventoryIR {
  std::vector<TypedRouteTopicIR> topics;
  std::size_t typed_count = 0;
  std::size_t legacy_count = 0;
  std::map<std::string, std::size_t> typed_by_family;
  // Topic count per RenderSeverity name ("typed", "typed-degraded",
  // "legacy-fallback", "best-effort", "failed").
  std::map<std::string, std::size_t> by_severity;
};

// The reason column of the per-topic report: empty for typed topics, the
// rejection for a failed family, otherwise the declined recognizers joined by
// " | ".
std::string typed_route_reason(const TypedRouteTopicIR &topic);

} // namespace geist::detail
