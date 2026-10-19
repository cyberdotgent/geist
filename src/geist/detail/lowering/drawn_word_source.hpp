// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;
struct OwnershipIR;

// Native display text and byte provenance used by the independent source-side
// conservation walk. Control metadata is excluded only where framing proves
// its opcode; a similarly spelled ordinary word remains drawn text.
struct DrawnTextIR {
  std::string text;
  // Per-byte provenance: true only in the first token after a native prefix,
  // the position where a control opcode can stand.
  std::vector<bool> opening;
  // Per-byte index into the source-line descriptions below.
  std::vector<std::size_t> line;
  std::vector<std::string> sources;
};

DrawnTextIR
project_drawn_topic_text(const std::vector<DecodedLogicalRecordSource> &records,
                         const OwnershipIR &ownership,
                         std::size_t *unframed_records);

} // namespace geist::detail
