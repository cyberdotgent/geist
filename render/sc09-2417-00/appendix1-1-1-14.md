[Previous](appendix1-1-1-13.md) | [Index](README.md) | [Next](appendix1-1-1-15.md)

---

## APPENDIX1.1.1.14 Signals

<a id="HDRSGHD"></a>

- The set of signals for the `signal()` function and the parameters and usage of each signal are described in [Chapter 14, "Handling](4-3.md) [ [Exceptions" in topi](4-3.md) c 4.3 and in the C Library Reference under signal.
- `<` SIG_DFL is the default signal, and the default action taken is termination.
- If the equivalent of `signal(sig,` `SIG_DFL);` is not executed at the beginning of signal handler, no signal blocking is performed.
- << Whenever you leave a signal handler, it is `reset` to SIG_DFL.
- At program startup the default handling for SIGIO is set to `SIG_IGN`, and the default handling for SIGABRT is set to `SIG_DFL`.

---

[Previous](appendix1-1-1-13.md) | [Index](README.md) | [Next](appendix1-1-1-15.md)
