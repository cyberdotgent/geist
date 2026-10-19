# Corpus regression gate

`tools/corpus_gate.py` compares the current `bootrace --coverage` and
`--conservation` reports with an explicit, edition-pinned baseline. It uses only
the Python standard library. Private books must be supplied separately; none
are distributed by this tooling. `packet.boo` retains its normal CTest coverage.

```
python3 tools/corpus_gate.py --corpus /path/to/private/books --jobs 4
python3 tests/corpus_gate_synthetic.py
```

The corpus directory is flat. Both `.boo` and `.BOO` suffixes are included;
filenames are case-sensitive. Every listed book must be present and no extra
book is silently admitted. The baseline pins each file's byte size and SHA-256,
plus its document number, title, timestamp and reported version. A file identity
mismatch fails before decoding, even when the replacement has the same name or
document number. No book is downloaded automatically.

The gate rejects a missing or unexpected coverage topic, any per-topic severity
regression (including strict typed to typed-degraded), a reduction in conservation
checked-topic count, an increase in any topic's unframed-record count, or an
increase in any individual topic/word's unaccounted count. Recovering a word in
one topic cannot excuse losing a different word or regressing another topic's
route. Improvements are enumerated without changing the baseline. Unknown or
inconsistent report schemas fail instead of being interpreted as clean output.
At the same severity, changing the lowering family or degradation details also
requires explicit review rather than silently passing. Exact rendered output
diffs remain a separate verification step; a passing gate does not imply
byte-identical output.

These checks inherit the scope and limitations of the current diagnostics:
coverage and conservation iterate the TOC inventory, not every item returned by
`topics()`. Conservation skips records without proven display-line framing and
forgives some opening-word shortfalls that could be control opcodes. The gate
tracks unframed growth but does not prove such records faithful. Checked-topic
counts are aggregate because the report has no per-topic checked flag. The
gate does not establish reference-reader equivalence, visual layout fidelity,
or absence of duplicates/reordering. Those require targeted comparisons and
regression tests. In particular, a placeholder-run decline earns no exemption.

Emitted-word conservation checks Markdown and HTML independently. Markdown
text is recovered from the serializer's complete byte-range trace, not by
guessing Markdown syntax; that trace must match the actual cached output.
HTML text projection preserves inline adjacency and block boundaries and
accounts for ordered-list markers and image replacement text. Link
destinations, unrelated attributes and diagnostic comments cannot supply
missing book words. The reported emitted count is the minimum across the two
formats, so success in one cannot hide a loss in the other. This remains an
occurrence-count check, not a proof of correct word order or geometry; exact
rendered-output comparisons are still required.
The text projections accept only the serializers' supported syntax and fail
on unsupported or malformed markup rather than guessing its visible text.

## Reviewing a baseline change

Checking never writes the baseline. An explicit capture creates a *new* file,
refuses to overwrite an existing file, and requires the source revision:

```
python3 tools/corpus_gate.py --corpus /path/to/private/books \
  --capture /tmp/corpus-candidate.json --revision COMMIT \
  --raw-dir /tmp/corpus-candidate-raw --jobs 4
```

`--raw-dir` must also be new; it preserves the exact reports for review. Keep it
outside the repository because reports contain source-text evidence. A capture
retains classification fields and loss counts, omitting unused decline prose
and source-line evidence from the maintained JSON snapshot. A capture
records observations, not approved exceptions. Compare its per-topic changes
with the maintained baseline and explain every accepted difference before
replacing the baseline in a scoped commit. Do not refresh the baseline merely
to make a failing change pass. Retain existing loss allowances until their
specific improvements are reviewed, then ratchet them downward. Adding an
edition requires a separate reviewed identity and measurements.

The older ratchet and conservation scripts were removed during the August 2026
cleanup. Their historical totals are not assumed by this gate; the maintained
snapshot records current measurements and the exact source revision that
produced them.

The initial snapshot was measured from revision `4470c8c9` using a frozen,
statically linked `bootrace`: 33 private books, 7,238 TOC topics, 7,103 strict
typed, two typed-degraded and 133 best-effort. It records 70 unaccounted word
occurrences and no unframed records; existing allowances are known findings,
not declarations of correct output. All 7,238 topics were checked. The corpus
contains seven uppercase `.BOO` filenames, so a lowercase-only sweep misses
part of it. For example, the full initial snapshot has 59 placeholder-run
declines; the 26 lowercase books alone have 50. Neither count establishes
faithfulness or authorizes an exemption.

The baseline was ratcheted at `d1b9bbf6` after the caption fix: the 20 lost
word occurrences in `SC09-2417-00` topic `1.3.3.4` are no longer allowed.
The remaining total is 50, with the same route counts and no new losses.
Markdown and HTML comparisons covered all 10,379 indexed topics across the
33 books, independently of the TOC-only diagnostic gate. The reviewed output
changes were three complete source titles, the restored caption, and 96
caption-ending dots proven to be structural line-length bytes rather than
visible punctuation.
