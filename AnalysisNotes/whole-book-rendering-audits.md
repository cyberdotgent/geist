# Whole-Book Rendering Audits

## Purpose

`tools/bookserver_book_audit.py` creates a repeatable, cacheable comparison of
every local BOO table-of-contents topic against the corresponding hosted IBM
BookManager BookServer page. The result is evidence for human or agent review,
not an automatic correctness verdict. In particular, Markdown reflow can lower
the text score without indicating lost content.

The audit output is scratch data by default. Put it under `/tmp` (or another
external work directory), then commit only compact findings that are useful for
future format work.

## Prerequisites

Build the local tools in the repository build directory:

```sh
cmake -S libgeist -B build
cmake --build build
```

Identify four values before running an audit:

1. The local BOO path.
2. The hosted `BOOKS/<book-id>` identifier. Do not assume it from the filename;
   first fetch a known topic such as `ABSTRACT` or `CONTENTS` and verify the
   returned HTML title and body.
3. The BookServer `DT` value. This is normally the BOO directory build date and
   time formatted as `YYYYMMDDhhmmss`.
4. A scratch output directory unique to the book and baseline revision.

When available, use the Docker fetch MCP described in
`AnalysisNotes/bookserver-docker-fetch.md` to verify and capture hosted pages.
The audit tool can reuse those captures with `--no-fetch`. Direct HTTP fetching
is also supported for environments whose routing can reach the hosted CGI.

## Complete Audit Command

For `GG24-4302-00.boo`, the verified source identifiers on 2026-08-25 were:

```text
BookServer id: GG24-4302-00
DT:            19950308184737
Full title:    International Technical Support Organization: IMS/ESA Version 5.1 Guide
Short title:   IMS 5.1 Guide
Document:      GG24-4302-00
Verification:  /BOOKS/GG24-4302-00/ABSTRACT?DT=19950308184737&SHELF=
BOO SHA-256:   ca21cfd3f523c117f9d3f9139ad3054b735c56a7f7332f2515bd5298ac7bb235
```

The `ABSTRACT` request returned HTTP 200 and an HTML title identifying
`IMS 5.1 Guide`; its body and `CONTENTS` link used the same document and `DT`
value. The Docker fetch MCP was not exposed in the 2026-08-25 session, so this
verification and the complete capture used direct HTTP, which was reachable
from that environment.

Run:

```sh
python3 tools/bookserver_book_audit.py \
  BOO/GG24-4302-00.boo \
  --book-id GG24-4302-00 \
  --timestamp 19950308184737 \
  --output /tmp/geist-GG24-4302-00-audit \
  --jobs 4
```

The output contains:

| Path | Contents |
| --- | --- |
| `book.json` | Source BOO, hosted identifiers, invocation, and topic count |
| `manifest.tsv` | One row per topic with exact URL, score, flags, structure counts, and artifact paths |
| `local/*.md` | Fresh `boorender --md` output |
| `reference/*.html` | Exact hosted response bytes |
| `diff/*.diff` | Normalized block-level HTML-versus-Markdown diff |

Existing reference files are reused. Use `--refresh` to refetch them, or
`--no-fetch` to require a completely offline rerun. A `server-error` status
means the CGI returned a known BookServer error page with HTTP success; it must
not be treated as renderer evidence.

Local tool output is decoded as UTF-8 with replacement so a malformed topic
cannot abort the remaining whole-book audit. Such a topic receives an
`invalid-utf8` flag; the replacement is diagnostic evidence and does not make
the underlying renderer output valid.

## Review Procedure

Use the heuristic flags only to order the work. Review every topic in the
assigned range, because a structurally wrong page may retain all its text and
therefore receive a high score.

For a suspicious topic, compare the local Markdown and hosted topic body, then
capture decoder evidence without modifying either fixture:

```sh
build/boorender BOO/GG24-4302-00.boo 2.2 --raw
build/bootrace BOO/GG24-4302-00.boo 2.2 --all
```

