# M9 Heuristic Retirement Inventory (2026-08-27, at c82c0ed)

Read-only survey for issue #58 milestone M9 of libgeist/src code that still implements the string-level pipeline. Line numbers are as of commit c82c0ed and drift with later edits; function names are the durable keys.


# M9 retirement-candidate inventory — `libgeist/src`

## A. String-search admission prefilters (decide whether typed pipeline is even reached)

These are the "string ownership searches" and the first slice named in the handoff (`AnalysisNotes/decoder-first-layout-handoff.md:896-921`).

| Item | Location | Heuristic | Typed replacement | Tests |
|---|---|---|---|---|
| `has_box_form_source_candidate` | `libgeist/src/document.cpp:29-48` | lowercases the decoded record, finds `srtbl`/`sretbl`, counts a run of >=40 `?` chars to guess a box form | `extract_box_fixed_form_grid` / `LayoutIR` display runs | indirectly `libgeist/tests/topic_document_lowering.cpp`, `lazy_open_*.cpp` |
| `has_implicit_grid_source_candidate` | `document.cpp:50-75` | string-finds `"cfont "` then re-parses operand triples out of the raw string | `ControlIR` (`BookControlKind::font`) + `implicit_grid.cpp` | `libgeist/tests/implicit_grid.cpp` |
| `has_generated_toc_source_candidate` | `document.cpp:77-82` | `find("ctoce ")` in decoded string | `BookControlKind` typed segments | `generated_list_production.cpp` |
| `has_selector_source_candidate` | `document.cpp:84-88` | `find("cselect ")` | `BookControlKind::select`, `selector_ir.cpp` | `selector_display_ir_synthetic.cpp` |
| `has_comment_delivery_source_candidate` | `document.cpp:90-96` | `find("csourcefn rcfaddr")` literal | `comment_delivery_ir.cpp` | `comment_delivery_ir_synthetic.cpp` |
| `has_st_fixed_prose_source_candidate` | `document.cpp:98-113` | segment starts with `"st "`, then `size() < 160` and a literal 10-space run test | `fixed_prose_topic_ir.cpp` | `fixed_prose_topic_ir_synthetic.cpp` |
| `has_publication_ir_source_candidate` | `document.cpp:115-131` | needs literal `chdlevel :h2`/`:h3`, `st `, `cfont ` prefixes | `publication_ir.cpp` `extract_publication_catalog_ir` | `sc31_711_ir_publications_acceptance.cpp`, `publication_document_lowering_synthetic.cpp` |
| `has_menu_ir_source_candidate` | `document.cpp:133-139` | record must contain both `cmenu` and `cmitem` substrings | `menu_ir.cpp` / `menu_topic_ir.cpp` | `menu_topic_ir_synthetic.cpp`, `menu_production.cpp` |
| `has_glossary_ir_source_candidate` | `document.cpp:141-152` | `chdlevel :glossary` + `srgls` substrings | `glossary_catalog_ir.cpp` | `glossary_production.cpp`, `glossary_catalog_ir_synthetic.cpp` |
| `load_source_layout_if_candidate` | `document.cpp:154-172` | ORs all of the above into `TopicData::use_legacy_source_layout` (`geist/detail/internal.hpp:129`); if none match, the typed dispatcher never sees the topic | `try_lower_topic_to_document_ir` (`topic_document_lowering.cpp:92-268`) already fails closed on its own | `topic_document_lowering.cpp:123,148` assert legacy retention |
| `has_semantic_srmsg_source_candidate` | `libgeist/src/source_rows.cpp:180-211` (decl `geist/detail/source_rows.hpp:62`) | scans decoded string for `"srmsg "` + numeric/lowercase operand, plus any `"cfont "` | `MessageTopicIR` boundary detection (`message_topic_ir.cpp:632`) | used by `document.cpp:161,698`, `source_rows_synthetic.cpp` |
| `contains_srmsg_control` | `libgeist/src/toc.cpp:487-503` | duplicate `srmsg ` word-boundary string search, drives `GmlRenderState::current_record_has_message_catalog` (`markup.cpp:6215`) | `BookControlKind::message_start` | `markup_synthetic.cpp` |

