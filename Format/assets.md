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

## Legacy Picture Types

The Transmogrifier utility in `Official Readers/Transmogrifier/transmog.exe`
confirms that older BOO picture payloads are not one single image encoding. The
utility's readme and IDB both describe version 1.2/1.3 books as legacy picture
books that can be rewritten into version 1.4 books with web-compatible pictures.

The readme progress markers give the picture families:

| Marker | Meaning in Transmogrifier readme | IDB-backed conversion path |
| --- | --- | --- |
| `g` | GDF picture converted to GIF | `TransmogConvertGdfToGif` writes source bytes to a temporary file, then `TransmogRunGdfImportGifExport` loads `IMGDF2.FLT` and `EBGIF2.FLT`. |
| `m` | MMR picture converted to GIF | `TransmogConvertMmrToGif` reads the payload bytes into memory and calls `TransmogWriteMmrAsGif`. |
| `C` | CGM picture converted to GIF | `TransmogReadCgmExtent` parses CGM extent data; CGM is treated as a recognized image object class. |
| `G` | Metafile bitmap converted to GIF | `TransmogConvertMetBitmapToGifOrJpeg` recovers bitmap data, then `TransmogWriteBitmapAsGifOrJpeg` writes GIF for non-24-bit bitmaps. |
| `J` | Metafile bitmap converted to JPEG | `TransmogWriteBitmapAsGifOrJpeg` writes JPEG for 24-bit bitmap data. |
| `V` | Metafile vector converted to GIF | `TransmogConvertMetVectorToGif` uses `IMMET2.FLT` and `EBGIF2.FLT`. |

The old page-0 descriptor `kind` byte identifies which legacy conversion path is
used:

| Kind byte | EBCDIC | Observed path |
| ---: | --- | --- |
| `0xc7` | `G` | Append `.gif`; convert GDF through `IMGDF2.FLT` -> `EBGIF2.FLT`. |
| `0xc9` | `I` | Append `.gif`; convert MMR/image payload through the internal GIF writer. This is the kind observed in `GG24-4302-00.boo`. |
| `0xd4` | `M` | Classify MET payload. Bitmap MET can become GIF or JPEG; vector MET becomes GIF through `IMMET2.FLT` -> `EBGIF2.FLT`. |

Unknown kind bytes produce `Unknown data type encountered %s` in the utility.

## Version 1.2/1.3 Picture Directory

The loaded BookServer stack supports legacy version 1.2/1.3 picture directories
through `Official Readers/BookSrv-Win32/ephimage.dll`. The CGI renderer in
`bookmgr.exe` first attempts the version 1.4 converted-object lookup described
below; when that does not find a converted object, it calls the `ephimage`
helper path to locate and convert a legacy picture payload.

`EphImageFindLegacyPictureDescriptor` reads the old picture directory directly
from the BOO header area, before the logical directory page:

1. Read page-0 bytes `0x0000..0x0001` as the physical directory page.
2. Seek to `(directory_page << 12)` and read 20 bytes from the physical directory
   page. Bytes at directory-page offsets `+9..+10` identify the legacy picture
   layout version for this path. If those bytes are `01 03`, the helper treats
   the picture directory as version 1.3; otherwise it uses the version 1.2
   offset. The Transmogrifier applies the same 1.2/1.3 distinction when
   rewriting old picture books.
3. Seek to page-0 offset `0x0004` and read a 32-bit big-endian picture/object
   count. If it is zero, the utility prints `Book %s contains no pictures`.
4. For version 1.2 books, read picture descriptors starting at page-0 offset
   `0x0118`/decimal `280`. For version 1.3 books, skip the first descriptor
   group and read the second descriptor group at
   `0x0118 + (16 * picture_count)`.

Each legacy descriptor consumed by `EphImageFindLegacyPictureDescriptor` and
`TransmogConvertLegacyPicturesToWorkFiles` is 16 bytes:

| Field | Size | Encoding | Meaning |
| --- | ---: | --- | --- |
| `id` | 8 | EBCDIC text, padded | Object/picture id used as the base temporary filename. |
| `kind` | 1 | EBCDIC byte | Legacy payload family (`G`, `I`, `M`, etc.). |
| `length` | 3 | big-endian unsigned integer | Payload length in bytes. |
| `offset` | 4 | big-endian unsigned integer | Absolute payload offset in the BOO file. |

