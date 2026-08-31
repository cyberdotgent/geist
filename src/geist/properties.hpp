#pragma once

#include <string>
#include <vector>

namespace geist {

// Book-level properties decoded from logical header controls. Unknown or absent
// controls are represented by empty strings/vectors.
struct BooBookProperties {
  std::string language;
  std::string version;
  std::string build_version;
  bool reflow = false;
  std::string title;
  std::string short_title;
  std::string copyright;
  std::string security;
  std::string date;
  std::vector<std::string> authors;
  std::string document_number;
};

// Decoded logical metadata controls. The current decoder is experimental and
// only covers the version-2 dictionary/token paths documented from the bundled
// fixtures.
struct BooLogicalControl {
  std::string key;
  std::string value;
};

} // namespace geist