## B. All-C / CFONT-as-semantics

- `libgeist/src/publication_ir.cpp:247-360` — publication recognizer requires `saw_title && font_segments != 0` and exact `font_runs.size() == font_segments`; the handoff (`decoder-first-layout-handoff.md:911-914`) explicitly calls this the surviving all-C admission constraint. Retiring it needs an ownership-envelope recognizer plus a new negative test, otherwise admission silently widens. Tests: `libgeist/tests/sc31_711_ir_publications_acceptance.cpp`.
- `libgeist/src/markup.cpp:3054-3110`, `4355-4460`, `4789+` — CFONT display-column arithmetic done on flattened strings (padding preservation, "column not covered by CFONT triples"). Covered typed-side by `layout_ir.cpp` display runs + `fixed_display.cpp`.
- `libgeist/src/document.cpp:50-75` (above) re-parses CFONT operands from text rather than `ControlIR`.

## C. Post-render / rendered-text repair

| Item | Location | Heuristic | Typed replacement | Tests |
|---|---|---|---|---|
| `strip_leaked_layout_controls` | `libgeist/src/markdown.cpp:296-411` (called 640, 660, 1141, 1753, 1811) | scans already-rendered text for `c.cc`, `cmenu`, `cmitem`, `cemenu`, `ctopicn`, `cparent`, `cforwardlevel`, `cbacklevel`, `csummary`, `chdlevel`, `csourcefn` and truncates; also erases literal `:h3`/`:h4` | `ControlIR` operand ranges — controls never reach text | `packet_markdown.cpp`, `qs3x36cm_markdown.cpp`, `sc31_605_markdown.cpp` | **Kept.** **Census (slice C, whole-corpus `boo2git` over 33 books + per-topic `boorender` over N2AH1MST, hit counters on the legacy route only; typed-route hits are unobservable):** changes text in 202 legacy-route topics (`c.cc` 76, `cfont` 57, `:h3`/`:h4` 32, `cmenu`/`cmitem`/`cemenu` 26, `picture` 11, `cforwardlevel` 8, `chdlevel` 4, `cparent` 3, `cbacklevel` 2, `ctopicn` 2, `csourcefn` 1). The `csummary` token and the `<image>`/`<other>`/`<internet>` placeholder erasures never fired and were deleted (corpus byte-identical). |
| `strip_inline_gml_markup` | `markdown.cpp:716-739` + `replace_all` 740-751 | find/replace passes over rendered text | `InlineIR` in `document_ir.cpp` | markdown fixture tests | **`strip_inline_gml_markup` kept** (every EDITION topic, 28 legacy-route). **`replace_all` deleted** together with its four literal call sites (`( May 1 991)`, `RPG/400, 400`, `( `, ` )`): 100 calls, zero matches anywhere in the corpus. |
| generated-menu example suppression | `markdown.cpp:1899-1912` | lookahead in `:xmp` block for `cmenu`/`cmitem` substrings to decide fence suppression | `MenuTopicIR` | `menu_production.cpp` | **Kept.** Fires in 18 legacy-route topics (GG24-4302-00 x13, DREICMST x4, packet 4.4). |
| `table_fallback_markdown` | `markdown.cpp:1439-1454`, used at 1502 | renders an unrecognized table as flat text | `TableBlockIR` | `qsysnewg_markdown.cpp` | **Kept.** 618 legacy-route calls (SC24-5527-02 156 topics, SC33-033 126, QSYSINFO 56, SC24-5520-00 28, GG24-395 20, ACPZMST1 13, ...). |
| `clean_fixed_st_row_markers` | `libgeist/src/toc.cpp:317-358` | hard-coded alpha marker list `{action,address,adapter,agent,are,can,an,as,a}` erased when attached to punctuation before a 4-space gap — exactly the literal cleanup the handoff rejects (`decoder-first-layout-handoff.md:38-44`) | Ownership marker slots (`ownership_ir.cpp`) | `toc`-driven fixtures, `lazy_open_sc31_711_topics.cpp` | **Spelling list replaced by typed evidence.** The alphabetic branch fired once observably (SC31-711 4.1.2, `actions.can    The traps`); the glyph branch fires in SC31-711 4.1.1/4.3.1/4.3.2 (`<`), 4.3.4/4.3.5 (`(`, `)`). Both surviving word erasures (4.1.2 `can`, LR101 marker bytes 0x10950 marker_value 44; IBMMMSTR FRONT_1 `an`, LR6 bytes 0x1079c marker_value 23) are origin-3 `LayoutIR` marker slots whose row `visible_text` follows the slot padding, so `libgeist/src/structural_marker_rows.cpp` (`StructuralMarkerRowEvidence`) now proves a word from the verified layout; `fixed_st_alpha_row_marker_length_at` and both `kAlphaMarkers` arrays are gone. The legacy geometry (attached to `.`/`)`/`:`, four-space padding) is retained for zero delta, so the other origin-3 slot words in IBMMMSTR FRONT_1 (`a`, `are`, `be`) still leak exactly as before; that is a fixed-prose lowering residual, not a cleanup residual. Corpus byte-identical. |
| `clean_fixed_rendered_line` | `toc.cpp:360-431` | post-render line cleanup | `LayoutIR` rows | as above | **Retired** with the publication repairs below (it had no other caller); the title-keyed trim it applied to `"publications"` fixed bodies is gone, so those `:xmp.` lines keep their source indentation like every other fixed body. |
| `looks_like_publication_catalog_row` / `start_publication_block` | `toc.cpp:433-486` | literal document-number markers `(sc,(gc,(ga,(sg,(sh,(sa,(sx,(zz,(isbn` inside rendered text to detect a publication row | `publication_ir.cpp` `PublicationCatalogIR` | `sc31_711_ir_publications_acceptance.cpp` | **Retired.** `extract_publication_catalog_ir` now admits deferred entry origins at record boundaries, title-only envelopes, and a shared entry margin column from `FontSpanIR` (SC31-711 BACK_1.3, SC31-605 BIBLIOGRAPHY.9/.5, ITPPIBOK BIBLIOGRAPHY.2, SG24-204 D.3); SH20-918 D.3.1 and QSYSINFO APPENDIX1.4.3 were never catalogs (table / ordering prose) and now render un-repaired. |
| `normalize_message_catalog_intro` | `toc.cpp:265-315` | trims trailing `) ( / < >` and strips a leading literal `cfont ` from an already-built title | `MessageTopicIR` introduction | `message_production.cpp` | **Kept.** 10 legacy-route topics (SC31-711 2.4.9, 4.1.1, 4.1.2, 4.1.3, 4.3.1, 4.3.2, 4.3.4, 4.3.5; GX27-3999-00 B.0; SC09-138 F.1). The visual-marker gap branch and the leading `cfont ` strip never fired and were deleted. |
| `strip_leading_visual_bar`, `has_reflow_off_line_markers`, `is_fixed_st_row_marker`, `fixed_st_alpha_row_marker_length_at`, `preserve_reflow_off_st_body_lines`, `render_st_form_items`, `clean_glossary_intro_fixed_line` | `toc.cpp:252-263, 550-602, 603-622, 798-860, 906-993, 1143-1163` | glyph/whitespace marker inference on rendered lines | `LayoutIR` marker slots + `fixed_prose_ir.cpp`, `glossary_ir.cpp` | `glossary_production.cpp`, `fixed_prose_topic_ir_synthetic.cpp` | **Kept** (load-bearing for the whole legacy fixed-body route: `has_reflow_off_line_markers` 431 legacy-route topics in 31 books, glyph split 361, `strip_leading_visual_bar` 119, `preserve_reflow_off_st_body_lines` 5, `render_st_form_items` SC31-711 2.4.5-2.4.9, `clean_glossary_intro_fixed_line` QSYSNEWG GLOSSARY only). Dead branches deleted: the leading-glyph pop in `render_st_form_items`, the short-gap prefix erase in `clean_glossary_intro_fixed_line`. |
| cselect intro punctuation trimming | `toc.cpp:1424-1470` | pops `?`/punctuation off a `cselect` intro, re-injects `:p.` records, then string-searches `"cfont "`/`"cselect "` | `SelectorCatalogIR` (`selector_ir.cpp`) | `sc31_711_cross_references.cpp` | **Deleted.** 102 calls, never changed the string (`trim_ascii` had already removed the whitespace and no `cselect` intro ends in `?`). `preserve_reflow_off_st_body_lines` still runs on `:`-terminated intros (FA1PLMM0 I.3, GG24-395 PREFACE.3, OFCUSEOV PREFACE.4, QS3X36CM 2.0, SH12-565 5.1.3.1). |
| legacy title-marker glyphs | `toc.cpp:870-886` (`is_legacy_title_marker`) | per-char marker guess in ST titles | ownership marker slots | `lazy_open_*` | **Kept.** Fires exclusively (single-space form, not the padded fixed-row form) in 16 legacy-route topics: FA1PLMM0 16.10.5, 5.1.7.1; N2AH1MST 1.2.3, FRONT_1; QSYSINFO APPENDIX1.4.2.9; SC24-5520-00 2.4.11.2, 2.4.16.2, 2.4.7.2, 5.10.4; SC31-711 2.4.8, 3.1, 4.1.1, 4.1.2, 4.1.3, 4.2.2, 4.3.5. |

