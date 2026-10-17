/* console.h — serial console input: receive ring, interrupt keys */

#ifndef BEEROS_CONSOLE_H
#define BEEROS_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/* Switch UART receive to interrupts (falls back to polling if the board
 * has none). Call once the scheduler exists. */
void console_init(void);

/* Called by the board's UART interrupt handler for every received byte;
 * is_break marks a serial BREAK. Runs in trap context. */
void console_rx(uint8_t b, bool is_break);

/* Next received byte; 1 if there was one */
int console_getc_nonblock(char* c);

/* Set while the REPL reads a line: Ctrl-C is then an editing key, not an
 * interrupt */
void console_set_reading(bool reading);

/* beer.tty natives: uart-byte, ctrl-c-interrupts!, ctrl-c-interrupts? */
void console_register_natives(void);

#endif
