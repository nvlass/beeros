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

`:write!` takes text with ANSI/ECMA-48 control sequences.
`beer.tty/*tty*` (dynamic; the serial tty unless a REPL binds it) is the
running task's console; `read-key` / `write!` go to it.

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
CR, CR LF, `CSI n A` / `CSI n C` (cursor up / right), `CSI J` (erase
below), `CSI 7m` / `CSI 0m` (inverse on/off), `CSI H` + `CSI 2J` (clear).

Lines wider than the tty wrap over several rows: each redraw goes up to
the prompt's row (`CSI n A`), rewrites, and erases below (`CSI J`). The
width is the tty's `:cols` (the term knows it; serial assumes
`@beer.tty/serial-cols`, 80, since we don't ask the terminal yet).

Not yet: C-r search, persistent history (needs ext2 writes:
`/var/history`). Every key redraws the whole line; redrawing only from
the cursor would make long lines cheaper (the term draws ~8 chars/ms).

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
it, and `attach-output!` adds a capture stream's slot (below) that it
drains. The term atom belongs to the renderer (it draws while updating
it, so no `swap!`, which may retry); other tasks touch only `(:in @t)`.
Scrolling is one `beer.gfx/copy-rect!`, glyphs are `beer.gfx/blit-bits!`.

### Capture streams

`(beer.tty/capture-stream)` returns `[stream slot]`: an output stream
whose bytes go into a buffer (`kernel/console.c`; its fd is one only
beeros's `write()` knows) instead of the UART. `(take-output slot)`
drains it, `(release-output! slot)` frees it. The term REPL binds `*out*`
and `*err*` to one and the renderer drains it, so whatever the REPL, its
forms and the tasks they `spawn` print (bindings are inherited) lands on
the terminal.

### Two REPLs

`/etc/init.beer` starts a term REPL when there is a framebuffer and
`:console {:term {...}}` is in `/etc/beeros.edn` (`false`: none). It runs
as a background task next to the serial REPL and is independent of it:

- it runs inside `(binding [*ns* 'user *out* s *err* s beer.tty/*tty*
  term-tty] ...)`, so `in-ns` there moves only its own namespace, and a
  program run from it that reads keys or writes through `beer.tty` uses
  the terminal;
- it takes the keyboard (`beer.tty/use-keyboard?` goes false; serial
  keeps the UART) and its interrupt keys (`beer.tty/keyboard-interrupt`):
  `Ctrl-\` or Ctrl-Alt-Esc stop its form with `(interrupt! repl-task)`,
  Ctrl-C does while a form runs (at the prompt it cancels the line). Its
  loop survives with `(catch :interrupt e ...)`. The serial keys still
  stop the serial REPL's form;
- `eval` runs in the calling task (beerlang since f0a1c8b), so two REPLs
  evaluating at once don't stall each other.

Shared on purpose: every definition, and the line editor's history and
kill ring.

## Window manager (`beer.wm`)

`/lib/beer/wm.beer`, minimal in the spirit of wm2: overlapping windows,
each a REPL on a `beer.term`, a title tab with a close box. `/etc/init.beer`
starts it when `/etc/beeros.edn` has `:console {:wm {...}}` (the default;
`:cols :rows :scale` for new windows), with one window open.

| | |
|---|---|
| click the desktop | new REPL window there |
| click a window | raise it, give it the keyboard |
| drag the tab | move |
| click the box in the tab | close |
| Ctrl-Alt-Enter / Ctrl-Alt-Tab / Ctrl-Alt-W | new / next / close (for when QEMU grabs the mouse) |
| F1 / Ctrl-Alt-H | the help window (`beer.help/browse`): namespaces, names, docstrings, docs pages |
| Ctrl-\, Ctrl-Alt-Esc, Ctrl-C | interrupt the focused window's REPL (as in "Two REPLs") |

From a REPL: `(beer.wm/open! {:x 100 :y 80 :cols 60})` (`:run f` runs f in
the window instead of a REPL; it closes when f returns), `(beer.wm/close!)`,
`(beer.wm/focus! id)`, `@beer.wm/state`.

**Canvases (games, drawing programs).** `(beer.wm/canvas! {:w 320 :h 200
:scale 3 :title "x"})` opens a window that is just a surface: draw on
`(:surface c)` with the `beer.gfx` `*-on!` calls (`fill-rect-on!`,
`blit-pixels!` for colour images, `blit-bits-on!` for 1-bit glyphs),
then `(beer.wm/show! c)` copies it, scaled, to the screen. `(:events c)`
is a channel: key events (down and up), `{:type :button :button :down?
:x :y}`, `{:type :pointer :x :y}`, `{:type :wheel ...}` in canvas
coordinates while it has the focus, and `{:type :close}` when the WM
closes it; finish with `(beer.wm/close-canvas! c)`. `:fullscreen true`
drops the frame and fills the screen (scale chosen to fit; Ctrl-Alt-Tab
still switches away). Ctrl-\\ interrupts the task that made the canvas.
Images: `beer.image` (`/lib/beer/image.beer`) loads 24/32-bit BMP into
`{:w :h :pixels :stride}` and `draw!`s them with a colour key. The
`bounce` package is a small example (`(beer.pkg/start "bounce")`).

**Compositing.** Each window draws into its own off-screen surface
(`beer.gfx/surface`, `fill-rect-on!` / `blit-bits-on!` / `copy-rect-on!`;
surface 0 is the screen). An owner map in C says which surface each screen
pixel shows: `restack!` sets it back to front (`own!`) and repaints, and
`present!` copies a surface to the screen only where it owns the pixels.
So a terminal's renderer can draw and present at any time without
painting over the window above it, and scrolling is a copy inside the
window's own surface. The pointer is a software cursor (`cursor!`) that
every screen write hides and redraws. A `beer.term` takes `:surface`,
`:present` (called after each batch it drew) and `:keys` (a channel the
WM feeds); without them it draws on the screen and reads beer.input.

**Input.** The WM task is the only reader of `beer.input/queue`: pointer
motion (`:motion` from a mouse, `:abs` from a tablet) moves the cursor,
buttons click and drag, key events go to the focused window's channel.
`beer.tty/keyboard-interrupt` sends the interrupt keys to the focused
window (`beer.term/interrupt-key!`).

Not yet: resizing, a root menu (wm2's hidden-window list), a title from
the window's namespace, moving windows partly off screen.

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
