<!SGML "ISO 8879:1986"

--====================================================================
     Copyright: (c) International Business Machines Corporation 1992, 2000

Classification:  Not classified

       Version:  4.3.5

          Name:  IBM Information Development Document SGML Declaration
                 for Codepage 01114 for use under OS/2.

          File:  IDDTC950.DCL

 Formal Public
   Identifiers:
                 +//ISBN 0-933186::IBM//TEXT IBMIDDoc SGML Declaration/Traditional Chinese SBCS IBM CP 01114/OS2//EN

      Function:  Markup declarations for the IBMIDDOC document type

Change History:   6 Nov 95  - Created declaration
                 16 Aug 96  - Add underscore to name character list.
                  5 Aug 96  - Increase capacities to avoid extraneous error messages.
                 19 Jan 99  - Increased TOTCAP and IDREFCAP
                 26 Sep 00  - Increased NAMELEN
                            - Changed SUBDOC to NO to match IBMIDDOC.DCL
                 12 Oct 00  - Decreased NAMELEN back to 32
                 16 Oct 00  - Increased NAMELEN to 64

         Notes:
                 1) This SGML declaration represents IBM CP 01114
                    which is the single byte codepage for CCSID 950.
                 2) This SGML declaration only defines character positions
                    x'20 - x'ff.  The graphic characters defined for CP
                    01041 in the x'01 - x'1f range may not be used and
                    must be expressed using character entities defined for
                    that purpose in the DTD.
                 3) The syntax section must have the characters above x'7f
                    set defined since MSSCHAR function is being assigned
                    to many of them.

  ====================================================================--

   CHARSET
      BASESET
         "ISO 646-1983//CHARSET International Reference Version (IRV)//ESC 2/5 4/0"
      DESCSET
          0   9   UNUSED
          9   1   9              -- HT --
         10   1   10             -- LF (RS) --
         11   2   UNUSED
         13   1   13             -- CR (RE) --
         14   2   UNUSED
         16   1   "Character graphic arrow right"
         17   1   "Character graphic arrow left"
         18  12   UNUSED
         30   1   "Character graphic arrow up"
         31   1   "Character graphic arrow down"
         32   4   32
         36   1   "Dollar sign"
         37  91   37
                                                                -- 80 --
        128   1   UNUSED
        129   1   "Double byte introducer x'81"
        130   1   "Double byte introducer x'82"
        131   1   "Double byte introducer x'83"
        132   1   "Double byte introducer x'84"
        133   1   "Double byte introducer x'85"
        134   1   "Double byte introducer x'86"
        135   1   "Double byte introducer x'87"
        136   1   "Double byte introducer x'88"
        137   1   "Double byte introducer x'89"
        138   1   "Double byte introducer x'8A"
        139   1   "Double byte introducer x'8B"
        140   1   "Double byte introducer x'8C"
        141   1   "Double byte introducer x'8D"
        142   1   "Double byte introducer x'8E"
        143   1   "Double byte introducer x'8F"
                                                                 -- 90 --
        144   1   "Double byte introducer x'90"
        145   1   "Double byte introducer x'91"
        146   1   "Double byte introducer x'92"
        147   1   "Double byte introducer x'93"
        148   1   "Double byte introducer x'94"
        149   1   "Double byte introducer x'95"
        150   1   "Double byte introducer x'96"
        151   1   "Double byte introducer x'97"
        152   1   "Double byte introducer x'98"
        153   1   "Double byte introducer x'99"
        154   1   "Double byte introducer x'9A"
        155   1   "Double byte introducer x'9B"
        156   1   "Double byte introducer x'9C"
        157   1   "Double byte introducer x'9D"
        158   1   "Double byte introducer x'9E"
        159   1   "Double byte introducer x'9F"
                                                                 -- A0 --
        160   1   "Double byte introducer x'A0"
        161   1   "Double byte introducer x'A1"
        162   1   "Double byte introducer x'A2"
        163   1   "Double byte introducer x'A3"
        164   1   "Double byte introducer x'A4"
        165   1   "Double byte introducer x'A5"
        166   1   "Double byte introducer x'A6"
        167   1   "Double byte introducer x'A7"
        168   1   "Double byte introducer x'A8"
        169   1   "Double byte introducer x'A9"
        170   1   "Double byte introducer x'AA"
        171   1   "Double byte introducer x'AB"
        172   1   "Double byte introducer x'AC"
        173   1   "Double byte introducer x'AD"
        174   1   "Double byte introducer x'AE"
        175   1   "Double byte introducer x'AF"
                                                                 -- B0 --
        176   1   "Double byte introducer x'B0"
        177   1   "Double byte introducer x'B1"
        178   1   "Double byte introducer x'B2"
        179   1   "Double byte introducer x'B3"
        180   1   "Double byte introducer x'B4"
        181   1   "Double byte introducer x'B5"
        182   1   "Double byte introducer x'B6"
        183   1   "Double byte introducer x'B7"
        184   1   "Double byte introducer x'B8"
        185   1   "Double byte introducer x'B9"
        186   1   "Double byte introducer x'BA"
        187   1   "Double byte introducer x'BB"
        188   1   "Double byte introducer x'BC"
        189   1   "Double byte introducer x'BD"
        190   1   "Double byte introducer x'BE"
        191   1   "Double byte introducer x'BF"
                                                                 -- C0 --
        192   1   "Double byte introducer x'C0"
        193   1   "Double byte introducer x'C1"
        194   1   "Double byte introducer x'C2"
        195   1   "Double byte introducer x'C3"
        196   1   "Double byte introducer x'C4"
        197   1   "Double byte introducer x'C5"
        198   1   "Double byte introducer x'C6"
        199   1   "Double byte introducer x'C7"
        200   1   "Double byte introducer x'C8"
        201   1   "Double byte introducer x'C9"
        202   1   "Double byte introducer x'CA"
        203   1   "Double byte introducer x'CB"
        204   1   "Double byte introducer x'CC"
        205   1   "Double byte introducer x'CD"
        206   1   "Double byte introducer x'CE"
        207   1   "Double byte introducer x'CF"
                                                                 -- D0 --
        208   1   "Double byte introducer x'D0"
        209   1   "Double byte introducer x'D1"
        210   1   "Double byte introducer x'D2"
        211   1   "Double byte introducer x'D3"
        212   1   "Double byte introducer x'D4"
        213   1   "Double byte introducer x'D5"
        214   1   "Double byte introducer x'D6"
        215   1   "Double byte introducer x'D7"
        216   1   "Double byte introducer x'D8"
        217   1   "Double byte introducer x'D9"
        218   1   "Double byte introducer x'DA"
        219   1   "Double byte introducer x'DB"
        220   1   "Double byte introducer x'DC"
        221   1   "Double byte introducer x'DD"
        222   1   "Double byte introducer x'DE"
        223   1   "Double byte introducer x'DF"
                                                                 -- E0 --
        224   1   "Double byte introducer x'E0"
        225   1   "Double byte introducer x'E1"
        226   1   "Double byte introducer x'E2"
        227   1   "Double byte introducer x'E3"
        228   1   "Double byte introducer x'E4"
        229   1   "Double byte introducer x'E5"
        230   1   "Double byte introducer x'E6"
        231   1   "Double byte introducer x'E7"
        232   1   "Double byte introducer x'E8"
        233   1   "Double byte introducer x'E9"
        234   1   "Double byte introducer x'EA"
        235   1   "Double byte introducer x'EB"
        236   1   "Double byte introducer x'EC"
        237   1   "Double byte introducer x'ED"
        238   1   "Double byte introducer x'EE"
        239   1   "Double byte introducer x'EF"
                                                                 -- F0 --
        240   1   "Double byte introducer x'F0"
        241   1   "Double byte introducer x'F1"
        242   1   "Double byte introducer x'F2"
        243   1   "Double byte introducer x'F3"
        244   1   "Double byte introducer x'F4"
        245   1   "Double byte introducer x'F5"
        246   1   "Double byte introducer x'F6"
        247   1   "Double byte introducer x'F7"
        248   1   "Double byte introducer x'F8"
        249   1   "Double byte introducer x'F9"
        250   1   "Double byte introducer x'FA"
        251   1   "Double byte introducer x'FB"
        252   1   "Double byte introducer x'FC"
        253   1   "Double byte introducer x'FD"
        254   1   "Double byte introducer x'FE"
        255   1   UNUSED

