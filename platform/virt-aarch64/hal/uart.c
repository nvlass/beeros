/* uart.c — ARM PL011 UART for qemu-system-aarch64 -M virt
 *
 * Base address: 0x09000000
 * Clock:        24,000,000 Hz (QEMU virt device tree)
 */

#include "uart.h"

#define PL011_BASE  0x09000000UL
#define PL011_CLOCK 24000000U

#define DR    (*(volatile uint32_t *)(PL011_BASE + 0x000))  /* data */
#define FR    (*(volatile uint32_t *)(PL011_BASE + 0x018))  /* flags */
#define IBRD  (*(volatile uint32_t *)(PL011_BASE + 0x024))  /* integer baud */
#define FBRD  (*(volatile uint32_t *)(PL011_BASE + 0x028))  /* fractional baud */
#define LCRH  (*(volatile uint32_t *)(PL011_BASE + 0x02C))  /* line control */
#define CR    (*(volatile uint32_t *)(PL011_BASE + 0x030))  /* control */

#define FR_RXFE  0x10   /* RX FIFO empty */
#define FR_TXFF  0x20   /* TX FIFO full */
#define FR_BUSY  0x08   /* transmitting */

/* LCRH: FEN=1 (bit4), WLEN=11 (bits6:5 = 8-bit) */
#define LCRH_8N1_FIFO  0x70
/* CR: UARTEN(0) + TXE(8) + RXE(9) */
#define CR_ENABLE      0x0301

void uart_init(uint32_t baud) {
    /* Baud divisor * 64: d = 4 * CLOCK / baud (rounded) */
    uint32_t d = (4U * PL011_CLOCK + baud / 2U) / baud;
    CR   = 0;                   /* disable UART */
    while (FR & FR_BUSY);       /* drain */
    LCRH = 0;                   /* flush FIFOs */
    IBRD = d / 64;
    FBRD = d % 64;
    LCRH = LCRH_8N1_FIFO;
    CR   = CR_ENABLE;
}

void uart_putc(char c) {
    while (FR & FR_TXFF);
    DR = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

char uart_getc(void) {
    while (FR & FR_RXFE);
    return (char)(DR & 0xFF);
}

int uart_getc_nonblock(char *c) {
    if (!(FR & FR_RXFE)) { *c = (char)(DR & 0xFF); return 1; }
    return 0;
}
