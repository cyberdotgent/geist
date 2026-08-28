#include "geist/detail/prose_topic_internal.hpp"

#include <algorithm>
#include <cctype>

namespace geist::detail::prose_internal {

struct LineBuilder {
  LineBuilder(const std::vector<DecodedLogicalRecordSource>& sources,
              const std::vector<Item>& stream, Ledger& owner, LineBuild& build,
              std::string* message)
      : records(sources), items(stream), ledger(owner), out(build),
        error(message) {}

  const std::vector<DecodedLogicalRecordSource>& records;
  const std::vector<Item>& items;
  Ledger& ledger;
  LineBuild& out;
  std::string* error;

  bool in_title = false;
  bool title_done = false;
  bool in_index = false;
  ProseIndexTermIR current_term;
  std::vector<std::pair<std::size_t, std::size_t>> term_refs;
  bool pending_space = false;
  std::size_t trailing_bare = 0;
  bool anchor_pending = false;
  std::size_t pending_anchor_index = npos;
  std::vector<std::size_t> pending_controls;
  bool line_open = false;
  std::size_t line_visible_cells = 0;

  Line& line() { return out.lines.back(); }

  bool is_token(std::size_t index) const {
    return index < items.size() && items[index].kind == ItemKind::token;
  }
  // Next token item after `index`, skipping controls, segment ends and bare
  // spacing tokens.
  std::size_t next_token(std::size_t index) const {
    for (auto cursor = index + 1; cursor < items.size(); ++cursor) {
      // A table/figure span is a hard boundary: no row geometry crosses it.
      if (items[cursor].kind == ItemKind::span) return npos;
      if (items[cursor].kind != ItemKind::token) continue;
      if (is_bare(items[cursor].token)) continue;
      return cursor;
    }
    return npos;
  }
  bool space_at(std::size_t index) const {
    return index != npos && is_token(index) && is_space_run(items[index].token);
  }
  bool visible_at(std::size_t index) const {
    return index != npos && is_token(index) && is_visible(items[index].token);
  }
  std::size_t run_length(std::size_t index) const {
    return items[index].token.body.size();
  }
  bool assign(const TokenView& view, ProseTokenRoleIR role) {
    return ledger.assign(view.record, view.token, role, error);
  }

  void open_line(std::size_t origin_cells, const TokenView* origin) {
    Line fresh;
    fresh.origin = origin_cells;
    fresh.breaks_before = trailing_bare;
    trailing_bare = 0;
    if (anchor_pending) {
      fresh.anchor_before = true;
      fresh.anchor_index = pending_anchor_index;
      anchor_pending = false;
    }
    if (origin != nullptr) {
      for (std::size_t word = 0; word < origin->body.size(); ++word)
        fresh.cells.push_back({origin->record, origin->token, " ", true});
      implied_origin = origin->body.size();
    }
    fresh.text_begin = fresh.cells.size();
    out.lines.push_back(std::move(fresh));
    line_open = true;
    line_visible_cells = 0;
    pending_space = false;
  }

  bool ensure_line() {
    if (!line_open) open_line(0, nullptr);
    return true;
  }

  // Finishes the ST title at the first structural boundary.
  bool finish_title() {
    if (!in_title) return true;
    in_title = false;
    title_done = true;
    out.title = collapse_ascii_whitespace(out.title);
    if (out.title.empty()) return fail(error, "ST title is empty");
    return true;
  }

  bool finish_index() {
    if (!in_index) return true;
    in_index = false;
    current_term.term = collapse_ascii_whitespace(current_term.term);
    current_term.slices = slices_for(records, term_refs);
    if (current_term.term.empty())
      return fail(error, "SI control has an empty index term");
    out.index_terms.push_back(std::move(current_term));
    current_term = {};
    term_refs.clear();
    return true;
  }

  void append_space_cells(const TokenView& view, bool literal_gap) {
    (void)literal_gap;
    for (std::size_t word = 0; word < view.body.size(); ++word)
      line().cells.push_back({view.record, view.token, " ", true});
  }