## D. Dual ownership paths (same rows claimed twice)

These build full Layout+Ownership IR and then *edit the legacy rendered GML string array* — both paths claim the same rows.

- `render_verified_glossary_gml` — `libgeist/src/markup.cpp:5629-5674`. Verifies `LayoutIR`/`OwnershipIR`/`GlossaryIntroductionIR`, then splices typed output into `render_gml_records_with_source_layout` output at a `:anchor id='GLS` scan point. Duplicate of `glossary_document_lowering.cpp`. Decl: `geist/detail/internal.hpp:200`. Tests: `glossary_production.cpp`.
- `project_verified_menu_gml` — `markup.cpp:5676-5703` (decl `internal.hpp:203`). Verifies `MenuIR` then locates `:li refid=` lines in rendered GML and rewrites them; bails if counts differ. Duplicate of `menu_document_lowering.cpp` / `menu_topic_ir.cpp`. Tests: `menu_production.cpp`, `boo2git_menu_links.cmake`.
- `project_verified_message_sections_gml` — `markup.cpp:5704-~5795` (decl `internal.hpp:207`). Verifies `MessageCatalogIR` then string-matches `:anchor id='MSG ` lines and injects `:hp2.`/`:ehp2.` around labels found by case-insensitive substring search. Duplicate of `message_document_lowering.cpp`. Tests: `message_production.cpp`, `boo2git_message_links.cmake`.
- `project_semantic_srmsg_source_markers` — `source_rows.cpp:214-256`; mutates `rendered` in place after gating on the SRMSG string search. Called `markup.cpp:5817`.
- `clean_source_owned_toc_title_markers` (`source_rows.cpp:258-314`), `project_source_owned_st_prose_rows` (`source_rows.cpp:315-353`), `clean_source_owned_selector_display_markers` (`source_segments.cpp:73-120`, string-prefix `"cselect"` at `:37,:44,:98`) — all called in sequence at `markup.cpp:5799-5818`, each rewriting decoded records before legacy rendering.
- Legacy re-render retry: `markup.cpp:5809-5814` — if the typed-ish projection yields nothing, it recomputes procedure steps and re-renders from the uncleaned records.
- Box-form replacement: `markup.cpp:5819-6220` — builds a `:table` and then `rendered.erase(...)`/`insert(...)` over previously rendered lines (`markup.cpp:6196-6218`), explicitly commented as suppressing "the flattened CFONT tail".

