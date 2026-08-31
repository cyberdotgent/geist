#pragma once

#include "geist/detail/internal.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace geist::detail {

// Where a contents record's display lines start, in the record's flattened
// projection.
//
// The book's table of contents is a run of `CTocE` entries inside the
// contents topic's records.  Each entry occupies one display line, so the
// record payload reads
//
//   <length byte> CTocE <level> <style> <id> <title>
//   <length byte> CTocE <level> <style> <id> <title>
//   ...
//
// (book_ir.hpp, `TokenFramingRole::line_length`).  A length byte is a raw
// byte below the book's token threshold, so a token reader resolves it
// through the dictionary and it acquires an arbitrary spelling: a run of
// blanks in most books, but also `;`, `%`, `:MSGNO`, `[`, `//`, `$`, `*`,
// `----------` and even a control operand such as `<BOOK>`.  Flattened, that
// spelling lands between one entry's title and the next entry's `CTocE`, and
// a reader that stops the title at the next `CTocE` keeps it.
//
// The framing is the only thing that separates the two roles, so it is what
// decides the title's end.  This hands back the flattened-string offset at
// which each display line's length byte begins; a `CTocE` title ends at the
// first such offset after it.
std::vector<std::size_t> display_line_start_output_offsets(
    const DecodedLogicalRecordSource& record);

} // namespace geist::detail