  bool append_visible(const TokenView& view, ProseTokenRoleIR role) {
    if (!ensure_line()) return false;
    if (pending_space && view.prefix != 0 && view.prefix != 1)
      line().cells.push_back({npos, 0, " ", true});
    for (const auto word : view.body)
      line().cells.push_back({view.record, view.token, word_text(word),
                              word == ' '});
    ++line_visible_cells;
    last_visible = body_text(view);
    pending_space = view.prefix != 2;
    if (!assign(view, role)) return false;
    if (role == ProseTokenRoleIR::text && !pending_controls.empty()) {
      for (const auto control : pending_controls) {
        const auto& item = items[control];
        if (item.kind == ItemKind::font) {
          for (const auto& span : item.spans)
            line().fonts.push_back(
                {span.column, span.column + span.length, span.style, {}});
        } else {
          line().links.push_back({item.column, item.column + item.length,
                                  FontStyleIR::unknown, item.target});
        }
      }
      pending_controls.clear();
    }
    return true;
  }

  // Classifies the token at `index` as a one-byte marker slot when it is
  // followed by exactly one origin run of three or more cells and then the
  // next line's first visible token.  A width-one word followed by a fill
  // run and another marker is text (the row's last word).
  // A standalone layout glyph (`(`, `)`, `-`, `<`, `>`, `/`, `=`, `"`...)
  // before any line break is a marker in both shapes; attached punctuation
  // (`conditions:`, `useful.`) and words are visible when a fill run
  // separates them from the origin.
  static bool ballot_token(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
                                        [](char ch) { return ch == '_'; });
  }

