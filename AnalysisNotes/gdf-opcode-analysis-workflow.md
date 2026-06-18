# GDF Opcode Analysis Workflow

This note records how the `IMGDF2.FLT` GDF opcode table in `Format/GDF.md` was
derived, and which upstream IBM documentation was used for the original GDDM
order names.

## Upstream Documentation Used

The relevant IBM books were found in the hosted BookManager collection:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS?Collection=Z:\var\www\html&BKCTITLE=IBM+SoftCopy+Library
```

The collection listing contains these GDDM books:

| Book name | Document | Title | Use |
| --- | --- | --- | --- |
| `QPRG1GDG` | `SC41-0536-00` | AS/400 GDDM Programming Guide | Background source for GDDM concepts. |
| `QPRG1GDR` | `SC41-0537-00` | AS/400 GDDM Programming Reference | Primary source for GDF order names, order codes, and order framing. |

The main source was `QPRG1GDR`, Appendix B, fetched through the Docker fetch
MCP from:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QPRG1GDR/CCONTENTS
```

Useful topics:

| Topic URL suffix | Topic | Why it mattered |
| --- | --- | --- |
| `/B.0` | Appendix B. GDF Order Descriptions | Establishes that GDF consists of orders generated from GDDM routines. |
| `/B.1` | Coordinates and Aspect Ratio in Comment Order | Identifies the initial comment order and data type `4` as System/370 floating point. |
| `/B.2` | List of GDF Orders | Gives the IBM/GDDM order names and code values. |
| `/B.4` | General Structure | States that a GDF file is a sequence of one-byte order codes with operand data. |
| `/B.5.1` | Normal Format | Confirms opcode, one-byte length, then operand bytes. |
| `/B.5.2` | Short Format | Confirms that high nibble `< 8` and low nibble `>= 8` means one operand byte and no length byte. |
| `/B.7` | Coordinate Data | Confirms AS/400 coordinate representation and clarifies that BookManager's type `4` fixtures are not AS/400's 2-byte coordinate form. |
| `/B.10.*`, `/B.11.*` | Individual order descriptions | Used for specific primitive and attribute names such as Line, Marker, Character, Arc, Line Relative, Pattern, and Segment orders. |
| `/B.10.3` | Area End Order | Confirms `0x60` has the same meaning as Area with the area-end bit set and uses reserved zero payload bytes. |
| `/B.10.4` | Area Order | Defines the `0x68` flag bits: start/end area and whether boundary lines are drawn. |
| `/B.11.13` | Line Order | Confirms `0xc1`/`0x81` join consecutive coordinate pairs and update current position. |
| `/B.11.15` | Line Type Order | Confirms line type `0x07` is solid and `0x08` is invisible. |
| `/B.11.1` | Character Mode Order | Identifies GDDM text precision modes. |
| `/B.11.2` | Character Order | Confirms EBCDIC string bytes, valid range `>= 0x40`, and that GDF character orders do not update current position. |
| `/B.11.3` | Character Set Order | Defines `0x38` as a one-byte local character-set id, with `0x00` default and `0x41..0xdf` user-defined sets. |
| `/B.11.4` | Character Shear Order | Defines the IBM shear vector semantics and the expected opcode `0x35`, which differs from the loaded filter's dispatched `0x36` handler. |

## Binary Analysis Path

The loaded IDA Pro MCP database was:

```text
Official Readers/Transmogrifier/IMGDF2.FLT.i64
```

IDA survey showed:

- module: `IMGDF2.FLT`
- architecture: 32-bit PE
- entrypoints: `ImportGR`, `ImportEmbeddedGR`, `ImportFile`,
  `CreateSetup`, `DestroySetup`, `EscapeSetup`
- relevant imports from `ISGDI32.DLL`: `imsOpen`, `imsReadChar`,
  `imsReadShort`, `imsReadDouble`, `imsReadStruct`, `imsSeek`, `CLine`,
  `CMarker`, `CText`, `CRestrText`, `CCellArray`, `CEllip`, `CEllipArc`, and
  many `CSet*` graphics attribute functions.

The static path was:

```text
ImportGR / ImportEmbeddedGR
  -> ImportFile
  -> sub_1C0010D0
  -> sub_1C003A40 / sub_1C003360 / sub_1C002950 header setup
  -> sub_1C0021D0 record loop
  -> sub_1C005199 record framing
  -> sub_1C0022FA opcode dispatch
```

`sub_1C005199` is the key framing function. It reads one opcode byte and then:

1. returns length zero for opcode `0x00`;
2. returns implicit length one when `(opcode >> 4) < 8` and
   `(opcode & 0x0f) >= 8`;
3. otherwise reads the next byte as the operand length.

This matched IBM `QPRG1GDR` topics `B.5.1` and `B.5.2`, confirming that the
BookManager payload after the initial comment/header is ordinary GDF order
framing.

## Handler Classification

`sub_1C0022FA` is the supported-order dispatch table. Each case was classified
by decompiling the handler and checking the ISGDI calls it made:

| Handler evidence | Classification method |
| --- | --- |
| Calls to `CLine` | Line, line-at-current, relative-line, arc fallback, or fillet fallback. |
| Calls to `CMarker` | Marker orders. |
| Calls to `CText` / `CRestrText` | Character orders. |
| Calls to `CCellArray` and `C_BeginCellArray` / `C_EndCellArray` | Graphics image begin/data/end orders. |
| Calls to `CEllip` / `CEllipArc` | Full arc, arc, or fillet orders. |
| Calls to `CSetLineType`, `CSetLineWidth`, `CSetTextFontInd`, `CSetFill*`, etc. | Attribute orders. |
| Calls to `imsSeek` and internal segment tables | Segment/replay control orders. |

