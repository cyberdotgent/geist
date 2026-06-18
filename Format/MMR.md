# Legacy MMR / Kind `I` Image Payloads

This note documents the legacy BookManager kind `I` payload format. The BOO
container stores these payloads as byte ranges referenced by the page-0 picture
directory; the container descriptor layout is documented in [assets.md](assets.md).

## Current Implementation Status

MMR support is implemented in `libgeist` for the observed legacy kind `I`
ImageMark wrapper and the CCITT fax line streams verified against hosted
BookServer output. The public PNG renderer treats `legacy-mmr` as a renderable
format, the same way it treats `legacy-gdf`, while still reporting unsupported
or not-yet-mapped fax variants as clear render errors.

The implementation is self-contained. It uses the standard CCITT T.4/T.6
white/black run tables and 2D modes, with wrapper handling derived from the IBM
reader IDBs and verified against BookServer output. It does not embed or copy
libtiff source.

## Reader Evidence

The attached BookServer and Transmogrifier IDBs establish that legacy kind `I`
payloads use the reader's MMR path rather than the GDF filter path:

| Binary / IDB | Evidence |
| --- | --- |
| `Official Readers/BookSrv-Win32/bookmgr.exe.i64` | `BookServerServePictureObject` delegates legacy picture conversion through the imported `ephimage` path. The byte-level MMR decoder is not in `bookmgr.exe`. |
| `Official Readers/BookSrv-Win32/ephimage.dll` | Local symbol/string evidence includes `process_mmr_pict`, `InitDecompress`, `WRcheckparms`, `WRraster`, `WRruns`, `dinitmmr`, `dlinemmr`, `decline`, `deceol`, `readcd`, and `writere`. |
| `Official Readers/Transmogrifier/transmog.exe` | Debug/object strings include `D:\Transmogrifier\source\ephdmmr.obj`, `_dinitmmr`, `_dlinemmr`, `_decline`, `_dlineabic`, and `?lConvertMMRtoGIF0:`. |
| `Official Readers/Transmogrifier/transmog.exe.i64` | The converted-output path reads legacy kind `I` bytes, calls the MMR writer, and emits a GIF object in the rewritten version 1.4 book. |

`Official Readers/BookSrv-Win32/ephimage.dll` exports the relevant entry points
used for harnessing and comparison:

| Export | RVA | Verified role |
| --- | ---: | --- |
| `ephimage` | `0x000107e4` | High-level conversion entry. Arguments match `argv`: book path, picture id, output GIF path, optional `/NOSCALE`. |
| `ConvertPicture__FPcclT3PP6__filei` | `0x00010160` | Reads a legacy descriptor payload and dispatches by kind byte. |
| `process_mmr_pict` | `0x00011018` | Kind `I` MMR/ImageMark conversion path. |
| `InitDecompress` | `0x00010fa0` | Initializes the decompressor state with image width. |
| `ecline` / `decline_main` | `0x00022db5` / `0x00022dd1` | Main line-decompression loop. |
| `dinitmmr` / `dlinemmr` | `0x00023f95` / `0x00024069` | MMR-family state setup and table-driven line decoder. |
| `writere`, `WRraster`, `WRruns` | `0x000233aa`, `0x000257e0`, `0x00025b91` | Convert decoded transition runs into the packed monochrome output raster. |

## Observed Payload Wrapper

Observed kind `I` payloads contain an ImageMark-style wrapper before the MMR
compressed bitmap data. Resource `1` in `GG24-4302-00.boo` is the clearest
fixture:

| Relative offset in payload | Bytes / value | Current interpretation |
| ---: | --- | --- |
| `0x00` | `00 08 d3 a8 7b` | Wrapper record prefix; `d3 a8 7b` is EBCDIC-like `Ly{`. |
| `0x0a` | `d3 a7 7b` | Second wrapper marker; EBCDIC-like `Lx{`. |
| `0x2e` | `09 60 09 60` | Repeated value observed before dimensions; likely source-unit metadata, unresolved. |
| `0x32` | `00 64` | Unresolved metadata value. Earlier notes mistook this field for width; `QSYSNEWG` and `GG24-4302-00` both show it as `100`. |
| `0x34` | `00 64` | Unresolved metadata value. Earlier notes mistook this field for height; `QSYSNEWG` and `GG24-4302-00` both show it as `100`. |
| `0x3a` | `d3 ee 7b 40` | Additional wrapper marker before bitmap payload metadata. |
| `0x42` | `03 c0` | Bitmap width: `960`. Confirmed by Transmogrifier trace behavior and rendered output. |
| `0x44` | `03 40` | Bitmap height: `832`. Confirmed by Transmogrifier trace behavior and rendered output. |
| `0x46` | `01 00` | Unresolved flag or depth-like field. |
| `0x48` | `1c ac` | First compressed segment record length. The reader byte-swaps this word, subtracts `8`, and passes that byte count to the decompressor. |
| `0x50` | `00 1b 50 d4...` | First byte consumed by the decompressor for the first segment. |

