# Legacy MMR / Kind `I` Image Payloads

This note documents the legacy BookManager kind `I` payload format. The BOO
container stores these payloads as byte ranges referenced by the page-0 picture
directory; the container descriptor layout is documented in [assets.md](assets.md).

## Current Implementation Status

MMR support is not yet implemented in `libgeist`. The current source tree keeps
an experimental Group-4-style decoder stub for future work, but the public
renderer intentionally reports kind `I` / `legacy-mmr` assets as unsupported
until the reader-specific line decoder is ported.

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
| `0x32` | `03 c0` | Image width: `960`. Confirmed by `process_mmr_pict` trace string and GIF logical screen size. |
| `0x34` | `03 40` | Image height: `832`. Confirmed by `process_mmr_pict` trace string and GIF logical screen size. |
| `0x36` | `01 00` | Unresolved flag or depth-like field. |
| `0x38` | `1c ac` | Candidate compressed byte count. This equals descriptor length `0x1cfc` minus `0x50`. |
| `0x3a` | `d3 ee 7b 40` | Additional wrapper marker before bitmap payload metadata. |
| `0x48` | `1c ac` | First compressed segment record length. `process_mmr_pict` byte-swaps this word, subtracts `8`, and passes that byte count to the decompressor. |
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
| `0x32` | `03 40` | Candidate image width: `832`. |
| `0x34` | `01 80` | Candidate image height: `384`. |
| `0x38` | `06 74` | Candidate compressed byte count. This equals descriptor length `0x06c4` minus `0x50`. |

## Segment Framing

IDA decompilation of `process_mmr_pict` verifies the first segment framing:

1. Read width and height from wrapper offsets `0x42` and `0x44` after the
   function's `a1 + 0x28` base, equivalent to payload-relative offsets `0x32`
   and `0x34`.
2. Set the first segment header pointer to payload-relative `0x48`.
3. Read the big-endian segment length word at `0x48`, subtract `8`, and store
   that as the compressed byte count.
4. Set the decompressor input pointer to payload-relative `0x50`.
5. Call `ecline` repeatedly. Status `0x2000` advances to the next segment by
   adding the previous compressed byte count plus the 8-byte segment header.
6. Invert the completed bitmap vertically with `gbm_ref_vert`, then write GIF
   output with `gif_w`.

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

The experimental self-contained decoder still fails on the same fixture with an
invalid standard T.6 two-dimensional mode when starting at the IBM-confirmed
payload-relative `0x50`. Therefore the remaining gap is not the segment start;
it is the reader-specific table-driven line decoder and transition-run raster
writer implemented by `dlinemmr`, `writere`, `WRraster`, and `WRruns`.

Future implementation should continue from the reader functions named above,
especially `dinitmmr`, `dlinemmr`, `decline`, `deceol`, and `readcd`, rather
than treating the wrapper as a raw CCITT G4 stream without verifying the exact
entry point.

## Open Questions

- Identify the full ImageMark/GDI payload grammar used around the compressed
  MMR segments.
- Port the reader-specific `dlinemmr` transition-run decoder and raster writer
  far enough for self-contained PNG rendering.
