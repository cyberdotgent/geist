#pragma once

#include "geist/detail/internal.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// The title a topic's own header carries.
//
// A topic header writes its title with the `ST` control, and the title is the
// visible text of that control's *display line* -- not the `ST` payload run of
// the flattened decoded record, which continues past the row break into the
// topic's body (Format/logical-controls.md, "Display Lines Govern Reflowed
// Prose Too").  The row is what hosted BookServer serves as the heading:
// QSYSINFO 2.1.21 (DT 19910524120827) heads the topic
// `SC09-1159, Languages:  System/38-Compatible COBOL User's Guide and` and
// opens the body with `Reference`.
//
// The line is found positionally: it is the first display line of the record
// whose first visible token spells `ST`.  That reaches the two source shapes
// alike -- the `ST` control the flattened splitter classifies, and the form
// where the opcode is glued to a one-cell marker (`ST|`, GC23-046 record 192
// token 29 `|`, ACPZMST1 record 78 token 26 U+2502) and so is split as a text
// segment.
//
// Inside the line:
//
//   * a marker slot *glued* to the opcode is layout, and the title begins
//     after the space that follows it.  `ST` followed by a space carries no
//     marker, so a leading punctuation word there is title text: SC24-5520-00
//     6.11.3 is `ST  *ACCOUNT System Service`, SH20-918 3.1 is
//     `ST  :ABSTRACT--Document Abstract` and SC09-138 4.7.1 is `ST  __amrc`,
//     while GC23-046 7.0 is `ST| Chapter 7.  Online Books`.
//
//   * a following control segment ends the title, unless the display-line
//     pass already proved that segment's opcode word to be display text
//     (display_lines.hpp): SC24-5527-02 record 605 line 8 is
//     `ST  Create an APPLY List from Two SRVAPPS Tables`, one row, and
//     `SRVAPPS` is the table's name.
//
// Returns `std::nullopt` when the record does not parse into display lines or
// carries no `ST` line, so the caller can fail over rather than invent a
// title.  An `ST` line with no payload is an empty title, and returns "".
std::optional<std::string> topic_header_title_of_record(
    const DecodedLogicalRecordSource& record);

} // namespace geist::detail
