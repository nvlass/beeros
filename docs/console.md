# Console: ttys, line editor, terminal emulator

Status: tty layer and line editor implemented (`lib/tty.beer`,
`lib/lineedit.beer`), the REPL reads through them. Terminal emulator,
fonts on disk and config: planned (below).

```
 keystrokes ──► beer.lineedit ──► ANSI text
     ▲          (REPL input)          │
     │                                ▼
 serial tty: UART bytes (VT100)   serial tty: UART  ──► your terminal
             + beer.input keys
 fb tty:     beer.input keys      fb tty: beer.term ──► framebuffer
                                  (VT100 emulator + PSF font)
```

## ttys (`beer.tty`)

A tty is `{:name :read-key :write!}`. `:read-key` yields until there is a
keystroke and returns one of

```clojure
{:char "λ"}                      ; text, any UTF-8 character
{:key :left :mods #{}}           ; :enter :tab :backspace :delete :home :end :up ... :f12
{:key :a :mods #{:ctrl}}         ; control / alt chords (alt-x arrives as ESC x on serial)
```

`:write!` takes text with ANSI/ECMA-48 control sequences. `beer.tty/current`
(an atom) is the console; `read-key` / `write!` go to it.

The serial tty decodes VT100/xterm input: CSI and SS3 sequences, xterm
modifier parameters (`ESC [1;5C` = C-Right), lone ESC by a 50 ms timeout,
UTF-8, CR/LF/CRLF as one Enter. With `use-keyboard?` on (default) it also
takes keystrokes from `beer.input` keyboards, so typing in the QEMU window
reaches the REPL.

## Line editor (`beer.lineedit`)

Emacs-style editing on one line at a time, multi-line forms by
continuation prompts:

| Keys | |
|---|---|
| C-a/Home, C-e/End, C-b/Left, C-f/Right | move |
| M-b/C-Left, M-f/C-Right | move by word (Lisp-aware: brackets and quotes break words) |
| Backspace/C-h, Delete/C-d | delete |
| C-k, C-u, C-w, M-d, C-y | kill to end / start / word back / word forward, yank |
| Up/C-p, Down/C-n | history (500 entries, consecutive duplicates dropped) |
| Tab | complete a symbol from the current ns, `beer.core` and special forms; `ns/prefix` completes in that ns; Tab again lists candidates |
| C-l | clear screen |
| C-c | cancel the whole form |
| Enter | submit if brackets balance (strings, comments, `\(` char literals understood), else continue on the next line |

The opener matching a closer just before the cursor is shown inverted.

The REPL (`kernel/repl_uart.c`) runs `(beer.lineedit/read-form
beer.lineedit/prompt)` for every input. If that throws, it prints the
error, sets `beer.lineedit/enabled?` to false and falls back to its
built-in reader, so a broken edit at the REPL can't lock you out;
`(beer.lineedit/enable!)` turns the editor back on.

Escape sequences the editor emits (all a terminal emulator needs for it):
CR, `CSI K` (erase to end of line), `CSI n D` (cursor left), `CSI 7m` /
`CSI 0m` (inverse on/off), `CSI H` + `CSI 2J` (clear).

Not yet: horizontal scrolling for lines wider than the terminal, C-r
search, persistent history (needs ext2 writes: `/var/history`).

## Next: terminal emulator (`beer.term`, planned)

A VT100/xterm-subset emulator drawing into the framebuffer, providing the
same tty map (`:write!` parses ANSI, `:read-key` turns `beer.input`
events into keystrokes). Being a standard terminal means anything written
for the serial console (the line editor, a future editor, `less`-style
pagers) works on screen unchanged.

- State: a grid of cells `{:ch :fg :bg :attrs}`, cursor, scroll region,
  saved cursor; parser states ground / ESC / CSI.
- First subset: printable text with wrap and scroll, CR/LF/BS/TAB, CUU/
  CUD/CUF/CUB/CUP, ED/EL, SGR (bold, inverse, 16 + 256 colours), DECSC/
  DECRC, cursor show/hide. Later: scroll regions, insert/delete line,
  alternate screen (for full-screen apps like the editor).
- Rendering: redraw only dirty cells, glyphs from a font in memory.
- Serial stays as a second console (or mirrored).

## Fonts and config on disk (planned)

- **Fonts: PSF2** (Linux console font format): a 32-byte header then
  fixed-size bitmaps, optional Unicode table — a few dozen lines of
  beerlang to load, and many fonts exist (e.g. Terminus, OFL-licensed).
  The 8×8 font now embedded in `lib/gfx.beer` moves to
  `/share/fonts/beer8x8.psf`; `beer.gfx/text!` takes a font. No font
  manager: a font is a map `{:width :height :glyphs :unicode}` and
  whoever draws picks one.
- **Config: EDN** in `/etc/beeros.edn` (`read-string` of the file), e.g.
  `{:console {:font "/share/fonts/ter-v16n.psf" :tty :fb} :keyboard
  {:layout :us}}`, read at boot.
- **Boot script**: `/etc/init.beer` (done; see `docs/filesystem.md`) —
  where the editor will eventually start.
- **Layout** (done, `make disk` from `rootfs/`): `/lib` (namespaces for
  `require`), `/etc` (config, init), `/share/fonts`, `/bin` (runnable
  scripts), `/var` (history, logs; needs writes).
