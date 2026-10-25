// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/core/internal.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace geist::detail {

// The name the reader gives the anchor an `SR` control row opens: the text of
// the row after `SR`, exactly as stored -- a run of blanks inside it stays a
// run -- without the blanks that end the row.  AFM41MST 1.3 stores
// `SRMSG   25` and hosted BookServer (DT 19910927164256) serves
// `<a name="MSG   25">`; GA27-3832-08 1.4 `SRMSG 96  to  98` is served as
// `<a name="MSG 96  to  98">`.
//
// `opcode_token` is the control's first token; it must open its native
// display line.  Nullopt where the record's framing is undecided, the token
// does not open its line, the line does not spell `SR`, or the row draws a
// word the name cannot spell (anything but printable ASCII or Latin-1).
std::optional<std::string>
sr_row_anchor_name(const DecodedLogicalRecordSource &record,
                   std::size_t opcode_token);

// The same for a message control's row: `MSG` followed by the row after
// `SRMSG` as stored (`MSG   25`, bare `MSG`).  Nullopt where
// `sr_row_anchor_name` is, or the row does not spell `SRMSG`.
std::optional<std::string>
message_row_anchor_name(const DecodedLogicalRecordSource &record,
                        const ControlSegmentIR &segment);

} // namespace geist::detail
