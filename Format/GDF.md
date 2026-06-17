# Legacy GDF / Kind `G` Image Payloads

This note documents the legacy BookManager kind `G` payload family. The BOO
container stores these payloads as byte ranges referenced by the page-0 picture
directory; the container descriptor layout is documented in [assets.md](assets.md).

## Reader Evidence

The Transmogrifier utility in `Official Readers/Transmogrifier/transmog.exe`
identifies kind `G` as a GDF picture converted to GIF. IDB-backed conversion
evidence shows that `TransmogConvertGdfToGif` copies the source payload to a
temporary file, then `TransmogRunGdfImportGifExport` loads `ISGDI32.DLL`,
`IMGDF2.FLT`, and `EBGIF2.FLT` and calls the ImageMark import/export path.

The BookServer `ephimage.dll` path follows the same filter stack for legacy
kind `G` payloads: `EphImageConvertLegacyPicture` loads `isgdi32.dll`, then
`imgdf2.flt` for import and `ebgif2.flt` for GIF export. `IMGDF2.FLT` exports
`ImportGR`, which dispatches into `ImportFile` and then the GDF record parser.

Local fixture verification found kind `G` descriptors:

| Kind byte | Example fixture | First verified descriptor and payload evidence |
| ---: | --- | --- |
| `0xc7` / `G` | `GG66-3212-00.boo` | Descriptor at `0x0118`: `f1 40 40 40 40 40 40 40 c7 00 4e 64 00 00 01 58`; payload at `0x0158` begins `01 12 00 04 00 00 00 00 42 64 00 01 00 00 00 00...`. |

## Payload Framing

`IMGDF2.FLT` shows that the stored kind `G` payload is parsed as a GDF record
stream, not as arbitrary coordinate data.

### IBM Short Hexadecimal Floating Point

Observed 4-byte GDF coordinates and header extents use IBM short hexadecimal
floating point, stored big-endian:

| Byte/bit range | Meaning |
| --- | --- |
| Byte 0, bit 7 | Sign. `0` is positive, `1` is negative. |
| Byte 0, bits 6..0 | Base-16 exponent with excess-64 bias. |
| Bytes 1..3 | 24-bit unsigned fraction. |

The decoded value is:

```text
sign * (fraction / 0x01000000) * 16^(exponent - 64)
```

where `sign` is `+1` or `-1`. The IBM importer returns `0.0` when the
24-bit fraction is zero.

This is the IBM System/360-family hexadecimal floating-point format, later
called HFP by IBM. The GDF payloads use the 32-bit/single-precision member of
that family, which IBM documentation calls the short format. It is an upstream
machine data format, not a BookManager-specific floating-point encoding and not
an IEEE 754 `binary32`/C `float`.

Differences from the common IEEE 754 single-precision float:

| Property | IBM short HFP used here | IEEE 754 binary32 |
| --- | --- | --- |
| Total size | 32 bits | 32 bits |
| Byte order in observed GDF payloads | Big-endian | Format does not require a file byte order; memory byte order is platform-specific. |
| Sign | 1 bit | 1 bit |
| Exponent | 7-bit base-16 characteristic, bias 64 | 8-bit base-2 exponent, bias 127 |
| Significand/fraction storage | 24 explicit fraction bits with the radix point to the left of the fraction | 23 stored fraction bits plus an implicit leading `1` for normal values |
| Value radix | `16` | `2` |
| Finite-value formula | `(-1)^sign * 0.fraction_hex * 16^(characteristic - 64)` | `(-1)^sign * 1.fraction_bin * 2^(exponent - 127)` for normal values |
| Special encodings | No IEEE-style infinities, NaNs, or subnormal class in the observed coordinate decoder; zero is a zero fraction | Exponent fields `0x00` and `0xff` encode zero/subnormal and infinity/NaN classes |
| Effective precision | Six hexadecimal fraction digits. In binary terms this can wobble because base-16 normalization can leave up to three leading zero bits in the binary significand. | 24 binary significant bits for normal values |

Implementation notes:

1. Read the four bytes as one big-endian 32-bit word.
2. If the 24-bit fraction field is zero, return `0.0`.
3. Let `sign` be negative when bit 31 is set.
4. Let `characteristic = (word >> 24) & 0x7f`.
5. Let `fraction = word & 0x00ffffff`.
6. Decode as:

