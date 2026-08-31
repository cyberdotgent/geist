#pragma once

#include "geist/detail/layout_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

enum class SourceDisposition {
  control_operand,
  layout_origin,
  layout_padding,
  marker_slot,
  visible_content,
  opaque,
};

// What the record's own display-line framing says about a source cell.
//
// A record payload tiles into `<length byte> <that many bytes>` display lines
// (book_ir.hpp, `TokenFramingRole`). The length byte is below the book's token
// threshold, so the dictionary spells an ordinary word for it -- `adapter`,
// `agent`, `and`, `be` -- and nothing about the cell itself separates that
// framing slot from the same word used as text. Only the walk from the record
// start does, and the record decoder has already done it. Carrying its answer
// here is what lets a consumer ask "does this cell occupy the display?"
// instead of re-deriving "is this a length byte" from a spelling.
enum class SourceFieldRole : std::uint8_t {
  // The record's payload does not tile into whole display lines, so no cell
  // of it has a decided field role. A consumer must not read this as either
  // answer.
  undecided,
  // The cell is content of a display line: the record's own framing places it
  // in the display.
  positioned,
  // The cell is a display line's length byte. It supplies the line's extent
  // and is never display text, whatever word the dictionary spells for it.
  supplemental,
};

struct OwnedSourceCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceDisposition disposition = SourceDisposition::opaque;
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  SourceFieldRole field_role = SourceFieldRole::undecided;
};

enum class RowCellRole {
  boundary,
  origin,
  padding,
  content,
};

// A source cell positioned in one physical row. Boundary cells participate in
// the row's source geometry but deliberately have no display column: assigning
// one would prematurely interpret the marker as visible prefix/suffix text.
struct PositionedRowCellIR {
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  RowCellRole role = RowCellRole::content;
  std::optional<std::size_t> display_column;
  // The record's display-line framing for this cell. A boundary cell still
  // carries no `display_column` -- the row's column table starts after the
  // marker -- but its field role says whether the record's own framing draws
  // the cell at all. `supplemental` is the line-length slot; `positioned` is
  // display text that the row happens to open on.
  SourceFieldRole field_role = SourceFieldRole::undecided;
};

enum class OwnershipConflictKind {
  // The row claims a cell that already holds a different disposition (for
  // example a control operand cell that the layout also uses as a marker).
  incompatible_disposition,
  // The row claims a cell that another run/row already owns.
  duplicate_row_assignment,
  // The row references a cell the source records do not contain.
  missing_source_cell,
  // A row-owned cell has no mechanical display column in the assembled output.
  no_display_column,
};

// A fail-closed ownership conflict scoped to one display run. The run owns no
// source cells and has no positioned row cells; every other run in the topic
// keeps its ownership. Consumers must treat a conflicted run as unowned and
// decline any structure that would include it.
struct OwnershipRunConflictIR {
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  OwnershipConflictKind kind = OwnershipConflictKind::incompatible_disposition;
  SourceDisposition existing = SourceDisposition::opaque;
  SourceDisposition requested = SourceDisposition::opaque;
};

struct OwnershipIR {
  std::vector<OwnedSourceCellIR> cells;
  std::vector<PositionedRowCellIR> row_cells;
  // Run-scoped, typed conflicts. The ledger remains verifiable; the listed
  // runs are simply unowned.
  std::vector<OwnershipRunConflictIR> run_conflicts;
  // Ledger-wide failures that leave no consistent ownership at all.
  std::vector<std::string> conflicts;
};

OwnershipIR build_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout);
bool verify_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const OwnershipIR& ownership,
    std::string* error = nullptr);

class VerifiedOwnershipIR;

// Builds the ledger for one topic and verifies it exactly once. A successful
// build is the only way to obtain a VerifiedOwnershipIR, so the fail-closed
// ownership check happens once per topic instead of once per consumer while
// remaining impossible to skip.
std::optional<VerifiedOwnershipIR> build_verified_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, std::string* error = nullptr);

// A ledger that has passed verify_ownership_ir against the records and layout
// it was built from. The constructor is private and the sole friend is
// build_verified_ownership_ir, which returns nothing when verification fails;
// a consumer that accepts this type therefore cannot be handed an unverified
// ledger, by accident or otherwise. The handle converts implicitly to the
// ledger it carries, so consumers that only read ownership need no change.
class VerifiedOwnershipIR {
 public:
  const OwnershipIR& ir() const { return ownership_; }
  operator const OwnershipIR&() const { return ownership_; }

  // True when this handle was verified against exactly these inputs. The
  // stored addresses are only ever compared, never dereferenced; the record
  // count and envelope are compared as well so that a container reusing a
  // dead handle's address cannot pass for the topic it was verified against.
  bool covers(const std::vector<DecodedLogicalRecordSource>& records,
              const LayoutIR& layout) const;

 private:
  VerifiedOwnershipIR(const std::vector<DecodedLogicalRecordSource>& records,
                      const LayoutIR& layout, OwnershipIR ownership);
  friend std::optional<VerifiedOwnershipIR> build_verified_ownership_ir(
      const std::vector<DecodedLogicalRecordSource>& records,
      const LayoutIR& layout, std::string* error);

  const std::vector<DecodedLogicalRecordSource>* records_ = nullptr;
  const LayoutIR* layout_ = nullptr;
  std::size_t record_count_ = 0;
  std::size_t run_count_ = 0;
  std::uint32_t first_logical_record_ = 0;
  std::uint32_t last_logical_record_ = 0;
  OwnershipIR ownership_;
};

// The guard a consumer of a verified ledger runs instead of re-verifying. It
// is free when the handle already attests these exact inputs and falls back to
// the full verification when it does not, so a consumer offered a ledger built
// for something else still fails closed exactly as before.
bool ownership_verified_for(
    const VerifiedOwnershipIR& ownership,
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, std::string* error = nullptr);
bool ownership_run_conflicted(const OwnershipIR& ownership, DisplayRunId run);
// The field role the record's stored display-line framing gives one token.
SourceFieldRole source_field_role(const DecodedLogicalRecordSource& record,
                                  std::size_t token);
std::string format_ownership_ir(const OwnershipIR& ownership);
std::string format_owned_source_cell_ir(const OwnedSourceCellIR& cell);
std::string format_positioned_row_cell_ir(const PositionedRowCellIR& cell);
std::string format_ownership_run_conflict_ir(
    const OwnershipRunConflictIR& conflict);

} // namespace geist::detail
