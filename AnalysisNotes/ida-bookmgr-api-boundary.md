# IDA BookServer API Boundary Notes

These are analysis notes for the active IDA database, not BOO file-format facts.
They describe where low-level BookManager access appears to live relative to the
currently attached `bookmgr.exe` IDB.

## Active IDB

- IDB: `Official Readers/BookSrv-Win32/bookmgr.exe.i64`
- Input binary: `Official Readers/BookSrv-Win32/bookmgr.exe`
- Observation date: 2026-06-14

The active `bookmgr.exe` IDB does not contain the low-level BOO container
parser. Instead, `bookmgr.exe` imports BookManager access routines from
`ephwam.dll`.

## Imported SCM Routines

| Imported routine | Import module | Observed role in `bookmgr.exe` |
| --- | --- | --- |
| `Scm_Bopen` | `ephwam` | Opens a book handle from a catalog/book path call site. |
| `Scm_Binfo` | `ephwam` | Returns book metadata used by catalog and search rendering code. |
| `Scm_Bkiopen` | `ephwam` | Opens a book index handle. |
| `Scm_BKIDatetime` | `ephwam` | Returns book-index date/time data to caller buffers. |
| `Scm_Loctopic` | `ephwam` | Iterates or locates book-index topics. |
| `Scm_Getctl` | `ephwam` | Reads named controls such as search result headings. |

## Call-Site Evidence

| Function | Evidence |
| --- | --- |
| `Catalog_AddBook` at `0x6c820` | Calls `Scm_Sopen(500, ..., "EPHW*.TAB", ...)`, then `Scm_Bopen(...)`, then `Catalog_AddOpenBook(...)`, and finally `Scm_Close(...)`. This establishes that book opening is delegated to `ephwam`. |
| `Catalog_AddOpenBook` at `0x6adc6` | Calls `Scm_Binfo(book_handle)` and consumes returned metadata fields, but does not parse raw BOO page bytes. |
| `sub_592E0` at `0x592e0` | Calls `Scm_Sopen(200, ..., "EPHW*.TAB", ...)`, `Scm_Bkiopen(...)`, `Scm_Loctopic(...)`, and `Scm_BKIDatetime(...)`, then copies the returned date/time through `Scm_Xoutcpy(...)`. This confirms index/date access is also delegated to `ephwam`. |

## Implication For Format Work

The active `bookmgr.exe` IDB validates the API boundary and shows that
BookServer obtains book metadata, index timestamps, topics, controls, and open
book handles through `ephwam` SCM APIs. It does not directly verify raw
page-0/page-1 BOO byte offsets.

Direct reader-code validation of BOO container fields requires analyzing
`ephwam.dll` or another module that implements the imported `Scm_*` routines.