The corresponding legacy descriptor for that payload is:

```text
GG24-4302-00.boo
descriptor 0x0118:
f1 40 40 40 40 40 40 40 c9 00 1c fc 00 00 99 f0

id "1", kind I, length 0x001cfc, absolute payload offset 0x000099f0
```

Another kind `I` fixture, `GG66-3212-00.boo` resource `3`, has the same wrapper
shape but different dimensions:

| Relative offset in payload | Bytes / value | Current interpretation |
| ---: | --- | --- |
| `0x00` | `00 08 d3 a8 7b` | Same wrapper prefix. |
| `0x2e` | `09 60 09 60` | Same unresolved repeated value. |
| `0x42` | `03 40` | Candidate bitmap width: `832`. |
| `0x44` | `01 80` | Candidate bitmap height: `384`. |
| `0x48` | `06 74` | Candidate first segment record length. This equals descriptor length `0x06c4` minus the `0x50` bytes before compressed data; the segment record length itself includes the 8-byte segment header. |

## Segment Framing

IDA decompilation of `TransmogConvertMmrBufferToGif` verifies the first segment
framing:

1. Copy the 32-byte image header from payload-relative `0x28`.
2. Use the big-endian bitmap width and height at payload-relative `0x42` and
   `0x44` (`0x1a` and `0x1c` inside the copied image header).
3. Set the first segment header pointer to payload-relative `0x48`.
4. Read the big-endian segment length word at `0x48`, subtract `8`, and store
   that as the compressed byte count.
5. Set the decompressor input pointer to payload-relative `0x50`.
6. Call `ecline` repeatedly. Status `0x2000` advances to the next segment by
   adding the previous compressed byte count plus the 8-byte segment header.
7. Invert the completed bitmap vertically with `gbm_ref_vert`, then write GIF
   output with `gif_w`.

Multiple compressed segment records can follow each other. The next record
starts at `previous_segment_header + segment_length`; its compressed data starts
8 bytes later.

## Fax Coding

The compressed segment body uses CCITT fax run-length coding. The coding is
compatible with the T.4/T.6 white and black terminating/makeup run tables used
by TIFF fax compression, but the BookManager wrapper is not a TIFF container.

Observed streams may begin each line with the 12-bit EOL code
`000000000001`, followed by a one-bit T.4 line tag:

| Tag bit after EOL | Meaning |
| --- | --- |
| `1` | The line is one-dimensional Modified Huffman / T.4 1D. |
| `0` | The line is two-dimensional Modified READ / T.4 2D, using the previous decoded line as reference. |

The two-dimensional path uses the standard pass, horizontal, and vertical codes
from CCITT T.6. Streams without per-line EOL markers are treated as T.6-style
2D MMR. A pure 1D MH fallback is retained for EOL-framed streams that do not
use a tag bit, but the hosted BookServer-backed fixture below uses the tagged
T.4 form.

The decoded reference line must be stored as alternating white/black run
lengths, not as absolute transition positions. This matches libtiff's
`curruns`/`refruns` model and the Transmogrifier IDB's `sub_4381D9` behavior,
which emits paired transition words and validates monotonic run structure. Each
decoded line also carries the final imaginary zero-length run used by the next
2D line. The earlier absolute-transition model happened to work for simpler
resources, including `QSYSNEWG` resource `1`, but desynchronized on dense 2D
resources `12` and `56`.

The first line of `QSYSNEWG.BOO` resource `1` demonstrates the tag:

```text
payload 0x50: 00 1a e1 80...
bits:        000000000001 1 010111 000011 ...
             EOL          tag=1  white makeup 192 + white term 13
```