```text
(-1 if sign else +1) * (fraction / 16777216.0) *
    16^(characteristic - 64)
```

Equivalently, `16^n` can be evaluated as `2^(4*n)`.

The observed BookManager/GDF payloads do not show a deviation from standard IBM
short HFP for 4-byte coordinates. The only GDF-specific rule observed so far is
where the values are stored: the picture header and 4-byte coordinate record
fields use this format, while a header length of `0x02` selects a separate
2-byte coordinate path in `IMGDF2.FLT`.

Examples from `GG66-3212-00.boo` resource 1:

| Bytes | Decoded value |
| --- | ---: |
| `00 00 00 00` | `0.0` |
| `42 64 00 01` | `100.000015258789` |
| `42 64 00 06` | `100.000091552734` |

### Picture Header

The stream begins with a variable-length header:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| `0x00` | 1 | Header introducer. Observed value `0x01`; `IMGDF2.FLT` rejects other values. |
| `0x01` | 1 | Header payload length in bytes. `GG66-3212-00.boo` resources 1 and 2 use `0x12`. |
| `0x02` | 2 | Header fields not yet identified. For the observed resources this is `00 04`. |
| `0x04` | 4 | Minimum X as IBM short hexadecimal floating point. |
| `0x08` | 4 | Maximum X as IBM short hexadecimal floating point. |
| `0x0c` | 4 | Minimum Y as IBM short hexadecimal floating point. |
| `0x10` | 4 | Maximum Y as IBM short hexadecimal floating point. |

For `GG66-3212-00.boo` resource 1, bytes `00 00 00 00 42 64 00 01 00 00 00 00
42 64 00 06` at payload-relative offsets `0x04..0x13` decode to an extent of
approximately `x=0..100.000015`, `y=0..100.000092`.

`IMGDF2.FLT` chooses the coordinate decoder from the header length. A length of
`0x02` selects a 2-byte path; other observed lengths select a 4-byte IBM
hexadecimal-floating path. The verified BookManager fixtures use the 4-byte
path.

### Record Header

Records begin at payload-relative offset `2 + header_length`. The importer
reads a one-byte opcode and then determines payload length as follows:

| Opcode condition | Payload length rule |
| --- | --- |
| Opcode `0x00` | Length is zero. |
| High nibble `< 8` and low nibble `>= 8` | Length is implicitly one byte. |
| All other non-zero opcodes | Next byte is an unsigned payload length. |

The payload immediately follows the optional length byte. Unknown records can
therefore be skipped by this length rule.

## Verified Drawing Opcodes

The following opcodes are implemented in `libgeist` because their behavior was
verified against `IMGDF2.FLT` decompilation:

| Opcode | IBM filter handler evidence | Observed behavior |
| ---: | --- | --- |
| `0x21`, `0x61` | `sub_1C007B82`, dispatched for opcodes `33` and `97` | Reads one coordinate pair and updates the current point without drawing. |
| `0x81` | `sub_1C007A2A`, dispatched for opcode `-127` | Reads absolute coordinate pairs, prepends the current point, calls `CLine`, then updates the current point to the last pair. |
| `0xc1` | `sub_1C0078C0`, dispatched for opcode `-63` | Reads absolute coordinate pairs and calls `CLine` when more than one point is present. |
| `0xe1` | `sub_1C007C05`, dispatched for opcode `-31` | Reads one absolute coordinate pair followed by signed byte delta pairs and calls `CLine`. |

Resource 1 in `GG66-3212-00.boo` contains many non-coordinate records,
including text/control records. A simple byte scanner misidentifies these as
coordinate runs and draws false long lines. The record parser avoids that by
only drawing verified line opcodes and skipping unsupported records by their
declared length.

## Current Rendering Scope

Current `libgeist` PNG rendering support for `legacy-gdf` is fixture-driven and
limited to observed vector-style GDF payloads. The decoder parses the stream
header, uses the declared extent as the viewport, walks opcode/length-framed
records, and rasterizes the verified line opcodes as black polylines.

This renders the verified `GG66-3212-00.boo` GDF resources without invoking the
historical `IMGDF2.FLT`/`EBGIF2.FLT` filter chain. It does not yet implement the
complete GDDM command grammar, color/style attributes, filled areas, or exact
text/font semantics.
