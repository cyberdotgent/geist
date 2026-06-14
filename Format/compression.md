# BOO Compression and Encoded Content

This page is a stub for compression, tokenization, and other encoded content
mechanisms in BOO files.

## Verified

- No compression algorithm has been identified yet.
- Several `0x0000` page-class runs contain mostly binary/compressed-looking data.
- Some `0x0100` pages contain visible EBCDIC words mixed with binary control
  bytes.

See [boo-header.md](boo-header.md) for the page-run evidence.

## Open Questions

- Whether body text is compressed, tokenized, indexed, or a mix of these.
- Whether different page classes use different encodings.
- Which reader functions decode page payloads.
