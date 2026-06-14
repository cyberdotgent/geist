# BOO Encoding and Tokenization

This page is a stub for character encoding, tokenization, and control-byte
mechanisms in BOO files.

## Verified

- Text observed in the page-0 and page-1 headers decodes as EBCDIC code page
  037.
- Some `0x0100` pages contain visible EBCDIC words mixed with binary control
  bytes.

See [boo-header.md](boo-header.md) for the current header and page-run
evidence.

## Open Questions

- Whether body text uses EBCDIC CP037 throughout or switches code pages.
- Whether visible words in `0x0100` pages are raw text, dictionary terms,
  index keys, or token-table entries.
- Meaning of the binary control bytes surrounding visible EBCDIC words.
- Which reader functions translate encoded records into displayable text.
