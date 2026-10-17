# Console: ttys, line editor, terminal emulator

Status: tty layer, line editor, interrupt keys and a framebuffer
terminal with its own REPL implemented (`lib/tty.beer`,
`lib/lineedit.beer`, `kernel/console.c`, `/lib/beer/term.beer`,
`/lib/beer/font.beer`). PSF fonts load; none ships on the disk yet.

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

### Interrupt keys

UART input arrives by interrupt into a 256-byte ring (`kernel/console.c`;
`beer.tty/uart-byte` reads it). The receive handler watches for the
interrupt keys, so they work while a form hogs the CPU:

| Key | |
|---|---|
| `Ctrl-\`, serial BREAK, Ctrl-Alt-Esc (keyboard) | always interrupt the running form |
| `Ctrl-C` | interrupts while a form runs and `(beer.tty/ctrl-c-interrupts?)`; an editing key at the prompt |

An interrupt drops the bytes typed ahead; beerlang unwinds the REPL's form
(`finally` runs, `catch` doesn't) and the REPL calls `beer.tty/recover!`.
At the prompt `Ctrl-\` drops the line being edited. Programs that use
`C-c` as a key call `(beer.tty/ctrl-c-interrupts! false)`. Details in
`docs/packages.md` ("Emergency drop to REPL").

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
search, persistent history (needs ext2 writes: `/var/history`). Slow on
long lines: every key redraws the whole line (fine when typing, but a
pasted 100-character line takes about a second); redrawing only from the
cursor would fix it.

## Terminal emulator (`beer.term`)

`/lib/beer/term.beer` on the disk, so it can be edited and reloaded like
any namespace. A term is an atom: a grid of cells `[char fg bg]`, the
cursor, SGR state and the parser state, drawn into a rectangle of the
framebuffer with a `beer.font` font (default: the builtin 8x8 at scale 2,
80x45 on the 1280x720 ramfb).

```clojure
(def t (beer.term/make {:x 0 :y 0 :cols 80 :rows 20 :scale 2}))
(beer.term/start! t)                 ; renderer task
(beer.term/write! t "\r\nhello")    ; from any task
(beer.term/tty t)                    ; a beer.tty map: keys from beer.input
(beer.term/start-repl!)              ; full screen + REPL (what init does)
```

Understood: printable text with deferred wrap and scrolling; CR LF BS TAB;
`ESC 7/8/c/D/M/E`; CSI `A B C D E F G H f d J K S T s u m` (bold,
inverse, 16/256/truecolour), `?25h/l`. Not yet: scroll regions,
insert/delete line/char, alternate screen.

Only the renderer task draws. Text reaches it two ways: `write!` queues
it, and `attach-output!` adds a capture slot (below) that it drains.
Scrolling is one `beer.gfx/copy-rect!`, glyphs are `beer.gfx/blit-bits!`.

### Output capture

`(beer.tty/capture-output!)` makes everything the calling task writes to
stdout/stderr (`print`, `println`, error messages) go into a buffer
instead of the UART (`kernel/console.c`, keyed by task); another task
reads it with `(take-output slot)`, `(release-output! slot)` ends it. This
is how a REPL's output lands on its terminal without beerlang having
per-task `*out*`. `eval` runs its form in a task of its own, so the term
REPL compiles each form to a fn and calls it in its own task. Tasks it
`spawn`s print to the UART.

### Two REPLs

`/etc/init.beer` starts a term REPL when there is a framebuffer and
`:console {:term {...}}` is in `/etc/beeros.edn` (`false`: none). It runs
as a background task next to the serial REPL and takes the keyboard
(`beer.tty/use-keyboard?` goes false); serial keeps the UART. Each has its
own line editor state (history and kill ring are shared). Not separate
yet, until beerlang has them (its TODO.md, "beeros requests" 8):

- the current namespace: `(in-ns 'foo)` in one moves the other;
- interrupts: `Ctrl-\` and friends stop the serial REPL's form; a
  runaway form in the term REPL can't be stopped;
- `beer.tty/current` is the serial tty, so a program run from the term
  that reads keys through it reads serial.

## Fonts and config on disk (planned)

- **Fonts** (`/lib/beer/font.beer`, done): a font is a map `{:width
  :height :stride :bytes :base :glyph-size :count :unicode}` over a
  ByteBuffer; `(beer.font/load-psf path)` reads PSF1 and PSF2 (Linux
  console fonts, many exist, e.g. Terminus, OFL), with their Unicode
  tables; `(beer.font/builtin)` wraps the 8x8 font of `lib/gfx.beer`.
  `draw!`/`draw-glyph!` on `beer.gfx/blit-bits!`. To do: ship a PSF in
  `/share/fonts` and pick it in `/etc/beeros.edn`.
- **Config: EDN** in `/etc/beeros.edn` (`read-string` of the file), e.g.
  `{:console {:font "/share/fonts/ter-v16n.psf" :tty :fb} :keyboard
  {:layout :us}}`, read at boot.
- **Boot script**: `/etc/init.beer` (done; see `docs/filesystem.md`) —
  where the editor will eventually start.
- **Layout** (done, `make disk` from `rootfs/`): `/lib` (namespaces for
  `require`), `/etc` (config, init), `/share/fonts`, `/bin` (runnable
  scripts), `/var` (history, logs; needs writes).
