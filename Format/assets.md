# BOO Assets and Media Resources

BOO files can embed image resources before the logical BOO directory page. These
resources are not normal 4096-byte logical pages and are not GIF/BMP/JPEG files
as stored. The BookServer reader retrieves raw BOO/resource bytes, then the
separate `ephimage`/ImageMark path converts the stored image payload to a GIF for
HTML output.

## Verified Reader Flow

The attached `Official Readers/BookSrv-Win32/ephwam.dll.i64` IDB shows that
export `Scm_Makeres(runtime, book_handle, logical_page_number)` is only a raw
resource-page loader:

1. It validates the book handle.
2. It calls `BooGetOrLoadPageBuffer(..., logical_page_number, allocation_class =
   3, &error_code)`.
3. `BooGetOrLoadPageBuffer` either returns a cached page buffer or calls
   `BooReadPhysicalPageIntoBuffer`.
4. `BooReadPhysicalPageIntoBuffer` seeks to
   `((directory_page + logical_page_number) << 12) - 4096` and reads exactly
   4096 bytes. The IDB path performs no GIF/BMP/JPEG decoding, decompression,
   decryption, or format conversion.

The BookServer image renderer is outside the currently attached IDB, but the
companion binaries identify the conversion stage:

| Binary | Offset | Evidence |
| --- | ---: | --- |
| `Official Readers/BookSrv-Win32/bookmgr.exe` | `0x092450` | `BOOKNAME.P999Z.GIF` |
| `Official Readers/BookSrv-Win32/bookmgr.exe` | `0x092470` | `ephimage.exe` |
| `Official Readers/BookSrv-Win32/bookmgr.exe` | `0x092480` | `/NOSCALE` |
| `Official Readers/BookSrv-Win32/bookmgr.exe` | `0x09248c` | `/bookmgr/pictures` |
| `Official Readers/BookSrv-Win32/ephimage.dll` | `0x016e7c` | `Requested GIF  Name = %s` |
| `Official Readers/BookSrv-Win32/ephimage.dll` | `0x017034` | `imgdf2.flt` |
| `Official Readers/BookSrv-Win32/ephimage.dll` | `0x01705c` | `ebgif2.flt` |
| `Official Readers/BookSrv-Win32/ephimage.dll` | `0x01708c` | `Picture %s is an image of size %hux%hu` |
| `Official Readers/BookSrv-Win32/ephimage.dll` | `0x01719c` | `GIF89a` |
| `Official Readers/BookSrv-Win32/ephimage.dll` | `0x0171a4` | `GIF87a` |
| `Official Readers/BookSrv-Win32/isgdi32.dll` | `0x09f1aa` | `CBeginFigure` |
| `Official Readers/BookSrv-Win32/isgdi32.dll` | `0x09f298` | `CEndFigure` |
| `Official Readers/BookSrv-Win32/isgdi32.dll` | `0x0a1c58` | `HELPCGI_GDI` |
| `Official Readers/BookSrv-Win32/isgdi32.dll` | `0x0a1cc8` | `ImageMark Software Labs v03.01` |

This separates the stored BOO asset payload from the rendered web asset: a BOO
reader should first expose or extract the stored payload exactly; GIF generation
is a reader/rendering concern.

## Embedded Resource Area

`GG24-4302-00.boo` verifies the embedded-resource area. Its page-0 directory
locator is `0x0034`, so the logical BOO directory starts at physical page 52:

```text
directory_offset = 0x0034 * 4096 = 0x00034000
```

Physical pages before `0x00034000` are a pre-directory area. Page 0 contains the
normal BOO file header plus a resource directory. The image payloads are packed
after that directory and before the logical BOO directory:

```text
resource directory: starts near 0x00000110
first image payload: 0x000002e8
last observed image payload end: 0x000330ab
logical BOO directory: 0x00034000
```

The gap from `0x000330ab` to `0x00034000` is zero padding in this fixture.

The original non-image repository fixtures do not have this pre-directory image
area:

| File | Page-0 directory locator | Meaning |
| --- | ---: | --- |
| `QS3X36CM.BOO` | `0x0001` | Directory starts at physical page 1; no pre-directory asset pages observed. |
| `OFCUSEOV.BOO` | `0x0001` | Directory starts at physical page 1; no pre-directory asset pages observed. |
| `GG24-4302-00.boo` | `0x0034` | Physical pages 1-51 precede the logical directory and contain embedded resources. |

## Page-0 Resource Directory

`GG24-4302-00.boo` has a resource directory entry at page-0 offset `0x0110`:

| Offset | Bytes | Decoded meaning |
| ---: | --- | --- |
| `0x0110` | `00` | Directory/control entry marker. |
| `0x0111` | `00 01 d0` | 24-bit big-endian length: `0x0001d0` bytes. |
| `0x0114` | `00 00 01 18` | 32-bit big-endian offset: `0x00000118`. |
| `0x0118` | `f1 40 40 40 40 40 40 40` | EBCDIC id field: `1` padded with EBCDIC spaces. |

The range `0x00000118..0x000002e7` is the observed resource-directory body. It
contains image descriptors and ends immediately before the first image payload at
`0x000002e8`.

