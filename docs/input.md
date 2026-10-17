# Input devices

Status: interface and the virtio-input backend implemented
(`lib/input.beer`, `lib/virtio_input.beer`, on the shared virtio transport
`lib/virtio.beer`). Tested under QEMU with keyboard, mouse and tablet.

Same idea as `docs/block-device.md`: consumers (REPL line editor, the
editor, Doom) only see input **events**. Each backend is independent and
turns its hardware into those events, so a board can have any mix of them.

## Events

Plain maps, emitted in order:

```clojure
{:dev "kbd0"   :type :key    :key :a     :down? true  :repeat? false :mods #{:shift}}
{:dev "mouse0" :type :button :button :left :down? true}
{:dev "mouse0" :type :motion :dx 3 :dy -1}            ; relative, summed per report
{:dev "mouse0" :type :wheel  :delta -1}
{:dev "tablet0" :type :abs   :x 0.25 :y 0.5}          ; 0.0-1.0 of the screen
```

`:key` is a keyword for the physical key (`:a`, `:1`, `:enter`, `:left-shift`,
`:f1`, ...), independent of layout. `:mods` is the set of modifiers
(`:shift :ctrl :alt :meta`) held when the key moved, so an event read later
still knows it was shifted. `(beer.input/key->char ev)` turns a key-down
into a one-character string using `:mods` and `beer.input/layout` (US by
default; `nil` for keys that don't type). Unknown keys come through as
`{:key :unknown :code n}`.

## `beer.input`

```clojure
(beer.input/poll-event)        ; next event or nil, never blocks (game loops)
(beer.input/wait-event)        ; next event, yields to other tasks until one arrives
(beer.input/down? :left-ctrl)  ; is the key/button held right now
(beer.input/devices)           ; {"kbd0" {...} "mouse0" {...}}
(beer.input/emit! ev)          ; what backends call
```

Events go into a bounded queue (oldest dropped when full) rather than a
channel: beerlang channels have no non-blocking put, and a backend must
never block because nobody is reading. `down?` comes from a held-keys set
updated on every key/button event, so a game can poll state instead of
consuming events.

Backends run as tasks (`spawn`) that poll their device and `yield`; the
REPL yields while waiting for UART input, so they keep running at the
prompt. Interrupt-driven delivery (`irq-wait!`) can replace the polling
later without changing the interface.

## Backends

| Backend | Where | Notes |
|---|---|---|
| **virtio-input** | QEMU | `virtio-keyboard-device`, `virtio-mouse-device`, `virtio-tablet-device`. Linux evdev codes over a virtqueue; reuses the virtio-mmio code from virtio-blk |
| **UART keyboard** | the ZX LISP Machine | a Pico (or other MCU) scans the 80-key matrix and sends events over a serial line; trackball/trackpoint on the same link |
| **USB HID** | generic boards | needs a USB host controller driver (xHCI/EHCI/DWC2) and a HID boot-protocol parser first; the big one |

### UART keyboard protocol (draft)

Kept trivial so the beeros side is a few lines of beerlang on `beer.mem`:

- Key: one byte, `0x00-0x7F` = key number pressed, `0x80 | n` = released.
  Key numbers are the matrix position; a table in beerlang maps them to
  key keywords, so remapping is a REPL edit, not a firmware flash.
- Pointer: `0xF0 dx dy buttons` (dx/dy signed bytes), wheel `0xF1 delta`.
- Other `0xF2-0xFF`: reserved (LEDs, config, firmware version).

The MCU firmware can be custom (matrix scan + UART write is short) or a
QMK/RMK build with a serial output. The same MCU can still enumerate as a
normal USB keyboard when plugged into a PC.

## QEMU

`make run-gfx` attaches a virtio keyboard and mouse to the display window.
Without a display, drive them from the QEMU monitor (`sendkey shift-a`,
`mouse_move 10 -5`, `mouse_button 1`) or QMP (`input-send-event`, needed
for tablet positions). `scripts/qemu-drive.py` scripts both alongside REPL
input. Devices are named by kind in slot order: `kbd0`, `mouse0`,
`tablet0` (a tablet also reports a rel wheel, so abs is checked first).

Polling tasks cost nothing measurable: a 200k-iteration loop and a
400 KB ext2 read time the same with and without keyboard + mouse attached.