This matches the image descriptors observed in `GG24-4302-00.boo` if viewed from
the `0x0118` body offset:

```text
0x0118: f1 40 40 40 40 40 40 40  c9 00 1c fc 00 00 99 f0
        id "1"                    kind I, length 0x001cfc, offset 0x000099f0

0x0128: f1 f0 40 40 40 40 40 40  c9 00 2d 0b 00 02 0d 66
        id "10"                   kind I, length 0x002d0b, offset 0x00020d66
```

The earlier 16-byte view starting at `0x0120` still describes the same bytes, but
the Transmogrifier establishes that the descriptor starts with the 8-byte id and
then the 8-byte `(kind, length, offset)` tuple. Therefore the directory/control
entry at `0x0110` points to the descriptor body at `0x0118`.

## Version 1.4 Converted Object Layout

The loaded `bookmgr.exe` IDB confirms direct runtime support for version 1.4
converted-object descriptors. `BookServerFindConvertedObjectDescriptors` reads
page 0, reads the physical directory page, and requires directory-page bytes
`+9..+10` to be `01 00`. If the bytes do not match, it returns failure to the
caller, which can then use the legacy `ephimage` path.

The version 1.4 object count is the 32-bit big-endian value at page-0 offsets
`0x0004..0x0007`. Descriptor groups start immediately after the fixed 280-byte
page-0 header area:

| Group | Start offset | BookServer use |
| ---: | ---: | --- |
| `0` | `0x0118 + (0 * 16 * object_count)` | Low-resolution or placeholder object descriptors. The loaded BookServer direct lookup does not use this group. |
| `1` | `0x0118 + (1 * 16 * object_count)` | Object-description descriptors. |
| `2` | `0x0118 + (2 * 16 * object_count)` | Object-data descriptors. |

For groups 1 and 2, `BookServerFindConvertedObjectDescriptors` copies matching
16-byte entries to the caller, and `BookServerServePictureObject` decodes them
with this layout:

| Field | Size | Encoding | Meaning |
| --- | ---: | --- | --- |
| `id` | 8 | EBCDIC-ish object id, padded with `0x40` | Object id matched against the requested picture id. |
| `length` | 4 | big-endian unsigned integer | Length of the description or object-data payload. |
| `offset` | 4 | big-endian unsigned integer | Absolute byte offset of the description or object-data payload. |

This is not the legacy `kind[1] + length[3] + offset[4]` tail. The version 1.4
groups used by BookServer store full 32-bit lengths and have no one-byte legacy
payload-kind field.

The group-1 description payload is stored as two-byte characters, normally
`00 xx` pairs for ASCII text. `BookServerReadConvertedObjectDescription` reads
the byte range, detects a leading zero byte, collapses each pair to the second
byte, and NUL-terminates the resulting string. The CGI renderer then lowercases
the description and looks for MIME attributes such as:

```text
type="image/gif"
type='image/jpeg'
```

It also consumes width and height attributes when present. The group-2 object
payload is the raw converted web object byte range. `BookServerExtractConvertedObjectDataToFile`
copies this range into the BookServer picture cache using the extension inferred
from the description `type` attribute.

The Transmogrifier does not just change image bytes in place. It builds a new
version 1.4 BOO file in a temporary stream, then patches the header tables with
new offsets and lengths.

The rewrite sequence in `TransmogRewriteBookWithConvertedObjects` is:

