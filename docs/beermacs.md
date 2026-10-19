# beermacs

A small Emacs for beeros, in the spirit of `emacs -nw`: a package
(`packages/beermacs` → `/pkg/beermacs.tar`) that runs on whatever tty it
is started from — a `beer.wm` window, the full-screen terminal, or the
serial console.

```clojure
(beer.pkg/run "beermacs")                    ; *scratch*
(beer.pkg/run "beermacs" "/tmp/notes.beer")  ; or files
(beer.wm/open! {:cols 76 :rows 30})          ; a bigger window first helps
```

Files save anywhere: on the ext2 disk (kept across reboots; `make
run-gfx` doesn't rebuild an existing image) or under `/tmp` (RAM, gone at
reboot).

## Keys

| | |
|---|---|
| C-f C-b C-n C-p, arrows | move |
| C-a C-e, M-f M-b, M-< M->, C-v M-v | line, word, buffer, page |
| C-d DEL, C-k, M-d M-DEL | delete, kill line, kill word |
| C-SPC, C-w M-w, C-y | mark, kill / copy region, yank |
| C-/ (C-_, C-x u) | undo |
| C-s | incremental search (C-s next, DEL back, RET stop, C-g return) |
| RET, C-o, TAB | newline with indentation, open line, two spaces |
| C-x C-e, C-u C-x C-e, C-j, M-: | eval the form before point: value in the echo area / inserted at point / inserted on a new line; eval a typed expression |
| C-u | universal argument for the next command |
| C-x C-f, C-x C-s, C-x C-w | find file, save, write to another file |
| C-x b, C-x k, C-x C-b | switch to / kill / list buffers |
| C-h f, C-h a, C-h k | describe a name (default: the one at point), apropos, describe a key — into `*Help*` (RET on a name follows it, q goes back); same data as F1 / `beer.help` |
| C-l, C-g, C-x C-c | recenter, quit the current command, exit |

ESC works as Meta (ESC f = M-f), for when Alt doesn't reach the guest.
Ctrl-C is a key while beermacs runs (`beer.tty/ctrl-c-key!`); Ctrl-\
still interrupts it, and the terminal is restored on the way out.

## Inside

- `beermacs.buffer` — the text as data: a buffer is a map (`:lines`, a
  vector of strings; point `:row`/`:col`; `:mark`; `:undo`, a list of
  earlier states). Every function takes a buffer and returns the new one,
  so it can be tried at a REPL; undo is just keeping old values.
- `beermacs.core` — the display (only rows that changed are rewritten),
  the minibuffer, isearch, files, evaluation (output captured with a
  `with-out-str`), and the keymaps: atoms holding maps from key names to
  commands — vars (`#'forward-char`) of fns from editor state to editor
  state, so redefining a command with `defn` takes effect at once. Rebind
  from a REPL: `(swap! beermacs.core/global-map assoc "C-z" #'beermacs.core/undo)`.
  `@beermacs.core/last-state` holds the state at exit.

Not yet: windows (C-x 2), M-x, reverse search, syntax colours, lisp
indentation, query-replace, a *Messages* buffer.
