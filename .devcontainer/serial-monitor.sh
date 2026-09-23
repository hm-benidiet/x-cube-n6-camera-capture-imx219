#!/usr/bin/env bash
# Open the ST-Link virtual COM port (app console, 115200 8N1).
#
# Usage: serial-monitor [device] [baud]
#
# The container only sees /dev nodes that existed when it was started. If the
# board was plugged in afterwards, the tty is recreated here from the host's
# sysfs entry (the container runs privileged, so /sys reflects the host).
set -euo pipefail

DEV="${1:-}"
BAUD="${2:-115200}"

if [[ -z "$DEV" ]]; then
  for sys in /sys/class/tty/ttyACM*; do
    [[ -e "$sys" ]] || continue
    DEV="/dev/$(basename "$sys")"
    break
  done
fi

if [[ -z "$DEV" ]]; then
  echo "error: no /dev/ttyACM* found. Is the board connected via the ST-Link USB port (CN10)?" >&2
  exit 1
fi

if [[ ! -e "$DEV" ]]; then
  majmin="$(cat "/sys/class/tty/$(basename "$DEV")/dev")"
  echo "Creating $DEV (${majmin}) inside the container..."
  sudo mknod -m 0666 "$DEV" c "${majmin%%:*}" "${majmin##*:}"
fi

echo "Opening $DEV at $BAUD baud (exit with Ctrl-A Ctrl-X)"
exec picocom -b "$BAUD" "$DEV"
