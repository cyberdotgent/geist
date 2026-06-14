# BOO Encoding and Tokenization

This page is a stub for character encoding, tokenization, and control-byte
mechanisms in BOO files.

## Verified

- Text observed in the page-0 and page-1 headers decodes as EBCDIC code page
  037.
- Some `0x0100` pages contain visible EBCDIC words mixed with binary control
  bytes.
- Logical book header controls are stored as tokenized logical records, not as
  raw ASCII or EBCDIC strings. See
  [logical-controls.md](logical-controls.md).

See [boo-header.md](boo-header.md) for the current header and page-run
evidence.

## Open Questions

- Whether body text uses EBCDIC CP037 throughout or switches code pages.
- Complete dictionary-page delta/update grammar for resolving every tokenized
  logical record.
- Whether visible words in `0x0100` pages are raw dictionary text, index keys,
  or mixed token-table entries.
