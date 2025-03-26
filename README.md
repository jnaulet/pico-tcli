# TinyCLI for picoRTOS

*TinyCLI* is a command line interface for embedded systems that is intended to be very easy to use.
Typical applications would e.g. be to provide a simple CLI over a serial line or over a Telnet connection.

This is a port of https://github.com/dpse/tcli for OpenPicoRTOS.

# What's different ?

In this version, the source code has been made compliant with OpenPicoRTOS' coding rules, which means:

 - any reference to printf/vprintf/vsprintf have been removed (temporarily ?)
 - function pointers have been removed (callbacks are statically linked)
 - NULL pointers checks have been outsourced to the static analysis
 - static_asserts have been replaced with preprocessor if checks (for sdcc compatibility)
