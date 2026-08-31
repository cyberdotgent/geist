#pragma once

#include "geist/detail/ir/book_ir.hpp"
#include "geist/detail/container/control_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

enum class MenuSourceCellKind {
  token_word,
  inserted_space,
};

// Exact decoded-output/source identity for one cell consumed by a menu
// operand or label.  output_word_index disambiguates inserted spaces that can
// otherwise share a token-local coordinate.
struct MenuSourceCellIR {
  std::uint32_t logical_record = 0;
  std::size_t output_word_index = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  MenuSourceCellKind kind = MenuSourceCellKind::token_word;
  std::uint16_t word = 0;
  SourceByteRange token_bytes;
};

// Source-local structure of the final label token when it is one compact
// width-1 encoded word without spacing control.  Source alone cannot decide
// whether that word is a display marker (`>`, `[`, `++`) or the last word of
// the title; catalog validation makes that decision from this evidence.
struct MenuCompactTerminalTokenIR {
  std::size_t token_index = 0;
  EncodedLogicalToken encoded;
  SourceByteRange bytes;
  std::size_t display_cells = 0;
  // Index into label_cells of the first cell produced by the token; every
  // later label cell belongs to the token or is trailing inserted space.
  std::size_t label_cell_begin = 0;
};

// Source-local structure of a menu item's record terminator token: the item's
// last payload token, a width-1 spacing-control token whose only visible word
// is the `.` glyph, standing immediately before the CEMENU control's first
// source token.  The same token shape closes the ST payload of the same
// record and is what produces the doubled `10577..` there (DREICMST record 28
// token 257 is the sentence period, token 259 the terminator), so a terminator
// glyph carries no label text.  Only the last item of a menu can have one;
// source alone records the evidence, catalog validation decides whether to
// exclude it.
struct MenuTerminatorTokenIR {
  std::size_t token_index = 0;
  EncodedLogicalToken encoded;
  SourceByteRange bytes;
  // Index into label_cells of the first cell produced by the token; every
  // later label cell belongs to the token or is trailing inserted space.
  std::size_t label_cell_begin = 0;
};

struct MenuItemIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::string target;
  std::string text;
  OutputRangeIR target_output;
  OutputRangeIR label_output;
  std::vector<MenuSourceCellIR> target_cells;
  std::vector<MenuSourceCellIR> label_cells;
  std::optional<MenuCompactTerminalTokenIR> compact_terminal;
  std::optional<MenuTerminatorTokenIR> terminator;
  std::optional<std::size_t> terminal_marker_token;
  std::optional<EncodedLogicalToken> terminal_marker_encoded;
  std::optional<SourceByteRange> terminal_marker_bytes;
  std::optional<std::size_t> terminal_marker_display_cells;
};

struct MenuIR {
  std::vector<MenuItemIR> items;
};

// Extract a menu using only the topic's decoded source.  Unlike the broader
// compatibility path below, this does not infer or repair labels by consulting
// the book-wide topic-title catalog.  Consequently terminal-marker metadata is
// never present; only the source-local compact_terminal evidence is recorded,
// and validate_source_menu_targets() decides whether it is a marker.
std::optional<MenuIR> extract_source_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error = nullptr);
bool verify_source_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const MenuIR& menu,
    std::string* error = nullptr);

std::optional<MenuIR> extract_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::map<std::string, std::string>& topic_titles,
    std::string* error = nullptr);
bool verify_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::map<std::string, std::string>& topic_titles,
    const MenuIR& menu,
    std::string* error = nullptr);
std::string format_menu_ir(const MenuIR& menu);

} // namespace geist::detail
