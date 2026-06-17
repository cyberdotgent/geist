# ImageMark `.FLT` Filter Modules

IBM BookManager and Transmogrifier use ImageMark filter modules with the file
extension `.FLT` to import and export image payloads. These files are Windows
PE DLLs with a different extension, not opaque data files.

## What They Are

In the BookManager reader assets, `.FLT` modules live beside `ISGDI32.DLL` in
`Official Readers/Transmogrifier/`. Observed examples include:

| File | Observed role |
| --- | --- |
| `IMGDF2.FLT` | Imports legacy GDF/vector payloads into ImageMark/ISGDI graphics state. |
| `EBGIF2.FLT` | Exports ImageMark/ISGDI graphics state to GIF. |
| `immet2.flt`, `emmet2.flt` | MET import/export filters. |
| `EMCGM2.FLT` | CGM export/import-family filter. |
| `imwmf2.flt` | WMF import filter. |

Although their suffix is `.FLT`, Windows loads them through the normal
`LoadLibraryA` path. IDA identifies them as 32-bit PE modules, and ordinary PE
tools can inspect their imports and exports.

## How They Work

The filters are plugins around `ISGDI32.DLL`, IBM's ImageMark/ISGDI graphics
runtime. The high-level path observed for BookManager legacy kind `G` is:

1. `ephimage.dll` extracts the raw BOO picture payload into memory.
2. `ephimage.dll` loads `isgdi32.dll`.
3. For kind `G`, it loads `imgdf2.flt`; for kind `M`/MET it loads
   `immet2.flt`; for kind `I`/MMR it uses a separate built-in decoder path.
4. The import filter parses the source payload and emits drawing calls into
   ISGDI, such as `CSetVDCExtent`, `CLine`, `CText`, and style setters.
5. `ephimage.dll` then loads an export filter such as `ebgif2.flt`, which
   reads the ImageMark/ISGDI graphics state and writes the requested output
   format.

`IMGDF2.FLT` exports functions such as `ImportGR`, `ImportEmbeddedGR`,
`CreateSetup`, `DestroySetup`, and `EscapeSetup`. `ImportGR` is the entry used
by the BookServer path for GDF payloads. It prepares an ImageMark setup object,
opens the source stream through `ISGDI32` `ims*` stream helpers, parses the
payload, and records result handles/extent values for the caller.

The filter module itself owns the payload grammar. For GDF, the important code
is in `IMGDF2.FLT`, not in `ephimage.dll` or `bookmgr.exe`; those caller modules
mainly select filters, allocate memory, and bridge between BOO resources and
ImageMark.

## How To Analyze One

Use both static and dynamic evidence when possible:

1. Identify the loader path.

   Start in the caller (`ephimage.dll` or `transmog.exe`) and find
   `LoadLibraryA`/`GetProcAddress` calls. This shows which `.FLT` file is used
   for each BOO kind and which exported entrypoints are invoked.

2. Inspect PE metadata.

   `dumpbin /exports` or IDA's entrypoint list quickly identifies exported
   ordinals and names. `dumpbin /imports` or IDA's import list shows calls into
   `ISGDI32.DLL`, especially `imsOpen`, `imsReadChar`, `imsSeek`, `CLine`,
   `CText`, and graphics attribute setters.

3. Load the `.FLT` directly in IDA.

   Open the `.FLT` file as a normal 32-bit PE. If IDA has already created an
   `.i64` database, reuse it. Use the export selected by the caller as the
   analysis root; for GDF this is `ImportGR`.

4. Follow the source stream setup.

   Many filters do not read files directly. They use `ISGDI32` stream helper
   functions (`imsOpen`, `imsReadChar`, `imsReadShort`, `imsReadDouble`,
   `imsSeek`, and `imsClose`). Track the state structure passed through these
   helpers to determine whether the source is a filename, a global-memory
   handle, or an embedded resource descriptor.

5. Find the parser loop.

   In `IMGDF2.FLT`, the useful path is:

   ```text
   ImportGR -> importer setup -> ImportFile -> parser initialization -> record loop
   ```

   The parser loop reads an opcode, determines a payload length, reads that
   many bytes into a state buffer, and dispatches on the opcode. Drawing
   handlers then call ISGDI functions. Xrefs to `CLine`, `CText`, and similar
   APIs are high-value anchors for decoding record semantics.

6. Recover record framing before interpreting payloads.

   For GDF, this was the key correction: the stream is opcode/length framed.
   Drawing every plausible coordinate-looking byte sequence created false
   geometry. Once the record header rule was recovered from `IMGDF2.FLT`,
   unsupported records could be skipped safely.

7. Build a small harness only when needed.

   If static analysis is ambiguous, build a temporary harness under `tmp/` that
   loads the original DLL/filter set and calls the same exported path as the
   reader. Keep all copied DLLs/filters and generated outputs under `tmp/` so
   the vendored reader directories are not modified.

## Practical Notes

- Match bitness. These filters are 32-bit modules; use a 32-bit harness when
  linking or calling into them.
- Current-directory and DLL-search behavior matters. If the caller uses
  `LoadLibraryA("imgdf2.flt")`, stage `ephimage.dll`, `ISGDI32.DLL`, and the
  needed `.FLT` files together in a temporary directory and run the harness
  from there.
- The export filter may fail even when the import grammar is understood. Treat
  high-level conversion failure as a runtime/setup fact, not proof that static
  parser analysis is wrong.
- Do not commit generated IDA sidecar files (`.id0`, `.id1`, `.id2`, `.nam`,
  `.til`) unless the project explicitly decides to track a database artifact.
- Document BOO storage facts in `Format/`; document filter loading, harness
  setup, IDA workflow, and runtime behavior here in `AnalysisNotes/`.
