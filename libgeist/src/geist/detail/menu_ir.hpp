#pragma once

#include "geist/detail/book_ir.hpp"
#include "geist/detail/control_ir.hpp"

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

struct MenuItemIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::string target;
  std::string text;
  OutputRangeIR target_output;
  OutputRangeIR label_output;
  std::vector<MenuSourceCellIR> target_cells;
  std::vector<MenuSourceCellIR> label_cells;
  std::optional<std::size_t> terminal_marker_token;
  std::optional<EncodedLogicalToken> terminal_marker_encoded;
  std::optional<SourceByteRange> terminal_marker_bytes;
  std::optional<std::size_t> terminal_marker_display_cells;
};

struct MenuIR {
  std::vector<MenuItemIR> items;
};

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
