[Previous](appendix1-1-1-5.md) | [Index](README.md) | [Next](appendix1-1-1-7.md)

---

### APPENDIX1\.1\.1\.6 Arrays and Pointers

<a id="HDRARPT"></a>

- The type of the integer required to hold the maximum size of an array \(the type of the `sizeof` operator, `size_t`\) is `unsigned int`
- The type of the integer required to hold the difference between two pointers to elements of the same array \(`ptrdiff_t`\) is `unsigned int`
- When you cast a pointer to an integer, the integer is assigned the offset value \(in bytes\) of the object from the beginning of the storage block\.
- When you cast an integer to a pointer, the pointer is set to `NULL`
- A 32 bit integer is required for a pointer to be converted to an integral type

---

[Previous](appendix1-1-1-5.md) | [Index](README.md) | [Next](appendix1-1-1-7.md)
