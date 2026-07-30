OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* QEMU -M virt AArch64: RAM base 0x40000000 */
MEMORY {
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 256M
}

SECTIONS {
    . = 0x40000000;

    .text : {
        *(.text.boot)
        *(.text*)
    } > RAM

    .rodata : ALIGN(4) {
        *(.rodata*)
    } > RAM

    .data : ALIGN(4) {
        *(.data*)
    } > RAM

    .bss : ALIGN(8) {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    . = ALIGN(16);
    _end = .;          /* picolibc _sbrk heap starts here */

    /* 256 KiB stack at the top of RAM */
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
