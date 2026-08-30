[Previous](appendix1-1-1-14.md) | [Index](README.md) | [Next](appendix1-1-1-16.md)

---

### APPENDIX1\.1\.1\.15 Translation

<a id="HDRTRLN"></a>

- Each non\-empty sequence of white\-space characters other than new\-line is replaced by one space character\.
- A diagnostic message is identified as `msg_id severity text` where `msg_id` consists of 7 characters\. The first three characters are upper case letters and the last 4 characters are decimal digits\.
- The message classes are:

<a id="TBLTBLUNIQ9"></a>

Table 37\. Message Class

| Message Class | Description | Return Status<br>Code |
| --- | --- | --- |
| Informational | Advises of conditions found during<br>compilation\. Compilation<br>continues\. | 0 |
| Warning | Warns of a possible error\.<br>Compilation continues\. | 4 |
| Error | Conditions that the compiler can<br>correct\. Compilation continues\. | 8 |
| Severe | Conditions that the compiler<br>cannot correct\. | 12 |
| Terminal | Internal error\. | 16 |

- The level of the diagnostic is controlled through the message limit, message severity limit and message flagging options in the `iccas` command\.

---

[Previous](appendix1-1-1-14.md) | [Index](README.md) | [Next](appendix1-1-1-16.md)
