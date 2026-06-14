# BOO Compression

This page is a stub for compression mechanisms in BOO files.

## Verified

- No compression algorithm has been identified yet.
- Several `0x0000` page-class runs contain mostly binary/compressed-looking data.

See [boo-header.md](boo-header.md) for the page-run evidence.

## Open Questions

- Whether body text or indexes are compressed.
- Whether different page classes use different compression schemes.
- Whether the binary-looking `0x0000` page runs are compressed data, indexes,
  or another packed structure.
- Which reader functions decompress page payloads.
