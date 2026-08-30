[Previous](appendix1-1-1-3.md) | [Index](README.md) | [Next](appendix1-1-1-5.md)

---

### APPENDIX1\.1\.1\.4 Integers

<a id="HDRINTG"></a>

<a id="TBLTBLUNIQ6"></a>

```
    ________________________________________________________________________
   | Table 34. Integer Storage and Range                                    |
   |____________________________ ______________ ____________________________|
   | Type                       | Size (bits)  | Range (in <limits.h>)      |
   |____________________________|______________|____________________________|
   | char                       | 8            | 0 to 255                   |
   |____________________________|______________|____________________________|
   | signed char                | 8            | -128 to 127                |
   |____________________________|______________|____________________________|
   | unsigned char              | 8            | 0 to 255                   |
   |____________________________|______________|____________________________|
   | short                      | 16           | -32768 to 32767            |
   |____________________________|______________|____________________________|
   | signed short               | 16           | -32768 to 32767            |
   |____________________________|______________|____________________________|
   | unsigned short             | 16           | 0 to 65535                 |
   |____________________________|______________|____________________________|
   | int                        | 32           | -2147483647 to 2147483647  |
   |____________________________|______________|____________________________|
   | signed int                 | 32           | -2147483648 to 2147483647  |
   |____________________________|______________|____________________________|
   | unsigned int               | 32           | 0 to 4294967295            |
   |____________________________|______________|____________________________|
   | long                       | 32           | -2147483648 to 2147483647  |
   |____________________________|______________|____________________________|
   | signed long                | 32           | -2147483648 to 2147483647  |
   |____________________________|______________|____________________________|
   | unsigned long              | 32           | 0 to 4294967295            |
   |____________________________|______________|____________________________|
   | Note:  Do not use the values in this table as numbers in a source      |
   |        program. Use the macros defined in <limits.h> to represent      |
   |        these values.                                                   |
   |________________________________________________________________________|
```

- When you convert an integer to a `signed char`, the least\-significant byte of the integer represents the `char`
- When you convert an integer to a `short` signed integer, the least\-significant 2 bytes of the integer represents the `short int`
- When you convert an unsigned integer to a signed integer of equal length, if the value cannot be represented, the magnitude is preserved and the sign is not
- When bitwise operations \(OR, AND, XOR\) are performed on a `signed int`, the representation is treated as a bit pattern
- The remainder of integer division is negative if exactly one operand is negative
- When either operand of the divide operator is negative, the result is truncated to the integer value and the sign will be negative
- The result of a bitwise right shift of a negative signed integral type is sign extended
- The result of a bitwise right shift of a non\-negative signed integral type or an unsigned integral type is the same as the type of the left operand

---

[Previous](appendix1-1-1-3.md) | [Index](README.md) | [Next](appendix1-1-1-5.md)
