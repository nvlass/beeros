#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baud);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int  uart_getc_nonblock(char *c);  /* returns 1 if char ready, 0 if not */

/* Receive interrupt: not implemented here yet (see virt-riscv's uart.h) */
#define UART_IRQ 33
int  uart_rx_irq_enable(void);
void uart_rx_isr(void);

#endif
