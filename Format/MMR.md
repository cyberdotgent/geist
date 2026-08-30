# Legacy MMR / Kind `I` Image Payloads

This note documents the legacy BookManager kind `I` payload format. The BOO
container stores these payloads as byte ranges referenced by the page-0 picture
directory; the container descriptor layout is documented in [assets.md](assets.md).

## Current Implementation Status

MMR support is implemented in `libgeist` for the observed legacy kind `I`
wrapper and the CCITT fax line streams verified against hosted
BookServer output. The public PNG renderer treats `legacy-mmr` as a renderable
format, the same way it treats `legacy-gdf`, while still reporting unsupported
or not-yet-mapped fax variants as clear render errors.

The implementation is self-contained. It uses the standard CCITT T.4/T.6
white/black run tables and 2D modes, with wrapper handling derived from the
fixtures and verified against hosted BookServer output. It does not embed or
copy third-party fax-decoder source.

## Payload Family

Legacy kind `I` payloads are a wrapper around CCITT fax (MMR) compressed
bitmap data. They are not GDF and not a standard image container: the stored
bytes begin with a fixed wrapper prefix, never with `GIF87a`, `GIF89a`, `BM`,
or a JPEG SOI marker. Every one of the 558 kind `I` descriptors across the
repository fixtures points at a payload starting `00 08 d3 a8 7b`.

The hosted BookServer serves these resources as GIFs generated on demand; see
[assets.md](assets.md). The GIF is the rendered artifact, so it is usable as an
independent check of a decoder's output but is not what the book stores.

## Observed Payload Wrapper

Observed kind `I` payloads contain a fixed-shape wrapper before the MMR
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
| `0x42` | `03 c0` | Bitmap width: `960`. Confirmed by decoded raster geometry and by the hosted GIF for this resource. |
| `0x44` | `03 40` | Bitmap height: `832`. Confirmed by decoded raster geometry and by the hosted GIF for this resource. |
| `0x46` | `01 00` | Unresolved flag or depth-like field. |
| `0x48` | `1c ac` | First compressed segment record length, big-endian, including the 8-byte segment header. Subtract `8` for the compressed byte count. |
| `0x50` | `00 1b 50 d4...` | First byte consumed by the decompressor for the first segment. |

The corresponding legacy descriptor for that payload is:

```text
GG24-4302-00.boo
descriptor 0x0118:
f1 40 40 40 40 40 40 40 c9 00 1c fc 00 00 99 f0

id "1", kind I, length 0x001cfc, absolute payload offset 0x000099f0
```

Other kind `I` fixtures have the same wrapper shape with different dimensions,
which confirms that the dimension fields are per-resource and not constants:

| Fixture, resource | `0x42` width | `0x44` height | `0x46` | `0x48` first segment length |
| --- | ---: | ---: | --- | ---: |
| `GG24-4302-00.boo` `1` | `03 c0` / 960 | `03 40` / 832 | `01 00` | `0x1cac` |
| `GG24-4302-00.boo` `3` | `03 a0` / 928 | `03 60` / 864 | `01 00` | `0x105e` |
| `QSYSNEWG.BOO` `1` | `00 cd` / 205 | `01 9d` / 413 | `01 00` | `0x0517` |
| `SC33-033.boo` `2` | `04 e0` / 1248 | `02 40` / 576 | `01 00` | `0x0cee` |

In every one of these, the first segment length equals
`descriptor_length - 0x50`, so the compressed data occupies exactly the payload
from `0x50` to the end minus the trailing terminator record.

## Segment Framing

The compressed data is carried in a chain of segment records:

1. The 32-byte image header begins at payload-relative `0x28`. Bitmap width and
   height are the big-endian words at payload-relative `0x42` and `0x44`
   (offsets `0x1a` and `0x1c` inside that header).
2. The first segment record header is at payload-relative `0x48`. Its first word
   is the big-endian record length, counted from the start of the header and
   including the 8-byte header itself.
3. Compressed bytes for that segment therefore start at
   `segment_header + 8` -- payload-relative `0x50` for the first segment -- and
   run for `segment_length - 8` bytes.
4. The next record starts at `previous_segment_header + segment_length`, with
   the same shape.
5. The chain ends with a record whose length is exactly `8`, i.e. a header with
   no compressed body, which lands exactly on the end of the payload.
6. The decoded bitmap is stored bottom-up: invert it vertically to obtain the
   image as rendered.

The chain rule is fixture-verified. Walking it from `0x48` in
`GG24-4302-00.boo` resources `1`, `2` and `3`, `QSYSNEWG.BOO` resources `1`,
`12` and `56`, and `SC33-033.boo` resources `1` and `2` lands in every case on
a terminal `8`-byte record ending exactly at the descriptor's declared payload
length, with no bytes left over.

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
lengths, not as absolute transition positions. This is the same run-pair model
TIFF fax decoders use, and it must validate monotonic run structure. Each
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
`56` visually identical for BookManager rendering purposes.

The hosted artifacts are scaled by two fifths, and their pixels are binary
rather than grayscale, so the scaling is not an area average. Nearest-neighbor
sampling at phase `(0,0)` is the closest match found against hosted output and
preserves the exact `P1` match above.

A decoder should treat a kind `I` payload as this wrapper plus its segment
chain, not as a raw TIFF Group 4 stream: the segment framing, the tagged-EOL
line form, and the bottom-up storage all differ from a bare Group 4 strip.

## Open Questions

- Identify the full wrapper-record grammar around the compressed MMR segments.
  Only the fields tabulated above are accounted for; the rest of the 0x50-byte
  prologue is unexplained.
- Identify the unresolved metadata fields at payload offsets `0x32` and
  `0x34`.
- Audit more kind `I` books as fixtures are mapped to hosted BookServer
  artifacts, especially if future samples expose additional wrapper records or
  fax stream framing.
