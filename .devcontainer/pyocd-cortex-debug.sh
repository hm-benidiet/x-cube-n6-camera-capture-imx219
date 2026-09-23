#!/usr/bin/env bash
# pyOCD wrapper for Cortex-Debug (used as "serverpath" in .vscode/launch.json).
#
# Cortex-Debug (1.12.x) waits for pyOCD to print "GDB server started on port",
# but current pyOCD prints "GDB server listening on port", so the debug session
# times out. Rewrite that line; pyOCD itself is exec'd so Cortex-Debug still
# stops the real process (and releases the probe) when the session ends.
exec pyocd "$@" > >(sed -u 's/GDB server listening on port/GDB server started on port/') 2>&1
