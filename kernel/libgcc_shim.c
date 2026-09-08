/* libgcc_shim.c — medany-compiled replacements for libgcc functions that
 * use medlow absolute relocations incompatible with RAM at 0x80000000+.
 *
 * Homebrew riscv64-elf-gcc ships libgcc.a compiled with medlow only.
 * Functions referencing __clz_tab use HI20 relocations that can't reach
 * addresses in the high 2 GB on RV64.  We provide our own medany-safe
 * versions so the linker uses these instead of pulling in libgcc's _clz.o.
 */

/* Count leading zeros — 32-bit */
int __clzsi2(unsigned int x) {
    if (x == 0) return 32;
    int n = 0;
    if (!(x & 0xFFFF0000U)) { n += 16; x <<= 16; }
    if (!(x & 0xFF000000U)) { n +=  8; x <<=  8; }
    if (!(x & 0xF0000000U)) { n +=  4; x <<=  4; }
    if (!(x & 0xC0000000U)) { n +=  2; x <<=  2; }
    if (!(x & 0x80000000U)) { n +=  1; }
    return n;
}

/* Count leading zeros — 64-bit */
int __clzdi2(unsigned long long x) {
    if (x == 0) return 64;
    int n = 0;
    if (!(x & 0xFFFFFFFF00000000ULL)) { n += 32; x <<= 32; }
    if (!(x & 0xFFFF000000000000ULL)) { n += 16; x <<= 16; }
    if (!(x & 0xFF00000000000000ULL)) { n +=  8; x <<=  8; }
    if (!(x & 0xF000000000000000ULL)) { n +=  4; x <<=  4; }
    if (!(x & 0xC000000000000000ULL)) { n +=  2; x <<=  2; }
    if (!(x & 0x8000000000000000ULL)) { n +=  1; }
    return n;
}
