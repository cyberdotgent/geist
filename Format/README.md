# BOO Format Notes

This directory is the specification of the IBM BookManager BOO file format. It
is meant to be complete enough for an independent implementer to build a BOO
reader from these notes alone.

## Evidence Rules

Every claim in these notes rests on one of two kinds of evidence, and says which:

- **Fixture** -- a `.BOO` file in this repository, cited by filename plus the
  byte offset, record number, topic id, or decoded control that shows the claim.
- **Hosted rendered output** -- what the hosted BookServer service serves for a
  named book, topic, and `DT` timestamp. This is observable behaviour of a
  running service and is used to confirm how a stored construct is meant to be
  presented.

Where neither can settle a point, the note says so explicitly and marks the
statement as an unverified hypothesis rather than stating it as fact. Analysis
workflow, tooling procedure, and rendering decisions belong in `AnalysisNotes/`,
not here.

Byte offsets in these notes are offsets **within a `.BOO` file** unless the text
says otherwise.

## Index

| Topic | File | Status |
| --- | --- | --- |
| Container header, directory page, and logical book header controls | [boo-header.md](boo-header.md) | Fixture verified for the page-0 locator, the version-2 directory fields, the picture-directory version bytes, and the decoded metadata control keys. Several fixed directory scalars have a confirmed position but no identified meaning. |
| Logical header-control storage | [logical-controls.md](logical-controls.md) | Fixture and hosted verified for tokenized logical-record storage, record framing, token references, dictionary delta transforms, translation-table decoding, and decoded control keys. Logical-record spacing controls still need complete field-level documentation. |
| Page organization | [pages.md](pages.md) | Fixture verified. |
| Topic and documentation page storage | [topics.md](topics.md) | Fixture verified for topic-start indexes, topic bounds, `SH` topic ids, `CTOPICN`, and TOC id-to-topic resolution. |
| Table of contents | [table-of-contents.md](table-of-contents.md) | Fixture and hosted verified for topic-start indexes, topic header controls, `CTOCDEF`, and literal `CTOCE` entries in the `CONTENTS` topic. |
| Decoded markup and controls | [markup.md](markup.md) | Fixture, hosted, and historical-source verified for GML-derived control names, topic headings, TOC controls, font/highlight spans, links, figures, picture references, and layout controls. Exact renderer styling remains partly open. |
| Compression | [compression.md](compression.md) | Fixture verified: the container applies no compression; body text is tokenized, not entropy coded. |
| Encoding and tokenization | [encoding.md](encoding.md) | Fixture and hosted verified for dictionary literal byte-to-token-word mapping, token-word translation tables, and the output decoding path. The full code-page matrix remains open. |
| Security, encryption, hashes, and signatures | [security.md](security.md) | Fixture verified: no encryption, signature, hash, CRC, or checksum anywhere in the container. |
| Assets and media resources | [assets.md](assets.md) | Fixture verified for pre-directory embedded image resources, the three picture-directory versions and their descriptor groups, absolute payload offsets, and version 1.4 converted-object descriptions. Hosted output establishes the stored-versus-rendered boundary. |
| Legacy GDF image payloads | [GDF.md](GDF.md) | Fixture verified for kind `G` framing and coordinate encoding; order names come from published IBM GDDM documentation read through the hosted BookServer. Orders that documentation does not name are marked unverified. |
| Legacy MMR image payloads | [MMR.md](MMR.md) | Fixture verified for kind `I` dispatch, wrapper fields, and segment-chain framing; hosted GIFs used for pixel-level validation. |
| Version 1.4 web image payloads | [WebImages.md](WebImages.md) | Fixture verified for GIF objects in the two version-1.4 fixtures. Other MIME types are expressible but unverified. |
