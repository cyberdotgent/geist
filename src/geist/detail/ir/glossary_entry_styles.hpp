// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/glossary_ir.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// One content segment of a glossary entry (its `CFONT` and text segments),
// in source order.
struct GlossaryStyleSegmentIR {
  const DecodedLogicalRecordSource *record = nullptr;
  const ControlSegmentIR *segment = nullptr;
};

// The CFONT styles of one glossary entry: `term` ranges index the entry's
// term, `definition` ranges its composed definition prose.
struct GlossaryEntryStylesIR {
  std::vector<GlossaryEmphasisIR> term;
  std::vector<GlossaryEmphasisIR> definition;
};

// Reads the styles the entry's `CFONT` controls give its drawn words.  Each
// control styles the next display line that opens on a blank (in the same
// record or, when the control ends its record, the next one's first), and
// its triples are display columns of that line.  A triple is admitted only
// where the columns it leaves out of each word it covers are punctuation the
// word opens or ends with (`<B>key</B>.`, `(<B>a</B>`).  The first drawn words of the entry's lines must spell the
// term, its delimiter glued to the term's last word, and the rest exactly the
// definition for the definition's styles to stand; a triple the columns do
// not prove, or a term that does not verify, leaves the whole entry plain.
GlossaryEntryStylesIR
glossary_entry_styles(const std::vector<DecodedLogicalRecordSource> &records,
                      const std::vector<GlossaryStyleSegmentIR> &segments,
                      const std::string &term, const std::string &delimiter,
                      const std::string &prose);

} // namespace geist::detail
