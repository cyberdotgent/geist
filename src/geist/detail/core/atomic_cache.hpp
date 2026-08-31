#pragma once

// Fill-once caches that are published, never mutated (issue #49).
//
// Every lazy cache libgeist keeps is a pure function of state that is fully
// built and thereafter immutable once `BooDocument::open()` returns: the file
// bytes, the directory, the decoded record index, the topic catalog. A topic
// never changes, so two threads that compute the same slot produce equal
// values. That makes the *computation* safe to duplicate and leaves only the
// *publication of the pointer* needing synchronisation.
//
// `publish_once` therefore computes into a fresh `shared_ptr<const T>` and
// installs it only while the slot is still empty. A thread that loses the race
// discards its own result and returns the winner's, so a published value is
// never replaced and never mutated: a reference into it stays valid for as
// long as the cache lives, which is what lets `link_targets()`,
// `render_diagnostic()` and `font_definitions()` keep returning references.
// The worst case under contention is one redundant computation whose result is
// dropped -- no lost update, no torn read, no partially constructed state.
//
// This argument holds only while rendering is deterministic. That property is
// therefore load-bearing for thread safety and is asserted explicitly by
// `libgeist/tests/concurrent_const_access.cpp`; read its header before
// introducing anything order-dependent into a lowering path.
//
// A replacement cache -- one whose slot is overwritten with a different value,
// such as `LogicalDecodeContext::source_record_memo` -- cannot use this and
// stays mutex-guarded.
//
// C++17 has no `std::atomic<std::shared_ptr<T>>`, so the free `std::atomic_*`
// overloads on `shared_ptr` are used here. They are deprecated in C++20.
// MIGRATION: when this project raises `cxx_std_17` in libgeist/CMakeLists.txt,
// replace `CacheSlot<T>` with `std::atomic<std::shared_ptr<const T>>` and the
// bodies below with its `load()` / `compare_exchange_strong()`; nothing outside
// this header names the free functions.

#include <memory>
#include <utility>

namespace geist::detail {

// The storage a `publish_once` slot needs. Declaring it through this alias
// keeps every site that participates in the scheme greppable, and gives the
// C++20 migration above a single type to change.
template <typename T>
using CacheSlot = std::shared_ptr<const T>;

// The slot's current value, or null when nothing has been published yet.
template <typename T>
std::shared_ptr<const T> peek_cache(const CacheSlot<T>& slot) {
  return std::atomic_load(&slot);
}

// Returns the published value of `slot`, computing it first if the slot is
// still empty. `compute` must be a pure function of immutable state and must
// never return null; it may run on several threads at once, in which case
// exactly one result is published and the others are discarded.
//
// The returned pointer is the published one, so dereferencing it is safe for
// as long as the slot lives: the value in the slot is never replaced.
template <typename T, typename Compute>
std::shared_ptr<const T> publish_once(CacheSlot<T>& slot, Compute compute) {
  if (auto published = std::atomic_load(&slot))
    return published;
  std::shared_ptr<const T> fresh = compute();
  // `expected` starts null, so the exchange succeeds only against a still
  // empty slot; when it fails it is loaded with the value that beat us, and
  // that -- not our own -- is what every thread must go on to use.
  std::shared_ptr<const T> expected;
  if (std::atomic_compare_exchange_strong(&slot, &expected, fresh))
    return fresh;
  return expected;
}

} // namespace geist::detail
