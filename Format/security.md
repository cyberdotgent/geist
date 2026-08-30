# BOO Security, Encryption, Hashes, And Signatures

This page records what the BOO container itself does, and does not, contain in
the way of encryption, integrity, and authenticity mechanisms. Every statement
below is derived from the `.BOO` fixtures in this repository and from rendered
output of the hosted BookServer service.

## Verified

- No encrypted region has been found in any repository fixture. Every byte range
  that the documented container structures account for -- page 0, the directory
  page, dictionary pages, content pages, and the pre-directory resource area --
  decodes with the plain structural rules in [boo-header.md](boo-header.md),
  [logical-controls.md](logical-controls.md), [encoding.md](encoding.md), and
  [assets.md](assets.md). Decoding a book requires no key, password, or
  book-specific parameter beyond the values stored in its own header.
- No checksum, CRC, cryptographic hash, or digital signature field has been
  identified. Editing a fixture byte and re-decoding it changes only the decoded
  text at that position; nothing in the container detects the change, and no
  header field varies with content in a way consistent with a checksum.
- Container integrity is structural only: page counts, the record-length chain,
  the topic-start index, and the resource descriptors are mutually redundant, so
  a corrupted file usually fails a bounds or length check rather than a
  cryptographic one.
- Legacy image payloads are stored as plain byte ranges at absolute file offsets
  and are not obfuscated. Verified across the kind `G` and kind `I` fixtures
  listed in [assets.md](assets.md).
- The logical header control `CSECURITY=` is book metadata. It is not evidence of
  encrypted content:

  | Fixture | Decoded `CSECURITY` value | Content still plainly decodable |
  | --- | --- | --- |
  | All fixtures except `XWEBDEMO.boo` | empty | yes |
  | `XWEBDEMO.boo` | `©IBM Corporation 1995, 1997` | yes -- for example topic `1.0` decodes to the same prose the hosted BookServer serves at `DT=19970423182524` |

  A non-empty `CSECURITY` value therefore does not change the container
  encoding at all.

## Implementer Guidance

A BOO reader needs no cryptographic code. It should validate structurally:
reject a directory page number outside the file, reject logical page numbers
outside the directory's page-count fields, reject a resource descriptor whose
`offset + length` overruns the directory page, and reject a record length that
runs past the end of its page.

## Open Questions

- Whether other BookManager versions or platforms added a security mechanism
  that no fixture in this repository exercises. Nothing in the observed
  container reserves space for one.
- Whether a non-empty `CSECURITY=` value affects reader access policy rather
  than storage. The hosted BookServer serves `XWEBDEMO` without any
  authorization step, so no enforcement is observable there.
- `XWEBDEMO.boo` decodes an empty `CCOPYR` control and a copyright-shaped
  `CSECURITY` value. Whether the book genuinely stores its copyright in the
  security control, or whether the two control keys are being distinguished
  incorrectly, is not yet settled by fixture evidence.
