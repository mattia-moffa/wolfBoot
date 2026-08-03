#!/bin/bash
# Stages an update to the NAND board (without triggering it). To trigger, run
# wolfBoot_update_trigger() from the application.

set -e

JLINK_DEVICE="${JLINK_DEVICE:-MAX32666}"
JLINK_SPEED="${JLINK_SPEED:-4000}"

tools/keytools/sign --ecc256 --sha256 test-app/image.bin \
    wolfboot_signing_private_key.der 2
cp test-app/image_v2_signed.bin update.bin

./flash_nand_max32666.sh update.bin 0x80000

CMDFILE="$(mktemp)"
trap 'rm -f "$CMDFILE"' EXIT
cat > "$CMDFILE" <<EOF
r
h
erase
loadbin factory.bin,0x10000000
verifybin factory.bin,0x10000000
r
h
q
EOF

JLinkExe -device "$JLINK_DEVICE" -if SWD -speed "$JLINK_SPEED" \
    -autoconnect 1 -CommandFile "$CMDFILE"