### Legacy-only routing

`TocEntry::markdown()` (`markdown.cpp:91-207`) tries `document_ir_loader_` and, on `nullopt`, falls back to legacy GML records + `lower_legacy_topic_to_document_ir` (`document_lowering.cpp:7-23`), which wraps the whole topic in a single `LegacyGmlRegionIR` (`geist/detail/document_ir.hpp:168-185`, rendered at `document_markdown_renderer.cpp:439-456`). `BooDocument::markdown()` (`markdown.cpp:208-210`) is legacy-only unconditionally.

Typed families implemented (`topic_document_lowering.cpp:106-256`): comment delivery, publication catalog, fixed prose, glossary, message catalog, generated list, menu — and `family_count != 1` rejects to legacy (`:152`). Everything else is **legacy-only**: ordinary prose topics, TOC/contents topics, tables and box forms, implicit grids, numbered procedures, selector/cross-reference topics, figures, footnotes, title/edition pages, index topics, message *structured* tables/lists (`message_section_blocks_ir.cpp:323-381` `application_table_fallback`, `provenance_complete=false`).

## E. TODOs naming the retirement (all in `message_ir.cpp`)

All five keep an "observed compact alphabet" of encoded width 1, value 19..43 until `OwnershipIR` grows positioned/supplemental field roles:

