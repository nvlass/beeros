/* gfx_ramfb.c — QEMU ramfb device init via fw-cfg DMA
 *
 * ramfb exposes a raw linear framebuffer backed by guest RAM. The only
 * hardware interaction is a one-time DMA write to the fw-cfg controller
 * at 0x10100000 that tells QEMU where the buffer lives and its geometry.
 *
 * After gfx_init_ramfb() returns, writing 32-bit XRGB8888 values to
 * FB_ADDR draws pixels on the QEMU display window.
 *
 * References:
 *   QEMU hw/display/ramfb.c
 *   QEMU docs/specs/fw_cfg.rst
 */

#include <stdint.h>
#include <string.h>
#include "gfx_ramfb.h"
#include "gfx.h"

/* ── fw-cfg MMIO registers (RISC-V MMIO variant) ─────────────────────── */
#define FW_CFG_BASE      0x10100000UL
#define FW_CFG_DATA      (*(volatile uint8_t  *)(FW_CFG_BASE + 0x00))
#define FW_CFG_SEL       (*(volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DMA_HI    (*(volatile uint32_t *)(FW_CFG_BASE + 0x10))
#define FW_CFG_DMA_LO    (*(volatile uint32_t *)(FW_CFG_BASE + 0x14))

/* fw-cfg selector for the "etc/ramfb" key.
 * 0x0019 = FW_CFG_FILE_DIR (directory listing).
 * 0x0059 is the typical selector for etc/ramfb when ramfb is the only
 * file-dir entry. We enumerate the directory to be safe. */
#define FW_CFG_FILE_DIR   0x0019

/* ── framebuffer parameters ───────────────────────────────────────────── */
#define FB_ADDR      0x84000000UL   /* 64 MB into RAM, after kernel */
#define FB_WIDTH     1280
#define FB_HEIGHT    720
#define FB_STRIDE    (FB_WIDTH * 4)

/* DRM_FORMAT_XRGB8888 = "XR24" in FourCC little-endian, 0x34325258 */
#define FOURCC_XRGB8888  0x34325258UL

/* ── big-endian helpers (all fw-cfg multi-byte fields are big-endian) ─── */
static uint16_t be16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static uint32_t be32(uint32_t v) {
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x000000FFu) << 24);
}
static uint64_t be64(uint64_t v) {
    return ((uint64_t)be32((uint32_t)(v >> 32))) |
           ((uint64_t)be32((uint32_t)(v & 0xFFFFFFFFu)) << 32);
}

/* ── fw-cfg file directory entry (64 bytes) ───────────────────────────── */
typedef struct {
    uint32_t size;       /* big-endian */
    uint16_t select;     /* big-endian — the selector to use */
    uint16_t reserved;
    char     name[56];
} __attribute__((packed)) FWCfgFile;

/* ── ramfb config struct (written to guest RAM, then DMA'd to QEMU) ───── */
typedef struct {
    uint64_t addr;    /* framebuffer guest physical address, BE */
    uint32_t fourcc;  /* pixel format FourCC, BE */
    uint32_t flags;   /* must be 0 */
    uint32_t width;   /* pixels, BE */
    uint32_t height;  /* pixels, BE */
    uint32_t stride;  /* bytes per row, BE */
} __attribute__((packed)) QemuRamfbConfig;

/* ── fw-cfg DMA access struct ─────────────────────────────────────────── */
typedef struct {
    uint32_t control;  /* (selector << 16) | op_flags, BE */
    uint32_t length;   /* bytes to transfer, BE */
    uint64_t address;  /* guest physical address of data, BE */
} __attribute__((packed)) FWCfgDmaAccess;

/* ── read one byte from fw-cfg data register (after selector is set) ──── */
static uint8_t fwcfg_read8(void) { return FW_CFG_DATA; }

/* ── find the selector for a named fw-cfg file ───────────────────────── */
static uint16_t fwcfg_find_file(const char* name) {
    /* Read the file directory */
    FW_CFG_SEL = be16(FW_CFG_FILE_DIR);

    /* First 4 bytes: number of entries (big-endian u32) */
    uint32_t count = 0;
    count |= (uint32_t)fwcfg_read8() << 24;
    count |= (uint32_t)fwcfg_read8() << 16;
    count |= (uint32_t)fwcfg_read8() <<  8;
    count |= (uint32_t)fwcfg_read8();

    for (uint32_t i = 0; i < count; i++) {
        FWCfgFile entry;
        uint8_t* p = (uint8_t*)&entry;
        for (size_t j = 0; j < sizeof(entry); j++) {
            p[j] = fwcfg_read8();
        }
        if (strncmp(entry.name, name, sizeof(entry.name)) == 0) {
            return be16(entry.select);
        }
    }
    return 0;   /* not found */
}

void gfx_init_ramfb(void) {
    uint16_t sel = fwcfg_find_file("etc/ramfb");
    if (sel == 0) return;   /* ramfb device not present */

    /* Config and DMA structs must be in RAM (not stack) for the DMA to work */
    static QemuRamfbConfig cfg;
    static FWCfgDmaAccess  dma;

    /* Fill the ramfb config — all fields big-endian */
    cfg.addr   = be64((uint64_t)FB_ADDR);
    cfg.fourcc = be32(FOURCC_XRGB8888);
    cfg.flags  = 0;
    cfg.width  = be32(FB_WIDTH);
    cfg.height = be32(FB_HEIGHT);
    cfg.stride = be32(FB_STRIDE);

    /* DMA control word: bits[31:16]=selector, bit4=SELECT, bit3=WRITE */
    dma.control = be32(((uint32_t)sel << 16) | 0x18u);
    dma.length  = be32(sizeof(cfg));
    dma.address = be64((uint64_t)(uintptr_t)&cfg);

    /* Trigger DMA: write 64-bit GPA of dma struct to FW_CFG_DMA_ADDR.
     * RISC-V is little-endian; fw-cfg expects high word first. */
    uint64_t dma_gpa = (uint64_t)(uintptr_t)&dma;
    FW_CFG_DMA_HI = (uint32_t)(dma_gpa >> 32);
    FW_CFG_DMA_LO = (uint32_t)(dma_gpa & 0xFFFFFFFFu);

    /* QEMU processes the DMA synchronously; by the time the store completes
     * the ramfb is configured. Clear the framebuffer to black. */
    volatile uint32_t* fb = (volatile uint32_t*)FB_ADDR;
    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++) fb[i] = 0;

    gfx_init(FB_WIDTH, FB_HEIGHT, FB_ADDR);
}
