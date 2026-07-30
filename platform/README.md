# platform/

Board-specific code lives here, one subdirectory per board.

## Directory layout

```
platform/
└── <board-name>/
    ├── boot/
    │   ├── start.S       ← RISC-V entry: set sp, clear BSS, call beeros_main()
    │   └── <board>.ld    ← linker script (DDR base address, stack, heap regions)
    └── hal/
        ├── uart.h        ← HAL interface (same for every board)
        ├── uart.c        ← board-specific UART implementation
        ├── timer.h       ← HAL interface
        └── timer.c       ← board-specific timer (wraps rdtime + freq)
```

## HAL contract

Every board HAL must implement these two headers exactly:

### uart.h

```c
void uart_init(uint32_t baud);
void uart_putc(char c);
void uart_puts(const char* s);
char uart_getc(void);            /* blocking */
int  uart_getc_nonblock(char*);  /* returns 1 if char ready, 0 if not */
```

### timer.h

```c
uint64_t timer_read_us(void);    /* microseconds since boot (uses rdtime) */
```

`timer_read_us` is used by `kernel/clock_shim.c` to implement `clock_gettime(CLOCK_MONOTONIC)`
for the beerlang scheduler's sleep/timer subsystem.

## Board candidates

| Board  | Chip                                 | Status   |
|--------|--------------------------------------|----------|
| mq-pro | Allwinner D1 (XuanTie C906, RV64GCV) | Sourcing |
| (TBD)  |                                      |          |
