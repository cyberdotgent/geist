#pragma once

#include "geist/export.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace geist {

class BooDocument;

// What one run of rendered Markdown is.  `content` is a projection of BOO
// source text and names the bytes it came from; `syntax` is Markdown the
// renderer adds around content; `generated` is reader-style text the renderer
// invents and no BOO byte states.
enum class RenderTraceRole {
  content,
  syntax,
  generated,
};

GEIST_API const char* render_trace_role_name(RenderTraceRole role) noexcept;

// Source coordinates of one run, in the original BOO file.  `byte_begin` and
// `byte_end` address the file itself, so they can be read back and decoded
// again independently of the renderer.
struct RenderTraceSlice {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::uint32_t byte_begin = 0;
  std::uint32_t byte_end = 0;
  // Byte range inside the single token's decoded word when nonzero.
  std::uint32_t character_begin = 0;
  std::uint32_t character_end = 0;
};

struct RenderTraceSpan {
  // Half-open byte range of the rendered Markdown.
  std::size_t output_begin = 0;
  std::size_t output_end = 0;
  RenderTraceRole role = RenderTraceRole::syntax;
  // Stable class of the run, e.g. "text", "heading marker", "table pipe".
  std::string reason;
  // Path from the document root to the producing node, e.g.
  // "block[7]/row[2]/cell[1]/inline[0]".
  std::string node_path;
  // "decoded", "semantic", "synthesized" or "legacy".
  std::string derivation;
  // The lowerer's own reason for the node, never rendered content.
  std::string origin_detail;
  std::vector<RenderTraceSlice> slices;
};

// The output-range to node map produced beside a rendered topic.  Spans are
// ordered, non-overlapping, and together cover the whole rendered output.
struct RenderTrace {
  std::vector<RenderTraceSpan> spans;

  // The span covering `offset`, or nullptr past the end of the output.
  GEIST_API const RenderTraceSpan* span_at(std::size_t offset) const noexcept;
};

// Reads back the BOO file bytes a trace slice names and returns the display
// text they hold.  This re-decodes the file rather than restating what the
// renderer believed, so it can prove or disprove a slice.
//
// Resolving a rendered span asks for many slices of one logical record in a
// row, so a reader keeps that record decoded between calls.  That memo is the
// reader's own -- it is the reason this is a handle and not a method on
// `BooDocument`.  A reader is therefore **not** thread-safe and must not be
// shared: give each thread its own.  Constructing one is cheap, and the
// document it is made from stays immutable and may be shared freely; a reader
// keeps the decoded source alive, so it may outlive the document it came from.
class TraceSourceReader {
public:
  // Throws when the document carries no decoded source context.
  GEIST_API explicit TraceSourceReader(const BooDocument& document);
  GEIST_API ~TraceSourceReader();
  GEIST_API TraceSourceReader(TraceSourceReader&&) noexcept;
  GEIST_API TraceSourceReader& operator=(TraceSourceReader&&) noexcept;
  TraceSourceReader(const TraceSourceReader&) = delete;
  TraceSourceReader& operator=(const TraceSourceReader&) = delete;

  // The display text `slice` names.  Throws when the named window does not
  // tile into whole tokens.  Non-const: reading updates the reader's memo.
  GEIST_API std::string decode(const RenderTraceSlice& slice);

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace geist
