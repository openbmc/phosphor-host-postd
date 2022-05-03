# LPC Snoop Broadcast Daemon

This is a simple daemon which reads a file interface from an lpc-snoop driver
and broadcasts the values read on DBus.

It presently assumes there's /dev/aspeed-lpc-snoop0, however this could be made
a command line parameter.
