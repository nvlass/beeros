/* uart.c — NS16550A UART for qemu-system-riscv64 -M virt
 *
 * Base address: 0x10000000
 * Clock:        3,686,400 Hz (as specified in QEMU virt device tree)
 * Registers:    byte-wide at consecutive addresses
 */

#include "uart.h"

#define UART0_BASE  0x10000000UL
#define UART0_CLOCK 3686400U

#define RBR  (*(volatile uint8_t *)(UART0_BASE + 0))  /* recv / transmit */
#define IER  (*(volatile uint8_t *)(UART0_BASE + 1))  /* interrupt enable */
#define FCR  (*(volatile uint8_t *)(UART0_BASE + 2))  /* FIFO control */
#define LCR  (*(volatile uint8_t *)(UART0_BASE + 3))  /* line control */
#define MCR  (*(volatile uint8_t *)(UART0_BASE + 4))  /* modem control */
#define LSR  (*(volatile uint8_t *)(UART0_BASE + 5))  /* line status */
#define DLL  RBR                                        /* divisor LSB (DLAB=1) */
#define DLM  IER                                        /* divisor MSB (DLAB=1) */

#define LSR_DR   0x01  /* data ready */
#define LSR_THRE 0x20  /* TX holding register empty */

void uart_init(uint32_t baud) {
    uint32_t div = UART0_CLOCK / (16U * baud);
    LCR = 0x80;           /* DLAB=1 to access divisor */
    DLL = (uint8_t)(div & 0xFF);
    DLM = (uint8_t)(div >> 8);
    LCR = 0x03;           /* 8N1, DLAB=0 */
    FCR = 0xC7;           /* enable+clear FIFOs, 14-byte trigger */
    MCR = 0x0B;           /* RTS, DTR, OUT2 */
    IER = 0x00;           /* no interrupts (polling) */
}

void uart_putc(char c) {
    while (!(LSR & LSR_THRE));
    RBR = (uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

char uart_getc(void) {
    while (!(LSR & LSR_DR));
    return (char)RBR;
}

int uart_getc_nonblock(char *c) {
    if (LSR & LSR_DR) { *c = (char)RBR; return 1; }
    return 0;
}
