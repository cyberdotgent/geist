// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Concurrent `const` access to one opened document (issue #49).
//
// libgeist's public contract is that every `const` operation on an opened
// `BooDocument` and its `TocEntry` values may be called from any number of
// threads at once with no external synchronisation. That contract is not kept
// by locking: it is kept by the observation that rendering a topic is a *pure
// function* of state that is immutable from `open()` onward, so a cache slot
// can be computed redundantly and published atomically, and two threads racing
// on one topic simply produce the same bytes (geist/detail/atomic_cache.hpp).
//
// The whole argument therefore rests on determinism. If a lowering path ever
// gained order-dependent output -- iteration over a `std::map` keyed by
// pointer, a hash order, an uninitialised read, anything that varies between
// two runs over identical input -- then the benign race would become a real
// one: two threads would compute *different* values and a caller could see
// either. Determinism used to be merely desirable. It is now load-bearing.
//
// So this file asserts it directly, and says so in the failure text:
//
//   * `deterministic_render` renders every topic of the fixture twice, from
//     two independently opened documents, and requires byte equality. Two
//     documents rather than two calls on one, because a memoised second call
//     would prove nothing about the computation.
//   * `contended_topic` puts several threads on *the same* `TocEntry` of *the
//     same* document at the same moment. Threads rendering different topics
//     never touch one entry's cache and so cannot trip the race this guards.
//   * `contended_document` does the same for the document-level caches --
//     font definitions, resources, pages, per-topic rendering and the trace
//     path that decodes source slices.
//
// Every thread's answer is compared against a reference computed serially on a
// cold document, so a race that corrupted a cache would show up as a mismatch
// and not merely as a sanitizer report. Run under ThreadSanitizer too:
//
//   cmake -S libgeist -B build_tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
//     -DCMAKE_CXX_FLAGS=-fsanitize=thread \
//     -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
//
// Only `packet.boo` may be redistributed, so it is the only book used here
// (issue #59).

#include "geist/document.hpp"
#include "test_failures.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads = 8;

// The shared failure accounting and `std::cerr` are the test harness's own
// state, not the library's, and this is the one test that reports from several
// threads -- so the reporting path, and only the reporting path, takes a lock.
// Nothing the library owns is guarded here; that is the point of the test.
std::mutex &report_mutex() {
  static std::mutex mutex;
  return mutex;
}

void require(const bool condition, const std::string &message) {
  if (condition)
    return;
  const std::lock_guard<std::mutex> lock(report_mutex());
  std::cerr << "FAIL: " << message << "\n";
  geist_test::record_failure();
}

std::filesystem::path fixture() {
  return std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
}

// Releases every worker at once, so the threads collide inside the cache
// rather than arriving one after another and each finding it already filled.
class StartLine {
public:
  explicit StartLine(int participants) : participants_(participants) {}

  void wait() {
    if (arrived_.fetch_add(1) + 1 == participants_) {
      open_.store(true);
      return;
    }
    while (!open_.load())
      std::this_thread::yield();
  }

private:
  const int participants_;
  std::atomic<int> arrived_{0};
  std::atomic<bool> open_{false};
};

template <typename Body> void run_threads(Body body) {
  StartLine line(kThreads);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int index = 0; index < kThreads; ++index) {
    threads.emplace_back([&line, &body, index] {
      line.wait();
      body(index);
    });
  }
  for (auto &thread : threads)
    thread.join();
}

std::string describe(const geist::RenderDiagnostic &diagnostic) {
  return std::to_string(static_cast<int>(diagnostic.severity)) + "|" +
         diagnostic.reason + "|" + diagnostic.detail;
}

std::string describe(const std::vector<geist::LinkTarget> &targets) {
  std::string out;
  for (const auto &target : targets) {
    out += std::to_string(static_cast<int>(target.kind));
    out += ':';
    out += target.id;
    out += ';';
  }
  return out;
}

