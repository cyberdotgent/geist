#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

"""Corpus gate policy tests; no private books or build tools required."""

import copy
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "corpus_gate", Path(__file__).resolve().parents[1] / "tools" / "corpus_gate.py")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

COVERAGE = (
    "id\tlevel\troute\tfamily\treason\tclass\tsignature\tseverity\tdegraded\tdeclined\t"
    "source_kind\tsource_index\tsource_start\tsource_end\n"
    "1.0\t1\ttyped\tprose\t\tprose\tprose\ttyped\t\t\ttopic-index\t0\t2\t4\n"
    "1.1\t2\tlegacy\t\tno proof\tprose\tprose\tbest-effort\t\tno proof\t"
    "topic-index\t1\t4\t7\n"
    "# summary\ttyped=1\tlegacy=1\ttotal=2\n"
    "# severity\tbest-effort=1\ttyped=1\n")
CONSERVATION = (
    "topic\troute\tword\tdrawn_opening\tdrawn_inline\temitted\tunaccounted\tevidence\t"
    "emitted_markdown\tunaccounted_markdown\temitted_html\tunaccounted_html\n"
    "# topic\t1.0\ttopic-index\t0\t2\t4\ttyped\n"
    "# topic\t1.1\ttopic-index\t1\t4\t7\tbest-effort\n"
    "1.0\ttyped\talpha\t1\t3\t1\t2\trecord 1 line 2\\tproof\t1\t2\t2\t1\n"
    "1.1\tbest-effort\tsi\t1\t0\t0\t0\topening control\t0\t0\t0\t0\n"
    "# unframed\t1.1\t1\t3\n"
    "# summary\tchecked=2\tdropping=1\tunaccounted=2\tforgiven=2\tunframed-topics=1\t"
    "markdown-dropping=1\tmarkdown-unaccounted=2\thtml-dropping=1\thtml-unaccounted=1\n")


RENDERED = (
    "# rendered\t1.0\talpha \\\\<name\\\\>\t"
    "<h2 class=\"geist-heading\">Alpha</h2><span class=\"geist-anchor\" "
    "id=\"A1\"></span><p class=\"geist-paragraph\"><strong class=\"geist-strong\">"
    "K</strong> key\\n&lt;name&gt;</p>\n"
    "# rendered\t1.1\t\t\n")
REPORTS = CONSERVATION.replace("# summary", RENDERED + "# summary")


def sample():
    return {"identity": {"size": 3, "sha256": "abc"},
            "edition": {"Timestamp": "19930422134757"},
            "topics": GATE.parse_coverage(COVERAGE),
            "conservation": GATE.parse_conservation(CONSERVATION)}