- `message_ir.cpp:471-477` — non-row terminal field, `width==1 && value in [19,43]` plus delimiter balancing.
- `message_ir.cpp:748-753` — opaque non-row fragments, same envelope.
- `message_ir.cpp:776-778` — opaque prefix, same envelope.
- `message_ir.cpp:925-930` — marker slots are geometry-only; keeps compact alphabet + indentation as "series evidence" (MSG2267, MSG739/MSG2108 value-34 `and` collision).
- `message_ir.cpp:998-1002` — encoded_value==4 decoder sentinel kept to tell soft wrap from punctuation.
- `message_ir.cpp:1647-1650` — record-prefix fields, same envelope.

Neighbour: `message_section_blocks_ir.cpp:367-381`, `:466-469` — `fallback_reason` / `provenance_complete=false` preformatted escape hatch; `geist/detail/message_section_blocks_ir.hpp:63`. Tests: `message_section_blocks_ir_synthetic.cpp`, `message_topic_ir_synthetic.cpp`, `message_production.cpp`.

## F. Every `.cpp` in `libgeist/src` by line count

**Typed-IR pipeline (Token/Control/Layout/Ownership -> family IR -> Document IR -> Markdown) — 20 files, 9,269 lines**

`document_lowering.cpp` 24, `book_ir.cpp` 84, `book_topic_catalog_ir.cpp` 143, `generated_list_document_lowering.cpp` 129, `fixed_prose_document_lowering.cpp` 156, `menu_document_lowering.cpp` 214, `fixed_prose_topic_ir.cpp` 223, `publication_document_lowering.cpp` 252, `topic_document_lowering.cpp` 269, `glossary_ir.cpp` 280, `selector_ir.cpp` 291, `control_ir.cpp` 328, `menu_ir.cpp` 332, `fixed_prose_ir.cpp` 342, `ownership_ir.cpp` 352, `glossary_document_lowering.cpp` 362, `layout_ir.cpp` 374, `message_document_lowering.cpp` 453, `document_ir.cpp` 469, `document_markdown_renderer.cpp` 469, `comment_delivery_document_lowering.cpp` 515, `menu_topic_ir.cpp` 539, `message_section_blocks_ir.cpp` 584, `publication_ir.cpp` 610, `generated_list_topic_ir.cpp` 687, `comment_delivery_ir.cpp` 834, `selector_display_ir.cpp` 958, `glossary_catalog_ir.cpp` 1080, `message_topic_ir.cpp` 1147, `message_ir.cpp` 2017.

**Legacy string pipeline — 5 files, 11,143 lines**

`markup.cpp` 6347, `markdown.cpp` 2239, `toc.cpp` 1685, `document.cpp` 804 (prefilters + trace), `document_lowering.cpp` 24 (legacy adapter; counted above too).

**Shared / boundary (string-derived source projections feeding both) — 6 files, 1,504 lines**

`source_segments.cpp` 168, `procedure_rows.cpp` 211, `fixed_display.cpp` 327, `source_rows.cpp` 353, `implicit_grid.cpp` 369, `page_runs.cpp` 76.

**Other (I/O, encoding, images) — 12 files, ~2,700 lines**

