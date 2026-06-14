# BOO Security, Encryption, Hashes, And Signatures

This page records the current analysis of security-related BOO mechanisms.

## Verified

- No encryption or decryption algorithm has been identified in the connected
  BookManager parsing library (`ephwam.dll`, SHA-256
  `bac3d7e216dd45b0d1307747f43170050da9db961967dd409f965bd9917d0406`).
- No digital-signature verifier, cryptographic hash, CRC, or checksum
  calculation has been identified in the BOO open/read path.
- The library imports no Windows crypto APIs and no compression/security
  libraries. IDA import searches found no `Crypt*`, `BCrypt*`, certificate,
  hash, signature, zlib, inflate, deflate, MD5, SHA, or CRC imports.
- IDA searches for common hash/checksum constants found no MD5, SHA-1,
  SHA-256, CRC-32, CRC-16, Adler-32, gzip, or zlib signatures in the parsing
  library.
- The logical header control `CSECURITY=` is parsed as book metadata. It is not
  evidence of encrypted content in the sampled fixtures; both bundled books
  currently decode an empty `CSECURITY` value.

## Reader-Code Evidence

| IDA name | Address | Evidence |
| --- | ---: | --- |
| `BooOpenPhysicalBookFile` | `0x1217d8d` | Opens a BOO file, reads locator/directory pages, initializes buffers, and parses the directory without any decryption or signature verification call. |
| `BooReadPhysicalPageIntoBuffer` | `0x12106ea` | Reads raw 4096-byte pages directly from disk. |
| `BooValidateBookFileTailPlaceholder` | `0x1216244` | Placeholder validation hook that returns success immediately in this build. It is called from failure/edge paths around file-tail probing but performs no hash/signature check. |
| `BooGetCurrentSummaryText` | `0x121f351` | Historical export `Scm_Getcsum`; walks logical records and copies summary text, not a checksum/hash. |

## Open Questions

- Whether other BookManager versions or platforms added security checks that
  are absent from this `ephwam.dll` build.
- Whether non-empty `CSECURITY=` values in other books affect reader UI policy
  rather than container encryption.
