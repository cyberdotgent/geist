#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

"""Corpus gate policy tests; no private books or build tools required."""

import copy
import contextlib
import importlib.util
import io
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
    "id\tlevel\troute\tfamily\treason\tclass\tsignature\tseverity\tdegraded\tdeclined\n"
    "1.0\t1\ttyped\tprose\t\tprose\tprose\ttyped\t\t\n"
    "1.1\t2\tlegacy\t\tno proof\tprose\tprose\tbest-effort\t\tno proof\n"
    "# summary\ttyped=1\tlegacy=1\ttotal=2\n"
    "# severity\tbest-effort=1\ttyped=1\n")
CONSERVATION = (
    "topic\troute\tword\tdrawn_opening\tdrawn_inline\temitted\tunaccounted\tevidence\n"
    "1.0\ttyped\talpha\t1\t3\t1\t2\trecord 1 line 2\\tproof\n"
    "1.1\tbest-effort\tsi\t1\t0\t0\t0\topening control\n"
    "# unframed\t1.1\t1\t3\n"
    "# summary\tchecked=2\tdropping=1\tunaccounted=2\tforgiven=2\tunframed-topics=1\n")


def sample():
    return {"identity": {"size": 3, "sha256": "abc"},
            "edition": {"Timestamp": "19930422134757"},
            "topics": GATE.parse_coverage(COVERAGE),
            "conservation": GATE.parse_conservation(CONSERVATION)}


class CorpusGateTest(unittest.TestCase):
    def test_parse_coverage(self):
        self.assertEqual(set(GATE.parse_coverage(COVERAGE)), {"1.0", "1.1"})

    def test_parse_conservation_header_versions(self):
        old_header = CONSERVATION.replace("\tunaccounted\tevidence\n", "\tunaccounted\n")
        self.assertEqual(GATE.parse_conservation(CONSERVATION),
                         GATE.parse_conservation(old_header))
        self.assertEqual(GATE.parse_conservation(CONSERVATION)["deficits"],
                         {"1.0": {"alpha": 2}})

    def test_escaped_topic_and_word_remain_distinct(self):
        escaped = CONSERVATION.replace("alpha", "alpha\\tbeta")
        self.assertEqual(GATE.parse_conservation(escaped)["deficits"],
                         {"1.0": {"alpha\\tbeta": 2}})

    def test_malformed_reports_fail(self):
        for text in ("", COVERAGE.replace("total=2", "total=3"),
                     COVERAGE.replace("best-effort=1", "best-effort=0"),
                     COVERAGE.replace("\ttyped\t\t\n", "\tunknown\t\t\n"),
                     COVERAGE.replace("1.1\t2", "1.0\t2"),
                     COVERAGE.replace("# summary", "# missing"),
                     COVERAGE.replace("1.0\t1\ttyped", "1.0\t1\tlegacy")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                GATE.parse_coverage(text)
        for text in ("", CONSERVATION.replace("unaccounted=2", "unaccounted=3"),
                     CONSERVATION.replace("dropping=1", "dropping=0"),
                     CONSERVATION.replace("unframed-topics=1", "unframed-topics=0"),
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
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            book = root / "synthetic.BOO"
            book.write_bytes(b"boo")
            baseline = root / "baseline.json"
            baseline.write_text(json.dumps({"schema": 1, "books": {book.name: sample()}}))
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


if __name__ == "__main__":
    unittest.main()
