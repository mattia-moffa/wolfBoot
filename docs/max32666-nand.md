# wolfBoot on MAX32666 with external SPI NAND

This configuration keeps the boot partition in the MAX32666's internal flash
and places the update and swap partitions on the external Micron
MT29F8G01ADBFD12 SPI NAND, which is accessed through the Shasta bad block
management layer. The linker scripts assume 448KB of SRAM, which is what
remains usable with SRAM ECC enabled.

## Memory layout

| Region         | Release           | DEBUG=1           |
|----------------|-------------------|-------------------|
| Bootloader     | 0x10000000 (32KB) | 0x10000000 (64KB) |
| Boot partition | 0x10008000        | 0x10010000        |
| Trailer pages  | 0x10048000 (2x8KB)| 0x10050000 (2x8KB)|

On the NAND, addresses are linear and the erase block (0x40000) is also
the wolfBoot sector size. Blocks 0 and 1 hold the bad block table, block 2
is the update partition (0x80000) and block 3 is the swap (0xC0000).

Because the wolfBoot sector size ends up being so large, partition state does
not live in a separate sector at the end of the partitions. Instead, this
configuration uses `CUSTOM_PARTITION_TRAILER` to keep it in a pair of internal
flash pages, written alternately.

## Building and flashing

```
cp config/examples/max32666-nand.config .config
make
./flash_update_nand_max32666.sh
```

`flash_update_nand_max32666.sh` signs the test app as version 2, writes it to
the NAND update partition, flashes `factory.bin` and leaves the target halted.
It writes the NAND by calling `flash_nand_max32666.sh`, which loads a small
stub into SRAM over the J-Link GDB server, which in turn uses the same NAND
driver stack wolfBoot uses. It also supports `--dump <addr>` to read the NAND
back and `--factory-reset` to erase the chip and rebuild the bad block table.

## Things to know

The synchronous (non-DMA) transfer path in `ZioHalSpi0.c` had a buffer overrun
that wolfBoot hits and the application firmware, which uses DMA, probably
doesn't. See `ZioHalSpiFixes.patch`.
