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

Local fixture verification found kind `G` descriptors:

| Kind byte | Example fixture | First verified descriptor and payload evidence |
| ---: | --- | --- |
| `0xc7` / `G` | `GG66-3212-00.boo` | Descriptor at `0x0118`: `f1 40 40 40 40 40 40 40 c7 00 4e 64 00 00 01 58`; payload at `0x0158` begins `01 12 00 04 00 00 00 00 42 64 00 01 00 00 00 00...`. |

## Current Rendering Approximation

Current `libgeist` PNG rendering support for `legacy-gdf` is fixture-driven and
limited to observed vector-style GDDM payloads. The decoder reads IBM
hexadecimal floating-point coordinate runs from the stored GDF byte stream,
maps the discovered coordinate extents to an RGBA canvas, and rasterizes the
coordinate runs as black polylines before PNG encoding.

This renders the verified `GG66-3212-00.boo` GDF resources without invoking the
historical `IMGDF2.FLT`/`EBGIF2.FLT` filter chain. It does not yet implement the
complete GDDM command grammar, color/style attributes, filled areas, or exact
text/font semantics.
