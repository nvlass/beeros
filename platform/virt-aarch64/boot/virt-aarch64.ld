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
        *(.sbss*)
        *(COMMON)
        *(SCOMMON)
        _bss_end = .;
    } > RAM

    . = ALIGN(16);
    _end        = .;
    __heap_start = .;

    /* 256 KiB stack at the top of RAM; heap fills the gap */
    _stack_top  = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end  = _stack_top - 0x40000;   /* leave 256 KiB for the stack */
}
