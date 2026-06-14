# BOO Format Notes

This directory is the canonical home for IBM BookManager BOO file-format facts.
Each topic note should separate verified observations from hypotheses and cite
fixture filenames, byte offsets, hex values, and decoded interpretations.

## Index

| Topic | File | Status |
| --- | --- | --- |
| Container header and page-1 directory | [boo-header.md](boo-header.md) | Initial findings verified against both repository BOO fixtures. |
| Page organization | [pages.md](pages.md) | Stub. Known facts currently limited to 4096-byte pages and observed page-class runs. |
| Compression | [compression.md](compression.md) | Stub. Compression scheme is not identified yet. |
| Encoding and tokenization | [encoding.md](encoding.md) | Stub. Character encoding is partly observed; token/control encoding is not identified yet. |
| Assets and media resources | [assets.md](assets.md) | Stub. Asset directory and payload layout are not identified yet. |
