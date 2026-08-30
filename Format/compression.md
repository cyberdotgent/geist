# BOO Compression

This page records what is and is not compressed in a BOO file. Evidence is the
`.BOO` fixtures in this repository plus rendered output from the hosted
BookServer service.

## Verified

- BOO files are a whole number of 4096-byte physical pages, and page content is
  addressed arithmetically, not through a compressed-stream index. A logical
  page maps to a file offset as
  `(directory_page + logical_page - 1) * 4096`, verified for every fixture in
  [boo-header.md](boo-header.md) and [pages.md](pages.md). There is no
  compressed-block table, no per-page compressed length, and no page-level
  transform to undo before a page can be parsed.
- No general-purpose compressed stream is present. Scanning the start of all
  8113 physical pages and all 623 resource payloads across the repository
  fixtures found no gzip, zip, bzip2, or `compress` container signature. The
  six weak two-byte zlib-header coincidences that such a scan produces all fall
  inside legacy image payload areas whose framing is fully accounted for by
  [GDF.md](GDF.md) and [MMR.md](MMR.md).
- Logical text is *tokenized*, not compressed. A decoder reconstructs the full
  visible text of every fixture using only the structural rules documented in
  [logical-controls.md](logical-controls.md) and [encoding.md](encoding.md):
  compact record lengths, token references resolved through the directory token
  map, dictionary base records with delta/update records, translation-table
  pages, and inter-token spacing rules. Every one of those steps is a table or
  index lookup. None is entropy decoding.
- The result is verifiable end to end: decoded topic text matches the text the
  hosted BookServer renders for the same book and topic, for example
  `packet.boo` topic `1.0` and `QSYSNEWG.BOO` topic `PREFACE`.
- The binary-looking `0x0000`-heavy runs inside content pages are therefore
  packed BookManager token structures, not a compressed byte stream. They are
  dense because a common word costs one or two bytes as a token reference.
- Embedded legacy image payloads carry their own, image-specific compression --
  CCITT fax coding for kind `I` payloads, see [MMR.md](MMR.md) -- but that
  compression belongs to the image payload, not to the BOO container. The
  container stores the payload bytes verbatim at an absolute file offset.

See [boo-header.md](boo-header.md) for the page-run evidence.

## Open Questions

- Whether any page class outside the paths exercised by the bundled fixtures
  uses a separate packed representation.
- Complete independent documentation of all body-text and index tokenization
  variants.
