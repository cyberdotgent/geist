# BOO Format Notes

This directory is the canonical home for IBM BookManager BOO file-format facts.
Each topic note should separate verified observations from hypotheses and cite
fixture filenames, byte offsets, hex values, and decoded interpretations.

## Index

| Topic | File | Status |
| --- | --- | --- |
| Container header, directory page, and logical book header controls | [boo-header.md](boo-header.md) | Reader-code and fixture verified for page-0 locator, page-1/version-2 directory fields, and decoded metadata control keys. |
| Page organization | [pages.md](pages.md) | Initial findings verified against both repository BOO fixtures. |
| Compression | [compression.md](compression.md) | Stub. Compression scheme is not identified yet. |
| Encoding and tokenization | [encoding.md](encoding.md) | Stub. Character encoding is partly observed; token/control encoding is not identified yet. |
| Assets and media resources | [assets.md](assets.md) | Stub. Asset directory and payload layout are not identified yet. |
