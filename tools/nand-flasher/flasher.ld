/* NAND flasher stub: runs entirely from SRAM, loaded via GDB */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x70000 /* 448KB: usable SRAM when ECC is enabled */
}

ENTRY(isr_reset)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM

    .data :
    {
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ :
    {
        *(.ARM.exidx*)
    }
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
