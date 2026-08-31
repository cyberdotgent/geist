#include "geist/trace.hpp"
#include "geist/detail/topic_lowering_outcome.hpp"

#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/source_slice_text.hpp"
#include "geist/document.hpp"
#include "geist/toc.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>

namespace geist {
namespace {

const char* derivation_name(detail::DocumentDerivationIR value) noexcept {
  switch (value) {
  case detail::DocumentDerivationIR::decoded: return "decoded";
  case detail::DocumentDerivationIR::semantic_lowering: return "semantic";
  case detail::DocumentDerivationIR::synthesized: return "synthesized";
  }
  return "invalid";
}

RenderTraceRole public_role(detail::DocumentTraceRoleIR value) noexcept {
  switch (value) {
  case detail::DocumentTraceRoleIR::content: return RenderTraceRole::content;
  case detail::DocumentTraceRoleIR::generated:
    return RenderTraceRole::generated;
  case detail::DocumentTraceRoleIR::syntax: break;
  }
  return RenderTraceRole::syntax;
}

} // namespace

const char* render_trace_role_name(RenderTraceRole role) noexcept {
  switch (role) {
  case RenderTraceRole::content: return "content";
  case RenderTraceRole::syntax: return "syntax";
  case RenderTraceRole::generated: return "generated";
  }
  return "invalid";
}

const RenderTraceSpan* RenderTrace::span_at(std::size_t offset) const noexcept {
  const auto found = std::upper_bound(
      spans.begin(), spans.end(), offset,
      [](std::size_t value, const RenderTraceSpan& span) {
        return value < span.output_begin;
      });
  if (found == spans.begin()) return nullptr;
  const auto& span = *std::prev(found);
  return offset < span.output_end ? &span : nullptr;
}

namespace detail {

RenderTrace to_public_render_trace(const DocumentRenderTraceIR& trace) {
  RenderTrace result;
  result.spans.reserve(trace.spans.size());
  for (const auto& span : trace.spans) {
    RenderTraceSpan out;
    out.output_begin = span.output_begin;
    out.output_end = span.output_end;
    out.role = public_role(span.role);
    out.reason = span.reason;
    out.node_path = format_document_node_path(span.path);
    if (span.origin) {
      out.derivation = derivation_name(span.origin->derivation);
      out.origin_detail = span.origin->detail;
      out.slices.reserve(span.origin->slices.size());
      for (const auto& slice : span.origin->slices)
        out.slices.push_back({slice.logical_record, slice.segment_index,
                              slice.token_begin, slice.token_end,
                              slice.byte_begin, slice.byte_end,
                              slice.character_begin, slice.character_end});
    }
    result.spans.push_back(std::move(out));
  }
  return result;
}

} // namespace detail

std::string TocEntry::markdown(RenderTrace& trace) const {
  trace.spans.clear();
  const auto& outcome = lowered().outcome;
  if (!outcome || !outcome->document) {
    // The legacy whole-topic route has no typed nodes to point at; the
    // caller is told so by an empty trace rather than by a fabricated one.
    return markdown();
  }
  detail::DocumentRenderTraceIR raw;
  auto rendered = detail::render_document_markdown(*outcome->document, {},
                                                   &raw);
  trace = detail::to_public_render_trace(raw);
  return rendered;
}

namespace {

// The one-record provenance memo, owned by the reader that uses it.  Proving a
// whole topic asks for many slices of one record in a row, so a single-slot
// memo turns that from quadratic into linear.  It lives here, per caller,
// rather than on the shared decode context, so the serving path keeps no
// mutable state at all.
struct SourceRecordMemo {
  std::uint32_t logical_record = 0;
  std::shared_ptr<const detail::LogicalRecordIR> record;
};

} // namespace

struct TraceSourceReader::State {
  // Keeps the decoded source alive independently of the document, so a reader
  // may outlive the `BooDocument` it was made from.
  std::shared_ptr<const detail::LogicalDecodeContext> context;
  // The context's token dictionary is published once and never replaced, so
  // this pointer stays valid for as long as `context` does.
  const std::map<std::uint16_t, detail::TokenWords>* dictionary = nullptr;
  SourceRecordMemo memo;
};

TraceSourceReader::TraceSourceReader(const BooDocument& document)
    : state_(std::make_unique<State>()) {
  if (!document.decode_context_)
    throw std::runtime_error("BOO document has no decoded source context");
  state_->context = document.decode_context_;
  state_->dictionary = &detail::source_dictionary_for(*state_->context);
}

TraceSourceReader::~TraceSourceReader() = default;
TraceSourceReader::TraceSourceReader(TraceSourceReader&&) noexcept = default;
TraceSourceReader& TraceSourceReader::operator=(TraceSourceReader&&) noexcept =
    default;

std::string TraceSourceReader::decode(const RenderTraceSlice& slice) {
  detail::DocumentSourceSliceIR internal;
  internal.logical_record = slice.logical_record;
  internal.segment_index = slice.segment_index;
  internal.token_begin = slice.token_begin;
  internal.token_end = slice.token_end;
  internal.byte_begin = slice.byte_begin;
  internal.byte_end = slice.byte_end;
  internal.character_begin = slice.character_begin;
  internal.character_end = slice.character_end;
  std::string error;
  const auto& context = *state_->context;
  const auto& dictionary = *state_->dictionary;
  // The record's own payload window is authoritative: a display-line record is
  // re-lined by the record decoder, so a walk that starts mid-record can land
  // off the token grid even though the slice's offsets are exact.
  const auto& ranges = context.record_payload_ranges;
  if (slice.logical_record != 0 && slice.logical_record <= ranges.size()) {
    auto& memo = state_->memo;
    if (memo.logical_record != slice.logical_record || !memo.record) {
      const auto& range = ranges[slice.logical_record - 1];
      memo.record = std::make_shared<const detail::LogicalRecordIR>(
          detail::decode_record_payload_ir(context.bytes, context.directory,
                                           dictionary, range.begin, range.end,
                                           slice.logical_record));
      memo.logical_record = slice.logical_record;
    }
    if (const auto text =
            detail::project_source_slice_text(*memo.record, internal, &error))
      return *text;
  }
  const auto text = detail::decode_source_slice_text(
      context.bytes, context.directory, dictionary, internal, &error);
  if (!text) throw std::runtime_error("BOO source slice is unreadable: " + error);
  return *text;
}

} // namespace geist
