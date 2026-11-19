#!/bin/bash
# Boot a real ARM64 Linux kernel in qemu-system-aarch64 with LithoOS
# litho-init as PID 1. Prints the 3+1 Timing Handshake ledger (and the
# legal GB demo hash) on the serial console, then reboots so -no-reboot exits.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
KERNEL="${KERNEL:-/tmp/kimg/debian-linux}"
INIT="${INIT:-$ROOT/build/aarch64/litho-init}"
ROM="${ROM:-$ROOT/roms/litho-demo.gb}"
OUT="${OUT:-/tmp/litho-initramfs.cpio.gz}"
LOG="${LOG:-/tmp/litho-qemu-serial.log}"

if [ ! -f "$KERNEL" ]; then
    echo "ARM64 kernel Image not found at $KERNEL" >&2
    echo "Place a virt-capable Image there (Debian trixie netboot linux works)." >&2
    exit 1
fi
if [ ! -x "$INIT" ]; then
    echo "static litho-init missing: $INIT (run: make cross)" >&2
    exit 1
fi

python3 "$ROOT/board/qemu-virt/pack-initramfs.py" "$INIT" "$ROM" "$OUT"

set +e
timeout --signal=KILL "${QEMU_TIMEOUT:-90}" \
    qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a53 \
        -smp 4 \
        -m 512M \
        -kernel "$KERNEL" \
        -initrd "$OUT" \
        -append "console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init litho.serial litho.once" \
        -nographic \
        -no-reboot \
    2>&1 | tee "$LOG"
ec=${PIPESTATUS[0]}
set -e
echo "qemu exit $ec; serial log $LOG"
exit 0
