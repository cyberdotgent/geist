// What a topic names, and where the answer comes from.
//
// A book-wide link map has to know, for every topic, which ids cross
// references may use to reach it.  Until this file existed the only source of
// that answer was `TocEntry::gml_records()`, so `boo2git` ran the legacy
// string renderer over all 7,362 corpus topics even though only 375 of them
// render through it.  Anchor, figure and table discovery was therefore the
// largest live surface of `markup.cpp`, and the legacy renderer could not
// shrink to the topics that actually need it.
//
// A typed topic answers from its own Document IR: every named destination is
// an `AnchorBlockIR`, and `AnchorRoleIR` carries the distinction the legacy
// GML projection used to state with a different tag per kind.  Only a topic
// that really renders through the legacy route is asked for its GML.

#include "geist/link_target.hpp"

#include "geist/detail/document_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/topic_lowering_outcome.hpp"
#include "geist/toc.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace geist {
namespace detail {

namespace {

// The id a `SRFIG<id>` / `SRTBL<id>` anchor was built from.  Both anchor
// spellings are the whole opcode without its `SR` prefix, so the reference id
// is the anchor without the three-letter object prefix.  An anchor that does
// not carry the prefix its role claims is left alone rather than truncated:
// the caller reports it as a naming the typed IR could not resolve.
std::string strip_object_prefix(const std::string& anchor,
                                const char* prefix) {
  return ascii_starts_with_case_insensitive(anchor, prefix)
             ? anchor.substr(3)
             : anchor;
}

// `markup.cpp` spells a table's reference id from the object id the same way,
// and cross references in source use that spelling.
std::string table_reference_id(std::string object_id) {
  if (object_id.empty())
    return object_id;
  if (ascii_starts_with_case_insensitive(object_id, "tbltbl"))
    return object_id;
  if (ascii_starts_with_case_insensitive(object_id, "tbl"))
    return "TBL" + object_id;
  return object_id;
}

std::string raw_attr(const std::string& record, const std::string& attr) {
  const auto pattern = attr + "='";
  const auto begin = record.find(pattern);
  if (begin == std::string::npos)
    return {};
  const auto value_begin = begin + pattern.size();
  const auto value_end = record.find('\'', value_begin);
  if (value_end == std::string::npos || value_end <= value_begin)
    return {};
  return record.substr(value_begin, value_end - value_begin);
}

// `:hdref refid='pic<n>'` names a stored object by ordinal.
std::string picture_resource_id(const std::string& target) {
  if (target.size() <= 3 || !ascii_starts_with_case_insensitive(target, "pic"))
    return {};
  for (std::size_t index = 3; index < target.size(); ++index)
    if (std::isdigit(static_cast<unsigned char>(target[index])) == 0)
      return {};
  return target.substr(3);
}

} // namespace

// The typed answer: every anchor block the topic's Document IR contains, in
// document order, with the role the lowering proved for it.  A figure's
// stored object is the `FigureBlockIR` that follows its anchor, which is the
// same adjacency the legacy `:fig` / `:image` record pair states.
std::vector<LinkTarget> document_link_targets(const DocumentIR& document) {
  std::vector<LinkTarget> targets;
  // An index, not a pointer: pushing the next target may reallocate.
  auto pending_figure = std::string::npos;
  for (const auto& block : document.blocks) {
    if (const auto* anchor = std::get_if<AnchorBlockIR>(&block.node)) {
      pending_figure = std::string::npos;
      if (anchor->id.empty() || anchor->role == AnchorRoleIR::local)
        continue;
      // A role the lowering proved is authoritative.  Where it did not prove
      // one -- a figure or table region the family declined, whose control
      // the prose stream kept as a plain anchor -- the anchor id is still the
      // whole opcode without its `SR`, so the object prefix in the id says
      // what the object is.  That is the same evidence `markup.cpp` reads,
      // which is why the two answers agree.
      auto role = anchor->role;
      if (role == AnchorRoleIR::cross_reference) {
        if (ascii_starts_with_case_insensitive(anchor->id, "fig"))
          role = AnchorRoleIR::figure;
        else if (ascii_starts_with_case_insensitive(anchor->id, "tbl"))
          role = AnchorRoleIR::table;
        else if (ascii_starts_with_case_insensitive(anchor->id, "ftn"))
          // `SRFTN<id>` produces no GML record at all: a footnote is reached
          // only from the topic that prints it.
          continue;
      }
      LinkTarget target;
      switch (role) {
      case AnchorRoleIR::figure:
        target.kind = LinkTargetKind::figure;
        target.id = strip_object_prefix(anchor->id, "fig");
        break;
      case AnchorRoleIR::table:
        target.kind = LinkTargetKind::table;
        target.id = table_reference_id(strip_object_prefix(anchor->id, "tbl"));
        break;
      case AnchorRoleIR::cross_reference:
      case AnchorRoleIR::local:
        target.kind = LinkTargetKind::anchor;
        target.id = anchor->id;
        break;
      }
      const auto is_figure = target.kind == LinkTargetKind::figure;
      targets.push_back(std::move(target));
      if (is_figure)
        pending_figure = targets.size() - 1;
      continue;
    }
    if (const auto* figure = std::get_if<FigureBlockIR>(&block.node)) {
      if (pending_figure != std::string::npos &&
          ascii_starts_with_case_insensitive(figure->resource, "resource:"))
        targets[pending_figure].resource = figure->resource;
      pending_figure = std::string::npos;
      continue;
    }
  }
  // Names the source gives the whole topic that the document does not place
  // as an anchor.  They resolve to the file, exactly as a placed anchor of
  // cross-reference role does.
  for (const auto& id : document.named_destinations)
    if (!id.empty())
      targets.push_back({LinkTargetKind::anchor, id, {}});
  return targets;
}

// The legacy answer, unchanged in behaviour: the same scan `boo2git` used to
// run inline over `TocEntry::gml_records()`.  It is reached only by a topic
// that renders through the legacy string route.
std::vector<LinkTarget> gml_link_targets(
    const std::vector<std::string>& records) {
  std::vector<LinkTarget> targets;
  auto pending_figure = std::string::npos;
  for (const auto& record : records) {
    if (record.rfind(":anchor ", 0) == 0) {
      auto id = raw_attr(record, "id");
      if (!id.empty())
        targets.push_back({LinkTargetKind::anchor, std::move(id), {}});
      continue;
    }
    if (record.rfind(":fig ", 0) == 0) {
      pending_figure = std::string::npos;
      auto id = raw_attr(record, "id");
      if (!id.empty()) {
        targets.push_back({LinkTargetKind::figure, std::move(id), {}});
        pending_figure = targets.size() - 1;
      }
      continue;
    }
    if (record.rfind(":table ", 0) == 0) {
      auto id = raw_attr(record, "id");
      if (!id.empty())
        targets.push_back({LinkTargetKind::table, std::move(id), {}});
      continue;
    }
    if (record.rfind(":image ", 0) == 0) {
      const auto resource = raw_attr(record, "resource");
      if (pending_figure != std::string::npos && !resource.empty()) {
        targets[pending_figure].resource = "resource:" + resource;
        pending_figure = std::string::npos;
      }
      continue;
    }
    if (record.rfind(":hdref ", 0) == 0) {
      const auto resource = picture_resource_id(raw_attr(record, "refid"));
      if (pending_figure != std::string::npos && !resource.empty()) {
        targets[pending_figure].resource = "resource:" + resource;
        pending_figure = std::string::npos;
      }
    }
  }
  return targets;
}

} // namespace detail

const std::vector<LinkTarget>& TocEntry::link_targets() const {
  render();
  if (link_targets_built_)
    return cached_link_targets_;
  link_targets_built_ = true;
  const detail::DocumentIR* document =
      cached_lowering_ && cached_lowering_->document
          ? &*cached_lowering_->document
          : nullptr;
  cached_link_targets_ = document != nullptr
                             ? detail::document_link_targets(*document)
                             : detail::gml_link_targets(gml_records());
  return cached_link_targets_;
}

} // namespace geist