The importer also writes pushed attribute state to an internal stream before
handling push-and-set orders, and opcode `0x3f` restores those attributes by
reading the saved stream backward. This explains the groups such as `0x18`
Line Type and `0x58` Push and Set Line Type.

Text/font-specific IDA findings:

| Function / data | Finding |
| --- | --- |
| `sub_1C007700` | Character orders use either explicit coordinates (`0xc3`) or current position (`0x83`), convert the string through `byte_1C018568`, then call `CText` or `CRestrText`. |
| `sub_1C004DEC` | Character-set orders scan the font-list table for the current LCID, call `CSetTextFontInd` with slot `+1`, and default to font index `1` when there is no match. |
| `sub_1C003360` | The optional GDF prolog can populate local font entries: LCID byte plus an eight-byte decoded GDDM character-set name. |
| `sub_1C0035A5` / `sub_1C003693` | Font setup builds a fallback table when no prolog list is present, then calls the same `CSetFontList` path. |
| `off_1C018158` and adjacent strings | Embedded fallback mapping from GDDM names such as `ADMDVECP`/`ADMUUTRP` to ImageMark names such as `Modern:Modern` and `Roman:Tms Rmn Bold`. |
| `Official Readers/Transmogrifier/ISGDI32.INI` | Confirms the ImageMark default font names available to the renderer, including `Swiss:Helvetica`, `Roman:Tms Rmn`, `Modern:Courier`, and related bold/italic variants. |

## Important Findings

- The BookManager fixture prefix `01 12 00 04 ...` is an initial GDF comment
  order, not a BookManager-only header. The `00 04` field selects
  System/370/IBM short hexadecimal floating-point coordinates.
- The renderer supports the IBM normal and short GDF order encodings exactly as
  documented in `QPRG1GDR`.
- The Windows `IMGDF2.FLT` importer supports text, markers, arcs, filled areas,
  cell-array images, model transforms, clipping, segment control, and pushed
  attributes in addition to lines. `libgeist` now consumes every opcode in this
  documented importer dispatch set and renders inspection-grade approximations
  for the drawable families.
- There are discrepancies between the AS/400-generated order list and this
  importer. For example, IBM lists character shear as `0x35`, character mode as
  `0x39`, and character direction as `0x3a`; the loaded importer does not
  dispatch those exact opcodes. It does dispatch `0x36`/`0x76` to a
  shear/spacing-like handler. These should remain documented as
  importer-observed behavior until matched against additional System/370 GDDM
  documentation.
- Synthetic verification lives in `libgeist/tests/gdf_synthetic.cpp`. The test
  builds a small type-2-coordinate GDF stream that emits each opcode documented
  in `Format/GDF.md`, renders it through `decode_gdf_to_rgba`, and fails if the
  result is empty or effectively blank. Passing an output path writes a BMP
  inspection artifact under `tmp/`.
- Text-rendering verification uses both static filter analysis and fixture
  output. The synthetic stream now uses EBCDIC bytes for character orders and a
  valid `0x38` local character-set id. The fixture resources in
  `GG66-3212-00.boo` render through `boorsrc --png` with legible axis/legend
  text; the visual result is compared against the same ImageMark/GDDM font path
  recovered from `IMGDF2.FLT`.
- Attempted direct validation with the historical Transmogrifier executable:
  `Official Readers/Transmogrifier/transmog.exe BOO/GG66-3212-00.boo
  tmp/transmog-gg66-3212`. Running from both the repository root and the
  Transmogrifier directory failed with Windows loader error "The specified module
  could not be found." `dumpbin /dependents` only lists `KERNEL32.dll` and
  `USER32.dll`, but binary strings show a dynamically referenced
  `CPPWRTM.DLL`, which is not included in the repository. Treat direct
  executable comparison as blocked until that historical runtime is available.
- Packet resource `2` was compared against the hosted BookServer output on
  2026-06-18 while debugging malformed local output in `render/packet/2.png`.
  The upstream image was
  `http://cbrdoc01.lan.cyber.gent/bookmgr/pictures/packet.20260614112503.P2.GIF`.
  The normal shell path could not resolve `cbrdoc01.lan.cyber.gent` before
  direct `curl` network access was approved. The fetched GIF was saved outside
  the repository as `/tmp/packet-P2-upstream.gif` and converted for inspection
  with `sips -s format png /tmp/packet-P2-upstream.gif --out
  /tmp/packet-P2-upstream.png`. The local comparison used
  `./build/boorsrc --extract BOO/packet.boo 2 /tmp/packet-P2.gdf` and
  `./build/boorsrc --png BOO/packet.boo 2 /tmp/packet-P2-fixed3.png`.
  Byte-level evidence and upstream pixel samples were recorded in
  `Format/GDF.md` under "Area Orders and Packet Resource 2 Evidence". A later
  closer comparison showed that packet labels are filled GDF areas, not visible
  construction strokes: rendering the `0x68 0x80 ... 0x60` label line orders
  directly produces vector tearing, while filling area edge segments and
  filtering the dense vector-font connector outliers matches the BookServer GIF
  much more closely.

## Repeatable Procedure

1. Use the IDA MCP `survey_binary` on the loaded `IMGDF2.FLT` database.
2. Start at `ImportFile`, then follow the parser path to the record loop.
3. Decompile `sub_1C005199` to confirm the order framing.
4. Decompile `sub_1C0022FA` to enumerate every dispatched opcode.
5. For each handler, classify behavior by its ISGDI calls and coordinate/text
   reads.
6. Fetch `QPRG1GDR` Appendix B through Docker fetch and map code values to IBM
   order names.
7. Document facts and unresolved discrepancies separately:
   - `Format/GDF.md` for byte-level GDF format facts and opcode encodings.
   - `AnalysisNotes/` for workflow, tool use, and upstream documentation
     references.
