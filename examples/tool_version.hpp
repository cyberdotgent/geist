// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// What a tool answers `--version` with.
//
// The same shape mod_geist reports, for the same reason: the tool's own
// version is compiled in, and libgeist's is asked for at run time. With the
// two packaged apart a tool can be running against a libgeist it was not
// built against, and only a call reports the one actually loaded. When they
// disagree, saying so is the point.

#pragma once

#include "geist/version.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace geist_tool {

// True when the arguments ask for the version, in which case it has been
// printed and the caller should exit successfully.
inline bool answered_version_request(const char* tool, int argc,
                                     char** argv) {
  bool asked = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0 ||
        std::strcmp(argv[i], "-V") == 0) {
      asked = true;
      break;
    }
  }
  if (!asked) {
    return false;
  }

  const std::string revision = GEIST_TOOL_REVISION;
  std::cout << tool << '/' << GEIST_TOOL_VERSION;
  if (!revision.empty() && revision != "unknown") {
    std::cout << " (" << revision << ')';
  }
  std::cout << " libgeist/" << geist::library_version();
  const std::string library = geist::library_revision();
  if (!library.empty() && library != "unknown") {
    std::cout << " (" << library << ')';
  }
  std::cout << '\n';
  return true;
}

} // namespace geist_tool
