[Previous](appendix1-1-1-6.md) | [Index](README.md) | [Next](appendix1-1-1-8.md)

---

## APPENDIX1.1.1.7 Structures, Unions, Enumerations, Bit-Fields

<a id="HDRSUEB"></a>

- If a member of a union object is accessed using a member of a different type, the result is undefined.
- The alignment of the most strictly aligned members are:

**Type** Alignment `char` 1 byte shor`t` 2 byte `int` 4 byte `long` 4 byte `float` 4 byte `double` 8 byte ``long` d`ouble 8 byte `pointer` 16 byte

- The default type of an integer bit field is `unsigned` `int`.
- Bit fields are allocated from low memory to high memory.
- Bit fields can cross storage unit boundaries.
- < The maximum bit field length is 32 bits. If a series of bit fields [ does not add up to the si`ze` of an int, padding may take place.
- A bit field cannot have type `long` `double`.
- The expression that defines the value of an enumeration constant cannot have type `long` `double`.
- An enumeration can have the type `unsigned` `char`, or `signed` `short`, or `signed` `int`. In C++, enumerations are a distinct type, and although they may be the same size as a data type such as `char`, they are not considered to be of that type.

---

[Previous](appendix1-1-1-6.md) | [Index](README.md) | [Next](appendix1-1-1-8.md)
