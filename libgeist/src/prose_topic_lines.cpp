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
  // CZ dialect: the stream carries layout items; every display line belongs
  // to the most recent directive.
  bool cz_mode = false;
  // Inside `cz OFF XMP` .. `cz OFF EXMP`: the rows are verbatim example
  // text, not reflowed prose, so no visible token is a row-control slot.
  bool xmp_mode = false;
  std::size_t current_directive = npos;

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
    fresh.directive = current_directive;
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

  // A CFONT/CSELECT operand addresses display columns of one display row, so
  // a row that already carries a span reaching past its current cell count
  // has not ended: the wide space run in front of the next word is in-row
  // spacing, not a markerless row break.  Two-column definition rows are the
  // shape that proves it: ACPZMST1 3.6 stores `cselect 43 3 SPTUSERID` and
  // `cfont 3 6 2` for the single hosted row
  // `   Userid                   User ID (topic 4.3)`, and DREICMST 2.8.1
  // stores `cfont 3 3 2 7 4 2 17 3 2 21 4 2` for the single hosted row
  // `   RFT Name      Log Type`.
  bool span_continues_row() const {
    if (!line_open || out.lines.empty()) return false;
    const auto& current = out.lines.back();
    const auto reaches = [&](const std::vector<Span>& spans) {
      for (const auto& span : spans)
        if (span.end > current.cells.size()) return true;
      return false;
    };
    return reaches(current.fonts) || reaches(current.links);
  }

  // True when a control that is still waiting for its display text covers
  // exactly the `cells` display columns starting at `column`.  An exact
  // match is what proves a glyph is styled display text; a span that merely
  // starts there could still be a span over the row's first word.
  bool pending_span_covers(std::size_t column, std::size_t cells) const {
    for (const auto control : pending_controls) {
      const auto& item = items[control];
      if (item.kind == ItemKind::font) {
        for (const auto& span : item.spans)
          if (span.column == column && span.length == cells) return true;
      } else if (item.column == column && item.length == cells) {
        return true;
      }
    }
    return false;
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

  // Display column the next visible token would occupy on the open row.
  std::size_t next_column(const TokenView& view) const {
    if (!line_open || out.lines.empty()) return npos;
    const auto extra =
        pending_space && view.prefix != 0 && view.prefix != 1 ? 1u : 0u;
    return out.lines.back().cells.size() + extra;
  }

  // True when the open row already carries a CFONT span over the column the
  // token would occupy.
  bool covered_by_font_span(const TokenView& view) const {
    const auto column = next_column(view);
    if (column == npos) return false;
    for (const auto& span : out.lines.back().fonts)
      if (column >= span.begin && column < span.end) return true;
    return false;
  }

  // Widest display row completed so far.  CZ rows are justified to one
  // width, so the widest finished row is the row width of the topic.
  std::size_t widest_row() const {
    std::size_t widest = 0;
    for (std::size_t seen = 0; seen + 1 < out.lines.size(); ++seen)
      widest = std::max(widest, out.lines[seen].cells.size());
    return widest;
  }

  // True when the space run at `space_index`, preceded by `extra_cells` of
  // visible text still to be appended to the open row, keeps the following
  // word inside the topic's row width.  Such a run is an in-row
  // justification gap rather than an implied row break: packet 1.1 keeps
  // `PRNET,   and   SATNET   (a   satellite ...` on one 77-cell row.
  bool run_fits_row(std::size_t extra_cells, std::size_t space_index) const {
    if (!cz_mode || !line_open || out.lines.empty()) return false;
    const auto widest = widest_row();
    if (widest < 40) return false;
    const auto after = next_token(space_index);
    if (!visible_at(after)) return false;
    const auto width = out.lines.back().cells.size() +
                       (pending_space ? 1u : 0u) + extra_cells +
                       items[space_index].token.body.size() +
                       items[after].token.body.size();
    return width <= widest;
  }

  bool marker_at(std::size_t index, std::size_t& origin_index) const {
    const auto& view = items[index].token;
    if (view.width != 1 || !is_visible(view)) return false;
    // Example blocks style every displayed word with the block's `CFONT`
    // spans (hosted `<samp>...</samp>` per word), so a one-byte token that
    // falls inside a span of the open row is display text and not a row
    // slot: SC09-2417-00 4.5.2.2 `void payroll::calc (employee *pe) {`
    // covers the trailing `{`, while the `;` that ends the next row is
    // covered by no span and stays the slot.
    if (xmp_mode && covered_by_font_span(view)) return false;
    // CZ dialect: a compact one-byte token is a whole dictionary word
    // (`and`, `a`, `protocol`) displayed wherever it stands, and the rows are
    // justified, so a space run behind it is an in-row gap or a plain row
    // break (packet 1.1 `PRNET,   and   SATNET`, packet 3.2 `... to  send
    // and` + 10 spaces + `receive`).  Only glyphs and placeholder slots mark
    // rows here.  Residual: packet 3.2 record 84 `NET/ROM` + `an` + fill +
    // origin, where hosted drops `an`; no positioned distinction separates it
    // from the cases above yet.
    if (cz_mode && alnum_word(view)) return false;
    const auto space = next_token(index);
    if (!space_at(space)) return false;
    if (space_at(next_token(space))) {
      // Fill/origin pair: a standalone glyph is a marker here, and so is a
      // one-byte alphanumeric piece glued (no space) onto a preceding word:
      // genuine text never joins two alphanumeric pieces without
      // punctuation (FA1PLMM0 record 1133 `Messages` + `access`).
      const auto attached =
          !pending_space || view.prefix == 0 || view.prefix == 1;
      // The first visible token of a freshly opened row stands at the row
      // origin, so it is glued to nothing even though no pending space
      // separates it: ACPZMST1 6.2 record 305 stores `cfont 4 4 R,` + a
      // four-cell origin run + the one-byte word `GUPI` + a fill/origin
      // pair, and hosted serves that row as `    <B>GUPI</B>`.
      const auto row_origin_word = line_open && line_visible_cells == 0;
      // A glued one-byte word in the row-control byte range is the slot
      // whatever precedes it: N2AH1MST PREFACE.4 `to:` + `access` (0x1c),
      // `Reference.` + `an` (the compact-marker collision in Format/markup.md).
      const auto glued_word =
          attached && !row_origin_word && alnum_word(view) &&
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
    // CZ dialect: a one-byte token glued to the word before it (SC09-2417-00
    // 3.1.7 `QXXITOP(` + `)` before the next row's origin run) ends an
    // exactly full row as text; only a free-standing glyph is a slot.
    if (cz_mode && line_open && line_visible_cells != 0 &&
        (view.prefix == 0 || view.prefix == 1 ||
         (!pending_space && !out.lines.back().cells.empty() &&
          !out.lines.back().cells.back().space)))
      return false;
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
    // Exception: the following token is itself a marker candidate.  In the
    // CZ dialect a one-byte word after the origin run is text (justified
    // rows: packet 2.2 `-` + 3 spaces + `a` + 3 spaces + `network`).
    const auto& following = items[after].token;
    if (following.width == 1 && !is_bullet_glyph(following) &&
        !(cz_mode && alnum_word(following))) {
      const auto space2 = next_token(after);
      if (space_at(space2) && run_length(space2) >= 3) {
        const auto after2 = next_token(space2);
        if (visible_at(after2) && !space_at(after2)) return false;
      }
    }
    origin_index = space;
    return true;
  }

  // Every compiled `cz FLOW FN` body ends with a row-terminator `.` token
  // that hosted does not print: packet 3.2 record 85 `... start with ax so,
  // ax0..` renders `ax0.`, record 86 `... connections)!.` renders
  // `connections)!`, and packet 1.1 record 17 `technique..` renders
  // `technique.`.  The terminator is always a standalone one-cell `.`
  // token, so a body that ends any other way is not modelled.
  bool trim_footnote_terminator() {
    if (out.lines.empty() || out.lines.back().directive != current_directive)
      return fail(error, "cz FLOW FN body has no display row");
    auto& cells = out.lines.back().cells;
    if (cells.size() < 2 || cells.back().space ||
        cells.back().record == npos || cells.back().text != ".")
      return fail(error, "cz FLOW FN body does not end with a row terminator");
    const auto& terminator = cells.back();
    const auto& previous = cells[cells.size() - 2];
    if (previous.record == terminator.record &&
        previous.token == terminator.token)
      return fail(error,
                  "cz FLOW FN row terminator is glued to the last word");
    auto& entry = ledger.at(terminator.record, terminator.token);
    if (entry.role != ProseTokenRoleIR::text)
      return fail(error, "cz FLOW FN row terminator is not display text");
    entry.role = ProseTokenRoleIR::marker;
    cells.pop_back();
    return true;
  }

  bool run() {
    cz_mode = std::any_of(items.begin(), items.end(), [](const auto& item) {
      return item.kind == ItemKind::layout;
    });
    for (std::size_t index = 0; index < items.size(); ++index) {
      const auto& item = items[index];
      switch (item.kind) {
      case ItemKind::segment_end:
        if (!finish_title() || !finish_index()) return false;
        break;
      case ItemKind::layout: {
        if (!finish_title() || !finish_index()) return false;
        if (item.directive.mode == "off" && item.directive.tag == "fn" &&
            !trim_footnote_terminator())
          return false;
        if (item.directive.mode == "off") {
          if (item.directive.tag == "xmp") xmp_mode = true;
          else if (item.directive.tag == "exmp") xmp_mode = false;
        }
        out.directives.push_back(item.directive);
        current_directive = out.directives.size() - 1;
        // A row opened by the previous directive's trailing slot and a lone
        // origin run that carries no text yet belongs to this directive.
        if (line_open && line_visible_cells == 0 && !out.lines.empty() &&
            out.lines.back().cells.size() <= out.lines.back().text_begin &&
            !out.lines.back().bullet) {
          out.lines.back().directive = current_directive;
        } else {
          line_open = false;
        }
        pending_space = false;
        break;
      }
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
        // A CFONT/CSELECT whose display text lies inside the span styles the
        // block's own content; the block models it and owns those tokens.
        pending_controls.clear();
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
      // CZ dialect rows are justified to one width (packet 1.1: 77 cells
      // `PRNET,   and   SATNET   (a   satellite ...`); a run that keeps the
      // next word inside the widest row seen so far is an in-row gap.
      const auto fits_row = [&]() { return run_fits_row(0, index); };
      if (view.body.size() >= 3 && pending_space && line_visible_cells != 0 &&
          !span_continues_row() &&
          index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
          visible_at(next_token(index)) &&
          !is_placeholder_run(items[next_token(index)].token) &&
          !ballot_token(last_visible) && !fits_row()) {
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
    if (cz_mode && line_open && line_visible_cells == 0 && !in_title &&
        !in_index && view.width == 1 && view.body.size() == 1) {
      const auto next = next_token(index);
      const auto after = space_at(next) ? next_token(next) : npos;
      const auto text_follows = visible_at(after) && !space_at(after);
      // A one-cell slot after the origin run and before a one- or two-cell
      // gap is the bullet of a `cz FLOW LI` row (SC09-2417-00 2.1 record
      // 137 `<` + 3 spaces + slot + 2 spaces + `"Introducing`; hosted
      // `<li>`).  It counts one display cell plus the synthetic space, which
      // keeps CFONT/CSELECT columns aligned (`cselect 7 55` on LI 3 7).
      // The slot decodes as a placeholder word or as the literal decoder
      // separator `?` (SC09-2417-00 4.2 record 894); neither is text.
      const auto slot = is_placeholder_run(view) ||
                        (item.separator && (view.body.front() == '?' ||
                                            is_bullet_glyph(view)));
      if (slot && text_follows && run_length(next) <= 2) {
        line().bullet = true;
        if (!assign(view, ProseTokenRoleIR::bullet)) return false;
        line().cells.push_back({view.record, view.token, "?", false});
        line().cells.push_back({npos, 0, " ", true});
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        ++line_visible_cells;
        pending_space = false;
        skip_until = next;
        line().text_begin = line().cells.size();
        return true;
      }
      // A change bar opening the row before its gap (SC41-485 1.2.2 record
      // 52 `|` + 4 spaces + `Object name` on DT 7 16, hosted `| Object
      // name`): one display cell in the margin, never text.  The bar's
      // encoded value is above the row-control range, unlike marker slots.
      if (view.body.front() == '|' && view.value >= row_control_byte_limit &&
          space_at(next) && text_follows) {
        if (!assign(view, ProseTokenRoleIR::marker)) return false;
        line().cells.push_back({view.record, view.token, "|", false});
        line().cells.push_back({npos, 0, " ", true});
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        pending_space = false;
        skip_until = next;
        line().text_begin = line().cells.size();
        return true;
      }
    }
    if (cz_mode && item.separator && !is_placeholder_run(view) &&
        (is_separator(view) || is_bullet_glyph(view)) &&
        view.body.size() == 1) {
      // Unclaimed one-cell decoder separators of the CZ dialect.  A `,`
      // glued (attach prefix) to the word before it is text the decoder split
      // off (SC09-2417-00 3.1.7 `QXXITOP(),`; hosted prints it).  A `?`
      // before a lone space run and text is the slot that closes the row and
      // the run is the next row's origin (SC41-485 1.1 record 6
      // `configuration` + `?` + 7 spaces + `descriptions`).  Anything else is
      // padding.
      if (view.body.front() == ',' && line_open && line_visible_cells != 0 &&
          (view.prefix == 0 || view.prefix == 1))
        return append_visible(view, ProseTokenRoleIR::text);
      // A `?` stored as a dictionary word (width 2) is a question mark
      // (packet 1.1 record 15 `network` + bare + `?`); the one-byte `?` is
      // the row slot.
      if (view.body.front() == '?' && view.width == 2 && line_open &&
          line_visible_cells != 0)
        return append_visible(view, ProseTokenRoleIR::text);
      if (view.body.front() == '?' || is_bullet_glyph(view)) {
        const auto next = next_token(index);
        if (space_at(next) && !space_at(next_token(next)) &&
            visible_at(next_token(next))) {
          if (!finish_title() || !finish_index()) return false;
          if (!assign(view, ProseTokenRoleIR::marker)) return false;
          const auto& origin = items[next].token;
          if (!assign(origin, ProseTokenRoleIR::origin)) return false;
          open_line(origin.body.size(), &origin);
          skip_until = next;
          return true;
        }
      }
      return assign(view, ProseTokenRoleIR::padding);
    }
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
    // A glued alphabetic one-byte token in the row-control byte range is the
    // display-line length byte of the next row: N2AH1MST record 17 `to:` +
    // `access` (0x1c) directly before the `/` row marker of the next row.
    // The byte stands at a row boundary, so a plain word glued behind it on
    // the same row is display text: SC26-457 3.14.2.8 record 560 token 113
    // `(` + `and` + `their` is served as `(and their associated entries)`.
    if (!cz_mode && view.width == 1 && view.value < row_control_byte_limit &&
        (!pending_space || view.prefix == 0 || view.prefix == 1) &&
        !in_title && !in_index && line_open && line_visible_cells != 0 &&
        alpha_word(view) && !last_visible.empty() &&
        std::isalnum(static_cast<unsigned char>(last_visible.back())) == 0) {
      const auto after = next_token(index);
      const auto row_boundary =
          after == npos || space_at(after) ||
          (items[after].token.width == 1 &&
           (punctuation_glyph(items[after].token) ||
            is_placeholder_run(items[after].token)));
      if (row_boundary) return assign(view, ProseTokenRoleIR::marker);
    }
    // A pending span that opens on this token's own display column proves it
    // is styled display text rather than the row's marker slot: GC28-183
    // 2.2.3 `cfont 5 2 E 15 4 E` over `     //        PEND` is served as
    // `<samp>//</samp>        <samp>PEND</samp>`.
    // CZ rows carry their marker slots explicitly, so the geometry exemption
    // is limited to the flowed dialect (SC09-2417-00 2.1.3.4 `++` before a
    // `cz flow nt` label stays a slot).
    const auto styled_at_column =
        line_open && !cz_mode && !is_placeholder_run(view) &&
        line_visible_cells == 0 &&
        pending_span_covers(line().cells.size(), view.body.size());
    if (!styled_at_column && marker_at(index, origin_index)) {
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
        // CZ rows carry their markers as explicit slots (bullet, change bar,
        // `?`), so an ordinary punctuation glyph opening a row is display
        // text there: packet 3.2 record 80 token 255 `#` before `name` is
        // styled by `cfont 5 1 E` and hosted prints `<samp>#</samp>`.
        (!cz_mode || is_placeholder_run(view) || view.body.front() == '|') &&
        // A pending CFONT/CSELECT span that opens on the glyph's own display
        // column proves the glyph is styled display text, not a row marker:
        // FA1PLMM0 3.5.1 `cfont 5 2 E 8 3 E ...` over `     // JOB COPY ...`
        // and GC28-183 2.2.3 `cfont 5 2 E 15 4 E` over `     //        PEND`
        // are served as `<samp>//</samp> <samp>JOB</samp> ...` and
        // `<samp>//</samp>        <samp>PEND</samp>`.
        // Decoder placeholder runs stay markers whatever the geometry says
        // (ACPZMST1 3.11 record 180): they carry no character.
        (is_placeholder_run(view) ||
         !pending_span_covers(line().cells.size(), view.body.size())) &&
        index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
        is_visible(items[index + 1].token) &&
        !is_placeholder_run(items[index + 1].token)) {
      // A visual row marker (`|`, box glyph) opening the row directly before
      // its text (GC23-046 record 151 `| ◆ The number of orders`, ACPZMST1
      // record 78 `│ The following sections`).  The glyph is not prose text,
      // but hosted BookServer prints it in its own display column
      // (ACPZMST1 1.2.3.1 ` | A local resource ...`, GG24-395 PREFACE.3
      // ` | Part 1, "Introduction"`), so the row keeps a blank cell for it
      // and for the spacing that follows.  Dropping those cells shifted
      // every CFONT/CSELECT column of the row by two.
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      for (std::size_t word = 0; word < view.body.size(); ++word)
        line().cells.push_back({npos, 0, " ", true});
      line().text_begin = line().cells.size();
      pending_space = view.prefix != 2;
      return true;
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