| Progress marker | Function | Stored data written to new book |
| --- | --- | --- |
| `h`/`x`/`L` | `TransmogCopyHeaderAndPictureDirectory` | Copies the original header and picture directory area through `0x0118 + 16 * picture_count`. |
| `x`/`L` | `TransmogWriteNullLowResPictureDirectory` | Writes low-resolution picture directory entries with original ids but zero offset/length fields. This is done twice. |
| `L` | `TransmogCopyOriginalObjectData` | Copies original object payloads into the new stream and records their new offsets/lengths in an in-memory table. |
| `O` | `TransmogAppendConvertedObjectData` | Finds converted files in the temporary `.pic` directory, appends their bytes to the new stream, and records offset/length. |
| `D` | `TransmogWriteObjectDescriptions` | Writes object descriptions after object data. Descriptions are stored as two-byte characters: a leading `0x00` followed by the ASCII byte. |
| `T` | `TransmogAppendTextComponentAsVersion14` | Appends the original logical/text component but writes version bytes `01 00` in the copied text header area. |
| `N` | `TransmogPatchHeaderAndWriteOutputBook` | Writes the output BOO and patches header-directory offset/length fields from the in-memory object table. |

`TransmogAppendConvertedObjectData` stores a MIME-style type string for each
converted object:

```text
type="image/%s"
```

The `%s` value comes from the converted file extension found in the temporary
picture directory. Width and height descriptions are added by
`TransmogDescribeWebImageObject`.

## Web-Compatible Image Objects

Version 1.4 object data can contain normal web image formats. The Transmogrifier
recognizes these extensions and validates/detects dimensions from their file
headers:

| Extension(s) | Dimension reader | Stored description behavior |
| --- | --- | --- |
| `GIF` | `TransmogReadGifDimensions` | Checks `GIF` signature, reads little-endian width and height from the logical screen descriptor, emits absolute `width="N"` / `height="N"`. |
| `PNG` | `TransmogReadPngDimensions` | Checks PNG signature and reads IHDR width/height. |
| `TIF`, `TIFF` | `TransmogReadTiffDimensions` | Handles both byte orders and reads TIFF tags for width/height. |
| `JPG`, `JPEG` | `TransmogReadJpegDimensions` | Checks JPEG/JFIF and reads dimensions from SOF markers. |
| `CGM` | `TransmogReadCgmExtent` | Parses CGM extent where available; plain-text and char-encoded CGM may not carry known extent. |

The dimension mode written by `TransmogDescribeWebImageObject` is:

| Mode | Output |
| ---: | --- |
| `10` | Absolute `width="N"` and `height="N"`. |
| `20` | Percentage width/height derived from command-line page dimensions. |
| `0` | Percentage width/height normalized against the larger dimension. |

The converted object data is therefore different from legacy picture storage:
legacy version 1.2/1.3 payloads are typed by the 1-byte descriptor kind and may be
ImageMark/GDI, MMR, GDF, CGM, or MET-derived data; version 1.4 payloads are
normal object byte ranges with object descriptions such as `type="image/gif"`,
`width="..."`, and `height="..."`.

## Extraction Rules

An independent BOO reader can extract stored image assets from the currently
verified layout as follows:

1. Read page 0 and decode the first two bytes as the big-endian physical
   directory page number.
2. If the directory page number is greater than `1`, treat bytes before
   `directory_page * 4096` as a possible pre-directory resource area.
3. In page 0, look for the resource-directory control entry at `0x0110`:
   marker `0x00`, 24-bit length, 32-bit absolute offset, and EBCDIC id. In the
   verified version 1.2 image fixture this points to the descriptor body at
   `0x0118`.
4. Walk 16-byte image descriptors inside the directory body. Decode id, kind,
   length, and offset as documented above.
5. Validate each descriptor by checking that `offset + length` is less than or
   equal to `directory_page * 4096`; image payloads must not overlap the logical
   BOO directory.
6. Export exactly the byte range `[offset, offset + length)` for each image.
   Do not convert the bytes to GIF/BMP/JPEG during extraction.

## Open Questions

- Identify the logical-record controls that reference image ids from document
  topics and figure lists.
- Identify the full ImageMark/GDI payload grammar. The BookServer conversion
  path uses ImageMark components and GIF output filters, but that is separate
  from BOO container extraction.
- Determine whether non-image media resources use kind bytes other than EBCDIC
  `I` (`0xc9`) in the same directory body.
- Verify version 1.3 and 1.4 descriptor groups against committed BOO fixtures
  that actually contain those layout versions. The reader-code layout is
  verified from the loaded IDBs; the current local fixture evidence still
  centers on a version 1.2-style legacy image directory.
