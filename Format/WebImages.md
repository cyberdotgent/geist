# Version 1.4 Web-Compatible Image Payloads

This note documents image-format details for version 1.4 converted object
payloads. The BOO container descriptor groups and description-payload storage
are documented in [assets.md](assets.md).

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
legacy version 1.2/1.3 payloads are typed by the 1-byte descriptor kind. Version
1.4 payloads are normal object byte ranges with object descriptions such as
`type="image/gif"`, `width="..."`, and `height="..."`.
