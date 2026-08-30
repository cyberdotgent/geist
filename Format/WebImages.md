# Version 1.4 Web-Compatible Image Payloads

This note documents image-format details for version 1.4 converted object
payloads. The BOO container descriptor groups and description-payload storage
are documented in [assets.md](assets.md).

A version 1.4 group-1 payload is a complete, ordinary web image file stored
verbatim as a byte range. It is not wrapped, framed, or typed by the container:
its format is declared only by the `type` attribute in the matching group-2
description, and confirmed by the payload's own file signature.

## Verified

Both version-1.4 fixtures in this repository store GIF:

| Fixture | Objects | Group-1 payload signatures | Group-2 `type` |
| --- | ---: | --- | --- |
| `XWEBDEMO.boo` | 2 | `GIF89a` for both | `image/gif` |
| `Official Readers/SoftCopy/HLCRUG21.boo` | 135 | `GIF87a` / `GIF89a` | `image/gif` |

The description also carries the pixel dimensions, and those match the image's
own header. For `XWEBDEMO.boo` object `1`, the description reads
`type="image/gif"width="620"height="480"`, and the GIF logical screen
descriptor at payload offset `0x06` reads `6c 02` / `e0 01` little-endian --
620 by 480.

Because the dimensions are duplicated this way, a reader can either trust the
description or parse the image header; the two agree in every verified object.

## Unverified

The description attribute is a MIME type string, so formats other than GIF are
expressible:

```text
type="image/jpeg"
type="image/png"
```

No repository fixture and no hosted book observed so far stores a non-GIF
version-1.4 object, so JPEG, PNG, TIFF and CGM payloads are recorded here as
**unverified**. An implementer should dispatch on the description `type`
attribute and validate against the payload's own signature rather than assume
GIF.

Percentage-valued `width` / `height` attributes are likewise possible in
principle -- the attribute value is a quoted string, not a fixed-width number --
but every observed description carries absolute pixel values.
