# BOO Assets and Media Resources

BOO files can embed image resources before the logical BOO directory page. These
resources are not normal 4096-byte logical pages and, for the legacy families,
are not GIF/BMP/JPEG files as stored.

## Stored Payload Versus Rendered Asset

The stored payload and the web image a reader shows are two different things,
and the container documents only the former.

The hosted BookServer makes the distinction observable. For
`GG24-4302-00.boo` topic `2.1.1` at `DT=19950308184737` it serves:

```html
<img src="/bookmgr/pictures/GG24-4302-00.19950308184737.P1.GIF" alt="PICTURE 1">
```

The served artifact is a GIF whose name is
`<book>.<build timestamp>.P<picture id>.GIF`, generated on demand from the
stored payload; `P1` corresponds to descriptor id `1`. In the book itself, that
same resource is a legacy kind `I` payload that does not begin with `GIF87a` or
`GIF89a` at all (see the payload prefixes below).

The consequence for an implementer is the rule this note follows throughout: a
BOO reader must expose or extract the stored payload byte-exactly. Converting
it to a web image format is a rendering concern layered on top, documented per
payload family in [GDF.md](GDF.md) and [MMR.md](MMR.md).

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

Image descriptors are 16 bytes. The descriptor starts with the picture id; the
one-byte legacy kind follows the id and identifies the stored payload family:

| Field | Size | Encoding | Meaning |
| --- | ---: | --- | --- |
| `id` | 8 | EBCDIC, padded with `0x40` | Resource id used by logical figure references and BookServer image naming. |
| `kind` | 1 | EBCDIC byte | Legacy picture family, for example `0xc7`/`G` or `0xc9`/`I`. |
| `length` | 3 | big-endian unsigned integer | Stored payload length in bytes. |
| `offset` | 4 | big-endian unsigned integer | Absolute byte offset in the BOO file. |

Example descriptors from `GG24-4302-00.boo`:

| Descriptor offset | Bytes | Parsed descriptor |
| ---: | --- | --- |
| `0x0118` | `f1 40 40 40 40 40 40 40 c9 00 1c fc 00 00 99 f0` | Image `1`, kind `I`, length `0x001cfc`, payload offset `0x000099f0`. |
| `0x0128` | `f1 f0 40 40 40 40 40 40 c9 00 2d 0b 00 02 0d 66` | Image `10`, kind `I`, length `0x002d0b`, payload offset `0x00020d66`. |
| `0x01c8` | `f2 40 40 40 40 40 40 40 c9 00 10 0f 00 00 37 ea` | Image `2`, kind `I`, length `0x00100f`, payload offset `0x000037ea`. |
| `0x02a8` | `f6 40 40 40 40 40 40 40 c9 00 22 96 00 00 02 e8` | Image `6`, kind `I`, length `0x002296`, payload offset `0x000002e8`. |

The last descriptor starts at `0x02d8` and ends exactly before the first payload:

```text
0x02d8: f9 40 40 40 40 40 40 40 c9 00 1e 05 00 02 9a 7a
```

This is image `9`, kind `I`, length `0x001e05`, and payload offset
`0x00029a7a`.

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
coordinate-oriented image stream. The stored payload does not start
with `GIF87a`, `GIF89a`, `BM`, or a valid JPEG header in the verified sample.
Byte-pattern hits for `BM` and `ff d8 ff` inside the payload area are internal
payload bytes, not standalone external image-file signatures.

## Legacy Payload Kind Bytes

The old page-0 descriptor `kind` byte identifies the stored legacy payload
family. This is a container-level type tag; payload decoding details belong in
the per-format notes linked below.

| Kind byte | EBCDIC | Observed path |
| ---: | --- | --- |
| `0xc7` | `G` | Legacy GDF image payload. See [GDF.md](GDF.md). |
| `0xc9` | `I` | Legacy MMR (CCITT fax) image payload. See [MMR.md](MMR.md). |
| `0xd4` | `M` | Legacy MET payload. **Unverified**: no repository fixture uses this kind byte, and no hosted book has been observed serving one. |

A decoder should reject an unrecognised kind byte rather than guess a payload
family, because the descriptor gives no other type information.

Fixture verification over the whole repository BOO set found legacy descriptor
kinds `0xc7` and `0xc9` only:

| Kind byte | Descriptors in this repository | Example fixture | First verified descriptor and payload evidence |
| ---: | ---: | --- | --- |
| `0xc7` / `G` | 65 in 10 books | `FA1PLMM0.boo` | Descriptor at `0x0118`: `f1 40 40 40 40 40 40 40 c7 00 48 de 00 00 01 38`; payload at `0x0138` begins `01 12 00 04 00 00 00 00 42 64 00 01 00 00 00 00...`. |
| `0xc9` / `I` | 558 in 16 books | `GG24-4302-00.boo` | Descriptor at `0x0118`: `f1 40 40 40 40 40 40 40 c9 00 1c fc 00 00 99 f0`; payload at `0x99f0` begins `00 08 d3 a8 7b 00 00 00 00 20 d3 a7 7b 00 00 00...`. |