Group findings by probable defect class rather than creating one issue for each
topic. A useful defect report includes:

- book identifier, BOO checksum or baseline commit, and build command;
- all affected topic IDs and exact hosted URLs;
- a short expected/actual excerpt that demonstrates the problem without
  reproducing substantial book text;
- whether content is missing, duplicated, reordered, mis-styled, or merely
  reflowed;
- the logical-record range and a focused `bootrace` command/output excerpt;
- the suspected decoder/normalizer/Markdown component, clearly marked as a
  lead rather than a verified cause;
- a proposed fixture-based regression assertion.

Maintain one tracking issue for the book that links the distinct defect issues
and records the audited topic count, fetch failures, baseline commit, and audit
date. This keeps repeated manifestations of one decoding bug together while
still proving that the entire book was covered.

## Validation

Before publishing findings:

1. Confirm `manifest.tsv` has the same row count as `bootoc` has topics.
2. Confirm every row has either a reference artifact or an explicit fetch/error
   status.
3. Rerun with `--no-fetch` and confirm it succeeds without network access.
4. Manually inspect at least one clean, one text-mismatch, and one structural-
   mismatch topic to calibrate false positives.
5. Run the libgeist test suite so an audit is tied to a known functional
   baseline: `ctest --test-dir build --output-on-failure`.

For performance comparisons, use the same cached reference directory and
measure the local-only rerun with `--no-fetch`; network latency must not be
included in renderer timing.

## SC31-711 post-issue-42 audit

On 2026-08-25, the issue-42 candidate was checked against BookServer id
`SC31-711`, DT `19941010174546`, using the fixture whose SHA-256 is
`ac5dcb35e10f6e08107fc2e6e87420ad2652bf675c069eb2f4cb2606a5415700`:

```sh
python3 tools/bookserver_book_audit.py BOO/SC31-711.boo \
  --book-id SC31-711 --timestamp 19941010174546 \
  --output /tmp/geist-SC31-711-post42-final --jobs 4
```

All 82 topics rendered and fetched; 43 received heuristic flags. The exact
URL, fetch status, local Markdown, hosted HTML, and normalized diff for every
topic are recorded in `/tmp/geist-SC31-711-post42-final/manifest.tsv` and its
adjacent artifact directories. The symbolic trap catalog and recovered
publication citations were checked directly. Remaining genuine fixed-form
and selector defects found by the all-topic review are tracked in the
follow-up GitHub issue 48 rather than being hidden by the aggregate similarity
score. Existing independent residuals remain in issues 43--45.

## SC31-711 implicit-grid investigation

Issue 48 topics `1.1` and `1.3` were compared with hosted BookServer book
`SC31-711` (document SC31-7111-00, DT `19941010174546`). Cached references:

- `/tmp/geist-SC31-711-post45-live/reference/0012-1.1.html`
- `/tmp/geist-SC31-711-post45-live/reference/0014-1.3.html`

The repository scan began with every CFONT heading whose first span is column
3 and which has at least three HP2 spans: 7,792 headings and 5,213 unique
candidate logical records after deduplication. Candidate records received a
two-record source lookahead. The topic-1.1 signature occurred only in genuine
SC31 grids. Topic 1.3's reset prefix had sparse false positives in SC31 message
prose, notably logical records 161--162, so classification also requires two
header groups, six repeated origins, stable columns, and source-tail ownership.
GG24-4302 topic `1.0` run-in headings were an explicit negative fixture.

`bootrace` supplied decoded segment/font-span traces. A temporary diagnostic
built against the candidate-local source API printed encoded token value and
width, resolved Unicode words, token index, and payload range. Every selected
slice was checked against the initial decoded logical record. BookServer HTML
established 18 directory rows and 19 process rows, including continuations.

The implementation retains one eight-byte payload range per logical record and
materializes provenance only after cheap CFONT geometry selects a topic. The
source dictionary is created on first request and reused, avoiding an eager
per-book provenance cache while retaining one- versus two-byte token identity.
