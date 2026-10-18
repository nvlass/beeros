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

    /* A NOBITS TLS section takes no address space (each thread would get
     * its own copy), but tp points at this one copy: reserve it, or .bss
     * can start on top of it and errno writes land in .bss variables
     * (as picolibc's own picolibc.ld does) */
    .tbss_space (NOLOAD) : {
        . = . + SIZEOF(.tbss);
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

    /* From the top of RAM: 256 KiB stack, then 4 MiB for the ramfb
     * framebuffer (1280x720x4); the heap fills the gap below them */
    _stack_top  = ORIGIN(RAM) + LENGTH(RAM);
    __fb_start  = _stack_top - 0x40000 - 0x400000;
    __heap_end  = __fb_start;
}
