#!/bin/bash
# Write a file to the external SPI NAND through a J-Link, using the
# tools/nand-flasher stub (goes through the Zio bad block layer).
#
# Usage: ./flash_nand_max32666.sh <file> <nand-address> [--no-verify]
#
# --factory-reset erases the whole NAND (including the bad
# block table, which is rebuilt empty) before programming.

set -e

DUMP_ADDR=""
if [ "$1" = "--dump" ]; then
    DUMP_ADDR="$2"
    if [ -z "$DUMP_ADDR" ]; then
        echo "Usage: $0 --dump <nand-address>" >&2
        exit 1
    fi
else
    FILE="$1"
    ADDR="$2"
    VERIFY=1
    FACTORY_RESET=0
    for arg in "${@:3}"; do
        case "$arg" in
            --no-verify) VERIFY=0 ;;
            --factory-reset) FACTORY_RESET=1 ;;
            *) echo "unknown option: $arg" >&2; exit 1 ;;
        esac
    done

    if [ -z "$FILE" ] || [ -z "$ADDR" ]; then
        echo "Usage: $0 <file> <nand-address> [--no-verify] [--factory-reset]" >&2
        exit 1
    fi
    if [ ! -f "$FILE" ]; then
        echo "$FILE: no such file" >&2
        exit 1
    fi
fi

JLINK_DEVICE="${JLINK_DEVICE:-MAX32666}"
JLINK_SPEED="${JLINK_SPEED:-4000}"
JLINK_GDB_PORT="${JLINK_GDB_PORT:-2331}"
GDB="${GDB:-arm-none-eabi-gdb}"
DIR="$(dirname "$0")/tools/nand-flasher"

make -C "$DIR" --quiet

JLinkGDBServerCLExe -device "$JLINK_DEVICE" -if SWD -speed "$JLINK_SPEED" \
    -port "$JLINK_GDB_PORT" -silent &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT

for _ in $(seq 50); do
    ss -ltn 2>/dev/null | grep -q ":$JLINK_GDB_PORT " && break
    sleep 0.1
done

NAND_FILE="$FILE" NAND_ADDR="$ADDR" NAND_VERIFY="$VERIFY" \
    NAND_FACTORY_RESET="$FACTORY_RESET" NAND_DUMP="$DUMP_ADDR" \
    JLINK_GDB_PORT="$JLINK_GDB_PORT" \
    "$GDB" -batch -nx "$DIR/nand_flasher.elf" -x "$DIR/flasher_gdb.py"
