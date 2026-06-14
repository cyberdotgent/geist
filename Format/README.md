# BOO Format Notes

This directory is the canonical home for IBM BookManager BOO file-format facts.
Each topic note should separate verified observations from hypotheses and cite
fixture filenames, byte offsets, hex values, and decoded interpretations.

## Index

| Topic | File | Status |
| --- | --- | --- |
| Container header, directory page, and logical book header controls | [boo-header.md](boo-header.md) | Reader-code and fixture verified for page-0 locator, page-1/version-2 directory fields, and decoded metadata control keys. |
| Logical header-control storage | [logical-controls.md](logical-controls.md) | Reader-code, fixture, and hosted-BookServer verified for tokenized logical-record storage, record framing, token references, dictionary delta transforms, translation-table decoding, and decoded control keys; logical-record spacing controls still need complete field-level documentation. |
| Page organization | [pages.md](pages.md) | Initial findings verified against both repository BOO fixtures. |
| Compression | [compression.md](compression.md) | Stub. Compression scheme is not identified yet. |
| Encoding and tokenization | [encoding.md](encoding.md) | Reader-code and fixture verified for dictionary literal byte-to-token-word mapping, token-word translation tables, and the main output-decoding path; full code-page matrix remains open. |
| Assets and media resources | [assets.md](assets.md) | Stub. Asset directory and payload layout are not identified yet. |