-- The CAPACITY specifications are based solely on DTD requirements.   --
   CAPACITY SGMLREF
      TOTALCAP     10000000
      ENTCAP        1000000
      ENTCHCAP       500000
      ELEMCAP         70000
      GRPCAP         380000
      EXGRPCAP        70000
      EXNMCAP        100000
      ATTCAP         480000
      ATTCHCAP        85000
      AVGRPCAP       270000
      NOTCAP          70000
      NOTCHCAP        35000
      IDCAP         2000000
      IDREFCAP      3000000
      MAPCAP          35000
      LKSETCAP        35000
      LKNMCAP         35000

   SCOPE DOCUMENT

   SYNTAX
      SHUNCHAR
           0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
                18 19 20 21 22 23 24 25 26 27 28 29
      BASESET
         "ISO 646-1983//CHARSET International Reference Version (IRV)//ESC 2/5 4/0"
      DESCSET
          0   9   UNUSED
          9   1   9              -- HT --
         10   1   10             -- LF (RS) --
         11   2   UNUSED
         13   1   13             -- CR (RE) --
         14  18   UNUSED
         32   4   32
         36   1   "Dollar sign"
         37  91   37
                                                                -- 80 --
        128   1   UNUSED
        129   1   "Double byte introducer x'81"
        130   1   "Double byte introducer x'82"
        131   1   "Double byte introducer x'83"
        132   1   "Double byte introducer x'84"
        133   1   "Double byte introducer x'85"
        134   1   "Double byte introducer x'86"
        135   1   "Double byte introducer x'87"
        136   1   "Double byte introducer x'88"
        137   1   "Double byte introducer x'89"
        138   1   "Double byte introducer x'8A"
        139   1   "Double byte introducer x'8B"
        140   1   "Double byte introducer x'8C"
        141   1   "Double byte introducer x'8D"
        142   1   "Double byte introducer x'8E"
        143   1   "Double byte introducer x'8F"
                                                                 -- 90 --
        144   1   "Double byte introducer x'90"
        145   1   "Double byte introducer x'91"
        146   1   "Double byte introducer x'92"
        147   1   "Double byte introducer x'93"
        148   1   "Double byte introducer x'94"
        149   1   "Double byte introducer x'95"
        150   1   "Double byte introducer x'96"
        151   1   "Double byte introducer x'97"
        152   1   "Double byte introducer x'98"
        153   1   "Double byte introducer x'99"
        154   1   "Double byte introducer x'9A"
        155   1   "Double byte introducer x'9B"
        156   1   "Double byte introducer x'9C"
        157   1   "Double byte introducer x'9D"
        158   1   "Double byte introducer x'9E"
        159   1   "Double byte introducer x'9F"
                                                                 -- A0 --
        160   1   "Double byte introducer x'A0"
        161   1   "Double byte introducer x'A1"
        162   1   "Double byte introducer x'A2"
        163   1   "Double byte introducer x'A3"
        164   1   "Double byte introducer x'A4"
        165   1   "Double byte introducer x'A5"
        166   1   "Double byte introducer x'A6"
        167   1   "Double byte introducer x'A7"
        168   1   "Double byte introducer x'A8"
        169   1   "Double byte introducer x'A9"
        170   1   "Double byte introducer x'AA"
        171   1   "Double byte introducer x'AB"
        172   1   "Double byte introducer x'AC"
        173   1   "Double byte introducer x'AD"
        174   1   "Double byte introducer x'AE"
        175   1   "Double byte introducer x'AF"
                                                                 -- B0 --
        176   1   "Double byte introducer x'B0"
        177   1   "Double byte introducer x'B1"
        178   1   "Double byte introducer x'B2"
        179   1   "Double byte introducer x'B3"
        180   1   "Double byte introducer x'B4"
        181   1   "Double byte introducer x'B5"
        182   1   "Double byte introducer x'B6"
        183   1   "Double byte introducer x'B7"
        184   1   "Double byte introducer x'B8"
        185   1   "Double byte introducer x'B9"
        186   1   "Double byte introducer x'BA"
        187   1   "Double byte introducer x'BB"
        188   1   "Double byte introducer x'BC"
        189   1   "Double byte introducer x'BD"
        190   1   "Double byte introducer x'BE"
        191   1   "Double byte introducer x'BF"
                                                                 -- C0 --
        192   1   "Double byte introducer x'C0"
        193   1   "Double byte introducer x'C1"
        194   1   "Double byte introducer x'C2"
        195   1   "Double byte introducer x'C3"
        196   1   "Double byte introducer x'C4"
        197   1   "Double byte introducer x'C5"
        198   1   "Double byte introducer x'C6"
        199   1   "Double byte introducer x'C7"
        200   1   "Double byte introducer x'C8"
        201   1   "Double byte introducer x'C9"
        202   1   "Double byte introducer x'CA"
        203   1   "Double byte introducer x'CB"
        204   1   "Double byte introducer x'CC"
        205   1   "Double byte introducer x'CD"
        206   1   "Double byte introducer x'CE"
        207   1   "Double byte introducer x'CF"
                                                                 -- D0 --
        208   1   "Double byte introducer x'D0"
        209   1   "Double byte introducer x'D1"
        210   1   "Double byte introducer x'D2"
        211   1   "Double byte introducer x'D3"
        212   1   "Double byte introducer x'D4"
        213   1   "Double byte introducer x'D5"
        214   1   "Double byte introducer x'D6"
        215   1   "Double byte introducer x'D7"
        216   1   "Double byte introducer x'D8"
        217   1   "Double byte introducer x'D9"
        218   1   "Double byte introducer x'DA"
        219   1   "Double byte introducer x'DB"
        220   1   "Double byte introducer x'DC"
        221   1   "Double byte introducer x'DD"
        222   1   "Double byte introducer x'DE"
        223   1   "Double byte introducer x'DF"
                                                                 -- E0 --
        224   1   "Double byte introducer x'E0"
        225   1   "Double byte introducer x'E1"
        226   1   "Double byte introducer x'E2"
        227   1   "Double byte introducer x'E3"
        228   1   "Double byte introducer x'E4"
        229   1   "Double byte introducer x'E5"
        230   1   "Double byte introducer x'E6"
        231   1   "Double byte introducer x'E7"
        232   1   "Double byte introducer x'E8"
        233   1   "Double byte introducer x'E9"
        234   1   "Double byte introducer x'EA"
        235   1   "Double byte introducer x'EB"
        236   1   "Double byte introducer x'EC"
        237   1   "Double byte introducer x'ED"
        238   1   "Double byte introducer x'EE"
        239   1   "Double byte introducer x'EF"
                                                                 -- F0 --
        240   1   "Double byte introducer x'F0"
        241   1   "Double byte introducer x'F1"
        242   1   "Double byte introducer x'F2"
        243   1   "Double byte introducer x'F3"
        244   1   "Double byte introducer x'F4"
        245   1   "Double byte introducer x'F5"
        246   1   "Double byte introducer x'F6"
        247   1   "Double byte introducer x'F7"
        248   1   "Double byte introducer x'F8"
        249   1   "Double byte introducer x'F9"
        250   1   "Double byte introducer x'FA"
        251   1   "Double byte introducer x'FB"
        252   1   "Double byte introducer x'FC"
        253   1   "Double byte introducer x'FD"
        254   1   "Double byte introducer x'FE"
        255   1   UNUSED

      FUNCTION
         RE       13
         RS       10
         SPACE    32
         TAB      SEPCHAR   9 -- SGMLS only supports the RCS --
         SS081X   MSSCHAR 129
         SS082X   MSSCHAR 130
         SS083X   MSSCHAR 131
         SS084X   MSSCHAR 132
         SS085X   MSSCHAR 133
         SS086X   MSSCHAR 134
         SS087X   MSSCHAR 135
         SS088X   MSSCHAR 136
         SS089X   MSSCHAR 137
         SS08AX   MSSCHAR 138
         SS08BX   MSSCHAR 139
         SS08CX   MSSCHAR 140
         SS08DX   MSSCHAR 141
         SS08EX   MSSCHAR 142
         SS08FX   MSSCHAR 143

         SS090X   MSSCHAR 144
         SS091X   MSSCHAR 145
         SS092X   MSSCHAR 146
         SS093X   MSSCHAR 147
         SS094X   MSSCHAR 148
         SS095X   MSSCHAR 149
         SS096X   MSSCHAR 150
         SS097X   MSSCHAR 151
         SS098X   MSSCHAR 152
         SS099X   MSSCHAR 153
         SS09AX   MSSCHAR 154
         SS09BX   MSSCHAR 155
         SS09CX   MSSCHAR 156
         SS09DX   MSSCHAR 157
         SS09EX   MSSCHAR 158
         SS09FX   MSSCHAR 159

         SS0A0X   MSSCHAR 160
         SS0A1X   MSSCHAR 161
         SS0A2X   MSSCHAR 162
         SS0A3X   MSSCHAR 163
         SS0A4X   MSSCHAR 164
         SS0A5X   MSSCHAR 165
         SS0A6X   MSSCHAR 166
         SS0A7X   MSSCHAR 167
         SS0A8X   MSSCHAR 168
         SS0A9X   MSSCHAR 169
         SS0AAX   MSSCHAR 170
         SS0ABX   MSSCHAR 171
         SS0ACX   MSSCHAR 172
         SS0ADX   MSSCHAR 173
         SS0AEX   MSSCHAR 174
         SS0AFX   MSSCHAR 175

         SS0B0X   MSSCHAR 176
         SS0B1X   MSSCHAR 177
         SS0B2X   MSSCHAR 178
         SS0B3X   MSSCHAR 179
         SS0B4X   MSSCHAR 180
         SS0B5X   MSSCHAR 181
         SS0B6X   MSSCHAR 182
         SS0B7X   MSSCHAR 183
         SS0B8X   MSSCHAR 184
         SS0B9X   MSSCHAR 185
         SS0BAX   MSSCHAR 186
         SS0BBX   MSSCHAR 187
         SS0BCX   MSSCHAR 188
         SS0BDX   MSSCHAR 189
         SS0BEX   MSSCHAR 190
         SS0BFX   MSSCHAR 191

         SS0C0X   MSSCHAR 192
         SS0C1X   MSSCHAR 193
         SS0C2X   MSSCHAR 194
         SS0C3X   MSSCHAR 195
         SS0C4X   MSSCHAR 196
         SS0C5X   MSSCHAR 197
         SS0C6X   MSSCHAR 198
         SS0C7X   MSSCHAR 199
         SS0C8X   MSSCHAR 200
         SS0C9X   MSSCHAR 201
         SS0CAX   MSSCHAR 202
         SS0CBX   MSSCHAR 203
         SS0CCX   MSSCHAR 204
         SS0CDX   MSSCHAR 205
         SS0CEX   MSSCHAR 206
         SS0CFX   MSSCHAR 207

         SS0D0X   MSSCHAR 208
         SS0D1X   MSSCHAR 209
         SS0D2X   MSSCHAR 210
         SS0D3X   MSSCHAR 211
         SS0D4X   MSSCHAR 212
         SS0D5X   MSSCHAR 213
         SS0D6X   MSSCHAR 214
         SS0D7X   MSSCHAR 215
         SS0D8X   MSSCHAR 216
         SS0D9X   MSSCHAR 217
         SS0DAX   MSSCHAR 218
         SS0DBX   MSSCHAR 219
         SS0DCX   MSSCHAR 220
         SS0DDX   MSSCHAR 221
         SS0DEX   MSSCHAR 222
         SS0DFX   MSSCHAR 223

         SS0E0X   MSSCHAR 224
         SS0E1X   MSSCHAR 225
         SS0E2X   MSSCHAR 226
         SS0E3X   MSSCHAR 227
         SS0E4X   MSSCHAR 228
         SS0E5X   MSSCHAR 229
         SS0E6X   MSSCHAR 230
         SS0E7X   MSSCHAR 231
         SS0E8X   MSSCHAR 232
         SS0E9X   MSSCHAR 233
         SS0EAX   MSSCHAR 234
         SS0EBX   MSSCHAR 235
         SS0ECX   MSSCHAR 236
         SS0EDX   MSSCHAR 237
         SS0EEX   MSSCHAR 238
         SS0EFX   MSSCHAR 239

         SS0F0X   MSSCHAR 240
         SS0F1X   MSSCHAR 241
         SS0F2X   MSSCHAR 242
         SS0F3X   MSSCHAR 243
         SS0F4X   MSSCHAR 244
         SS0F5X   MSSCHAR 245
         SS0F6X   MSSCHAR 246
         SS0F7X   MSSCHAR 247
         SS0F8X   MSSCHAR 248
         SS0F9X   MSSCHAR 249
         SS0FAX   MSSCHAR 250
         SS0FBX   MSSCHAR 251
         SS0FCX   MSSCHAR 252
         SS0FDX   MSSCHAR 253
         SS0FEX   MSSCHAR 254
      NAMING
         LCNMSTRT ""
         UCNMSTRT ""
         LCNMCHAR "&#45;&#46;&#95;"                    -- '-' '.' '_' --
         UCNMCHAR "&#45;&#46;&#95;"                    -- '-' '.' '_' --
      NAMECASE
         GENERAL  YES
         ENTITY   NO
      DELIM
         GENERAL  SGMLREF     -- Using RCS delimiter set               --
         SHORTREF NONE        -- SHORTREF delimiters may be defined
                                 for editing DTDs.                     --
      NAMES    SGMLREF
      QUANTITY SGMLREF
         ATTCNT      180      -- TBD based on DTD requirements         --
         ATTSPLEN  16000      -- Would like to increase to 64K         --
         GRPCNT       64      -- TBD based on DTD requirements         --
         LITLEN     2400      -- Would like to increase to 64K         --
         NAMELEN      64      -- Increased to 64 on 2000-10-16         --
         PILEN      2400      -- Would like to increase to 64K         --
         TAGLEN    16000      -- Would like to increase to 64K         --
         TAGLVL      100      -- Increased to 100 on 1996-08-05        --

   FEATURES
      MINIMIZE
         DATATAG  NO
         OMITTAG  YES
         RANK     NO
         SHORTTAG YES
      LINK
         SIMPLE   NO
         IMPLICIT NO
         EXPLICIT NO
      OTHER
         CONCUR   NO
         SUBDOC   NO
         FORMAL   YES

   APPINFO
     "InfoMaster InfoMast
      BookMaster BookMaster
      SDATA
         InfoMaster (IFM: USE)
         BookMaster (BKM: USE)
         Other      (=ALL IGNORE)
     "
>