// Rendering must be a pure function of the book. This is the premise the
// lock-free cache publication rests on; see the file header.
void deterministic_render() {
  const auto first = geist::BooDocument::open(fixture());
  const auto second = geist::BooDocument::open(fixture());
  require(first.table_of_contents().size() ==
              second.table_of_contents().size(),
          "two opens of one book disagree about the table of contents");
  if (first.table_of_contents().size() != second.table_of_contents().size())
    return;

  for (std::size_t index = 0; index < first.table_of_contents().size();
       ++index) {
    const auto &here = first.table_of_contents()[index];
    const auto &there = second.table_of_contents()[index];
    const std::string what = "topic " + here.id;
    require(here.markdown() == there.markdown(),
            what +
                ": two independent renders of one topic differ. Rendering "
                "must be deterministic: libgeist publishes its lazy caches "
                "without locks precisely because two threads computing one "
                "topic must produce identical bytes (issue #49, "
                "geist/detail/atomic_cache.hpp). A nondeterministic lowering "
                "path turns that benign race into a real one -- fix the "
                "nondeterminism rather than this test.");
    require(describe(here.render_diagnostic()) ==
                describe(there.render_diagnostic()),
            what + ": two independent renders disagree about the render "
                   "diagnostic; see the determinism note above");
    require(describe(here.link_targets()) == describe(there.link_targets()),
            what + ": two independent renders disagree about the link "
                   "targets; see the determinism note above");
  }

  require(first.font_definitions() == second.font_definitions(),
          "two opens of one book disagree about the font definitions");
  require(first.markdown() == second.markdown(),
          "two opens of one book render different whole-book Markdown");
}

// Several threads on one TocEntry of one document, entering together.
void contended_topic() {
  const auto reference_document = geist::BooDocument::open(fixture());
  require(!reference_document.table_of_contents().empty(),
          "the fixture has no TOC entries to contend on");
  if (reference_document.table_of_contents().empty())
    return;

  // A topic with content of its own, so the render pass has real work to
  // collide over rather than returning an empty string immediately.
  const geist::TocEntry *reference = &reference_document.table_of_contents()[0];
  for (const auto &candidate : reference_document.table_of_contents()) {
    if (candidate.markdown().size() > reference->markdown().size())
      reference = &candidate;
  }
  const auto expected_markdown = reference->markdown();
  const auto expected_diagnostic = describe(reference->render_diagnostic());
  const auto expected_targets = describe(reference->link_targets());
  const auto topic_id = reference->id;

  const auto document = geist::BooDocument::open(fixture());
  const auto *entry = document.find_toc_entry(topic_id);
  require(entry != nullptr, "the contended topic is missing: " + topic_id);
  if (entry == nullptr)
    return;

  // Every thread must observe the same published objects, not merely equal
  // ones: a slot is filled once and never replaced, which is what makes the
  // references these accessors hand out safe to keep.
  std::atomic<const void *> published_targets{nullptr};
  std::atomic<const void *> published_diagnostic{nullptr};

  run_threads([&](int index) {
    for (int repeat = 0; repeat < 4; ++repeat) {
      switch ((index + repeat) % 4) {
      case 0:
        require(entry->markdown() == expected_markdown,
                "concurrent markdown() of " + topic_id +
                    " differs from the serial render");
        break;
      case 1: {
        const auto &diagnostic = entry->render_diagnostic();
        require(describe(diagnostic) == expected_diagnostic,
                "concurrent render_diagnostic() of " + topic_id +
                    " differs from the serial render");
        const void *previous = published_diagnostic.exchange(&diagnostic);
        require(previous == nullptr || previous == &diagnostic,
                "render_diagnostic() of " + topic_id +
                    " handed two threads different objects: the cache slot "
                    "was published more than once");
        break;
      }
      case 2: {
        const auto &targets = entry->link_targets();
        require(describe(targets) == expected_targets,
                "concurrent link_targets() of " + topic_id +
                    " differs from the serial answer");
        const void *previous = published_targets.exchange(&targets);
        require(previous == nullptr || previous == &targets,
                "link_targets() of " + topic_id +
                    " handed two threads different objects: the cache slot "
                    "was published more than once");
        break;
      }
      default: {
        geist::RenderTrace trace;
        const auto traced = entry->markdown(trace);
        require(!traced.empty() || expected_markdown.empty(),
                "concurrent markdown(trace) of " + topic_id +
                    " produced nothing");
        break;
      }
      }
    }
  });
}

