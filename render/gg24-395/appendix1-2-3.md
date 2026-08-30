[Previous](appendix1-2-2-1.md) | [Index](README.md) | [Next](appendix1-2-4.md)

---

## APPENDIX1\.2\.3 VM Protocols

<a id="HDRHPMA104"></a>

In the following table, VM protocols are featured\.

**Note:** The DCE CDS require the equivalent of the AIX/6000 DCE Cell Directory Services Server\. The same is true of the Security Services\.

<a id="TBLVMPROT"></a>

```
    _____________________________________________________________________________________________
   | Table 11. VM Protocols                                                                      |
   |______________________________ _______________ _______________ ______________________________|
   | Standard                     | Product       | Available     | Notes & References           |
   |______________________________|_______________|_______________|______________________________|
SI DCE
 | | DCE                          |               | 4Q95          | (294-159)                    |
 | |  Security Service            |               |               |                              |
 | |   Client                     | X             |               |                              |
 | |   Server                     |               |               |                              |
 | |  Cell Directory Service      |               |               |                              |
 | |   Client                     | X             |               |                              |
 | |   Server                     |               |               |                              |
 | |  RPC                         | X             |               |                              |
 | |  Threads                     | X             |               |                              |
   |______________________________|_______________|_______________|______________________________|
 | | PCL                          |               | Not Announced |                              |
   |______________________________|_______________|_______________|______________________________|
 | | PostScript                   | GDDM*         | Now           | Generate                     |
 | |                              |               |               | (294-280)                    |
   |                              |               |               |                              |
 | |                              | DCF*          | Now           | Generate                     |
 | |                              |               |               | (290-656)                    |
   |                              |               |               |                              |
 | |                              | BookMaster*   | Now           | Generate                     |
 | |                              |               |               | (292-339)                    |
   |                              |               |               |                              |
 | |                              | CATIA         | Now           | Generate                     |
 | |                              | Publishing*   |               | (289-073)                    |
   |                              |               |               |                              |
 | |                              | PostScript    | Now           | Receive                      |
 | |                              |  Interpreter  |               | (288-574,289-356)            |
 | |                              |  for AFP*     |               |                              |
   |                              |               |               |                              |
   |______________________________|_______________|_______________|______________________________|
 | | NetBIOS                      |               | Not Announced |                              |
   |______________________________|_______________|_______________|______________________________|
```

---

[Previous](appendix1-2-2-1.md) | [Index](README.md) | [Next](appendix1-2-4.md)
