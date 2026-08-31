// The caller-owned handle that reads a trace slice back to its source bytes.
//
// The one-record memo a slice walk needs is a *replacement* cache, so it
// cannot be published once the way every other cache in the library is.  It
// therefore lives in the caller's own reader rather than on the shared decode
// context, which is what leaves the serving path with no mutable state at all
// (issue #49).  Three properties make that safe to rely on, and each is
// asserted below:
//
//   * the memo is transparent -- a reader walking a whole topic's slices in
//     order answers exactly what a fresh reader per slice answers, so nothing
//     about the answer depends on what the reader read before;
//   * two readers over one document are independent, including when they walk
//     the same records in opposite orders;
//   * a reader keeps the decoded source alive, so it may outlive the document
//     it was made from.
//
// Concurrency is covered separately by concurrent_const_access.cpp.

#include "geist/document.hpp"
#include "geist/trace.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

bool require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "trace_source_reader: " << message << '\n';
    ++failures;
  }
  return condition;
}

// Every slice of the first traced topic that carries any, in render order.
std::vector<geist::RenderTraceSlice> traced_slices(
    const geist::BooDocument &document) {
  for (const auto &entry : document.table_of_contents()) {
    geist::RenderTrace trace;
    entry.markdown(trace);
    std::vector<geist::RenderTraceSlice> slices;
    for (const auto &span : trace.spans)
      for (const auto &slice : span.slices)
        slices.push_back(slice);
    if (slices.size() > 1)
      return slices;
  }
  return {};
}

} // namespace

int main() {
  const auto path = std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
  const auto document = geist::BooDocument::open(path);

  const auto slices = traced_slices(document);
  if (!require(!slices.empty(),
               "packet.boo produced no traced source slices to read back"))
    return 1;

  // The memo must not change any answer: one reader walking the whole topic
  // has a warm memo for all but the first slice of each record, a fresh
  // reader per slice never does.
  geist::TraceSourceReader walker(document);
  std::vector<std::string> walked;
  for (const auto &slice : slices)
    walked.push_back(walker.decode(slice));

  for (std::size_t at = 0; at < slices.size(); ++at) {
    geist::TraceSourceReader cold(document);
    require(cold.decode(slices[at]) == walked[at],
            "a warm memo changed the text of slice " + std::to_string(at));
  }

  // Re-reading the same slices in reverse order through a second reader must
  // give the same answers: the memo is keyed by logical record, so a reverse
  // walk replaces it on almost every step.
  geist::TraceSourceReader backwards(document);
  for (std::size_t at = slices.size(); at > 0; --at)
    require(backwards.decode(slices[at - 1]) == walked[at - 1],
            "a reverse walk changed the text of slice " +
                std::to_string(at - 1));

  // Two readers interleaved over one document stay independent.
  geist::TraceSourceReader left(document);
  geist::TraceSourceReader right(document);
  for (std::size_t at = 0; at < slices.size(); ++at) {
    const auto &other = slices[slices.size() - 1 - at];
    require(left.decode(slices[at]) == walked[at] &&
                right.decode(other) == walked[slices.size() - 1 - at],
            "interleaved readers disagreed at slice " + std::to_string(at));
  }

  // At least one slice must decode to something, or the checks above are
  // vacuous.
  bool any_text = false;
  for (const auto &text : walked)
    if (!text.empty())
      any_text = true;
  require(any_text, "every traced slice decoded to empty text");

  // A reader holds the decoded source itself, so it outlives its document.
  {
    std::vector<std::string> outlived;
    {
      auto scoped = std::make_unique<geist::BooDocument>(
          geist::BooDocument::open(path));
      geist::TraceSourceReader reader(*scoped);
      scoped.reset();
      for (const auto &slice : slices)
        outlived.push_back(reader.decode(slice));
    }
    require(outlived == walked,
            "a reader outliving its document answered differently");
  }

  // A slice that names no real window is refused rather than guessed at.
  {
    geist::RenderTraceSlice bogus;
    bogus.logical_record = 0xffffffff;
    bogus.token_begin = 0;
    bogus.token_end = 1;
    bogus.byte_begin = 0xfffffff0;
    bogus.byte_end = 0xffffffff;
    geist::TraceSourceReader reader(document);
    bool threw = false;
    try {
      reader.decode(bogus);
    } catch (const std::exception &) {
      threw = true;
    }
    require(threw, "an unreadable slice was not refused");
  }

  return failures == 0 ? 0 : 1;
}