`properties.cpp` 51, `util.cpp` 64, `io.cpp` 85, `ebcdic.cpp` 109, `strings.cpp` 245, `resources.cpp` 254, `img/mmr_tables.cpp` 159, `img/png.cpp` 222, `img/gdf_raster.cpp` 407, `img/mmr.cpp` 518, `img/gdf.cpp` 744.

Total: 30,639 lines.

## M8 Markdown-emission audit

Acceptance item: *decoder/layout code emits no Markdown syntax*. Every emission of Markdown/HTML-in-Markdown syntax in `libgeist/src` outside `document_markdown_renderer.cpp` and `markdown.cpp` (searched: backslash escapes, `**`, `*`, `[...](`, `#` headings, fences, `<a id=`, `|` table pipes, `- ` list markers, `<br>`, `<pre>`). None of these reach the typed route; each is a legacy GML-projection artefact that the typed Document IR already models.

| Location | Emission | Route | Typed structure that already covers it |
|---|---|---|---|
| `libgeist/src/markup.cpp:2431`, `:2550`, `:4743` | `<br>` joining continuation lines inside a pending `:row.` table cell | legacy GML | `TableCellIR.content` is an `InlineSequenceIR`; line structure is `HardBreakInlineIR` |
| `libgeist/src/markup.cpp:3783-3789` | `<br>\n` between physical lines of a reflow-off paragraph (`append_line_break`) | legacy GML | `HardBreakInlineIR` inside `ParagraphBlockIR`, or `PreformattedBlockIR.lines` |
| `libgeist/src/implicit_grid.cpp:274` | `<br>` joining continuation values in a discovered key/value grid | legacy GML (shared/boundary) | `TableCellIR` + `HardBreakInlineIR` |
| `libgeist/src/fixed_display.cpp:194` | `<br>` joining wrapped cells of a fixed form grid | legacy GML (shared/boundary) | `TableCellIR` + `HardBreakInlineIR` |
| `libgeist/src/fixed_display.cpp:55` | `\_` Markdown escape substituted for the U+2500 rule glyph | legacy GML (shared/boundary) | `TextInlineIR` text is escaped once by `escape_markdown_text` (`document_markdown_renderer.cpp:50`); the glyph itself belongs in the text |
| `libgeist/src/toc.cpp:706` | `<br>\n` between preserved reflow-off `ST` body lines (`preserve_reflow_off_st_body_lines`) | legacy GML | `HardBreakInlineIR` |
| `libgeist/src/toc.cpp:558`, `:560` | `\| ` / `\| ° ` visual rail prefix on `:xline.` rows (inside a fenced block, so not Markdown table syntax, but a rendered glyph decision made in layout code) | legacy GML | `PreformattedBlockIR.lines` / `ListBlockIR` |

Excluded on inspection: `document_ir.cpp:224-231` are C-style escapes of the IR dump formatter (`quoted`), not Markdown; `markup.cpp` `escape_gml_attr` escapes GML attributes; `\xC2\xB0` list bullets in `toc.cpp` are content glyphs. No `**`, `*`, `[...](`, `#`, fence, `<a id=`, or `- ` emission exists outside the two renderer files. All seven rows retire with the legacy GML route (`LegacyGmlRegionIR`), not by individual fixes.

## Suggested ordering (matches `decoder-first-layout-handoff.md:917-921`)

1. Delete section A prefilters; let `try_lower_topic_to_document_ir` fail closed. Merge `fixed_layout_sources`/`typed_sources` into one lazy cache (`document.cpp:200-215`).
2. Refactor `publication_ir.cpp:247-360` off the font-segment constraint, with a new negative test, *before* deleting it.
3. Delete section C publication repairs in `toc.cpp:317-486` once branch-hit audits show unreachable.
4. Remove the three `project_verified_*`/`render_verified_*` projectors (`markup.cpp:5629-5795`) after menu/message production migration.
5. Only then retire `LegacyGmlRegionIR` — blocked while `TocEntry::gml_records()` is public and `document_ir_synthetic.cpp:46-73` requires byte-identical legacy output.

No files were modified.
