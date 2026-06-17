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

### IBM GDF Order Structure

IBM documents GDF as a sequence of one-byte order codes, each followed by
operand data. The upstream reference used for the order names below is
`QPRG1GDR`, `SC41-0537-00`, "AS/400 GDDM Programming Reference", Appendix B,
"GDF Order Descriptions", fetched through the local BookServer collection at:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QPRG1GDR/B.0
```

Important IBM reference topics:

| Topic | Evidence used |
| --- | --- |
| `B.1` | Initial comment order encodes the window coordinates and data type. Data type `4` means System/370 floating point. |
| `B.2` | Lists GDF order names and code values. |
| `B.5.1` | Normal format is order code, one-byte length, then operand bytes. |
| `B.5.2` | Short format omits the length; high nibble `< 8` and low nibble `>= 8` means one operand byte. |
| `B.7` | AS/400 GDDM uses 2-byte signed coordinates, while the BookManager fixtures here use the System/370 floating-point coordinate type from the initial comment. |

The first BookManager bytes currently called the picture header are therefore
also interpretable as the initial GDF comment order. For
`GG66-3212-00.boo`, payload bytes `01 12 00 04 ...` are:

| Offset | Bytes | IBM GDF interpretation |
| ---: | --- | --- |
| `0x00` | `01` | Comment order code. |
| `0x01` | `12` | Comment payload length, 18 bytes. |
| `0x02` | `00 04` | Coordinate data type `4`: System/370 floating point. |
| `0x04..0x13` | four 4-byte values | X low, X high, Y low, Y high, decoded as IBM short HFP. |

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

## GDF Orders Supported by `IMGDF2.FLT`

The following table is the actual `IMGDF2.FLT` dispatch set observed in
`sub_1C0022FA`, with behavior checked against handler xrefs to `ISGDI32.DLL`
calls. Names come from IBM `QPRG1GDR` Appendix B where that appendix gives a
matching order name. "Renderer action" describes what this importer does; it is
not necessarily the full historical GDDM behavior.

| Opcode | IBM/GDDM name | Encoding | Renderer action / evidence |
| ---: | --- | --- | --- |
| `0x03` | Push and set character box | Normal. One coordinate pair. | Saves previous character-box state, then acts like character box. Calls `CSetCharHt`. |
| `0x07` | Not identified in IBM Appendix B list | Normal. Payload skipped after side effects. | Toggles a renderer replay mode and may seek to a recorded segment offset. |
| `0x09` | Push and set pattern | Short. One byte. | Saves previous pattern state, then acts like pattern. |
| `0x0a` | Color | Short. One byte color index. | Sets line, marker, text, edge, and sometimes fill color through `CSet*Colr`. |
| `0x0c` | Color mix | Short. One byte. | Stores a color-mix/transparency flag; no direct draw call. |
| `0x0d` | Background mix | Short. One byte. | Calls `CSetTran` and `CSetDrawMode`. IBM Appendix B lists this as accepted by AS/400 GDDM but not generated. |
| `0x10` | Text alignment / renderer text attribute | Normal. Two bytes. | Maps horizontal/vertical values and calls `CSetTextAlign`. Not listed in the AS/400 GDF order summary. |
| `0x11` | Fractional line width | Normal. Two bytes interpreted as a fixed/fractional width. | Calls `CSetLineWidth` and `CSetEdgeWidth`. IBM lists this as accepted but not generated. |
| `0x18` | Line type | Short. One byte. | Maps GDF line type to ISGDI line/edge type and calls `CSetLineType` and `CSetEdgeType`. |
| `0x19` | Line width | Short. One byte. | Converts byte to width and calls `CSetLineWidth` and `CSetEdgeWidth`. |
| `0x21` | Current position | Normal. One coordinate pair. | Reads one point, transforms it, and updates current position without drawing. |
| `0x22` | Arc parameters | Normal. Four coordinate values: `P`, `Q`, `R`, `S`. | Stores the arc transform used by later arc/full-arc orders. |
| `0x24` | Model transform | Normal. Payload begins with transform control/mask bytes, followed by selected coordinate values. | Updates the importer model transform; no direct ISGDI primitive call. |
| `0x26` | Color set extended | Normal. Uses second payload byte unless first byte is `0xff`. | Updates current color and calls the same color-setting helper used by `0x0a`. |
| `0x27` | Clip rectangle / renderer clipping attribute | Normal. Payload byte 1 is a bitmask; selected coordinates follow. | Decodes two points and calls `CSetClipRect`. Not listed in the AS/400 generated-order summary. |
| `0x28` | Pattern | Short. One byte. | Maps GDF pattern number to fill interior style/pattern/hatch/color and calls `CSetFill*`. |
| `0x29` | Marker type | Short. One byte. | Maps GDF marker number and calls `CSetMarkerType`. |
| `0x33` | Character box | Normal. One coordinate pair. | Sets character height from the decoded box vector and marks restricted text sizing. Calls `CSetCharHt`. |
| `0x34` | Character angle | Normal. One coordinate pair. | Converts vector to orientation and calls `CSetCharOri`. |
| `0x36` | Character shear-like renderer attribute | Normal. Flag byte plus one coordinate pair at payload offset 2. | Stores a text spacing/shear vector and may reset `CSetCharSpace`. IBM Appendix B names character shear as `0x35`; this importer dispatches `0x36` instead. |
| `0x38` | Character set | Short. One byte symbol-set id. | Looks up loaded font/symbol set and calls `CSetTextFontInd`. |
| `0x3f` | Pop attribute | Normal. Operand length may be zero. | Looks backward in the saved attribute stream and restores the most recent push-and-set attribute; may call `CSetCharHt`, `CSetDrawMode`, `CSetTextAlign`, `CSetLineWidth`, `CSetLineType`, `CSetMarkerType`, `CSetMarkerSize`, or `CSetTextFontInd`. |
| `0x4a` | Push and set color | Short. One byte. | Saves previous color, then acts like color. |
| `0x4c` | Push and set mix | Short. One byte. | Saves previous mix flag, then acts like color mix. |
| `0x4d` | Push and set background mix | Short. One byte. | Saves previous draw mode, then acts like background mix. |
| `0x50` | Push and set text alignment / renderer text attribute | Normal. Two bytes. | Saves previous text alignment, then acts like `0x10`. |
| `0x51` | Push and set fractional line width | Normal. Two bytes. | Saves previous fractional width, then acts like `0x11`. |
| `0x58` | Push and set line type | Short. One byte. | Saves previous line type, then acts like line type. |
| `0x59` | Push and set line width | Short. One byte. | Saves previous line width, then acts like line width. |
| `0x61` | Push and set current position | Normal. One coordinate pair. | Saves previous current position, then acts like current position. |
| `0x62` | Push and set arc parameters | Normal. Four coordinate values. | Saves previous arc parameters, then acts like arc parameters. |
| `0x64` | Push and set model transform | Normal. Same as model transform. | Saves previous transform, then acts like model transform. |
| `0x66` | Push and set extended color | Normal. Same as extended color. | Saves previous color, then acts like extended color. |
| `0x67` | Push and set clip rectangle / renderer clipping attribute | Normal. Same as clip rectangle. | Saves previous clipping state, then acts like clip rectangle. |
| `0x68` | Area | Short. One flag byte. | Starts or prepares a fill area/figure with `CBeginFigure`; toggles edge visibility and pending fill state. |
| `0x69` | Push and set marker type | Short. One byte. | Saves previous marker type, then acts like marker type. |
| `0x70` | Segment start | Normal. First four payload bytes are segment id. | Records the segment id and current stream offset in an internal table. |
| `0x71` | Segment close | Normal. Payload ignored by handler. | If replay mode is active, seeks back to the saved segment start offset. |
| `0x81` | Line at current position | Normal. Coordinate pairs, omitting initial coordinate. | Prepends current position to decoded points, calls `CLine`, then updates current position. |
| `0x82` | Marker at current position | Normal. Optional coordinate pairs after current position. | Prepends current position, calls `CMarker`, then updates current position. |
| `0x83` | Character at current position | Normal. Text bytes only. | Uses current position and calls `CText` or `CRestrText`. Text bytes are passed through the importer text conversion helper before drawing. |
| `0x85` | Fillet at current position | Normal. Two coordinate pairs after current position. | Builds a three-point curved fillet from current position and calls the shared arc/fillet routine, which falls back to `CLine` when needed. |
| `0x86` | Arc at current position | Normal. Two coordinate pairs after current position. | Uses current position plus two decoded points and calls the shared arc routine (`CEllipArc` or fallback `CLine`). |
| `0x87` | Full arc at current position | Normal. Scale/extent operand after current position. | Uses current position and arc parameters to call `CEllip`. IBM lists full arc as accepted but not generated. |
| `0x91` | Graphics image begin at current position | Normal. Starts with image size; optional second point. | Initializes a cell-array image using current position as origin. |
| `0x92` | Graphics image data | Normal. Row/image bytes. | Copies or bit-expands image data into the current cell-array buffer. |
| `0x93` | Graphics image end | Normal. No required payload observed. | Emits the buffered image through `C_BeginCellArray`, `CCellArray`, and `C_EndCellArray`, then frees the buffer. |
| `0xa1` | Line relative at current position | Normal. Signed byte delta pairs. | Prepends current position, applies signed one-byte deltas, calls `CLine`, then updates current position. |
| `0xc1` | Line | Normal. Coordinate pairs. | Decodes absolute point list, calls `CLine` when drawable, then updates current position. |
| `0xc2` | Marker | Normal. Coordinate pairs. | Decodes absolute point list, calls `CMarker`, then updates current position. |
| `0xc3` | Character | Normal. Coordinate pair followed by text bytes. | Uses first point as text origin, then calls `CText` or `CRestrText`. |
| `0xc5` | Fillet | Normal. Three coordinate pairs. | Calls the shared arc/fillet routine, which can emit `CEllipArc` or fall back to `CLine`. |
| `0xc6` | Arc | Normal. Three coordinate pairs. | Calls the shared arc routine using arc parameters; emits `CEllipArc` or fallback `CLine`. |
| `0xc7` | Full arc | Normal. Coordinate pair plus scale/extent operand. | Uses explicit center/current point and arc parameters to call `CEllip`. IBM lists full arc as accepted but not generated. |
| `0xd1` | Graphics image begin | Normal. Coordinate pair, image size, optional second point. | Initializes a cell-array image using explicit origin. |
| `0xe1` | Line relative | Normal. Initial coordinate pair followed by signed byte delta pairs. | Decodes an absolute start point, applies signed one-byte deltas, calls `CLine`, then updates current position. |

### Supported-Set Discrepancies

The importer is not a byte-for-byte implementation of the AS/400 generated-order
subset listed in IBM `QPRG1GDR` Appendix B:

- IBM lists `0x35` as Character Shear, `0x39` as Character Mode, and `0x3a` as
  Character Direction. These exact opcodes are not dispatched by the loaded
  `IMGDF2.FLT`; unknown records with those opcodes are skipped by length.
- `IMGDF2.FLT` dispatches `0x36` and `0x76` to a character-shear/spacing-like
  handler even though the AS/400 appendix names character shear as `0x35`.
- The importer handles several IBM "accepted but not generated" orders, such as
  `0x0d`, `0x11`, `0x21`, `0x87`, `0xa1`, `0xc7`, and `0xe1`.
- Some renderer attributes (`0x10`/`0x50`, `0x27`/`0x67`, and `0x07`) are
  implemented by this importer but are not part of the generated-order summary
  in the AS/400 appendix. Treat their names as renderer-action names until they
  are matched against additional GDDM/System/370 documentation.

Resource 1 in `GG66-3212-00.boo` contains many non-coordinate records,
including text/control records. A simple byte scanner misidentifies these as
coordinate runs and draws false long lines. The record parser avoids that by
using IBM's GDF order framing and skipping unsupported records by their
declared length.

## Current Rendering Scope

Current `libgeist` PNG rendering support for `legacy-gdf` is an approximate
rasterizer for the complete `IMGDF2.FLT` dispatch set documented above. The
decoder parses the initial comment/header, selects 2-byte signed or 4-byte IBM
short HFP coordinates, walks IBM normal/short framed orders, and handles every
documented importer opcode.

Implemented drawing behavior:

| Order family | `libgeist` rendering behavior |
| --- | --- |
| Current-position, attribute, transform, clipping, segment, and push/pop orders | Parsed and reflected in renderer state where the current rasterizer uses the state; otherwise consumed so later orders remain aligned. |
| Line and relative-line orders | Rendered as polylines with the current color. |
| Marker orders | Rendered as simple cross/star marker shapes. |
| Character orders | Rendered as placeholder glyph boxes at the decoded text origin; text bytes are not yet converted with the IBM reader's full font/text path. |
| Fillet, arc, and full-arc orders | Rendered as approximate elliptical polylines. |
| Area orders | Collects points while an area is active and fills the resulting polygon with the current fill style. |
| Graphics image begin/data/end orders | Renders a simple cell-array block from the buffered image bytes. |

This is sufficient for inspection-oriented rendering and for visual recognition
of BookManager GDF assets without invoking the historical
`IMGDF2.FLT`/`EBGIF2.FLT` filter chain. It is not a pixel-exact clone of the IBM
ImageMark/GDDM renderer: exact text shaping, fonts, clipping, transforms,
fill-pattern semantics, arc geometry, segment replay behavior, and cell-array
color interpretation remain intentionally approximate until more fixture-backed
evidence is available.
