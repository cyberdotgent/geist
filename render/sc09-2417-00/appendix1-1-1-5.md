[Previous](appendix1-1-1-4.md) | [Index](README.md) | [Next](appendix1-1-1-6.md)

---

## APPENDIX1.1.1.5 Floating-Point Values

<a id="HDRFLTP"></a>

<a id="TBLTBLUNIQ7"></a>

Table 35. Floating Point

| Range of Exponents (base | Type | Size (bits) |
| --- | --- | --- |
| 10) (in <float.h>) | float (32-bit) | 32 |
| 1.17549435e-38 to<br>3.40282347e+38 | double (64-bit) | 64 |
| 2.2250738585072014e-308 to<br>1.7976931348623157e+308 | long double (64-bit) | 64<br>2.2250738585072014e-308 to<br>1.7976931348623157e+308 |


- [ When an integral number is converted to a floating-point number that cannot exactly represent the original value, it is truncated to the nearest representable value
- ++ When a floating-point number is converted to a narrower floating-point number, it is rounded to the nearest representable value

---

[Previous](appendix1-1-1-4.md) | [Index](README.md) | [Next](appendix1-1-1-6.md)
