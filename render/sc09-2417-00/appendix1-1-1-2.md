[Previous](appendix1-1-1-1.md) | [Index](README.md) | [Next](appendix1-1-1-3.md)

---

### APPENDIX1\.1\.1\.2 Characters

<a id="HDRCHRS"></a>

- The source code page on Windows is the active code page set in `SYSTEM.INI`\. The compiler translates to an executable for AS/400 which is EBCDIC, and the default is CCSID 037\. The compiler can translate to other EBCDIC code pages with the `/AScp` compiler option\.
- When an integer character constant contains a character or escape sequence that is not represented in the basic execution character set, the `char` is assigned the character after the backslash and a warning is issued\. For example, `\q` is interpreted as `q`\.
- When a wide character constant contains a character or escape sequence that is not represented in the extended execution character set, the `wchar_t` is assigned the character after the backslash, and a warning is issued\.
- When an integer character constant contains more than one character, the last 4 bytes represent the character constant\.
- When a wide character constant contains more than one multibyte character, the last `wchar_t` value represents the character constant\.
- The default behavior for `char` is `unsigned`\.
- Any sequential spaces in your source program are interpreted as one space\.
- All spaces are retained for the listing file\.
- The escape sequence values for listed sequences are:

- **Sequence:** Value
- **`\a`:** `0x2F`
- **`\b`:** `0x16`
- **`\f`:** `0x12`
- **`\n`:** `0x25` \(Compiler option `ASi-` results in `0x15`\)
- **`\r`:** `0x13`
- **`\t`:** `0x05`
- **`\v`:** `0x11`

- Multibyte characters on AS/400 are enclosed in SO \(shift out\) hexadecimal value `0x0E`, and a SI \(shift in\) hexadecimal value `0x0F` characters\.
- A character is represented by 8 bits, as defined by the `CHAR_BIT` macro in `<limits.h>`\.
- Mapping is one\-to\-one between the members of the source character sets \(in character and string literals\) to members of the execution character set\.

---

[Previous](appendix1-1-1-1.md) | [Index](README.md) | [Next](appendix1-1-1-3.md)
