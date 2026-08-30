[Previous](appendix1-2-1.md) | [Index](README.md) | [Next](appendix1-2-2-1.md)

---

## APPENDIX1\.2\.2 VM Interfaces and Profiles

<a id="HDRHPMA102"></a>

[Table 10](<#TBLVMSVC>) lists the interfaces supported by VM, with respect to Open Systems\. A star \(\*\) is used in this table to indicate where the IBM\-marketed product supports a standard that is part of the Open Blueprint

<a id="TBLVMSVC"></a>

```
    _____________________________________________________________________________________________
   | Table 10. VM Interfaces                                                                     |
   |______________________________ _______________ _______________ ______________________________|
   | Standard                     | Product       | Available     | Notes & References           |
   |______________________________|_______________|_______________|______________________________|
   | 1003.1-1990                  | VM/ESA V2     | 4Q95          | (294-525)                    |
   | 1001.1a                      |               |               |                              |
   | 1001.3c                      |               |               |                              |
   | 1003.2                       |               |               |                              |
   |______________________________|_______________|_______________|______________________________|
   | Berkeley Sockets             | TCP/IP for    | Now           | (290-308,291-701)            |
   |   BSD 4.3                    |  VM* V2R2     |  12/16/94     |  (294-526)                   |
   |                              |  V2R3         |               |                              |
   |______________________________|_______________|_______________|______________________________|
 | | CICS                         |               | Not Announced |                              |
   |______________________________|_______________|_______________|______________________________|
 | | CORBA                        |               | Not Announced |                              |
   |______________________________|_______________|_______________|______________________________|
   | CPI-C                        | VM/ESA*       | Now           | Includes Sync Point          |
   |                              |               |               |  Also see notes              |
   |                              |               |               |  (GC26-4675)                 |
   |______________________________|_______________|_______________|______________________________|
   | Database                     | SQL/DS*       |               | DRDA RUOW Support            |
   |   SQL-89 Level 2             |   V3R4        | Now           |  (293-064)                   |
   |                              |               |               |                              |
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
   | EDI                          |               | Not           |                              |
   |                              |               | Announced     |                              |
   |______________________________|_______________|_______________|______________________________|
   | FIPS 151                     |               | Statement of  | (293-061)                    |
   |                              |               | Direction     |                              |
   |______________________________|_______________|_______________|______________________________|
   |______________________________|_______________|_______________|______________________________|
     Graphics
      PHIGS



      GKS


      CGM

     GDDM/
      graPHIGS
       V2R2

     GDDM-GKS
       V1

     GDDM/VMXA
       V2R3
     GDDM/VM
       V3R1

     Now



     Now


     Now

     Now
     See notes
     (289-533,292-615)



     (287-066)


     (290-256)

     (292-495)
     MQI
     Not
     Announced
 |   OSF/Motif V1.1.2
 |   TCP/IP for VM*
 |   V2R2
 |   V2R3
 |   Now
 |   12/16/94
 |   (291-701)
 |   (294-526)
 |   TLPB
 |   Not
 |   Announced
 |   VIM
 |   Not
 |   Announced
 |   X/Open XA
 |   Not
 |   Announced
 |   XDS
 |   Not
 |   Announced
 |   XMP
 |   Not
 |   Announced
 |   XTI
 |   Not
 |   Announced
     Note:

     CPI-C          VM/SP R6 also implements all of the SAA Communications Interface except Sync
                    Point.

     Database       SQL/DS V3R2 and higher supports with FIPS PUB 127-1 that includes ANSI
                    X3.135-1989 and ANS X3.168-1989; and ISO/IEC 9075:1989 (without the optional
                    Integrity Enhancement Feature).  (290-524,293-064)

                    SQL/DS V3R4 provides FIPS flagger support for embedded SQL.  (293-064)

     Graphics       GDDM/graPHIGS Programming Interface V2 is based on the ANSI and ISO
                    standards.  (289-533)

                    GDDM/graPHIGS Programming Interface V2.2.3 supports the 1991 interim draft
                    PHIGS C bindings (ISO/IEC 9593-4).  (292-190)

                    The C and FORTRAN bindings of GDDM/graPHIGS Programming Interface V2.2.4 have
                    some limitations.  (292-615)

                    GDDM-GKS is an implementation of level 2b of GKS, ISO GKSM metafile.
                    GDDM-GKS supports ANSI GKS and ISO GKS with some limitations; see
                    announcement letter 287-066.

                    GDDM/VMXA V2R3 can receive and generate ISO/IEC 8632 CGM binary.  (290-256)
```

Subtopics:

- [APPENDIX1\.2\.2\.1 VM Profiles](appendix1-2-2-1.md)

---

[Previous](appendix1-2-1.md) | [Index](README.md) | [Next](appendix1-2-2-1.md)
