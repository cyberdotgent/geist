#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

"""Format/*.md  ->  libgeist/doc/boo-spec/*.adoc

Provenance for the AsciiDoc specification: it records exactly how the .adoc
files were derived from the page-per-file Markdown notes, so the two can be
reconciled.  The .adoc files are the specification; this script is not part of
any build and nothing depends on it.

Run it from the repository root, where Format/ still exists.  Format/ is
outside the published libgeist/ tree, so a published copy of the library has
the specification but not this script's input -- which is the point: the book
stands alone.

The conversion is in two parts.  Everything above main() is mechanical and
content-preserving.  The EDITS table just below it is the editorial pass that
turns twelve separately written pages into one book; every entry is an exact
string match that must apply exactly once, and none of them changes a
technical claim.
"""
import os, re, sys

SRC = 'Format'
DST = 'libgeist/doc/boo-spec'

# Deliberate reading order: what the container is, how it is paged, how a
# record is stored and decoded, what the document structure is, how it is
# marked up, and finally the media payloads it can carry.
ORDER = [
    ('README.md',            'scope',            'Scope, Evidence, and Corpus'),
    ('boo-header.md',        'container',        'The Container Header and Directory'),
    ('pages.md',             'pages',            'Page Organization'),
    ('logical-controls.md',  'logical-controls', 'Logical Record Storage'),
    ('encoding.md',          'encoding',         'Encoding and Tokenization'),
    ('topics.md',            'topics',           'Topics and Documentation Pages'),
    ('table-of-contents.md', 'toc',              'The Table of Contents'),
    ('markup.md',            'markup',           'Decoded Markup and Controls'),
    ('assets.md',            'assets',           'Assets and Media Resources'),
    ('GDF.md',               'gdf',              'Legacy GDF Image Payloads'),
    ('MMR.md',               'mmr',              'Legacy MMR Image Payloads'),
    ('WebImages.md',         'web-images',       'Version 1.4 Web Image Payloads'),
]
FILE2CH = {f: c for f, c, _ in ORDER}
CH2TITLE = {c: t for _, c, t in ORDER}

# dblatex 0.3.12 has no unicode_map entry for these and its latin-1 LaTeX
# backend emits a raw XML character reference in their place.  The notes'
# own dominant convention for box-drawing characters is the U+xxxx notation,
# so the four literal occurrences are written that way instead.
GLYPH_AS_CODEPOINT = {'│': 'U+2502', '┐': 'U+2510'}

FENCE = re.compile(r'^(\s*)```(\w*)\s*$')
CODE = re.compile(r'`([^`]*)`', re.S)
LIST = re.compile(r'^(\s*)([-*+]|\d+\.)\s+(.*)$')
ALIGN = re.compile(r'^:?-{2,}:?$')


def slug(text):
    """GitHub-flavoured heading slug, so the notes' own #anchors keep working."""
    s = text.strip().lower().replace('`', '')
    s = re.sub(r'[^\w\s-]', '', s, flags=re.UNICODE)
    return re.sub(r'\s+', '-', s).strip('-')


# ---------------------------------------------------------------- inline ----

def ws_significant(s):
    """True when a code span's own whitespace carries information."""
    return s != s.strip() or '  ' in s


def longest_run(s):
    return max((len(w) for w in s.split(' ')), default=0)