  static bool alpha_word(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::isalpha(static_cast<int>(word)) != 0;
           });
  }

  static bool alnum_word(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::isalnum(static_cast<int>(word)) != 0;
           });
  }

  std::string last_visible;
  // Origin of the most recent row opened by an explicit origin run; an
  // implied row break (word + lone run + word) reuses it.
  std::size_t implied_origin = 3;

  std::string where(const TokenView& view) const {
    return "record " +
           std::to_string(records[view.record].logical_record) + " token " +
           std::to_string(view.token);
  }

  static bool punctuation_glyph(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::ispunct(static_cast<int>(word)) != 0;
           });
  }

  bool marker_at(std::size_t index, std::size_t& origin_index) const {
    const auto& view = items[index].token;
    if (view.width != 1 || !is_visible(view)) return false;
    const auto space = next_token(index);
    if (!space_at(space)) return false;
    if (space_at(next_token(space))) {
      // Fill/origin pair: a standalone glyph is a marker here, and so is a
      // one-byte alphanumeric piece glued (no space) onto a preceding word:
      // genuine text never joins two alphanumeric pieces without
      // punctuation (FA1PLMM0 record 1133 `Messages` + `access`).
      const auto attached =
          !pending_space || view.prefix == 0 || view.prefix == 1;
      // A glued one-byte word in the row-control byte range is the slot
      // whatever precedes it: N2AH1MST PREFACE.4 `to:` + `access` (0x1c),
      // `Reference.` + `an` (the compact-marker collision in Format/markup.md).
      const auto glued_word =
          attached && alnum_word(view) &&
          ((!last_visible.empty() &&
            std::isalnum(static_cast<unsigned char>(last_visible.back())) !=
                0) ||
           view.value < row_control_byte_limit);
      if (!(punctuation_glyph(view) && !attached) && !glued_word)
        return false;
      auto last = next_token(space);
      while (space_at(next_token(last))) last = next_token(last);
      origin_index = last;
      return true;
    }
    const auto after = next_token(space);
    if (space_at(after) || !visible_at(after)) return false;
    // `( sp1 │ text` / `a sp1 │ text` (ACPZMST1 record 35 tokens 134 and
    // 163): the one-byte slot before a one-cell origin and the row's visual
    // marker glyph; the glyph proves the row start.
    const auto glyph_before_slot =
        run_length(space) < 3 && pending_space &&
        (punctuation_glyph(view) || view.value < row_control_byte_limit) &&
        is_placeholder_run(items[after].token) &&
        items[after].token.width == 1;
    if (run_length(space) < 3 && !glyph_before_slot) return false;
    if (run_length(space) >= 3 && alnum_word(view) &&
        view.value >= row_control_byte_limit)
      return false;
    // Exception: the following token is itself a marker candidate.
    const auto& following = items[after].token;
    if (following.width == 1 && !is_bullet_glyph(following)) {
      const auto space2 = next_token(after);
      if (space_at(space2) && run_length(space2) >= 3) {
        const auto after2 = next_token(space2);
        if (visible_at(after2) && !space_at(after2)) return false;
      }
    }
    origin_index = space;
    return true;
  }

  bool run() {
    for (std::size_t index = 0; index < items.size(); ++index) {
      const auto& item = items[index];
      switch (item.kind) {
      case ItemKind::segment_end:
        if (!finish_title() || !finish_index()) return false;
        break;
      case ItemKind::font:
      case ItemKind::select:
        if (in_title) return fail(error, "font/selector inside the ST title");
        pending_controls.push_back(index);
        break;
      case ItemKind::anchor: {
        if (!finish_title() || !finish_index()) return false;
        ProseAnchorIR anchor;
        anchor.id = item.anchor_id;
        anchor.source = item.source;
        out.body_anchors.push_back(std::move(anchor));
        anchor_pending = true;
        pending_anchor_index = out.body_anchors.size() - 1;
        line_open = false;
        break;
      }
      case ItemKind::span:
        if (!finish_title() || !finish_index()) return false;
        if (!pending_controls.empty())
          return fail(error, "font/selector control has no display text "
                             "before a table/figure span");
        out.span_marks.push_back(
            {item.span_index, out.lines.size(), out.body_anchors.size()});
        line_open = false;
        pending_space = false;
        skip_until = npos;
        break;
      case ItemKind::token:
        if (!token(index)) return false;
        break;
      }
    }
    if (!finish_title() || !finish_index()) return false;
    if (!pending_controls.empty())
      return fail(error, "font/selector control has no display text");
    if (anchor_pending) {
      // Trailing anchor: precedes no line; keep as terminal anchor.
      anchor_pending = false;
    }
    if (!title_done) return fail(error, "ST title was never completed");
    return true;
  }

  bool token(std::size_t index) {
    const auto& item = items[index];
    const auto& view = item.token;
    if (skip_until != npos) {
      if (index <= skip_until) {
        if (is_bare(view)) {
          pending_space = false;
          ++trailing_bare;
          return assign(view, ProseTokenRoleIR::spacing);
        }
        return true;
      }
      skip_until = npos;
    }
    if (item.title_start) {
      in_title = true;
      out.title.clear();
    }
    if (item.index_start) {
      if (in_index) return fail(error, "nested SI control");
      in_index = true;
      current_term = {};
      term_refs.clear();
      const auto keyword = body_text(view);
      if (ascii_lower(keyword) != "si")
        return fail(error, "SI keyword mismatch");
      return assign(view, ProseTokenRoleIR::index_keyword);
    }

    if (is_bare(view)) {
      pending_space = false;
      ++trailing_bare;
      return assign(view, ProseTokenRoleIR::spacing);
    }

    if (is_space_run(view)) {
      const auto next = next_token(index);
      if (space_at(next)) {
        // Fill/origin pair: every run but the last is fill.
        auto cursor = index;
        auto last = next;
        while (space_at(next_token(last))) last = next_token(last);
        while (cursor != last) {
          if (!assign(items[cursor].token, ProseTokenRoleIR::fill)) return false;
          cursor = next_token(cursor);
        }
        const auto& origin = items[last].token;
        if (!assign(origin, ProseTokenRoleIR::origin)) return false;
        if (!finish_title() || !finish_index()) return false;
        open_line(origin.body.size(), &origin);
        // Skip the consumed runs (bare tokens between were handled as they
        // came; controls stay in sequence).
        skip_until = last;
        return true;
      }
      // Lone run: literal in-line gap (or trailing fill before a control).
      if (in_title) {
        out.title.push_back(' ');
        return assign(view, ProseTokenRoleIR::gap);
      }
      if (in_index) {
        current_term.term.push_back(' ');
        return assign(view, ProseTokenRoleIR::gap);
      }
      if (!line_open) {
        // Padding before the first line of the body: fill.
        return assign(view, ProseTokenRoleIR::fill);
      }
      // A lone run of three or more cells after a word and before the next
      // word is a row break without a marker byte: the word ends its row
      // and the next row keeps the block indent (SC31-711 LR57 `check` + 10
      // spaces + `the following` renders at indent 3; SC24-5520-00 LR51
      // `are` + 3 spaces + `discussed`).  The gap after a ballot token
      // (`__`) is display spacing inside the row.
      if (view.body.size() >= 3 && pending_space && line_visible_cells != 0 &&
          index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
          visible_at(next_token(index)) &&
          !is_placeholder_run(items[next_token(index)].token) &&
          !ballot_token(last_visible)) {
        if (!assign(view, ProseTokenRoleIR::fill)) return false;
        if (!finish_title() || !finish_index()) return false;
        open_line(implied_origin, nullptr);
        for (std::size_t cell = 0; cell < implied_origin; ++cell)
          line().cells.push_back({npos, 0, " ", true});
        line().text_begin = line().cells.size();
        return true;
      }
      if (pending_space) {
        line().cells.push_back({npos, 0, " ", true});
        pending_space = false;
      }
      append_space_cells(view, true);
      return assign(view, ProseTokenRoleIR::gap);
    }
    // Visible token.
    std::size_t origin_index = npos;
    const auto line_start_marker =
        line_open && line_visible_cells == 0 && view.width == 1 &&
        index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
        is_visible(items[index + 1].token) &&
        !is_placeholder_run(items[index + 1].token);
    if (is_placeholder_run(view) && !line_start_marker &&
        !marker_at(index, origin_index)) {
      // A placeholder run that does not open a row is layout padding: either
      // trailing before a control/segment end, the slot of a fill/origin
      // pair (whose runs are classified when they are reached), or the
      // marker glued between `ST` and the title (ACPZMST1 record 78).
      const auto next = next_token(index);
      // An inter-segment placeholder stands at a control boundary as well.
      const auto before_control =
          index + 1 >= items.size() ||
          items[index + 1].kind != ItemKind::token || item.separator;
      const auto title_marker = in_title && out.title.empty();
      if (next != npos && !space_at(next) && !before_control && !title_marker)
        return fail(error, "placeholder run '" + body_text(view) +
                               "' is followed by visible text at " +
                               where(view));
      if (title_marker) return assign(view, ProseTokenRoleIR::marker);
      if (in_title && !finish_title()) return false;
      if (in_index && !finish_index()) return false;
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      // The slot ends the current row even when its origin run is absent
      // (GC23-046 record 151: `for {9472}... [cfont] | the CIDTABL`).
      line_open = false;
      // A placeholder slot followed by one space run of any width opens a
      // row: GC23-046 record 151 `{9524} sp1 | ◆ The total number`.
      if (space_at(next) && !space_at(next_token(next)) &&
          visible_at(next_token(next))) {
        const auto& origin = items[next].token;
        if (!assign(origin, ProseTokenRoleIR::origin)) return false;
        open_line(origin.body.size(), &origin);
        skip_until = next;
      }
      return true;
    }
    // A glued alphabetic one-byte token in the row-control byte range is a
    // slot whatever follows it: N2AH1MST record 17 `to:` + `access` (0x1c)
    // directly before the `/` row marker of the next row.
    if (view.width == 1 && view.value < row_control_byte_limit &&
        (!pending_space || view.prefix == 0 || view.prefix == 1) &&
        !in_title && !in_index && line_open && line_visible_cells != 0 &&
        alpha_word(view) && !last_visible.empty() &&
        std::isalnum(static_cast<unsigned char>(last_visible.back())) == 0)
      return assign(view, ProseTokenRoleIR::marker);
    if (marker_at(index, origin_index)) {
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      for (auto cursor = next_token(index); cursor != origin_index;
           cursor = next_token(cursor))
        if (!assign(items[cursor].token, ProseTokenRoleIR::fill)) return false;
      const auto& origin = items[origin_index].token;
      if (!assign(origin, ProseTokenRoleIR::origin)) return false;
      if (!finish_title() || !finish_index()) return false;
      open_line(origin.body.size(), &origin);
      skip_until = origin_index;
      return true;
    }

    if ((in_title || in_index) && is_bullet_glyph(view))
      return assign(view, ProseTokenRoleIR::padding);
    if (in_title && out.title.empty() && view.width == 1 &&
        punctuation_glyph(view))
      return assign(view, ProseTokenRoleIR::marker);
    if (in_title) {
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        out.title.push_back(' ');
      out.title += body_text(view);
      last_visible = body_text(view);
      pending_space = view.prefix != 2;
      out.title_refs.emplace_back(view.record, view.token);
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::title);
    }
    if (in_index) {
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        current_term.term.push_back(' ');
      current_term.term += body_text(view);
      last_visible = body_text(view);
      pending_space = view.prefix != 2;
      term_refs.emplace_back(view.record, view.token);
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::index_term);
    }
    if (!line_open) open_line(0, nullptr);
    trailing_bare = 0;
    if (line_visible_cells == 0 && view.width == 1 &&
        (punctuation_glyph(view) || is_placeholder_run(view)) &&
        index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
        is_visible(items[index + 1].token) &&
        !is_placeholder_run(items[index + 1].token)) {
      // A visual row marker (`|`, box glyph) opening the row directly before
      // its text (GC23-046 record 151 `| ◆ The number of orders`, ACPZMST1
      // record 78 `│ The following sections`).
      return assign(view, ProseTokenRoleIR::marker);
    }
    if (line_visible_cells == 0 && is_glyph(view) && view.width == 1) {
      // A glyph opening the line is the list bullet.
      line().bullet = true;
      if (!assign(view, ProseTokenRoleIR::bullet)) return false;
      line().cells.push_back({view.record, view.token, word_text(view.body[0]),
                              false});
      ++line_visible_cells;
      pending_space = true;
      const auto next = next_token(index);
      if (space_at(next) && !space_at(next_token(next))) {
        line().cells.push_back({npos, 0, " ", true});
        pending_space = false;
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        skip_until = next;
      }
      line().text_begin = line().cells.size();
      return true;
    }
    if (is_glyph(view) || is_placeholder_run(view))
      return fail(error, "placeholder glyph '" + body_text(view) +
                             "' inside prose text at " + where(view));
    if (std::any_of(view.body.begin(), view.body.end(),
                    [](const auto word) { return word == unmapped_word; }))
      return fail(error, "unmapped word '" + body_text(view) +
                             "' inside prose text at " + where(view));
    // A `c.<xx>` word is a body control opcode, never visible text: the
    // `c.cp <n>` pagination form is modelled by the stream pass and hosted
    // BookServer serves no such word at all (checked on SH20-918 3.31.1,
    // DREICMST 1.7.7.3, SH12-565 1.1.2 and GG24-4302-00 8.1.5).  When the
    // decoder loses the control boundary the opcode stays glued to a text
    // run; fail the topic closed instead of printing the control.
    {
      const auto text = body_text(view);
      for (std::size_t at = text.find("c."); at != std::string::npos;
           at = text.find("c.", at + 1)) {
        if (at != 0 && text[at - 1] != ' ') continue;
        std::size_t end = at + 2;
        while (end < text.size() &&
               std::islower(static_cast<unsigned char>(text[end])) != 0)
          ++end;
        if (end - at < 4) continue;  // `c.` plus at least two opcode letters
        if (end != text.size() && text[end] != ' ') continue;
        return fail(error, "body control '" + text.substr(at, end - at) +
                               "' is glued into prose text at " + where(view));
      }
    }
    return append_visible(view, ProseTokenRoleIR::text);
  }

  std::size_t skip_until = npos;
};


bool build_lines(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Item>& items, Ledger& ledger,
                 LineBuild& out, std::string* error) {
  LineBuilder builder(records, items, ledger, out, error);
  return builder.run();
}

} // namespace geist::detail::prose_internal
