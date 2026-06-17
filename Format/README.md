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
| Topic and documentation page storage | [topics.md](topics.md) | Reader-code and fixture verified for topic-start indexes, topic bounds, `SH` topic ids, `CTOPICN`, and TOC id-to-topic resolution. |
| Table of contents | [table-of-contents.md](table-of-contents.md) | Reader-code, fixture, and hosted-BookServer verified for topic-start indexes, topic header controls, `CTOCDEF`, and literal `CTOCE` entries in the `CONTENTS` topic. |
| Decoded markup and controls | [markup.md](markup.md) | Reader-code, fixture, and historical-source verified for GML-derived control names, topic headings, TOC controls, font/highlight spans, links, figures, picture references, and layout controls; exact renderer styling remains partly open. |
| Compression | [compression.md](compression.md) | Reader-code verified: no standard compression layer identified in the BOO page-read path; binary-looking content is currently tokenized/packed BookManager data. |
| Encoding and tokenization | [encoding.md](encoding.md) | Reader-code and fixture verified for dictionary literal byte-to-token-word mapping, token-word translation tables, and the main output-decoding path; full code-page matrix remains open. |
| Security, encryption, hashes, and signatures | [security.md](security.md) | Reader-code verified: no encryption, signature, cryptographic hash, CRC, or checksum algorithm identified in the sampled BOO open/read path. |
| Assets and media resources | [assets.md](assets.md) | Reader-code, Transmogrifier-code, and fixture verified for pre-directory embedded image resources, version 1.2/1.3 legacy picture descriptors, version 1.4 converted object descriptor groups, absolute payload offsets, and the BookServer/ImageMark GIF conversion boundary. |
| Legacy GDF image payloads | [GDF.md](GDF.md) | Reader-code and fixture verified for kind `G` dispatch and current fixture-driven rendering limits. |
| Legacy MMR image payloads | [MMR.md](MMR.md) | Reader-code and fixture verified for kind `I` dispatch, observed wrapper fields, first compressed-segment framing, and IBM reference rendering. |
| Version 1.4 web image payloads | [WebImages.md](WebImages.md) | Transmogrifier-code verified for converted object payload image extensions and dimension extraction behavior. |