class CorpusGateTest(unittest.TestCase):
    def test_parse_coverage(self):
        self.assertEqual(set(GATE.parse_coverage(COVERAGE)), {"1.0", "1.1"})

    def test_parse_conservation_formats(self):
        self.assertEqual(GATE.parse_conservation(CONSERVATION)["deficits"],
                         {"1.0": {"alpha": 2}})
        self.assertEqual(
            GATE.parse_conservation(CONSERVATION)["format_deficits"],
            {"markdown": {"1.0": {"alpha": 2}},
             "html": {"1.0": {"alpha": 1}}})

    def test_escaped_topic_and_word_remain_distinct(self):
        escaped = CONSERVATION.replace("alpha", "alpha\\tbeta")
        self.assertEqual(GATE.parse_conservation(escaped)["deficits"],
                         {"1.0": {"alpha\\tbeta": 2}})

    def test_malformed_reports_fail(self):
        for text in ("", COVERAGE.replace("total=2", "total=3"),
                     COVERAGE.replace("best-effort=1", "best-effort=0"),
                     COVERAGE.replace("\ttyped\t\t\ttopic-index",
                                      "\tunknown\t\t\ttopic-index"),
                     COVERAGE.replace("1.1\t2", "1.0\t2"),
                     COVERAGE.replace("# summary", "# missing"),
                     COVERAGE.replace("1.0\t1\ttyped", "1.0\t1\tlegacy"),
                     COVERAGE.replace("topic-index\t0\t2\t4",
                                      "topic-index\t0\t5\t4"),
                     COVERAGE.replace("topic-index\t1\t4\t7",
                                      "topic-index\t0\t2\t4")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                GATE.parse_coverage(text)
        for text in ("", CONSERVATION.replace("unaccounted=2", "unaccounted=3"),
                     CONSERVATION.replace("dropping=1", "dropping=0"),
                     CONSERVATION.replace("unframed-topics=1", "unframed-topics=0"),
                     CONSERVATION.replace("markdown-unaccounted=2",
                                          "markdown-unaccounted=3"),
                     CONSERVATION.replace("# topic\t1.1", "# unknown\t1.1"),
                     CONSERVATION.replace("# summary", "# missing"),
                     CONSERVATION.replace("\t1\t3\t1\t2\t", "\t1\t3\t1\t-2\t"),
                     CONSERVATION.replace("record 1 line 2\\tproof", "record\tunexpected")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                GATE.parse_conservation(text)

    def compare(self, change):
        before = sample()
        after = copy.deepcopy(before)
        change(after)
        return GATE.compare_book("synthetic.boo", before, after)

    def test_unchanged_passes(self):
        self.assertEqual(self.compare(lambda _: None), ([], []))

    def test_identity_and_edition_mismatch_fail(self):
        for area, key in (("identity", "sha256"), ("identity", "size"),
                          ("edition", "Timestamp")):
            failures, _ = self.compare(lambda book: book[area].update({key: "changed"}))
            self.assertTrue(failures)

    def test_strict_to_degraded_is_regression(self):
        failures, _ = self.compare(lambda book: book["topics"]["1.0"].update(
            severity="typed-degraded"))
        self.assertTrue(failures)

    def test_route_loss_is_regression(self):
        failures, _ = self.compare(lambda book: book["topics"]["1.0"].update(
            severity="best-effort", route="legacy"))
        self.assertTrue(failures)

    def test_same_rank_family_change_requires_review(self):
        failures, _ = self.compare(lambda book: book["topics"]["1.0"].update(family="menu"))
        self.assertTrue(failures)

    def test_same_rank_degradation_change_requires_review(self):
        before = sample()
        before["topics"]["1.0"].update(severity="typed-degraded", degraded="table")
        after = copy.deepcopy(before)
        after["topics"]["1.0"]["degraded"] = "table,figure"
        self.assertTrue(GATE.compare_book("synthetic.boo", before, after)[0])

    def test_source_identity_change_requires_review(self):
        failures, _ = self.compare(
            lambda book: book["topics"]["1.0"].update(source_start="3"))
        self.assertTrue(failures)

    def test_topic_exchange_cannot_hide_regression(self):
        def exchange(book):
            book["topics"]["1.0"].update(severity="best-effort", route="legacy")
            book["topics"]["1.1"].update(severity="typed", route="typed")
        failures, improvements = self.compare(exchange)
        self.assertEqual(len(failures), 1)
        self.assertEqual(len(improvements), 1)

    def test_missing_and_new_topics_fail(self):
        self.assertTrue(self.compare(lambda book: book["topics"].pop("1.0"))[0])
        self.assertTrue(self.compare(lambda book: book["topics"].update(
            extra=book["topics"]["1.0"]))[0])

    def test_loss_exchange_cannot_hide_regression(self):
        def exchange(book):
            book["conservation"]["deficits"] = {"1.0": {"beta": 1}}
        failures, improvements = self.compare(exchange)
        self.assertEqual(len(failures), 1)
        self.assertEqual(len(improvements), 1)

    def test_same_word_in_different_topic_is_new_loss(self):
        failures, _ = self.compare(lambda book: book["conservation"]["deficits"].update(
            {"1.1": {"alpha": 1}}))
        self.assertTrue(failures)

    def test_new_or_increased_loss_fails(self):
        self.assertTrue(self.compare(lambda book: book["conservation"]["deficits"]["1.0"].update(
            alpha=3))[0])

    def test_formats_are_ratchet_checked_independently(self):
        def move_loss(book):
            book["conservation"]["format_deficits"] = {
                "markdown": {"1.0": {"alpha": 1}},
                "html": {"1.0": {"alpha": 2}},
            }
        failures, improvements = self.compare(move_loss)
        self.assertEqual(len(failures), 1)
        self.assertEqual(len(improvements), 1)

    def test_unframed_growth_fails(self):
        for topic in ("1.0", "1.1"):
            self.assertTrue(self.compare(lambda book: book["conservation"]["unframed"].update(
                {topic: {"unframed": 2, "records": 3}}))[0])

    def test_checked_count_loss_fails(self):
        failures, _ = self.compare(lambda book: book["conservation"]["summary"].update(checked=1))
        self.assertTrue(failures)

    def test_recovery_reported(self):
        failures, improvements = self.compare(lambda book: book["conservation"]["deficits"].clear())
        self.assertFalse(failures)
        self.assertTrue(improvements)

    def test_identity_rejected_before_tools_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            book = root / "synthetic.BOO"
            book.write_bytes(b"boo")
            baseline = root / "baseline.json"
            baseline.write_text(json.dumps({"schema": 2, "books": {book.name: sample()}}))
            with patch.object(GATE, "run_tool") as tool, self.assertRaisesRegex(
                    ValueError, "identity mismatch"):
                GATE.main(["--corpus", str(root), "--baseline", str(baseline)])
            tool.assert_not_called()

    def test_capture_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "synthetic.boo").write_bytes(b"boo")
            existing = root / "existing.json"
            existing.write_text("preserve")
            with self.assertRaisesRegex(ValueError, "refusing overwrite"):
                GATE.main(["--corpus", str(root), "--capture", str(existing),
                           "--revision", "synthetic"])
            self.assertEqual(existing.read_text(), "preserve")

    def test_capture_and_check_cli_preserve_baseline(self):
        def reports(_executable, _book, *args):
            if args == ("--coverage",):
                return COVERAGE
            if args == ("--conservation",):
                return CONSERVATION
            if args == ("--conservation", "--rendered"):
                return REPORTS
            return "Timestamp: 19930422134757\nDocument number: synthetic\nTitle: Test\nVersion: 1.2\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "synthetic.BOO").write_bytes(b"boo")
            baseline = root / "baseline.json"
            common = ["--corpus", str(root), "--jobs", "1"]
            with patch.object(GATE, "run_tool", side_effect=reports), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(GATE.main(common + ["--capture", str(baseline),
                                                    "--revision", "synthetic"]), 0)
                original = baseline.read_bytes()
                self.assertEqual(GATE.main(common + ["--baseline", str(baseline)]), 0)
                self.assertEqual(baseline.read_bytes(), original)
                (root / "unexpected.boo").write_bytes(b"other")
                with self.assertRaisesRegex(ValueError, "book set mismatch"):
                    GATE.main(common + ["--baseline", str(baseline)])

    def test_explicit_books_support_separate_pinned_corpus(self):
        def reports(_executable, _book, *args):
            if args == ("--coverage",):
                return COVERAGE
            if args == ("--conservation",):
                return CONSERVATION
            if args == ("--conservation", "--rendered"):
                return REPORTS
            return "Timestamp: 19930422134757\nDocument number: synthetic\nTitle: Test\nVersion: 1.2\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.boo"
            second = root / "second.BOO"
            first.write_bytes(b"one")
            second.write_bytes(b"two")
            capture = root / "supplemental.json"
            with patch.object(GATE, "run_tool", side_effect=reports), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(GATE.main([
                    "--book", str(first), "--book", str(second),
                    "--capture", str(capture), "--revision", "synthetic",
                    "--jobs", "1"]), 0)
            snapshot = json.loads(capture.read_text())
            self.assertEqual(set(snapshot["books"]), {first.name, second.name})

    def test_zero_topic_book_is_measured_and_reported(self):
        # What bootrace prints for a book it finds no topics in: the severity
        # summary carries no fields at all.
        coverage = COVERAGE.split("\n", 1)[0] + (
            "\n# summary\ttyped=0\tlegacy=0\ttotal=0\n# severity\n")
        conservation = CONSERVATION.split("\n", 1)[0] + (
            "\n# summary\tchecked=0\tdropping=0\tunaccounted=0\tforgiven=0\t"
            "unframed-topics=0\tmarkdown-dropping=0\tmarkdown-unaccounted=0\t"
            "html-dropping=0\thtml-unaccounted=0\n")
        self.assertEqual(GATE.parse_coverage(coverage), {})
        self.assertEqual(GATE.parse_conservation(conservation)["topics"], {})
        for text in (coverage.replace("# severity\n", ""),
                     coverage.replace("# severity\n", "# severity\n# severity\n"),
                     coverage.replace("# severity\n", "# severity\ttyped=1\n")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                GATE.parse_coverage(text)

        def reports(_executable, _book, *args):
            if args == ("--coverage",):
                return coverage
            if args[:1] == ("--conservation",):
                return conservation
            return "Timestamp: 19891031000000\nDocument number: empty\nTitle: Empty\nVersion: 1.0\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "empty.BOO").write_bytes(b"boo")
            baseline = root / "baseline.json"
            common = ["--corpus", str(root), "--jobs", "1"]
            out = io.StringIO()
            with patch.object(GATE, "run_tool", side_effect=reports), \
                    contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(GATE.main(common + ["--capture", str(baseline),
                                                    "--revision", "synthetic"]), 0)
                self.assertIn("ZERO TOPICS empty.BOO", out.getvalue())
                out.truncate(0)
                self.assertEqual(GATE.main(common + ["--baseline", str(baseline)]), 0)
                self.assertIn("ZERO TOPICS empty.BOO", out.getvalue())

    def test_explicit_books_reject_invalid_and_duplicate_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            invalid = root / "not-a-book.txt"
            invalid.write_bytes(b"one")
            with self.assertRaisesRegex(ValueError, "not a BOO file"):
                GATE.select_books(None, [invalid])
            first_dir = root / "first"
            second_dir = root / "second"
            first_dir.mkdir()
            second_dir.mkdir()
            first = first_dir / "same.boo"
            second = second_dir / "same.boo"
            first.write_bytes(b"one")
            second.write_bytes(b"two")
            with self.assertRaisesRegex(ValueError, "duplicate corpus filename"):
                GATE.select_books(None, [first, second])


def html_print(html, markdown=""):
    return GATE.fingerprint(markdown, html)


class FingerprintTest(unittest.TestCase):
    def test_unescape(self):
        self.assertEqual(GATE.tsv_unescape(r"a\tb\\n\n"), "a\tb\\n\n")
        for value in (r"bad\x", "trailing\\"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                GATE.tsv_unescape(value)

    def test_split_rendered_keeps_report_and_unicode_separators(self):
        report, rendered = GATE.split_rendered(REPORTS.replace("key", "key line"))
        self.assertEqual(report, CONSERVATION)
        self.assertEqual(set(rendered), {"1.0", "1.1"})
        self.assertIn(" ", rendered["1.0"][1])
        self.assertIn("\n", rendered["1.0"][1])
        self.assertEqual(rendered["1.0"][0], r"alpha \<name\>")
        with self.assertRaises(ValueError):
            GATE.split_rendered(RENDERED + RENDERED)

    def test_style_runs_and_nesting(self):
        _, detail = html_print(
            '<p class="geist-paragraph">a <strong class="geist-strong-emphasis">'
            '<em>both</em></strong> <code class="geist-monospace">x</code>'
            '<code class="geist-opaque-inline" data-geist-opaque-kind="k">o</code>'
            ' <u class="geist-underline">u</u></p>')
        self.assertEqual(detail["style"], ["em+strong:both", "code:x", "opaque:o", "u:u"])
        self.assertEqual(detail["midword"], ["[code]x|[opaque]o"])

    def test_phrase_elements_are_style_kinds(self):
        summary, detail = html_print(
            '<p><samp class="geist-monospace geist-sample">s</samp> '
            '<kbd class="geist-monospace geist-keyboard">k</kbd> <var>v</var> '
            '<cite class="geist-emphasis geist-citation">c</cite></p>')
        self.assertEqual(detail["style"], ["samp:s", "kbd:k", "var:v", "cite:c"])
        self.assertEqual(detail["structure"], ["0:p"])
        self.assertEqual(summary["style_runs"], "cite:1 kbd:1 samp:1 var:1")

    def test_mid_word_spans(self):
        for html, count in (
                ("<p><em>o</em>rientation o</p>", 1),
                ("<p><em>orientation</em> o</p>", 0),
                ("<p><code>CLIST</code>s</p>", 1),
                ("<p>a<em>b</em>c</p>", 2),
                ("<p><em>a</em></p><p>b</p>", 0),
                ("<p><em>a</em><br>b</p>", 0),
                ('<p><em>a</em><a href="#x">b</a></p>', 1),
                ('<p><em>a<a href="#x">b</a></em></p>', 0)):
            with self.subTest(html=html):
                self.assertEqual(html_print(html)[0].get("midword", 0), count)

    def test_structure_anchors_links(self):
        summary, detail = html_print(
            '<h2 class="geist-heading">T</h2>'
            '<ol class="geist-list" type="a"><li class="geist-list-item" value="2">'
            '<a href="#t">x</a></li></ol><span class="geist-anchor" id="Z"></span>'
            '<pre class="geist-pre">a\nb</pre><hr class="geist-rule">'
            '<img class="geist-image" src="r.png" alt="Fig">')
        self.assertEqual(detail["structure"], [
            "0:h2.heading", "0:ol.list[type=a]", "1:li.list-item[value=2]",
            "0:pre.pre", "0:hr.rule"])
        self.assertEqual(detail["anchors"], ["Z"])
        self.assertEqual(detail["links"], ["#t", "img:r.png"])
        self.assertEqual(summary["blocks"],
                         "h2.heading:1 hr.rule:1 li.list-item:1 ol.list:1 pre.pre:1")
        self.assertEqual(GATE.parse_multiset(summary["chars"]),
                         {"T": 1, "x": 1, "a": 1, "b": 1, "F": 1, "i": 1, "g": 1})

    def test_markdown_visible_text_is_counted_as_text(self):
        # The trace projection has already removed Markdown escapes; whatever
        # punctuation surrounds them, the letters of `<name>` are text.
        summary, _ = html_print("<p>&lt;name&gt;</p>", r"\<name\>")
        self.assertNotIn("markdown_chars", summary)
        summary, _ = html_print("<p>&lt;name&gt;</p>", r"\<nam\>")
        self.assertEqual(GATE.parse_multiset(summary["markdown_chars"]),
                         {"n": 1, "a": 1, "m": 1})

    def test_entities_and_characters(self):
        summary, _ = html_print("<p>&amp; caf&eacute; 2</p>")
        self.assertEqual(GATE.parse_multiset(summary["chars"]),
                         {"c": 1, "a": 1, "f": 1, "é": 1, "2": 1})

    def test_malformed_html_fails(self):
        for html in ("<p><em>a</p></em>", "<p>open", "</p>"):
            with self.subTest(html=html), self.assertRaises(ValueError):
                html_print(html)

    def compare(self, before_html, after_html, strict=False, before_md="",
                after_md=""):
        before = html_print(before_html, before_md)
        after = html_print(after_html, after_md)
        tally = {}
        result = GATE.compare_fingerprints(
            "b.boo", {"1": before[0]}, {"1": after[0]}, {"1": before[1]},
            {"1": after[1]}, strict, tally)
        return result + (tally,)

    def test_unchanged(self):
        html = "<p><strong>K</strong> key</p>"
        self.assertEqual(self.compare(html, html), ([], [], [], {}))

    def test_bold_to_italic_needs_review(self):
        before = '<p><strong class="geist-strong">K</strong> key</p>'
        after = '<p><em class="geist-emphasis">K</em> key</p>'
        failures, _, reviews, tally = self.compare(before, after)
        self.assertEqual(failures, [])
        self.assertEqual(set(tally), {"style"})
        self.assertIn("em 0->1, strong 1->0", reviews[0])
        self.assertIn("-'strong:K' +'em:K'", reviews[0])
        failures, _, reviews, _ = self.compare(before, after, strict=True)
        self.assertEqual((len(failures), reviews), (1, []))

    def test_style_moved_onto_wrong_characters_fails(self):
        failures, _, reviews, tally = self.compare(
            "<p><em>orientation</em> o</p>", "<p><em>o</em>rientation o</p>")
        self.assertEqual(len(failures), 1)
        self.assertIn("mid-word", failures[0])
        self.assertIn("[em]o|[]rientation", failures[0])
        self.assertEqual(set(tally), {"mid-word", "style"})
        self.assertEqual(len(reviews), 1)

    def test_mid_word_recovery_is_improvement(self):
        failures, improvements, _, _ = self.compare(
            "<p><em>o</em>rientation o</p>", "<p><em>orientation</em> o</p>")
        self.assertEqual(failures, [])
        self.assertEqual(len(improvements), 1)

    def test_character_loss_fails_and_gain_needs_review(self):
        failures, _, _, _ = self.compare("<p>abc</p>", "<p>ab</p>")
        self.assertEqual(len(failures), 1)
        self.assertIn("HTML lost 'c'x1", failures[0])
        failures, _, reviews, _ = self.compare("<p>ab</p>", "<p>abc</p>")
        self.assertEqual(failures, [])
        self.assertIn("HTML gained 'c'x1", reviews[0])

    def test_markdown_character_loss_is_checked_independently(self):
        failures, _, _, _ = self.compare("<p>ab</p>", "<p>ab</p>", before_md="ab",
                                         after_md="a")
        self.assertEqual(len(failures), 1)
        self.assertIn("Markdown lost 'b'x1", failures[0])

    def test_case_change_is_loss(self):
        self.assertTrue(self.compare("<p>Ab</p>", "<p>ab</p>")[0])

    def test_structure_and_anchor_changes_need_review(self):
        failures, _, reviews, tally = self.compare(
            '<ul class="geist-list"><li>a</li></ul><span id="X"></span>',
            '<ol class="geist-list" type="a"><li>a</li></ol><span id="Y"></span>')
        self.assertEqual(failures, [])
        self.assertEqual(set(tally), {"structure", "anchors"})
        self.assertTrue(any("ol.list:1" not in review and "ol.list 0->1" in review
                            for review in reviews))
        self.assertTrue(any("same count" in review and "'Y'" in review
                            for review in reviews))

    def test_link_change_needs_review(self):
        _, _, _, tally = self.compare('<p><a href="#a">x</a></p>',
                                      '<p><a href="#b">x</a></p>')
        self.assertEqual(set(tally), {"links"})

    def test_missing_fingerprint_fails(self):
        summary, _ = html_print("<p>a</p>")
        failures, _, _ = GATE.compare_fingerprints("b.boo", {"1": summary}, {})
        self.assertTrue(failures)

    def test_expand_rejects_unknown_fields(self):
        with self.assertRaises(ValueError):
            GATE.expand_fingerprint({"colour": "red"})

    def test_capture_writes_schema_3_and_schema_2_still_checks(self):
        calls = []
        def reports(_executable, _book, *args):
            calls.append(args)
            if args == ("--coverage",):
                return COVERAGE
            if args == ("--conservation",):
                return CONSERVATION
            if args == ("--conservation", "--rendered"):
                return REPORTS
            return "Timestamp: 1\nDocument number: s\nTitle: T\nVersion: 1\n"
        changed = REPORTS.replace('<strong class="geist-strong">K</strong>',
                                  '<em class="geist-emphasis">K</em>')
        def restyled(executable, book, *args):
            if args == ("--conservation", "--rendered"):
                return changed
            return reports(executable, book, *args)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "s.boo").write_bytes(b"boo")
            capture = root / "capture.json"
            raw = root / "raw"
            common = ["--corpus", str(root), "--jobs", "1"]
            out = io.StringIO()
            with contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(io.StringIO()):
                with patch.object(GATE, "run_tool", side_effect=reports):
                    self.assertEqual(GATE.main(common + [
                        "--capture", str(capture), "--revision", "r",
                        "--raw-dir", str(raw)]), 0)
                    snapshot = json.loads(capture.read_text())
                    self.assertEqual(snapshot["schema"], 3)
                    book = snapshot["books"]["s.boo"]
                    self.assertNotIn("details", book)
                    self.assertEqual(book["fingerprints"]["1.1"], {})
                    self.assertEqual(book["fingerprints"]["1.0"]["style_runs"],
                                     "strong:1")
                    self.assertTrue((raw / "s.boo.fingerprint.json").is_file())
                    self.assertEqual(GATE.main(common + [
                        "--baseline", str(capture), "--baseline-raw", str(raw)]), 0)
                    self.assertIn("0 fingerprint changes", out.getvalue())
                # A changed style is reported but not fatal unless asked.
                with patch.object(GATE, "run_tool", side_effect=restyled):
                    self.assertEqual(GATE.main(common + [
                        "--baseline", str(capture), "--baseline-raw", str(raw)]), 0)
                    self.assertIn("REVIEW s.boo: 1.0: style: em 0->1, strong 1->0 "
                                  "{-'strong:K' +'em:K'}", out.getvalue())
                    self.assertIn("CHANGED TOPICS s.boo: style 1", out.getvalue())
                    self.assertEqual(GATE.main(common + [
                        "--baseline", str(capture), "--fail-on-review"]), 1)
                # A schema 2 baseline is checked without rendering fingerprints.
                legacy = root / "legacy.json"
                del book["fingerprints"]
                legacy.write_text(json.dumps({"schema": 2, "books": {"s.boo": book}}))
                calls.clear()
                with patch.object(GATE, "run_tool", side_effect=reports):
                    self.assertEqual(GATE.main(common + ["--baseline", str(legacy)]), 0)
                    self.assertIn(("--conservation",), calls)
                    self.assertNotIn(("--conservation", "--rendered"), calls)
                    # A schema 3 baseline must carry fingerprints.
                    legacy.write_text(json.dumps({"schema": 3,
                                                  "books": {"s.boo": book}}))
                    with self.assertRaisesRegex(ValueError, "without fingerprints"):
                        GATE.main(common + ["--baseline", str(legacy)])


if __name__ == "__main__":
    unittest.main()
