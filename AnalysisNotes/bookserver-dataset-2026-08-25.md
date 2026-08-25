# BookServer Dataset Expansion — 2026-08-25

## Selection and Source

Twenty BOO fixtures were downloaded from the hosted IBM BookManager
BookServer catalog on 2026-08-25. The catalog contained 5,819 unique
`BOOKS/<id>/CCONTENTS?DT=<timestamp>` entries.

Selection was deterministic: sort by the catalog's BookServer ID and select
the entries at the 20 inclusive alphabetic quantiles (indices produced by
`round(i * (5819 - 1) / 19)` for `i = 0..19`). Existing local fixtures would
have been skipped and the next catalog entry used, but none of these 20 server
IDs collided with an existing `BOO/` filename. This provides broad catalog
coverage without selecting books based on current renderer behavior.

The catalog source was:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/FINDBOOK?filter=&SUBMIT=Find
```

Each fixture was fetched from:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/download/<book-id>.boo?DT=<timestamp>
```

The Docker fetch MCP was not exposed in this session. Direct HTTP access to the
hosted CGI succeeded. Before repository insertion, every response reported
HTTP 200 and `application/x-boo`, its size matched `Content-Length`, and both
`booinfo` and `bootoc` parsed it successfully.

## Fixture Manifest

| File / BookServer ID | DT | Bytes | Topics | Document number | SHA-256 |
| --- | --- | ---: | ---: | --- | --- |
| `ACPZMST1.boo` | `19920319123146` | 245760 | 200 | GC24-5647-01 | `e3bb500ac4d168cd3a659ce451d2d6f3192cbfe291804278f572864717200e50` |
| `DREICMST.boo` | `19911219125856` | 323584 | 372 | SH19-6437-3 | `7925f4b4ec7894afbca05f2487d18f0bc9a0e8e5cb7d605ab144029e75d59751` |
| `FA1PLMM0.boo` | `19910927114801` | 589824 | 420 | SC33-6503-0 | `373438bcd488c420409406fb7465e665332f7044aa314b223cc515d2fe82a35d` |
| `GC23-046.boo` | `19920330095121` | 135168 | 99 | GC23-0469-01 | `dad82d578aff5eb0d197aa68579f76a1b751d6dd9c24db120c3c6fcbb6a9fc49` |
| `GC28-183.boo` | `19930625102617` | 389120 | 146 | GC28-1830-02 | `976e0cb5982bab3e9aa31caebdf26aaf1dd404d8d7341e864cc76ed52f469eb1` |
| `GG24-395.boo` | `19941215160749` | 1449984 | 226 | GG24-3950-01 | `530e0f8a0d9f3f1c6ebe795e6bc16de992943129d74457158f1bfdcfb237d70d` |
| `IBMMMSTR.boo` | `19911004151140` | 614400 | 60 | SC26-4309-2 | `918704528faa883efbdbde14ce26d8dd87f52927d4829fffe7161be7911e8f6c` |
| `PRG1SORT.boo` | `19900829171904` | 385024 | 207 | SC09-1164-01 | `7379a8122d46d56fa171da177052140adc799f8ecbf0266831ecc3e8bf652bd6` |
| `SC09-138.boo` | `19910321130500` | 1363968 | 536 | SC09-1384-00 | `ac1c96282e7b557c6070096464baf8b3bfa577286c3342334dfd5a09c827daac` |
| `SC24-546.boo` | `19940323131240` | 569344 | 321 | SC24-5466-04 | `57f8108194135bb17db0c2a764752bdf13c18509dce24459f3236b52eedaeab7` |
| `SC26-457.boo` | `19911220191142` | 2887680 | 361 | SC26-4570-01 | `ea7eb213dbf3adace205fa867ddd6906e0d1a72fde8f9d42e0f57e5c46899141` |
| `SC31-605.boo` | `19911015203151` | 327680 | 110 | SC31-6055-1 | `3e2a07f85a32aa8d09b5a6df30cc59e4cfbd4ce06202f8cd2a84ae09cd04022f` |
| `SC31-711.boo` | `19941010174546` | 258048 | 82 | SC31-7111-00 | `ac5dcb35e10f6e08107fc2e6e87420ad2652bf675c069eb2f4cb2606a5415700` |
| `SC33-033.boo` | `19930422134757` | 499712 | 236 | SC33-0333-00 | `5eba8b4990e161b2fd24ed8828811914a531d0f2171082b9f5234f96b355b76c` |
| `SC34-425.boo` | `19921112160049` | 2138112 | 257 | SC34-4254-03 | `e1eb7adadc1f96eaa2b564e2dc4e875a267c63be033d023901b7c135eded7a07` |
| `SC41-485.boo` | `19951003131222` | 225280 | 36 | SC41-4853-00 | `01dc238e07a4df9848225e2aee117c829ebb6f96692b17f5c74c7a3524a50d1b` |
| `SG24-204.boo` | `19971218054640` | 3117056 | 93 | SG24-2047-00 | `30d5ce942b2acd2dbada537952886b0a5a7fb11b6fa8663d42bb62ada3a77016` |
| `SH12-565.boo` | `19941206115523` | 434176 | 290 | SH12-5657-04 | `c97ca92b4fc9076141d4052bafab6d8e1e21ed64732e8add16fe6bd4f6729542` |
| `SH20-918.boo` | `19910520154851` | 507904 | 201 | SH20-9187-06 | `99ce2d9ec45f71bb396d2764860042fde3f6543b0c5d889570e216aa59c43bde` |
| `XWEBDEMO.boo` | `19970423182524` | 73728 | 13 | XWEBDEMO | `b70669be82ae573edb7593c0258266fcd01d4f49edddbd26304010f5c64a91b2` |

Total: 20 fixtures, 16,535,552 bytes, and 4,266 TOC topics.

## Rendering Audit

Each fixture is audited with the complete-book procedure in
`AnalysisNotes/whole-book-rendering-audits.md`. Hosted HTML and generated local
output remain scratch artifacts outside the repository; GitHub tracking issues
record the durable baseline, failures, and deduplicated defect classes.