Full kind census over the 23 repository books that carry a picture directory
(counting group-0 descriptors, one per picture):

| Kind | Books | Descriptors |
| --- | --- | ---: |
| `0xc7` / `G` | `FA1PLMM0`, `IEAC6MST`, `SC09-138`, `SC24-5520-00`, `SC26-457`, `SC28-1881-05`, `SC34-425`, `SH20-918`, `XWEBDEMO`, `packet` | 65 |
| `0xc9` / `I` | `DREICMST`, `GG24-395`, `GG24-4302-00`, `GX27-3999-00`, `ITPPIBOK`, `QSYSNEWG`, `SC09-2417-00`, `SC24-546`, `SC24-5527-02`, `SC26-457`, `SC33-033`, `SG24-204`, `SH12-565`, `SH20-918`, `XWEBDEMO`, `HLCRUG21` | 558 |

No other kind byte occurs in any repository fixture. `0xd4` / `M` is listed
above as a family the container's type tag can express, but it is unverified
here.

## Picture Directory Versions

The two bytes at directory-page offsets `+9..+10` select the picture-directory
layout. Across the 23 repository books that carry a picture directory, exactly
three values occur, and each one determines how many 16-byte descriptor groups
follow the fixed 280-byte (`0x0118`) page-0 header area:

| Bytes at `+9..+10` | Layout | Descriptor groups | Repository fixtures |
| --- | --- | ---: | --- |
| `00 00` | version 1.2 | 1 | 16 books, e.g. `GG24-4302-00.boo`, `QSYSNEWG.BOO`, `SC34-425.boo` |
| `01 03` | version 1.3 | 2 | `GX27-3999-00.boo`, `SC09-2417-00.boo`, `SG24-204.boo`, `packet.boo` |
| `01 00` | version 1.4 | 3 | `XWEBDEMO.boo`, `Official Readers/SoftCopy/HLCRUG21.boo` |

The object count is the 32-bit big-endian value at page-0 offsets
`0x0004..0x0007` in every version. Group `n` begins at
`0x0118 + (n * 16 * object_count)`, and the first payload begins immediately
after the last group. This is verified by construction: for every fixture, the
lowest payload offset named by any descriptor equals
`0x0118 + (group_count * 16 * object_count)`.

| Fixture | Version | Objects | Groups end at | Lowest payload offset |
| --- | --- | ---: | ---: | ---: |
| `GG24-4302-00.boo` | 1.2 | 29 | `0x0002e8` | `0x0002e8` |
| `SC34-425.boo` | 1.2 | 32 | `0x000318` | `0x000318` |
| `GX27-3999-00.boo` | 1.3 | 19 | `0x000378` | `0x000378` |
| `SG24-204.boo` | 1.3 | 123 | `0x001078` | `0x001078` |
| `packet.boo` | 1.3 | 9 | `0x000238` | `0x000238` |
| `XWEBDEMO.boo` | 1.4 | 2 | `0x000178` | `0x000178` |
| `HLCRUG21.boo` | 1.4 | 135 | `0x00247c` | `0x00247c` |

### Version 1.2

