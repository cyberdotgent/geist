[Previous](appendix1-1-1-11.md) | [Index](README.md) | [Next](appendix1-1-1-13.md)

---

## APPENDIX1.1.1.12 Library Functions

<a id="HDRLIBFU"></a>

- ] In extended mode (compile`r o`ption /Se) and for all C++ programs, the `NULL` macro is defined to be `0`.
- When `assert` is executed, if the expression is false, the diagnostic message written by the `assert` macro has the format:

```text
<<          Assertion failed:  expression, file file_name, line line_number
```

- The characters tested by the `isalnum()`, `isalpha()`, `iscntrl()`,`islower`, `isprint()` and `isupper()` functions, are:

<a id="TBLTBLUNIQ8"></a>

Table 36. Characters Tested

| Characters Tested | Function | A-Z, a-z, 0-9 |
| --- | --- | --- |
| A-Z, a-z | isalpha() | anything with 0x00 to 0x40, 0x42 |
| to 0x47, 0x50 to 0x61, or 0x63. | a-z | islower() |


- << The value returned by all math functions after a domain error (EDOM) is zero.
- The value `errno` is set the value of the macro ERANGE on underflow range errors.
- ++ If you `call` the fmod() func`t`ion with 0 as the second `argume`nt, fmod() returns `0` and a domain error.

---

[Previous](appendix1-1-1-11.md) | [Index](README.md) | [Next](appendix1-1-1-13.md)