// Several threads across one document's own caches and read paths.
void contended_document() {
  const auto reference_document = geist::BooDocument::open(fixture());
  const auto expected_fonts = reference_document.font_definitions();
  const auto expected_records = reference_document.decoded_logical_records();
  const auto expected_book = reference_document.markdown();
  std::vector<std::string> topic_ids;
  for (const auto &entry : reference_document.table_of_contents())
    topic_ids.push_back(entry.id);
  std::vector<std::string> expected_topics;
  for (const auto &id : topic_ids)
    expected_topics.push_back(reference_document.topic_markdown(id));
  std::string resource_id;
  if (!reference_document.resources().empty())
    resource_id = reference_document.resources().front().id;

  const auto document = geist::BooDocument::open(fixture());
  std::atomic<const void *> published_fonts{nullptr};

  run_threads([&](int index) {
    const auto &fonts = document.font_definitions();
    require(fonts == expected_fonts,
            "concurrent font_definitions() differs from the serial answer");
    const void *previous = published_fonts.exchange(&fonts);
    require(previous == nullptr || previous == &fonts,
            "font_definitions() handed two threads different objects: the "
            "cache slot was published more than once");

    require(document.decoded_logical_records() == expected_records,
            "concurrent decoded_logical_records() differs");
    require(document.read_page(0).size() == 4096,
            "concurrent read_page(0) did not return one physical page");
    if (!resource_id.empty())
      require(!document.read_resource_data(resource_id).empty(),
              "concurrent read_resource_data() returned nothing");

    // Each thread starts at a different topic, so the threads overlap on the
    // shared decode context while walking the same list.
    for (std::size_t step = 0; step < topic_ids.size(); ++step) {
      const auto at = (step + static_cast<std::size_t>(index)) %
                      topic_ids.size();
      require(document.topic_markdown(topic_ids[at]) == expected_topics[at],
              "concurrent topic_markdown(" + topic_ids[at] +
                  ") differs from the serial render");
    }

    require(document.markdown() == expected_book,
            "concurrent whole-book markdown() differs from the serial render");
  });

  // The provenance path. The one-record memo it needs is a replacement cache,
  // so it cannot be published once; it therefore lives in a caller-owned
  // `TraceSourceReader` rather than on the shared decode context, and no lock
  // is left anywhere on this path. What the threads still share is the
  // document -- including the publish-once token dictionary each reader binds
  // to at construction -- so this asserts both that a per-thread reader agrees
  // with the serial answer and that building readers concurrently is safe.
  const auto &toc = document.table_of_contents();
  if (!toc.empty()) {
    geist::RenderTrace reference_trace;
    toc.front().markdown(reference_trace);
    if (!reference_trace.spans.empty()) {
      std::vector<geist::RenderTraceSlice> slices;
      for (const auto &span : reference_trace.spans)
        for (const auto &slice : span.slices)
          slices.push_back(slice);
      std::vector<std::string> expected;
      geist::TraceSourceReader reference_reader(document);
      for (const auto &slice : slices)
        expected.push_back(reference_reader.decode(slice));
      run_threads([&](int index) {
        geist::TraceSourceReader reader(document);
        for (std::size_t step = 0; step < slices.size(); ++step) {
          const auto at = (step + static_cast<std::size_t>(index)) %
                          slices.size();
          require(reader.decode(slices[at]) == expected[at],
                  "concurrent TraceSourceReader::decode() differs from the "
                  "serial answer");
        }
      });
    }
  }
}

} // namespace

int main() {
  deterministic_render();
  contended_topic();
  contended_document();
  std::cout << "concurrent const access assertions complete\n";
  return 0;
}
