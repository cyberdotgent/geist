#pragma once

// Shared failure accounting for the standalone test executables.
//
// Assertions report immediately and keep running so one execution enumerates
// every failing expectation; the process still exits non-zero when any
// assertion failed. A test that cannot continue safely after a failure should
// still return early itself.

#include <cstdlib>
#include <iostream>

namespace geist_test {

inline int &failure_count() {
  static int count = 0;
  return count;
}

inline void exit_with_failures() {
  if (failure_count() == 0)
    return;
  std::cerr << failure_count() << " assertion(s) failed\n";
  std::cout.flush();
  std::cerr.flush();
  std::_Exit(1);
}

inline void record_failure() {
  static const bool registered = (std::atexit(exit_with_failures), true);
  (void)registered;
  ++failure_count();
}

} // namespace geist_test
