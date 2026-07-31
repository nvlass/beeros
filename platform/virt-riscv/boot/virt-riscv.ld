OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 256M
}

SECTIONS {
    . = 0x80000000;

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

    /* TLS initialized data (picolibc uses thread_local errno etc.) */
    .tdata : ALIGN(8) {
        __tdata_start = .;
        *(.tdata .tdata.*)
        . = ALIGN(8);
        __tdata_end = .;
    } > RAM

    /* TLS zero-initialized data — cleared by start.S alongside .bss */
    .tbss (NOLOAD) : ALIGN(8) {
        __tbss_start = .;
        *(.tbss .tbss.*)
        *(TCOMMON)
        . = ALIGN(8);
        __tbss_end = .;
    } > RAM

    /* tp (thread pointer) points to the start of the TLS block */
    __tls_base = __tdata_start;

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
