# Transmogrifier Runtime Notes

The Transmogrifier utility was added under `Official Readers/Transmogrifier/`.
It is useful as static evidence for the BOO picture conversion pipeline and has
an active IDA Pro instance for `transmog.exe`.

## Runtime Attempt

On 2026-06-14, `transmog.exe` was tested against a copy of
`BOO/GG24-4302-00.boo` outside the repository:

```text
C:\tmp\geist-transmog-f0a71822\GG244302.BOO
```

The utility was run from its own directory so `ISGDI32.DLL` and the filter files
were on the DLL search path:

```text
Official Readers\Transmogrifier\transmog.exe C:\tmp\geist-transmog-f0a71822\GG244302.BOO C:\tmp\geist-transmog-f0a71822\OUT
```

It created:

```text
C:\tmp\geist-transmog-f0a71822\GG244302.pic
C:\tmp\geist-transmog-f0a71822\OUT
```

Then it failed before producing converted image files or a converted BOO:

```text
The specified module could not be found.
```

The static IDA analysis remains usable for the conversion pipeline and converted
version 1.4 object layout, but a byte-for-byte original/converted BOO comparison
still needs the missing runtime dependency resolved.