The first decoded line is therefore a full-width white line (`192 + 13 = 205`)
when the tag bit is consumed. Treating the bit after EOL as image data causes
the line to desynchronize.

## Reference Render

A 32-bit harness calling the exported `ephimage` entry rendered
`GG24-4302-00.boo` resource `1` to a recognizable reference image. The generated
GIF starts with:

```text
47 49 46 38 37 61 c0 03 40 03 80 00 00 ff ff ff
GIF87a, width 960, height 832
```

The image content is a black-and-white "Parallel S/390 microprocessors" diagram
with the expected end-user workstation, MVS/IMS boxes, ESCON Director, and
shared-data disks. The harness output was kept under ignored `tmp/` as
`tmp/ibm-gg24-4302-1.gif` and converted to
`tmp/ibm-gg24-4302-1.png` for visual comparison.

The hosted BookServer-backed fixture used for pixel-level validation is
`QSYSNEWG.BOO` resource `1`:

| Evidence | Value |
| --- | --- |
| Local fixture | `BOO/QSYSNEWG.BOO`, resource `1`, kind `I`, raw payload size `1383` bytes. |
| Hosted book | `BOOKS/QSYSNEWG`, `DT=19910524085706`, topic `1.1`. |
| Hosted artifact | `http://cbrdoc01.lan.cyber.gent/bookmgr/pictures/QSYSNEWG.19910524085706.P1.GIF` |
| Wrapper bitmap size | payload `0x42 = 00 cd` (`205`), payload `0x44 = 01 9d` (`413`). |
| First segment | payload `0x48 = 05 17`; compressed byte count `0x050f` after subtracting the 8-byte segment header. |
| BookServer GIF size | `82 x 165`. This is `floor(205 * 2 / 5)` by `floor(413 * 2 / 5)`. |

On 2026-06-18, `boorsrc --png BOO/QSYSNEWG.BOO 1` rendered a PNG that matched
the BookServer GIF at pixel level after normalizing the GIF to RGB PNG:

```text
dims: local 82x165, BookServer 82x165
pixel mismatches: 0 of 13530
```

The regression test `mmr_qsysnewg_test` renders this resource through
`BooDocument::read_resource_png()`, decodes the PNG to RGBA, and checks
dimensions `82 x 165` plus FNV-1a pixel hash `0x9491199eae92882e`.

A full `boo2git --force BOO/QSYSNEWG.BOO /tmp/geist-mmr/qsysnewg-boo2git`
smoke test now renders all 88 of the book's PNG resources through the same
public API. The two resources that previously failed are:

| Resource | Topic | Hosted artifact | Local dimensions | BookServer dimensions | Pixel comparison |
| --- | --- | --- | --- | --- | --- |
| `12` | `2.0` | `QSYSNEWG.19910524085706.P12.GIF` | `340 x 294` | `340 x 294` | `838` mismatches of `99960` pixels. |
| `56` | `6.0` | `QSYSNEWG.19910524085706.P56.GIF` | `344 x 385` | `344 x 385` | `450` mismatches of `132440` pixels. |

The remaining pixel deltas are sparse binary stroke differences after scaling,
not decode failures or dimension mismatches. Visual inspection of the local PNGs
and downloaded BookServer artifacts on 2026-06-18 judged resources `12` and
`56` visually identical for BookManager rendering purposes. The IDB's
`ScaleMonoBitmap2xTo5x` routine expands each source pixel to `2 x 2` bytes and
averages `5 x 5` blocks, but the hosted BookServer GIFs for these topics are
binary rather than grayscale; nearest-neighbor phase `(0,0)` remains the
closest verified local match and preserves the exact `P1` match above.

Future implementation should continue from the reader functions named above,
especially `dinitmmr`, `dlinemmr`, `decline`, `deceol`, and `readcd`, rather
than treating every legacy kind `I` resource as a raw TIFF Group 4 stream.

## Open Questions

- Identify the full ImageMark/GDI payload grammar used around the compressed
  MMR segments.
- Identify the unresolved metadata fields at payload offsets `0x32` and
  `0x34`.
- Audit more kind `I` books as fixtures are mapped to hosted BookServer
  artifacts, especially if future samples expose additional ImageMark wrapper
  records or fax stream framing.
