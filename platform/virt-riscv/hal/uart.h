#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baud);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int  uart_getc_nonblock(char *c);  /* returns 1 if char ready, 0 if not */

/* Optional: receive by interrupt. uart_rx_irq_enable() turns it on and
 * returns 1 (0 if the board can't); from then on uart_rx_isr(), called by
 * the trap handler for UART_IRQ, hands every byte to console_rx()
 * (kernel/console.c) and uart_getc_nonblock must not be used. */
#define UART_IRQ 10
int  uart_rx_irq_enable(void);
void uart_rx_isr(void);

#endif