One descriptor group at `0x0118`, in the legacy 16-byte form documented under
[Page-0 Resource Directory](#page-0-resource-directory): 8-byte EBCDIC id,
1-byte kind, 3-byte big-endian length, 4-byte big-endian absolute payload
offset.

### Version 1.3

Two descriptor groups, both in the same legacy 16-byte form. In all four
version-1.3 fixtures the two groups are **byte-identical**: group 1 is an exact
duplicate of group 0, covering the same ids, kinds, lengths and payload offsets.
A decoder may therefore read group 0 and ignore group 1, but it must account for
group 1's size when computing where payloads begin.

### Version 1.4 Converted Object Layout

Three descriptor groups, and only group 0 uses the legacy form:

| Group | Start offset | Descriptor form | Content |
| ---: | ---: | --- | --- |
| `0` | `0x0118` | legacy: id[8], kind[1], length[3], offset[4] | The original legacy payload, still stored. Both 1.4 fixtures carry kind `G` or kind `I` payloads here. |
| `1` | `0x0118 + 16 * n` | id[8], length[4], offset[4] | The converted web object. |
| `2` | `0x0118 + 32 * n` | id[8], length[4], offset[4] | The object description string. |

Groups 1 and 2 use a different tail from group 0: a full 32-bit length and no
one-byte kind field.

| Field | Size | Encoding | Meaning |
| --- | ---: | --- | --- |
| `id` | 8 | EBCDIC, padded with `0x40` | Object id; matches the group-0 id for the same picture. |
| `length` | 4 | big-endian unsigned integer | Length of the object data or description payload. |
| `offset` | 4 | big-endian unsigned integer | Absolute byte offset of that payload in the BOO file. |

The group-2 description payload is stored as two-byte characters, normally
`00 xx` pairs for ASCII text: a leading zero byte identifies the form, each pair
collapses to its second byte, and the result is an attribute string.

Verified against both version-1.4 fixtures:

| Fixture | Object | Group 1 descriptor | Payload signature | Group 2 decoded description |
| --- | --- | --- | --- | --- |
| `XWEBDEMO.boo` | `1` | at `0x0138`: `f1 40 40 40 40 40 40 40 00 00 28 15 00 00 61 ca` | `GIF89a` at `0x61ca` | `type="image/gif"width="620"height="480"` |
| `XWEBDEMO.boo` | `2` | at `0x0148`: id `2`, length `0x00003b4b`, offset `0x000089e0` | `GIF89a` at `0x89e0` | `type="image/gif"width="576"height="576"` |
| `HLCRUG21.boo` | `1` | at `0x0988`: id `1`, length `0x0000936f`, offset `0x00451a48` | `GIF87a` at `0x451a48` | `type="image/gif"width="1005"height="629"` |
| `HLCRUG21.boo` | `10` | at `0x0998`: id `10`, length `0x0000039d`, offset `0x006d1f88` | `GIF89a` at `0x6d1f88` | `type="image/gif"width="17"height="17"` |

The description carries a MIME type and, when known, pixel dimensions:

```text
type="image/gif"
type='image/jpeg'
```

Trailing NUL bytes can pad the description to its declared length; strip them
after collapsing the byte pairs. The group-1 payload is the converted web object
byte range, stored verbatim, and in both fixtures every group-1 payload begins
with a valid `GIF87a` or `GIF89a` signature.

## Version 1.4 Object Data

Version 1.4 object data is stored as normal object byte ranges with object
descriptions such as `type="image/gif"`, `width="..."`, and `height="..."`.
Image-format-specific validation and dimension parsing are documented in
[WebImages.md](WebImages.md).

## Extraction Rules

An independent BOO reader can extract stored image assets from the currently
verified layout as follows:

1. Read page 0 and decode the first two bytes as the big-endian physical
   directory page number.
2. If the directory page number is greater than `1`, treat bytes before
   `directory_page * 4096` as a possible pre-directory resource area.
3. Read the object count from page-0 offsets `0x0004..0x0007`, and the
   picture-directory version from directory-page bytes `+9..+10`
   (`00 00` = 1.2, `01 03` = 1.3, `01 00` = 1.4). Group 0 always begins at
   page-0 offset `0x0118`.
4. Walk `object_count` 16-byte legacy descriptors in group 0. Decode id, kind,
   length, and offset as documented above. Reject an unrecognised kind byte.
5. Validate each descriptor by checking that `offset + length` is less than or
   equal to `directory_page * 4096`; image payloads must not overlap the logical
   BOO directory.
6. Export exactly the byte range `[offset, offset + length)` for each image.
   Do not convert the bytes to GIF/BMP/JPEG during extraction.

For version 1.4 converted objects:

1. Read the same page-0 object count at `0x0004..0x0007`.
2. Confirm directory-page bytes `+9..+10` are `01 00`.
3. Read group 1 entries as raw object-data descriptors and group 2 entries as
   description descriptors.
4. Decode group 2 description payloads from `00 xx` two-byte characters when a
   leading zero byte is present, then read MIME and dimension attributes from
   the resulting ASCII string.
5. Export the group 1 byte range exactly as stored. Converted GIF/JPEG/PNG/TIFF
   objects may already be standard web image files; legacy objects still remain
   legacy payloads and must not be converted by container extraction.

## Open Questions

- Identify the logical-record controls that reference image ids from document
  topics and figure lists.
- Determine whether non-image media resources use kind bytes beyond the verified
  local legacy `G` (`0xc7`) and `I` (`0xc9`) descriptor families.
- Whether a version-1.3 book can ever carry two *differing* descriptor groups.
  In all four repository 1.3 fixtures the two groups are byte-identical, so the
  purpose of the duplicate is unexplained.
- Whether a version-1.4 group-1 payload can hold a format other than GIF. Both
  1.4 fixtures declare only `type="image/gif"`, so JPEG, PNG, and TIFF
  descriptions remain unverified here.