def xml_escape(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


# Punctuation a stored string may be broken after without a reader mistaking
# the break for part of the string.
BREAK_AFTER = '/.,=?|;:&_>)'


def docbook_literal(s, limit=None):
    """Render one code span as raw DocBook <literal>.

    Two things AsciiDoc.py's inline literal cannot do are needed here.

    Its `...` span may not begin with a space, and the <literal> it produces
    collapses runs of spaces -- but in this corpus a leading space is a row's
    left margin and a run of spaces is column geometry, both of them evidence.
    Hard spaces keep them.

    Typewriter text also has no hyphenation points, so a stored string longer
    than the line -- a URL, a hosted href, a comma-separated byte list -- is
    set past the text block and clipped.  Splitting it into adjacent <literal>
    elements adds line-break opportunities without changing one rendered
    character: the pieces abut exactly as before when they fit on a line.
    """
    chunks, cur = [], ''
    for c in s:
        cur += c
        if limit and len(cur) >= limit and c in BREAK_AFTER:
            chunks.append(cur); cur = ''
    if cur:
        chunks.append(cur)

    def one(part):
        b = xml_escape(part)
        # Hard-space the margins and every run of two or more, which is where
        # the geometry lives; leave lone interior spaces breakable so a long
        # row still wraps instead of running off the page.
        b = re.sub(r'  +', lambda m: '&#160;' * len(m.group(0)), b)
        return re.sub(r'^ | $', '&#160;', b)
    return 'pass:[%s]' % ''.join('<literal>%s</literal>' % one(c)
                                 for c in chunks)


def prose(text, ch, anchors, in_table=False, run_limit=58):
    """Inline Markdown -> inline AsciiDoc, over a whole block at a time."""
    spans = []

    def take(m):
        # A code span wrapped across source lines is one span; keeping the
        # newline would let AsciiDoc's line-oriented block parser see its
        # continuation as a new block.
        s = re.sub(r'\s*\n\s*', ' ', m.group(1))
        for g, name in GLYPH_AS_CODEPOINT.items():
            s = s.replace(g, name)
        spans.append(s)
        return '\x00%d\x00' % (len(spans) - 1)
    text = CODE.sub(take, text)

    for g, name in GLYPH_AS_CODEPOINT.items():
        text = text.replace(g, name)

    def link(m):
        label, target = m.group(1), m.group(2)
        if target.startswith('http'):
            return 'link:%s[%s]' % (target, label)
        if target.startswith('#'):
            key = (ch, target[1:])
        elif '#' in target:
            f, a = target.split('#', 1)
            key = (FILE2CH.get(f), a)
        else:
            key = (FILE2CH.get(target), None)
        chap, anch = key
        if chap is None:
            # Outside the published libgeist/ tree.  Name the file in text; a
            # dangling xref would be worse than a plain named reference.
            return '%s (`%s`)' % (label, target)
        ident = chap if anch is None else anchors.get((chap, anch))
        if ident is None:
            sys.stderr.write('UNRESOLVED %s -> %s\n' % (ch, target))
            return label
        # A bare filename as link text reads as a page reference; let AsciiDoc
        # name the chapter instead so the book reads as one document.
        if re.fullmatch(r'[\w.-]+\.md', label):
            return '<<%s>>' % ident
        return '<<%s,%s>>' % (ident, label)
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, text, flags=re.S)

    # Strong first, parked out of the way so the emphasis rule cannot see the
    # single asterisks it just produced.
    text = re.sub(r'\*\*(?=\S)(.+?)(?<=\S)\*\*', '\x01\\1\x01', text, flags=re.S)
    text = re.sub(r'(?<![\w*])\*(?=\S)([^*]+?)(?<=\S)\*(?![\w*])',
                  r'_\1_', text, flags=re.S)
    text = text.replace('\x01', '*')

    def put(m):
        s = spans[int(m.group(1))]
        # AsciiDoc.py backticks are an inline literal passthrough: nothing is
        # substituted inside, so the only hazard left is the table row parser.
        too_long = longest_run(s) > run_limit
        if (ws_significant(s) or too_long) and ']' not in s:
            out = docbook_literal(s, max(8, run_limit // 3) if too_long
                                  else None)
            return out.replace('|', r'\|') if in_table else out
        return '`%s`' % (s.replace('|', r'\|') if in_table else s)
    text = re.sub('\x00(\\d+)\x00', put, text)
    return unwrap(text)


# A wrapped prose line whose first character happens to be block-significant
# to AsciiDoc's line-oriented parser would start a spurious block.  Fold such
# a line onto its predecessor; only the source wrapping changes.
DANGEROUS = re.compile(r'^\s*([.\[|=<>:~^_+]|//|[-*+]\s|\d+[.)]\s|\.{3})')


def unwrap(text):
    lines = text.split('\n')
    res = [lines[0]] if lines else []
    for l in lines[1:]:
        if res and res[-1].strip() and DANGEROUS.match(l):
            res[-1] = res[-1].rstrip() + ' ' + l.strip()
        else:
            res.append(l)
    return '\n'.join(res)


# ---------------------------------------------------------------- tables ----

def split_row(line):
    """Split a Markdown table row, honouring \\| escapes and code spans."""
    cells, buf, i, tick = [], '', 0, False
    while i < len(line):
        c = line[i]
        if c == '\\' and line[i + 1:i + 2] == '|':
            buf += '|'; i += 2; continue
        if c == '`':
            tick = not tick
        if c == '|' and not tick:
            cells.append(buf); buf = ''
        else:
            buf += c
        i += 1
    cells.append(buf)
    if cells and not cells[0].strip():
        cells = cells[1:]
    if cells and not cells[-1].strip():
        cells = cells[:-1]
    return [c.strip() for c in cells]


def colweights(rows, n):
    """Width weights from the content, so a column of long stored strings is
    not squeezed into the same width as a column of two-digit counts."""
    w = []
    for c in range(n):
        longest = max((len(r[c]) for r in rows if len(r) > c), default=1)
        # Weight by the width the column actually needs.  The floor keeps a
        # narrow monospace column off its rule; the cap stops one long prose
        # column from squeezing every other column to nothing.
        w.append(max(8, min(45, longest)))
    return w


def colspec(seps, weights):
    out = []
    for s, k in zip(seps, weights):
        left, right = s.startswith(':'), s.endswith(':')
        out.append('%s%d' % ('^' if left and right else
                             '>' if right else '<', k))
    return ','.join(out)


def emit_table(rows, seps, ch, anchors, out):
    n = len(seps)
    weights = colweights(rows, n)
    total = sum(weights)
    # An inline monospace run has no break point, so a cell holding one wider
    # than its column is set past the table edge and clipped.  A literal cell
    # goes through the listing environment instead, which breaks anywhere and
    # marks the continuation.  Roughly 94 monospace characters span the text
    # block at the table's body size.  dblatex allots each column
    # weight/total of (\textwidth - 2*tabcolsep per column); \textwidth is
    # 512pt, tabcolsep 6pt, and a 10pt Courier character is 6pt wide.
    spare = 512.0 - 12.0 * (n + 1)
    budget = [max(8, int(spare * k / total / 6.0) - 1) for k in weights]
    out.append('[cols="%s",options="header"]' % colspec(seps, weights))
    out.append('|===')
    for ri, cells in enumerate(rows):
        cells = list(cells) + [''] * (n - len(cells))
        parts = []
        for ci, c in enumerate(cells):
            m = CODE.fullmatch(c)
            if ri and m and (ws_significant(m.group(1)) or
                             longest_run(m.group(1)) > budget[ci]):
                # Whole-cell fixed-width evidence.  A literal cell keeps the
                # leading, trailing and interior spacing that an inline
                # monospace span would collapse.
                body = m.group(1)
                for g, name in GLYPH_AS_CODEPOINT.items():
                    body = body.replace(g, name)
                # dblatex leaks \ldots{} into verbatim; the character is the
                # notes' own elision marker, not stored data.
                body = body.replace('…', '...')
                # Still a PSV cell: a bare '|' would split the row.
                parts.append('l|' + body.replace('|', r'\|'))
            else:
                parts.append('|' + prose(c, ch, anchors, in_table=True,
                                         run_limit=budget[ci]))
        for p in parts:
            # Every cell must contribute exactly one separator, or the row
            # silently shifts into the wrong columns.
            assert len(re.findall(r'(?<!\\)\|', p)) == 1, (ch, ri, p)
        out.append(' '.join(parts))
        if ri == 0:
            out.append('')
    out.append('|===')
    out.append('')


# ------------------------------------------------------------------ main ----

def collect_anchors():
    anchors = {}
    for f, ch, _ in ORDER:
        seen = {}
        for line in open(os.path.join(SRC, f), encoding='utf-8'):
            if not line.startswith('#'):
                continue
            lvl = len(line) - len(line.lstrip('#'))
            s = slug(line[lvl:])
            n = seen.get(s, 0); seen[s] = n + 1
            key = s if not n else '%s-%d' % (s, n)
            anchors[(ch, key)] = '%s_%s' % (ch, key.replace('-', '_'))
        anchors[(ch, None)] = ch
    return anchors


def convert(f, ch, title, anchors):
    lines = open(os.path.join(SRC, f), encoding='utf-8').read().split('\n')
    out = ['//',
           '// Derived from Format/%s.  The mechanical part of the' % f,
           '// conversion is tools/md-to-adoc.py; the editorial pass that makes'
           ' the',
           '// pages read as one book is the EDITS table in that script.',
           '//', '[[%s]]' % ch, '== %s' % title, '']
    i, seen, para = 0, {}, []
    state = {'list': False}     # inside a list whose items may be continued

    def cont():
        """Attach the block about to be written to the open list item."""
        if state['list']:
            out.append('+')

    def flush():
        if para:
            cont()
            out.append(prose('\n'.join(para), ch, anchors))
            out.append('')
            del para[:]

    while i < len(lines):
        line = lines[i]

        m = FENCE.match(line)
        if m:                                             # fenced code block
            flush()
            indent, lang = m.group(1), m.group(2)
            if indent:
                cont()
            else:
                state['list'] = False
            i += 1
            body = []
            while i < len(lines) and not FENCE.match(lines[i]):
                b = lines[i]
                body.append(b[len(indent):] if b.startswith(indent) else b)
                i += 1
            i += 1
            if lang in ('c', 'html'):
                out.append('[source,%s]' % lang)
            out.append('----')
            out.extend(body)
            out.append('----')
            out.append('')
            continue

        if line.startswith('#'):                          # heading
            flush()
            state['list'] = False
            lvl = len(line) - len(line.lstrip('#'))
            text = line[lvl:].strip()
            if lvl == 1:
                i += 1                                    # H1 is the chapter
                continue
            s = slug(text)
            n = seen.get(s, 0); seen[s] = n + 1
            key = s if not n else '%s-%d' % (s, n)
            # One stray H5 in the corpus whose siblings are all H4; AsciiDoc
            # has no level below that anyway.
            out.append('[[%s]]' % anchors[(ch, key)])
            out.append('=' * min(lvl + 1, 5) + ' ' + prose(text, ch, anchors))
            out.append('')
            i += 1
            continue

        if line.lstrip().startswith('|') and '|' in line.strip()[1:]:
            flush()                                       # table
            if line.startswith('|'):
                state['list'] = False
            else:
                cont()
            rows, seps = [], None
            while i < len(lines) and lines[i].lstrip().startswith('|'):
                cells = split_row(lines[i].strip())
                if cells and all(ALIGN.match(c) for c in cells):
                    seps = cells
                else:
                    rows.append(cells)
                i += 1
            if seps is None:
                seps = ['---'] * (len(rows[0]) if rows else 1)
            emit_table(rows, seps, ch, anchors, out)
            continue

        if line.startswith('>'):                          # blockquote
            flush()
            state['list'] = False
            body = []
            while i < len(lines) and lines[i].startswith('>'):
                t = lines[i][1:]
                body.append(t[1:] if t[:1] == ' ' else t)
                i += 1
            out.append('[IMPORTANT]')
            out.append('====')
            out.append(prose('\n'.join(body), ch, anchors))
            out.append('====')
            out.append('')
            continue

        m = LIST.match(line)
        if m:                                             # list item
            flush()
            depth = 1 + len(m.group(1)) // 2
            mark = ('.' if m.group(2)[0].isdigit() else '*') * depth
            item = [m.group(3)]
            i += 1
            while (i < len(lines) and lines[i].strip()
                   and lines[i].startswith(' ')
                   and not LIST.match(lines[i])
                   and not FENCE.match(lines[i])
                   and not lines[i].lstrip().startswith('|')):
                item.append(lines[i].strip())
                i += 1
            out.append('%s %s' % (mark, prose('\n'.join(item), ch, anchors)))
            state['list'] = True
            continue

        if line.strip():
            if not para and not line.startswith(' '):
                state['list'] = False
            para.append(line.strip() if state['list'] else line)
        else:
            flush()
            out.append('')      # a blank line ends the block it follows
        i += 1
    flush()

    res, blank = [], True
    for l in out:
        if not l.strip():
            if blank:
                continue
            blank = True
        else:
            blank = False
        res.append(l)
    return '\n'.join(res).rstrip() + '\n'


# The Markdown was written one page at a time and refers to itself as a
# directory of notes.  As one book it has to speak with one voice.  Each edit
# is exact and must match once; nothing here changes a technical claim, and
# quoted stored text is deliberately left alone.
EDITS = {
    'scope': [
        ('This directory is the specification of the IBM BookManager BOO file '
         'format. It\nis meant to be complete enough for an independent '
         'implementer to build a BOO\nreader from these notes alone.',
         'This book is the specification of the IBM BookManager BOO file '
         'format. It is\nmeant to be complete enough for an independent '
         'implementer to build a BOO\nreader from it alone. This chapter '
         'states the rules the rest of the book\nfollows: what counts as '
         'evidence for a claim, which files the evidence is\ndrawn from, and '
         'how far each chapter has been verified.'),
        ('Every claim in these notes rests on',
         'Every claim in this book rests on'),
        ('Where neither can settle a point, the note says so explicitly and '
         'marks the\nstatement as an unverified hypothesis rather than '
         'stating it as fact. Analysis\nworkflow, tooling procedure, and '
         'rendering decisions belong in `AnalysisNotes/`,\nnot here.',
         'Where neither can settle a point, the text says so explicitly and '
         'marks the\nstatement as an unverified hypothesis rather than '
         'stating it as fact. Those\npoints are collected under the "Open '
         'Questions" heading of each chapter, and\neach one names the '
         'evidence that would settle it.\n\nAnalysis workflow, tooling '
         'procedure, and rendering decisions are not part of\nthe format and '
         'are not documented here.'),
        ('Byte offsets in these notes are offsets',
         'Byte offsets in this book are offsets'),
        ('=== Index', '=== Chapter Index and Verification Status'),
        ('|Topic |File |Status', '|Subject |Chapter |Evidence status'),
    ],
    'container': [
        ('This note describes the BOO file header structures',
         'This chapter describes the BOO file header structures'),
        ('this note says so.', 'this book says so.'),
    ],
    'pages': [
        ('This note documents how\na reader decides',
         'This chapter documents how\na reader decides'),
    ],
    'logical-controls': [
        ('and by this document are', 'and by this book are'),
    ],
    'markup': [
        ('the two kinds this note already describes:',
         'the two kinds this chapter already describes:'),
    ],
    'assets': [
        ('the rule this note follows throughout:',
         'the rule this book follows throughout:'),
        ('families this note documents.', 'families this book documents.'),
    ],
    'gdf': [
        ('This note documents the legacy BookManager kind `G` payload family.',
         'This chapter documents the legacy BookManager kind `G` payload '
         'family.'),
        ('appendix, this note says so explicitly',
         'appendix, this book says so explicitly'),
    ],
    'mmr': [
        ('This note documents the legacy BookManager kind `I` payload format.',
         'This chapter documents the legacy BookManager kind `I` payload '
         'format.'),
    ],
    'web-images': [
        ('This note documents image-format details',
         'This chapter documents image-format details'),
    ],
}


def editorial(text, ch):
    for old, new in EDITS.get(ch, ()):
        if text.count(old) != 1:
            sys.stderr.write('EDIT MISMATCH %s: %r x%d\n'
                             % (ch, old[:60], text.count(old)))
            continue
        text = text.replace(old, new)
    return text


def main():
    anchors = collect_anchors()
    os.makedirs(DST, exist_ok=True)
    for f, ch, title in ORDER:
        open(os.path.join(DST, '%s.adoc' % ch), 'w', encoding='utf-8').write(
            editorial(convert(f, ch, title, anchors), ch))
        print('wrote', ch)


if __name__ == '__main__':
    main()