Most image descriptors are 16 bytes:

| Field | Size | Encoding | Meaning |
| --- | ---: | --- | --- |
| `kind` | 1 | EBCDIC byte | `0xc9`, EBCDIC `I`, for image resources. |
| `length` | 3 | big-endian unsigned integer | Stored payload length in bytes. |
| `offset` | 4 | big-endian unsigned integer | Absolute byte offset in the BOO file. |
| `id` | 8 | EBCDIC, padded with `0x40` | Resource id used by logical figure references and BookServer image naming. |

Example descriptors from `GG24-4302-00.boo`:

| Descriptor offset | Bytes | Parsed descriptor |
| ---: | --- | --- |
| `0x0120` | `c9 00 1c fc 00 00 99 f0 f1 f0 40 40 40 40 40 40` | Image `10`, length `0x001cfc`, payload offset `0x000099f0`. |
| `0x01c0` | `c9 00 10 0f 00 00 37 ea f2 40 40 40 40 40 40 40` | Image `2`, length `0x00100f`, payload offset `0x000037ea`. |
| `0x02a0` | `c9 00 22 96 00 00 02 e8 f6 40 40 40 40 40 40 40` | Image `6`, length `0x002296`, payload offset `0x000002e8`. |
| `0x02d0` | `c9 00 1a a2 00 02 7f d8 f9 40 40 40 40 40 40 40` | Image `9`, length `0x001aa2`, payload offset `0x00027fd8`. |

The last descriptor-sized bytes before payload data are:

```text
0x02e0: c9 00 1e 05 00 02 9a 7a
```

This is an image-kind marker, length `0x001e05`, and payload offset
`0x00029a7a`, but the usual 8-byte id field is not present because the first
payload starts at `0x000002e8`. The likely interpretation is that this is the
payload descriptor for image `1`, whose EBCDIC id appears in the directory
control entry at `0x0118`; keep this as a verified byte layout but unresolved
field pairing until another image-bearing fixture confirms it.

## Payload Layout

Image payload offsets are absolute file offsets, not page-relative logical page
numbers. The payloads in `GG24-4302-00.boo` are packed contiguously, but the
directory table is not sorted by payload offset.

Selected entries in payload-offset order:

| Image id | Payload offset | Length | End offset |
| --- | ---: | ---: | ---: |
| `6` | `0x000002e8` | `0x002296` | `0x0000257e` |
| `19` | `0x0000257e` | `0x00126b` | `0x000037e9` |
| `2` | `0x000037ea` | `0x00100f` | `0x000047f9` |
| `18` | `0x000047fa` | `0x001445` | `0x00005c3f` |
| `29` | `0x00005c40` | `0x001520` | `0x00007160` |
| `10` | `0x000099f0` | `0x001cfc` | `0x0000b6ec` |
| `17` | `0x0000b6ec` | `0x00496c` | `0x00010058` |
| `20` | `0x0002b880` | `0x001bd7` | `0x0002d457` |
| `27` | `0x00031948` | `0x001763` | `0x000330ab` |

Most payloads observed in `GG24-4302-00.boo` begin with the same 32-byte prefix:

```text
00 08 d3 a8 7b 00 00 00 00 20 d3 a7 7b 00 00 00
00 00 00 00 00 00 00 00 2d 00 00 00 00 00 00 00
```

The bytes `d3 a8` and `d3 a7` are EBCDIC `Ly` and `Lx`, which fits an
ImageMark/GDI coordinate-oriented image stream. The stored payload does not start
with `GIF87a`, `GIF89a`, `BM`, or a valid JPEG header in the verified sample.
Byte-pattern hits for `BM` and `ff d8 ff` inside the payload area are internal
payload bytes, not standalone external image-file signatures.

## Extraction Rules

An independent BOO reader can extract stored image assets from the currently
verified layout as follows:

1. Read page 0 and decode the first two bytes as the big-endian physical
   directory page number.
2. If the directory page number is greater than `1`, treat bytes before
   `directory_page * 4096` as a possible pre-directory resource area.
3. In page 0, look for the resource-directory control entry at `0x0110`:
   marker `0x00`, 24-bit length, 32-bit absolute offset, and EBCDIC id.
4. Walk image descriptors inside the directory body. For each descriptor with
   kind byte `0xc9`, read the 24-bit length, 32-bit absolute offset, and EBCDIC
   id when present.
5. Validate each descriptor by checking that `offset + length` is less than or
   equal to `directory_page * 4096`; image payloads must not overlap the logical
   BOO directory.
6. Export exactly the byte range `[offset, offset + length)` for each image.
   Do not convert the bytes to GIF/BMP/JPEG during extraction.

## Open Questions

- Confirm the `0x0110` directory/control entry and the trailing 8-byte descriptor
  pairing for image `1` against a second image-bearing BOO fixture.
- Identify the logical-record controls that reference image ids from document
  topics and figure lists.
- Identify the full ImageMark/GDI payload grammar. The BookServer conversion
  path uses ImageMark components and GIF output filters, but that is separate
  from BOO container extraction.
- Determine whether non-image media resources use kind bytes other than EBCDIC
  `I` (`0xc9`) in the same directory body.
